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
};

} // namespace p5
