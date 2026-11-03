#include "weather/noctalia_weather.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace p5 {

namespace {

bool find_integer_after(std::string_view text, std::string_view key, int64_t& out) {
    const auto key_pos = text.find(key);
    if (key_pos == std::string_view::npos) return false;

    auto pos = text.find(':', key_pos + key.size());
    if (pos == std::string_view::npos) return false;
    ++pos;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
                                 text[pos] == '\r' || text[pos] == '\n')) {
        ++pos;
    }

    bool negative = false;
    if (pos < text.size() && text[pos] == '-') {
        negative = true;
        ++pos;
    }
    if (pos >= text.size() || text[pos] < '0' || text[pos] > '9') return false;

    int64_t value = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        value = value * 10 + (text[pos] - '0');
        ++pos;
    }
    out = negative ? -value : value;
    return true;
}

bool find_time_after(std::string_view text, std::string_view key, int& out_minutes) {
    const auto key_pos = text.find(key);
    if (key_pos == std::string_view::npos) return false;

    // ISO-8601 local time: YYYY-MM-DDTHH:MM[:SS]
    const auto t_pos = text.find('T', key_pos + key.size());
    if (t_pos == std::string_view::npos || t_pos + 5 >= text.size()) return false;
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (!digit(text[t_pos + 1]) || !digit(text[t_pos + 2]) || text[t_pos + 3] != ':' ||
        !digit(text[t_pos + 4]) || !digit(text[t_pos + 5])) {
        return false;
    }
    const int hours = (text[t_pos + 1] - '0') * 10 + (text[t_pos + 2] - '0');
    const int minutes = (text[t_pos + 4] - '0') * 10 + (text[t_pos + 5] - '0');
    if (hours > 23 || minutes > 59) return false;
    out_minutes = hours * 60 + minutes;
    return true;
}

bool find_boolean_after(std::string_view text, std::string_view key, bool& out) {
    const auto key_pos = text.find(key);
    if (key_pos == std::string_view::npos) return false;

    auto pos = text.find(':', key_pos + key.size());
    if (pos == std::string_view::npos) return false;
    ++pos;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
                                 text[pos] == '\r' || text[pos] == '\n')) {
        ++pos;
    }

    if (text.substr(pos, 4) == "true") {
        out = true;
        return true;
    }
    if (text.substr(pos, 5) == "false") {
        out = false;
        return true;
    }
    return false;
}

int64_t file_mtime_unix(const std::filesystem::path& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) return 0;
    return static_cast<int64_t>(st.st_mtime);
}

} // namespace

std::filesystem::path noctalia_cache_dir() {
    if (const char* override_dir = std::getenv("P5_NOCTALIA_CACHE_DIR");
        override_dir && *override_dir) {
        return std::filesystem::path(override_dir);
    }
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "noctalia";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".cache" / "noctalia";
    }
    return "/tmp/noctalia";
}

std::filesystem::path noctalia_weather_cache_path() {
    if (const char* override_path = std::getenv("P5_CALENDAR_WEATHER_CACHE");
        override_path && *override_path) {
        return std::filesystem::path(override_path);
    }
    return noctalia_cache_dir() / "weather.json";
}

std::string weather_icon_code(int wmo_code, bool is_day) {
    std::string base;
    if (wmo_code == 0) {
        base = "01";                       // clear sky
    } else if (wmo_code == 1 || wmo_code == 2) {
        base = "02";                       // mainly clear / partly cloudy
    } else if (wmo_code == 3) {
        base = "04";                       // overcast
    } else if (wmo_code == 45 || wmo_code == 48) {
        base = "50";                       // fog / depositing rime fog
    } else if ((wmo_code >= 51 && wmo_code <= 57) ||
               (wmo_code >= 80 && wmo_code <= 82)) {
        base = "09";                       // drizzle / rain showers
    } else if (wmo_code >= 61 && wmo_code <= 67) {
        base = "10";                       // rain / freezing rain
    } else if ((wmo_code >= 71 && wmo_code <= 77) ||
               wmo_code == 85 || wmo_code == 86) {
        base = "13";                       // snow / snow showers
    } else if (wmo_code >= 95 && wmo_code <= 99) {
        base = "11";                       // thunderstorm
    } else {
        base = "01";
    }
    return base + (is_day ? "d" : "n");
}

NoctaliaWeather parse_noctalia_weather_json(std::string_view json, int64_t now_unix,
                                            int64_t max_age_seconds) {
    NoctaliaWeather weather;

    // Restrict current-condition parsing to the "current" object so forecast
    // arrays later in the document cannot shadow it.
    std::string_view current = json;
    const auto current_pos = json.find("\"current\"");
    if (current_pos != std::string_view::npos) {
        const auto units_pos = json.find("\"current_units\"", current_pos);
        if (units_pos != std::string_view::npos && units_pos > current_pos) {
            current = json.substr(current_pos, units_pos - current_pos);
        } else {
            current = json.substr(current_pos);
        }
    }

    int64_t code = -1;
    bool is_day = true;
    if (!find_integer_after(current, "\"weather_code\"", code)) return weather;
    if (code < 0 || code > 99) return weather;

    (void)find_boolean_after(current, "\"is_day\"", is_day);

    int64_t fetched_at = 0;
    (void)find_integer_after(json, "\"fetched_at\"", fetched_at);

    int sunrise = -1;
    int sunset = -1;
    (void)find_time_after(json, "\"sunrise_iso\"", sunrise);
    (void)find_time_after(json, "\"sunset_iso\"", sunset);

    weather.weather_code = static_cast<int>(code);
    weather.is_day = is_day;
    weather.sunrise_minutes = sunrise;
    weather.sunset_minutes = sunset;
    weather.fetched_at = fetched_at;

    if (fetched_at <= 0) return weather;
    if (now_unix > 0 && max_age_seconds > 0) {
        const int64_t age = now_unix - fetched_at;
        // A timestamp slightly in the future is fine (clock skew); anything
        // beyond that means the cache is not trustworthy.
        if (age > max_age_seconds || age < -300) return weather;
    }

    weather.available = true;
    return weather;
}

NoctaliaWeather read_noctalia_weather(int64_t now_unix, int64_t max_age_seconds) {
    NoctaliaWeather weather;
    weather.cache_path = noctalia_weather_cache_path();

    std::error_code ec;
    if (!std::filesystem::exists(weather.cache_path, ec)) return weather;

    std::ifstream in(weather.cache_path, std::ios::binary);
    if (!in.is_open()) return weather;

    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string json = buf.str();
    if (json.empty()) return weather;

    weather = parse_noctalia_weather_json(json, now_unix, max_age_seconds);
    weather.cache_path = noctalia_weather_cache_path();
    if (weather.fetched_at <= 0) {
        // Older Noctalia builds may omit fetched_at; fall back to file mtime.
        weather.fetched_at = file_mtime_unix(weather.cache_path);
        if (weather.weather_code >= 0) {
            if (now_unix > 0 && max_age_seconds > 0) {
                const int64_t age = now_unix - weather.fetched_at;
                weather.available = weather.fetched_at > 0 &&
                                    age <= max_age_seconds && age >= -300;
            } else {
                weather.available = true;
            }
        }
    }
    return weather;
}

std::string NoctaliaWeather::icon_code() const {
    if (!available || weather_code < 0) return {};
    return weather_icon_code(weather_code, is_day);
}

} // namespace p5
