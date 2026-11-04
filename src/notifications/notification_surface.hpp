#pragma once

#include "core/asset_config.hpp"
#include "core/common.hpp"
#include "core/image_cache.hpp"
#include "notifications/notification_event.hpp"
#include "core/shm_buffer.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <cairo/cairo.h>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <wayland-client.h>

namespace p5 {

struct BannerItem {
    uint64_t uid = 0;
    std::string tag;
    std::string body;
    int box_index = 0;
    int32_t timeout_ms = 6000;
    int32_t elapsed_hover_paused_ms = 0;
    int64_t created_ms = 0;
    int64_t last_tick_ms = 0;
    bool critical = false;
    bool play_sound = false;
    // Sender identity for click-to-open (banner text opens the sending app).
    std::string app_name;
    std::string desktop_entry;
    // D-Bus sender PID (0 = unknown): allows the scratchpad blocker to match
    // the sender's process tree against stowed windows.
    uint32_t sender_pid = 0;
};

class NotificationSurface {
public:
    NotificationSurface(
        wl_compositor* compositor,
        zwlr_layer_shell_v1* layer_shell,
        wl_shm* shm,
        wl_output* output,
        const LoadedFonts& fonts,
        const AssetConfig& assets,
        int notification_timeout_seconds = 0,
        bool notification_scratchpad_block = true,
        std::function<void(uint64_t)> on_dismiss_uid = nullptr
    );
    ~NotificationSurface();

    NotificationSurface(const NotificationSurface&) = delete;
    NotificationSurface& operator=(const NotificationSurface&) = delete;

    bool init();
    void destroy();

    wl_surface* surface() const { return surface_; }

    void set_profile_active(bool active);
    bool profile_active() const { return profile_active_; }

    void push_notification(
        const NotificationEvent& ev,
        uint64_t shared_uid,
        int shared_box_index,
        bool trigger_effects
    );
    void dismiss_uid(uint64_t uid);
    void clear_all();

    static int pick_box_index_for_tag(std::string_view tag);

    // Returns true if animations or timers are active and need periodic ticks
    bool tick(int64_t now);
    bool is_animating(int64_t now) const;

    // Pointer interaction
    void on_pointer_motion(double x, double y);
    void on_pointer_leave();
    void on_pointer_button(uint32_t button, uint32_t state);

    // Wayland callbacks
    void handle_configure(uint32_t serial, uint32_t width, uint32_t height);
    void handle_closed();

private:
    static constexpr double kBannerW = 700.0;
    static constexpr int kShowSeconds = 6;
    static constexpr size_t kMaxQueued = 8;
    static constexpr double kColMargin = 10.0;

    // banner2.png placement fractions from Main.qml
    // The banner's white tag strip is wider than the old 0.15 box. Use more of
    // it so long sender names fit before Pango ellipsizes.
    static constexpr double kTagCX = 0.789;
    static constexpr double kTagCY = 0.268;
    static constexpr double kTagW = 0.15;
    static constexpr double kTagRot = 15.7;

    static constexpr double kBodyCX = 0.536;
    static constexpr double kBodyCY = 0.538;
    static constexpr double kBodyW = 0.54;
    static constexpr double kBodyH = 0.30;
    static constexpr double kBodyRot = -1.0;

    wl_compositor* compositor_ = nullptr;
    zwlr_layer_shell_v1* layer_shell_ = nullptr;
    wl_shm* shm_ = nullptr;
    wl_output* output_ = nullptr;
    LoadedFonts fonts_;
    // Settings → Plugins override (seconds). 0 = auto (app timeout or 6s).
    int base_timeout_seconds_ = 0;
    // Don't click-open senders whose windows are hidden in Umbriel's scratchpad.
    bool scratchpad_block_ = true;
    std::function<void(uint64_t)> on_dismiss_uid_;

    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
    ShmPool pool_{};

    SharedImage banner_img_;
    double banner_h_ = 160.0;
    std::filesystem::path sound_path_;
    // Notification sound volume in percent (0 = silent, 100 = full).
    int sound_volume_ = 100;

    int width_ = 740;
    int height_ = 960;
    bool configured_ = false;
    bool profile_active_ = true;
    bool pointer_inside_ = false;
    double pointer_x_ = -1.0;
    double pointer_y_ = -1.0;

    // Front is the only visible banner; the rest wait in order.
    std::deque<BannerItem> banners_;

    void render(int64_t now);
    void start_current(int64_t now);
    void draw_banner_card(cairo_t* cr, const BannerItem& card, double card_x, double card_y, int64_t now);
    void update_input_region();
    int hit_test_banner(double x, double y) const;
    // True when the point is over the clickable text (title tag or body panel).
    bool hit_test_text(double x, double y) const;
};

} // namespace p5
