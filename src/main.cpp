#include "common.hpp"
#include "dbus_mpris.hpp"
#include "dbus_notifications.hpp"
#include "p5music_surface.hpp"
#include "p5notify_surface.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <vector>
#include <wayland-client.h>
#include <wayland-cursor.h>

namespace {

std::atomic<bool> g_running{true};

void sig_handler(int /*sig*/) {
    g_running.store(false);
}

struct OutputSlot {
    uint32_t registry_name = 0;
    wl_output* output = nullptr;
    std::unique_ptr<p5::P5NotifySurface> notify_surface;
    std::unique_ptr<p5::P5MusicSurface> music_surface;

    ~OutputSlot() {
        music_surface.reset();
        notify_surface.reset();
        if (output) {
            wl_output_destroy(output);
            output = nullptr;
        }
    }
};

struct DaemonContext {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    zwlr_layer_shell_v1* layer_shell = nullptr;

    wl_cursor_theme* cursor_theme = nullptr;
    wl_surface* cursor_surface = nullptr;

    bool enable_notify = true;
    bool enable_music = true;
    bool surfaces_ready = false;
    bool music_visible = true;
    std::string active_profile = "persona";
    uint64_t next_notif_uid = 1;

    p5::LoadedFonts fonts{};
    p5::DbusMpris* mpris = nullptr;
    wl_surface* focused_surface = nullptr;

    std::vector<std::unique_ptr<OutputSlot>> outputs;

    void set_cursor(uint32_t serial, const char* name) {
        if (!cursor_theme || !cursor_surface || !pointer) return;
        wl_cursor* cur = wl_cursor_theme_get_cursor(cursor_theme, name);
        if (!cur) cur = wl_cursor_theme_get_cursor(cursor_theme, "left_ptr");
        if (!cur || cur->image_count == 0) return;
        wl_cursor_image* img = cur->images[0];
        wl_buffer* buf = wl_cursor_image_get_buffer(img);
        if (!buf) return;
        wl_pointer_set_cursor(
            pointer,
            serial,
            cursor_surface,
            static_cast<int32_t>(img->hotspot_x),
            static_cast<int32_t>(img->hotspot_y)
        );
        wl_surface_attach(cursor_surface, buf, 0, 0);
        wl_surface_damage_buffer(
            cursor_surface,
            0,
            0,
            static_cast<int32_t>(img->width),
            static_cast<int32_t>(img->height)
        );
        wl_surface_commit(cursor_surface);
    }

    void dismiss_uid_all_outputs(uint64_t uid) {
        for (auto& slot : outputs) {
            if (slot->notify_surface) {
                slot->notify_surface->dismiss_uid(uid);
            }
        }
    }

    void init_slot_surfaces(OutputSlot& slot) {
        if (!compositor || !shm || !layer_shell) return;
        const bool is_persona = (active_profile == "persona");

        if (enable_notify && !slot.notify_surface) {
            slot.notify_surface = std::make_unique<p5::P5NotifySurface>(
                compositor,
                layer_shell,
                shm,
                slot.output,
                fonts,
                [this](uint64_t uid) { dismiss_uid_all_outputs(uid); }
            );
            slot.notify_surface->init();
            slot.notify_surface->set_persona_active(is_persona);
        }

        if (enable_music && mpris && !slot.music_surface) {
            slot.music_surface = std::make_unique<p5::P5MusicSurface>(
                compositor,
                layer_shell,
                shm,
                slot.output,
                mpris,
                fonts
            );
            slot.music_surface->init();
            slot.music_surface->set_visible(is_persona && music_visible);
        }
    }

    void init_all_outputs() {
        // If compositor hasn't announced any wl_output yet, create a fallback nullptr slot
        if (outputs.empty()) {
            auto fallback = std::make_unique<OutputSlot>();
            fallback->registry_name = 0;
            fallback->output = nullptr;
            init_slot_surfaces(*fallback);
            outputs.push_back(std::move(fallback));
        } else {
            for (auto& slot : outputs) {
                init_slot_surfaces(*slot);
            }
        }
        surfaces_ready = true;
    }

    p5::P5NotifySurface* find_notify_surface(wl_surface* s) {
        if (!s) return nullptr;
        for (auto& slot : outputs) {
            if (slot->notify_surface && slot->notify_surface->surface() == s) {
                return slot->notify_surface.get();
            }
        }
        return nullptr;
    }

    p5::P5MusicSurface* find_music_surface(wl_surface* s) {
        if (!s) return nullptr;
        for (auto& slot : outputs) {
            if (slot->music_surface && slot->music_surface->surface() == s) {
                return slot->music_surface.get();
            }
        }
        return nullptr;
    }

    void sync_noctalia_notification_suppression(bool is_persona) {
        if (!enable_notify) return;
        p5::spawn_detached({
            "noctalia",
            "msg",
            "config-set",
            "notification.enable_daemon",
            is_persona ? "false" : "true"
        });
        p5::spawn_detached({
            "noctalia",
            "msg",
            "config-set",
            "notification.filter.persona_suppress_toast.enabled",
            is_persona ? "true" : "false"
        });
        if (is_persona) {
            p5::spawn_detached({
                "noctalia",
                "msg",
                "config-set",
                "notification.filter.persona_suppress_toast.match_content",
                ".*"
            });
            p5::spawn_detached({
                "noctalia",
                "msg",
                "config-set",
                "notification.filter.persona_suppress_toast.show_toast",
                "false"
            });
            p5::spawn_detached({
                "noctalia",
                "msg",
                "config-set",
                "notification.filter.persona_suppress_toast.play_sound",
                "false"
            });
            p5::spawn_detached({
                "noctalia",
                "msg",
                "config-set",
                "notification.filter.persona_suppress_toast.save_history",
                "true"
            });
            p5::spawn_detached({
                "noctalia",
                "msg",
                "config-set",
                "notification.filter_order",
                "[\"persona_suppress_toast\"]"
            });
            p5::spawn_detached({"noctalia", "msg", "notification-dnd-set", "true"});
            p5::spawn_detached({"noctalia", "msg", "notification-clear-active"});
        } else {
            p5::spawn_detached({"noctalia", "msg", "notification-dnd-set", "false"});
        }
    }

    void apply_profile(const std::string& prof) {
        if (prof.empty()) return;
        const bool changed = (active_profile != prof);
        active_profile = prof;
        const bool is_persona = (prof == "persona");

        for (auto& slot : outputs) {
            if (slot->notify_surface) {
                slot->notify_surface->set_persona_active(is_persona);
            }
            if (slot->music_surface) {
                slot->music_surface->set_visible(is_persona && music_visible);
            }
        }

        if (changed && enable_notify) {
            sync_noctalia_notification_suppression(is_persona);
        }
    }

    void set_music_widget_visible(bool vis) {
        music_visible = vis;
        const bool show = (active_profile == "persona") && music_visible;
        for (auto& slot : outputs) {
            if (slot->music_surface) {
                slot->music_surface->set_visible(show);
            }
        }
    }
};

void pointer_enter(
    void* data,
    wl_pointer* /*wl_pointer*/,
    uint32_t serial,
    wl_surface* surface,
    wl_fixed_t surface_x,
    wl_fixed_t surface_y
) {
    auto* ctx = static_cast<DaemonContext*>(data);
    if (!ctx) return;
    ctx->focused_surface = surface;
    const double x = wl_fixed_to_double(surface_x);
    const double y = wl_fixed_to_double(surface_y);

    if (auto* ns = ctx->find_notify_surface(surface)) {
        ctx->set_cursor(serial, "pointer");
        ns->on_pointer_motion(x, y);
    } else if (auto* ms = ctx->find_music_surface(surface)) {
        ctx->set_cursor(serial, "pointer");
        ms->on_pointer_motion(x, y);
    }
}

void pointer_leave(
    void* data,
    wl_pointer* /*wl_pointer*/,
    uint32_t /*serial*/,
    wl_surface* surface
) {
    auto* ctx = static_cast<DaemonContext*>(data);
    if (!ctx) return;
    if (auto* ns = ctx->find_notify_surface(surface)) {
        ns->on_pointer_leave();
    } else if (auto* ms = ctx->find_music_surface(surface)) {
        ms->on_pointer_leave();
    }
    if (ctx->focused_surface == surface) {
        ctx->focused_surface = nullptr;
    }
}

void pointer_motion(
    void* data,
    wl_pointer* /*wl_pointer*/,
    uint32_t /*time*/,
    wl_fixed_t surface_x,
    wl_fixed_t surface_y
) {
    auto* ctx = static_cast<DaemonContext*>(data);
    if (!ctx) return;
    const double x = wl_fixed_to_double(surface_x);
    const double y = wl_fixed_to_double(surface_y);
    if (auto* ns = ctx->find_notify_surface(ctx->focused_surface)) {
        ns->on_pointer_motion(x, y);
    } else if (auto* ms = ctx->find_music_surface(ctx->focused_surface)) {
        ms->on_pointer_motion(x, y);
    }
}

void pointer_button(
    void* data,
    wl_pointer* /*wl_pointer*/,
    uint32_t /*serial*/,
    uint32_t /*time*/,
    uint32_t button,
    uint32_t state
) {
    auto* ctx = static_cast<DaemonContext*>(data);
    if (!ctx) return;
    if (auto* ns = ctx->find_notify_surface(ctx->focused_surface)) {
        ns->on_pointer_button(button, state);
    } else if (auto* ms = ctx->find_music_surface(ctx->focused_surface)) {
        ms->on_pointer_button(button, state);
    }
}

void pointer_axis(
    void* /*data*/,
    wl_pointer* /*wl_pointer*/,
    uint32_t /*time*/,
    uint32_t /*axis*/,
    wl_fixed_t /*value*/
) {}

void pointer_frame(void* /*data*/, wl_pointer* /*wl_pointer*/) {}
void pointer_axis_source(void* /*data*/, wl_pointer* /*wl_pointer*/, uint32_t /*axis_source*/) {}
void pointer_axis_stop(void* /*data*/, wl_pointer* /*wl_pointer*/, uint32_t /*time*/, uint32_t /*axis*/) {}
void pointer_axis_discrete(void* /*data*/, wl_pointer* /*wl_pointer*/, uint32_t /*axis*/, int32_t /*discrete*/) {}

constexpr wl_pointer_listener kPointerListener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
    .axis_value120 = nullptr,
    .axis_relative_direction = nullptr,
    .warp = nullptr,
};

void seat_capabilities(void* data, wl_seat* seat, uint32_t caps) {
    auto* ctx = static_cast<DaemonContext*>(data);
    if (!ctx) return;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !ctx->pointer) {
        ctx->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(ctx->pointer, &kPointerListener, ctx);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && ctx->pointer) {
        wl_pointer_destroy(ctx->pointer);
        ctx->pointer = nullptr;
    }
}

void seat_name(void* /*data*/, wl_seat* /*seat*/, const char* /*name*/) {}

constexpr wl_seat_listener kSeatListener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

void registry_global(
    void* data,
    wl_registry* registry,
    uint32_t name,
    const char* interface,
    uint32_t version
) {
    auto* ctx = static_cast<DaemonContext*>(data);
    if (!ctx || !interface) return;

    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        ctx->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u))
        );
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        ctx->shm = static_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1u)
        );
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        ctx->seat = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 5u))
        );
        wl_seat_add_listener(ctx->seat, &kSeatListener, ctx);
    } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        ctx->layer_shell = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u))
        );
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        // Bind every monitor (wl_output) so widgets appear on all screens
        auto* out = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4u))
        );
        if (out) {
            // Remove any fallback nullptr output slot if present
            std::erase_if(ctx->outputs, [](const std::unique_ptr<OutputSlot>& s) {
                return s->output == nullptr && s->registry_name == 0;
            });

            auto slot = std::make_unique<OutputSlot>();
            slot->registry_name = name;
            slot->output = out;
            if (ctx->surfaces_ready) {
                ctx->init_slot_surfaces(*slot);
            }
            ctx->outputs.push_back(std::move(slot));
        }
    }
}

void registry_global_remove(void* data, wl_registry* /*registry*/, uint32_t name) {
    auto* ctx = static_cast<DaemonContext*>(data);
    if (!ctx) return;
    std::erase_if(ctx->outputs, [name](const std::unique_ptr<OutputSlot>& s) {
        return s->registry_name == name;
    });
}

constexpr wl_registry_listener kRegistryListener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

void arm_timerfd(int tfd, int64_t interval_ms) {
    itimerspec its{};
    its.it_value.tv_sec = interval_ms / 1000;
    its.it_value.tv_nsec = (interval_ms % 1000) * 1000000L;
    its.it_interval = its.it_value;
    timerfd_settime(tfd, 0, &its, nullptr);
}

} // namespace

int main(int argc, char** argv) {
    DaemonContext ctx;

    std::string initial_profile;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: p5-rice-daemon [options]\n"
                      << "Native C++23 Wayland Layer-Shell daemon for Persona 5 Rice (Umbriel + Noctalia)\n\n"
                      << "Options:\n"
                      << "  --no-notify          Disable Persona 5 notification banner overlay\n"
                      << "  --no-music           Disable Persona 5 Futaba desktop music widget\n"
                      << "  --profile <slug>     Override initial profile (default: read from ~/.config/noctalia/p5-profile)\n"
                      << "  -v, --version        Show version\n"
                      << "  -h, --help           Show this help\n";
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::cout << "p5-rice-daemon 2.1.0 (native C++23 / multi-monitor zwlr-layer-shell-v1)\n";
            return 0;
        }
        if (arg == "--no-notify") {
            ctx.enable_notify = false;
        } else if (arg == "--no-music") {
            ctx.enable_music = false;
        } else if (arg == "--profile" && i + 1 < argc) {
            initial_profile = argv[++i];
        }
    }

    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);

    ctx.display = wl_display_connect(nullptr);
    if (!ctx.display) {
        std::cerr << "[p5-rice-daemon] Failed to connect to Wayland display.\n";
        return 1;
    }

    ctx.registry = wl_display_get_registry(ctx.display);
    wl_registry_add_listener(ctx.registry, &kRegistryListener, &ctx);
    wl_display_roundtrip(ctx.display);
    wl_display_roundtrip(ctx.display);

    if (!ctx.compositor || !ctx.shm || !ctx.layer_shell) {
        std::cerr << "[p5-rice-daemon] Missing required Wayland globals (wl_compositor, wl_shm, or zwlr_layer_shell_v1).\n";
        wl_display_disconnect(ctx.display);
        return 1;
    }

    ctx.cursor_theme = wl_cursor_theme_load(nullptr, 24, ctx.shm);
    ctx.cursor_surface = wl_compositor_create_surface(ctx.compositor);

    ctx.fonts = p5::register_all_custom_fonts();
    ctx.active_profile = initial_profile.empty() ? p5::read_active_profile() : initial_profile;

    // If persona notifications are enabled, disable Noctalia's built-in notification toast daemon
    // and suppress internal Noctalia toast popups so all notifications render through Persona 5.
    if (ctx.enable_notify) {
        ctx.sync_noctalia_notification_suppression(ctx.active_profile == "persona");
    }

    p5::DbusMpris mpris([&]() {
        for (auto& slot : ctx.outputs) {
            if (slot->music_surface) {
                slot->music_surface->on_mpris_updated();
            }
        }
    });
    ctx.mpris = &mpris;
    if (ctx.enable_music) {
        mpris.start();
    }

    ctx.init_all_outputs();

    auto push_notification_to_all = [&](const p5::NotificationEvent& ev) {
        if (!ctx.enable_notify) return;
        const uint64_t uid = ctx.next_notif_uid++;
        std::string summary = p5::strip_markup(ev.summary);
        std::string app_name = p5::strip_markup(ev.app_name);
        std::string body = p5::strip_markup(ev.body);
        std::string tag = (body.empty() && !summary.empty())
            ? (!app_name.empty() ? app_name : "Notice")
            : (!summary.empty() ? summary : (!app_name.empty() ? app_name : "Notification"));
        const int box_idx = p5::P5NotifySurface::pick_box_index_for_tag(tag);

        bool first = true;
        for (auto& slot : ctx.outputs) {
            if (slot->notify_surface) {
                slot->notify_surface->push_notification(ev, uid, box_idx, first);
                first = false;
            }
        }
    };

    p5::ControlCallbacks cbs;
    cbs.on_notification = push_notification_to_all;
    cbs.on_profile_change = [&](const std::string& prof) {
        auto pfile = p5::config_home() / "noctalia" / "p5-profile";
        std::error_code ec;
        std::filesystem::create_directories(pfile.parent_path(), ec);
        std::ofstream(pfile) << prof << "\n";
        ctx.apply_profile(prof);
    };
    cbs.get_profile = [&]() { return ctx.active_profile; };
    cbs.on_dismiss_all = [&]() {
        for (auto& slot : ctx.outputs) {
            if (slot->notify_surface) {
                slot->notify_surface->clear_all();
            }
        }
    };
    cbs.on_set_music_visible = [&](bool vis) {
        ctx.set_music_widget_visible(vis);
    };
    cbs.on_toggle_music = [&]() {
        ctx.set_music_widget_visible(!ctx.music_visible);
    };
    cbs.on_play_pause = [&]() {
        if (ctx.enable_music) mpris.toggle_playing();
    };
    cbs.on_next = [&]() {
        if (ctx.enable_music) mpris.next();
    };
    cbs.on_prev = [&]() {
        if (ctx.enable_music) mpris.previous();
    };

    p5::DbusNotifications dbus_notif(std::move(cbs));
    dbus_notif.start();

    wl_display_roundtrip(ctx.display);

    // Set up inotify for profile file changes AND Noctalia internal screenshot saves
    int inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    int wd_pictures = -1;
    int wd_screenshots = -1;
    std::filesystem::path pictures_dir;
    std::filesystem::path screenshots_dir;
    std::string last_screenshot_notified;

    if (inotify_fd >= 0) {
        auto noctalia_dir = p5::config_home() / "noctalia";
        auto dms_dir = p5::config_home() / "dms";
        std::error_code ec;
        std::filesystem::create_directories(noctalia_dir, ec);
        inotify_add_watch(inotify_fd, noctalia_dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
        if (std::filesystem::exists(dms_dir, ec)) {
            inotify_add_watch(inotify_fd, dms_dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
        }

        const char* home_env = std::getenv("HOME");
        if (home_env && *home_env) {
            pictures_dir = std::filesystem::path(home_env) / "Pictures";
            screenshots_dir = pictures_dir / "Screenshots";
            if (std::filesystem::exists(pictures_dir, ec)) {
                wd_pictures = inotify_add_watch(inotify_fd, pictures_dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO);
            }
            if (std::filesystem::exists(screenshots_dir, ec)) {
                wd_screenshots = inotify_add_watch(inotify_fd, screenshots_dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO);
            }
        }
    }

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    int64_t current_timer_interval = 250;
    arm_timerfd(tfd, current_timer_interval);

    int64_t last_mpris_poll = p5::now_ms();
    int64_t last_notif_claim = p5::now_ms();

    while (g_running.load()) {
        while (wl_display_prepare_read(ctx.display) != 0) {
            wl_display_dispatch_pending(ctx.display);
        }
        wl_display_flush(ctx.display);

        std::vector<pollfd> fds;
        fds.push_back({wl_display_get_fd(ctx.display), POLLIN, 0});
        fds.push_back({tfd, POLLIN, 0});

        const int notif_pfd = dbus_notif.primary_fd();
        if (notif_pfd >= 0) fds.push_back({notif_pfd, POLLIN, 0});

        const int notif_mfd = dbus_notif.monitor_fd();
        if (notif_mfd >= 0) fds.push_back({notif_mfd, POLLIN, 0});

        const int mpris_fd = mpris.fd();
        if (mpris_fd >= 0) fds.push_back({mpris_fd, POLLIN, 0});

        if (inotify_fd >= 0) fds.push_back({inotify_fd, POLLIN, 0});

        int ret = poll(fds.data(), fds.size(), -1);
        if (ret < 0) {
            wl_display_cancel_read(ctx.display);
            if (errno == EINTR) continue;
            break;
        }

        if (fds[0].revents & POLLIN) {
            wl_display_read_events(ctx.display);
            wl_display_dispatch_pending(ctx.display);
        } else {
            wl_display_cancel_read(ctx.display);
        }

        for (size_t i = 1; i < fds.size(); ++i) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            const int fd = fds[i].fd;
            if (fd == tfd) {
                uint64_t expirations = 0;
                (void)read(tfd, &expirations, sizeof(expirations));
                const int64_t now = p5::now_ms();
                for (auto& slot : ctx.outputs) {
                    if (slot->notify_surface) slot->notify_surface->tick(now);
                    if (slot->music_surface) slot->music_surface->tick(now);
                }
                if (now - last_mpris_poll >= 500) {
                    last_mpris_poll = now;
                    mpris.poll_position();
                }
                if (ctx.enable_notify && ctx.active_profile == "persona" && now - last_notif_claim >= 2000) {
                    last_notif_claim = now;
                    dbus_notif.try_claim_notifications_name();
                }
            } else if (fd == notif_pfd) {
                dbus_notif.process_primary();
            } else if (fd == notif_mfd) {
                dbus_notif.process_monitor();
            } else if (fd == mpris_fd) {
                mpris.process();
            } else if (fd == inotify_fd) {
                alignas(inotify_event) char buf[4096];
                ssize_t n = 0;
                bool check_profile = false;
                while ((n = read(inotify_fd, buf, sizeof(buf))) > 0) {
                    const char* ptr = buf;
                    while (ptr < buf + n) {
                        const auto* ev = reinterpret_cast<const inotify_event*>(ptr);
                        if (ev->wd == wd_pictures || ev->wd == wd_screenshots) {
                            if (ev->len > 0 && ctx.enable_notify && ctx.active_profile == "persona") {
                                std::string fname(ev->name);
                                if (fname.starts_with("screenshot") &&
                                    (fname.ends_with(".png") || fname.ends_with(".jpg") || fname.ends_with(".webp")) &&
                                    fname != last_screenshot_notified) {
                                    last_screenshot_notified = fname;
                                    const auto& base_dir = (ev->wd == wd_screenshots) ? screenshots_dir : pictures_dir;
                                    std::string full_path = (base_dir / fname).string();
                                    p5::NotificationEvent nev;
                                    nev.app_name = "Noctalia";
                                    nev.summary = "Screenshot saved";
                                    nev.body = full_path;
                                    nev.expire_timeout = 4500;
                                    push_notification_to_all(nev);
                                }
                            }
                        } else {
                            check_profile = true;
                        }
                        ptr += sizeof(inotify_event) + ev->len;
                    }
                }
                if (check_profile) {
                    ctx.apply_profile(p5::read_active_profile());
                }
            }
        }

        // Adapt timer rate: 16ms (~60fps) during active animations, 250ms when idle
        const int64_t now = p5::now_ms();
        bool animating = false;
        for (const auto& slot : ctx.outputs) {
            if ((slot->notify_surface && slot->notify_surface->is_animating(now)) ||
                (slot->music_surface && slot->music_surface->is_animating(now))) {
                animating = true;
                break;
            }
        }
        const int64_t desired_interval = animating ? 16 : 250;
        if (desired_interval != current_timer_interval) {
            current_timer_interval = desired_interval;
            arm_timerfd(tfd, current_timer_interval);
        }

        wl_display_flush(ctx.display);
    }

    if (tfd >= 0) close(tfd);
    if (inotify_fd >= 0) close(inotify_fd);

    ctx.outputs.clear();

    if (ctx.cursor_surface) wl_surface_destroy(ctx.cursor_surface);
    if (ctx.cursor_theme) wl_cursor_theme_destroy(ctx.cursor_theme);
    if (ctx.pointer) wl_pointer_destroy(ctx.pointer);
    if (ctx.seat) wl_seat_destroy(ctx.seat);
    if (ctx.layer_shell) zwlr_layer_shell_v1_destroy(ctx.layer_shell);
    if (ctx.shm) wl_shm_destroy(ctx.shm);
    if (ctx.compositor) wl_compositor_destroy(ctx.compositor);
    if (ctx.registry) wl_registry_destroy(ctx.registry);
    if (ctx.display) wl_display_disconnect(ctx.display);

    return 0;
}
