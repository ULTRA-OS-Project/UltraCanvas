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
// "Made from" is the file as it was before the thumbnail was drawn: the
// caller stamps the source ahead of the decode and Store() refuses an entry
// whose source has moved on since. Stamping it when the entry was written
// instead let a thumbnail of the old content - decoded while the file was
// being saved over, or served by an in-memory image cache that had not
// noticed the save - be recorded as the answer for the new file, and every
// later run showed the old picture for good.
//
// Retention is UltraCanvasDiskCache's: an entry the Filer serves is stamped
// with the day it was served, and entries not served for two weeks are swept
// at startup. A folder the user visits keeps its thumbnails indefinitely; one
// they visited once pays for itself and then goes away.
//
// The source file is not the only thing a thumbnail depends on: the code that
// drew it matters as much. Every entry therefore also records the renderer
// generation (kRendererGeneration) of the build that wrote it, and an entry
// from another generation is a miss. Without it a renderer fix never reached
// a thumbnail already on disk - the vector previews drawn "at the fit
// squared" before 2026-09-26 stayed specks in a corner for as long as the
// tile kept being shown, at exactly the sizes cached back then.
// Version: 1.2.0
// Last Modified: 2026-10-09
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

        // Which renderers made the thumbnails: bump it in any change that makes
        // a thumbnail producer draw something different for the same file at
        // the same size - a renderer fix, a new producer taking over a format,
        // a change of fit or background. Every entry written by an earlier
        // generation then misses (and is deleted when it is next asked for),
        // so the fix shows on the first look instead of never.
        //   1  until 2026-10-03 (implicit: entries carried no generation)
        //   2  vector previews drawn at the fit, not the fit squared
        //   3  entries were stamped when stored rather than before the
        //      decode, and the image cache served a file's previous content
        //      after it was saved over - so an older entry may be the old
        //      picture recorded as the answer for the new file
        constexpr uint32_t kRendererGeneration = 3;

        // The generation Load() and Store() use - kRendererGeneration unless
        // a test overrode it. Pass 0 to go back to the built-in one.
        uint32_t RendererGeneration();
        void SetRendererGenerationOverride(uint32_t generation);

        // Is the cache usable at all? False when the platform offered nowhere
        // writable, or when the application switched it off. Every call below
        // is a safe no-op in that case.
        bool IsAvailable();

        // Off switches the whole thing: nothing is read, nothing is written.
        // The files already on disk are left alone (and expire on their own),
        // so switching it back on costs nothing. On by default.
        void SetEnabled(bool enabled);
        bool IsEnabled();

        // Where the files live, whether or not the cache is switched on -
        // switching it off does not move them, and a settings page shows the
        // location either way. Empty only when the platform offered nowhere
        // writable.
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

        // The size and modification time of a source file: what an entry
        // records, and what decides whether it is still true. `valid` is
        // false when the file cannot be examined.
        struct SourceStamp {
            uint64_t size = 0;
            int64_t  time = 0;
            bool     valid = false;
            bool operator==(const SourceStamp&) const = default;
        };
        SourceStamp StampSource(const std::string& path);

        // Store `blob` for `request`. `madeFrom` is the source as it was
        // BEFORE the thumbnail was drawn - StampSource() taken ahead of the
        // decode - and is what the entry records. When the source no longer
        // matches it, nothing is stored: the file changed while the thumbnail
        // was being made, so the picture may show the old content, and
        // stamping it with the new file's size and time (which is what
        // stamping it at the time of storing did) made that old picture the
        // valid answer for the new file on every later run.
        //
        // Written to a temporary name and renamed into place, so two
        // processes thumbnailing the same folder at once cannot leave a
        // half-written file behind. Silently does nothing when the cache is
        // unavailable or the blob is empty.
        bool Store(const Request& request, const std::vector<uint8_t>& blob,
                   const SourceStamp& madeFrom);

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
