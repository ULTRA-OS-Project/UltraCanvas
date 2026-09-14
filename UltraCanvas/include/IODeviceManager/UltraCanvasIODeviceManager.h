// include/IODeviceManager/UltraCanvasIODeviceManager.h
// Single entry point for discovering and holding I/O devices. Owns the
// device registry; the devices themselves are created by per-backend
// enumerators registered into the manager.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODevice.h"
#include "UltraCanvasIODeviceTypes.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// ENUMERATORS
// ============================================================================
//
// One enumerator discovers the devices of one category reachable through one
// backend, and returns them constructed but not connected. Several
// enumerators may serve the same category — on Linux the Camera category has
// both a V4L2 enumerator (webcams) and a gphoto2 one (DSLRs) — and the
// manager runs all of them and merges the results.
//
// This is deliberately not a set of EnumerateScanners()/EnumerateCameras()
// methods on the manager: with one method per category, each platform
// backend has to define the same symbol, so two backends serving one
// category on one platform collide at link time and only one set of devices
// is ever found.
//
using IODeviceEnumerator = std::function<std::vector<IODevicePtr>()>;

// ============================================================================
// HOT-PLUG NOTIFICATION
// ============================================================================

enum class IODeviceChange {
    Added,
    Removed,
    StateChanged
};

using IODeviceChangeCallback =
    std::function<void(IODeviceChange change, const IODeviceInfo& info)>;

// ============================================================================
// IODeviceManager
// ============================================================================

class IODeviceManager {
public:
    static IODeviceManager& GetInstance();

    IODeviceManager(const IODeviceManager&) = delete;
    IODeviceManager& operator=(const IODeviceManager&) = delete;

    // ===== LIFECYCLE =====

    // Registers the backends this build was compiled with. Idempotent.
    IODeviceResult Initialize();

    // Disconnects and drops every registered device, then clears the
    // enumerators. Idempotent; safe to call without a prior Initialize().
    void Shutdown();

    bool IsInitialized() const;

    // ===== BACKEND REGISTRATION =====

    // `backendName` labels the source of the devices ("SANE", "V4L2",
    // "CUPS", "GutenPrint", ...) and must be unique per category.
    // Registering an existing (category, backendName) pair replaces it.
    void RegisterEnumerator(IODeviceCategory category,
                            const std::string& backendName,
                            IODeviceEnumerator enumerator);

    void UnregisterEnumerator(IODeviceCategory category,
                              const std::string& backendName);

    // Backend names registered for `category`, in registration order.
    std::vector<std::string> GetRegisteredBackends(IODeviceCategory category) const;

    // ===== DISCOVERY =====

    // Runs every enumerator for `category` and merges the results into the
    // registry. Devices already registered keep their identity (and any open
    // session); devices that disappeared are removed. Returns the number of
    // devices now registered in that category, or an error if no enumerator
    // is registered for it.
    IODeviceResult EnumerateDevices(IODeviceCategory category);

    // As above for every category that has an enumerator.
    IODeviceResult EnumerateAllDevices();

    // ===== REGISTRY =====

    // Adds a device the manager did not enumerate — a test double, or
    // hardware reached through an application's own backend. The manager
    // takes shared ownership. Fails on a duplicate device id.
    IODeviceResult RegisterDevice(const IODevicePtr& device);

    // Disconnects and drops the device. Succeeds even if it is unknown.
    void UnregisterDevice(const IODeviceId& deviceId);

    std::vector<IODevicePtr> GetDevices() const;
    std::vector<IODevicePtr> GetDevices(IODeviceCategory category) const;
    std::vector<IODeviceInfo> GetDeviceInfos(IODeviceCategory category) const;

    // Null when no device carries that id.
    IODevicePtr GetDeviceById(const IODeviceId& deviceId) const;

    // Index is into GetDevices(category); null when out of range. Provided
    // for the common "just give me the first scanner" case.
    IODevicePtr GetDevice(IODeviceCategory category, size_t index) const;

    size_t GetDeviceCount() const;
    size_t GetDeviceCount(IODeviceCategory category) const;

    // ===== NOTIFICATION =====

    // Fires when a device is added or removed by enumeration, or when a
    // backend reports a state change. Callbacks run on the thread that
    // caused the change — do not block in them.
    void SetDeviceChangeCallback(IODeviceChangeCallback callback);

private:
    IODeviceManager() = default;
    ~IODeviceManager();

    struct EnumeratorEntry {
        IODeviceCategory category = IODeviceCategory::Unknown;
        std::string backendName;
        IODeviceEnumerator enumerate;
    };

    struct PendingChange {
        IODeviceChange change = IODeviceChange::Added;
        IODeviceInfo info;
    };

    // Fires the change callback for each entry. Always called with
    // registryMutex released, so a callback may re-enter the manager.
    void Notify(const std::vector<PendingChange>& changes);

    mutable std::mutex registryMutex;
    std::vector<EnumeratorEntry> enumerators;
    std::vector<IODevicePtr> devices;
    IODeviceChangeCallback changeCallback;
    bool initialized = false;
};

} // namespace UltraCanvas
