#include "core/notification_launcher.hpp"

#include "core/common.hpp" // spawn_detached (PATH fallback)

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace p5 {

namespace {

std::string ascii_lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// Unicode-aware lowercase so Vietnamese/localized desktop names still match.
std::string utf8_lower(const std::string& s) {
    if (!g_utf8_validate(s.data(), static_cast<gssize>(s.size()), nullptr)) {
        return ascii_lower(s);
    }
    gchar* down = g_utf8_strdown(s.data(), static_cast<gssize>(s.size()));
    std::string out = down ? down : s;
    g_free(down);
    return out;
}

std::vector<std::string> split_env_list(const char* env_value, const std::string& fallback) {
    const std::string raw = (env_value && *env_value) ? env_value : fallback;
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= raw.size()) {
        const size_t sep = raw.find(':', pos);
        const size_t end = (sep == std::string::npos) ? raw.size() : sep;
        const std::string part = raw.substr(pos, end - pos);
        if (!part.empty()) parts.push_back(part);
        if (sep == std::string::npos) break;
        pos = sep + 1;
    }
    return parts;
}

// Everyone's applications/ search path, XDG priority order, plus Flatpak
// exports (often missing from the systemd service environment).
std::vector<std::filesystem::path> application_dirs() {
    std::vector<std::filesystem::path> bases;
    for (const std::string& dir : split_env_list(std::getenv("XDG_DATA_HOME"), "")) {
        const std::filesystem::path base(dir);
        bases.emplace_back(base);
        bases.emplace_back(base / "flatpak" / "exports" / "share");
    }
    if (bases.empty()) {
        if (const char* home = std::getenv("HOME"); home && *home) {
            bases.emplace_back(std::filesystem::path(home) / ".local" / "share");
            bases.emplace_back(std::filesystem::path(home) / ".local" / "share" / "flatpak" / "exports" / "share");
        }
    }
    bases.emplace_back("/var/lib/flatpak/exports/share");
    for (const std::string& dir : split_env_list(std::getenv("XDG_DATA_DIRS"), "/usr/local/share:/usr/share")) {
        bases.emplace_back(dir);
    }

    std::vector<std::filesystem::path> dirs;
    std::unordered_set<std::string> seen;
    for (const auto& base : bases) {
        std::filesystem::path apps = base / "applications";
        if (seen.insert(apps.string()).second) {
            dirs.push_back(std::move(apps));
        }
    }
    return dirs;
}

std::filesystem::path find_desktop_file_by_id(const std::string& desktop_id) {
    for (const auto& dir : application_dirs()) {
        std::error_code ec;
        const std::filesystem::path candidate = dir / desktop_id;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

bool launch_desktop_file(const std::filesystem::path& file, const std::string& sender_label) {
    GDesktopAppInfo* info = g_desktop_app_info_new_from_filename(file.c_str());
    if (!info) return false;

    GAppLaunchContext* ctx = g_app_launch_context_new();
    GError* err = nullptr;
    const bool ok = g_app_info_launch(G_APP_INFO(info), nullptr, ctx, &err) != 0;
    g_object_unref(ctx);
    g_object_unref(info);

    if (!ok) {
        std::cerr << "[p5-rice] Could not open notification app '" << sender_label << "': "
                  << (err && err->message ? err->message : "unknown error") << "\n";
        if (err) g_error_free(err);
        return false;
    }
    std::cout << "[p5-rice] Opened notification app '" << sender_label << "' via "
              << file.string() << "\n";
    return true;
}

// First real command of an Exec= line: skips `env`, VAR=value assignments and
// takes the first non-assignment, non-shell-wrapping token.
std::string exec_command_basename(const char* exec_line) {
    if (!exec_line) return {};
    std::vector<std::string> tokens;
    std::string current;
    char quote = '\0';
    bool in_token = false;
    for (const char* p = exec_line; *p; ++p) {
        const char c = *p;
        if (quote) {
            if (c == quote) {
                quote = '\0';
            } else {
                current.push_back(c);
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            in_token = true;
            continue;
        }
        if (c == ' ' || c == '\t') {
            if (in_token) {
                tokens.push_back(std::exchange(current, {}));
                in_token = false;
            }
            continue;
        }
        current.push_back(c);
        in_token = true;
    }
    if (in_token) tokens.push_back(current);

    size_t idx = 0;
    while (idx < tokens.size()) {
        const std::string& token = tokens[idx];
        if (token == "env" || token == "sh" || token == "bash" || token == "ksh" ||
            token == "-c" || token == "-e") {
            ++idx;
            continue;
        }
        if (token.find('=') != std::string::npos) { // env-style assignment
            ++idx;
            continue;
        }
        break;
    }
    if (idx >= tokens.size()) return {};
    std::filesystem::path p(tokens[idx]);
    return p.filename().string();
}

// Match strength for one .desktop file against the sender's presented name:
//   3 = Name equals app_name          (own presentation name)
//   2 = StartupWMClass / Exec matches (window/class identity)
//   1 = Name or GenericName contains  (best-effort substring)
int score_desktop_file(const std::filesystem::path& file, const std::string& needle) {
    GKeyFile* kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, file.c_str(), G_KEY_FILE_NONE, nullptr)) {
        g_key_file_free(kf);
        return 0;
    }

    int best = 0;
    if (g_key_file_get_boolean(kf, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_HIDDEN, nullptr)) {
        g_key_file_free(kf);
        return 0;
    }

    auto read_string = [&](const gchar* key) -> gchar* {
        return g_key_file_get_string(kf, G_KEY_FILE_DESKTOP_GROUP, key, nullptr);
    };
    auto read_locale_string = [&](const gchar* key) -> gchar* {
        return g_key_file_get_locale_string(kf, G_KEY_FILE_DESKTOP_GROUP, key, nullptr, nullptr);
    };

    if (gchar* name = read_locale_string(G_KEY_FILE_DESKTOP_KEY_NAME)) {
        const std::string lowered = utf8_lower(name);
        g_free(name);
        if (lowered == needle) {
            g_key_file_free(kf);
            return 3;
        }
        if (lowered.find(needle) != std::string::npos) {
            best = std::max(best, 1);
        }
    }
    if (gchar* generic = read_locale_string(G_KEY_FILE_DESKTOP_KEY_GENERIC_NAME)) {
        const std::string lowered = utf8_lower(generic);
        g_free(generic);
        if (lowered == needle) best = std::max(best, 2);
        else if (lowered.find(needle) != std::string::npos) best = std::max(best, 1);
    }
    if (gchar* wmclass = read_string(G_KEY_FILE_DESKTOP_KEY_STARTUP_WM_CLASS)) {
        const std::string lowered = ascii_lower(wmclass);
        g_free(wmclass);
        if (lowered == needle) best = std::max(best, 2);
    }
    if (gchar* exec_line = read_string(G_KEY_FILE_DESKTOP_KEY_EXEC)) {
        const std::string lowered = ascii_lower(exec_command_basename(exec_line));
        g_free(exec_line);
        if (lowered == needle) best = std::max(best, 2);
        else if (!lowered.empty() && lowered.find(needle) != std::string::npos) {
            best = std::max(best, 1);
        }
    }

    g_key_file_free(kf);
    return best;
}

std::filesystem::path find_desktop_file_by_app_name(const std::string& app_name) {
    const std::string needle = utf8_lower(app_name);
    if (needle.empty()) return {};

    int best_score = 0;
    std::filesystem::path best_file;

    for (const auto& dir : application_dirs()) {
        std::error_code ec;
        std::filesystem::directory_iterator it(
            dir, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec) continue;
        for (const auto& entry : it) {
            const std::filesystem::path path = entry.path();
            if (!path.filename().string().ends_with(".desktop")) continue;
            const int score = score_desktop_file(path, needle);
            if (score > best_score) {
                best_score = score;
                best_file = path;
            }
            if (best_score >= 3) {
                return best_file; // exact presentation-name match wins
            }
        }
    }
    return best_file;
}

// Last resort for senders that put their argv[0] into app_name with no hint.
bool launch_from_path(const std::string& app_name) {
    if (app_name.empty() || app_name.find('/') != std::string::npos) return false;
    for (char c : app_name) {
        if (std::isspace(static_cast<unsigned char>(c))) return false;
    }
    if (!std::getenv("PATH")) return false;

    for (const std::string& dir : split_env_list(std::getenv("PATH"), "")) {
        const std::filesystem::path candidate = std::filesystem::path(dir) / app_name;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec) || ec) continue;
        const auto perms = std::filesystem::status(candidate, ec).permissions();
        if (ec) continue;
        if ((perms & std::filesystem::perms::owner_exec) == std::filesystem::perms::none) continue;
        spawn_detached({candidate.string()});
        std::cout << "[p5-rice] Opened notification app '" << app_name << "' via PATH: "
                  << candidate.string() << "\n";
        return true;
    }
    return false;
}

// Resolution cache: same banner clicked twice (or re-created bursts) must not
// rescan the applications tree every time. The launch itself always runs.
std::unordered_map<std::string, std::filesystem::path>& resolution_cache() {
    static std::unordered_map<std::string, std::filesystem::path> cache;
    return cache;
}

} // namespace

bool launch_notification_app(const std::string& desktop_entry, const std::string& app_name) {
    const std::string sender_label = !desktop_entry.empty() ? desktop_entry
                                     : (!app_name.empty() ? app_name : std::string("unknown"));

    const std::string cache_key = desktop_entry + "\x1f" + app_name;
    auto& cache = resolution_cache();
    const auto cached = cache.find(cache_key);
    if (cached != cache.end()) {
        if (cached->second.empty()) return false;
        return launch_desktop_file(cached->second, sender_label);
    }

    std::filesystem::path resolved;
    if (!desktop_entry.empty()) {
        std::string id = desktop_entry;
        if (!id.ends_with(".desktop")) id += ".desktop";
        resolved = find_desktop_file_by_id(id);
    }
    if (resolved.empty() && !app_name.empty()) {
        resolved = find_desktop_file_by_app_name(app_name);
    }
    cache[cache_key] = resolved;

    if (!resolved.empty()) {
        return launch_desktop_file(resolved, sender_label);
    }
    return launch_from_path(app_name);
}

} // namespace p5
