// include/UltraCanvasRemovableDrivesBackend.h
// Internal contract between UltraCanvasRemovableDrives and its per-platform
// backend. Applications include UltraCanvasRemovableDrives.h instead. The
// core sorts what List returns and fills a missing reason.
// Version: 1.0.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASREMOVABLEDRIVESBACKEND_H
#define ULTRACANVASREMOVABLEDRIVESBACKEND_H

#include "UltraCanvasRemovableDrives.h"

// A platform has a native backend when one of the OS/<Platform> files is
// compiled into the build; everything else links the fallback in the core.
#if defined(__APPLE__)
    #include <TargetConditionals.h>
#endif
#if (defined(__linux__) && !defined(__ANDROID__)) || defined(_WIN32) || (defined(__APPLE__) && TARGET_OS_OSX)
    #define ULTRACANVAS_REMOVABLEDRIVES_NATIVE 1
#endif

namespace UltraCanvas {
namespace RemovableDrivesBackend {

    std::string BackendName();
    std::vector<RemovableDrive> List(std::string& error);
    bool SafelyRemove(const std::string& id, std::string& error);

} // namespace RemovableDrivesBackend
} // namespace UltraCanvas

#endif // ULTRACANVASREMOVABLEDRIVESBACKEND_H
