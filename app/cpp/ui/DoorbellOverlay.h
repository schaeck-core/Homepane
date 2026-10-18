#ifndef APP_CPP_UI_DOORBELLOVERLAY_H
#define APP_CPP_UI_DOORBELLOVERLAY_H

#include "../core/CancellationToken.h"
#include "../media/ImageDecoder.h"

#include "lvgl/lvgl.h"

#include <functional>
#include <memory>
#include <string>

namespace app::ui {

// The doorbell overlay of the design guide (Doorbell artboard), above
// everything: the camera picture refreshed continuously, "Es klingelt an der
// Haustür · vor N Sekunden", close, and hold to open. Wakes the display and
// keeps it on while shown; closes by itself after two minutes, five seconds
// after opening. UI thread.
class DoorbellOverlay {
public:
    using OpenFn = std::function<void(std::function<void(bool ok)> done)>;

    ~DoorbellOverlay();

    // camera: "camera.x" (may be empty); open: null hides the open button.
    // A ring while shown restarts the "seconds ago" count.
    void show(const std::string& camera, OpenFn open);
    void close();
    bool is_open() const { return root_ != nullptr; }

    // LVGL callbacks (free functions in the .cpp forward here).
    void on_root_deleted();
    void on_tick();
    void on_hold(bool pressed);
    void on_hold_step();
    void load_picture();  // stills, one after the other (cameras without a stream)
    void start_stream();  // the camera's MJPEG stream

private:
    void set_texts(const char* title, const std::string& sub);
    void opened(bool ok);

    std::string camera_;
    OpenFn open_;
    uint32_t rang_at_ = 0;  // lv_tick of the last ring
    uint32_t closes_at_ = 0; // lv_tick; 0: two minutes after the last ring
    int progress_ = 0;      // hold to open, 0..100
    bool opening_ = false, done_ = false;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* frame_ = nullptr;
    lv_obj_t* image_ = nullptr;
    lv_obj_t* placeholder_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* sub_ = nullptr;
    lv_obj_t* open_btn_ = nullptr;
    lv_obj_t* open_fill_ = nullptr;
    lv_obj_t* open_label_ = nullptr;
    lv_obj_t* open_icon_ = nullptr;
    lv_timer_t* tick_ = nullptr;
    lv_timer_t* hold_timer_ = nullptr;
    lv_timer_t* next_picture_ = nullptr;

    lv_image_dsc_t dsc_{};
    std::shared_ptr<app::media::DecodedImage> picture_;
    app::core::CancellationSource pending_;
    bool loading_ = false;
    bool streaming_ = false;
    uint32_t frames_ = 0, fps_since_ = 0; // measured picture rate (log)
    void show_picture(std::shared_ptr<app::media::DecodedImage> picture);
};

} // namespace app::ui

#endif // APP_CPP_UI_DOORBELLOVERLAY_H
