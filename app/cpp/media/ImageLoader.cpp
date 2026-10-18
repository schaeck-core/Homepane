#include "ImageLoader.h"

#include <atomic>
#include <map>
#include <mutex>
#include <chrono>
#include <cstdio>

namespace app::media {

namespace {
constexpr size_t kMaxBytes = 2 * 1024 * 1024;
ImageLoader* g_loader = nullptr;

struct Builtin {
    const unsigned char* data;
    size_t size;
};
std::map<std::string, Builtin>& builtins()
{
    static std::map<std::string, Builtin> b;
    return b;
}

bool read_file(const std::string& path, std::string& body, std::string& error)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = "no file";
        return false;
    }
    body.clear();
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        if (body.size() + n > kMaxBytes) {
            std::fclose(f);
            error = "too large";
            return false;
        }
        body.append(buf, n);
    }
    std::fclose(f);
    return true;
}
} // namespace

ImageLoader* image_loader() { return g_loader; }
void register_builtin_picture(const std::string& name, const unsigned char* data, size_t size)
{
    builtins()[name] = Builtin{data, size};
}
void set_image_loader(ImageLoader* loader) { g_loader = loader; }

struct ImageLoader::Delivery {
    app::core::CancellationToken token;
    Done done;
    std::shared_ptr<DecodedImage> image;
    std::string error;
    ImageLoader* owner = nullptr; // a queued load: frees its place
    std::string remember_key;     // Options::cache: kept decoded
    // timing (ms since the load was asked for), for the slow-load log
    std::string url;
    long long started = 0, fetched = 0, decoded = 0;
    bool from_file = false;
    std::chrono::steady_clock::time_point asked;
};

namespace {
long long ms_since(std::chrono::steady_clock::time_point t)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count();
}
} // namespace

ImageLoader::ImageLoader(app::net::IHttpFetcher& http, app::net::IBackgroundRunner& background,
                         app::core::ITaskRunner& ui, std::function<Server()> server)
    : http_(http), background_(background), ui_(ui), server_(std::move(server)) {}

std::string ImageLoader::resolve(const std::string& path_or_url, const Server& server, bool* with_token)
{
    if (path_or_url.rfind("builtin:", 0) == 0) {
        *with_token = false;
        return path_or_url;
    }
    if (path_or_url.find("://") != std::string::npos) { // http(s), file (simulator demo)
        // another server never gets the Home Assistant token
        *with_token = !server.base_url.empty() && path_or_url.rfind(server.base_url + "/", 0) == 0;
        return path_or_url;
    }
    *with_token = true;
    std::string base = server.base_url;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base + (path_or_url.empty() || path_or_url[0] != '/' ? "/" : "") + path_or_url;
}

void ImageLoader::load(const std::string& path_or_url, int max_w, int max_h, bool cover,
                       app::core::CancellationToken token, Done done)
{
    load(path_or_url, max_w, max_h, cover, std::move(token), std::move(done), Options{});
}

void ImageLoader::load(const std::string& path_or_url, int max_w, int max_h, bool cover,
                       app::core::CancellationToken token, Done done, Options options)
{
    const Server server = server_ ? server_() : Server{};
    bool with_token = false;
    const std::string url = resolve(path_or_url, server, &with_token);
    const std::string bearer = with_token ? server.token : std::string();
    const std::string key = options.cache ? url + "|" + std::to_string(max_w) + "x" + std::to_string(max_h) + (cover ? "c" : "f")
                                          : std::string();
    if (!key.empty()) {
        if (auto image = remembered(key); image && options.immediate) {
            if (!token.is_cancelled() && done) done(image, {});
            return;
        }
        if (auto image = remembered(key)) { // decoded already: still delivered later, like a load
            auto* d = new Delivery{token, std::move(done), std::move(image), {}};
            app::core::PostOptions po;
            po.critical = true;
            ui_.post(&ImageLoader::deliver, d, po);
            return;
        }
    }
    FileImageCache* files = options.cache ? file_cache_ : nullptr;
    Job job;
    job.token = token;
    job.visible = std::move(options.visible);
    const auto asked = std::chrono::steady_clock::now();
    job.start = [this, url, bearer, max_w, max_h, cover, token, files, key, asked, done = std::move(done)]() mutable {
        (load_runner_ ? *load_runner_ : background_).run([this, url, bearer, max_w, max_h, cover, token, files, key, asked, done = std::move(done)]() mutable {
            auto* d = new Delivery{token, std::move(done), nullptr, {}};
            d->owner = this;
            d->remember_key = key;
            d->url = url;
            d->asked = asked;
            d->started = ms_since(asked);
            if (!token.is_cancelled()) {
                std::string body, error;
                const bool local = url.rfind("file://", 0) == 0; // a picture on the panel
                const bool builtin = url.rfind("builtin:", 0) == 0; // compiled in
                bool got = false;
                if (builtin) {
                    auto it = builtins().find(url.substr(8));
                    got = it != builtins().end();
                    if (got) body.assign(reinterpret_cast<const char*>(it->second.data), it->second.size);
                    else error = "no such built-in picture";
                } else if (local) {
                    got = read_file(url.substr(7), body, error);
                } else {
                    got = files && files->get(url, body);
                    d->from_file = got;
                    if (!got) {
                        got = http_.get(url, bearer, kMaxBytes, body, error);
                        if (got && files) files->put(url, body);
                    }
                }
                d->fetched = ms_since(asked);
                if (got && !token.is_cancelled()) {
                    auto image = std::make_shared<DecodedImage>();
                    if (decode_image(reinterpret_cast<const uint8_t*>(body.data()), body.size(), max_w, max_h, cover,
                                    *image, error)) {
                        d->image = std::move(image);
                    }
                }
                d->decoded = ms_since(asked);
                d->error = error;
            }
            app::core::PostOptions po;
            po.critical = true; // frees the delivery (and the place in the queue)
            ui_.post(&ImageLoader::deliver, d, po);
        });
    };
    waiting_.push_front(std::move(job)); // newest first
    pump();
}

// Starts waiting loads while fewer than kMaxParallel run. UI thread.
void ImageLoader::pump()
{
    while (running_ < kMaxParallel && !waiting_.empty()) {
        // the newest one on screen, else the newest
        auto pick = waiting_.begin();
        for (auto it = waiting_.begin(); it != waiting_.end(); ++it) {
            if (!it->token.is_cancelled() && it->visible && it->visible()) {
                pick = it;
                break;
            }
        }
        Job job = std::move(*pick);
        waiting_.erase(pick);
        if (job.token.is_cancelled()) continue; // scrolled away before its turn
        ++running_;
        job.start();
    }
}

std::shared_ptr<DecodedImage> ImageLoader::remembered(const std::string& key)
{
    auto it = lru_index_.find(key);
    if (it == lru_index_.end()) return nullptr;
    lru_.splice(lru_.begin(), lru_, it->second); // most recently used
    return it->second->second;
}

void ImageLoader::remember(const std::string& key, std::shared_ptr<DecodedImage> image)
{
    if (key.empty() || !image || lru_index_.count(key)) return;
    const size_t bytes = image->pixels.size();
    if (bytes > kMemoryCacheBytes / 4) return; // full-screen pictures are not list covers
    lru_.emplace_front(key, std::move(image));
    lru_index_[key] = lru_.begin();
    lru_bytes_ += bytes;
    while (lru_bytes_ > kMemoryCacheBytes && !lru_.empty()) {
        lru_bytes_ -= lru_.back().second->pixels.size();
        lru_index_.erase(lru_.back().first);
        lru_.pop_back();
    }
}

struct ImageLoader::UploadDone {
    std::function<void(int)> done;
    int status = 0;
};

std::string ImageLoader::upload_body(const std::string& boundary, const std::string& folder_content_id,
                                     const std::string& filename, const std::string& jpeg)
{
    std::string b;
    b += "--" + boundary + "\r\n";
    b += "Content-Disposition: form-data; name=\"media_content_id\"\r\n\r\n";
    b += folder_content_id + "\r\n";
    b += "--" + boundary + "\r\n";
    b += "Content-Disposition: form-data; name=\"file\"; filename=\"" + filename + "\"\r\n";
    b += "Content-Type: image/jpeg\r\n\r\n";
    b += jpeg;
    b += "\r\n--" + boundary + "--\r\n";
    return b;
}

void ImageLoader::upload(const std::string& folder_content_id, const std::string& filename, std::string jpeg,
                         std::function<void(int status)> done)
{
    const Server server = server_ ? server_() : Server{};
    bool with_token = false;
    const std::string url = resolve("/api/media_source/local_source/upload", server, &with_token);
    const std::string boundary = "homepane-upload-7f3a9c";
    std::string body = upload_body(boundary, folder_content_id, filename, jpeg);
    background_.run([this, url, token = server.token, boundary, body = std::move(body), done = std::move(done)]() mutable {
        auto* d = new UploadDone{std::move(done), 0};
        std::string response, error;
        if (!http_.post(url, token, "multipart/form-data; boundary=" + boundary, body, d->status, response, error)) {
            d->status = 0;
        }
        app::core::PostOptions options;
        options.critical = true; // frees the delivery
        ui_.post(&ImageLoader::deliver_upload, d, options);
    });
}

void ImageLoader::deliver_upload(void* ctx)
{
    std::unique_ptr<UploadDone> d(static_cast<UploadDone*>(ctx));
    if (d->done) d->done(d->status);
}

void ImageLoader::deliver(void* ctx)
{
    std::unique_ptr<Delivery> d(static_cast<Delivery*>(ctx));
    if (d->owner && d->owner->logger_ && ms_since(d->asked) > kSlowMs) {
        const std::string where = d->url.substr(0, d->url.find('?')); // no tokens in the log
        char line[200];
        std::snprintf(line, sizeof(line), "ImageLoader: slow %lld ms (waited %lld, %s %lld, decode %lld, %d running, %d waiting) %.60s",
                      ms_since(d->asked), d->started, d->from_file ? "SD" : "fetch", d->fetched - d->started,
                      d->decoded - d->fetched, d->owner->running_, static_cast<int>(d->owner->waiting_.size()),
                      where.c_str());
        d->owner->logger_->log(app::core::LogLevel::Warn, line);
    }
    if (d->owner) {
        --d->owner->running_;
        if (d->image) d->owner->remember(d->remember_key, d->image);
        d->owner->pump();
    }
    if (d->token.is_cancelled() || !d->done) return;
    d->done(d->image, d->image ? std::string() : d->error);
}

bool ImageLoader::take_jpeg(std::string& buf, std::string& jpeg)
{
    const size_t soi = buf.find("\xFF\xD8");
    if (soi == std::string::npos) {
        // keep a last 0xFF: it may start the next marker
        buf.erase(0, buf.empty() || static_cast<unsigned char>(buf.back()) != 0xFF ? buf.size() : buf.size() - 1);
        return false;
    }
    const size_t eoi = buf.find("\xFF\xD9", soi + 2);
    if (eoi == std::string::npos) {
        buf.erase(0, soi);
        return false;
    }
    jpeg.assign(buf, soi, eoi + 2 - soi);
    buf.erase(0, eoi + 2);
    return true;
}

namespace {
// The newest decoded picture, waiting for the UI: decoding goes on while the
// UI shows the last one (two cores), and the UI always takes the newest.
struct StreamSlot {
    std::mutex lock;
    std::shared_ptr<DecodedImage> latest;
    bool posted = false; // a delivery is queued
};

struct StreamFrame {
    app::core::CancellationToken token;
    std::shared_ptr<ImageLoader::Done> frame;
    std::shared_ptr<StreamSlot> slot;
};

void deliver_frame(void* ctx)
{
    std::unique_ptr<StreamFrame> f(static_cast<StreamFrame*>(ctx));
    std::shared_ptr<DecodedImage> image;
    {
        std::lock_guard<std::mutex> g(f->slot->lock);
        image = std::move(f->slot->latest);
        f->slot->posted = false;
    }
    if (image && !f->token.is_cancelled() && *f->frame) (*f->frame)(image, std::string());
}

struct StreamEnd {
    app::core::CancellationToken token;
    std::function<void(const std::string&)> ended;
    std::string error;
};

void deliver_end(void* ctx)
{
    std::unique_ptr<StreamEnd> e(static_cast<StreamEnd*>(ctx));
    if (!e->token.is_cancelled() && e->ended) e->ended(e->error);
}
} // namespace

void ImageLoader::stream(const std::string& path_or_url, int max_w, int max_h, bool cover,
                         app::core::CancellationToken token, Done frame,
                         std::function<void(const std::string& error)> ended)
{
    const Server server = server_ ? server_() : Server{};
    bool with_token = false;
    const std::string url = resolve(path_or_url, server, &with_token);
    const std::string bearer = with_token ? server.token : std::string();
    auto shared_frame = std::make_shared<Done>(std::move(frame));
    background_.run([this, url, bearer, max_w, max_h, cover, token, shared_frame, ended = std::move(ended)]() mutable {
        auto slot = std::make_shared<StreamSlot>();
        std::string buf, jpeg, error;
        // measured, logged every 5 s: what arrives, what is skipped, decode time
        using clock = std::chrono::steady_clock;
        auto since = clock::now();
        unsigned seen = 0, skipped = 0, decoded = 0;
        double decode_ms = 0;
        const bool ok = http_.stream(
            url, bearer,
            [&](const char* data, size_t size) {
                if (token.is_cancelled()) return false;
                buf.append(data, size);
                if (buf.size() > kMaxBytes) buf.clear(); // garbage, not a picture stream
                while (take_jpeg(buf, jpeg)) {
                    ++seen;
                    const auto now = clock::now();
                    if (now - since >= std::chrono::seconds(5)) {
                        const double secs = std::chrono::duration<double>(now - since).count();
                        std::printf("camera stream: %.1f/s in, %.1f/s decoded (%.0f ms each), %.1f/s skipped\n",
                                    seen / secs, decoded / secs, decoded ? decode_ms / decoded : 0.0, skipped / secs);
                        since = now;
                        seen = skipped = decoded = 0;
                        decode_ms = 0;
                    }
                    // pictures that arrived during the last decode are old: only the newest counts
                    if (!buf.empty() && buf.find("\xFF\xD9") != std::string::npos) {
                        ++skipped;
                        continue;
                    }
                    auto image = std::make_shared<DecodedImage>();
                    std::string decode_error;
                    const auto t0 = clock::now();
                    const bool ok_picture = decode_image(reinterpret_cast<const uint8_t*>(jpeg.data()), jpeg.size(), max_w,
                                                         max_h, cover, *image, decode_error);
                    decode_ms += std::chrono::duration<double, std::milli>(clock::now() - t0).count();
                    ++decoded;
                    if (!ok_picture) continue;
                    bool post = false;
                    {
                        std::lock_guard<std::mutex> g(slot->lock);
                        if (slot->latest) ++skipped; // replaced before the UI took it
                        slot->latest = std::move(image);
                        post = !slot->posted;
                        slot->posted = true;
                    }
                    if (post) {
                        app::core::PostOptions options;
                        options.critical = true; // frees the frame and clears posted
                        ui_.post(&deliver_frame, new StreamFrame{token, shared_frame, slot}, options);
                    }
                }
                return !token.is_cancelled();
            },
            error);
        if (token.is_cancelled()) return;
        app::core::PostOptions options;
        options.critical = true;
        ui_.post(&deliver_end, new StreamEnd{token, std::move(ended), ok ? std::string("stream ended") : error}, options);
    });
}

} // namespace app::media
