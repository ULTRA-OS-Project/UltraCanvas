// include/NetworkMonitor/NetworkMonitorBackend.h
// Internal contract between the NetworkMonitor core and its per-platform
// backends, in the shape of IFolderWatchBackend / HardwareInfoBackend: an
// interface implemented under OS/<Platform>/, a factory that returns null
// where no backend exists, and a public surface (NetworkMonitor.h) that
// callers use instead of this. Applications include NetworkMonitor.h.
//
// Version: 0.1.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "NetworkMonitor/NetworkMonitor.h"

#include <memory>

// A platform has a native backend when one of the OS/<Platform> sources is
// compiled in. Everything else (WASM, Android, the BSDs, and Windows / macOS
// until their Phase 2 backends land) gets the null factory from the core
// file, and every NetworkMonitor_ListConnections() reports NotSupported.
#if defined(__linux__) && !defined(__ANDROID__)
    #define ULTRACANVAS_NETWORKMONITOR_NATIVE 1
#endif

namespace UltraCanvas {

class INetworkMonitorBackend {
public:
    virtual ~INetworkMonitorBackend() = default;

    // What this backend can deliver here, now, at this privilege. Cheap.
    virtual NetworkMonitorCapabilities Capabilities() const = 0;

    // One read of the socket table, unfiltered: the core applies the
    // options' filters so every backend filters identically. `out` is
    // replaced. Process attribution is done here when `resolveProcesses`
    // is set, because the join key (an inode, a PID) is platform-specific.
    virtual NetworkMonitorResult Snapshot(std::vector<NetworkConnection>& out,
                                          bool resolveProcesses) = 0;
};

// Provided by the platform backend where one exists; the core file supplies
// a null-returning definition on every other platform.
std::unique_ptr<INetworkMonitorBackend> CreateNativeNetworkMonitorBackend();

} // namespace UltraCanvas
