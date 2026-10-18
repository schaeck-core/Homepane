#include "FileImageCache.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

namespace app::media {

namespace {
constexpr size_t kMaxFileBytes = 2 * 1024 * 1024;

bool is_cache_file(const char* name)
{
    const std::string n = name;
    return n.size() == 12 && n.compare(8, 4, ".JPG") == 0;
}
} // namespace

FileImageCache::FileImageCache(std::string dir, size_t max_files) : dir_(std::move(dir)), max_files_(max_files) {}

std::string FileImageCache::file_name(const std::string& url)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : url) {
        h ^= c;
        h *= 16777619u;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08lX.JPG", static_cast<unsigned long>(h));
    return buf;
}

// The folder made and counted once, on first use (in a background job: the
// SD card may take a moment).
void FileImageCache::prepare()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (ready_) return;
    ready_ = true;
    mkdir(dir_.c_str(), 0775);
    size_t n = 0;
    if (DIR* d = opendir(dir_.c_str())) {
        while (dirent* e = readdir(d)) {
            if (is_cache_file(e->d_name)) ++n;
        }
        closedir(d);
    }
    files_ = n;
}

bool FileImageCache::get(const std::string& url, std::string& bytes)
{
    prepare();
    FILE* f = std::fopen((dir_ + "/" + file_name(url)).c_str(), "rb");
    if (!f) return false;
    bytes.clear();
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && bytes.size() <= kMaxFileBytes) bytes.append(buf, n);
    std::fclose(f);
    return !bytes.empty() && bytes.size() <= kMaxFileBytes;
}

void FileImageCache::put(const std::string& url, const std::string& bytes)
{
    if (bytes.empty() || bytes.size() > kMaxFileBytes) return;
    prepare();
    const std::string name = file_name(url);
    const std::string path = dir_ + "/" + name;
    const std::string tmp = dir_ + "/" + name.substr(0, 8) + ".TMP"; // complete files only
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    struct stat st {};
    const bool existed = stat(path.c_str(), &st) == 0;
    if (!ok || (existed && std::remove(path.c_str()) != 0) || std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return;
    }
    if (!existed) ++files_;
    if ((++writes_ % 64) == 0 || files_ > max_files_ + max_files_ / 10) trim();
}

// beyond max_files: the oldest files (by time of writing) go
void FileImageCache::trim()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<long long, std::string>> all;
    if (DIR* d = opendir(dir_.c_str())) {
        while (dirent* e = readdir(d)) {
            if (!is_cache_file(e->d_name)) continue;
            const std::string path = dir_ + "/" + e->d_name;
            struct stat st {};
            if (stat(path.c_str(), &st) == 0) all.emplace_back(static_cast<long long>(st.st_mtime), path);
        }
        closedir(d);
    }
    files_ = all.size();
    if (all.size() <= max_files_) return;
    std::sort(all.begin(), all.end());
    const size_t drop = all.size() - max_files_;
    for (size_t i = 0; i < drop; ++i) std::remove(all[i].second.c_str());
    files_ = max_files_;
}

void FileImageCache::clear()
{
    prepare();
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> paths;
    if (DIR* d = opendir(dir_.c_str())) {
        while (dirent* e = readdir(d)) {
            if (is_cache_file(e->d_name)) paths.push_back(dir_ + "/" + e->d_name);
        }
        closedir(d);
    }
    for (const auto& p : paths) std::remove(p.c_str());
    files_ = 0;
}

} // namespace app::media
