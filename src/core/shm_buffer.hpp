#pragma once

#include <array>
#include <cairo/cairo.h>
#include <cstdint>
#include <wayland-client.h>

namespace p5 {

struct ShmSlot {
    wl_buffer* buffer = nullptr;
    cairo_surface_t* cairo_surface = nullptr;
    void* data = nullptr;
    size_t size = 0;
    int width = 0;
    int height = 0;
    bool busy = false;

    void destroy();
};

class ShmPool {
public:
    ShmPool() = default;
    ~ShmPool();

    ShmPool(const ShmPool&) = delete;
    ShmPool& operator=(const ShmPool&) = delete;

    ShmSlot* acquire(wl_shm* shm, int width, int height);
    void release_all();

private:
    std::array<ShmSlot, 2> slots_{};
    bool init_slot(wl_shm* shm, ShmSlot& slot, int width, int height);
};

} // namespace p5

