#include "music/music_surface.hpp"

#include "music/jitter_text.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <linux/input-event-codes.h>
#include <numbers>
#include <pango/pangocairo.h>

namespace p5 {

namespace {

void music_layer_configure(
    void* data,
    zwlr_layer_surface_v1* /*surface*/,
    uint32_t serial,
    uint32_t width,
    uint32_t height
) {
    auto* self = static_cast<MusicSurface*>(data);
    if (self) {
        self->handle_configure(serial, width, height);
    }
}

void music_layer_closed(void* data, zwlr_layer_surface_v1* /*surface*/) {
    auto* self = static_cast<MusicSurface*>(data);
    if (self) {
        self->handle_closed();
    }
}

constexpr zwlr_layer_surface_v1_listener kMusicLayerListener = {
    .configure = music_layer_configure,
    .closed = music_layer_closed,
};

// Inverse-transform a canvas-space point into an item rotated around its center
bool point_in_rotated_center_box(
    double cx_pt,
    double cy_pt,
    double item_x,
    double item_y,
    double item_w,
    double item_h,
    double rot_deg,
    double margin = 0.0
) {
    const double center_x = item_x + item_w * 0.5;
    const double center_y = item_y + item_h * 0.5;
    const double dx = cx_pt - center_x;
    const double dy = cy_pt - center_y;
    const double rad = -rot_deg * std::numbers::pi / 180.0;
    const double lx = dx * std::cos(rad) - dy * std::sin(rad) + item_w * 0.5;
    const double ly = dx * std::sin(rad) + dy * std::cos(rad) + item_h * 0.5;
    return (lx >= -margin && lx <= item_w + margin && ly >= -margin && ly <= item_h + margin);
}

} // namespace

MusicSurface::MusicSurface(
    wl_compositor* compositor,
    zwlr_layer_shell_v1* layer_shell,
    wl_shm* shm,
    wl_output* output,
    DbusMpris* mpris,
    const LoadedFonts& fonts,
    const AssetConfig& assets
)
    : compositor_(compositor),
      layer_shell_(layer_shell),
      shm_(shm),
      output_(output),
      mpris_(mpris),
      fonts_(fonts) {
    if (!assets.music_banner.empty()) {
        banner_img_ = load_shared_image(assets.music_banner);
    }
    if (!banner_img_) {
        banner_img_ = load_shared_image(resolve_asset_path("personaMusic", "assets/banner.png"));
    }
    if (!banner_img_) {
        banner_img_ = adopt_image(create_fallback_music_banner(1223, 344));
    }

    std::error_code ec;
    if (!assets.music_character_frame.empty()) {
        futaba_img_ = load_shared_image_scaled(assets.music_character_frame, 352, 272);
        if (futaba_img_) {
            character_name_ = assets.music_character_name.empty()
                ? "Custom"
                : assets.music_character_name;
            character_inv_ = {1};
        }
    }

    auto char_custom_path = resolve_asset_path("personaMusic", "assets/character_frame.png");
    if (!futaba_img_ && std::filesystem::exists(char_custom_path, ec)) {
        futaba_img_ = load_shared_image_scaled(char_custom_path, 352, 272);
        auto name_file = resolve_asset_path("personaMusic", "assets/character_name");
        if (std::filesystem::exists(name_file, ec)) {
            std::ifstream in(name_file);
            std::string n;
            if (in >> n && !n.empty()) {
                character_name_ = n;
                character_inv_ = {1};
            }
        } else {
            character_name_ = "Sumire";
            character_inv_ = {1};
        }
    }

    if (!futaba_img_) {
        auto sumire_path = resolve_asset_path("personaMusic", "assets/sumire_frame.png");
        if (std::filesystem::exists(sumire_path, ec)) {
            futaba_img_ = load_shared_image_scaled(sumire_path, 352, 272);
            character_name_ = "Sumire";
            character_inv_ = {1};
        }
    }

    if (!futaba_img_) {
        futaba_img_ = load_shared_image_scaled(
            resolve_asset_path("personaMusic", "assets/futaba_frame.png"), 352, 272);
        character_name_ = "Futaba";
        character_inv_ = {2};
    }

    if (!futaba_img_) {
        futaba_img_ = adopt_image(create_fallback_futaba_frame(352, 272));
    }

    if (!assets.music_character_name.empty()) {
        character_name_ = assets.music_character_name;
        character_inv_ = {1};
    }
}

MusicSurface::~MusicSurface() {
    destroy();
}

bool MusicSurface::init() {
    if (!compositor_ || !layer_shell_ || !shm_) return false;

    surface_ = wl_compositor_create_surface(compositor_);
    if (!surface_) return false;

    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell_,
        surface_,
        output_,
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        "noctalia-desktop-widget-personaMusic"
    );
    if (!layer_surface_) {
        wl_surface_destroy(surface_);
        surface_ = nullptr;
        return false;
    }

    zwlr_layer_surface_v1_add_listener(layer_surface_, &kMusicLayerListener, this);
    zwlr_layer_surface_v1_set_anchor(
        layer_surface_,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
    );
    zwlr_layer_surface_v1_set_margin(layer_surface_, 12, 0, 0, 16);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layer_surface_,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE
    );
    zwlr_layer_surface_v1_set_size(
        layer_surface_,
        static_cast<uint32_t>(width_),
        static_cast<uint32_t>(height_)
    );

    update_input_region();
    wl_surface_commit(surface_);
    return true;
}

void MusicSurface::destroy() {
    pool_.release_all();
    if (layer_surface_) {
        zwlr_layer_surface_v1_destroy(layer_surface_);
        layer_surface_ = nullptr;
    }
    if (surface_) {
        wl_surface_destroy(surface_);
        surface_ = nullptr;
    }
    configured_ = false;
}

void MusicSurface::set_visible(bool visible) {
    if (visible_ != visible) {
        visible_ = visible;
        update_input_region();
        if (configured_) {
            render(now_ms());
        }
    }
}

void MusicSurface::on_mpris_updated() {
    if (!mpris_) return;
    const auto& st = mpris_->state();
    if (st.track_title != last_track_title_) {
        last_track_title_ = st.track_title;
        track_anim_start_ms_ = now_ms();
    }
    if (configured_ && visible_) {
        render(now_ms());
    }
}

bool MusicSurface::is_animating(int64_t now) const {
    return visible_ && (now - track_anim_start_ms_ <= 400);
}

bool MusicSurface::tick(int64_t now) {
    if (!visible_ || !configured_) return false;
    if (is_animating(now)) {
        render(now);
        return true;
    }
    return false;
}

void MusicSurface::update_input_region() {
    if (!compositor_ || !surface_) return;
    wl_region* reg = wl_compositor_create_region(compositor_);
    if (visible_) {
        wl_region_add(reg, 0, 0, width_, height_);
    }
    wl_surface_set_input_region(surface_, reg);
    wl_region_destroy(reg);
}

MusicSurface::HitZone MusicSurface::hit_test(double px, double py, double* out_seek_frac) const {
    if (!visible_) return HitZone::None;
    const double stage_scale = std::min(
        static_cast<double>(width_) / kDesignW,
        static_cast<double>(height_) / kDesignH
    );
    if (stage_scale <= 0.0) return HitZone::None;

    // Convert surface coordinates -> stage -> canvas coordinates (canvas offset is -25, -85)
    const double cx = px / stage_scale + 25.0;
    const double cy = py / stage_scale + 85.0;

    // 1. Controls on top. The row uses fixed, generous regions so the whole
    // sticker reacts, including the rotated glyphs and their outline rings.
    const double prev_w = std::max(btn_prev_w_, 220.0) + kBtnHitPad * 2.0;
    const double prev_h = std::max(btn_prev_h_, 74.0) + kBtnHitPad * 2.0;
    if (point_in_rotated_center_box(cx, cy,
                                    kBtnPrevX - kBtnHitPad,
                                    kBtnPrevY - kBtnHitPad,
                                    prev_w, prev_h, -4.0)) {
        return HitZone::BtnPrev;
    }

    const double play_w = std::max(btn_play_w_, 150.0) + kBtnHitPad * 2.0;
    const double play_h = std::max(btn_play_h_, 74.0) + kBtnHitPad * 2.0;
    if (point_in_rotated_center_box(cx, cy,
                                    kBtnPlayX - kBtnHitPad,
                                    kBtnPlayY - kBtnHitPad,
                                    play_w, play_h, -6.0)) {
        return HitZone::BtnPlay;
    }

    const double next_w = std::max(btn_next_w_, 220.0) + kBtnHitPad * 2.0;
    const double next_h = std::max(btn_next_h_, 74.0) + kBtnHitPad * 2.0;
    if (point_in_rotated_center_box(cx, cy,
                                    kBtnNextX - kBtnHitPad,
                                    kBtnNextY - kBtnHitPad,
                                    next_w, next_h, 2.0)) {
        return HitZone::BtnNext;
    }

    if (point_in_rotated_center_box(cx, cy,
                                    kBtnShuffleX - kBtnHitPad,
                                    kBtnShuffleY - kBtnHitPad,
                                    74.0 + kBtnHitPad * 2.0,
                                    52.0 + kBtnHitPad * 2.0, -4.0)) {
        return HitZone::BtnShuffle;
    }

    // 2. Futaba frame (x=33, y=95, w=352, h=272)
    if (cx >= 33.0 && cx <= 33.0 + 352.0 && cy >= 95.0 && cy <= 95.0 + 272.0) {
        return HitZone::FutabaFrame;
    }

    // 3. Inside bannerBox (x=240, y=240, scale=0.601, rotation=3 deg around TopLeft)
    {
        const double dx = cx - 240.0;
        const double dy = cy - 240.0;
        const double rad = -3.0 * std::numbers::pi / 180.0;
        const double bx = (dx * std::cos(rad) - dy * std::sin(rad)) / 0.601;
        const double by = (dx * std::sin(rad) + dy * std::cos(rad)) / 0.601;

        // Seek bar inside bannerBox: x=300, y=240, w=640, h=12, rot=5.2 deg around TopLeft, margins -18 top/bottom
        const double sdx = bx - 300.0;
        const double sdy = by - 240.0;
        const double srad = -5.2 * std::numbers::pi / 180.0;
        const double sx = sdx * std::cos(srad) - sdy * std::sin(srad);
        const double sy = sdx * std::sin(srad) + sdy * std::cos(srad);
        if (sx >= 0.0 && sx <= 640.0 && sy >= -28.0 && sy <= 12.0 + 28.0) {
            if (out_seek_frac) {
                *out_seek_frac = std::clamp(sx / 640.0, 0.0, 1.0);
            }
            return HitZone::SeekBar;
        }

        // timeMa inside bannerBox: x=1000, y=100, w=220, h=170
        if (bx >= 1000.0 && bx <= 1220.0 && by >= 100.0 && by <= 270.0) {
            return HitZone::TimeToggle;
        }
    }

    // 4. Album card (x=535, y=138, w=213, h=213, rot=17 deg around center)
    if (point_in_rotated_center_box(cx, cy, 535.0, 138.0, 213.0, 213.0, 17.0)) {
        return HitZone::AlbumCard;
    }

    return HitZone::None;
}

void MusicSurface::on_pointer_motion(double x, double y) {
    pointer_inside_ = true;
    pointer_x_ = x;
    pointer_y_ = y;
    HitZone hz = hit_test(x, y);
    if (hz != hovered_zone_) {
        hovered_zone_ = hz;
        if (configured_ && visible_) {
            render(now_ms());
        }
    }
}

void MusicSurface::on_pointer_leave() {
    pointer_inside_ = false;
    pointer_pressed_ = false;
    if (hovered_zone_ != HitZone::None) {
        hovered_zone_ = HitZone::None;
        if (configured_ && visible_) {
            render(now_ms());
        }
    }
}

void MusicSurface::on_pointer_button(uint32_t button, uint32_t state) {
    if (button != BTN_LEFT || !pointer_inside_ || !visible_) return;

    const bool pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);
    pointer_pressed_ = pressed;

    if (pressed) {
        double seek_frac = 0.0;
        HitZone hz = hit_test(pointer_x_, pointer_y_, &seek_frac);
        hovered_zone_ = hz;
        if (mpris_) {
            switch (hz) {
                case HitZone::AlbumCard:
                    mpris_->raise();
                    break;
                case HitZone::SeekBar:
                    mpris_->seek_to_fraction(seek_frac);
                    break;
                case HitZone::TimeToggle:
                    show_remaining_ = !show_remaining_;
                    break;
                case HitZone::BtnPrev:
                    mpris_->previous();
                    break;
                case HitZone::BtnPlay:
                    mpris_->toggle_playing();
                    break;
                case HitZone::BtnNext:
                    mpris_->next();
                    break;
                case HitZone::BtnShuffle:
                    mpris_->toggle_shuffle();
                    break;
                default:
                    break;
            }
        }
    }

    if (configured_) {
        render(now_ms());
    }
}

void MusicSurface::handle_configure(uint32_t serial, uint32_t width, uint32_t height) {
    zwlr_layer_surface_v1_ack_configure(layer_surface_, serial);
    if (width > 0) width_ = static_cast<int>(width);
    if (height > 0) height_ = static_cast<int>(height);
    configured_ = true;
    update_input_region();
    render(now_ms());
}

void MusicSurface::handle_closed() {
    configured_ = false;
}

void MusicSurface::draw_button(
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
) {
    JitterSpec spec;
    spec.text = label;
    spec.family = fonts_.music_persona;
    spec.pixel_size = 42.0;
    spec.pad = 10.0;
    spec.seed = seed;
    spec.rings = 14;
    spec.color = hovered ? Color{0.0, 0.0, 0.0, 1.0} : Color{1.0, 1.0, 1.0, 1.0};
    spec.edge_color = hovered ? Color{1.0, 1.0, 1.0, 1.0} : Color::from_hex("#0a0d10");
    spec.edge = hovered ? 6.5 : 3.2;
    spec.inv = boxed;
    spec.box_color = hovered ? Color{1.0, 1.0, 1.0, 1.0} : Color{0.0, 0.0, 0.0, 1.0};
    spec.box_text_color = hovered ? Color{0.0, 0.0, 0.0, 1.0} : Color{1.0, 1.0, 1.0, 1.0};
    spec.box_border = Color{0.0, 0.0, 0.0, 1.0};

    JitterMetrics m = measure_jitter_text(cr, spec);
    out_w = m.width;
    out_h = m.height;

    const double scl = pressed ? 0.92 : (hovered ? 1.1 : 1.0);

    cairo_save(cr);
    cairo_translate(cr, x + m.width * 0.5, y + m.height * 0.5);
    cairo_rotate(cr, rot_deg * std::numbers::pi / 180.0);
    cairo_scale(cr, scl, scl);
    cairo_translate(cr, -m.width * 0.5, -m.height * 0.5);

    draw_jitter_text(cr, spec, &m);
    cairo_restore(cr);
}

void MusicSurface::draw_shuffle(cairo_t* cr, bool hovered, bool pressed) {
    const auto& st = mpris_->state();
    const bool supported = st.has_player && st.shuffle_supported;
    const bool active = supported && st.shuffle;
    const bool lit = (hovered && pressed) || active;

    const Color fill_c = lit ? Color{0.0, 0.0, 0.0, 1.0} : Color{1.0, 1.0, 1.0, 1.0};
    const Color edge_c = lit ? Color{1.0, 1.0, 1.0, 1.0} : Color{0.0, 0.0, 0.0, 1.0};
    const double edge_w = lit ? 20.0 : 12.0;

    struct Layer {
        Color c;
        double w;
        double hw;
        double dx;
        double dy;
    };
    std::vector<Layer> layers;
    if (lit) {
        layers.push_back({Color{0.0, 0.0, 0.0, 1.0}, 30.0, 24.0, 0.0, 0.0});
    }
    constexpr int kRing = 8;
    for (int k = 0; k < kRing; ++k) {
        double a = static_cast<double>(k) * 2.0 * std::numbers::pi / static_cast<double>(kRing);
        layers.push_back({edge_c, edge_w, edge_w - 3.0, std::cos(a) * 2.6, std::sin(a) * 2.6});
    }
    layers.push_back({fill_c, 7.0, 0.0, 0.0, 0.0});

    const double rot_deg = (hovered && pressed) ? -11.0 : (hovered ? -7.0 : -4.0);
    const double scl = (hovered && pressed) ? 1.3 : (hovered ? 1.18 : 1.0);
    const double alpha = 1.0;

    cairo_save(cr);
    cairo_translate(cr, kBtnShuffleX + 37.0, kBtnShuffleY + 26.0);
    cairo_rotate(cr, rot_deg * std::numbers::pi / 180.0);
    cairo_scale(cr, scl, scl);
    cairo_translate(cr, -37.0, -26.0);

    cairo_push_group(cr);
    for (const auto& L : layers) {
        cairo_save(cr);
        cairo_translate(cr, L.dx, L.dy);
        cairo_set_source_rgba(cr, L.c.r, L.c.g, L.c.b, L.c.a);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
        cairo_set_miter_limit(cr, 8.0);
        cairo_set_line_width(cr, L.w);

        // Crossed arrow line 1
        cairo_move_to(cr, 2.0, 10.0);
        cairo_line_to(cr, 22.0, 10.0);
        cairo_line_to(cr, 44.0, 40.0);
        cairo_line_to(cr, 56.0, 40.0);
        cairo_stroke(cr);

        // Crossed arrow line 2
        cairo_move_to(cr, 2.0, 40.0);
        cairo_line_to(cr, 22.0, 40.0);
        cairo_line_to(cr, 44.0, 10.0);
        cairo_line_to(cr, 56.0, 10.0);
        cairo_stroke(cr);

        // Arrowhead 1
        cairo_move_to(cr, 54.0, 0.0);
        cairo_line_to(cr, 72.0, 10.0);
        cairo_line_to(cr, 54.0, 20.0);
        cairo_close_path(cr);
        if (L.hw > 0.0) {
            cairo_fill_preserve(cr);
            cairo_set_line_width(cr, L.hw);
            cairo_stroke(cr);
        } else {
            cairo_fill(cr);
        }

        // Arrowhead 2
        cairo_move_to(cr, 54.0, 30.0);
        cairo_line_to(cr, 72.0, 40.0);
        cairo_line_to(cr, 54.0, 50.0);
        cairo_close_path(cr);
        if (L.hw > 0.0) {
            cairo_fill_preserve(cr);
            cairo_set_line_width(cr, L.hw);
            cairo_stroke(cr);
        } else {
            cairo_fill(cr);
        }

        cairo_restore(cr);
    }
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
}

void MusicSurface::render(int64_t now) {
    if (!configured_ || !surface_ || !shm_) return;

    ShmSlot* slot = pool_.acquire(shm_, width_, height_);
    if (!slot) return;

    cairo_surface_flush(slot->cairo_surface);
    cairo_t* cr = cairo_create(slot->cairo_surface);

    // Clear transparent
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);

    if (visible_) {
        const double stage_scale = std::min(
            static_cast<double>(width_) / kDesignW,
            static_cast<double>(height_) / kDesignH
        );
        cairo_save(cr);
        cairo_scale(cr, stage_scale, stage_scale);
        cairo_translate(cr, -25.0, -85.0);

        const auto& st = mpris_->state();
        double anim_t = 1.0;
        if (track_anim_start_ms_ > 0 && now >= track_anim_start_ms_ && (now - track_anim_start_ms_) < 380) {
            anim_t = static_cast<double>(now - track_anim_start_ms_) / 380.0;
        }
        const double track_opacity = std::clamp(anim_t, 0.0, 1.0);
        const double text_group_x = 290.0 + (235.0 - 290.0) * ease_out_cubic(anim_t);

        // ---------- 1. Album Card (x=535, y=138, w=213, h=213, rot=17 deg) ----------
        {
            const double card_scale = (hovered_zone_ == HitZone::AlbumCard) ? 1.05 : 1.0;
            cairo_save(cr);
            cairo_translate(cr, 535.0 + 106.5, 138.0 + 106.5);
            cairo_rotate(cr, 17.0 * std::numbers::pi / 180.0);
            cairo_scale(cr, card_scale, card_scale);
            cairo_translate(cr, -106.5, -106.5);

            cairo_push_group(cr);
            // Shadow at (+7, +7)
            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
            cairo_rectangle(cr, 7.0, 7.0, 213.0, 213.0);
            cairo_fill(cr);

            // Background #16181c
            const Color bg = Color::from_hex("#16181c");
            cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, 1.0);
            cairo_rectangle(cr, 0.0, 0.0, 213.0, 213.0);
            cairo_fill(cr);

            cairo_surface_t* art = mpris_->art_surface();
            if (art) {
                const int aw = cairo_image_surface_get_width(art);
                const int ah = cairo_image_surface_get_height(art);
                if (aw > 0 && ah > 0) {
                    // Square cover crop: fill the 213x213 card with the
                    // artwork and crop the excess, matching normal album art.
                    cairo_save(cr);
                    cairo_rectangle(cr, 0.0, 0.0, 213.0, 213.0);
                    cairo_clip(cr);
                    const double s = std::max(
                        213.0 / static_cast<double>(aw),
                        213.0 / static_cast<double>(ah));
                    const double ox = (213.0 - aw * s) * 0.5;
                    const double oy = (213.0 - ah * s) * 0.5;
                    cairo_translate(cr, ox, oy);
                    cairo_scale(cr, s, s);
                    cairo_set_source_surface(cr, art, 0.0, 0.0);
                    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
                    cairo_paint(cr);
                    cairo_restore(cr);
                }
            } else {
                PangoFontDescription* desc = pango_font_description_new();
                pango_font_description_set_family(desc, fonts_.music_persona.c_str());
                pango_font_description_set_absolute_size(desc, 36.0 * PANGO_SCALE);
                PangoLayout* lay = pango_cairo_create_layout(cr);
                pango_layout_set_font_description(lay, desc);
                pango_layout_set_text(lay, "NO ART", -1);
                int tw = 0, th = 0;
                pango_layout_get_pixel_size(lay, &tw, &th);
                const Color fg = Color::from_hex("#555a63");
                cairo_set_source_rgba(cr, fg.r, fg.g, fg.b, 1.0);
                cairo_move_to(cr, (213.0 - tw) * 0.5, (213.0 - th) * 0.5);
                pango_cairo_show_layout(cr, lay);
                g_object_unref(lay);
                pango_font_description_free(desc);
            }

            // Border #0a0d10 width 2
            const Color ink = Color::from_hex("#0a0d10");
            cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, 1.0);
            cairo_set_line_width(cr, 2.0);
            cairo_rectangle(cr, 1.0, 1.0, 211.0, 211.0);
            cairo_stroke(cr);

            cairo_pop_group_to_source(cr);
            cairo_paint_with_alpha(cr, track_opacity);
            cairo_restore(cr);
        }

        // ---------- 2. Banner Box (x=240, y=240, w=1223, h=344, scale=0.601, rot=3 deg TopLeft) ----------
        {
            cairo_save(cr);
            cairo_translate(cr, 240.0, 240.0);
            cairo_rotate(cr, 3.0 * std::numbers::pi / 180.0);
            cairo_scale(cr, 0.601, 0.601);

            if (banner_img_) {
                int bw = cairo_image_surface_get_width(banner_img_.get());
                int bh = cairo_image_surface_get_height(banner_img_.get());
                if (bw > 0 && bh > 0) {
                    cairo_save(cr);
                    cairo_scale(cr, 1223.0 / static_cast<double>(bw), 344.0 / static_cast<double>(bh));
                    cairo_set_source_surface(cr, banner_img_.get(), 0.0, 0.0);
                    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
                    cairo_paint(cr);
                    cairo_restore(cr);
                }
            }

            // Character name tag at (268, 78) rot -15 deg around center
            {
                JitterSpec name_spec;
                name_spec.text = character_name_;
                name_spec.family = fonts_.music_ux_regular;
                name_spec.pixel_size = 40.0;
                name_spec.bold = true;
                name_spec.color = {0.0, 0.0, 0.0, 1.0};
                name_spec.jit = 0.9;
                name_spec.seed = 3.0;
                name_spec.inv = character_inv_;
                name_spec.box_color = {0.0, 0.0, 0.0, 1.0};
                name_spec.box_text_color = {1.0, 1.0, 1.0, 1.0};

                JitterMetrics fm = measure_jitter_text(cr, name_spec);
                cairo_save(cr);
                cairo_translate(cr, 268.0, 78.0);
                cairo_rotate(cr, -15.0 * std::numbers::pi / 180.0);
                cairo_translate(cr, -fm.width * 0.5, -fm.height * 0.5);
                draw_jitter_text(cr, name_spec, &fm);
                cairo_restore(cr);
            }

            // "Now playing ..." & "by ..." textGroup (x=text_group_x, y=140, rot=2.5 TopLeft)
            {
                cairo_save(cr);
                cairo_translate(cr, text_group_x, 140.0);
                cairo_rotate(cr, 2.5 * std::numbers::pi / 180.0);
                cairo_push_group(cr);

                JitterSpec line1;
                line1.text = st.has_player
                    ? ("Now playing " + (st.track_title.empty() ? "Unknown" : st.track_title))
                    : "Nothing playing";
                line1.family = fonts_.music_ux_regular;
                line1.pixel_size = 32.0;
                line1.bold = true;
                line1.color = {1.0, 1.0, 1.0, 1.0};
                line1.jit = 0.55;
                line1.seed = 7.0;
                line1.max_chars = 34;

                JitterMetrics m1 = measure_jitter_text(cr, line1);
                cairo_save(cr);
                cairo_translate(cr, 385.0 - m1.width * 0.5, 0.0);
                draw_jitter_text(cr, line1, &m1);
                cairo_restore(cr);

                JitterSpec line2;
                line2.text = st.has_player
                    ? ("by " + (st.track_artist.empty() ? "Unknown artist" : st.track_artist))
                    : "Play any media";
                line2.family = fonts_.music_ux_regular;
                line2.pixel_size = 32.0;
                line2.bold = true;
                line2.color = {1.0, 1.0, 1.0, 1.0};
                line2.jit = 0.55;
                line2.seed = 11.0;
                line2.max_chars = 34;

                JitterMetrics m2 = measure_jitter_text(cr, line2);
                cairo_save(cr);
                cairo_translate(cr, 385.0 - m2.width * 0.5, 40.0);
                draw_jitter_text(cr, line2, &m2);
                cairo_restore(cr);

                cairo_pop_group_to_source(cr);
                cairo_paint_with_alpha(cr, track_opacity);
                cairo_restore(cr);
            }

            // Seek bar (x=300, y=240, w=640, h=12, rot=5.2 TopLeft)
            {
                cairo_save(cr);
                cairo_translate(cr, 300.0, 240.0);
                cairo_rotate(cr, 5.2 * std::numbers::pi / 180.0);

                const Color track_c = Color::from_hex("#1c1f24");
                cairo_set_source_rgba(cr, track_c.r, track_c.g, track_c.b, 1.0);
                cairo_rectangle(cr, 0.0, 0.0, 640.0, 12.0);
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
                cairo_set_line_width(cr, 2.0);
                cairo_stroke(cr);

                const double frac = (st.length_sec > 0.0)
                    ? std::clamp(st.position_sec / st.length_sec, 0.0, 1.0)
                    : 0.0;
                if (frac > 0.0) {
                    const Color fill_c = Color::from_hex("#e60012");
                    cairo_set_source_rgba(cr, fill_c.r, fill_c.g, fill_c.b, 1.0);
                    cairo_rectangle(cr, 2.0, 2.0, (640.0 - 4.0) * frac, 8.0);
                    cairo_fill(cr);
                }

                if (hovered_zone_ == HitZone::SeekBar && st.length_sec > 0.0) {
                    const double thumb_x = std::clamp(640.0 * frac - 6.0, 0.0, 640.0 - 12.0);
                    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
                    cairo_rectangle(cr, thumb_x, -8.0, 12.0, 28.0);
                    cairo_fill(cr);
                }
                cairo_restore(cr);
            }

            // Time text at (1124, 188) rot -24 deg around center
            {
                const double display_sec = show_remaining_
                    ? std::max(0.0, st.length_sec - st.position_sec)
                    : st.position_sec;
                JitterSpec time_spec;
                time_spec.text = (show_remaining_ ? "-" : "") + format_mm_ss(display_sec);
                time_spec.family = fonts_.music_ux_regular;
                time_spec.pixel_size = 30.0;
                time_spec.bold = true;
                time_spec.color = {0.0, 0.0, 0.0, 1.0};
                time_spec.jit = 0.8;
                time_spec.seed = 5.0;

                JitterMetrics tm = measure_jitter_text(cr, time_spec);
                const double t_scale = (hovered_zone_ == HitZone::TimeToggle) ? 1.1 : 1.0;

                cairo_save(cr);
                cairo_translate(cr, 1124.0, 188.0);
                cairo_rotate(cr, -24.0 * std::numbers::pi / 180.0);
                cairo_scale(cr, t_scale, t_scale);
                cairo_translate(cr, -tm.width * 0.5, -tm.height * 0.5);
                draw_jitter_text(cr, time_spec, &tm);
                cairo_restore(cr);
            }

            cairo_restore(cr);
        }

        // ---------- 3. Futaba Frame (x=33, y=95, w=352, h=272) ----------
        if (futaba_img_) {
            const double f_scale = (hovered_zone_ == HitZone::FutabaFrame) ? 1.03 : 1.0;
            int fw = cairo_image_surface_get_width(futaba_img_.get());
            int fh = cairo_image_surface_get_height(futaba_img_.get());
            if (fw > 0 && fh > 0) {
                cairo_save(cr);
                cairo_translate(cr, 33.0 + 176.0, 95.0 + 136.0);
                cairo_scale(cr, f_scale, f_scale);
                cairo_translate(cr, -176.0, -136.0);
                cairo_scale(cr, 352.0 / static_cast<double>(fw), 272.0 / static_cast<double>(fh));
                cairo_set_source_surface(cr, futaba_img_.get(), 0.0, 0.0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
                cairo_paint(cr);
                cairo_restore(cr);
            }
        }

        // ---------- 4. Controls ----------
        draw_button(
            cr,
            kBtnPrevX,
            kBtnPrevY,
            -4.0,
            "PREV. SONG",
            {2, 8},
            1.0,
            hovered_zone_ == HitZone::BtnPrev,
            hovered_zone_ == HitZone::BtnPrev && pointer_pressed_,
            btn_prev_w_,
            btn_prev_h_
        );

        draw_button(
            cr,
            kBtnPlayX,
            kBtnPlayY,
            -6.0,
            st.is_playing ? "PAUSE" : "PLAY",
            {1},
            2.0,
            hovered_zone_ == HitZone::BtnPlay,
            hovered_zone_ == HitZone::BtnPlay && pointer_pressed_,
            btn_play_w_,
            btn_play_h_
        );

        draw_button(
            cr,
            kBtnNextX,
            kBtnNextY,
            2.0,
            "NEXT SONG",
            {2, 6},
            4.0,
            hovered_zone_ == HitZone::BtnNext,
            hovered_zone_ == HitZone::BtnNext && pointer_pressed_,
            btn_next_w_,
            btn_next_h_
        );

        // ---------- 5. Shuffle ----------
        draw_shuffle(
            cr,
            hovered_zone_ == HitZone::BtnShuffle,
            hovered_zone_ == HitZone::BtnShuffle && pointer_pressed_
        );

        cairo_restore(cr);
    }

    cairo_destroy(cr);
    cairo_surface_flush(slot->cairo_surface);

    slot->busy = true;
    wl_surface_attach(surface_, slot->buffer, 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, width_, height_);
    wl_surface_commit(surface_);
}

} // namespace p5

