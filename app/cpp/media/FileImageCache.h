#ifndef APP_CPP_MEDIA_FILEIMAGECACHE_H
#define APP_CPP_MEDIA_FILEIMAGECACHE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace app::media {

// Downloaded pictures (e.g. Music Assistant covers) as files in a folder,
// on the panel the SD card: a cover the panel has seen once comes from the
// card instead of the network (the first fetch of a cover can take seconds
// while MA gets it from the streaming service).
//
// Names are 8.3 (the SD card's FAT has no long names): the URL's 32-bit
// FNV-1a hash in hex plus ".JPG". At most max_files files: beyond that the
// oldest go (checked at start and every 64 writes). Thread-safe: called from
// the image loader's background jobs.
class FileImageCache {
public:
    FileImageCache(std::string dir, size_t max_files);

    bool get(const std::string& url, std::string& bytes);
    void put(const std::string& url, const std::string& bytes);
    // all files out (the settings' "clear cache")
    void clear();
    // counted on first use: the settings ask before any cover was loaded
    size_t files()
    {
        prepare();
        return files_.load();
    }

    static std::string file_name(const std::string& url); // "1A2B3C4D.JPG"

private:
    void prepare();
    void trim();

    std::string dir_;
    size_t max_files_;
    std::atomic<size_t> files_{0};
    std::atomic<uint32_t> writes_{0};
    std::mutex mutex_; // trim and clear against each other
    bool ready_ = false;
};

} // namespace app::media

#endif // APP_CPP_MEDIA_FILEIMAGECACHE_H
