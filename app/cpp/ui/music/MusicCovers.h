#ifndef APP_CPP_UI_MUSIC_MUSICCOVERS_H
#define APP_CPP_UI_MUSIC_MUSICCOVERS_H

#include "../../music_assistant/MaPlayerStore.h"

#include "lvgl/lvgl.h"

#include <string>

namespace app::ui {

// Covers in lists (queue, search, browse): put(box, image) shows the
// picture inside `box` (a placeholder until it is there). The load goes
// through the image loader's queue and caches (memory, SD card) and is
// cancelled when the box is deleted (a list rebuilt or closed). UI thread.
class MusicCovers {
public:
    explicit MusicCovers(std::string image_base) : image_base_(std::move(image_base)) {}

    // `size`: the box's side in px; `radius`: its corners
    void put(lv_obj_t* box, const app::music_assistant::MaImage& image, int32_t size, int32_t radius) const;

private:
    std::string image_base_;
};

} // namespace app::ui

#endif // APP_CPP_UI_MUSIC_MUSICCOVERS_H
