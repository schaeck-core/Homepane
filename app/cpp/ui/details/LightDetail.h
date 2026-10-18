#ifndef APP_CPP_UI_DETAILS_LIGHTDETAIL_H
#define APP_CPP_UI_DETAILS_LIGHTDETAIL_H

#include <cstdint>
#include <functional>
#include <string>

struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;

namespace app::ui {

// The light detail of the design guide (LightDetail artboards): header
// with on/off, colour / white tabs, a hue x saturation field or a colour
// temperature band, brightness as a row, favourite colours, white tones
// and profiles. Talks to HA only through `send`; update() refreshes it
// from the entity while it is open. UI thread.
class LightDetail {
public:
    struct State {
        std::string name;
        bool on = false;
        int brightness = 255; // 0..255
        std::string color_mode;
        double hue = 30, saturation = 70; // hs_color
        int kelvin = 2700, min_kelvin = 2200, max_kelvin = 6500;
        bool supports_color = false, supports_temperature = false, supports_brightness = true;
    };
    // service ("turn_on" / "turn_off") and its extra service data (JSON object)
    using Send = std::function<void(const std::string& service, const std::string& json)>;

    LightDetail(Send send, std::function<void()> on_closed);
    ~LightDetail();
    LightDetail(const LightDetail&) = delete;
    LightDetail& operator=(const LightDetail&) = delete;

    void open(const State& state);
    void update(const State& state);
    bool is_open() const { return root_ != nullptr; }

    // HSL (h 0..360, s, l 0..100) -> 0xRRGGBB; the design's colour field and swatches.
    static uint32_t hsl(double h, double s, double l);
    // The colour of a colour temperature on the panel (warm amber .. cool blue).
    static uint32_t kelvin_color(int kelvin);
    // "Kerzenlicht", "Warmweiß", ...
    static const char* kelvin_name(int kelvin);

    // LVGL callbacks (free functions in the .cpp forward here).
    void on_root_deleted();
    void on_toggle();
    void on_tab();
    void on_field(bool released);
    void on_band(bool released);
    void on_brightness(bool released);
    void on_favourite(int index);
    void on_white(int index);
    void on_profile(int index);

private:
    void build();
    void refresh();
    void show_tab(bool color);
    void place_field_knob();
    void place_band_marker();

    Send send_;
    std::function<void()> on_closed_;
    State state_;
    bool color_tab_ = true;
    bool dragging_ = false;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* subtitle_ = nullptr;
    lv_obj_t* toggle_ = nullptr;
    lv_obj_t* tabs_ = nullptr;
    lv_obj_t* field_ = nullptr;
    lv_obj_t* field_knob_ = nullptr;
    lv_obj_t* band_ = nullptr;
    lv_obj_t* band_marker_ = nullptr;
    lv_obj_t* band_pill_ = nullptr;
    lv_obj_t* brightness_ = nullptr;
    lv_obj_t* favourites_ = nullptr;
    lv_obj_t* whites_ = nullptr;
    lv_obj_t* profiles_ = nullptr;
};

} // namespace app::ui

#endif // APP_CPP_UI_DETAILS_LIGHTDETAIL_H
