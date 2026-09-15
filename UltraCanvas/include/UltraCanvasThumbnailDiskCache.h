// include/UltraCanvasThumbnailDiskCache.h
// Thumbnails that survive the process that made them.
//
// The Filer's thumbnail cache (UltraCanvasFilerWidget) is memory: bounded,
// evicted least-recently-drawn first, and gone the moment the application
// closes. That is the right shape for what is on screen and the wrong shape
// for what it cost — a folder of photos, videos or PDFs is minutes of decode
// work, redone in full every launch, and redone again after any browsing wide
// enough to push the folder out of the memory budget.
//
// So a finished thumbnail is also written here, as a file under
// %LOCALAPPDATA%\UltraCanvas\thumbnails (…/Library/Caches/… on macOS,
// $XDG_CACHE_HOME/… elsewhere; see UltraCanvasDiskCache.h for the root). The
// memory cache asks this one before it queues a decode, so the second launch
// draws the folder from disk in milliseconds.
//
// What is stored, and what is not:
//
//   - Content previews only: photos, video poster frames, document and model
//     renders, font specimens. Application icons are NOT stored — the shell
//     extracts one in well under the time it takes to open a file, and an
//     icon that changed because the program was upgraded must never be served
//     from yesterday.
//   - Blobs are QOI (QoiPixmapCodec), the same compression the in-memory
//     "compressed thumbnails" option uses: a few milliseconds to write, under
//     a millisecond to read back, and roughly a quarter the size of raw
//     ARGB32.
//
// Staleness is decided by the source file, not by the cache: every entry
// records the size and modification time of the file it was made from, and a
// mismatch is a miss (and deletes the entry). Editing a picture therefore
// shows the edit, and a cache file cannot outlive the meaning of its key.
//
// Retention is UltraCanvasDiskCache's: an entry the Filer serves is stamped
// with the day it was served, and entries not served for two weeks are swept
// at startup. A folder the user visits keeps its thumbnails indefinitely; one
// they visited once pays for itself and then goes away.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasDiskCache.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {
    namespace ThumbnailDiskCache {

        // Everything about one wanted thumbnail: which file, at what size.
        // The geometry is part of the identity — a 64px and a 256px thumbnail
        // of one picture are two entries — because that is how the memory
        // cache keys them and the two must agree or every tile misses.
        struct Request {
            std::string sourcePath;   // the file the thumbnail is OF
            int width = 0;
            int height = 0;
            int fit = 0;              // ImageFitMode, as an int: no image
                                      // headers wanted here
            float scale = 1.0f;       // HiDPI device scale
        };

        // Is the cache usable at all? False when the platform offered nowhere
        // writable, or when the application switched it off. Every call below
        // is a safe no-op in that case.
        bool IsAvailable();

        // Off switches the whole thing: nothing is read, nothing is written.
        // The files already on disk are left alone (and expire on their own),
        // so switching it back on costs nothing. On by default.
        void SetEnabled(bool enabled);
        bool IsEnabled();

        // Where the files live. Empty when unavailable. For a settings page
        // that wants to show the location, and for the tests.
        std::string Directory();

        // Point the cache at another directory — for tests, which must not
        // write into the user's real cache. Drops the remembered sweep state.
        // Pass an empty string to go back to the platform location.
        void SetDirectoryOverride(const std::string& directory);

        // The QOI blob stored for `request`, or empty on a miss. A hit stamps
        // the file as served today (at most one write per file per day). An
        // entry whose source has changed size or modification time since is
        // not a hit: it is deleted and reported as a miss.
        std::vector<uint8_t> Load(const Request& request);

        // Store `blob` for `request`, recording the source's size and
        // modification time alongside it. Written to a temporary name and
        // renamed into place, so two processes thumbnailing the same folder
        // at once cannot leave a half-written file behind. Silently does
        // nothing when the cache is unavailable or the blob is empty.
        bool Store(const Request& request, const std::vector<uint8_t>& blob);

        // Delete everything not served within DiskCache::kDefaultMaxAge. Call
        // once per process before the first Load; calling it again is a cheap
        // no-op, so it is safe to put at the top of any entry point.
        void SweepOnce();

        // Bytes and files currently held. For a settings page, and for tests.
        DiskCache::Usage GetUsage();

        // Throw the whole cache away now — what a "clear thumbnail cache"
        // button calls. Returns how many files went.
        size_t Clear();

    } // namespace ThumbnailDiskCache
} // namespace UltraCanvas
