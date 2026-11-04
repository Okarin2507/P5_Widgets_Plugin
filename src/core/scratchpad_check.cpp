#include "core/scratchpad_check.hpp"

#include "core/common.hpp" // run_command_capture

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace p5 {

namespace {

std::string ascii_lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string strip_desktop_suffix(std::string s) {
    if (s.size() >= 8 && s.compare(s.size() - 8, 8, ".desktop") == 0) {
        s.resize(s.size() - 8);
    }
    return s;
}

std::string normalize_identity(std::string s) {
    return strip_desktop_suffix(ascii_lower(std::move(s)));
}

std::string json_unescape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\' || i + 1 >= in.size()) {
            out.push_back(in[i]);
            continue;
        }
        const char next = in[++i];
        switch (next) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        default: out.push_back(next); break; // \uXXXX rare in titles; keep raw
        }
    }
    return out;
}

struct ScratchpadWindow {
    uint32_t pid = 0;
    std::string app_id;
    std::string title;
};

// Extract `<value>` for `<key>":` from a serialized JSON object body.
// Only handles numbers and quoted strings; the keys of interest are simple.
std::string json_object_field(const std::string& object, std::string_view key) {
    // Find "key" as a quoted JSON name followed by a colon.
    std::string needle = "\"";
    needle += key;
    needle += "\":";
    size_t at = object.find(needle);
    if (at == std::string::npos) return {};

    size_t value_at = at + needle.size();
    while (value_at < object.size() && std::isspace(static_cast<unsigned char>(object[value_at]))) {
        ++value_at;
    }
    if (value_at >= object.size()) return {};

    const char lead = object[value_at];
    if (lead == '"') {
        std::string raw;
        size_t i = value_at + 1;
        for (; i < object.size() && object[i] != '"'; ++i) {
            if (object[i] == '\\' && i + 1 < object.size()) {
                raw.push_back(object[i]);
                raw.push_back(object[i + 1]);
                ++i;
            } else {
                raw.push_back(object[i]);
            }
        }
        return json_unescape(raw);
    }
    // Number literal: consume digits/minus.
    size_t i = value_at;
    while (i < object.size() && (std::isdigit(static_cast<unsigned char>(object[i])) || object[i] == '-')) {
        ++i;
    }
    return object.substr(value_at, i - value_at);
}

uint32_t parse_u32(const std::string& text) {
    if (text.empty()) return 0;
    uint32_t value = 0;
    for (const char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return 0;
        value = value * 10 + static_cast<uint32_t>(c - '0');
    }
    return value;
}

// One targeted scan of `umbriel windows --json`. Frozen at umbriel 0.1 field
// names; unknown entries are simply skipped, missing binary -> empty list.
std::vector<ScratchpadWindow> scan_windows_json() {
    std::vector<ScratchpadWindow> windows;

    int exit_code = 0;
    const std::string json = run_command_capture(
        {"umbriel", "windows", "--json"}, &exit_code);
    if (exit_code != 0) return windows;

    size_t cursor = json.find('[');
    while (cursor != std::string::npos) {
        const size_t object_start = json.find('{', cursor);
        if (object_start == std::string::npos) break;

        // Walk to the matching '}' honouring string literals with escapes.
        size_t i = object_start + 1;
        bool in_string = false;
        size_t object_end = std::string::npos;
        for (int depth = 1; i < json.size() && depth > 0; ++i) {
            const char c = json[i];
            if (in_string) {
                if (c == '\\') { ++i; continue; }
                if (c == '"') in_string = false;
                continue;
            }
            if (c == '"') in_string = true;
            else if (c == '{') ++depth;
            else if (c == '}') --depth;
            if (depth == 0) object_end = i;
        }
        i = (object_end == std::string::npos) ? json.size() : object_end;
        if (object_end != std::string::npos) {
            const std::string object = json.substr(object_start, object_end - object_start + 1);
            const std::string scratchpad = json_object_field(object, "scratchpad");
            if (!scratchpad.empty()) { // hidden in some scratchpad
                ScratchpadWindow win;
                win.pid = parse_u32(json_object_field(object, "pid"));
                win.app_id = normalize_identity(json_object_field(object, "app_id"));
                win.title = ascii_lower(json_object_field(object, "title"));
                windows.push_back(std::move(win));
            }
        }
        cursor = i;
    }
    return windows;
}

// /proc access ------------------------------------------------------------

uint32_t proc_ppid(uint32_t pid) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%u/stat", pid);
    FILE* f = std::fopen(path, "r");
    if (!f) return 0;
    char buf[1024];
    const char* line = std::fgets(buf, sizeof(buf), f);
    std::fclose(f);
    if (!line) return 0;

    // "pid (comm) state ppid ..." — the comm may contain ') ', use last ')'.
    const char* tail = std::strrchr(buf, ')');
    if (!tail) return 0;
    const char* p = tail + 1;
    while (*p == ' ') ++p;         // at the state character
    while (*p && *p != ' ') ++p;   // skip the state character
    while (*p == ' ') ++p;         // at ppid
    unsigned ppid = 0;
    if (std::sscanf(p, "%u", &ppid) != 1) return 0;
    return ppid;
}

std::string proc_comm(uint32_t pid) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%u/comm", pid);
    FILE* f = std::fopen(path, "r");
    if (!f) return {};
    char buf[256];
    const char* got = std::fgets(buf, sizeof(buf), f);
    std::fclose(f);
    if (!got) return {};
    std::string comm(got);
    while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\r')) comm.pop_back();
    return ascii_lower(std::move(comm));
}

// Does the window's process own the sender's process tree?
bool window_owns_process(uint32_t window_pid, uint32_t sender_pid) {
    if (!window_pid || !sender_pid) return false;
    uint32_t current = sender_pid;
    for (int depth = 0; depth < 16 && current >= 1; ++depth) {
        if (current == window_pid) return true;
        const uint32_t ppid = proc_ppid(current);
        if (ppid == 0 || ppid == current) break;
        current = ppid;
        if (current == 1) break;
    }
    return false;
}

// Terminal wrapping fallback: an ancestor's comm equals the window app_id
// ("kitty" hosts opencode, ghostty hosts the session, ...).
bool ancestor_comm_matches(uint32_t sender_pid, const std::string& app_id) {
    if (!sender_pid || app_id.empty()) return false;
    uint32_t current = sender_pid;
    for (int depth = 0; depth < 16 && current >= 1; ++depth) {
        const uint32_t ppid = proc_ppid(current);
        if (ppid == 0 || ppid == current) break;
        current = ppid;
        if (current == 1) break;
        if (proc_comm(current) == app_id) return true;
    }
    return false;
}

} // namespace

bool sender_window_in_scratchpad(const std::string& desktop_entry,
                                 const std::string& app_name,
                                 uint32_t sender_pid) {
    // Identities derived from the notification (most precise first).
    std::vector<std::string> identities;
    for (const std::string* raw : {&desktop_entry, &app_name}) {
        if (raw && !raw->empty()) {
            const std::string normalized = normalize_identity(*raw);
            if (!normalized.empty()) identities.push_back(normalized);
        }
    }

    const std::vector<ScratchpadWindow> windows = scan_windows_json();
    if (windows.empty() && identities.empty()) return false;

    for (const ScratchpadWindow& win : windows) {
        bool matched = false;
        for (const std::string& identity : identities) {
            if (!win.app_id.empty() && win.app_id == identity) {
                matched = true;
                break;
            }
            if (win.title.find(identity) != std::string::npos) {
                matched = true;
                break;
            }
        }
        if (!matched && sender_pid) {
            if (window_owns_process(win.pid, sender_pid) ||
                ancestor_comm_matches(sender_pid, win.app_id)) {
                matched = true;
            }
        }
        if (matched) return true;
    }
    return false;
}

} // namespace p5
