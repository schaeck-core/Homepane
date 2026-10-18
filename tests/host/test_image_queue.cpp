// ImageLoader's queue: at most kMaxParallel loads at a time, the newest
// waiting one first, cancelled ones never started; covers kept decoded
// (memory) and as files (FileImageCache). Fake network and runners.

#include "../../app/cpp/media/FileImageCache.h"
#include "../../app/cpp/media/ImageLoader.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace app::media;

namespace {
int g_failures = 0;

void expect(bool condition, const std::string& description)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description.c_str());
        ++g_failures;
    } else {
        std::fprintf(stdout, "OK:   %s\n", description.c_str());
    }
}

// a 32 x 16 baseline JPEG (left red, right blue) from test_tiles.cpp would do;
// any bytes that do not decode still finish the job (with an error)
struct FakeHttp final : app::net::IHttpFetcher {
    std::vector<std::string> fetched;
    bool get(const std::string& url, const std::string&, size_t, std::string& body, std::string&) override
    {
        fetched.push_back(url);
        body = "not a picture";
        return true;
    }
    bool stream(const std::string&, const std::string&, const std::function<bool(const char*, size_t)>&,
                std::string&) override
    {
        return false;
    }
    bool post(const std::string&, const std::string&, const std::string&, const std::string&, int& status, std::string&,
              std::string&) override
    {
        status = 0;
        return false;
    }
};

// jobs wait until the test runs them
struct ManualRunner final : app::net::IBackgroundRunner {
    std::vector<std::function<void()>> jobs;
    void run(std::function<void()> job) override { jobs.push_back(std::move(job)); }
    void run_one()
    {
        auto job = std::move(jobs.front());
        jobs.erase(jobs.begin());
        job();
    }
};

struct ManualUi final : app::core::ITaskRunner {
    std::vector<std::pair<void (*)(void*), void*>> posted;
    void post(void (*fn)(void*), void* ctx, const app::core::PostOptions&) override { posted.emplace_back(fn, ctx); }
    void drain() override
    {
        auto now = std::move(posted);
        posted.clear();
        for (auto& p : now) p.first(p.second);
    }
};
} // namespace

int main()
{
    FakeHttp http;
    ManualRunner runner;
    ManualUi ui;
    ImageLoader loader(http, runner, ui, [] { return ImageLoader::Server{"http://ha:8123", "t"}; });

    std::vector<std::string> done;
    auto load = [&](const std::string& url, app::core::CancellationToken token) {
        loader.load(url, 80, 80, true, token, [&done, url](std::shared_ptr<DecodedImage>, const std::string&) {
            done.push_back(url);
        });
    };
    app::core::CancellationSource keep;
    for (int i = 0; i < 6; ++i) load("http://x/" + std::to_string(i), keep.token());
    expect(runner.jobs.size() == 3, "six loads: three started, three waiting");

    app::core::CancellationSource gone;
    load("http://x/cancelled", gone.token());
    gone.cancel();
    load("http://x/newest", keep.token());

    runner.run_one(); // one finishes ...
    ui.drain();       // ... and its place goes to the newest waiting load
    expect(runner.jobs.size() == 3, "a finished load frees its place");
    runner.run_one();
    runner.run_one();
    runner.run_one();
    ui.drain();
    while (!runner.jobs.empty()) {
        runner.run_one();
        ui.drain();
    }
    expect(http.fetched.size() == 7 && http.fetched[3] == "http://x/newest",
           "the newest waiting load goes first; the cancelled one never starts");
    bool cancelled_fetched = false;
    for (const auto& u : http.fetched) cancelled_fetched |= u == "http://x/cancelled";
    expect(!cancelled_fetched && done.size() == 7, "seven deliveries, none for the cancelled load");

    // on screen first: a page built top to bottom shows its first rows first
    {
        FakeHttp http2;
        ManualRunner runner2;
        ImageLoader l2(http2, runner2, ui, [] { return ImageLoader::Server{"http://ha:8123", "t"}; });
        for (int i = 0; i < 6; ++i) {
            ImageLoader::Options o;
            o.visible = [i] { return i == 4; }; // only the fifth is on screen
            l2.load("http://y/" + std::to_string(i), 80, 80, true, keep.token(),
                    [](std::shared_ptr<DecodedImage>, const std::string&) {}, o);
        }
        while (!runner2.jobs.empty()) {
            runner2.run_one();
            ui.drain();
        }
        expect(http2.fetched.size() == 6 && http2.fetched[3] == "http://y/4" && http2.fetched[4] == "http://y/5",
               "the waiting load on screen goes first, then the newest");
    }

    // the file cache: 8.3 names; put / get / clear
    expect(FileImageCache::file_name("http://ma:8095/imageproxy/abc?size=80&fmt=jpeg").size() == 12,
           "file names are 8.3 (FAT without long names)");
    FileImageCache cache("build/test_image_cache", 2);
    cache.clear();
    cache.put("a", "AAA");
    cache.put("b", "BBB");
    std::string bytes;
    expect(cache.get("a", bytes) && bytes == "AAA" && !cache.get("zzz", bytes), "a file comes back; an unknown URL not");
    cache.put("c", "CCC");
    cache.put("d", "DDD"); // beyond 2 (+10 %): the oldest go
    expect(cache.files() <= 3, "at most max_files (+10 %) files are kept");
    cache.clear();
    expect(!cache.get("c", bytes) && cache.files() == 0, "clear removes them all");

    std::printf("%s\n", g_failures ? "FAILED" : "ALL OK");
    return g_failures ? 1 : 0;
}
