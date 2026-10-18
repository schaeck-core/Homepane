#include "ImageDecoder.h"

#include "../../../lvgl/src/libs/tjpgd/tjpgd.h"
#include "lvgl/lvgl.h" // LV_COLOR_DEPTH

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS 8192
// stb keeps "flip vertically on load" in thread-local variables; in raw
// FreeRTOS tasks (ImageLoader's background jobs) those are not reliably
// zeroed, and pictures came out upside down on the ESP32-P4.
#define STBI_NO_THREAD_LOCALS
#include "../../../third_party/stb/stb_image.h"

#include <algorithm>
#include <memory>

namespace app::media {

namespace {
constexpr size_t kWorkBytes = 4096; // what LVGL gives TJpgDec too

// RGB565 with ordered (4x4 Bayer) dithering: dark, soft gradients would
// otherwise show steps of 8 levels (the screensaver's pictures).
constexpr uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
inline void put(DecodedImage& img, int x, int y, int r, int g, int b)
{
    uint8_t* d = img.pixels.data() + (static_cast<size_t>(y) * img.w + x) * static_cast<size_t>(img.bytes_per_pixel());
    if (img.rgb888) { // as LVGL keeps it: B, G, R
        d[0] = static_cast<uint8_t>(b);
        d[1] = static_cast<uint8_t>(g);
        d[2] = static_cast<uint8_t>(r);
        return;
    }
    const int t = kBayer[y & 3][x & 3];
    r = std::min(255, r + t / 2); // 5 bits: steps of 8
    g = std::min(255, g + t / 4); // 6 bits: steps of 4
    b = std::min(255, b + t / 2);
    const auto v = static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    d[0] = static_cast<uint8_t>(v & 0xFF);
    d[1] = static_cast<uint8_t>(v >> 8);
}

struct Source {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    // target sampling: source coordinate of every target column / row
    std::vector<int> xs, ys;
    DecodedImage* out = nullptr;
};

size_t input(JDEC* jd, uint8_t* buff, size_t n)
{
    auto* s = static_cast<Source*>(jd->device);
    n = std::min(n, s->size - s->pos);
    if (buff) std::copy(s->data + s->pos, s->data + s->pos + n, buff);
    s->pos += n;
    return n;
}

int output(JDEC* jd, void* bitmap, JRECT* rect)
{
    auto* s = static_cast<Source*>(jd->device);
    const auto* rgb = static_cast<const uint8_t*>(bitmap); // 3 bytes per pixel; LVGL's copy writes B, G, R
    const int rw = rect->right - rect->left + 1;
    const auto x0 = std::lower_bound(s->xs.begin(), s->xs.end(), static_cast<int>(rect->left));
    const auto x1 = std::upper_bound(s->xs.begin(), s->xs.end(), static_cast<int>(rect->right));
    const auto y0 = std::lower_bound(s->ys.begin(), s->ys.end(), static_cast<int>(rect->top));
    const auto y1 = std::upper_bound(s->ys.begin(), s->ys.end(), static_cast<int>(rect->bottom));
    for (auto y = y0; y != y1; ++y) {
        const int dy = static_cast<int>(y - s->ys.begin());
        const uint8_t* line = rgb + static_cast<size_t>(*y - rect->top) * rw * 3;
        for (auto x = x0; x != x1; ++x) {
            const uint8_t* p = line + static_cast<size_t>(*x - rect->left) * 3;
            put(*s->out, static_cast<int>(x - s->xs.begin()), dy, p[2], p[1], p[0]);
        }
    }
    return 1;
}

// Source coordinate of each of n target pixels: start + (i + 0.5) * step.
std::vector<int> samples(int n, double start, double step, int limit)
{
    std::vector<int> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v[i] = std::min(limit - 1, static_cast<int>(start + (i + 0.5) * step));
    return v;
}
// The target size and the source pixel of every target column / row.
bool plan(int w, int h, int max_w, int max_h, bool cover, DecodedImage& out, std::vector<int>& xs,
          std::vector<int>& ys, std::string& error)
{
    // source pixels per target pixel; below 1 enlarges (nearest neighbour,
    // once per picture: a 600 px camera picture fills a 688 px frame)
    const double step = cover ? std::min(static_cast<double>(w) / max_w, static_cast<double>(h) / max_h)
                              : std::max(static_cast<double>(w) / max_w, static_cast<double>(h) / max_h);
    out.w = std::min(max_w, static_cast<int>(w / step));
    out.h = std::min(max_h, static_cast<int>(h / step));
    if (out.w <= 0 || out.h <= 0) {
        error = "empty picture";
        return false;
    }
    out.pixels.assign(static_cast<size_t>(out.w) * out.h * out.bytes_per_pixel(), 0);
    xs = samples(out.w, (w - out.w * step) / 2, step, w);
    ys = samples(out.h, (h - out.h * step) / 2, step, h);
    return true;
}

bool decode_with_stb(const uint8_t* data, size_t size, int max_w, int max_h, bool cover, DecodedImage& out,
                     std::string& error)
{
    int w = 0, h = 0, channels = 0;
    stbi_uc* rgb = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 3);
    if (!rgb) {
        error = "unsupported picture";
        return false;
    }
    std::vector<int> xs, ys;
    const bool ok = plan(w, h, max_w, max_h, cover, out, xs, ys, error);
    if (ok) {
        for (int dy = 0; dy < out.h; ++dy) {
            const stbi_uc* line = rgb + static_cast<size_t>(ys[dy]) * w * 3;
            for (int dx = 0; dx < out.w; ++dx) {
                const stbi_uc* p = line + static_cast<size_t>(xs[dx]) * 3; // R, G, B
                put(out, dx, dy, p[0], p[1], p[2]);
            }
        }
    }
    stbi_image_free(rgb);
    return ok;
}
} // namespace

bool native_rgb888() { return LV_COLOR_DEPTH != 16; }

bool decode_image(const uint8_t* data, size_t size, int max_w, int max_h, bool cover, DecodedImage& out,
                  std::string& error)
{
    if (decode_jpeg(data, size, max_w, max_h, cover, out, error)) return true;
    // progressive JPEG or PNG
    return decode_with_stb(data, size, max_w, max_h, cover, out, error);
}

bool decode_jpeg(const uint8_t* data, size_t size, int max_w, int max_h, bool cover, DecodedImage& out,
                 std::string& error)
{
    if (!data || size < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        error = "not a JPEG";
        return false;
    }
    Source src{data, size};
    std::unique_ptr<uint8_t[]> work(new (std::nothrow) uint8_t[kWorkBytes]);
    if (!work) {
        error = "out of memory";
        return false;
    }
    JDEC jd;
    JRESULT rc = jd_prepare(&jd, input, work.get(), kWorkBytes, &src);
    if (rc != JDR_OK) {
        error = rc == JDR_FMT3 ? "unsupported JPEG (progressive?)" : "JPEG header error " + std::to_string(rc);
        return false;
    }
    if (!plan(jd.width, jd.height, max_w, max_h, cover, out, src.xs, src.ys, error)) return false;
    src.out = &out;
    rc = jd_decomp(&jd, output, 0);
    if (rc != JDR_OK) {
        error = "JPEG decode error " + std::to_string(rc);
        return false;
    }
    return true;
}

} // namespace app::media
