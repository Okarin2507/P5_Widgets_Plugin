#pragma once

#include <filesystem>
#include <string>

namespace p5 {

// Optional overrides written by the Noctalia plugin service to
// ~/.config/noctalia/p5-rice/assets.conf. Empty paths mean "use the bundled
// default". The daemon reads this once at startup; the plugin restarts the
// user service when an asset setting changes.
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
    bool wallpaper_auto = true;
    std::filesystem::path wallpaper_day;
    std::filesystem::path wallpaper_afternoon;
    std::filesystem::path wallpaper_night;
};

std::filesystem::path asset_config_path();
std::filesystem::path expand_user_path(const std::filesystem::path& path);

AssetConfig read_asset_config();

} // namespace p5
