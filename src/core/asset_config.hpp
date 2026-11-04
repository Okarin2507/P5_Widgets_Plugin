#pragma once

#include <filesystem>
#include <string>

namespace p5 {

// Optional overrides written by the Noctalia plugin service to
// ~/.config/noctalia/p5-rice/assets.conf. Empty paths mean "use the bundled
// default". The daemon reads this once at startup; the plugin restarts the
// user service when a setting changes.
struct AssetConfig {
    std::filesystem::path notification_banner;
    std::filesystem::path notification_sound;
    std::filesystem::path notification_font;
    std::filesystem::path ui_font;
    std::filesystem::path music_font;
    std::filesystem::path music_banner;
    std::filesystem::path music_character_frame;
    std::string music_character_name;
    std::filesystem::path calendar_assets_dir;

    bool respect_noctalia_dnd = true;
    // Persona banner auto-dismiss seconds; 0 = auto (app expire_timeout or 6s).
    int notification_timeout_seconds = 0;
    // Notification sound volume in percent (0 = silent, 100 = default).
    int notification_volume = 100;
    // Don't click-open the sender while its window is hidden in Umbriel's
    // scratchpad (opening would yank the stowed window back onto screen).
    bool notification_scratchpad_block = true;
    bool wallpaper_auto = true;
    std::filesystem::path wallpaper_day;
    std::filesystem::path wallpaper_afternoon;
    std::filesystem::path wallpaper_night;
};

std::filesystem::path asset_config_path();
std::filesystem::path expand_user_path(const std::filesystem::path& path);

AssetConfig read_asset_config();

} // namespace p5
