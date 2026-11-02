#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <systemd/sd-bus.h>

namespace p5 {

struct NotificationEvent {
    uint32_t id = 0;
    std::string app_name;
    std::string summary;
    std::string body;
    uint8_t urgency = 1;       // 0 = Low, 1 = Normal, 2 = Critical
    int32_t expire_timeout = -1;
};

struct ControlCallbacks {
    std::function<void(const NotificationEvent&)> on_notification;
    std::function<void(const std::string&)> on_profile_change;
    std::function<std::string()> get_profile;
    std::function<void()> on_dismiss_all;
    std::function<void(bool)> on_set_music_visible;
};

class DbusNotifications {
public:
    explicit DbusNotifications(ControlCallbacks callbacks);
    ~DbusNotifications();

    DbusNotifications(const DbusNotifications&) = delete;
    DbusNotifications& operator=(const DbusNotifications&) = delete;

    bool start();
    void stop();

    int primary_fd() const;
    int monitor_fd() const;

    void process_primary();
    void process_monitor();

    // Used by internal callbacks
    const ControlCallbacks& callbacks() const { return callbacks_; }
    uint32_t next_id() { return ++last_id_; }

private:
    ControlCallbacks callbacks_;
    sd_bus* bus_ = nullptr;
    sd_bus* monitor_bus_ = nullptr;
    sd_bus_slot* vtable_slot_ = nullptr;
    sd_bus_slot* fallback_notif_slot_ = nullptr;
    sd_bus_slot* monitor_filter_slot_ = nullptr;
    uint32_t last_id_ = 100;

    bool setup_monitor_bus();
};

} // namespace p5

