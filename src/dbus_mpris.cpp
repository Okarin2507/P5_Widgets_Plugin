#include "dbus_mpris.hpp"
#include "common.hpp"

#include <algorithm>
#include <cstring>
#include <curl/curl.h>
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

void parse_metadata_array(sd_bus_message* m, MprisState& st) {
    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}") < 0) {
        return;
    }
    while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        const char* key = nullptr;
        if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &key) >= 0 && key) {
            const char* vtype = nullptr;
            if (sd_bus_message_peek_type(m, nullptr, &vtype) >= 0 && vtype) {
                if (std::strcmp(key, "xesam:title") == 0 && std::strcmp(vtype, "s") == 0) {
                    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s") >= 0) {
                        const char* val = nullptr;
                        if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &val) >= 0 && val) {
                            st.track_title = val;
                        }
                        sd_bus_message_exit_container(m);
                    }
                } else if (std::strcmp(key, "xesam:artist") == 0 && std::strcmp(vtype, "as") == 0) {
                    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "as") >= 0) {
                        if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "s") >= 0) {
                            std::string artists;
                            const char* a = nullptr;
                            while (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &a) > 0) {
                                if (a && *a) {
                                    if (!artists.empty()) artists += ", ";
                                    artists += a;
                                }
                            }
                            if (!artists.empty()) st.track_artist = artists;
                            sd_bus_message_exit_container(m);
                        }
                        sd_bus_message_exit_container(m);
                    }
                } else if (std::strcmp(key, "mpris:artUrl") == 0 && std::strcmp(vtype, "s") == 0) {
                    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s") >= 0) {
                        const char* val = nullptr;
                        if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &val) >= 0 && val) {
                            st.art_url = val;
                        }
                        sd_bus_message_exit_container(m);
                    }
                } else if (std::strcmp(key, "mpris:trackid") == 0 && (std::strcmp(vtype, "o") == 0 || std::strcmp(vtype, "s") == 0)) {
                    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, vtype) >= 0) {
                        const char* val = nullptr;
                        char basic_t = vtype[0];
                        if (sd_bus_message_read_basic(m, basic_t, &val) >= 0 && val) {
                            st.track_id = val;
                        }
                        sd_bus_message_exit_container(m);
                    }
                } else if (std::strcmp(key, "mpris:length") == 0 && (std::strcmp(vtype, "x") == 0 || std::strcmp(vtype, "t") == 0)) {
                    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, vtype) >= 0) {
                        if (vtype[0] == 'x') {
                            int64_t us = 0;
                            if (sd_bus_message_read_basic(m, SD_BUS_TYPE_INT64, &us) >= 0 && us > 0) {
                                st.length_sec = static_cast<double>(us) / 1e6;
                            }
                        } else {
                            uint64_t us = 0;
                            if (sd_bus_message_read_basic(m, SD_BUS_TYPE_UINT64, &us) >= 0 && us > 0) {
                                st.length_sec = static_cast<double>(us) / 1e6;
                            }
                        }
                        sd_bus_message_exit_container(m);
                    }
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
    get_bool_prop("org.mpris.MediaPlayer2", "CanRaise", st.can_raise);

    std::string status;
    if (get_string_prop("org.mpris.MediaPlayer2.Player", "PlaybackStatus", status)) {
        st.is_playing = (status == "Playing");
    }

    get_bool_prop("org.mpris.MediaPlayer2.Player", "CanSeek", st.can_seek);
    if (get_bool_prop("org.mpris.MediaPlayer2.Player", "Shuffle", st.shuffle)) {
        st.shuffle_supported = true;
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
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
    }
    sd_bus_error_free(&err);

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

    std::vector<std::string> player_names;
    for (char** p = names; *p; ++p) {
        std::string_view sv(*p);
        if (sv.starts_with("org.mpris.MediaPlayer2.")) {
            player_names.emplace_back(sv);
        }
        std::free(*p);
    }
    std::free(names);

    std::vector<MprisState> players;
    players.reserve(player_names.size());
    for (const auto& name : player_names) {
        players.push_back(query_player(name));
    }

    // Selection logic matching PersonaMusicWidget.qml:
    // 1. First playing player
    // 2. Identity == "Spotify"
    // 3. First player in list
    MprisState chosen{};
    for (const auto& p : players) {
        if (p.is_playing) {
            chosen = p;
            break;
        }
    }
    if (!chosen.has_player) {
        for (const auto& p : players) {
            if (p.identity == "Spotify" || p.bus_name.find("spotify") != std::string::npos) {
                chosen = p;
                break;
            }
        }
    }
    if (!chosen.has_player && !players.empty()) {
        chosen = players.front();
    }

    const bool changed = (chosen.has_player != state_.has_player) ||
                         (chosen.bus_name != state_.bus_name) ||
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
    if (!bus_ || !state_.has_player || !state_.is_playing) return;

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

    if (url.starts_with("http://") || url.starts_with("https://")) {
        CURL* curl = curl_easy_init();
        if (!curl) return;
        std::vector<uint8_t> data;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
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
    fraction = std::clamp(fraction, 0.0, 1.0);
    const int64_t target_us = static_cast<int64_t>(state_.length_sec * fraction * 1e6);

    sd_bus_error err = SD_BUS_ERROR_NULL;
    if (!state_.track_id.empty() && state_.track_id.starts_with("/")) {
        sd_bus_call_method(
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
    } else {
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
    }
    sd_bus_error_free(&err);
    state_.position_sec = state_.length_sec * fraction;
    if (on_state_changed_) on_state_changed_();
}

void DbusMpris::toggle_shuffle() {
    if (!bus_ || !state_.has_player || !state_.shuffle_supported) return;
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

