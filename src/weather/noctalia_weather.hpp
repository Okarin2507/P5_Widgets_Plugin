#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace p5 {

// Read-only bridge to the weather snapshot Noctalia already keeps in
// $XDG_CACHE_HOME/noctalia/weather.json.  The native daemon deliberately does
// not fetch the network itself: Noctalia's WeatherService remains the single
// owner of location resolution, polling and caching.
struct NoctaliaWeather {
    bool available = false;
    int weather_code = -1;      // WMO code (0..99)
    bool is_day = true;         // Noctalia's current.is_day flag
    int sunrise_minutes = -1;   // local minutes from forecast_days[0].sunrise_iso
    int sunset_minutes = -1;    // local minutes from forecast_days[0].sunset_iso
    int64_t fetched_at = 0;     // Unix seconds
    std::filesystem::path cache_path;

    // Maps the WMO code to the OpenWeatherMap-style icon names used by the
    // Persona 5 calendar assets, e.g. "01d", "04n", "13d".
    std::string icon_code() const;
};

std::filesystem::path noctalia_cache_dir();
std::filesystem::path noctalia_weather_cache_path();

// Pure parser used by read_noctalia_weather() and by the preview/test path.
NoctaliaWeather parse_noctalia_weather_json(std::string_view json, int64_t now_unix,
                                            int64_t max_age_seconds);

// Reads the cache and rejects snapshots older than max_age_seconds.
// A non-positive max_age_seconds disables the age check.
NoctaliaWeather read_noctalia_weather(int64_t now_unix, int64_t max_age_seconds);

std::string weather_icon_code(int wmo_code, bool is_day);

} // namespace p5
