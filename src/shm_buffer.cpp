#include "shm_buffer.hpp"

#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace p5 {

namespace {
void buffer_release(void* data, wl_buffer* /*wl_buffer*/) {
    auto* slot = static_cast<ShmSlot*>(data);
    if (slot) {
        slot->busy = false;
    }
}

constexpr wl_buffer_listener kBufferListener = {
    .release = buffer_release,
};
} // namespace

void ShmSlot::destroy() {
    if (cairo_surface) {
        cairo_surface_destroy(cairo_surface);
        cairo_surface = nullptr;
    }
    if (buffer) {
        wl_buffer_destroy(buffer);
        buffer = nullptr;
    }
    if (data && size > 0) {
        munmap(data, size);
        data = nullptr;
        size = 0;
    }
    width = 0;
    height = 0;
    busy = false;
}

ShmPool::~ShmPool() {
    release_all();
}

void ShmPool::release_all() {
    for (auto& slot : slots_) {
        slot.destroy();
    }
}

bool ShmPool::init_slot(wl_shm* shm, ShmSlot& slot, int width, int height) {
    slot.destroy();
    if (!shm || width <= 0 || height <= 0) {
        return false;
    }

    const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    const size_t total_size = static_cast<size_t>(stride) * static_cast<size_t>(height);

    int fd = memfd_create("p5-rice-shm", MFD_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    if (ftruncate(fd, static_cast<off_t>(total_size)) < 0) {
        close(fd);
        return false;
    }

    void* map = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return false;
    }

    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, static_cast<int32_t>(total_size));
    close(fd);
    if (!pool) {
        munmap(map, total_size);
        return false;
    }

    wl_buffer* buf = wl_shm_pool_create_buffer(
        pool,
        0,
        width,
        height,
        stride,
        WL_SHM_FORMAT_ARGB8888
    );
    wl_shm_pool_destroy(pool);
    if (!buf) {
        munmap(map, total_size);
        return false;
    }

    cairo_surface_t* surf = cairo_image_surface_create_for_data(
        static_cast<unsigned char*>(map),
        CAIRO_FORMAT_ARGB32,
        width,
        height,
        stride
    );
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        wl_buffer_destroy(buf);
        munmap(map, total_size);
        return false;
    }

    slot.buffer = buf;
    slot.cairo_surface = surf;
    slot.data = map;
    slot.size = total_size;
    slot.width = width;
    slot.height = height;
    slot.busy = false;

    wl_buffer_add_listener(slot.buffer, &kBufferListener, &slot);
    return true;
}

ShmSlot* ShmPool::acquire(wl_shm* shm, int width, int height) {
    for (auto& slot : slots_) {
        if (!slot.busy) {
            if (slot.width != width || slot.height != height || !slot.buffer) {
                if (!init_slot(shm, slot, width, height)) {
                    return nullptr;
                }
            }
            return &slot;
        }
    }
    // If both buffers are still marked busy by the compositor, re-initialize slot 0
    if (!init_slot(shm, slots_[0], width, height)) {
        return nullptr;
    }
    return &slots_[0];
}

} // namespace p5

