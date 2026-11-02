#include "common.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>
#include <fstream>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <numbers>
#include <sys/wait.h>
#include <unistd.h>

namespace p5 {

Color Color::from_hex(std::string_view hex) {
    if (!hex.empty() && hex.front() == '#') {
        hex.remove_prefix(1);
    }
    auto parse_byte = [](std::string_view s) -> uint8_t {
        unsigned int val = 0;
        for (char c : s) {
            val <<= 4;
            if (c >= '0' && c <= '9') val |= (c - '0');
            else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
        }
        return static_cast<uint8_t>(val & 0xFF);
    };
    if (hex.size() == 6) {
        return from_rgba8(
            parse_byte(hex.substr(0, 2)),
            parse_byte(hex.substr(2, 2)),
            parse_byte(hex.substr(4, 2)),
            255
        );
    }
    if (hex.size() == 8) {
        return from_rgba8(
            parse_byte(hex.substr(0, 2)),
            parse_byte(hex.substr(2, 2)),
            parse_byte(hex.substr(4, 2)),
            parse_byte(hex.substr(6, 2))
        );
    }
    return {1.0, 1.0, 1.0, 1.0};
}

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

double ease_out_back(double t, double overshoot) {
    t = std::clamp(t, 0.0, 1.0);
    const double c1 = overshoot;
    const double c3 = c1 + 1.0;
    const double tm1 = t - 1.0;
    return 1.0 + c3 * tm1 * tm1 * tm1 + c1 * tm1 * tm1;
}

double ease_out_cubic(double t) {
    t = std::clamp(t, 0.0, 1.0);
    const double inv = 1.0 - t;
    return 1.0 - inv * inv * inv;
}

double ease_in_out_sine(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return -(std::cos(std::numbers::pi * t) - 1.0) / 2.0;
}

namespace {
void replace_all(std::string& s, std::string_view from, std::string_view to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}
} // namespace

std::string strip_markup(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    int depth = 0;
    for (char c : s) {
        if (c == '<') {
            depth++;
        } else if (c == '>' && depth > 0) {
            depth--;
        } else if (depth == 0) {
            out.push_back(c);
        }
    }
    if (depth > 0) {
        out.assign(s);
    }
    replace_all(out, "&lt;", "<");
    replace_all(out, "&gt;", ">");
    replace_all(out, "&quot;", "\"");
    replace_all(out, "&#39;", "'");
    replace_all(out, "&apos;", "'");
    replace_all(out, "&nbsp;", " ");
    replace_all(out, "&amp;", "&");

    if (!out.empty() && g_utf8_validate(out.data(), static_cast<gssize>(out.size()), nullptr)) {
        if (gchar* norm = g_utf8_normalize(out.data(), static_cast<gssize>(out.size()), G_NORMALIZE_DEFAULT_COMPOSE)) {
            out.assign(norm);
            g_free(norm);
        }
    }
    return out;
}

std::string format_mm_ss(double seconds) {
    int total = std::max(0, static_cast<int>(std::floor(seconds)));
    int m = total / 60;
    int sec = total % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", m, sec);
    return buf;
}

bool is_pure_ascii(std::string_view str) {
    for (unsigned char c : str) {
        if (c >= 0x80) return false;
    }
    return true;
}

std::vector<std::string> utf8_split_chars(std::string_view str) {
    std::string normalized;
    std::string_view src = str;
    if (!str.empty() && g_utf8_validate(str.data(), static_cast<gssize>(str.size()), nullptr)) {
        if (gchar* norm = g_utf8_normalize(str.data(), static_cast<gssize>(str.size()), G_NORMALIZE_DEFAULT_COMPOSE)) {
            normalized.assign(norm);
            g_free(norm);
            src = normalized;
        }
    }

    std::vector<std::string> result;
    const char* p = src.data();
    const char* end = src.data() + src.size();
    bool prev_was_zwj = false;
    bool prev_was_ri = false;

    while (p < end) {
        gunichar cp = g_utf8_get_char_validated(p, end - p);
        if (cp == static_cast<gunichar>(-1) || cp == static_cast<gunichar>(-2)) {
            result.emplace_back(p, 1);
            ++p;
            prev_was_zwj = false;
            prev_was_ri = false;
            continue;
        }
        const char* next = g_utf8_next_char(p);
        const bool is_zwj = (cp == 0x200D);
        const bool is_vs = (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0100 && cp <= 0xE01EF);
        const bool is_skin_tone = (cp >= 0x1F3FB && cp <= 0x1F3FF);
        const bool is_ri = (cp >= 0x1F1E6 && cp <= 0x1F1FF);
        const bool join_with_prev = !result.empty() && (
            g_unichar_ismark(cp) ||
            is_zwj ||
            prev_was_zwj ||
            is_vs ||
            is_skin_tone ||
            (is_ri && prev_was_ri)
        );

        if (join_with_prev) {
            result.back().append(p, static_cast<size_t>(next - p));
            prev_was_ri = false; // Pair of 2 regional indicators completes 1 flag
        } else {
            result.emplace_back(p, static_cast<size_t>(next - p));
            prev_was_ri = is_ri;
        }
        prev_was_zwj = is_zwj;
        p = next;
    }
    return result;
}

std::filesystem::path config_home() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg);
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config";
    }
    return "/tmp";
}

std::filesystem::path profile_file_path() {
    auto noctalia_prof = config_home() / "noctalia" / "p5-profile";
    if (std::filesystem::exists(noctalia_prof)) {
        return noctalia_prof;
    }
    auto dms_prof = config_home() / "dms" / "p5notify-profile";
    if (std::filesystem::exists(dms_prof)) {
        return dms_prof;
    }
    return noctalia_prof;
}

std::filesystem::path resolve_asset_path(std::string_view plugin_subdir, std::string_view rel_path) {
    const auto cfg = config_home();
    std::vector<std::filesystem::path> candidates = {
        cfg / "noctalia" / "p5-rice" / plugin_subdir / rel_path,
        cfg / "noctalia" / "plugins" / plugin_subdir / rel_path,
        cfg / "dms" / "plugins" / plugin_subdir / rel_path,
    };
    if (const char* home = std::getenv("HOME"); home && *home) {
        candidates.push_back(std::filesystem::path(home) / ".local" / "share" / "p5-rice" / plugin_subdir / rel_path);
    }
    candidates.push_back(std::filesystem::current_path() / "dms" / "plugins" / plugin_subdir / rel_path);
    candidates.push_back(std::filesystem::current_path() / "assets" / plugin_subdir / rel_path);

    for (const auto& p : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) {
            return p;
        }
    }
    return candidates.front();
}

namespace {
std::string register_single_font(FcConfig* cfg, const std::filesystem::path& path, bool& out_ready, std::string_view fallback) {
    out_ready = false;
    std::error_code ec;
    if (!cfg || !std::filesystem::exists(path, ec)) {
        return std::string(fallback);
    }

    const FcChar8* fc_path = reinterpret_cast<const FcChar8*>(path.c_str());
    FcConfigAppFontAddFile(cfg, fc_path);

    int count = 0;
    FcBlanks* blanks = FcConfigGetBlanks(cfg);
    FcPattern* pat = FcFreeTypeQuery(fc_path, 0, blanks, &count);
    std::string family(fallback);
    if (pat) {
        FcChar8* fam = nullptr;
        if (FcPatternGetString(pat, FC_FAMILY, 0, &fam) == FcResultMatch && fam) {
            family = reinterpret_cast<const char*>(fam);
            out_ready = true;
        }
        FcPatternDestroy(pat);
    }
    return family;
}
} // namespace

LoadedFonts register_all_custom_fonts() {
    FcConfig* cfg = FcInitLoadConfigAndFonts();
    LoadedFonts fonts;
    bool dummy = false;
    fonts.notify_p5_main = register_single_font(
        cfg,
        resolve_asset_path("p5notify", "fonts/Persona5main.ttf"),
        fonts.notify_p5_ready,
        "Impact"
    );
    fonts.notify_ux_regular = register_single_font(
        cfg,
        resolve_asset_path("p5notify", "fonts/PersonifiedUXRegular.ttf"),
        dummy,
        "Sans"
    );
    fonts.music_persona = register_single_font(
        cfg,
        resolve_asset_path("personaMusic", "fonts/personafont.ttf"),
        fonts.music_persona_ready,
        "Impact"
    );
    fonts.music_ux_regular = register_single_font(
        cfg,
        resolve_asset_path("personaMusic", "fonts/PersonifiedUXRegular.ttf"),
        dummy,
        fonts.notify_ux_regular
    );
    if (cfg) {
        FcConfigSetCurrent(cfg);
    }
    return fonts;
}

namespace {
cairo_surface_t* pixbuf_to_cairo_surface(GdkPixbuf* pixbuf) {
    if (!pixbuf) return nullptr;
    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const int stride = gdk_pixbuf_get_rowstride(pixbuf);
    const int n_channels = gdk_pixbuf_get_n_channels(pixbuf);
    const guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        return nullptr;
    }

    cairo_surface_flush(surface);
    unsigned char* dst_data = cairo_image_surface_get_data(surface);
    const int dst_stride = cairo_image_surface_get_stride(surface);

    for (int y = 0; y < height; ++y) {
        const guchar* src_row = pixels + y * stride;
        uint32_t* dst_row = reinterpret_cast<uint32_t*>(dst_data + y * dst_stride);
        for (int x = 0; x < width; ++x) {
            uint8_t r = src_row[x * n_channels + 0];
            uint8_t g = src_row[x * n_channels + 1];
            uint8_t b = src_row[x * n_channels + 2];
            uint8_t a = (n_channels >= 4) ? src_row[x * n_channels + 3] : 255;
            // Premultiply alpha for CAIRO_FORMAT_ARGB32
            uint32_t pr = (static_cast<uint32_t>(r) * a + 127) / 255;
            uint32_t pg = (static_cast<uint32_t>(g) * a + 127) / 255;
            uint32_t pb = (static_cast<uint32_t>(b) * a + 127) / 255;
            dst_row[x] = (static_cast<uint32_t>(a) << 24) | (pr << 16) | (pg << 8) | pb;
        }
    }
    cairo_surface_mark_dirty(surface);
    return surface;
}
} // namespace

cairo_surface_t* load_image_surface(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return nullptr;
    }
    GError* err = nullptr;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file(path.c_str(), &err);
    if (!pixbuf) {
        if (err) g_error_free(err);
        return nullptr;
    }
    cairo_surface_t* surf = pixbuf_to_cairo_surface(pixbuf);
    g_object_unref(pixbuf);
    return surf;
}

cairo_surface_t* load_image_surface_from_memory(std::span<const uint8_t> data) {
    if (data.empty()) return nullptr;
    GError* err = nullptr;
    GInputStream* stream = g_memory_input_stream_new_from_data(data.data(), data.size(), nullptr);
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_stream(stream, nullptr, &err);
    g_object_unref(stream);
    if (!pixbuf) {
        if (err) g_error_free(err);
        return nullptr;
    }
    cairo_surface_t* surf = pixbuf_to_cairo_surface(pixbuf);
    g_object_unref(pixbuf);
    return surf;
}

cairo_surface_t* create_fallback_notify_banner(int width, int height) {
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create(surf);

    // Outer crimson jagged P5 shard
    cairo_set_source_rgba(cr, 0.90, 0.0, 0.07, 0.96);
    cairo_move_to(cr, width * 0.14, height * 0.22);
    cairo_line_to(cr, width * 0.95, height * 0.08);
    cairo_line_to(cr, width * 0.89, height * 0.92);
    cairo_line_to(cr, width * 0.10, height * 0.86);
    cairo_close_path(cr);
    cairo_fill(cr);

    // White jagged rim
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 5.0);
    cairo_move_to(cr, width * 0.18, height * 0.26);
    cairo_line_to(cr, width * 0.86, height * 0.22);
    cairo_line_to(cr, width * 0.84, height * 0.82);
    cairo_line_to(cr, width * 0.20, height * 0.80);
    cairo_close_path(cr);
    cairo_stroke_preserve(cr);

    // Inner black dialogue box (centered around bodyCX=0.536, bodyCY=0.538)
    cairo_set_source_rgba(cr, 0.04, 0.05, 0.06, 0.98);
    cairo_fill(cr);

    // Upper-right tilted white tag bubble (around tagCX=0.789, tagCY=0.268, rot=15.7 deg)
    cairo_save(cr);
    cairo_translate(cr, width * 0.789, height * 0.268);
    cairo_rotate(cr, 15.7 * std::numbers::pi / 180.0);
    cairo_move_to(cr, -width * 0.10, -18);
    cairo_line_to(cr, width * 0.10, -21);
    cairo_line_to(cr, width * 0.09, 18);
    cairo_line_to(cr, -width * 0.09, 16);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    cairo_set_line_width(cr, 3.5);
    cairo_stroke(cr);
    cairo_restore(cr);

    cairo_destroy(cr);
    return surf;
}

cairo_surface_t* create_fallback_music_banner(int width, int height) {
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create(surf);

    // Red shadow polygon
    cairo_set_source_rgba(cr, 0.90, 0.0, 0.07, 0.95);
    cairo_move_to(cr, 95, 70);
    cairo_line_to(cr, width - 35, 45);
    cairo_line_to(cr, width - 70, height - 20);
    cairo_line_to(cr, 120, height - 35);
    cairo_close_path(cr);
    cairo_fill(cr);

    // Main black dialogue polygon with white sticker border
    cairo_move_to(cr, 115, 85);
    cairo_line_to(cr, width - 65, 65);
    cairo_line_to(cr, width - 95, height - 40);
    cairo_line_to(cr, 140, height - 55);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.03, 0.04, 0.05, 0.98);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 7.0);
    cairo_stroke(cr);

    // White name tag polygon around (268, 78) tilted -15 deg
    cairo_save(cr);
    cairo_translate(cr, 268, 78);
    cairo_rotate(cr, -15.0 * std::numbers::pi / 180.0);
    cairo_move_to(cr, -110, -28);
    cairo_line_to(cr, 115, -32);
    cairo_line_to(cr, 105, 28);
    cairo_line_to(cr, -100, 26);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    cairo_set_line_width(cr, 5.0);
    cairo_stroke(cr);
    cairo_restore(cr);

    // Right-side white wedge for timeText around (1124, 188) tilted -24 deg
    cairo_save(cr);
    cairo_translate(cr, 1124, 188);
    cairo_rotate(cr, -24.0 * std::numbers::pi / 180.0);
    cairo_move_to(cr, -75, -26);
    cairo_line_to(cr, 80, -30);
    cairo_line_to(cr, 70, 26);
    cairo_line_to(cr, -68, 24);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    cairo_set_line_width(cr, 5.0);
    cairo_stroke(cr);
    cairo_restore(cr);

    cairo_destroy(cr);
    return surf;
}

cairo_surface_t* create_fallback_futaba_frame(int width, int height) {
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create(surf);

    // Stylized P5 confidant portrait diamond/polygon frame
    cairo_move_to(cr, 38, 22);
    cairo_line_to(cr, width - 24, 12);
    cairo_line_to(cr, width - 48, height - 18);
    cairo_line_to(cr, 18, height - 34);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.08, 0.09, 0.11, 0.96);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.90, 0.0, 0.07, 1.0);
    cairo_set_line_width(cr, 8.0);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 3.0);
    cairo_stroke(cr);

    cairo_destroy(cr);
    return surf;
}

void spawn_detached(const std::vector<std::string>& args) {
    if (args.empty()) return;
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        pid_t pid2 = fork();
        if (pid2 < 0) _exit(127);
        if (pid2 > 0) _exit(0);
        setsid();
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& s : args) {
            argv.push_back(const_cast<char*>(s.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
}

void play_sound_async(const std::filesystem::path& sound_path) {
    std::error_code ec;
    if (!std::filesystem::exists(sound_path, ec)) return;
    spawn_detached({
        "sh",
        "-c",
        "pw-play \"$1\" >/dev/null 2>&1 || paplay \"$1\" >/dev/null 2>&1 || aplay -q \"$1\" >/dev/null 2>&1",
        "sh",
        sound_path.string()
    });
}

std::string read_active_profile() {
    std::vector<std::filesystem::path> files = {
        config_home() / "noctalia" / "p5-profile",
        config_home() / "dms" / "p5notify-profile",
    };
    for (const auto& f : files) {
        std::ifstream in(f);
        if (!in.is_open()) continue;
        std::string line;
        std::getline(in, line);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
            start++;
        }
        line = line.substr(start);
        if (!line.empty()) {
            return line;
        }
    }
    // Default to "persona" if no profile file exists yet so the user sees banners immediately
    return "persona";
}

} // namespace p5

