// include/UltraCanvasFileAssociationsBackend.h
// INTERNAL backend contract of UltraCanvasFileAssociations — implemented once
// per platform (OS/Linux/UltraCanvasLinuxFileAssociations.cpp, …) and called
// only by core/UltraCanvasFileAssociations.cpp, which owns the worker thread
// and the cache. Application code uses UltraCanvasFileAssociations.h instead.
// All functions here may block on filesystem I/O; the core keeps them off the
// UI thread. They are called under the core's backend mutex — never from two
// threads at once — so implementations need no locking of their own.
// Version: 1.0.0
// Last Modified: 2026-08-16
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasFileAssociations.h"

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

    } // namespace FileAssociationsBackend
} // namespace UltraCanvas
