#pragma once

#include "core/asset_config.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace p5 {

struct WallpaperAutomation {
    bool enabled = true;
    std::filesystem::path day;
    std::filesystem::path afternoon;
    std::filesystem::path night;
    std::string last_variant;
    int64_t last_check_ms = 0;
};

WallpaperAutomation make_wallpaper_automation(const AssetConfig& assets);
void tick_wallpaper(WallpaperAutomation& automation, int64_t monotonic_ms);

} // namespace p5
