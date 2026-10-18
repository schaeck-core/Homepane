#ifndef APP_CPP_MEDIA_IMAGEDSC_H
#define APP_CPP_MEDIA_IMAGEDSC_H

#include "ImageDecoder.h"

#include "lvgl/lvgl.h"

namespace app::media {

// An LVGL image descriptor over a decoded picture (which must outlive it).
inline void set_image_dsc(lv_image_dsc_t& dsc, const DecodedImage& picture)
{
    dsc = lv_image_dsc_t{};
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = picture.rgb888 ? LV_COLOR_FORMAT_RGB888 : LV_COLOR_FORMAT_RGB565;
    dsc.header.w = static_cast<uint32_t>(picture.w);
    dsc.header.h = static_cast<uint32_t>(picture.h);
    dsc.header.stride = static_cast<uint32_t>(picture.w * picture.bytes_per_pixel());
    dsc.data = picture.pixels.data();
    dsc.data_size = static_cast<uint32_t>(picture.pixels.size());
}

} // namespace app::media

#endif // APP_CPP_MEDIA_IMAGEDSC_H
