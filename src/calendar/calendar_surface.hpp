#pragma once

#include "core/asset_config.hpp"
#include "core/common.hpp"
#include "weather/noctalia_weather.hpp"
#include "core/shm_buffer.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <cairo/cairo.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <wayland-client.h>

namespace p5 {

// Runtime configuration for the Persona 5 calendar desktop surface.
struct CalendarConfig {
    int width = 400;                 // logical width; height follows the 835:653 art
    int margin = 24;                 // logical margin from the anchored screen corner
    std::string position = "bottom-right"; // top-left | top-right | bottom-left | bottom-right
    int version = 2;                 // 0 = vanilla, 1 = Royal inverted, 2 = dynamic Jan/Feb
    bool weather = true;             // read Noctalia's cached weather snapshot
    int weather_max_age_seconds = 7200;
};

struct CalendarState {
    int year = 1970;
    int month = 1;
    int day = 1;
    int weekday_index = 0;           // 0 = Sunday
    std::string weekday;             // asset stem: Sunday..Saturday
    int time_of_day = 4;             // 1..6, matching Assets/ToD
    int version = 0;                 // resolved asset folder (0 or 1)
    bool has_weather = false;
    std::string weather_icon;        // OpenWeatherMap-style code, e.g. "04d"
    int weather_frame = 0;           // 0..5, matching Assets/Weather/iconsN
};

CalendarState compute_calendar_state(int64_t wall_ms, int64_t monotonic_ms,
                                     const CalendarConfig& config,
                                     const NoctaliaWeather& weather);
bool calendar_state_equal(const CalendarState& a, const CalendarState& b);

struct CalendarArt;

class CalendarSurface {
public:
    CalendarSurface(wl_compositor* compositor,
                      zwlr_layer_shell_v1* layer_shell,
                      wl_shm* shm,
                      wl_output* output,
                      const LoadedFonts& fonts,
                      CalendarConfig config,
                      const AssetConfig& assets);
    ~CalendarSurface();

    CalendarSurface(const CalendarSurface&) = delete;
    CalendarSurface& operator=(const CalendarSurface&) = delete;

    bool init();
    void destroy();

    wl_surface* surface() const { return surface_; }

    void set_persona_active(bool active);
    bool persona_active() const { return persona_active_; }

    void set_visible(bool visible);
    bool visible() const { return visible_; }

    // Called from the daemon's 250 ms housekeeping timer. It polls the
    // Noctalia weather cache at most once a minute and re-renders only when
    // the date, time-of-day or animated weather frame actually changes.
    void tick(int64_t now_ms);
    bool is_animating(int64_t /*now*/) const { return false; }

    void handle_configure(uint32_t serial, uint32_t width, uint32_t height);
    void handle_closed();

private:
    wl_compositor* compositor_ = nullptr;
    zwlr_layer_shell_v1* layer_shell_ = nullptr;
    wl_shm* shm_ = nullptr;
    wl_output* output_ = nullptr;
    LoadedFonts fonts_;
    CalendarConfig config_;
    std::filesystem::path assets_root_;

    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
    ShmPool pool_{};

    int width_ = 400;
    int height_ = 313;
    bool configured_ = false;
    bool persona_active_ = true;
    bool visible_ = true;
    bool blank_rendered_ = false;

    CalendarState state_{};
    CalendarState last_rendered_{};
    std::unique_ptr<CalendarArt> art_;
    std::string art_key_;
    NoctaliaWeather weather_;
    int64_t last_weather_poll_ms_ = 0;

    void update(int64_t now_ms, bool force_render);
    void render();
    void update_input_region();
    void apply_anchor();
};

// Renders a PNG preview without a Wayland connection. Used by
// `p5-rice-daemon --calendar-preview ...` and for asset/weather debugging.
bool render_calendar_preview(const std::filesystem::path& output_path,
                             int64_t wall_ms,
                             const CalendarConfig& config,
                             const LoadedFonts& fonts,
                             const AssetConfig& assets);

} // namespace p5
