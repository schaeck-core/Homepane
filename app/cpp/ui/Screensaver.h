#ifndef APP_CPP_UI_SCREENSAVER_H
#define APP_CPP_UI_SCREENSAVER_H

#include "../core/CancellationToken.h"
#include "../eventcalendar/EventFeed.h"
#include "../media/ImageDecoder.h"

#include "lvgl/lvgl.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app::ui {

// The screensaver of the design guide (Screensaver and ScreensaverPhoto
// artboards). Two views:
// - events (the HA Eventcalendar is on and has events): full-screen
//   pictures changing every card_seconds, one card per event (date chip,
//   title, time, place, distance), with progress segments at the top, the
//   clock top left and the waste pickup top right;
// - picture (calendar off or nothing coming up): the background picture
//   with a large clock, date and weather bottom left.
// In both, the track that plays (Music Assistant) top right, below the
// waste pickup.
// A swipe changes the card by hand (and pauses the change for
// idle_seconds); a tap ends the screensaver. UI thread.
class Screensaver {
public:
    struct Weather {
        bool known = false;
        std::string condition; // HA condition ("sunny", ...)
        double temperature = 0;
    };
    struct NowPlaying {
        bool playing = false;
        std::string title, artist;
        std::string cover_url; // "" : none
    };
    struct Sources {
        const app::eventcalendar::EventFeed* feed = nullptr;
        bool events_enabled = false;
        std::string background;               // picture URL ("builtin:..." or an HA media URL)
        std::function<Weather()> weather;     // may be empty
        std::function<float()> room_temperature; // NAN: none
        std::string start_event;              // show this event first (the settings' list)
        std::function<NowPlaying()> now_playing; // may be empty
    };

    ~Screensaver();

    void show(Sources sources);
    void hide();
    bool is_open() const { return root_ != nullptr; }
    // The feed changed: keeps the shown card when it is still there.
    void feed_changed();
    // A new background picture (e.g. resolved from HA media).
    void set_background(const std::string& url);

    // Texts of an event card (pure; host-tested): "Sa. 10. Okt.",
    // "11:00–18:00 Uhr" / "Ganztägig" / "15:00 Uhr – Mo. 12. Okt.", "8,4 km entfernt".
    static std::string chip_text(const app::eventcalendar::Event& e);
    static std::string time_text(const app::eventcalendar::Event& e);
    static std::string distance_text(const app::eventcalendar::Event& e);
    // The waste pickup's circle colour from its title (yellow bin, paper, organic, glass).
    static uint32_t waste_color(const std::string& title);

    // LVGL callbacks (free functions in the .cpp forward here).
    void on_root_deleted();
    void on_tick();
    void on_gesture(lv_dir_t dir);

private:
    bool event_view() const;
    int first_card() const;
    int card_count() const;
    void build();
    void render();
    void render_card();
    void render_waste();
    void render_progress();
    void render_music();
    void load_cover(const std::string& url);
    void step(int delta);
    void load_picture(const std::string& url);
    void show_picture(std::shared_ptr<app::media::DecodedImage> picture);
    void reveal();

    Sources src_;
    int card_ = 0;               // 0: the background picture (no events); 1..n: the events
    std::string card_event_;     // the event shown, by id (survives updates)
    uint32_t card_since_ = 0;    // lv_tick when the card appeared
    uint32_t paused_until_ = 0;  // after a swipe
    std::string picture_url_;    // what the picture shows
    uint32_t shown_at_ = 0;      // lv_tick of show(): revealed with the picture or after kRevealMs
    std::shared_ptr<app::media::DecodedImage> cached_; // the background, kept between screensavers
    std::string cached_key_;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* image_ = nullptr;
    lv_obj_t* segments_ = nullptr;
    lv_obj_t* clock_box_ = nullptr; // events view: top left
    lv_obj_t* time_ = nullptr;
    lv_obj_t* meridiem_ = nullptr; // AM/PM (12-hour format)
    lv_obj_t* date_ = nullptr;
    lv_obj_t* big_box_ = nullptr; // picture view: bottom left
    lv_obj_t* big_time_ = nullptr;
    lv_obj_t* big_meridiem_ = nullptr;
    lv_obj_t* big_date_ = nullptr;
    lv_obj_t* big_weather_icon_ = nullptr;
    lv_obj_t* big_weather_ = nullptr;
    lv_obj_t* waste_ = nullptr;
    lv_obj_t* waste_dot_ = nullptr;
    lv_obj_t* waste_title_ = nullptr;
    lv_obj_t* waste_hint_ = nullptr;
    lv_obj_t* own_card_ = nullptr;
    lv_obj_t* own_weather_icon_ = nullptr;
    lv_obj_t* own_weather_ = nullptr;
    lv_obj_t* own_inside_ = nullptr;
    lv_obj_t* own_next_ = nullptr;
    lv_obj_t* event_card_ = nullptr;
    lv_obj_t* ev_chip_ = nullptr;
    lv_obj_t* ev_title_ = nullptr;
    lv_obj_t* ev_time_ = nullptr;
    lv_obj_t* ev_place_ = nullptr;
    lv_obj_t* ev_distance_ = nullptr;
    lv_obj_t* symbol_badge_ = nullptr;
    lv_obj_t* music_ = nullptr;
    lv_obj_t* music_cover_ = nullptr; // the box: an icon until the picture is there
    lv_obj_t* music_image_ = nullptr;
    lv_obj_t* music_title_ = nullptr;
    lv_obj_t* music_artist_ = nullptr;
    lv_timer_t* tick_ = nullptr;

    std::string music_key_; // what the music box shows
    std::string cover_url_;
    lv_image_dsc_t cover_dsc_{};
    std::shared_ptr<app::media::DecodedImage> cover_;
    app::core::CancellationSource cover_load_;

    lv_image_dsc_t dsc_{};
    std::shared_ptr<app::media::DecodedImage> picture_;
    app::core::CancellationSource pending_;
};

} // namespace app::ui

#endif // APP_CPP_UI_SCREENSAVER_H
