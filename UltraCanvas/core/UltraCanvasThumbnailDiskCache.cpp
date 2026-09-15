// core/UltraCanvasThumbnailDiskCache.cpp
// Thumbnails kept as files between runs. See UltraCanvasThumbnailDiskCache.h
// for what is stored and why.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "UltraCanvasThumbnailDiskCache.h"
#include "UltraCanvasUtils.h"   // PathFromUtf8

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>    // GetCurrentProcessId, for the temporary's name
#else
#include <unistd.h>     // getpid, likewise
#endif

namespace UltraCanvas {
namespace ThumbnailDiskCache {

namespace fs = std::filesystem;

namespace {

    // The directory under DiskCache::Root(), and the extension every file in
    // it carries. ".tmp" is an interrupted write's leftover and is swept on
    // the same rule, exactly as the icon cache does it.
    constexpr const char* kDirectoryName = "thumbnails";

    const std::vector<std::string>& CacheExtensions() {
        static const std::vector<std::string> extensions = { ".ucth", ".tmp" };
        return extensions;
    }

    // ===== ON-DISK ENTRY FORMAT =====
    // A fixed header followed by the QOI blob. Every field in it exists to
    // answer one question — "is this file still the answer to my request?" —
    // and anything that cannot answer yes is treated as a miss.
    //
    // QOI blobs are documented (QoiPixmapCodec.h) as in-process only: their
    // byte layout is endian-local and carries no interchange guarantees. That
    // is fine for a per-user cache on one machine, and `endian` plus
    // `version` are what make it safe — a file written by a build that laid
    // the bytes out differently fails the check and is re-made rather than
    // decoded into nonsense.
    constexpr uint32_t kMagic = 0x48544355;      // 'UCTH' little-endian
    constexpr uint32_t kFormatVersion = 1;
    constexpr uint32_t kEndianTag = 0x01020304;

    struct Header {
        uint32_t magic = kMagic;
        uint32_t version = kFormatVersion;
        uint32_t endian = kEndianTag;
        uint32_t blobSize = 0;
        // What the thumbnail was made from. A source that has changed size or
        // modification time makes the entry stale — that is the whole
        // freshness rule, and it is the file's own, not a timer.
        uint64_t sourceSize = 0;
        int64_t  sourceTime = 0;
        // The request, repeated. The file name is a hash of exactly these,
        // so this is the collision guard: two different requests that hash
        // alike produce a miss instead of each other's picture.
        uint64_t pathHash = 0;
        int32_t  width = 0;
        int32_t  height = 0;
        int32_t  fit = 0;
        int32_t  scaleQ = 0;
    };

    uint64_t HashText(const std::string& text, uint64_t seed) {
        uint64_t hash = seed;
        for (unsigned char c : text) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ull;            // FNV-1a, 64 bit
        }
        return hash;
    }

    // The scale as an integer, the same quantisation the in-memory slot key
    // uses (UltraCanvasFilerWidget::ThumbSlotKey) so the two caches agree on
    // what counts as the same request.
    int32_t Quantise(float scale) {
        return static_cast<int32_t>(scale * 100.0f);
    }

    uint64_t HashRequest(const Request& request) {
        uint64_t hash = HashText(request.sourcePath, 1469598103934665603ull);
        hash = HashText("|" + std::to_string(request.width)
                            + "x" + std::to_string(request.height)
                            + "|" + std::to_string(request.fit)
                            + "|" + std::to_string(Quantise(request.scale)),
                        hash);
        return hash;
    }

    std::string HexOf(uint64_t value) {
        char buffer[17] = {};
        std::snprintf(buffer, sizeof buffer, "%016llx",
                      static_cast<unsigned long long>(value));
        return buffer;
    }

    // ===== STATE =====
    // A mutex around the directory and the enabled flag only. The file
    // operations themselves are not serialised: several thumbnail workers
    // read and write different files at once, which is the point of having
    // four of them, and the rename-into-place below is what makes concurrent
    // writes of the SAME file safe.
    std::mutex g_mutex;
    std::string g_directoryOverride;
    std::atomic<bool> g_enabled{true};
    std::atomic<bool> g_swept{false};

    std::string ResolveDirectory() {
        std::string override;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            override = g_directoryOverride;
        }
        if (!override.empty()) {
            std::error_code ec;
            fs::create_directories(PathFromUtf8(override), ec);
            return ec ? std::string() : override;
        }
        return DiskCache::Directory(kDirectoryName);
    }

    std::string FilePathFor(const std::string& directory, const Request& request) {
        return directory + "/" + HexOf(HashRequest(request)) + ".ucth";
    }

    // Size and modification time of the file the thumbnail is of. Both zero
    // when it cannot be read, which never matches a stored header — an
    // unreadable source is always a miss, never a stale hit.
    bool DescribeSource(const std::string& path, uint64_t& size, int64_t& time) {
        std::error_code ec;
        const fs::path file = PathFromUtf8(path);
        const auto bytes = fs::file_size(file, ec);
        if (ec) return false;
        const auto written = fs::last_write_time(file, ec);
        if (ec) return false;
        size = static_cast<uint64_t>(bytes);
        time = static_cast<int64_t>(written.time_since_epoch().count());
        return true;
    }

    bool ValidHeader(const Header& header, const Request& request,
                     uint64_t sourceSize, int64_t sourceTime) {
        return header.magic == kMagic
            && header.version == kFormatVersion
            && header.endian == kEndianTag
            && header.blobSize > 0
            && header.sourceSize == sourceSize
            && header.sourceTime == sourceTime
            && header.pathHash == HashText(request.sourcePath,
                                           1469598103934665603ull)
            && header.width == request.width
            && header.height == request.height
            && header.fit == request.fit
            && header.scaleQ == Quantise(request.scale);
    }

    // A file handle that closes itself however the function leaves. The C
    // stdio calls are deliberate: this runs on the thumbnail workers, where a
    // read is a few dozen kilobytes and an iostream's buffering and locale
    // machinery is all cost and no benefit.
    struct FileHandle {
        std::FILE* file = nullptr;
        explicit FileHandle(std::FILE* f) : file(f) {}
        ~FileHandle() { Close(); }
        FileHandle(const FileHandle&) = delete;
        FileHandle& operator=(const FileHandle&) = delete;
        explicit operator bool() const { return file != nullptr; }
        // Closing early, by name rather than by destructor, because what
        // follows is a remove() or a rename() of the same file — and Windows
        // refuses both while a handle is open.
        void Close() {
            if (file) { std::fclose(file); file = nullptr; }
        }
    };

    std::FILE* OpenFile(const std::string& path, const char* mode) {
#if defined(_WIN32) || defined(_WIN64)
        // The path may hold characters the active code page cannot express;
        // go through the wide form, which is what PathFromUtf8 produces.
        const std::wstring wide = PathFromUtf8(path).wstring();
        const std::wstring wideMode(mode, mode + std::strlen(mode));
        return _wfopen(wide.c_str(), wideMode.c_str());
#else
        return std::fopen(path.c_str(), mode);
#endif
    }

} // namespace

bool IsEnabled() { return g_enabled.load(); }

void SetEnabled(bool enabled) { g_enabled.store(enabled); }

std::string Directory() {
    if (!g_enabled.load()) return {};
    return ResolveDirectory();
}

void SetDirectoryOverride(const std::string& directory) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_directoryOverride = directory;
    g_swept.store(false);
}

bool IsAvailable() {
    return g_enabled.load() && !ResolveDirectory().empty();
}

std::vector<uint8_t> Load(const Request& request) {
    if (!g_enabled.load() || request.sourcePath.empty()) return {};
    if (request.width <= 0 || request.height <= 0) return {};
    const std::string directory = ResolveDirectory();
    if (directory.empty()) return {};

    uint64_t sourceSize = 0;
    int64_t sourceTime = 0;
    if (!DescribeSource(request.sourcePath, sourceSize, sourceTime)) return {};

    const std::string path = FilePathFor(directory, request);
    FileHandle handle(OpenFile(path, "rb"));
    if (!handle) return {};

    Header header;
    if (std::fread(&header, sizeof header, 1, handle.file) != 1) return {};
    if (!ValidHeader(header, request, sourceSize, sourceTime)) {
        // Either the source changed under it or the file is from another
        // build. Both mean this entry can never be a hit again, so it goes
        // now rather than waiting two weeks to be swept: the same tile is
        // about to write its replacement.
        handle.Close();
        std::error_code ec;
        fs::remove(PathFromUtf8(path), ec);
        return {};
    }

    std::vector<uint8_t> blob(header.blobSize);
    if (std::fread(blob.data(), 1, blob.size(), handle.file) != blob.size()) {
        return {};   // truncated: treated as a miss, rewritten by the caller
    }

    // Served today. DiskCache::Touch writes at most once a day per file, so
    // scrolling a folder of a thousand pictures costs no disk writes at all
    // after the first.
    DiskCache::Touch(path);
    return blob;
}

bool Store(const Request& request, const std::vector<uint8_t>& blob) {
    if (!g_enabled.load() || blob.empty() || request.sourcePath.empty()) return false;
    if (request.width <= 0 || request.height <= 0) return false;
    if (blob.size() > 0xFFFFFFFFull) return false;
    const std::string directory = ResolveDirectory();
    if (directory.empty()) return false;

    uint64_t sourceSize = 0;
    int64_t sourceTime = 0;
    if (!DescribeSource(request.sourcePath, sourceSize, sourceTime)) return false;

    Header header;
    header.blobSize = static_cast<uint32_t>(blob.size());
    header.sourceSize = sourceSize;
    header.sourceTime = sourceTime;
    header.pathHash = HashText(request.sourcePath, 1469598103934665603ull);
    header.width = request.width;
    header.height = request.height;
    header.fit = request.fit;
    header.scaleQ = Quantise(request.scale);

    const std::string path = FilePathFor(directory, request);
    // Write beside the target and rename: four workers (and two copies of the
    // application) can be thumbnailing the same folder, and a reader must
    // never see a half-written entry. The process id keeps two writers from
    // sharing one temporary.
    const std::string temp = path + "."
#if defined(_WIN32) || defined(_WIN64)
            + std::to_string(static_cast<unsigned long>(::GetCurrentProcessId()))
#else
            + std::to_string(static_cast<unsigned long>(::getpid()))
#endif
            + ".tmp";

    {
        FileHandle handle(OpenFile(temp, "wb"));
        if (!handle) return false;
        if (std::fwrite(&header, sizeof header, 1, handle.file) != 1 ||
            std::fwrite(blob.data(), 1, blob.size(), handle.file) != blob.size()) {
            handle.Close();
            std::error_code ec;
            fs::remove(PathFromUtf8(temp), ec);
            return false;
        }
    }

    std::error_code ec;
    fs::rename(PathFromUtf8(temp), PathFromUtf8(path), ec);
    if (ec) {
        // Windows will not rename onto an existing file that someone has
        // open; the entry already there is as good as this one, so drop the
        // temporary and call it done.
        fs::remove(PathFromUtf8(temp), ec);
        return false;
    }
    return true;
}

void SweepOnce() {
    if (!g_enabled.load()) return;
    if (g_swept.exchange(true)) return;
    const std::string directory = ResolveDirectory();
    if (directory.empty()) return;
    DiskCache::Sweep(directory, CacheExtensions());
}

DiskCache::Usage GetUsage() {
    const std::string directory = ResolveDirectory();
    if (directory.empty()) return {};
    return DiskCache::Measure(directory, CacheExtensions());
}

size_t Clear() {
    const std::string directory = ResolveDirectory();
    if (directory.empty()) return 0;
    return DiskCache::Clear(directory, CacheExtensions());
}

} // namespace ThumbnailDiskCache
} // namespace UltraCanvas
