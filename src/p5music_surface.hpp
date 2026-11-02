#pragma once

#include "common.hpp"
#include "dbus_mpris.hpp"
#include "jitter_text.hpp"
#include "shm_buffer.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <cairo/cairo.h>
#include <cstdint>
#include <string>
#include <wayland-client.h>

namespace p5 {

class P5MusicSurface {
public:
    P5MusicSurface(
        wl_compositor* compositor,
        zwlr_layer_shell_v1* layer_shell,
        wl_shm* shm,
        DbusMpris* mpris,
        const LoadedFonts& fonts
    );
    ~P5MusicSurface();

    P5MusicSurface(const P5MusicSurface&) = delete;
    P5MusicSurface& operator=(const P5MusicSurface&) = delete;

    bool init();
    void destroy();

    wl_surface* surface() const { return surface_; }

    void set_visible(bool visible);
    bool visible() const { return visible_; }

    void on_mpris_updated();
    bool tick(int64_t now);
    bool is_animating(int64_t now) const;

    // Pointer interaction
    void on_pointer_motion(double x, double y);
    void on_pointer_leave();
    void on_pointer_button(uint32_t button, uint32_t state);

    // Wayland callbacks
    void handle_configure(uint32_t serial, uint32_t width, uint32_t height);
    void handle_closed();

private:
    static constexpr double kDesignW = 980.0;
    static constexpr double kDesignH = 545.0;
    static constexpr int kDefaultW = 780;
    static constexpr int kDefaultH = 434;

    enum class HitZone {
        None = 0,
        AlbumCard,
        SeekBar,
        TimeToggle,
        FutabaFrame,
        BtnPrev,
        BtnPlay,
        BtnNext,
        BtnShuffle,
    };

    wl_compositor* compositor_ = nullptr;
    zwlr_layer_shell_v1* layer_shell_ = nullptr;
    wl_shm* shm_ = nullptr;
    DbusMpris* mpris_ = nullptr;
    LoadedFonts fonts_;

    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
    ShmPool pool_{};

    cairo_surface_t* banner_img_ = nullptr;
    cairo_surface_t* futaba_img_ = nullptr;

    int width_ = kDefaultW;
    int height_ = kDefaultH;
    bool configured_ = false;
    bool visible_ = true;
    bool show_remaining_ = false;

    // Track change animation state (380ms OutCubic slide + fade)
    std::string last_track_title_;
    int64_t track_anim_start_ms_ = 0;

    // Pointer state
    bool pointer_inside_ = false;
    bool pointer_pressed_ = false;
    double pointer_x_ = -1.0;
    double pointer_y_ = -1.0;
    HitZone hovered_zone_ = HitZone::None;

    // Cached button dimensions from last render for accurate hit testing
    double btn_prev_w_ = 220.0, btn_prev_h_ = 75.0;
    double btn_play_w_ = 150.0, btn_play_h_ = 75.0;
    double btn_next_w_ = 220.0, btn_next_h_ = 75.0;

    void render(int64_t now);
    void update_input_region();
    HitZone hit_test(double px, double py, double* out_seek_frac = nullptr) const;
    void draw_button(
        cairo_t* cr,
        double x,
        double y,
        double rot_deg,
        const std::string& label,
        const std::vector<int>& boxed,
        double seed,
        bool hovered,
        bool pressed,
        double& out_w,
        double& out_h
    );
    void draw_shuffle(cairo_t* cr, bool hovered, bool pressed);
};

} // namespace p5

