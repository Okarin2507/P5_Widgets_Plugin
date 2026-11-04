#include "core/asset_config.hpp"

#include "core/common.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string_view>

namespace p5 {

namespace {

std::string trim(std::string_view input) {
    size_t begin = 0;
    size_t end = input.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(input[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) --end;
    return std::string(input.substr(begin, end - begin));
}

std::string unquote(std::string value) {
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            value = value.substr(1, value.size() - 2);
        }
    }
    return value;
}

bool parse_bool(std::string value, bool fallback) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "1" || value == "true" || value == "yes" || value == "on") return true;
    if (value == "0" || value == "false" || value == "no" || value == "off") return false;
    return fallback;
}

int parse_int(std::string_view value, int fallback) {
    std::string trimmed = trim(value);
    if (trimmed.empty()) return fallback;
    char* end = nullptr;
    const long parsed = std::strtol(trimmed.c_str(), &end, 10);
    if (end == trimmed.c_str()) return fallback;
    return static_cast<int>(std::clamp<long>(parsed, -2147483648L, 2147483647L));
}

} // namespace

std::filesystem::path asset_config_path() {
    return config_home() / "noctalia" / "p5-rice" / "assets.conf";
}

std::filesystem::path expand_user_path(const std::filesystem::path& path) {
    if (path.empty()) return {};

    const std::string text = path.string();
    if (text == "~" || text.rfind("~/", 0) == 0) {
        if (const char* home = std::getenv("HOME"); home && *home) {
            if (text == "~") return std::filesystem::path(home);
            return std::filesystem::path(home) / text.substr(2);
        }
    }

    if (path.is_relative()) {
        return config_home() / "noctalia" / "p5-rice" / path;
    }
    return path;
}

AssetConfig read_asset_config() {
    AssetConfig config;

    std::ifstream in(asset_config_path());
    if (!in.is_open()) return config;

    std::string line;
    while (std::getline(in, line)) {
        const std::string cleaned = trim(line);
        if (cleaned.empty() || cleaned.front() == '#') continue;

        const size_t equals = cleaned.find('=');
        if (equals == std::string::npos) continue;

        const std::string key = trim(std::string_view(cleaned).substr(0, equals));
        const std::string value = unquote(trim(std::string_view(cleaned).substr(equals + 1)));
        if (key.empty()) continue;

        if (key == "notification_banner") {
            config.notification_banner = expand_user_path(value);
        } else if (key == "notification_sound") {
            config.notification_sound = expand_user_path(value);
        } else if (key == "notification_font") {
            config.notification_font = expand_user_path(value);
        } else if (key == "ui_font") {
            config.ui_font = expand_user_path(value);
        } else if (key == "music_font") {
            config.music_font = expand_user_path(value);
        } else if (key == "music_banner") {
            config.music_banner = expand_user_path(value);
        } else if (key == "music_character_frame") {
            config.music_character_frame = expand_user_path(value);
        } else if (key == "music_character_name") {
            config.music_character_name = value;
        } else if (key == "calendar_assets_dir") {
            config.calendar_assets_dir = expand_user_path(value);
        } else if (key == "respect_noctalia_dnd") {
            config.respect_noctalia_dnd = parse_bool(value, config.respect_noctalia_dnd);
        } else if (key == "notification_timeout_seconds") {
            config.notification_timeout_seconds =
                std::clamp(parse_int(value, config.notification_timeout_seconds), 0, 120);
        } else if (key == "notification_volume") {
            config.notification_volume =
                std::clamp(parse_int(value, config.notification_volume), 0, 100);
        } else if (key == "notification_scratchpad_block") {
            config.notification_scratchpad_block =
                parse_bool(value, config.notification_scratchpad_block);
        } else if (key == "wallpaper_auto_enabled") {
            config.wallpaper_auto = parse_bool(value, config.wallpaper_auto);
        } else if (key == "wallpaper_day") {
            config.wallpaper_day = expand_user_path(value);
        } else if (key == "wallpaper_afternoon") {
            config.wallpaper_afternoon = expand_user_path(value);
        } else if (key == "wallpaper_night") {
            config.wallpaper_night = expand_user_path(value);
        }
    }

    return config;
}

} // namespace p5
