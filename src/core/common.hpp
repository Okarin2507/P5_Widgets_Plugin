#pragma once

#include <cairo/cairo.h>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace p5 {

struct Color {
    double r = 1.0;
    double g = 1.0;
    double b = 1.0;
    double a = 1.0;

    static constexpr Color from_rgba8(uint8_t r8, uint8_t g8, uint8_t b8, uint8_t a8 = 255) {
        return {r8 / 255.0, g8 / 255.0, b8 / 255.0, a8 / 255.0};
    }

    static Color from_hex(std::string_view hex);
};

// Monotonic clock in milliseconds
int64_t now_ms();

// Easing functions matching QtQuick Easing.*
double ease_out_back(double t, double overshoot = 1.70158);
double ease_out_cubic(double t);
double ease_in_out_sine(double t);

// HTML/Pango notification markup stripper matching Main.qml stripMarkup()
std::string strip_markup(std::string_view s);

// Time formatter matching PersonaMusicWidget.qml fmt(s)
std::string format_mm_ss(double seconds);

// UTF-8 codepoint splitter for per-character Jitter rendering (NFC-normalized & grapheme-aware)
std::vector<std::string> utf8_split_chars(std::string_view str);
bool is_pure_ascii(std::string_view str);

// Asset & state path resolution
std::filesystem::path config_home();
std::filesystem::path resolve_asset_path(std::string_view plugin_subdir, std::string_view rel_path);

// Font registration (extracts exact FC_FAMILY name like QML FontLoader.name)
struct AssetConfig;

struct LoadedFonts {
    std::string notify_p5_main;     // fonts/p5main.ttf
    std::string notify_ux_regular;  // fonts/PersonifiedUXRegular.ttf
    std::string music_p5;      // fonts/p5font.ttf
    std::string music_ux_regular;   // fonts/PersonifiedUXRegular.ttf
    bool notify_p5_ready = false;
    bool music_p5_ready = false;
};

LoadedFonts register_all_custom_fonts(const AssetConfig& assets);

// Image loading into premultiplied CAIRO_FORMAT_ARGB32 surface
cairo_surface_t* load_image_surface(const std::filesystem::path& path);
cairo_surface_t* load_image_surface_from_memory(std::span<const uint8_t> data);

// Procedural P5 comic-style fallbacks when user PNGs are absent
cairo_surface_t* create_fallback_notify_banner(int width = 700, int height = 190);
cairo_surface_t* create_fallback_music_banner(int width = 1223, int height = 344);
cairo_surface_t* create_fallback_music_character(int width = 352, int height = 272);

// Non-blocking process helpers
void spawn_detached(const std::vector<std::string>& args);
int run_command_sync(const std::vector<std::string>& args);
std::string run_command_capture(const std::vector<std::string>& args, int* exit_code = nullptr);
// Plays a sound file through pw-play/paplay/aplay, detached. volume_percent is
// 0-100; 0 skips playback entirely. Does not touch the system volume.
void play_sound_async(const std::filesystem::path& sound_path, int volume_percent = 100);


} // namespace p5

