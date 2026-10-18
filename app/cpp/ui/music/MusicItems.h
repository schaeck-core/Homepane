#ifndef APP_CPP_UI_MUSIC_MUSICITEMS_H
#define APP_CPP_UI_MUSIC_MUSICITEMS_H

#include "../../music_assistant/MaController.h"

#include "lvgl/lvgl.h"

#include <functional>
#include <string>

namespace app::ui {

// What the search, its covers and the playlist / album / artist pages share:
// how a media item is labelled and drawn, and the sheet that asks how it
// goes into the queue.

// "Playlist", "Album", ... and its Lucide icon
const char* music_type_label(const std::string& media_type);
const char* music_type_icon(const std::string& media_type);
// playlists, albums and artists open a page; tracks and radio play
bool music_opens_page(const std::string& media_type);
// covers: artists round, radio stations a bit rounder than the rest
int32_t music_cover_radius(const std::string& media_type, int32_t size);
// "Playlist · Music Assistant", "Album · Queen", "Titel · Queen · Jazz"
std::string music_item_sub(const app::music_assistant::MaQueueItem& it, bool with_type);
// a cover box in one of the placeholder tones (n), the type's icon in it
lv_obj_t* music_placeholder(lv_obj_t* parent, int32_t size, const app::music_assistant::MaQueueItem& it, size_t n);
uint32_t music_tone(size_t n);
// "3:45"; "48 Min.", "1 Std. 12 Min."
std::string music_track_time(double seconds);
std::string music_total_time(double seconds);

// Covers only near what is shown: every cover on screen costs the panel
// internal RAM (which Wi-Fi needs), so a long list keeps the covers of the
// rows / cards within half a screen of the visible part and drops the others;
// those that come near again load anew (from the image cache). `list`'s
// child i shows item i, its child 0 is the cover box; `put(i, box)` puts
// item i's cover into box. Call when `scroller` stopped and after adding.
using MusicCoverPut = std::function<void(size_t index, lv_obj_t* box)>;
void music_window_covers(lv_obj_t* scroller, lv_obj_t* list, const MusicCoverPut& put);

// The sheet over a page (`root`: a page's root with UI_EDGE padding): play,
// replace the queue, play next, add to the end. `done` gets the choice; the
// sheet closes itself (also on a tap beside it).
using MusicEnqueueDone = std::function<void(app::music_assistant::MaController::Enqueue how)>;
void music_enqueue_sheet(lv_obj_t* root, const app::music_assistant::MaQueueItem& item, MusicEnqueueDone done);

} // namespace app::ui

#endif // APP_CPP_UI_MUSIC_MUSICITEMS_H
