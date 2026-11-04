#pragma once

#include <cstdint>
#include <string>

namespace p5 {

// Plain data shared by the D-Bus notification server and the overlay renderer.
// Keeping it outside the IPC layer lets the notification UI be tested without
// linking sd-bus.
struct NotificationEvent {
    uint32_t id = 0;
    std::string app_name;
    std::string summary;
    std::string body;
    uint8_t urgency = 1;       // 0 = Low, 1 = Normal, 2 = Critical
    int32_t expire_timeout = -1;
    // FreeDesktop "desktop-entry" hint: .desktop id of the sending app.
    // Used by the banner overlay so clicking the text can open the sender.
    std::string desktop_entry;
    // PID of the notification's D-Bus sender, resolved at parse time. Feeds
    // the scratchpad blocker's process-tree matching (0 = unknown).
    uint32_t sender_pid = 0;
};

} // namespace p5
