// core/UltraCanvasRemovableDrives.cpp
// The shared half of UltraCanvasRemovableDrives: the order of the list, the
// argument check, and the fallback backend for platforms with none. The
// platform halves are OS/Linux/UltraCanvasLinuxRemovableDrives.cpp,
// OS/MSWindows/UltraCanvasWindowsRemovableDrives.cpp and
// OS/MacOS/UltraCanvasMacOSRemovableDrives.mm.
// Version: 1.0.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework

#include "UltraCanvasRemovableDrivesBackend.h"

#include <algorithm>
#include <cctype>

namespace UltraCanvas {

std::string UltraCanvasRemovableDrives::GetBackendName() {
    return RemovableDrivesBackend::BackendName();
}

std::vector<RemovableDrive> UltraCanvasRemovableDrives::List(std::string* error) {
    std::string reason;
    std::vector<RemovableDrive> drives = RemovableDrivesBackend::List(reason);
    for (RemovableDrive& drive : drives) {
        if (drive.name.empty()) drive.name = !drive.labels.empty() && !drive.labels.front().empty()
                                                 ? drive.labels.front()
                                                 : "Removable drive";
        drive.labels.resize(drive.mountPoints.size());
    }
    std::stable_sort(drives.begin(), drives.end(), [](const RemovableDrive& a, const RemovableDrive& b) {
        auto lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        };
        return lower(a.name) < lower(b.name);
    });
    if (error) *error = reason;
    return drives;
}

bool UltraCanvasRemovableDrives::SafelyRemove(const std::string& id, std::string* error) {
    if (id.empty()) {
        if (error) *error = "No drive was named.";
        return false;
    }
    std::string reason;
    if (RemovableDrivesBackend::SafelyRemove(id, reason)) return true;
    if (error) *error = reason.empty() ? "The drive could not be removed safely." : reason;
    return false;
}

// ===== FALLBACK BACKEND =====

#ifndef ULTRACANVAS_REMOVABLEDRIVES_NATIVE
namespace RemovableDrivesBackend {
    std::string BackendName() { return "null"; }
    std::vector<RemovableDrive> List(std::string& error) {
        error = "Removable drives are not listed on this platform yet.";
        return {};
    }
    bool SafelyRemove(const std::string&, std::string& error) {
        error = "Removable drives are not removed on this platform yet.";
        return false;
    }
} // namespace RemovableDrivesBackend
#endif

} // namespace UltraCanvas
