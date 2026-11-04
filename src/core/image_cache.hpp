#pragma once

#include <cairo/cairo.h>

#include <filesystem>
#include <memory>

namespace p5 {

// Cairo surfaces are reference-counted through shared_ptr so multiple outputs
// can draw the same decoded (and optionally pre-scaled) image. The cache holds
// weak references only: an asset stays in RAM exactly as long as a visible
// surface still needs it.
using SharedImage = std::shared_ptr<cairo_surface_t>;

SharedImage adopt_image(cairo_surface_t* surface);

SharedImage load_shared_image(const std::filesystem::path& path);

// Scales to an exact pixel size. Intended for assets whose on-screen size is
// fixed (calendar layers, notification banner, character frame).
SharedImage load_shared_image_scaled(const std::filesystem::path& path,
                                     int width, int height);

// Scales proportionally to the requested width.
SharedImage load_shared_image_fit(const std::filesystem::path& path, int width);

} // namespace p5
