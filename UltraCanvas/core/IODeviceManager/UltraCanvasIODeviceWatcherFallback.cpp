// core/IODeviceManager/UltraCanvasIODeviceWatcherFallback.cpp
// The hot-plug watcher for platforms that do not have one yet.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

// Compiled only when no platform watcher is. Keeping the fallback in its own
// translation unit behind the inverse guard means exactly one definition of
// CreateDeviceWatcher() exists in any build, without the platform files
// having to know about each other.
#if !(defined(__linux__) && defined(ULTRACANVAS_HAS_UDEV))

#include "UltraCanvasIODeviceBackends.h"

namespace UltraCanvas {
namespace Internal {

// Null rather than a watcher that never fires: StartMonitoring() then reports
// BackendUnavailable, so a caller can tell "this platform cannot watch" from
// "nothing has been plugged in yet".
IDeviceWatcherPtr CreateDeviceWatcher() {
    return nullptr;
}

}  // namespace Internal
}  // namespace UltraCanvas

#endif
