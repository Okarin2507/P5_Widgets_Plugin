#include "wallpaper/wallpaper_automation.hpp"

#include "core/common.hpp"
#include "weather/day_phase.hpp"
#include "weather/noctalia_weather.hpp"

#include <chrono>

namespace p5 {

namespace {

std::filesystem::path default_wallpaper(const char* name) {
    return resolve_asset_path("p5wallpaper", name);
}

int64_t wall_ms_now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace


WallpaperAutomation make_wallpaper_automation(const AssetConfig& assets) {
    WallpaperAutomation automation;
    automation.enabled = assets.wallpaper_auto;
    automation.day = assets.wallpaper_day.empty()
        ? default_wallpaper("p5cityday.png")
        : assets.wallpaper_day;
    automation.afternoon = assets.wallpaper_afternoon.empty()
        ? default_wallpaper("p5cityafternoon.png")
        : assets.wallpaper_afternoon;
    automation.night = assets.wallpaper_night.empty()
        ? default_wallpaper("p5citynight.png")
        : assets.wallpaper_night;
    return automation;
}

void tick_wallpaper(WallpaperAutomation& automation, int64_t monotonic_ms) {
    if (!automation.enabled) return;

    if (automation.last_check_ms != 0 &&
        monotonic_ms - automation.last_check_ms < 60000) {
        return;
    }
    automation.last_check_ms = monotonic_ms;

    const int64_t wall_ms = wall_ms_now();
    // Sunrise/sunset are daily values; accept a 24-hour-old cache so the
    // wallpaper does not stop switching when weather polling is slow.
    const NoctaliaWeather weather = read_noctalia_weather(wall_ms / 1000, 24 * 3600);
    const DayTimeline timeline{
        weather.sunrise_minutes >= 0 ? weather.sunrise_minutes : 6 * 60,
        weather.sunset_minutes >= 0 ? weather.sunset_minutes : 18 * 60,
    };

    const DayPhase phase = day_phase_for(wall_ms, timeline);
    const std::string variant = wallpaper_variant(phase);
    if (variant == automation.last_variant) return;

    const std::filesystem::path* target = nullptr;
    if (variant == "day") {
        target = &automation.day;
    } else if (variant == "afternoon") {
        target = &automation.afternoon;
    } else {
        target = &automation.night;
    }

    std::error_code ec;
    if (!target->empty() && std::filesystem::exists(*target, ec)) {
        // A synchronous call lets us retry every minute if Noctalia was not
        // ready yet at login, instead of silently skipping the whole phase.
        if (run_command_sync({"noctalia", "msg", "wallpaper-set", target->string()}) == 0) {
            automation.last_variant = variant;
        }
    }
}

} // namespace p5
