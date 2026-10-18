// C++ allocations in PSRAM (ESP32-P4).
//
// Smaller requests than CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (16 KB) would
// otherwise go to the ~450 KB of internal RAM first. With the app's Home
// Assistant states, strings and containers that left ~20 KB free, too
// little for another TLS connection or task. LVGL objects (C, lv_malloc)
// and buffers requested with explicit caps stay where they are; the app's
// C++ data takes the PSRAM (cached, 32 MB), and only falls back to internal
// RAM when PSRAM is full.

#include "esp_heap_caps.h"

#include <cstdlib>
#include <new>

extern "C" int homepane_psram_new = 0; // referenced by the linker (-u): keeps this file in

namespace {
void* allocate(std::size_t size)
{
    if (size == 0) size = 1;
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (!p) std::abort(); // as the default operator new without exceptions
    return p;
}

void* allocate_aligned(std::size_t size, std::align_val_t align)
{
    if (size == 0) size = 1;
    const auto a = static_cast<std::size_t>(align);
    void* p = heap_caps_aligned_alloc(a, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_aligned_alloc(a, size, MALLOC_CAP_8BIT);
    if (!p) std::abort();
    return p;
}
} // namespace

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    void* p = heap_caps_malloc(size ? size : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(size ? size : 1, MALLOC_CAP_8BIT);
}
void* operator new[](std::size_t size, const std::nothrow_t& t) noexcept { return operator new(size, t); }
void* operator new(std::size_t size, std::align_val_t align) { return allocate_aligned(size, align); }
void* operator new[](std::size_t size, std::align_val_t align) { return allocate_aligned(size, align); }

void operator delete(void* p) noexcept { heap_caps_free(p); }
void operator delete[](void* p) noexcept { heap_caps_free(p); }
void operator delete(void* p, std::size_t) noexcept { heap_caps_free(p); }
void operator delete[](void* p, std::size_t) noexcept { heap_caps_free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { heap_caps_free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { heap_caps_free(p); }
void operator delete(void* p, std::align_val_t) noexcept { heap_caps_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { heap_caps_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { heap_caps_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { heap_caps_free(p); }

// Developer check: internal RAM lost by creating and dropping shared_ptrs
// (the toolchain counts references under a mutex: does each leave one behind?).
#include <atomic>
#include <memory>
extern "C" int homepane_shared_ptr_leak_test(int n)
{
    const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    for (int i = 0; i < n; ++i) {
        auto p = std::make_shared<std::atomic<bool>>(false);
        auto q = p; // a copy: the count goes up and down
        q->store(true);
    }
    const size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    return static_cast<int>(before) - static_cast<int>(after);
}
