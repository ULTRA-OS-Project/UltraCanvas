// include/UltraCanvasFileAssociationsBackend.h
// INTERNAL backend contract of UltraCanvasFileAssociations — implemented once
// per platform (OS/Linux/UltraCanvasLinuxFileAssociations.cpp, …) and called
// only by core/UltraCanvasFileAssociations.cpp, which owns the worker thread
// and the cache. Application code uses UltraCanvasFileAssociations.h instead.
// All functions here may block on filesystem I/O; the core keeps them off the
// UI thread. They are called under the core's backend mutex — never from two
// threads at once — so implementations need no locking of their own.
// Version: 1.2.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasDiskCache.h"

#include <chrono>
#include <string>
#include <vector>

namespace UltraCanvas {
    namespace FileAssociationsBackend {

        // Parse (or re-check) the platform association database. Idempotent
        // and cheap when nothing changed on disk. Returns true when the index
        // was (re)built — the core then drops its per-extension cache.
        bool RefreshGlobalIndex();

        // Ordered candidates for one representative file name (matched by its
        // extension / full name), default application first and flagged.
        std::vector<FileAssociationApp> ResolveFile(const std::string& fileName);

        // The freedesktop MIME name of one file name, or "" where the
        // platform keeps no MIME database — see FileAssociations::GetMimeType
        // for what that means and who asks. `fileName` is a bare file name,
        // never a path: the freedesktop globs match on the name alone, and a
        // directory component with a dot in it would otherwise be read as a
        // suffix.
        std::string MimeTypeFor(const std::string& fileName);

        // The generic icon name the type database gives a MIME type, or ""
        // when it names none (or the platform has no such database). See
        // FileAssociations::GetMimeGenericIcon.
        std::string MimeGenericIconFor(const std::string& mime);

        // Launchers. All detach the started application from this process.
        bool LaunchDefault(const std::vector<std::string>& paths,
                           std::string& outError);
        bool LaunchWith(const FileAssociationApp& app,
                        const std::vector<std::string>& paths,
                        std::string& outError);
        bool LaunchWithPath(const std::string& applicationPath,
                            const std::vector<std::string>& paths,
                            std::string& outError);

        FileAssociations::ApplicationFilter GetApplicationFilter();
        std::string GetApplicationsDirectory();

        // ===== ON-DISK ICON CACHE =====
        // The Windows and macOS backends extract each handler's icon into a
        // PNG file under a per-user cache directory, because the shared menu
        // API draws image files rather than pixmaps. Those files outlive the
        // process and are keyed by where the icon came from, so upgrading or
        // uninstalling an application orphans its PNG for good: nothing will
        // ever ask for that key again, and without a sweep the directory
        // grows for the life of the account.
        //
        // The two calls below are the whole retention policy, kept here
        // instead of once per backend so both platforms expire on the same
        // rule. Implemented in core/UltraCanvasFileAssociations.cpp on top of
        // UltraCanvasDiskCache, which is where the stamp-on-use and sweep
        // themselves live — the Filer's thumbnail cache expires on that same
        // code, so the two on-disk caches cannot drift apart.

        // How long a cached icon survives without being served. The shared
        // default: an icon and a thumbnail are orphaned by the same kinds of
        // event (a file moved, renamed, upgraded away) and there is no reason
        // for one to outlive the other.
        constexpr auto kIconCacheMaxAge = DiskCache::kDefaultMaxAge;

        // Record that `path` is still in use — call it on every cache hit.
        // The file's modification time is the "last served" stamp: Windows
        // stopped maintaining last-access times by default with Vista, so the
        // only timestamp worth trusting is the one we write. Rewritten at most
        // once a day per file, which is all the resolution a two-week window
        // needs, so a context menu that opens repeatedly costs no disk writes.
        void StampIconCacheFile(const std::string& path);

        // Delete everything in `directory` not served within kIconCacheMaxAge,
        // plus any leftover .tmp file from a write that was interrupted. Only
        // .png and .tmp are ever considered — anything else in there belongs
        // to someone else. Call once per process, before the first lookup.
        void SweepIconCache(const std::string& directory);

    } // namespace FileAssociationsBackend
} // namespace UltraCanvas
