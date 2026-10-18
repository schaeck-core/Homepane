#include "MusicCovers.h"

#include "../../core/CancellationToken.h"
#include "../../media/ImageDsc.h"
#include "../../media/ImageLoader.h"

#include <memory>

namespace app::ui {

namespace {
// one cover: its picture and pending load, freed with the image object
struct Cover {
    lv_image_dsc_t dsc{};
    std::shared_ptr<app::media::DecodedImage> picture;
    app::core::CancellationSource load;
};

void cover_deleted_cb(lv_event_t* e)
{
    auto* c = static_cast<Cover*>(lv_event_get_user_data(e));
    c->load.cancel();
    if (c->picture) lv_image_cache_drop(&c->dsc);
    delete c;
}
} // namespace

void MusicCovers::put(lv_obj_t* box, const app::music_assistant::MaImage& image, int32_t size, int32_t radius) const
{
    const std::string url = app::music_assistant::ma_image_url(image_base_, image, size);
    app::media::ImageLoader* loader = app::media::image_loader();
    if (url.empty() || !loader) return;
    lv_obj_t* img = lv_image_create(box);
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(img, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(img, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_radius(img, radius, 0); // the picture's own corners (no clip_corner layer)
    auto* c = new Cover();
    lv_obj_add_event_cb(img, cover_deleted_cb, LV_EVENT_DELETE, c);
    app::media::ImageLoader::Options o;
    o.cache = true;
    o.immediate = true; // scrolled back: no frame with the placeholder
    o.visible = [box] { return lv_obj_is_visible(box); }; // the token ends with the box
    loader->load(
        url, size, size, true, c->load.token(),
        [img, c](std::shared_ptr<app::media::DecodedImage> picture, const std::string&) {
            if (!picture) return; // keeps the placeholder
            c->picture = std::move(picture);
            app::media::set_image_dsc(c->dsc, *c->picture);
            lv_image_set_src(img, &c->dsc);
            lv_obj_center(img);
            lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
        },
        o);
}

} // namespace app::ui
