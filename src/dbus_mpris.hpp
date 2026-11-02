#pragma once

#include <cairo/cairo.h>
#include <cstdint>
#include <functional>
#include <string>
#include <systemd/sd-bus.h>
#include <unordered_map>
#include <vector>

namespace p5 {

struct MprisState {
    bool has_player = false;
    std::string bus_name;
    std::string identity;
    std::string desktop_entry;
    std::string playback_status;
    std::string track_id;
    std::string track_title;
    std::string track_artist;
    std::string art_url;
    bool is_playing = false;
    bool can_seek = false;
    bool can_raise = false;
    bool shuffle_supported = false;
    bool shuffle = false;
    double position_sec = 0.0;
    double length_sec = 0.0;
};

class DbusMpris {
public:
    explicit DbusMpris(std::function<void()> on_state_changed);
    ~DbusMpris();

    DbusMpris(const DbusMpris&) = delete;
    DbusMpris& operator=(const DbusMpris&) = delete;

    bool start();
    void stop();

    int fd() const;
    void process();

    // Poll position and periodically refresh all MPRIS players
    void poll_position();
    void refresh_players();

    const MprisState& state() const { return state_; }
    cairo_surface_t* art_surface() const { return art_surface_; }

    // Transport actions
    void toggle_playing();
    void next();
    void previous();
    void seek_to_fraction(double fraction);
    void toggle_shuffle();
    void raise();

private:
    struct PlayerHistory {
        bool was_playing = false;
        int64_t play_started_ms = 0;
        int64_t first_seen_ms = 0;
    };

    std::function<void()> on_state_changed_;
    sd_bus* bus_ = nullptr;
    sd_bus_slot* prop_slot_ = nullptr;
    sd_bus_slot* owner_slot_ = nullptr;
    sd_bus_slot* seek_slot_ = nullptr;
    sd_bus_slot* noctalia_active_slot_ = nullptr;
    sd_bus_slot* noctalia_track_slot_ = nullptr;
    sd_bus_slot* noctalia_players_slot_ = nullptr;

    MprisState state_{};
    std::string last_active_bus_name_;
    std::string loaded_art_url_;
    cairo_surface_t* art_surface_ = nullptr;
    int poll_counter_ = 0;
    std::unordered_map<std::string, PlayerHistory> history_;

    MprisState query_player(const std::string& bus_name);
    void query_noctalia_mpris(std::vector<MprisState>& out_players, std::string& out_active_bus);
    void update_art_surface(const std::string& url);
};

} // namespace p5
