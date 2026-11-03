#include "calendar/calendar_surface.hpp"

#include "core/image_cache.hpp"
#include "weather/day_phase.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <numbers>
#include <pango/pangocairo.h>
#include <sstream>

namespace p5 {

namespace {

constexpr double kDesignW = 835.0;
constexpr double kDesignH = 653.0;

constexpr const char* kWeekdays[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

constexpr const char* kMonths[] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};

constexpr const char* kTimeOfDayLabels[] = {
    "", "Evening", "Early Morning", "Morning", "Daytime", "Afternoon", "Evening"
};

int64_t wall_ms_now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string upper_ascii(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

void layer_configure(void* data, zwlr_layer_surface_v1* /*surface*/, uint32_t serial,
                     uint32_t width, uint32_t height) {
    auto* self = static_cast<CalendarSurface*>(data);
    if (self) self->handle_configure(serial, width, height);
}

void layer_closed(void* data, zwlr_layer_surface_v1* /*surface*/) {
    auto* self = static_cast<CalendarSurface*>(data);
    if (self) self->handle_closed();
}

constexpr zwlr_layer_surface_v1_listener kLayerListener = {
    .configure = layer_configure,
    .closed = layer_closed,
};

void paint_surface(cairo_t* cr, cairo_surface_t* image) {
    if (!image) return;
    cairo_set_source_surface(cr, image, 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
    cairo_paint(cr);
}

void draw_text(cairo_t* cr, const std::string& text, const std::string& family,
               double size, double center_x, double center_y, double rotation_deg,
               const Color& fill, const Color& outline, double outline_width) {
    if (text.empty()) return;

    cairo_save(cr);
    cairo_translate(cr, center_x, center_y);
    cairo_rotate(cr, rotation_deg * std::numbers::pi / 180.0);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* desc = pango_font_description_new();
    pango_font_description_set_family(desc, family.empty() ? "Sans" : family.c_str());
    pango_font_description_set_absolute_size(desc, size * PANGO_SCALE);
    pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, text.c_str(), -1);

    int text_w = 0;
    int text_h = 0;
    pango_layout_get_pixel_size(layout, &text_w, &text_h);
    cairo_translate(cr, -text_w * 0.5, -text_h * 0.5);

    if (outline_width > 0.0) {
        pango_cairo_layout_path(cr, layout);
        cairo_set_source_rgba(cr, outline.r, outline.g, outline.b, outline.a);
        cairo_set_line_width(cr, outline_width);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
        cairo_stroke_preserve(cr);
        cairo_set_source_rgba(cr, fill.r, fill.g, fill.b, fill.a);
        cairo_fill(cr);
    } else {
        cairo_set_source_rgba(cr, fill.r, fill.g, fill.b, fill.a);
        pango_cairo_show_layout(cr, layout);
    }

    pango_font_description_free(desc);
    g_object_unref(layout);
    cairo_restore(cr);
}

void draw_jagged_panel(cairo_t* cr, double x, double y, double w, double h,
                       double rotation_deg, const Color& fill, const Color& outline,
                       double outline_width) {
    cairo_save(cr);
    cairo_translate(cr, x + w * 0.5, y + h * 0.5);
    cairo_rotate(cr, rotation_deg * std::numbers::pi / 180.0);
    cairo_translate(cr, -w * 0.5, -h * 0.5);

    constexpr double points[][2] = {
        {0.00, 0.10}, {0.16, 0.00}, {0.40, 0.05}, {0.61, 0.00},
        {0.84, 0.04}, {1.00, 0.18}, {0.96, 0.46}, {1.00, 0.72},
        {0.86, 1.00}, {0.60, 0.94}, {0.35, 1.00}, {0.10, 0.93},
        {0.00, 0.70},
    };

    bool first = true;
    for (const auto& point : points) {
        const double px = point[0] * w;
        const double py = point[1] * h;
        if (first) {
            cairo_move_to(cr, px, py);
            first = false;
        } else {
            cairo_line_to(cr, px, py);
        }
    }
    cairo_close_path(cr);

    cairo_set_source_rgba(cr, fill.r, fill.g, fill.b, fill.a);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, outline.r, outline.g, outline.b, outline.a);
    cairo_set_line_width(cr, outline_width);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void draw_fallback_weather(cairo_t* cr, double cx, double cy, double size,
                           const std::string& icon) {
    const bool day = icon.empty() || icon.back() == 'd';
    const std::string kind = icon.size() >= 2 ? icon.substr(0, 2) : "01";

    const Color ink = Color::from_rgba8(12, 12, 14);
    const Color paper = Color::from_rgba8(250, 250, 247);
    const Color cyan = Color::from_rgba8(0, 194, 220);

    cairo_save(cr);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    const double sun_r = size * 0.18;
    const double sun_x = cx - size * 0.13;
    const double sun_y = cy - size * 0.12;

    if (kind == "01") {
        cairo_set_source_rgba(cr, paper.r, paper.g, paper.b, 1.0);
        cairo_arc(cr, sun_x, sun_y, sun_r, 0.0, 2.0 * std::numbers::pi);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, 1.0);
        cairo_set_line_width(cr, size * 0.035);
        cairo_stroke(cr);

        if (day) {
            for (int i = 0; i < 8; ++i) {
                const double angle = i * std::numbers::pi / 4.0;
                cairo_move_to(cr, sun_x + std::cos(angle) * sun_r * 1.35,
                              sun_y + std::sin(angle) * sun_r * 1.35);
                cairo_line_to(cr, sun_x + std::cos(angle) * sun_r * 1.75,
                              sun_y + std::sin(angle) * sun_r * 1.75);
            }
            cairo_stroke(cr);
        } else {
            cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, 1.0);
            cairo_arc(cr, sun_x, sun_y, sun_r * 0.55, 0.0, 2.0 * std::numbers::pi);
            cairo_fill(cr);
        }
    } else {
        // Sun/moon behind the cloud for the partly-cloudy variants.
        if (kind == "02") {
            cairo_set_source_rgba(cr, paper.r, paper.g, paper.b, 1.0);
            cairo_arc(cr, sun_x - size * 0.10, sun_y - size * 0.08, sun_r * 0.85,
                      0.0, 2.0 * std::numbers::pi);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, 1.0);
            cairo_set_line_width(cr, size * 0.03);
            cairo_stroke(cr);
        }

        cairo_set_source_rgba(cr, paper.r, paper.g, paper.b, 1.0);
        cairo_arc(cr, cx - size * 0.24, cy + size * 0.10, size * 0.16, 0.0, 2.0 * std::numbers::pi);
        cairo_arc(cr, cx - size * 0.02, cy - size * 0.04, size * 0.22, 0.0, 2.0 * std::numbers::pi);
        cairo_arc(cr, cx + size * 0.22, cy + size * 0.09, size * 0.16, 0.0, 2.0 * std::numbers::pi);
        cairo_rectangle(cr, cx - size * 0.24, cy + size * 0.07, size * 0.48, size * 0.18);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, 1.0);
        cairo_set_line_width(cr, size * 0.035);
        cairo_stroke(cr);

        cairo_set_source_rgba(cr, cyan.r, cyan.g, cyan.b, 1.0);
        cairo_set_line_width(cr, size * 0.045);
        if (kind == "09" || kind == "10") {
            for (int i = -1; i <= 1; ++i) {
                const double x = cx + i * size * 0.17;
                cairo_move_to(cr, x, cy + size * 0.33);
                cairo_line_to(cr, x - size * 0.05, cy + size * 0.52);
            }
            cairo_stroke(cr);
        } else if (kind == "13") {
            for (int i = -1; i <= 1; ++i) {
                const double x = cx + i * size * 0.17;
                const double y = cy + size * 0.40;
                for (int arm = 0; arm < 3; ++arm) {
                    const double a = arm * std::numbers::pi / 3.0;
                    cairo_move_to(cr, x - std::cos(a) * size * 0.06, y - std::sin(a) * size * 0.06);
                    cairo_line_to(cr, x + std::cos(a) * size * 0.06, y + std::sin(a) * size * 0.06);
                }
            }
            cairo_stroke(cr);
        } else if (kind == "11") {
            cairo_move_to(cr, cx - size * 0.04, cy + size * 0.31);
            cairo_line_to(cr, cx + size * 0.07, cy + size * 0.44);
            cairo_line_to(cr, cx - size * 0.03, cy + size * 0.44);
            cairo_line_to(cr, cx + size * 0.04, cy + size * 0.58);
            cairo_stroke(cr);
        } else if (kind == "50") {
            for (int i = 0; i < 3; ++i) {
                cairo_move_to(cr, cx - size * 0.24, cy + size * (0.30 + i * 0.09));
                cairo_line_to(cr, cx + size * 0.24, cy + size * (0.30 + i * 0.09));
            }
            cairo_stroke(cr);
        }
    }

    cairo_restore(cr);
}

void draw_fallback_calendar(cairo_t* cr, const CalendarState& state, const LoadedFonts& fonts) {
    const Color ink = Color::from_rgba8(10, 10, 12);
    const Color paper = Color::from_rgba8(248, 248, 244);
    const Color cyan = Color::from_rgba8(0, 194, 220);
    const Color red = Color::from_rgba8(214, 30, 38);

    // Crimson slash + white comic panels, the same visual vocabulary as the
    // notification and music fallbacks.
    cairo_save(cr);
    cairo_set_source_rgba(cr, red.r, red.g, red.b, 1.0);
    cairo_move_to(cr, 32.0, 330.0);
    cairo_line_to(cr, 790.0, 250.0);
    cairo_line_to(cr, 805.0, 300.0);
    cairo_line_to(cr, 44.0, 382.0);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_restore(cr);

    draw_jagged_panel(cr, 62.0, 122.0, 675.0, 336.0, -3.5, paper, ink, 8.0);

    const std::string month_label = kMonths[std::clamp(state.month - 1, 0, 11)];
    const std::string day_label = std::to_string(state.day);

    draw_text(cr, month_label, fonts.notify_p5_main, 118.0, 185.0, 250.0, -12.0,
              ink, paper, 10.0);
    draw_text(cr, day_label, fonts.notify_p5_main, 286.0, 420.0, 255.0, 4.0,
              ink, paper, 14.0);

    draw_jagged_panel(cr, 335.0, 350.0, 408.0, 112.0, -5.5, cyan, ink, 7.0);
    draw_text(cr, upper_ascii(state.weekday), fonts.notify_ux_regular, 76.0,
              539.0, 407.0, -5.5, ink, paper, 7.0);

    draw_jagged_panel(cr, 60.0, 472.0, 300.0, 105.0, -16.0, ink, paper, 7.0);
    draw_text(cr, kTimeOfDayLabels[std::clamp(state.time_of_day, 1, 6)],
              fonts.notify_ux_regular, 58.0, 210.0, 523.0, -16.0, paper, ink, 0.0);

    if (state.has_weather) {
        draw_fallback_weather(cr, 710.0, 132.0, 128.0, state.weather_icon);
    }
}

} // namespace

struct CalendarArt {
    static constexpr int kCoreCount = 10;
    std::array<SharedImage, kCoreCount> core{};
    std::array<SharedImage, 3> weather{};
    bool has_core = false;
    bool has_weather = false;

    CalendarArt() = default;
    CalendarArt(const CalendarArt&) = delete;
    CalendarArt& operator=(const CalendarArt&) = delete;

    ~CalendarArt() { clear(); }

    void clear() {
        for (auto& surface : core) surface.reset();
        for (auto& surface : weather) surface.reset();
        has_core = false;
        has_weather = false;
    }
};

namespace {

std::filesystem::path resolve_calendar_root(const AssetConfig& assets) {
    if (assets.calendar_assets_dir.empty()) {
        return resolve_asset_path("p5calendar", "Assets");
    }
    std::error_code ec;
    if (std::filesystem::exists(assets.calendar_assets_dir / "Assets" / "Day", ec)) {
        return assets.calendar_assets_dir / "Assets";
    }
    return assets.calendar_assets_dir;
}

SharedImage load_calendar_asset(const std::filesystem::path& root,
                               std::string_view relative_path, int width, int height) {
    return load_shared_image_scaled(root / std::filesystem::path(relative_path),
                                    width, height);
}

std::string calendar_art_key(const CalendarState& state, int width, int height) {
    std::ostringstream key;
    key << state.version << '|' << state.month << '|' << state.day << '|'
        << state.weekday_index << '|' << state.time_of_day << '|'
        << (state.has_weather ? state.weather_icon : std::string("-")) << '|'
        << width << 'x' << height;
    return key.str();
}

void load_calendar_art(CalendarArt& art, const CalendarState& state,
                       int width, int height,
                       const std::filesystem::path& assets_root) {
    art.clear();

    const std::string version = std::to_string(state.version);
    // Rainmeter's MeasureMonthLeft: one-digit days use the compact month
    // asset; two-digit days shift the month left with the *100 variants.
    const int month_key = state.day < 10 ? state.month : state.month * 100;
    const std::string month = std::to_string(month_key);
    const std::string day = std::to_string(state.day);
    const std::string& weekday = state.weekday;

    // The Rainmeter source layer order: Bottom (shadow/background), middle
    // (inverted cut-out) and Top (colored face). Assets are decoded directly at
    // the surface size: a 400 px widget no longer keeps thirteen 835x653
    // surfaces in RAM.
    art.core[0] = load_calendar_asset(assets_root, "Day/" + version + "/" + day + "Bottom.png", width, height);
    art.core[1] = load_calendar_asset(assets_root, "Month/" + version + "/" + month + "Bottom.png", width, height);
    art.core[2] = load_calendar_asset(assets_root, "Week/" + version + "/" + weekday + "Bottom.png", width, height);
    art.core[3] = load_calendar_asset(assets_root, "ToD/" + version + "/" + std::to_string(state.time_of_day) + ".png", width, height);
    art.core[4] = load_calendar_asset(assets_root, "Day/" + version + "/" + day + ".png", width, height);
    art.core[5] = load_calendar_asset(assets_root, "Month/" + version + "/" + month + ".png", width, height);
    art.core[6] = load_calendar_asset(assets_root, "Week/" + version + "/" + weekday + ".png", width, height);
    art.core[7] = load_calendar_asset(assets_root, "Day/" + version + "/" + day + "Top.png", width, height);
    art.core[8] = load_calendar_asset(assets_root, "Month/" + version + "/" + month + "Top.png", width, height);
    art.core[9] = load_calendar_asset(assets_root, "Week/" + version + "/" + weekday + "Top.png", width, height);

    art.has_core = art.core[0] && art.core[1] && art.core[2] &&
                   art.core[4] && art.core[5] && art.core[6] &&
                   art.core[7] && art.core[8] && art.core[9];
    if (!art.has_core) {
        // Keep one complete rendering path: if any core layer is unavailable,
        // drop the rest and use the procedural Persona 5 fallback.
        art.clear();
        return;
    }

    if (state.has_weather && !state.weather_icon.empty()) {
        const int base = state.day < 10 ? 3 : 0;
        for (int i = 0; i < 3; ++i) {
            const int frame = base + i;
            art.weather[i] = load_calendar_asset(
                assets_root,
                "Weather/icons" + std::to_string(frame) + "/" +
                version + "/" + state.weather_icon + ".png", width, height);
        }
        art.has_weather = art.weather[0] || art.weather[1] || art.weather[2];
    }
}

void draw_calendar(cairo_t* cr, int width, int height, const CalendarState& state,
                   const CalendarArt& art, const LoadedFonts& fonts) {
    if (width <= 0 || height <= 0) return;

    if (art.has_core) {
        paint_surface(cr, art.core[0].get());
        paint_surface(cr, art.core[1].get());
        paint_surface(cr, art.core[2].get());
        paint_surface(cr, art.core[3].get());

        if (state.has_weather) {
            if (art.has_weather) {
                const int base = state.day < 10 ? 3 : 0;
                const int index = std::clamp(state.weather_frame - base, 0, 2);
                paint_surface(cr, art.weather[index].get());
                if (!art.weather[index]) {
                    // A partial asset set should still show something useful.
                    for (const auto& surface : art.weather) {
                        if (surface) {
                            paint_surface(cr, surface.get());
                            break;
                        }
                    }
                }
            } else {
                cairo_save(cr);
                cairo_scale(cr, static_cast<double>(width) / kDesignW,
                            static_cast<double>(height) / kDesignH);
                draw_fallback_weather(cr, 710.0, 132.0, 128.0, state.weather_icon);
                cairo_restore(cr);
            }
        }

        paint_surface(cr, art.core[4].get());
        paint_surface(cr, art.core[5].get());
        paint_surface(cr, art.core[6].get());
        paint_surface(cr, art.core[7].get());
        paint_surface(cr, art.core[8].get());
        paint_surface(cr, art.core[9].get());
    } else {
        cairo_save(cr);
        cairo_scale(cr, static_cast<double>(width) / kDesignW,
                    static_cast<double>(height) / kDesignH);
        draw_fallback_calendar(cr, state, fonts);
        cairo_restore(cr);
    }
}

} // namespace


CalendarState compute_calendar_state(int64_t wall_ms, int64_t monotonic_ms,
                                     const CalendarConfig& config,
                                     const NoctaliaWeather& weather) {
    CalendarState state;

    const std::time_t seconds = static_cast<std::time_t>(wall_ms / 1000);
    struct tm local {};
    if (localtime_r(&seconds, &local) == nullptr) {
        return state;
    }

    state.year = local.tm_year + 1900;
    state.month = local.tm_mon + 1;
    state.day = local.tm_mday;
    state.weekday_index = std::clamp(local.tm_wday, 0, 6);
    state.weekday = kWeekdays[state.weekday_index];

    const DayTimeline timeline{
        weather.sunrise_minutes >= 0 ? weather.sunrise_minutes : 6 * 60,
        weather.sunset_minutes >= 0 ? weather.sunset_minutes : 18 * 60,
    };
    const DayPhase phase = day_phase_for(wall_ms, timeline);
    state.time_of_day = calendar_time_of_day_for_phase(phase);

    if (config.version == 1) {
        state.version = 1;
    } else if (config.version == 2) {
        state.version = (state.month <= 2) ? 1 : 0;
    } else {
        state.version = 0;
    }

    if (config.weather && weather.available && weather.weather_code >= 0) {
        state.has_weather = true;
        state.weather_icon = weather.icon_code();
        const int base = state.day < 10 ? 3 : 0;
        const int64_t frame_phase = (monotonic_ms < 0 ? 0 : monotonic_ms / 450) % 3;
        state.weather_frame = base + static_cast<int>(frame_phase);
    }

    return state;
}

bool calendar_state_equal(const CalendarState& a, const CalendarState& b) {
    return a.year == b.year && a.month == b.month && a.day == b.day &&
           a.weekday_index == b.weekday_index && a.weekday == b.weekday &&
           a.time_of_day == b.time_of_day && a.version == b.version &&
           a.has_weather == b.has_weather && a.weather_icon == b.weather_icon &&
           a.weather_frame == b.weather_frame;
}

CalendarSurface::CalendarSurface(wl_compositor* compositor,
                                     zwlr_layer_shell_v1* layer_shell,
                                     wl_shm* shm,
                                     wl_output* output,
                                     const LoadedFonts& fonts,
                                     CalendarConfig config,
                                     const AssetConfig& assets)
    : compositor_(compositor),
      layer_shell_(layer_shell),
      shm_(shm),
      output_(output),
      fonts_(fonts),
      config_(std::move(config)) {
    assets_root_ = resolve_calendar_root(assets);
    width_ = std::clamp(config_.width, 120, 2000);
    height_ = std::max(1, static_cast<int>(std::lround(
                               static_cast<double>(width_) * kDesignH / kDesignW)));
}

CalendarSurface::~CalendarSurface() {
    destroy();
}

bool CalendarSurface::init() {
    if (!compositor_ || !layer_shell_ || !shm_) return false;

    surface_ = wl_compositor_create_surface(compositor_);
    if (!surface_) return false;

    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell_, surface_, output_, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        "noctalia-p5calendar");
    if (!layer_surface_) {
        wl_surface_destroy(surface_);
        surface_ = nullptr;
        return false;
    }

    zwlr_layer_surface_v1_add_listener(layer_surface_, &kLayerListener, this);
    apply_anchor();
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layer_surface_, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_set_size(layer_surface_, static_cast<uint32_t>(width_),
                                   static_cast<uint32_t>(height_));

    update_input_region();
    wl_surface_commit(surface_);
    return true;
}

void CalendarSurface::destroy() {
    pool_.release_all();
    art_.reset();
    if (layer_surface_) {
        zwlr_layer_surface_v1_destroy(layer_surface_);
        layer_surface_ = nullptr;
    }
    if (surface_) {
        wl_surface_destroy(surface_);
        surface_ = nullptr;
    }
    configured_ = false;
}

void CalendarSurface::apply_anchor() {
    if (!layer_surface_) return;

    const bool top = config_.position.rfind("top", 0) == 0;
    const bool left = config_.position.find("left") != std::string::npos;

    uint32_t anchors = top ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
                           : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    anchors |= left ? ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
                    : ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_anchor(layer_surface_, anchors);

    const int margin = std::clamp(config_.margin, 0, 500);
    zwlr_layer_surface_v1_set_margin(
        layer_surface_,
        top ? margin : 0,
        left ? 0 : margin,
        top ? 0 : margin,
        left ? margin : 0);
}

void CalendarSurface::update_input_region() {
    if (!compositor_ || !surface_) return;
    // The calendar is a passive desktop decoration: let every pointer event
    // reach the wallpaper and windows below it.
    wl_region* region = wl_compositor_create_region(compositor_);
    wl_surface_set_input_region(surface_, region);
    wl_region_destroy(region);
}

void CalendarSurface::set_persona_active(bool active) {
    if (persona_active_ == active) return;
    persona_active_ = active;
    if (configured_) update(now_ms(), true);
}

void CalendarSurface::set_visible(bool visible) {
    if (visible_ == visible) return;
    visible_ = visible;
    if (configured_) update(now_ms(), true);
}

void CalendarSurface::tick(int64_t now_ms) {
    if (!configured_) return;
    update(now_ms, false);
}

void CalendarSurface::update(int64_t now_ms, bool force_render) {
    if (!configured_) return;

    if (!visible_ || !persona_active_) {
        // Free the calendar art while the widget is hidden and avoid waking up
        // for weather/time changes. One transparent commit is enough.
        if (!blank_rendered_) {
            art_.reset();
            art_key_.clear();
            render();
            blank_rendered_ = true;
        }
        return;
    }
    blank_rendered_ = false;

    const int64_t wall_ms = wall_ms_now();

    if (!config_.weather) {
        weather_ = NoctaliaWeather{};
        last_weather_poll_ms_ = now_ms;
    } else if (last_weather_poll_ms_ == 0 ||
               now_ms - last_weather_poll_ms_ >= 60000) {
        last_weather_poll_ms_ = now_ms;
        weather_ = read_noctalia_weather(wall_ms / 1000,
                                         config_.weather_max_age_seconds);
    }

    const CalendarState next = compute_calendar_state(wall_ms, now_ms, config_, weather_);
    const std::string next_key = calendar_art_key(next, width_, height_);

    bool reloaded = false;
    if (!art_ || next_key != art_key_) {
        auto fresh = std::make_unique<CalendarArt>();
        load_calendar_art(*fresh, next, width_, height_, assets_root_);
        art_ = std::move(fresh);
        art_key_ = next_key;
        reloaded = true;
    }

    const bool changed = !calendar_state_equal(next, state_);
    state_ = next;

    if (force_render || reloaded || changed) {
        render();
    }
}

void CalendarSurface::render() {
    if (!configured_ || !surface_ || !shm_) return;

    ShmSlot* slot = pool_.acquire(shm_, width_, height_);
    if (!slot) return;

    cairo_surface_flush(slot->cairo_surface);
    cairo_t* cr = cairo_create(slot->cairo_surface);

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);

    if (visible_ && persona_active_) {
        if (art_) {
            draw_calendar(cr, width_, height_, state_, *art_, fonts_);
        } else {
            CalendarArt empty;
            draw_calendar(cr, width_, height_, state_, empty, fonts_);
        }
    }

    cairo_destroy(cr);
    cairo_surface_flush(slot->cairo_surface);

    slot->busy = true;
    wl_surface_attach(surface_, slot->buffer, 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, width_, height_);
    wl_surface_commit(surface_);
}

void CalendarSurface::handle_configure(uint32_t serial, uint32_t width, uint32_t height) {
    zwlr_layer_surface_v1_ack_configure(layer_surface_, serial);
    if (width > 0) width_ = static_cast<int>(width);
    if (height > 0) height_ = static_cast<int>(height);
    configured_ = true;
    update(now_ms(), true);
}

void CalendarSurface::handle_closed() {
    configured_ = false;
}

bool render_calendar_preview(const std::filesystem::path& output_path,
                             int64_t wall_ms,
                             const CalendarConfig& config,
                             const LoadedFonts& fonts,
                             const AssetConfig& assets) {
    const int width = std::clamp(config.width, 120, 2000);
    const int height = std::max(1, static_cast<int>(std::lround(
                                       static_cast<double>(width) * kDesignH / kDesignW)));

    NoctaliaWeather weather;
    if (config.weather) {
        weather = read_noctalia_weather(wall_ms / 1000, config.weather_max_age_seconds);
    }

    const CalendarState state = compute_calendar_state(wall_ms, 0, config, weather);

    const std::filesystem::path assets_root = resolve_calendar_root(assets);
    CalendarArt art;
    load_calendar_art(art, state, width, height, assets_root);

    cairo_surface_t* image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(image);
        return false;
    }

    cairo_t* cr = cairo_create(image);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    draw_calendar(cr, width, height, state, art, fonts);

    cairo_destroy(cr);
    cairo_surface_flush(image);

    std::error_code ec;
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path(), ec);
    }

    const cairo_status_t status = cairo_surface_write_to_png(image, output_path.c_str());
    cairo_surface_destroy(image);
    return status == CAIRO_STATUS_SUCCESS;
}

} // namespace p5
