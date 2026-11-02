#include "dbus_mpris.hpp"
#include "common.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <curl/curl.h>
#include <glib.h>
#include <iostream>

namespace p5 {

namespace {

size_t curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto* buf = static_cast<std::vector<uint8_t>*>(userp);
    const auto* ptr = static_cast<const uint8_t*>(contents);
    buf->insert(buf->end(), ptr, ptr + realsize);
    return realsize;
}

std::string decode_file_uri(std::string_view uri) {
    if (uri.starts_with("file://")) {
        uri.remove_prefix(7);
    }
    std::string out;
    out.reserve(uri.size());
    for (size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            auto hex_val = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex_val(uri[i + 1]);
            int lo = hex_val(uri[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(uri[i]);
    }
    return out;
}

std::string read_string_or_string_array(sd_bus_message* m, const char* vtype) {
    std::string result;
    if (!vtype) return result;
    if (std::strcmp(vtype, "s") == 0) {
        if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s") >= 0) {
            const char* val = nullptr;
            if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &val) >= 0 && val) {
                result = val;
            }
            sd_bus_message_exit_container(m);
        }
    } else if (std::strcmp(vtype, "as") == 0) {
        if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "as") >= 0) {
            if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "s") >= 0) {
                const char* a = nullptr;
                while (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &a) > 0) {
                    if (a && *a) {
                        if (!result.empty()) result += ", ";
                        result += a;
                    }
                }
                sd_bus_message_exit_container(m);
            }
            sd_bus_message_exit_container(m);
        }
    } else {
        sd_bus_message_skip(m, "v");
    }
    return result;
}

double read_numeric_variant_us_to_sec(sd_bus_message* m, const char* vtype) {
    double sec = 0.0;
    if (!vtype) {
        sd_bus_message_skip(m, "v");
        return 0.0;
    }
    if (std::strcmp(vtype, "x") == 0) {
        if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "x") >= 0) {
            int64_t us = 0;
            if (sd_bus_message_read_basic(m, SD_BUS_TYPE_INT64, &us) >= 0 && us > 0) {
                sec = static_cast<double>(us) / 1e6;
            }
            sd_bus_message_exit_container(m);
        }
    } else if (std::strcmp(vtype, "t") == 0) {
        if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "t") >= 0) {
            uint64_t us = 0;
            if (sd_bus_message_read_basic(m, SD_BUS_TYPE_UINT64, &us) >= 0 && us > 0) {
                sec = static_cast<double>(us) / 1e6;
            }
            sd_bus_message_exit_container(m);
        }
    } else if (std::strcmp(vtype, "i") == 0) {
        if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "i") >= 0) {
            int32_t us = 0;
            if (sd_bus_message_read_basic(m, SD_BUS_TYPE_INT32, &us) >= 0 && us > 0) {
                sec = static_cast<double>(us) / 1e6;
            }
            sd_bus_message_exit_container(m);
        }
    } else if (std::strcmp(vtype, "u") == 0) {
        if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "u") >= 0) {
            uint32_t us = 0;
            if (sd_bus_message_read_basic(m, SD_BUS_TYPE_UINT32, &us) >= 0 && us > 0) {
                sec = static_cast<double>(us) / 1e6;
            }
            sd_bus_message_exit_container(m);
        }
    } else if (std::strcmp(vtype, "d") == 0) {
        if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "d") >= 0) {
            double val = 0.0;
            if (sd_bus_message_read_basic(m, SD_BUS_TYPE_DOUBLE, &val) >= 0 && val > 0.0) {
                sec = (val > 100000.0) ? (val / 1e6) : val;
            }
            sd_bus_message_exit_container(m);
        }
    } else {
        sd_bus_message_skip(m, "v");
    }
    return sec;
}

void parse_metadata_array(sd_bus_message* m, MprisState& st) {
    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}") < 0) {
        return;
    }

    std::string album_artist;
    std::string album;

    while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        const char* key = nullptr;
        if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &key) >= 0 && key) {
            const char* vtype = nullptr;
            if (sd_bus_message_peek_type(m, nullptr, &vtype) >= 0 && vtype) {
                if (std::strcmp(key, "xesam:title") == 0) {
                    std::string s = read_string_or_string_array(m, vtype);
                    if (!s.empty()) st.track_title = std::move(s);
                } else if (std::strcmp(key, "xesam:artist") == 0) {
                    std::string s = read_string_or_string_array(m, vtype);
                    if (!s.empty()) st.track_artist = std::move(s);
                } else if (std::strcmp(key, "xesam:albumArtist") == 0) {
                    std::string s = read_string_or_string_array(m, vtype);
                    if (!s.empty()) album_artist = std::move(s);
                } else if (std::strcmp(key, "xesam:album") == 0) {
                    std::string s = read_string_or_string_array(m, vtype);
                    if (!s.empty()) album = std::move(s);
                } else if (std::strcmp(key, "mpris:artUrl") == 0 && std::strcmp(vtype, "s") == 0) {
                    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s") >= 0) {
                        const char* val = nullptr;
                        if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &val) >= 0 && val) {
                            st.art_url = val;
                        }
                        sd_bus_message_exit_container(m);
                    }
                } else if (std::strcmp(key, "mpris:trackid") == 0 &&
                           (std::strcmp(vtype, "o") == 0 || std::strcmp(vtype, "s") == 0)) {
                    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, vtype) >= 0) {
                        const char* val = nullptr;
                        char basic_t = vtype[0];
                        if (sd_bus_message_read_basic(m, basic_t, &val) >= 0 && val) {
                            st.track_id = val;
                        }
                        sd_bus_message_exit_container(m);
                    }
                } else if (std::strcmp(key, "mpris:length") == 0) {
                    double sec = read_numeric_variant_us_to_sec(m, vtype);
                    if (sec > 0.0) st.length_sec = sec;
                } else {
                    sd_bus_message_skip(m, "v");
                }
            } else {
                sd_bus_message_skip(m, "v");
            }
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);

    // Fallback artist for web videos / mpv / streams that omit xesam:artist
    if (st.track_artist.empty()) {
        if (!album_artist.empty()) {
            st.track_artist = std::move(album_artist);
        } else if (!album.empty()) {
            st.track_artist = std::move(album);
        } else if (!st.identity.empty()) {
            st.track_artist = st.identity;
        }
    }
}

int on_dbus_signal(sd_bus_message* /*m*/, void* userdata, sd_bus_error* /*ret_error*/) {
    auto* self = static_cast<DbusMpris*>(userdata);
    if (self) {
        self->refresh_players();
    }
    return 0;
}

} // namespace

DbusMpris::DbusMpris(std::function<void()> on_state_changed)
    : on_state_changed_(std::move(on_state_changed)) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

DbusMpris::~DbusMpris() {
    stop();
    if (art_surface_) {
        cairo_surface_destroy(art_surface_);
        art_surface_ = nullptr;
    }
    curl_global_cleanup();
}

bool DbusMpris::start() {
    int r = sd_bus_open_user(&bus_);
    if (r < 0) return false;

    sd_bus_match_signal(
        bus_,
        &prop_slot_,
        nullptr,
        "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        on_dbus_signal,
        this
    );

    sd_bus_match_signal(
        bus_,
        &seek_slot_,
        nullptr,
        "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2.Player",
        "Seeked",
        on_dbus_signal,
        this
    );

    sd_bus_match_signal(
        bus_,
        &owner_slot_,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "NameOwnerChanged",
        on_dbus_signal,
        this
    );

    refresh_players();
    return true;
}

void DbusMpris::stop() {
    if (prop_slot_) {
        sd_bus_slot_unref(prop_slot_);
        prop_slot_ = nullptr;
    }
    if (seek_slot_) {
        sd_bus_slot_unref(seek_slot_);
        seek_slot_ = nullptr;
    }
    if (owner_slot_) {
        sd_bus_slot_unref(owner_slot_);
        owner_slot_ = nullptr;
    }
    if (bus_) {
        sd_bus_unref(bus_);
        bus_ = nullptr;
    }
}

int DbusMpris::fd() const {
    return bus_ ? sd_bus_get_fd(bus_) : -1;
}

void DbusMpris::process() {
    if (!bus_) return;
    while (sd_bus_process(bus_, nullptr) > 0) {}
}

MprisState DbusMpris::query_player(const std::string& bus_name) {
    MprisState st;
    st.bus_name = bus_name;
    st.has_player = true;

    auto get_string_prop = [&](const char* iface, const char* prop, std::string& out) -> bool {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        char* val = nullptr;
        int r = sd_bus_get_property_string(bus_, bus_name.c_str(), "/org/mpris/MediaPlayer2", iface, prop, &err, &val);
        sd_bus_error_free(&err);
        if (r >= 0 && val) {
            out = val;
            std::free(val);
            return true;
        }
        return false;
    };

    auto get_bool_prop = [&](const char* iface, const char* prop, bool& out) -> bool {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        int val = 0;
        int r = sd_bus_get_property_trivial(bus_, bus_name.c_str(), "/org/mpris/MediaPlayer2", iface, prop, &err, 'b', &val);
        sd_bus_error_free(&err);
        if (r >= 0) {
            out = (val != 0);
            return true;
        }
        return false;
    };

    get_string_prop("org.mpris.MediaPlayer2", "Identity", st.identity);
    if (st.identity.empty()) {
        // Fallback identity from bus name (e.g., org.mpris.MediaPlayer2.firefox.instance_1 -> firefox)
        constexpr std::string_view prefix = "org.mpris.MediaPlayer2.";
        if (bus_name.starts_with(prefix)) {
            std::string sub = bus_name.substr(prefix.size());
            auto dot = sub.find('.');
            if (dot != std::string::npos) sub.resize(dot);
            if (!sub.empty()) {
                sub[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(sub[0])));
                st.identity = std::move(sub);
            }
        }
    }

    get_bool_prop("org.mpris.MediaPlayer2", "CanRaise", st.can_raise);

    std::string status;
    if (get_string_prop("org.mpris.MediaPlayer2.Player", "PlaybackStatus", status)) {
        st.is_playing = (status == "Playing");
    }

    get_bool_prop("org.mpris.MediaPlayer2.Player", "CanSeek", st.can_seek);
    if (get_bool_prop("org.mpris.MediaPlayer2.Player", "Shuffle", st.shuffle)) {
        st.shuffle_supported = true;
    }

    // Read Position (handles int64, uint64, int32, double)
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* pos_reply = nullptr;
        int64_t pos_us = 0;
        if (sd_bus_get_property_trivial(
                bus_,
                bus_name.c_str(),
                "/org/mpris/MediaPlayer2",
                "org.mpris.MediaPlayer2.Player",
                "Position",
                &err,
                'x',
                &pos_us
            ) >= 0) {
            st.position_sec = std::max(0.0, static_cast<double>(pos_us) / 1e6);
        } else {
            sd_bus_error_free(&err);
            uint64_t upos_us = 0;
            if (sd_bus_get_property_trivial(
                    bus_,
                    bus_name.c_str(),
                    "/org/mpris/MediaPlayer2",
                    "org.mpris.MediaPlayer2.Player",
                    "Position",
                    &err,
                    't',
                    &upos_us
                ) >= 0) {
                st.position_sec = static_cast<double>(upos_us) / 1e6;
            }
        }
        sd_bus_error_free(&err);
        if (pos_reply) sd_bus_message_unref(pos_reply);
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    if (sd_bus_get_property(
            bus_,
            bus_name.c_str(),
            "/org/mpris/MediaPlayer2",
            "org.mpris.MediaPlayer2.Player",
            "Metadata",
            &err,
            &reply,
            "a{sv}"
        ) >= 0 && reply) {
        parse_metadata_array(reply, st);
    }
    sd_bus_error_free(&err);
    if (reply) sd_bus_message_unref(reply);

    return st;
}

void DbusMpris::refresh_players() {
    if (!bus_) return;

    char** names = nullptr;
    if (sd_bus_list_names(bus_, &names, nullptr) < 0 || !names) {
        return;
    }

    std::vector<std::string> raw_player_names;
    bool has_playerctld = false;
    for (char** p = names; *p; ++p) {
        std::string_view sv(*p);
        if (sv == "org.mpris.MediaPlayer2.playerctld") {
            has_playerctld = true;
        } else if (sv.starts_with("org.mpris.MediaPlayer2.")) {
            raw_player_names.emplace_back(sv);
        }
        std::free(*p);
    }
    std::free(names);

    if (raw_player_names.empty() && has_playerctld) {
        raw_player_names.emplace_back("org.mpris.MediaPlayer2.playerctld");
    }

    std::vector<MprisState> players;
    players.reserve(raw_player_names.size());
    for (const auto& name : raw_player_names) {
        players.push_back(query_player(name));
    }

    // Universal media player selection logic (all browsers, mpv, VLC, MPD, Spotify, etc.):
    // 1. Keep last_active_bus_name_ if it is currently Playing and has a title
    // 2. First Playing player with a non-empty track_title
    // 3. First Playing player (even if track_title is empty)
    // 4. Last active player (last_active_bus_name_) if still present (so pausing a player stays on that player)
    // 5. First Paused/Stopped player with a non-empty track_title
    // 6. First available player
    MprisState chosen{};

    if (!last_active_bus_name_.empty()) {
        for (const auto& p : players) {
            if (p.bus_name == last_active_bus_name_ && p.is_playing && !p.track_title.empty()) {
                chosen = p;
                break;
            }
        }
    }

    if (!chosen.has_player) {
        for (const auto& p : players) {
            if (p.is_playing && !p.track_title.empty()) {
                chosen = p;
                break;
            }
        }
    }

    if (!chosen.has_player) {
        for (const auto& p : players) {
            if (p.is_playing) {
                chosen = p;
                break;
            }
        }
    }

    if (!chosen.has_player && !last_active_bus_name_.empty()) {
        for (const auto& p : players) {
            if (p.bus_name == last_active_bus_name_ && !p.track_title.empty()) {
                chosen = p;
                break;
            }
        }
    }

    if (!chosen.has_player) {
        for (const auto& p : players) {
            if (!p.track_title.empty()) {
                chosen = p;
                break;
            }
        }
    }

    if (!chosen.has_player && !last_active_bus_name_.empty()) {
        for (const auto& p : players) {
            if (p.bus_name == last_active_bus_name_) {
                chosen = p;
                break;
            }
        }
    }

    if (!chosen.has_player && !players.empty()) {
        chosen = players.front();
    }

    if (chosen.has_player) {
        if (chosen.is_playing || last_active_bus_name_.empty()) {
            last_active_bus_name_ = chosen.bus_name;
        } else {
            bool prev_still_exists = false;
            for (const auto& p : players) {
                if (p.bus_name == last_active_bus_name_) {
                    prev_still_exists = true;
                    break;
                }
            }
            if (!prev_still_exists) {
                last_active_bus_name_ = chosen.bus_name;
            }
        }
    } else {
        last_active_bus_name_.clear();
    }

    const bool changed = (chosen.has_player != state_.has_player) ||
                         (chosen.bus_name != state_.bus_name) ||
                         (chosen.identity != state_.identity) ||
                         (chosen.track_title != state_.track_title) ||
                         (chosen.track_artist != state_.track_artist) ||
                         (chosen.art_url != state_.art_url) ||
                         (chosen.is_playing != state_.is_playing) ||
                         (chosen.shuffle != state_.shuffle) ||
                         (chosen.shuffle_supported != state_.shuffle_supported) ||
                         (std::abs(chosen.position_sec - state_.position_sec) > 0.4) ||
                         (std::abs(chosen.length_sec - state_.length_sec) > 0.4);

    if (chosen.art_url != loaded_art_url_) {
        update_art_surface(chosen.art_url);
    }

    state_ = std::move(chosen);
    if (changed && on_state_changed_) {
        on_state_changed_();
    }
}

void DbusMpris::poll_position() {
    if (!bus_) return;

    // Every 2 ticks (~1 second), refresh all players so players that don't emit
    // standard PropertiesChanged signals on /org/mpris/MediaPlayer2 are still detected
    if (++poll_counter_ >= 2) {
        poll_counter_ = 0;
        refresh_players();
        return;
    }

    if (!state_.has_player || !state_.is_playing) return;

    sd_bus_error err = SD_BUS_ERROR_NULL;
    int64_t pos_us = 0;
    int r = sd_bus_get_property_trivial(
        bus_,
        state_.bus_name.c_str(),
        "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2.Player",
        "Position",
        &err,
        'x',
        &pos_us
    );
    sd_bus_error_free(&err);
    if (r >= 0) {
        double new_sec = std::max(0.0, static_cast<double>(pos_us) / 1e6);
        if (std::abs(new_sec - state_.position_sec) >= 0.25) {
            state_.position_sec = new_sec;
            if (on_state_changed_) {
                on_state_changed_();
            }
        }
    }
}

void DbusMpris::update_art_surface(const std::string& url) {
    loaded_art_url_ = url;
    if (art_surface_) {
        cairo_surface_destroy(art_surface_);
        art_surface_ = nullptr;
    }
    if (url.empty()) return;

    if (url.starts_with("file://") || url.starts_with("/")) {
        std::string path = decode_file_uri(url);
        art_surface_ = load_image_surface(path);
        return;
    }

    // Handle data:image/...;base64,... inline data URLs from web browsers
    if (url.starts_with("data:image/")) {
        auto comma = url.find(',');
        if (comma != std::string::npos && comma + 1 < url.size()) {
            gsize out_len = 0;
            guchar* decoded = g_base64_decode(url.c_str() + comma + 1, &out_len);
            if (decoded && out_len > 0) {
                std::vector<uint8_t> data(decoded, decoded + out_len);
                g_free(decoded);
                art_surface_ = load_image_surface_from_memory(data);
                return;
            }
            if (decoded) g_free(decoded);
        }
        return;
    }

    if (url.starts_with("http://") || url.starts_with("https://")) {
        CURL* curl = curl_easy_init();
        if (!curl) return;
        std::vector<uint8_t> data;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (X11; Linux x86_64) PersonaRice/2.0");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2500L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res == CURLE_OK && !data.empty()) {
            art_surface_ = load_image_surface_from_memory(data);
        }
    }
}

void DbusMpris::toggle_playing() {
    if (!bus_ || !state_.has_player) return;
    last_active_bus_name_ = state_.bus_name;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_call_method(
        bus_,
        state_.bus_name.c_str(),
        "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2.Player",
        "PlayPause",
        &err,
        nullptr,
        ""
    );
    sd_bus_error_free(&err);
    refresh_players();
}

void DbusMpris::next() {
    if (!bus_ || !state_.has_player) return;
    last_active_bus_name_ = state_.bus_name;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_call_method(
        bus_,
        state_.bus_name.c_str(),
        "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2.Player",
        "Next",
        &err,
        nullptr,
        ""
    );
    sd_bus_error_free(&err);
    refresh_players();
}

void DbusMpris::previous() {
    if (!bus_ || !state_.has_player) return;
    last_active_bus_name_ = state_.bus_name;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_call_method(
        bus_,
        state_.bus_name.c_str(),
        "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2.Player",
        "Previous",
        &err,
        nullptr,
        ""
    );
    sd_bus_error_free(&err);
    refresh_players();
}

void DbusMpris::seek_to_fraction(double fraction) {
    if (!bus_ || !state_.has_player || !state_.can_seek || state_.length_sec <= 0.0) return;
    last_active_bus_name_ = state_.bus_name;
    fraction = std::clamp(fraction, 0.0, 1.0);
    const int64_t target_us = static_cast<int64_t>(state_.length_sec * fraction * 1e6);

    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = -1;
    if (!state_.track_id.empty() && state_.track_id.starts_with("/")) {
        r = sd_bus_call_method(
            bus_,
            state_.bus_name.c_str(),
            "/org/mpris/MediaPlayer2",
            "org.mpris.MediaPlayer2.Player",
            "SetPosition",
            &err,
            nullptr,
            "ox",
            state_.track_id.c_str(),
            target_us
        );
        sd_bus_error_free(&err);
    }
    if (r < 0) {
        int64_t offset_us = target_us - static_cast<int64_t>(state_.position_sec * 1e6);
        sd_bus_call_method(
            bus_,
            state_.bus_name.c_str(),
            "/org/mpris/MediaPlayer2",
            "org.mpris.MediaPlayer2.Player",
            "Seek",
            &err,
            nullptr,
            "x",
            offset_us
        );
        sd_bus_error_free(&err);
    }
    state_.position_sec = state_.length_sec * fraction;
    if (on_state_changed_) on_state_changed_();
}

void DbusMpris::toggle_shuffle() {
    if (!bus_ || !state_.has_player || !state_.shuffle_supported) return;
    last_active_bus_name_ = state_.bus_name;
    const int new_val = state_.shuffle ? 0 : 1;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_set_property(
        bus_,
        state_.bus_name.c_str(),
        "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2.Player",
        "Shuffle",
        &err,
        "b",
        new_val
    );
    sd_bus_error_free(&err);
    state_.shuffle = !state_.shuffle;
    if (on_state_changed_) on_state_changed_();
}

void DbusMpris::raise() {
    if (!bus_ || !state_.has_player || !state_.can_raise) return;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_call_method(
        bus_,
        state_.bus_name.c_str(),
        "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2",
        "Raise",
        &err,
        nullptr,
        ""
    );
    sd_bus_error_free(&err);
}

} // namespace p5
