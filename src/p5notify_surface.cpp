#include "p5notify_surface.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <linux/input-event-codes.h>
#include <numbers>
#include <pango/pangocairo.h>
#include <random>

namespace p5 {

namespace {

void layer_surface_configure(
    void* data,
    zwlr_layer_surface_v1* /*surface*/,
    uint32_t serial,
    uint32_t width,
    uint32_t height
) {
    auto* self = static_cast<P5NotifySurface*>(data);
    if (self) {
        self->handle_configure(serial, width, height);
    }
}

void layer_surface_closed(void* data, zwlr_layer_surface_v1* /*surface*/) {
    auto* self = static_cast<P5NotifySurface*>(data);
    if (self) {
        self->handle_closed();
    }
}

constexpr zwlr_layer_surface_v1_listener kLayerSurfaceListener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

int pick_box_index(std::string_view tag) {
    std::vector<int> candidates;
    for (size_t i = 0; i < tag.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(tag[i]);
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            candidates.push_back(static_cast<int>(i));
        }
    }
    if (candidates.empty()) return 0;
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    return candidates[dist(rng)];
}

void compute_card_transform(int64_t age_ms, double& out_tx, double& out_opacity, double& out_angle_deg) {
    // 0..300ms: x slides from 120 -> 0 with OutBack
    if (age_ms <= 0) {
        out_tx = 120.0;
        out_opacity = 0.0;
        out_angle_deg = 0.0;
        return;
    }
    if (age_ms < 300) {
        double t = static_cast<double>(age_ms) / 300.0;
        out_tx = 120.0 * (1.0 - ease_out_back(t));
    } else {
        out_tx = 0.0;
    }

    if (age_ms < 180) {
        out_opacity = std::clamp(static_cast<double>(age_ms) / 180.0, 0.0, 1.0);
    } else {
        out_opacity = 1.0;
    }

    // Sequential wobble after 300ms:
    // [300..370]: 0 -> -3.2 (70ms)
    // [370..450]: -3.2 -> 2.6 (80ms)
    // [450..530]: 2.6 -> -1.8 (80ms)
    // [530..610]: -1.8 -> 1.1 (80ms)
    // [610..690]: 1.1 -> -0.5 (80ms)
    // [690..770]: -0.5 -> 0.0 (80ms)
    struct Keyframe {
        int64_t t0;
        int64_t t1;
        double a0;
        double a1;
    };
    static constexpr Keyframe kWobble[] = {
        {300, 370,  0.0, -3.2},
        {370, 450, -3.2,  2.6},
        {450, 530,  2.6, -1.8},
        {530, 610, -1.8,  1.1},
        {610, 690,  1.1, -0.5},
        {690, 770, -0.5,  0.0},
    };
    out_angle_deg = 0.0;
    for (const auto& kf : kWobble) {
        if (age_ms >= kf.t0 && age_ms < kf.t1) {
            double p = static_cast<double>(age_ms - kf.t0) / static_cast<double>(kf.t1 - kf.t0);
            out_angle_deg = kf.a0 + (kf.a1 - kf.a0) * ease_in_out_sine(p);
            break;
        }
    }
}

} // namespace

P5NotifySurface::P5NotifySurface(
    wl_compositor* compositor,
    zwlr_layer_shell_v1* layer_shell,
    wl_shm* shm,
    const LoadedFonts& fonts
)
    : compositor_(compositor),
      layer_shell_(layer_shell),
      shm_(shm),
      fonts_(fonts) {
    auto b2 = resolve_asset_path("p5notify", "banner2.png");
    banner_img_ = load_image_surface(b2);
    if (!banner_img_) {
        auto b1 = resolve_asset_path("p5notify", "banner.png");
        banner_img_ = load_image_surface(b1);
    }
    if (!banner_img_) {
        banner_img_ = create_fallback_notify_banner(700, 190);
    }
    if (banner_img_) {
        int iw = cairo_image_surface_get_width(banner_img_);
        int ih = cairo_image_surface_get_height(banner_img_);
        if (iw > 0 && ih > 0) {
            banner_h_ = kBannerW * static_cast<double>(ih) / static_cast<double>(iw);
        }
    }
    sound_path_ = resolve_asset_path("p5notify", "sounds/papersound.wav");

    width_ = static_cast<int>(kBannerW + 40);
    height_ = static_cast<int>(banner_h_ * static_cast<double>(kMaxBanners) + 60);
}

P5NotifySurface::~P5NotifySurface() {
    destroy();
    if (banner_img_) {
        cairo_surface_destroy(banner_img_);
        banner_img_ = nullptr;
    }
}

bool P5NotifySurface::init() {
    if (!compositor_ || !layer_shell_ || !shm_) return false;

    surface_ = wl_compositor_create_surface(compositor_);
    if (!surface_) return false;

    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell_,
        surface_,
        nullptr,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        "noctalia-p5notify"
    );
    if (!layer_surface_) {
        wl_surface_destroy(surface_);
        surface_ = nullptr;
        return false;
    }

    zwlr_layer_surface_v1_add_listener(layer_surface_, &kLayerSurfaceListener, this);
    zwlr_layer_surface_v1_set_anchor(
        layer_surface_,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
    );
    zwlr_layer_surface_v1_set_margin(layer_surface_, 14, 14, 0, 0);
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

void P5NotifySurface::destroy() {
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

void P5NotifySurface::set_persona_active(bool active) {
    if (persona_active_ != active) {
        persona_active_ = active;
        if (!active) {
            clear_all();
        }
    }
}

void P5NotifySurface::push_notification(const NotificationEvent& ev) {
    if (!persona_active_) return;

    std::string summary = strip_markup(ev.summary);
    std::string app_name = strip_markup(ev.app_name);
    std::string tag = !summary.empty() ? summary : (!app_name.empty() ? app_name : "Notification");
    std::string body = strip_markup(ev.body);

    int32_t timeout_ms = kShowSeconds * 1000;
    if (ev.expire_timeout > 0) {
        timeout_ms = (ev.expire_timeout < 1000) ? ev.expire_timeout * 1000 : ev.expire_timeout;
        timeout_ms = std::clamp(timeout_ms, 3000, 15000);
    }

    const bool critical = (ev.urgency == 2);

    while (banners_.size() >= kMaxBanners) {
        banners_.pop_back();
    }

    const int64_t now = now_ms();
    BannerItem item;
    item.uid = next_uid_++;
    item.tag = std::move(tag);
    item.body = std::move(body);
    item.box_index = pick_box_index(item.tag);
    item.timeout_ms = timeout_ms;
    item.elapsed_hover_paused_ms = 0;
    item.created_ms = now;
    item.last_tick_ms = now;
    item.critical = critical;

    banners_.push_front(std::move(item));
    play_sound_async(sound_path_);

    if (critical) {
        dismiss_stock_pass_ = 1;
        next_dismiss_stock_ms_ = now + 30;
    }

    update_input_region();
    if (configured_) {
        render(now);
    }
}

void P5NotifySurface::clear_all() {
    if (banners_.empty()) return;
    banners_.clear();
    update_input_region();
    if (configured_) {
        render(now_ms());
    }
}

bool P5NotifySurface::is_animating(int64_t now) const {
    for (const auto& b : banners_) {
        if (now - b.created_ms <= 800) {
            return true;
        }
    }
    return false;
}

bool P5NotifySurface::tick(int64_t now) {
    bool needs_redraw = false;

    if (dismiss_stock_pass_ > 0 && now >= next_dismiss_stock_ms_) {
        spawn_detached({"noctalia", "msg", "notification-clear-active"});
        if (dismiss_stock_pass_ == 1) {
            dismiss_stock_pass_ = 2;
            next_dismiss_stock_ms_ = now + 250;
        } else {
            dismiss_stock_pass_ = 0;
        }
    }

    const int hovered_idx = pointer_inside_ ? hit_test_banner(pointer_x_, pointer_y_) : -1;

    for (size_t i = 0; i < banners_.size();) {
        auto& b = banners_[i];
        const int64_t dt = std::max<int64_t>(0, now - b.last_tick_ms);
        b.last_tick_ms = now;

        if (now - b.created_ms <= 800) {
            needs_redraw = true;
        }

        const bool is_hovered = (static_cast<int>(i) == hovered_idx);
        if (!b.critical && !is_hovered) {
            b.elapsed_hover_paused_ms += static_cast<int32_t>(dt);
            if (b.elapsed_hover_paused_ms >= b.timeout_ms) {
                banners_.erase(banners_.begin() + static_cast<std::ptrdiff_t>(i));
                needs_redraw = true;
                update_input_region();
                continue;
            }
        }
        ++i;
    }

    if (needs_redraw && configured_) {
        render(now);
    }
    return !banners_.empty() || dismiss_stock_pass_ > 0;
}

int P5NotifySurface::hit_test_banner(double x, double y) const {
    if (banners_.empty()) return -1;
    const double col_x = static_cast<double>(width_) - kColMargin - kBannerW;
    if (x < col_x || x > col_x + kBannerW) return -1;

    double cur_y = kColMargin;
    for (size_t i = 0; i < banners_.size(); ++i) {
        if (y >= cur_y && y <= cur_y + banner_h_) {
            return static_cast<int>(i);
        }
        cur_y += banner_h_ + kSpacing;
    }
    return -1;
}

void P5NotifySurface::on_pointer_motion(double x, double y) {
    pointer_inside_ = true;
    pointer_x_ = x;
    pointer_y_ = y;
}

void P5NotifySurface::on_pointer_leave() {
    pointer_inside_ = false;
    pointer_x_ = -1.0;
    pointer_y_ = -1.0;
}

void P5NotifySurface::on_pointer_button(uint32_t button, uint32_t state) {
    if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED && pointer_inside_) {
        int idx = hit_test_banner(pointer_x_, pointer_y_);
        if (idx >= 0 && static_cast<size_t>(idx) < banners_.size()) {
            banners_.erase(banners_.begin() + idx);
            update_input_region();
            if (configured_) {
                render(now_ms());
            }
        }
    }
}

void P5NotifySurface::handle_configure(uint32_t serial, uint32_t width, uint32_t height) {
    zwlr_layer_surface_v1_ack_configure(layer_surface_, serial);
    if (width > 0) width_ = static_cast<int>(width);
    if (height > 0) height_ = static_cast<int>(height);
    configured_ = true;
    update_input_region();
    render(now_ms());
}

void P5NotifySurface::handle_closed() {
    configured_ = false;
}

void P5NotifySurface::update_input_region() {
    if (!compositor_ || !surface_) return;
    wl_region* reg = wl_compositor_create_region(compositor_);
    if (!banners_.empty()) {
        const int col_x = static_cast<int>(width_ - kColMargin - kBannerW);
        const int col_y = static_cast<int>(kColMargin);
        const int col_w = static_cast<int>(kBannerW);
        const int col_h = static_cast<int>(
            banners_.size() * banner_h_ + (banners_.size() - 1) * kSpacing + 10.0
        );
        wl_region_add(reg, col_x, col_y, col_w, std::max(1, col_h));
    }
    wl_surface_set_input_region(surface_, reg);
    wl_region_destroy(reg);
}

void P5NotifySurface::draw_banner_card(
    cairo_t* cr,
    const BannerItem& card,
    double card_x,
    double card_y,
    int64_t now
) {
    double tx = 0.0;
    double opacity = 1.0;
    double angle_deg = 0.0;
    compute_card_transform(now - card.created_ms, tx, opacity, angle_deg);

    cairo_save(cr);
    // Translate to card position + slide-in tx
    cairo_translate(cr, card_x + tx, card_y);

    // Wobble rotation around origin (card.width * 0.7, card.height * 0.5)
    const double ox = kBannerW * 0.7;
    const double oy = banner_h_ * 0.5;
    cairo_translate(cr, ox, oy);
    cairo_rotate(cr, angle_deg * std::numbers::pi / 180.0);
    cairo_translate(cr, -ox, -oy);

    cairo_push_group(cr);

    // 1. Draw banner2.png
    if (banner_img_) {
        cairo_save(cr);
        int iw = cairo_image_surface_get_width(banner_img_);
        int ih = cairo_image_surface_get_height(banner_img_);
        if (iw > 0 && ih > 0) {
            cairo_scale(cr, kBannerW / static_cast<double>(iw), banner_h_ / static_cast<double>(ih));
            cairo_set_source_surface(cr, banner_img_, 0.0, 0.0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_paint(cr);
        }
        cairo_restore(cr);
    }

    // 2. White upper strip: title with one reversed boxed letter, centered
    {
        const double tag_box_w = kBannerW * kTagW;
        const double tag_box_h = 26.0;
        const double tag_cx = kBannerW * kTagCX;
        const double tag_cy = banner_h_ * kTagCY;

        cairo_save(cr);
        cairo_translate(cr, tag_cx, tag_cy);
        cairo_rotate(cr, kTagRot * std::numbers::pi / 180.0);
        cairo_translate(cr, -tag_box_w * 0.5, -tag_box_h * 0.5);
        cairo_rectangle(cr, 0.0, 0.0, tag_box_w, tag_box_h);
        cairo_clip(cr);

        const std::string& title_font = fonts_.notify_p5_ready ? fonts_.notify_p5_main : fonts_.notify_ux_regular;
        const double title_size = fonts_.notify_p5_ready ? 12.0 : 15.0;
        const bool title_bold = !fonts_.notify_p5_ready;

        const size_t b_idx = std::min(static_cast<size_t>( std::max(0, card.box_index)), card.tag.empty() ? 0 : card.tag.size() - 1);
        const std::string before_str = card.tag.empty() ? "" : card.tag.substr(0, b_idx);
        const std::string boxed_str = card.tag.empty() ? "N" : card.tag.substr(b_idx, 1);
        const std::string after_str = (card.tag.size() > b_idx + 1) ? card.tag.substr(b_idx + 1) : "";

        PangoFontDescription* desc_norm = pango_font_description_new();
        pango_font_description_set_family(desc_norm, title_font.c_str());
        pango_font_description_set_absolute_size(desc_norm, title_size * PANGO_SCALE);
        pango_font_description_set_weight(desc_norm, title_bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);

        PangoFontDescription* desc_box = pango_font_description_new();
        pango_font_description_set_family(desc_box, title_font.c_str());
        pango_font_description_set_absolute_size(desc_box, (title_size + 1.0) * PANGO_SCALE);
        pango_font_description_set_weight(desc_box, title_bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);

        PangoLayout* lay_before = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(lay_before, desc_norm);
        pango_layout_set_text(lay_before, before_str.c_str(), -1);
        int bw = 0, bh = 0;
        pango_layout_get_pixel_size(lay_before, &bw, &bh);
        double w_before = std::min(static_cast<double>(bw), tag_box_w * 0.6);
        pango_layout_set_width(lay_before, static_cast<int>(w_before * PANGO_SCALE));
        pango_layout_set_ellipsize(lay_before, PANGO_ELLIPSIZE_END);

        PangoLayout* lay_boxed = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(lay_boxed, desc_box);
        pango_layout_set_text(lay_boxed, boxed_str.c_str(), -1);
        int xw = 0, xh = 0;
        pango_layout_get_pixel_size(lay_boxed, &xw, &xh);
        double w_box = static_cast<double>(xw) + 6.0;
        double h_box = 21.0;

        PangoLayout* lay_after = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(lay_after, desc_norm);
        pango_layout_set_text(lay_after, after_str.c_str(), -1);
        int aw = 0, ah = 0;
        pango_layout_get_pixel_size(lay_after, &aw, &ah);
        double w_after = std::min(static_cast<double>(aw), std::max(0.0, tag_box_w - w_before - w_box));
        pango_layout_set_width(lay_after, static_cast<int>(w_after * PANGO_SCALE));
        pango_layout_set_ellipsize(lay_after, PANGO_ELLIPSIZE_END);

        const double total_row_w = w_before + w_box + w_after;
        double start_x = (tag_box_w - total_row_w) * 0.5;

        // Draw before text
        if (!before_str.empty() && w_before > 0.0) {
            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
            cairo_move_to(cr, start_x, (tag_box_h - bh) * 0.5);
            pango_cairo_show_layout(cr, lay_before);
        }
        start_x += w_before;

        // Draw boxed letter
        const double box_y = (tag_box_h - h_box) * 0.5;
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
        cairo_rectangle(cr, start_x, box_y, w_box, h_box);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
        cairo_move_to(cr, start_x + (w_box - xw) * 0.5, box_y + (h_box - xh) * 0.5);
        pango_cairo_show_layout(cr, lay_boxed);
        start_x += w_box;

        // Draw after text
        if (!after_str.empty() && w_after > 0.0) {
            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
            cairo_move_to(cr, start_x, (tag_box_h - ah) * 0.5);
            pango_cairo_show_layout(cr, lay_after);
        }

        g_object_unref(lay_after);
        g_object_unref(lay_boxed);
        g_object_unref(lay_before);
        pango_font_description_free(desc_box);
        pango_font_description_free(desc_norm);
        cairo_restore(cr);
    }

    // 3. Black lower panel: body in white, centered both ways
    if (!card.body.empty()) {
        const double body_w = kBannerW * kBodyW;
        const double body_h = banner_h_ * kBodyH;
        const double body_cx = kBannerW * kBodyCX;
        const double body_cy = banner_h_ * kBodyCY;

        cairo_save(cr);
        cairo_translate(cr, body_cx, body_cy);
        cairo_rotate(cr, kBodyRot * std::numbers::pi / 180.0);
        cairo_translate(cr, -body_w * 0.5, -body_h * 0.5);
        cairo_rectangle(cr, 0.0, 0.0, body_w, body_h);
        cairo_clip(cr);

        PangoFontDescription* desc_body = pango_font_description_new();
        pango_font_description_set_family(desc_body, fonts_.notify_ux_regular.c_str());
        pango_font_description_set_absolute_size(desc_body, 19.0 * PANGO_SCALE);

        PangoLayout* lay_body = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(lay_body, desc_body);
        pango_layout_set_width(lay_body, static_cast<int>(body_w * PANGO_SCALE));
        pango_layout_set_wrap(lay_body, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_alignment(lay_body, PANGO_ALIGN_CENTER);
        pango_layout_set_ellipsize(lay_body, PANGO_ELLIPSIZE_END);
        pango_layout_set_height(lay_body, -3); // maximumLineCount: 3
        pango_layout_set_text(lay_body, card.body.c_str(), -1);

        int tw = 0, th = 0;
        pango_layout_get_pixel_size(lay_body, &tw, &th);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
        cairo_move_to(cr, 0.0, std::max(0.0, (body_h - th) * 0.5));
        pango_cairo_show_layout(cr, lay_body);

        g_object_unref(lay_body);
        pango_font_description_free(desc_body);
        cairo_restore(cr);
    }

    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, opacity);
    cairo_restore(cr);
}

void P5NotifySurface::render(int64_t now) {
    if (!configured_ || !surface_ || !shm_) return;

    ShmSlot* slot = pool_.acquire(shm_, width_, height_);
    if (!slot) return;

    cairo_surface_flush(slot->cairo_surface);
    cairo_t* cr = cairo_create(slot->cairo_surface);

    // Clear surface to transparent
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);

    if (persona_active_ && !banners_.empty()) {
        const double col_x = static_cast<double>(width_) - kColMargin - kBannerW;
        double cur_y = kColMargin;
        for (const auto& card : banners_) {
            draw_banner_card(cr, card, col_x, cur_y, now);
            cur_y += banner_h_ + kSpacing;
        }
    }

    cairo_destroy(cr);
    cairo_surface_flush(slot->cairo_surface);

    slot->busy = true;
    wl_surface_attach(surface_, slot->buffer, 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, width_, height_);
    wl_surface_commit(surface_);
}

} // namespace p5

