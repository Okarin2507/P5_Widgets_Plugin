#include "common.hpp"
#include "dbus_mpris.hpp"
#include "dbus_notifications.hpp"
#include "p5music_surface.hpp"
#include "p5notify_surface.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

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
#include <wayland-client.h>
#include <wayland-cursor.h>

namespace {

std::atomic<bool> g_running{true};

void sig_handler(int /*sig*/) {
    g_running.store(false);
}

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

    std::unique_ptr<p5::P5NotifySurface> notify_surface;
    std::unique_ptr<p5::P5MusicSurface> music_surface;
    wl_surface* focused_surface = nullptr;

    std::string active_profile = "persona";

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

    void apply_profile(const std::string& prof) {
        if (prof.empty()) return;
        active_profile = prof;
        const bool is_persona = (prof == "persona");
        if (notify_surface) {
            notify_surface->set_persona_active(is_persona);
        }
        if (music_surface) {
            music_surface->set_visible(is_persona);
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

    if (ctx->notify_surface && surface == ctx->notify_surface->surface()) {
        ctx->set_cursor(serial, "pointer");
        ctx->notify_surface->on_pointer_motion(x, y);
    } else if (ctx->music_surface && surface == ctx->music_surface->surface()) {
        ctx->set_cursor(serial, "pointer");
        ctx->music_surface->on_pointer_motion(x, y);
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
    if (ctx->notify_surface && surface == ctx->notify_surface->surface()) {
        ctx->notify_surface->on_pointer_leave();
    } else if (ctx->music_surface && surface == ctx->music_surface->surface()) {
        ctx->music_surface->on_pointer_leave();
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
    if (ctx->notify_surface && ctx->focused_surface == ctx->notify_surface->surface()) {
        ctx->notify_surface->on_pointer_motion(x, y);
    } else if (ctx->music_surface && ctx->focused_surface == ctx->music_surface->surface()) {
        ctx->music_surface->on_pointer_motion(x, y);
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
    if (ctx->notify_surface && ctx->focused_surface == ctx->notify_surface->surface()) {
        ctx->notify_surface->on_pointer_button(button, state);
    } else if (ctx->music_surface && ctx->focused_surface == ctx->music_surface->surface()) {
        ctx->music_surface->on_pointer_button(button, state);
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
    }
}

void registry_global_remove(void* /*data*/, wl_registry* /*registry*/, uint32_t /*name*/) {}

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
    bool enable_notify = true;
    bool enable_music = true;
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
            std::cout << "p5-rice-daemon 2.0.0 (native C++23 / zwlr-layer-shell-v1)\n";
            return 0;
        }
        if (arg == "--no-notify") {
            enable_notify = false;
        } else if (arg == "--no-music") {
            enable_music = false;
        } else if (arg == "--profile" && i + 1 < argc) {
            initial_profile = argv[++i];
        }
    }

    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);

    DaemonContext ctx;
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

    const p5::LoadedFonts fonts = p5::register_all_custom_fonts();
    ctx.active_profile = initial_profile.empty() ? p5::read_active_profile() : initial_profile;

    p5::DbusMpris mpris([&]() {
        if (ctx.music_surface) {
            ctx.music_surface->on_mpris_updated();
        }
    });
    if (enable_music) {
        mpris.start();
    }

    if (enable_notify) {
        ctx.notify_surface = std::make_unique<p5::P5NotifySurface>(
            ctx.compositor,
            ctx.layer_shell,
            ctx.shm,
            fonts
        );
        ctx.notify_surface->init();
    }

    if (enable_music) {
        ctx.music_surface = std::make_unique<p5::P5MusicSurface>(
            ctx.compositor,
            ctx.layer_shell,
            ctx.shm,
            &mpris,
            fonts
        );
        ctx.music_surface->init();
    }

    ctx.apply_profile(ctx.active_profile);

    p5::ControlCallbacks cbs;
    cbs.on_notification = [&](const p5::NotificationEvent& ev) {
        if (ctx.notify_surface) {
            ctx.notify_surface->push_notification(ev);
        }
    };
    cbs.on_profile_change = [&](const std::string& prof) {
        auto pfile = p5::config_home() / "noctalia" / "p5-profile";
        std::error_code ec;
        std::filesystem::create_directories(pfile.parent_path(), ec);
        std::ofstream(pfile) << prof << "\n";
        ctx.apply_profile(prof);
    };
    cbs.get_profile = [&]() { return ctx.active_profile; };
    cbs.on_dismiss_all = [&]() {
        if (ctx.notify_surface) {
            ctx.notify_surface->clear_all();
        }
    };
    cbs.on_set_music_visible = [&](bool vis) {
        if (ctx.music_surface) {
            ctx.music_surface->set_visible(vis);
        }
    };

    p5::DbusNotifications dbus_notif(std::move(cbs));
    dbus_notif.start();

    wl_display_roundtrip(ctx.display);

    // Set up inotify for profile file changes
    int inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd >= 0) {
        auto noctalia_dir = p5::config_home() / "noctalia";
        auto dms_dir = p5::config_home() / "dms";
        std::error_code ec;
        std::filesystem::create_directories(noctalia_dir, ec);
        inotify_add_watch(inotify_fd, noctalia_dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
        if (std::filesystem::exists(dms_dir, ec)) {
            inotify_add_watch(inotify_fd, dms_dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
        }
    }

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    int64_t current_timer_interval = 250;
    arm_timerfd(tfd, current_timer_interval);

    int64_t last_mpris_poll = p5::now_ms();

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
            if (!(fds[i].revents & POLLIN)) continue;
            const int fd = fds[i].fd;
            if (fd == tfd) {
                uint64_t expirations = 0;
                (void)read(tfd, &expirations, sizeof(expirations));
                const int64_t now = p5::now_ms();
                if (ctx.notify_surface) ctx.notify_surface->tick(now);
                if (ctx.music_surface) ctx.music_surface->tick(now);
                if (now - last_mpris_poll >= 500) {
                    last_mpris_poll = now;
                    mpris.poll_position();
                }
            } else if (fd == notif_pfd) {
                dbus_notif.process_primary();
            } else if (fd == notif_mfd) {
                dbus_notif.process_monitor();
            } else if (fd == mpris_fd) {
                mpris.process();
            } else if (fd == inotify_fd) {
                char buf[512];
                while (read(inotify_fd, buf, sizeof(buf)) > 0) {}
                ctx.apply_profile(p5::read_active_profile());
            }
        }

        // Adapt timer rate: 16ms (~60fps) during active animations, 250ms when idle
        const int64_t now = p5::now_ms();
        const bool animating =
            (ctx.notify_surface && ctx.notify_surface->is_animating(now)) ||
            (ctx.music_surface && ctx.music_surface->is_animating(now));
        const int64_t desired_interval = animating ? 16 : 250;
        if (desired_interval != current_timer_interval) {
            current_timer_interval = desired_interval;
            arm_timerfd(tfd, current_timer_interval);
        }

        wl_display_flush(ctx.display);
    }

    if (tfd >= 0) close(tfd);
    if (inotify_fd >= 0) close(inotify_fd);

    ctx.music_surface.reset();
    ctx.notify_surface.reset();

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
