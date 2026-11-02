#include "jitter_text.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <pango/pangocairo.h>

namespace p5 {

namespace {

double jitter_rnd(int i, int k, double seed) {
    const double x = std::sin((i + 1) * 12.9898 + k * 78.233 + seed) * 43758.5453;
    return x - std::floor(x);
}

PangoFontDescription* make_font_desc(const std::string& family, double pixel_size, bool bold) {
    PangoFontDescription* desc = pango_font_description_new();
    const std::string fam_chain = (family.empty() ? "Sans" : family) + ", Noto Color Emoji, Sans";
    pango_font_description_set_family(desc, fam_chain.c_str());
    pango_font_description_set_absolute_size(desc, pixel_size * PANGO_SCALE);
    pango_font_description_set_weight(desc, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    return desc;
}

std::vector<std::string> compute_shown_chars(const JitterSpec& spec) {
    auto chars = utf8_split_chars(spec.text);
    if (spec.max_chars > 3 && static_cast<int>(chars.size()) > spec.max_chars) {
        chars.resize(static_cast<size_t>(spec.max_chars - 3));
        chars.emplace_back(".");
        chars.emplace_back(".");
        chars.emplace_back(".");
    }
    return chars;
}

} // namespace

JitterMetrics measure_jitter_text(cairo_t* cr, const JitterSpec& spec) {
    JitterMetrics m;
    m.chars = compute_shown_chars(spec);
    m.char_widths.reserve(m.chars.size());
    m.char_heights.reserve(m.chars.size());

    PangoFontDescription* desc = make_font_desc(spec.family, spec.pixel_size, spec.bold);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, desc);

    double row_w = 0.0;
    for (size_t i = 0; i < m.chars.size(); ++i) {
        pango_layout_set_text(layout, m.chars[i].c_str(), -1);
        int pw = 0, ph = 0;
        pango_layout_get_pixel_size(layout, &pw, &ph);
        // Ensure spaces have visible advance width
        if (m.chars[i] == " " && pw < spec.pixel_size * 0.28) {
            pw = static_cast<int>(std::ceil(spec.pixel_size * 0.30));
        }
        m.char_widths.push_back(static_cast<double>(pw));
        m.char_heights.push_back(static_cast<double>(ph));
        row_w += static_cast<double>(pw);
        if (i + 1 < m.chars.size()) {
            row_w += spec.spacing;
        }
    }

    g_object_unref(layout);
    pango_font_description_free(desc);

    m.width = row_w + spec.pad * 2.0;
    m.height = spec.pixel_size * 1.3 + spec.pad * 2.0;
    return m;
}

void draw_jitter_text(cairo_t* cr, const JitterSpec& spec, const JitterMetrics* precomputed) {
    JitterMetrics local_m;
    const JitterMetrics& m = precomputed ? *precomputed : (local_m = measure_jitter_text(cr, spec));

    PangoFontDescription* desc = make_font_desc(spec.family, spec.pixel_size, spec.bold);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, desc);

    double cur_x = spec.pad;
    for (size_t idx = 0; idx < m.chars.size(); ++idx) {
        const int i = static_cast<int>(idx);
        const double cw = m.char_widths[idx];
        const double ch = m.char_heights[idx];

        const bool boxed = std::find(spec.inv.begin(), spec.inv.end(), i) != spec.inv.end();
        const double rot_deg = (jitter_rnd(i, 1, spec.seed) - 0.5) * 14.0 * spec.jit;
        const double rot_rad = rot_deg * std::numbers::pi / 180.0;
        const double y_off = (jitter_rnd(i, 2, spec.seed) - 0.5) * spec.pixel_size * 0.22 * spec.jit;
        const double scl = 1.0 + (jitter_rnd(i, 3, spec.seed) - 0.5) * 0.22 * spec.jit;

        cairo_save(cr);
        // Transform around glyph center (cur_x + cw/2, spec.pad + y_off + ch/2)
        const double cx = cur_x + cw * 0.5;
        const double cy = spec.pad + y_off + ch * 0.5;
        cairo_translate(cr, cx, cy);
        cairo_rotate(cr, rot_rad);
        cairo_scale(cr, scl, scl);
        cairo_translate(cr, -cw * 0.5, -ch * 0.5);

        pango_layout_set_text(layout, m.chars[idx].c_str(), -1);

        if (boxed) {
            // Rectangle with margins -4 and border.width 2
            cairo_rectangle(cr, -4.0, -4.0, cw + 8.0, ch + 8.0);
            cairo_set_source_rgba(cr, spec.box_color.r, spec.box_color.g, spec.box_color.b, spec.box_color.a);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, spec.box_border.r, spec.box_border.g, spec.box_border.b, spec.box_border.a);
            cairo_set_line_width(cr, 2.0);
            cairo_stroke(cr);
        } else if (spec.edge > 0.0 && spec.rings > 0) {
            // Spiky sticker outline ring
            cairo_set_source_rgba(cr, spec.edge_color.r, spec.edge_color.g, spec.edge_color.b, spec.edge_color.a);
            for (int r = 0; r < spec.rings; ++r) {
                const double a = static_cast<double>(r) * 2.0 * std::numbers::pi / static_cast<double>(spec.rings);
                const double rr = spec.edge * ((r % 2 == 0) ? 1.0 : 0.45);
                const double dx = std::cos(a) * rr;
                const double dy = std::sin(a) * rr;
                cairo_move_to(cr, dx, dy);
                pango_cairo_show_layout(cr, layout);
            }
        }

        const Color& fg = boxed ? spec.box_text_color : spec.color;
        cairo_set_source_rgba(cr, fg.r, fg.g, fg.b, fg.a);
        cairo_move_to(cr, 0.0, 0.0);
        pango_cairo_show_layout(cr, layout);

        cairo_restore(cr);
        cur_x += cw + spec.spacing;
    }

    g_object_unref(layout);
    pango_font_description_free(desc);
}

} // namespace p5

