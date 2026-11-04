#pragma once

#include "core/common.hpp"
#include <cairo/cairo.h>
#include <string>
#include <vector>

namespace p5 {

struct JitterSpec {
    std::string text;
    std::string family = "Sans";
    double pixel_size = 30.0;
    bool bold = false;
    Color color = {1.0, 1.0, 1.0, 1.0};
    Color edge_color = {0.0, 0.0, 0.0, 1.0};
    double edge = 0.0;
    int rings = 14;
    double jit = 1.0;
    double spacing = 0.0;
    double seed = 0.0;
    double pad = 0.0;
    int max_chars = 0;
    std::vector<int> inv;
    Color box_color = {0.0, 0.0, 0.0, 1.0};
    Color box_text_color = {1.0, 1.0, 1.0, 1.0};
    Color box_border = {1.0, 1.0, 1.0, 1.0};
};

struct JitterMetrics {
    double width = 0.0;
    double height = 0.0;
    std::vector<std::string> chars;
    std::vector<double> char_widths;
    std::vector<double> char_heights;
};

JitterMetrics measure_jitter_text(cairo_t* cr, const JitterSpec& spec);
void draw_jitter_text(cairo_t* cr, const JitterSpec& spec, const JitterMetrics* precomputed = nullptr);

} // namespace p5

