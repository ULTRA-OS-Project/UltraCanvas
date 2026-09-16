// include/UltraCanvasDiskCache.h
// Retention policy for the caches UltraCanvas keeps as files on disk.
//
// Two of them exist: the handler icons the "Open with" menu draws
// (%LOCALAPPDATA%\UltraCanvas\openwith-icons, see
// UltraCanvasFileAssociationsBackend.h) and the file thumbnails the Filer
// shows (…\UltraCanvas\thumbnails, see UltraCanvasThumbnailDiskCache.h).
// Both share one problem and therefore one answer.
//
// The problem is that a disk cache is keyed by where its content came from —
// an executable's path, a picture's path and tile size — and the user is free
// to upgrade, move, rename or delete that source at any time. Nothing tells
// the cache. So every such change orphans a file that nothing will ever ask
// for again, and a cache with no expiry grows for the life of the account.
//
// The answer is a "last served" stamp and a sweep:
//
//   - Every hit calls Touch(), which writes the current time as the file's
//     modification time. The modification time is used as the stamp because
//     it is the only timestamp worth trusting: Windows stopped maintaining
//     last-access times by default with Vista, and network and removable
//     filesystems vary. Touch() rewrites at most once a day, which is all
//     the resolution a two-week window needs — so a folder redrawn all
//     afternoon costs no disk writes at all.
//
//   - Sweep() deletes everything not served within kDefaultMaxAge, plus any
//     leftover .tmp from a write that was interrupted. Called once per
//     process, before the first lookup. A swept file that turns out to still
//     be wanted is simply produced again.
//
// The policy lives here, once, rather than in each cache: two caches that
// expire on different rules are two behaviours to explain, and the one that
// was written second is the one that gets it wrong.
//
// Everything here is plain std::filesystem and getenv — no platform API — so
// it compiles on every platform whether or not that platform has a backend
// that uses it.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {
    namespace DiskCache {

        // How long a cached file survives without being served.
        constexpr auto kDefaultMaxAge = std::chrono::hours(24 * 14);

        // How stale a stamp gets before a hit rewrites it.
        constexpr auto kDefaultStampInterval = std::chrono::hours(24);

        // The per-user cache root UltraCanvas keeps its files under:
        // %LOCALAPPDATA%\UltraCanvas on Windows (TEMP/TMP for the rare account
        // without one), ~/Library/Caches/UltraCanvas on macOS, and
        // $XDG_CACHE_HOME/UltraCanvas — else ~/.cache/UltraCanvas — elsewhere.
        // Empty when the platform offers nowhere writable, which every caller
        // must treat as "this cache is switched off" rather than as an error:
        // a disk cache is an optimisation, never a requirement.
        std::string Root();

        // Root() + "/" + name, created on demand. Empty when there is no root
        // or the directory cannot be created. The answer is computed once per
        // name and remembered, so this is cheap to call per lookup.
        std::string Directory(const std::string& name);

        // Record that `file` is still in use — call it on every cache hit.
        // Rewrites the stamp only once it has gone `minInterval` stale;
        // returns true when it actually wrote. A read-only cache directory
        // makes this fail silently, which is harmless: the file is swept
        // earlier than it would otherwise have been, and produced again.
        bool Touch(const std::string& file,
                   std::chrono::seconds minInterval =
                           std::chrono::duration_cast<std::chrono::seconds>(
                                   kDefaultStampInterval));

        // Delete every file in `directory` whose extension is in `extensions`
        // and which has not been served within `maxAge`. Returns how many
        // were deleted. Files with any other extension belong to someone else
        // and are never touched; subdirectories are not descended into.
        //
        // A stamp in the future — a clock that was set back, a file copied
        // from another machine — reads as infinitely fresh. That is the safe
        // way round: the cost of keeping a file too long is disk space, the
        // cost of deleting a live one is work redone.
        size_t Sweep(const std::string& directory,
                     const std::vector<std::string>& extensions,
                     std::chrono::seconds maxAge =
                             std::chrono::duration_cast<std::chrono::seconds>(
                                     kDefaultMaxAge));

        // What a cache directory currently occupies. For a settings page that
        // wants to show it, and for tests.
        struct Usage {
            size_t files = 0;
            uint64_t bytes = 0;
        };
        Usage Measure(const std::string& directory,
                      const std::vector<std::string>& extensions);

        // Delete every file in `directory` with one of `extensions`,
        // whatever its age. What a "clear the cache now" button calls.
        size_t Clear(const std::string& directory,
                     const std::vector<std::string>& extensions);

    } // namespace DiskCache
} // namespace UltraCanvas
