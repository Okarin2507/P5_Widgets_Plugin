#include "weather/day_phase.hpp"

#include <algorithm>
#include <ctime>

namespace p5 {

DayPhase day_phase_for(int64_t wall_ms, const DayTimeline& timeline) {
    const std::time_t seconds = static_cast<std::time_t>(wall_ms / 1000);
    struct tm local {};
    if (localtime_r(&seconds, &local) == nullptr) {
        return DayPhase::Daytime;
    }

    const int minutes = local.tm_hour * 60 + local.tm_min;
    int sunrise = std::clamp(timeline.sunrise_minutes, 0, 1439);
    int sunset = std::clamp(timeline.sunset_minutes, 0, 1439);
    if (sunrise <= 0 || sunset <= 0 || sunrise >= sunset) {
        sunrise = 6 * 60;
        sunset = 18 * 60;
    }
    const int dusk = std::min(1439, sunset + 22); // timeline: ~5:28 -> ~5:50 PM

    if (minutes < 60) return DayPhase::Midnight;       // 00:00 - 01:00
    if (minutes < 180) return DayPhase::LateNight;     // 01:00 - 03:00
    if (minutes < sunrise) return DayPhase::EarlyMorning;
    if (minutes < 600 && minutes >= sunrise) return DayPhase::Morning; // sunrise - 10:00
    if (minutes < 720) return DayPhase::Daytime;       // 10:00 - 12:00
    if (minutes < sunset) return DayPhase::Afternoon;  // 12:00 - sunset
    if (minutes < dusk) return DayPhase::Evening;      // sunset - dusk
    return DayPhase::Night;                            // dusk - midnight
}

const char* day_phase_name(DayPhase phase) {
    switch (phase) {
        case DayPhase::Midnight: return "Midnight";
        case DayPhase::LateNight: return "Late Night";
        case DayPhase::EarlyMorning: return "Early Morning";
        case DayPhase::Morning: return "Morning";
        case DayPhase::Daytime: return "Daytime";
        case DayPhase::Afternoon: return "Afternoon";
        case DayPhase::Evening: return "Evening";
        case DayPhase::Night: return "Night";
    }
    return "Daytime";
}

int calendar_time_of_day_for_phase(DayPhase phase) {
    // The calendar has six ToD images. Midnight/Late Night/Night use the
    // existing "Evening" artwork, matching the original game's lack of a
    // distinct Night label.
    switch (phase) {
        case DayPhase::Midnight: return 1;
        case DayPhase::LateNight: return 1;
        case DayPhase::EarlyMorning: return 2;
        case DayPhase::Morning: return 3;
        case DayPhase::Daytime: return 4;
        case DayPhase::Afternoon: return 5;
        case DayPhase::Evening: return 6;
        case DayPhase::Night: return 1;
    }
    return 4;
}

const char* wallpaper_variant(DayPhase phase) {
    switch (phase) {
        case DayPhase::Morning:
        case DayPhase::Daytime:
            return "day";
        case DayPhase::Afternoon:
        case DayPhase::Evening:
            return "afternoon";
        case DayPhase::Midnight:
        case DayPhase::LateNight:
        case DayPhase::EarlyMorning:
        case DayPhase::Night:
            return "night";
    }
    return "day";
}

} // namespace p5
