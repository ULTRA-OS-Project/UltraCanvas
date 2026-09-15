// core/UltraCanvasDiskCache.cpp
// The retention policy shared by every cache UltraCanvas keeps on disk.
// See UltraCanvasDiskCache.h for why it lives in one place.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "UltraCanvasDiskCache.h"
#include "UltraCanvasUtils.h"   // PathFromUtf8

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>

namespace UltraCanvas {
namespace DiskCache {

namespace fs = std::filesystem;

namespace {

    std::string EnvOrEmpty(const char* name) {
        const char* value = std::getenv(name);
        return (value && *value) ? std::string(value) : std::string();
    }

    // Compared as paths rather than as strings: fs::path::string_type is wide
    // on Windows and narrow elsewhere, and path comparison needs no encoding
    // conversion that could throw on an odd file name.
    bool HasWantedExtension(const fs::path& path,
                            const std::vector<std::string>& extensions) {
        const fs::path ext = path.extension();
        for (const std::string& wanted : extensions) {
            if (ext == fs::path(wanted)) return true;
        }
        return false;
    }

    // Every file this policy governs, in one pass. Collected before anything
    // is deleted: removing entries from a directory while walking it is not
    // something the iterator promises to survive.
    std::vector<fs::directory_entry> CollectFiles(
            const std::string& directory,
            const std::vector<std::string>& extensions) {
        std::vector<fs::directory_entry> files;
        if (directory.empty() || extensions.empty()) return files;
        std::error_code ec;
        fs::directory_iterator it(PathFromUtf8(directory), ec);
        if (ec) return files;
        for (const fs::directory_entry& entry : it) {
            if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
            if (!HasWantedExtension(entry.path(), extensions)) continue;
            files.push_back(entry);
        }
        return files;
    }

    size_t RemoveAll(const std::vector<fs::path>& paths) {
        size_t removed = 0;
        std::error_code ec;
        for (const fs::path& path : paths) {
            // In use by another process: it goes on the next sweep.
            if (fs::remove(path, ec)) ++removed;
            ec.clear();
        }
        return removed;
    }

} // namespace

std::string Root() {
    static const std::string root = []() -> std::string {
#if defined(_WIN32) || defined(_WIN64)
        // TEMP/TMP cover the (rare) account without a local app-data
        // directory. A cache in TEMP is still a cache: the sweep keeps it
        // bounded and anything the system clears is simply produced again.
        for (const char* name : { "LOCALAPPDATA", "TEMP", "TMP" }) {
            const std::string value = EnvOrEmpty(name);
            if (!value.empty()) return value + "\\UltraCanvas";
        }
        return {};
#elif defined(__APPLE__)
        const std::string home = EnvOrEmpty("HOME");
        return home.empty() ? std::string()
                            : home + "/Library/Caches/UltraCanvas";
#else
        const std::string xdg = EnvOrEmpty("XDG_CACHE_HOME");
        if (!xdg.empty()) return xdg + "/UltraCanvas";
        const std::string home = EnvOrEmpty("HOME");
        if (!home.empty()) return home + "/.cache/UltraCanvas";
        // Android exports HOME/TMPDIR from the activity glue; a plain TMPDIR
        // is the last writable place left anywhere else.
        const std::string tmp = EnvOrEmpty("TMPDIR");
        return tmp.empty() ? std::string() : tmp + "/UltraCanvas";
#endif
    }();
    return root;
}

std::string Directory(const std::string& name) {
    // One answer per name for the life of the process: the directory is
    // created on the first ask, and every lookup afterwards is a map read
    // rather than a create_directories() syscall.
    static std::mutex mutex;
    static std::map<std::string, std::string> resolved;

    std::lock_guard<std::mutex> lock(mutex);
    auto it = resolved.find(name);
    if (it != resolved.end()) return it->second;

    std::string directory;
    const std::string root = Root();
    if (!root.empty() && !name.empty()) {
        const std::string candidate = root + "/" + name;
        std::error_code ec;
        fs::create_directories(PathFromUtf8(candidate), ec);
        // Already there is success; create_directories reports false with no
        // error for an existing directory.
        if (!ec) directory = candidate;
    }
    resolved.emplace(name, directory);
    return directory;
}

bool Touch(const std::string& file, std::chrono::seconds minInterval) {
    if (file.empty()) return false;
    std::error_code ec;
    const fs::path path = PathFromUtf8(file);
    const auto stamp = fs::last_write_time(path, ec);
    if (ec) return false;
    const auto now = fs::file_time_type::clock::now();
    // A stamp in the future — a clock that was set back — already reads as
    // infinitely fresh to the sweep; moving it backwards would only shorten
    // the file's life.
    if (now < stamp) return false;
    if (now - stamp < minInterval) return false;
    fs::last_write_time(path, now, ec);
    return !ec;
}

size_t Sweep(const std::string& directory,
             const std::vector<std::string>& extensions,
             std::chrono::seconds maxAge) {
    const auto now = fs::file_time_type::clock::now();
    std::vector<fs::path> expired;
    std::error_code ec;
    for (const fs::directory_entry& entry : CollectFiles(directory, extensions)) {
        const auto stamp = entry.last_write_time(ec);
        // Unreadable stamp: leave the file alone rather than guess at it.
        if (ec) { ec.clear(); continue; }
        if (now < stamp) continue;          // clock set back: reads as fresh
        if (now - stamp <= maxAge) continue;
        expired.push_back(entry.path());
    }
    return RemoveAll(expired);
}

Usage Measure(const std::string& directory,
              const std::vector<std::string>& extensions) {
    Usage usage;
    std::error_code ec;
    for (const fs::directory_entry& entry : CollectFiles(directory, extensions)) {
        const auto size = entry.file_size(ec);
        if (ec) { ec.clear(); continue; }
        ++usage.files;
        usage.bytes += static_cast<uint64_t>(size);
    }
    return usage;
}

size_t Clear(const std::string& directory,
             const std::vector<std::string>& extensions) {
    std::vector<fs::path> all;
    for (const fs::directory_entry& entry : CollectFiles(directory, extensions)) {
        all.push_back(entry.path());
    }
    return RemoveAll(all);
}

} // namespace DiskCache
} // namespace UltraCanvas
