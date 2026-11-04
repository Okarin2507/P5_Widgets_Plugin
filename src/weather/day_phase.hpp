#pragma once

#include <cstdint>

namespace p5 {

enum class DayPhase {
    Midnight,
    LateNight,
    EarlyMorning,
    Morning,
    Daytime,
    Afternoon,
    Evening,
    Night,
};

struct DayTimeline {
    int sunrise_minutes = 6 * 60;
    int sunset_minutes = 18 * 60;
};

DayPhase day_phase_for(int64_t wall_ms, const DayTimeline& timeline);
const char* day_phase_name(DayPhase phase);

// Maps the eight wallpaper/weather phases onto the six P5 ToD assets.
int calendar_time_of_day_for_phase(DayPhase phase);

// "day", "afternoon" or "night" for the wallpaper automator.
const char* wallpaper_variant(DayPhase phase);

} // namespace p5
