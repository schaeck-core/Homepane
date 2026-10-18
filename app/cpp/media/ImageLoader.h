#ifndef APP_CPP_MEDIA_IMAGELOADER_H
#define APP_CPP_MEDIA_IMAGELOADER_H

#include "../core/CancellationToken.h"
#include "../core/ILogger.h"
#include "../core/ITaskRunner.h"
#include "../net/IHttpFetcher.h"
#include "../net/IWifiAdapter.h"
#include "FileImageCache.h"
#include "ImageDecoder.h"

#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>

namespace app::media {

// Pictures from Home Assistant (camera snapshots; later the screensaver's
// event pictures): fetched and decoded to the target size off the UI
// thread, delivered on the UI thread. A path ("/api/camera_proxy/...") is
// resolved against haas_url and sent with the token; a full URL is fetched
// as it is, without the token.
class ImageLoader {
public:
    using Done = std::function<void(std::shared_ptr<DecodedImage> image, const std::string& error)>;
    struct Server {
        std::string base_url; // "http://192.0.2.5:8123"
        std::string token;
    };

    ImageLoader(app::net::IHttpFetcher& http, app::net::IBackgroundRunner& background, app::core::ITaskRunner& ui,
                std::function<Server()> server);

    struct Options {
        // keep the file (SD card) and the decoded picture (memory): for
        // pictures that do not change behind the same URL, e.g. covers
        bool cache = false;
        // a picture still kept decoded is handed to done right away, inside
        // load() (no frame without it, e.g. a cover scrolled back into view)
        bool immediate = false;
        // whether its place is on screen now (UI thread): waiting loads that
        // are go first, so a page being built shows its first rows first
        std::function<bool()> visible;
    };

    // done runs on the UI thread, unless the token was cancelled by then.
    // At most kMaxParallel loads run at a time; waiting ones start visible
    // first (Options::visible), then newest first (in a scrolled list: the
    // rows now shown), cancelled ones never.
    void load(const std::string& path_or_url, int max_w, int max_h, bool cover, app::core::CancellationToken token,
              Done done);
    void load(const std::string& path_or_url, int max_w, int max_h, bool cover, app::core::CancellationToken token,
              Done done, Options options);
    static constexpr int kMaxParallel = 3;

    // Where loads run (default: the constructor's runner, which also runs
    // streams): e.g. a fixed pool of workers.
    void set_load_runner(app::net::IBackgroundRunner* runner) { load_runner_ = runner; }
    // Files of Options::cache pictures (none: no file cache).
    void set_file_cache(FileImageCache* cache) { file_cache_ = cache; }
    // Loads that take longer than kSlowMs are logged with where the time
    // went (waiting, fetching, decoding).
    void set_logger(app::core::ILogger* logger) { logger_ = logger; }
    static constexpr int kSlowMs = 1000;
    FileImageCache* file_cache() const { return file_cache_; }

    // A camera stream (multipart MJPEG, e.g. /api/camera_proxy_stream/...):
    // every picture, decoded and delivered like load() until the token is
    // cancelled; decoding goes on while the UI shows the last picture, and
    // only the newest is decoded and shown (no growing delay). ended(error) runs on
    // the UI thread when the stream stops by itself (not after a cancel).
    void stream(const std::string& path_or_url, int max_w, int max_h, bool cover, app::core::CancellationToken token,
                Done frame, std::function<void(const std::string& error)> ended);

    // Uploads a picture into Home Assistant's media (a folder of "My
    // media", e.g. "media-source://media_source/local/Homepane"; HA
    // creates a missing folder; admin users only). done(HTTP status, 0: no
    // answer) on the UI thread.
    void upload(const std::string& folder_content_id, const std::string& filename, std::string jpeg,
                std::function<void(int status)> done);
    // The multipart/form-data body of such an upload (pure; host-tested).
    static std::string upload_body(const std::string& boundary, const std::string& folder_content_id,
                                   const std::string& filename, const std::string& jpeg);

    // The next complete JPEG (FF D8 ... FF D9) in buf, removed from it with
    // everything before (pure; host-tested). False: none complete yet.
    static bool take_jpeg(std::string& buf, std::string& jpeg);

    // The URL to fetch and whether the token goes along (pure; host-tested).
    static std::string resolve(const std::string& path_or_url, const Server& server, bool* with_token);

private:
    struct Delivery;
    struct Job {
        app::core::CancellationToken token;
        std::function<void()> start;
        std::function<bool()> visible;
    };
    app::core::ILogger* logger_ = nullptr;
    static void deliver(void* ctx);
    void pump();
    // decoded Options::cache pictures, least recently used out first (PSRAM)
    static constexpr size_t kMemoryCacheBytes = 6 * 1024 * 1024;
    std::shared_ptr<DecodedImage> remembered(const std::string& key);
    void remember(const std::string& key, std::shared_ptr<DecodedImage> image);
    std::list<std::pair<std::string, std::shared_ptr<DecodedImage>>> lru_;
    std::unordered_map<std::string, decltype(lru_)::iterator> lru_index_;
    size_t lru_bytes_ = 0;
    std::deque<Job> waiting_; // UI thread only
    int running_ = 0;
    FileImageCache* file_cache_ = nullptr;
    app::net::IBackgroundRunner* load_runner_ = nullptr;
    struct UploadDone;
    static void deliver_upload(void* ctx);

    app::net::IHttpFetcher& http_;
    app::net::IBackgroundRunner& background_;
    app::core::ITaskRunner& ui_;
    std::function<Server()> server_;
};

// Pictures compiled into the firmware: "builtin:<name>" loads them like a URL.
void register_builtin_picture(const std::string& name, const unsigned char* data, size_t size);

// The app's loader (set by app_facade.cpp; null in host tests).
ImageLoader* image_loader();
void set_image_loader(ImageLoader* loader);

} // namespace app::media

#endif // APP_CPP_MEDIA_IMAGELOADER_H
