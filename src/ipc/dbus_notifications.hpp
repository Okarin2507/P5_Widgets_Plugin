#pragma once

#include "notifications/notification_event.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <systemd/sd-bus.h>

namespace p5 {

struct ControlCallbacks {
    std::function<void(const NotificationEvent&)> on_notification;
    std::function<void()> on_dismiss_all;
    std::function<void(bool)> on_set_music_visible;
    std::function<void()> on_toggle_music;
    std::function<void(bool)> on_set_calendar_visible;
    std::function<void()> on_toggle_calendar;
    std::function<void()> on_play_pause;
    std::function<void()> on_next;
    std::function<void()> on_prev;
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

    // Deduplicate (sender, cookie) across (sender, cookie) seen on the monitor bus
    bool mark_seen_message(sd_bus_message* m);

    const ControlCallbacks& callbacks() const { return callbacks_; }
    uint32_t next_id() { return ++last_id_; }

private:
    struct SeenMsg {
        std::string sender;
        uint64_t cookie = 0;
    };

    ControlCallbacks callbacks_;
    sd_bus* bus_ = nullptr;
    sd_bus* monitor_bus_ = nullptr;
    sd_bus_slot* vtable_slot_ = nullptr;
    sd_bus_slot* monitor_filter_slot_ = nullptr;
    uint32_t last_id_ = 100;

    std::array<SeenMsg, 16> seen_msgs_{};
    size_t seen_idx_ = 0;

    bool setup_monitor_bus();
};

} // namespace p5
