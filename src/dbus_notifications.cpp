#include "dbus_notifications.hpp"

#include <cstring>
#include <iostream>

namespace p5 {

namespace {

bool parse_freedesktop_notify_message(sd_bus_message* m, NotificationEvent& out) {
    int r = sd_bus_message_rewind(m, true);
    if (r < 0) return false;

    const char* app_name = nullptr;
    uint32_t replaces_id = 0;
    const char* app_icon = nullptr;
    const char* summary = nullptr;
    const char* body = nullptr;

    r = sd_bus_message_read(m, "susss", &app_name, &replaces_id, &app_icon, &summary, &body);
    if (r < 0) return false;

    out.id = replaces_id;
    out.app_name = app_name ? app_name : "";
    out.summary = summary ? summary : "";
    out.body = body ? body : "";
    out.urgency = 1;
    out.expire_timeout = -1;

    // Skip actions array "as"
    r = sd_bus_message_skip(m, "as");
    if (r < 0) return true;

    // Read hints dictionary "a{sv}" to extract urgency
    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    if (r >= 0) {
        while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
            const char* key = nullptr;
            if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &key) >= 0 && key) {
                if (std::strcmp(key, "urgency") == 0) {
                    const char* contents = nullptr;
                    if (sd_bus_message_peek_type(m, nullptr, &contents) >= 0 && contents) {
                        if (std::strcmp(contents, "y") == 0) {
                            uint8_t u = 1;
                            if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "y") >= 0) {
                                sd_bus_message_read_basic(m, SD_BUS_TYPE_BYTE, &u);
                                sd_bus_message_exit_container(m);
                                out.urgency = u;
                            }
                        } else if (std::strcmp(contents, "i") == 0) {
                            int32_t u = 1;
                            if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "i") >= 0) {
                                sd_bus_message_read_basic(m, SD_BUS_TYPE_INT32, &u);
                                sd_bus_message_exit_container(m);
                                out.urgency = static_cast<uint8_t>(u);
                            }
                        } else if (std::strcmp(contents, "u") == 0) {
                            uint32_t u = 1;
                            if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "u") >= 0) {
                                sd_bus_message_read_basic(m, SD_BUS_TYPE_UINT32, &u);
                                sd_bus_message_exit_container(m);
                                out.urgency = static_cast<uint8_t>(u);
                            }
                        } else {
                            sd_bus_message_skip(m, "v");
                        }
                    } else {
                        sd_bus_message_skip(m, "v");
                    }
                } else {
                    sd_bus_message_skip(m, "v");
                }
            }
            sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
    }

    int32_t timeout = -1;
    if (sd_bus_message_read_basic(m, SD_BUS_TYPE_INT32, &timeout) >= 0) {
        out.expire_timeout = timeout;
    }
    return true;
}

int monitor_filter_cb(sd_bus_message* m, void* userdata, sd_bus_error* /*ret_error*/) {
    auto* self = static_cast<DbusNotifications*>(userdata);
    if (!self || !m) return 0;

    if (sd_bus_message_is_method_call(m, "org.freedesktop.Notifications", "Notify") > 0) {
        NotificationEvent ev;
        if (parse_freedesktop_notify_message(m, ev)) {
            if (ev.id == 0) {
                ev.id = self->next_id();
            }
            if (self->callbacks().on_notification) {
                self->callbacks().on_notification(ev);
            }
        }
    }
    return 0;
}

// Control interface methods on dev.noctalia.PersonaRice
int method_notify(sd_bus_message* m, void* userdata, sd_bus_error* /*ret_error*/) {
    auto* self = static_cast<DbusNotifications*>(userdata);
    const char* summary = nullptr;
    const char* body = nullptr;
    const char* app_name = nullptr;
    uint32_t urgency = 1;
    int32_t timeout_ms = 6000;

    int r = sd_bus_message_read(m, "sssui", &summary, &body, &app_name, &urgency, &timeout_ms);
    if (r < 0) return r;

    NotificationEvent ev;
    ev.id = self->next_id();
    ev.summary = summary ? summary : "";
    ev.body = body ? body : "";
    ev.app_name = app_name ? app_name : "PersonaRice";
    ev.urgency = static_cast<uint8_t>(urgency);
    ev.expire_timeout = timeout_ms;

    if (self->callbacks().on_notification) {
        self->callbacks().on_notification(ev);
    }
    return sd_bus_reply_method_return(m, "u", ev.id);
}

int method_set_profile(sd_bus_message* m, void* userdata, sd_bus_error* /*ret_error*/) {
    auto* self = static_cast<DbusNotifications*>(userdata);
    const char* profile = nullptr;
    int r = sd_bus_message_read(m, "s", &profile);
    if (r < 0) return r;

    if (profile && self->callbacks().on_profile_change) {
        self->callbacks().on_profile_change(profile);
    }
    return sd_bus_reply_method_return(m, "b", 1);
}

int method_get_profile(sd_bus_message* m, void* userdata, sd_bus_error* /*ret_error*/) {
    auto* self = static_cast<DbusNotifications*>(userdata);
    std::string prof = self->callbacks().get_profile ? self->callbacks().get_profile() : "persona";
    return sd_bus_reply_method_return(m, "s", prof.c_str());
}

int method_dismiss_all(sd_bus_message* m, void* userdata, sd_bus_error* /*ret_error*/) {
    auto* self = static_cast<DbusNotifications*>(userdata);
    if (self->callbacks().on_dismiss_all) {
        self->callbacks().on_dismiss_all();
    }
    return sd_bus_reply_method_return(m, "");
}

int method_set_music_visible(sd_bus_message* m, void* userdata, sd_bus_error* /*ret_error*/) {
    auto* self = static_cast<DbusNotifications*>(userdata);
    int visible = 1;
    int r = sd_bus_message_read(m, "b", &visible);
    if (r < 0) return r;
    if (self->callbacks().on_set_music_visible) {
        self->callbacks().on_set_music_visible(visible != 0);
    }
    return sd_bus_reply_method_return(m, "");
}

const sd_bus_vtable kPersonaRiceVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Notify", "sssui", "u", method_notify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetProfile", "s", "b", method_set_profile, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetProfile", "", "s", method_get_profile, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("DismissAll", "", "", method_dismiss_all, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetMusicVisible", "b", "", method_set_music_visible, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

// Fallback org.freedesktop.Notifications server in case Noctalia is not running
int fd_get_capabilities(sd_bus_message* m, void* /*userdata*/, sd_bus_error* /*ret_error*/) {
    return sd_bus_reply_method_return(m, "as", 2, "body", "body-markup");
}

int fd_notify(sd_bus_message* m, void* userdata, sd_bus_error* /*ret_error*/) {
    auto* self = static_cast<DbusNotifications*>(userdata);
    NotificationEvent ev;
    if (parse_freedesktop_notify_message(m, ev)) {
        if (ev.id == 0) ev.id = self->next_id();
        if (self->callbacks().on_notification) {
            self->callbacks().on_notification(ev);
        }
    } else {
        ev.id = self->next_id();
    }
    return sd_bus_reply_method_return(m, "u", ev.id);
}

int fd_close_notification(sd_bus_message* m, void* /*userdata*/, sd_bus_error* /*ret_error*/) {
    return sd_bus_reply_method_return(m, "");
}

int fd_get_server_info(sd_bus_message* m, void* /*userdata*/, sd_bus_error* /*ret_error*/) {
    return sd_bus_reply_method_return(m, "ssss", "p5-rice-daemon", "noctalia-p5", "2.0.0", "1.2");
}

const sd_bus_vtable kFreedesktopNotifVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetCapabilities", "", "as", fd_get_capabilities, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Notify", "susssasa{sv}i", "u", fd_notify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("CloseNotification", "u", "", fd_close_notification, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetServerInformation", "", "ssss", fd_get_server_info, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

} // namespace

DbusNotifications::DbusNotifications(ControlCallbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

DbusNotifications::~DbusNotifications() {
    stop();
}

bool DbusNotifications::start() {
    int r = sd_bus_open_user(&bus_);
    if (r < 0) {
        std::cerr << "[p5-rice] Failed to connect to user D-Bus: " << std::strerror(-r) << "\n";
        return false;
    }

    r = sd_bus_add_object_vtable(
        bus_,
        &vtable_slot_,
        "/dev/noctalia/PersonaRice",
        "dev.noctalia.PersonaRice",
        kPersonaRiceVtable,
        this
    );
    if (r < 0) {
        std::cerr << "[p5-rice] Failed to register dev.noctalia.PersonaRice vtable\n";
    }

    sd_bus_request_name(bus_, "dev.noctalia.PersonaRice", SD_BUS_NAME_REPLACE_EXISTING);

    // Attempt to set up the BecomeMonitor bus so we passively observe all
    // org.freedesktop.Notifications.Notify calls while Noctalia owns the service.
    if (!setup_monitor_bus()) {
        // Fallback: also register /org/freedesktop/Notifications in case no server is running
        sd_bus_add_object_vtable(
            bus_,
            &fallback_notif_slot_,
            "/org/freedesktop/Notifications",
            "org.freedesktop.Notifications",
            kFreedesktopNotifVtable,
            this
        );
        sd_bus_request_name(bus_, "org.freedesktop.Notifications", 0);
    }

    sd_bus_flush(bus_);
    return true;
}

bool DbusNotifications::setup_monitor_bus() {
    int r = sd_bus_new(&monitor_bus_);
    if (r < 0) return false;

    r = sd_bus_set_bus_client(monitor_bus_, true);
    if (r < 0) {
        sd_bus_unref(monitor_bus_);
        monitor_bus_ = nullptr;
        return false;
    }

    const char* addr = nullptr;
    if (sd_bus_get_address(bus_, &addr) >= 0 && addr) {
        sd_bus_set_address(monitor_bus_, addr);
    } else {
        sd_bus_unref(monitor_bus_);
        monitor_bus_ = nullptr;
        if (sd_bus_open_user(&monitor_bus_) < 0) {
            monitor_bus_ = nullptr;
            return false;
        }
    }

    if (addr) {
        if (sd_bus_start(monitor_bus_) < 0) {
            sd_bus_unref(monitor_bus_);
            monitor_bus_ = nullptr;
            return false;
        }
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    r = sd_bus_call_method(
        monitor_bus_,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus.Monitoring",
        "BecomeMonitor",
        &err,
        &reply,
        "asu",
        1,
        "type='method_call',interface='org.freedesktop.Notifications',member='Notify'",
        0u
    );
    sd_bus_error_free(&err);
    if (reply) sd_bus_message_unref(reply);

    if (r < 0) {
        sd_bus_unref(monitor_bus_);
        monitor_bus_ = nullptr;
        return false;
    }

    sd_bus_add_filter(monitor_bus_, &monitor_filter_slot_, monitor_filter_cb, this);
    sd_bus_flush(monitor_bus_);
    return true;
}

void DbusNotifications::stop() {
    if (monitor_filter_slot_) {
        sd_bus_slot_unref(monitor_filter_slot_);
        monitor_filter_slot_ = nullptr;
    }
    if (monitor_bus_) {
        sd_bus_unref(monitor_bus_);
        monitor_bus_ = nullptr;
    }
    if (fallback_notif_slot_) {
        sd_bus_slot_unref(fallback_notif_slot_);
        fallback_notif_slot_ = nullptr;
    }
    if (vtable_slot_) {
        sd_bus_slot_unref(vtable_slot_);
        vtable_slot_ = nullptr;
    }
    if (bus_) {
        sd_bus_unref(bus_);
        bus_ = nullptr;
    }
}

int DbusNotifications::primary_fd() const {
    return bus_ ? sd_bus_get_fd(bus_) : -1;
}

int DbusNotifications::monitor_fd() const {
    return monitor_bus_ ? sd_bus_get_fd(monitor_bus_) : -1;
}

void DbusNotifications::process_primary() {
    if (!bus_) return;
    while (sd_bus_process(bus_, nullptr) > 0) {}
}

void DbusNotifications::process_monitor() {
    if (!monitor_bus_) return;
    while (sd_bus_process(monitor_bus_, nullptr) > 0) {}
}

} // namespace p5

