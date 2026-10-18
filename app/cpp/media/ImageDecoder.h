#ifndef APP_CPP_MEDIA_IMAGEDECODER_H
#define APP_CPP_MEDIA_IMAGEDECODER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace app::media {

// Whether pictures are decoded to RGB888: on a 24/32-bit display (the
// panel's colour depth, LV_COLOR_DEPTH). On a 16-bit display: RGB565 with
// ordered dithering.
bool native_rgb888();

// A decoded picture, w * h pixels in LVGL's RGB888 (3 bytes: B, G, R) or
// RGB565 (2 bytes, little endian). Set rgb888 before decoding to choose.
struct DecodedImage {
    int w = 0;
    int h = 0;
    bool rgb888 = native_rgb888();
    std::vector<uint8_t> pixels;

    int bytes_per_pixel() const { return rgb888 ? 3 : 2; }
    // pixel i as 8-bit R, G, B (tests)
    void rgb_at(size_t i, int& r, int& g, int& b) const
    {
        const uint8_t* p = pixels.data() + i * static_cast<size_t>(bytes_per_pixel());
        if (rgb888) {
            b = p[0], g = p[1], r = p[2];
        } else {
            const int v = p[0] | (p[1] << 8);
            r = (v >> 11) << 3, g = ((v >> 5) & 0x3F) << 2, b = (v & 0x1F) << 3;
        }
    }
};

// Decodes a baseline JPEG (TJpgDec, bundled with LVGL) straight into the
// target size, sampling while decoding: only the small target buffer is
// allocated, never the full picture. cover: fill w x h and crop the
// middle; otherwise fit inside w x h (smaller pictures are enlarged). No
// LVGL calls, so it may run on any thread.
bool decode_jpeg(const uint8_t* data, size_t size, int max_w, int max_h, bool cover, DecodedImage& out,
                 std::string& error);

// Any picture: baseline JPEG through decode_jpeg(); progressive JPEG and
// PNG (which TJpgDec cannot read) through stb_image, which decodes the full
// picture first (on the ESP32-P4 that lands in PSRAM) and is then sampled
// down the same way.
bool decode_image(const uint8_t* data, size_t size, int max_w, int max_h, bool cover, DecodedImage& out,
                  std::string& error);

} // namespace app::media

#endif // APP_CPP_MEDIA_IMAGEDECODER_H
