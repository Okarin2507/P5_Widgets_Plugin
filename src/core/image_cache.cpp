#include "core/image_cache.hpp"

#include "core/common.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace p5 {

namespace {

using ImageCache = std::unordered_map<std::string, std::weak_ptr<cairo_surface_t>>;

ImageCache& raw_cache() {
    static ImageCache cache;
    return cache;
}

ImageCache& scaled_cache() {
    static ImageCache cache;
    return cache;
}

std::shared_ptr<cairo_surface_t> make_shared_surface(cairo_surface_t* surface) {
    if (!surface) return {};
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return {};
    }
    return std::shared_ptr<cairo_surface_t>(surface, [](cairo_surface_t* ptr) {
        if (ptr) cairo_surface_destroy(ptr);
    });
}

SharedImage scale_surface(cairo_surface_t* source, int width, int height) {
    if (!source || width <= 0 || height <= 0) return {};

    cairo_surface_t* dest = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(dest) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(dest);
        return {};
    }

    cairo_t* cr = cairo_create(dest);
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);

    const int sw = cairo_image_surface_get_width(source);
    const int sh = cairo_image_surface_get_height(source);
    if (sw > 0 && sh > 0) {
        cairo_scale(cr, static_cast<double>(width) / sw,
                    static_cast<double>(height) / sh);
        cairo_set_source_surface(cr, source, 0.0, 0.0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint(cr);
    }
    cairo_destroy(cr);
    cairo_surface_flush(dest);

    return make_shared_surface(dest);
}

} // namespace

SharedImage adopt_image(cairo_surface_t* surface) {
    return make_shared_surface(surface);
}

SharedImage load_shared_image(const std::filesystem::path& path) {
    if (path.empty()) return {};

    const std::string key = path.string();
    auto& cache = raw_cache();
    auto found = cache.find(key);
    if (found != cache.end()) {
        if (auto existing = found->second.lock()) return existing;
    }

    auto surface = make_shared_surface(load_image_surface(path));
    if (surface) cache[key] = surface;
    return surface;
}

SharedImage load_shared_image_scaled(const std::filesystem::path& path,
                                     int width, int height) {
    if (path.empty() || width <= 0 || height <= 0) return {};

    const std::string key = path.string() + "|" + std::to_string(width) + "x" +
                            std::to_string(height);
    auto& cache = scaled_cache();
    auto found = cache.find(key);
    if (found != cache.end()) {
        if (auto existing = found->second.lock()) return existing;
    }

    auto source = load_shared_image(path);
    if (!source) return {};

    auto scaled = scale_surface(source.get(), width, height);
    if (scaled) cache[key] = scaled;
    return scaled;
}

SharedImage load_shared_image_fit(const std::filesystem::path& path, int width) {
    auto source = load_shared_image(path);
    if (!source || width <= 0) return {};

    const int sw = cairo_image_surface_get_width(source.get());
    const int sh = cairo_image_surface_get_height(source.get());
    if (sw <= 0 || sh <= 0) return {};

    const int height = std::max(1, static_cast<int>(std::lround(
                                      static_cast<double>(width) * sh / sw)));
    return load_shared_image_scaled(path, width, height);
}

} // namespace p5
