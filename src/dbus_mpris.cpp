#include "dbus_mpris.hpp"
#include "common.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <curl/curl.h>
#include <filesystem>
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

std::string to_lower_ascii(std::string_view sv) {
    std::string out(sv.size(), '\0');
    for (size_t i = 0; i < sv.size(); ++i) {
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(sv[i])));
    }
    return out;
}

bool starts_with_mpris_prefix(std::string_view sv) {
    constexpr std::string_view prefix = "org.mpris.mediaplayer2.";
    if (sv.size() <= prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(sv[i])) != prefix[i]) {
            return false;
        }
    }
    return true;
}

bool is_browser_player(const MprisState& p) {
    const std::string hay = to_lower_ascii(p.bus_name) + " " +
                            to_lower_ascii(p.identity) + " " +
                            to_lower_ascii(p.desktop_entry);
    static constexpr std::string_view kBrowserTokens[] = {
        "firefox",
        "chrom",
        "brave",
        "vivaldi",
        "msedge",
        "microsoft-edge",
        ".edge",
        "opera",
        "zen",
        "librewolf",
        "waterfox",
        "floorp",
        "thorium",
        "epiphany",
        "falkon",
        "midori",
        "qutebrowser",
        "plasma-browser-integration",
        "webkit",
    };
    for (const auto& tok : kBrowserTokens) {
        if (hay.find(tok) != std::string::npos) {
            return true;
        }
    }
    return false;
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

std::string fallback_identity_from_bus_name(std::string_view bus_name) {
    constexpr size_t prefix_len = 23; // "org.mpris.MediaPlayer2."
    if (bus_name.size() <= prefix_len) return {};
    std::string sub(bus_name.substr(prefix_len));
    auto dot = sub.find('.');
    if (dot != std::string::npos) sub.resize(dot);
    if (!sub.empty()) {
        sub[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(sub[0])));
    }
    return sub;
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
    std::string xesam_url;

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
                } else if (std::strcmp(key, "xesam:url") == 0) {
                    std::string s = read_string_or_string_array(m, vtype);
                    if (!s.empty()) xesam_url = std::move(s);
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

    // Fallback title from xesam:url filename if xesam:title was empty (e.g., local tracks in Harmonoid/mpv)
    if (st.track_title.empty() && !xesam_url.empty()) {
        std::string decoded = decode_file_uri(xesam_url);
        std::filesystem::path p(decoded);
        std::string stem = p.stem().string();
        if (!stem.empty()) {
            st.track_title = std::move(stem);
        }
    }

    // Fallback artist for local tracks / web videos / streams that omit xesam:artist
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

bool parse_noctalia_player_dict(sd_bus_message* m, MprisState& st) {
    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}") < 0) {
        return false;
    }

    std::string album;
    std::string source_url;

    while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        const char* key = nullptr;
        if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &key) >= 0 && key) {
            const char* vtype = nullptr;
            if (sd_bus_message_peek_type(m, nullptr, &vtype) >= 0 && vtype) {
                if (std::strcmp(key, "bus_name") == 0) {
                    st.bus_name = read_string_or_string_array(m, vtype);
                } else if (std::strcmp(key, "identity") == 0) {
                    st.identity = read_string_or_string_array(m, vtype);
                } else if (std::strcmp(key, "desktop_entry") == 0) {
                    st.desktop_entry = read_string_or_string_array(m, vtype);
                } else if (std::strcmp(key, "playback_status") == 0) {
                    st.playback_status = read_string_or_string_array(m, vtype);
                    st.is_playing = (st.playback_status == "Playing");
                } else if (std::strcmp(key, "track_id") == 0) {
                    st.track_id = read_string_or_string_array(m, vtype);
                } else if (std::strcmp(key, "title") == 0) {
                    st.track_title = read_string_or_string_array(m, vtype);
                } else if (std::strcmp(key, "artists") == 0) {
                    st.track_artist = read_string_or_string_array(m, vtype);
                } else if (std::strcmp(key, "album") == 0) {
                    album = read_string_or_string_array(m, vtype);
                } else if (std::strcmp(key, "source_url") == 0) {
                    source_url = read_string_or_string_array(m, vtype);
                } else if (std::strcmp(key, "art_url") == 0) {
                    st.art_url = read_string_or_string_array(m, vtype);
                } else if (std::strcmp(key, "shuffle") == 0 && std::strcmp(vtype, "b") == 0) {
                    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "b") >= 0) {
                        int b = 0;
                        if (sd_bus_message_read_basic(m, SD_BUS_TYPE_BOOLEAN, &b) >= 0) {
                            st.shuffle = (b != 0);
                            st.shuffle_supported = true;
                        }
                        sd_bus_message_exit_container(m);
                    }
                } else if (std::strcmp(key, "can_seek") == 0 && std::strcmp(vtype, "b") == 0) {
                    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "b") >= 0) {
                        int b = 0;
                        if (sd_bus_message_read_basic(m, SD_BUS_TYPE_BOOLEAN, &b) >= 0) {
                            st.can_seek = (b != 0);
                        }
                        sd_bus_message_exit_container(m);
                    }
                } else if (std::strcmp(key, "position_us") == 0) {
                    double sec = read_numeric_variant_us_to_sec(m, vtype);
                    if (sec >= 0.0) st.position_sec = sec;
                } else if (std::strcmp(key, "length_us") == 0) {
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

    if (st.bus_name.empty()) {
        return false;
    }
    st.has_player = true;
    if (st.identity.empty()) {
        st.identity = fallback_identity_from_bus_name(st.bus_name);
    }
    if (st.track_title.empty() && !source_url.empty()) {
        std::string decoded = decode_file_uri(source_url);
        std::filesystem::path p(decoded);
        std::string stem = p.stem().string();
        if (!stem.empty()) {
            st.track_title = std::move(stem);
        }
    }
    if (st.track_artist.empty()) {
        if (!album.empty()) {
            st.track_artist = std::move(album);
        } else if (!st.identity.empty()) {
            st.track_artist = st.identity;
        }
    }
    return true;
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

    // Subscribe to Noctalia Shell's built-in MprisService signals on /dev/noctalia/Mpris
    sd_bus_match_signal(
        bus_,
        &noctalia_active_slot_,
        nullptr,
        "/dev/noctalia/Mpris",
        "dev.noctalia.Mpris",
        "ActivePlayerChanged",
        on_dbus_signal,
        this
    );

    sd_bus_match_signal(
        bus_,
        &noctalia_track_slot_,
        nullptr,
        "/dev/noctalia/Mpris",
        "dev.noctalia.Mpris",
        "TrackChanged",
        on_dbus_signal,
        this
    );

    sd_bus_match_signal(
        bus_,
        &noctalia_players_slot_,
        nullptr,
        "/dev/noctalia/Mpris",
        "dev.noctalia.Mpris",
        "PlayersChanged",
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
    if (noctalia_active_slot_) {
        sd_bus_slot_unref(noctalia_active_slot_);
        noctalia_active_slot_ = nullptr;
    }
    if (noctalia_track_slot_) {
        sd_bus_slot_unref(noctalia_track_slot_);
        noctalia_track_slot_ = nullptr;
    }
    if (noctalia_players_slot_) {
        sd_bus_slot_unref(noctalia_players_slot_);
        noctalia_players_slot_ = nullptr;
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

void DbusMpris::query_noctalia_mpris(std::vector<MprisState>& out_players, std::string& out_active_bus) {
    if (!bus_) return;

    // 1. Query GetActivePlayer from dev.noctalia.Mpris
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        if (sd_bus_call_method(
                bus_,
                "dev.noctalia.Mpris",
                "/dev/noctalia/Mpris",
                "dev.noctalia.Mpris",
                "GetActivePlayer",
                &err,
                &reply,
                ""
            ) >= 0 && reply) {
            MprisState active_st;
            if (parse_noctalia_player_dict(reply, active_st)) {
                out_active_bus = active_st.bus_name;
                out_players.push_back(std::move(active_st));
            }
        }
        sd_bus_error_free(&err);
        if (reply) sd_bus_message_unref(reply);
    }

    // 2. Query GetPlayers from dev.noctalia.Mpris
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        if (sd_bus_call_method(
                bus_,
                "dev.noctalia.Mpris",
                "/dev/noctalia/Mpris",
                "dev.noctalia.Mpris",
                "GetPlayers",
                &err,
                &reply,
                ""
            ) >= 0 && reply) {
            if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "a{sv}") >= 0) {
                while (true) {
                    const char* elem_type = nullptr;
                    if (sd_bus_message_peek_type(reply, nullptr, &elem_type) <= 0) break;
                    MprisState st;
                    if (!parse_noctalia_player_dict(reply, st)) break;
                    bool exists = false;
                    for (auto& existing : out_players) {
                        if (existing.bus_name == st.bus_name) {
                            existing = std::move(st);
                            exists = true;
                            break;
                        }
                    }
                    if (!exists) {
                        out_players.push_back(std::move(st));
                    }
                }
                sd_bus_message_exit_container(reply);
            }
        }
        sd_bus_error_free(&err);
        if (reply) sd_bus_message_unref(reply);
    }
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

    // 1. Try GetAll on org.mpris.MediaPlayer2 (matches Noctalia's MprisService behavior)
    bool got_root_all = false;
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        if (sd_bus_call_method(
                bus_,
                bus_name.c_str(),
                "/org/mpris/MediaPlayer2",
                "org.freedesktop.DBus.Properties",
                "GetAll",
                &err,
                &reply,
                "s",
                "org.mpris.MediaPlayer2"
            ) >= 0 && reply) {
            if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sv}") >= 0) {
                got_root_all = true;
                while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
                    const char* key = nullptr;
                    if (sd_bus_message_read_basic(reply, SD_BUS_TYPE_STRING, &key) >= 0 && key) {
                        const char* vtype = nullptr;
                        if (sd_bus_message_peek_type(reply, nullptr, &vtype) >= 0 && vtype) {
                            if (std::strcmp(key, "Identity") == 0) {
                                st.identity = read_string_or_string_array(reply, vtype);
                            } else if (std::strcmp(key, "DesktopEntry") == 0) {
                                st.desktop_entry = read_string_or_string_array(reply, vtype);
                            } else if (std::strcmp(key, "CanRaise") == 0 && std::strcmp(vtype, "b") == 0) {
                                if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "b") >= 0) {
                                    int b = 0;
                                    if (sd_bus_message_read_basic(reply, SD_BUS_TYPE_BOOLEAN, &b) >= 0) {
                                        st.can_raise = (b != 0);
                                    }
                                    sd_bus_message_exit_container(reply);
                                }
                            } else {
                                sd_bus_message_skip(reply, "v");
                            }
                        } else {
                            sd_bus_message_skip(reply, "v");
                        }
                    }
                    sd_bus_message_exit_container(reply);
                }
                sd_bus_message_exit_container(reply);
            }
        }
        sd_bus_error_free(&err);
        if (reply) sd_bus_message_unref(reply);
    }

    if (!got_root_all) {
        get_string_prop("org.mpris.MediaPlayer2", "Identity", st.identity);
        get_string_prop("org.mpris.MediaPlayer2", "DesktopEntry", st.desktop_entry);
        get_bool_prop("org.mpris.MediaPlayer2", "CanRaise", st.can_raise);
    }

    if (st.identity.empty()) {
        st.identity = fallback_identity_from_bus_name(bus_name);
    }

    // 2. Try GetAll on org.mpris.MediaPlayer2.Player (used by Harmonoid / Flutter dbus & Noctalia)
    bool got_player_all = false;
    bool got_metadata_in_all = false;
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        if (sd_bus_call_method(
                bus_,
                bus_name.c_str(),
                "/org/mpris/MediaPlayer2",
                "org.freedesktop.DBus.Properties",
                "GetAll",
                &err,
                &reply,
                "s",
                "org.mpris.MediaPlayer2.Player"
            ) >= 0 && reply) {
            if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sv}") >= 0) {
                got_player_all = true;
                while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
                    const char* key = nullptr;
                    if (sd_bus_message_read_basic(reply, SD_BUS_TYPE_STRING, &key) >= 0 && key) {
                        const char* vtype = nullptr;
                        if (sd_bus_message_peek_type(reply, nullptr, &vtype) >= 0 && vtype) {
                            if (std::strcmp(key, "PlaybackStatus") == 0) {
                                st.playback_status = read_string_or_string_array(reply, vtype);
                                st.is_playing = (st.playback_status == "Playing");
                            } else if (std::strcmp(key, "CanSeek") == 0 && std::strcmp(vtype, "b") == 0) {
                                if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "b") >= 0) {
                                    int b = 0;
                                    if (sd_bus_message_read_basic(reply, SD_BUS_TYPE_BOOLEAN, &b) >= 0) {
                                        st.can_seek = (b != 0);
                                    }
                                    sd_bus_message_exit_container(reply);
                                }
                            } else if (std::strcmp(key, "Shuffle") == 0 && std::strcmp(vtype, "b") == 0) {
                                if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "b") >= 0) {
                                    int b = 0;
                                    if (sd_bus_message_read_basic(reply, SD_BUS_TYPE_BOOLEAN, &b) >= 0) {
                                        st.shuffle = (b != 0);
                                        st.shuffle_supported = true;
                                    }
                                    sd_bus_message_exit_container(reply);
                                }
                            } else if (std::strcmp(key, "Position") == 0) {
                                double sec = read_numeric_variant_us_to_sec(reply, vtype);
                                if (sec >= 0.0) st.position_sec = sec;
                            } else if (std::strcmp(key, "Metadata") == 0 && std::strcmp(vtype, "a{sv}") == 0) {
                                if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "a{sv}") >= 0) {
                                    parse_metadata_array(reply, st);
                                    got_metadata_in_all = true;
                                    sd_bus_message_exit_container(reply);
                                }
                            } else {
                                sd_bus_message_skip(reply, "v");
                            }
                        } else {
                            sd_bus_message_skip(reply, "v");
                        }
                    }
                    sd_bus_message_exit_container(reply);
                }
                sd_bus_message_exit_container(reply);
            }
        }
        sd_bus_error_free(&err);
        if (reply) sd_bus_message_unref(reply);
    }

    if (!got_player_all) {
        std::string status;
        if (get_string_prop("org.mpris.MediaPlayer2.Player", "PlaybackStatus", status)) {
            st.playback_status = status;
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
    }

    if (!got_metadata_in_all || st.track_title.empty()) {
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
    }

    return st;
}

void DbusMpris::refresh_players() {
    if (!bus_) return;

    // 1. Query Noctalia's built-in MprisService (dev.noctalia.Mpris)
    std::vector<MprisState> noctalia_players;
    std::string noctalia_active_bus;
    query_noctalia_mpris(noctalia_players, noctalia_active_bus);

    // 2. Discover all direct org.mpris.MediaPlayer2.* services on session D-Bus
    char** names = nullptr;
    std::vector<std::string> raw_player_names;
    bool has_playerctld = false;
    if (sd_bus_list_names(bus_, &names, nullptr) >= 0 && names) {
        for (char** p = names; *p; ++p) {
            std::string_view sv(*p);
            if (sv == "org.mpris.MediaPlayer2.playerctld") {
                has_playerctld = true;
            } else if (starts_with_mpris_prefix(sv)) {
                raw_player_names.emplace_back(sv);
            }
            std::free(*p);
        }
        std::free(names);
    }

    if (raw_player_names.empty() && has_playerctld) {
        raw_player_names.emplace_back("org.mpris.MediaPlayer2.playerctld");
    }

    std::vector<MprisState> players;
    players.reserve(raw_player_names.size() + noctalia_players.size());
    for (const auto& name : raw_player_names) {
        players.push_back(query_player(name));
    }

    // 3. Merge Noctalia MprisService players with direct MPRIS players
    for (auto& np : noctalia_players) {
        bool merged = false;
        for (auto& dp : players) {
            if (dp.bus_name == np.bus_name) {
                if (dp.identity.empty() && !np.identity.empty()) dp.identity = np.identity;
                if (dp.desktop_entry.empty() && !np.desktop_entry.empty()) dp.desktop_entry = np.desktop_entry;
                if (dp.track_title.empty() && !np.track_title.empty()) dp.track_title = np.track_title;
                if (dp.track_artist.empty() && !np.track_artist.empty()) dp.track_artist = np.track_artist;
                // Prefer Noctalia's cached/resolved art_url if direct art_url is empty or remote
                if (dp.art_url.empty() || (np.art_url.starts_with("/") || np.art_url.starts_with("file://"))) {
                    if (!np.art_url.empty()) dp.art_url = np.art_url;
                }
                if (dp.length_sec <= 0.0 && np.length_sec > 0.0) dp.length_sec = np.length_sec;
                if (dp.position_sec <= 0.0 && np.position_sec > 0.0) dp.position_sec = np.position_sec;
                if (!dp.is_playing && np.is_playing) {
                    dp.is_playing = true;
                    dp.playback_status = "Playing";
                }
                if (!dp.can_seek && np.can_seek) dp.can_seek = true;
                merged = true;
                break;
            }
        }
        if (!merged) {
            players.push_back(std::move(np));
        }
    }

    // 4. Update per-player history (track when each player was opened and when it started playing)
    const int64_t now = now_ms();
    for (const auto& p : players) {
        auto& h = history_[p.bus_name];
        if (h.first_seen_ms == 0) {
            h.first_seen_ms = now;
        }
        if (p.is_playing && !h.was_playing) {
            h.play_started_ms = now;
            // A newly playing player immediately becomes the active candidate
            last_active_bus_name_ = p.bus_name;
        }
        h.was_playing = p.is_playing;
    }
    // Prune closed players from history_
    std::erase_if(history_, [&](const auto& kv) {
        for (const auto& p : players) {
            if (p.bus_name == kv.first) return false;
        }
        return true;
    });

    // 5. Score and select the best player:
    //    - Playing players always beat non-playing players.
    //    - Dedicated music apps (Harmonoid, Spotify, MPD, VLC, etc., i.e. !is_browser_player)
    //      always take priority over web browsers in the same playback state!
    //    - Newly playing or newly opened players beat older background players.
    //    - Noctalia's active player (noctalia_active_bus) and last_active_bus_name_ break ties.
    MprisState chosen{};
    int64_t best_score = INT64_MIN;

    for (const auto& p : players) {
        const bool dedicated_app = !is_browser_player(p);
        const auto& h = history_[p.bus_name];
        const bool is_noctalia_active = (!noctalia_active_bus.empty() && p.bus_name == noctalia_active_bus);
        const bool is_last_active = (!last_active_bus_name_.empty() && p.bus_name == last_active_bus_name_);
        const bool has_title = !p.track_title.empty();

        int64_t score = 0;
        if (p.is_playing) {
            score += 1'000'000'000'000LL;
            if (dedicated_app) {
                score += 200'000'000'000LL;
            }
            if (is_noctalia_active) {
                score += 50'000'000'000LL;
            }
            if (is_last_active) {
                score += 20'000'000'000LL;
            }
            if (has_title) {
                score += 10'000'000'000LL;
            }
            score += (h.play_started_ms % 1'000'000'000LL);
        } else {
            // Paused / Stopped: Dedicated music apps (like Harmonoid) beat paused browser tabs
            if (dedicated_app) {
                score += 200'000'000'000LL;
            }
            if (is_noctalia_active && (dedicated_app || players.size() == 1)) {
                score += 50'000'000'000LL;
            }
            if (is_last_active && dedicated_app) {
                score += 30'000'000'000LL;
            } else if (is_last_active) {
                score += 5'000'000'000LL;
            }
            if (has_title) {
                score += 10'000'000'000LL;
            }
            score += (std::max(h.play_started_ms, h.first_seen_ms) % 1'000'000'000LL);
        }

        if (!chosen.has_player || score > best_score) {
            best_score = score;
            chosen = p;
        }
    }

    if (chosen.has_player) {
        // Ensure track_title is never empty when displaying a valid player
        if (chosen.track_title.empty() && !chosen.identity.empty()) {
            chosen.track_title = chosen.identity;
        }
        last_active_bus_name_ = chosen.bus_name;
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
    int r = sd_bus_call_method(
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
    if (r < 0) {
        sd_bus_call_method(
            bus_,
            "dev.noctalia.Mpris",
            "/dev/noctalia/Mpris",
            "dev.noctalia.Mpris",
            "PlayPausePlayer",
            &err,
            nullptr,
            "s",
            state_.bus_name.c_str()
        );
        sd_bus_error_free(&err);
    }
    refresh_players();
}

void DbusMpris::next() {
    if (!bus_ || !state_.has_player) return;
    last_active_bus_name_ = state_.bus_name;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method(
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
    if (r < 0) {
        sd_bus_call_method(
            bus_,
            "dev.noctalia.Mpris",
            "/dev/noctalia/Mpris",
            "dev.noctalia.Mpris",
            "NextPlayer",
            &err,
            nullptr,
            "s",
            state_.bus_name.c_str()
        );
        sd_bus_error_free(&err);
    }
    refresh_players();
}

void DbusMpris::previous() {
    if (!bus_ || !state_.has_player) return;
    last_active_bus_name_ = state_.bus_name;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method(
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
    if (r < 0) {
        sd_bus_call_method(
            bus_,
            "dev.noctalia.Mpris",
            "/dev/noctalia/Mpris",
            "dev.noctalia.Mpris",
            "PreviousPlayer",
            &err,
            nullptr,
            "s",
            state_.bus_name.c_str()
        );
        sd_bus_error_free(&err);
    }
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
        r = sd_bus_call_method(
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
    if (r < 0) {
        sd_bus_call_method(
            bus_,
            "dev.noctalia.Mpris",
            "/dev/noctalia/Mpris",
            "dev.noctalia.Mpris",
            "SetPositionPlayer",
            &err,
            nullptr,
            "sx",
            state_.bus_name.c_str(),
            target_us
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
    int r = sd_bus_set_property(
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
    if (r < 0) {
        sd_bus_call_method(
            bus_,
            "dev.noctalia.Mpris",
            "/dev/noctalia/Mpris",
            "dev.noctalia.Mpris",
            "SetShufflePlayer",
            &err,
            nullptr,
            "sb",
            state_.bus_name.c_str(),
            new_val
        );
        sd_bus_error_free(&err);
    }
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
