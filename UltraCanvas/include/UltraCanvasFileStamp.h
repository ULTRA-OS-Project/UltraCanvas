// include/UltraCanvasFileStamp.h
// The size and modification time of a file: the cheap fingerprint that tells
// whether a file changed since something was made from it.
//
// Every cache of something read from a file needs the same answer - the image
// cache (UCImage::GetFresh), the thumbnail disk cache (ThumbnailDiskCache),
// the watch that keeps an album's pictures current (UltraCanvasImageFileWatch),
// UltraFiler's preview pane - and each had grown its own copy of these few
// lines. A file saved over gets a new modification time (and usually a new
// size), so comparing the two is how they all decide; the content is never
// read for it.
//
// Header-only and C++17, like UltraCanvasPathUtf8.h, so headless code can use
// it without linking anything.
// Version: 1.0.0
// Last Modified: 2026-10-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasPathUtf8.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace UltraCanvas {

    struct FileStamp {
        uint64_t size = 0;
        int64_t  time = 0;        // last_write_time, in the filesystem clock's ticks
        bool     valid = false;   // false: the file could not be examined

        bool operator==(const FileStamp& other) const {
            return valid == other.valid && size == other.size && time == other.time;
        }
        bool operator!=(const FileStamp& other) const { return !(*this == other); }
    };

    // The file at `path` (UTF-8) as it is now. One stat; never call it on a
    // paint path. A file that is gone, or that cannot be examined, gives an
    // invalid stamp - which differs from any stamp of a file that was there.
    inline FileStamp StampFile(const std::string& path) {
        FileStamp stamp;
        if (path.empty()) return stamp;
        std::error_code ec;
        const std::filesystem::path file = PathFromUtf8(path);
        const auto bytes = std::filesystem::file_size(file, ec);
        if (ec) return stamp;
        const auto written = std::filesystem::last_write_time(file, ec);
        if (ec) return stamp;
        stamp.size = static_cast<uint64_t>(bytes);
        stamp.time = static_cast<int64_t>(written.time_since_epoch().count());
        stamp.valid = true;
        return stamp;
    }

} // namespace UltraCanvas
