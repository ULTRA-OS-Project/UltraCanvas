// core/IODeviceManager/UltraCanvasIODeviceManager.cpp
// Device registry and backend dispatch. Platform-neutral: every backend
// reaches the manager through a registered enumerator, so this file never
// names a platform API.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "../../include/IODeviceManager/UltraCanvasIODeviceManager.h"
#include "UltraCanvasIODeviceBackends.h"

#include <algorithm>
#include <unordered_set>

namespace UltraCanvas {

// ============================================================================
// ENUM HELPERS
// ============================================================================

const char* IODeviceCategoryToString(IODeviceCategory category) {
    switch (category) {
        case IODeviceCategory::Scanner:        return "Scanner";
        case IODeviceCategory::Camera:         return "Camera";
        case IODeviceCategory::Printer:        return "Printer";
        case IODeviceCategory::Microphone:     return "Microphone";
        case IODeviceCategory::Speaker:        return "Speaker";
        case IODeviceCategory::Storage:        return "Storage";
        case IODeviceCategory::NetworkAdapter: return "NetworkAdapter";
        case IODeviceCategory::Serial:         return "Serial";
        case IODeviceCategory::Bluetooth:      return "Bluetooth";
        case IODeviceCategory::GPIO:           return "GPIO";
        case IODeviceCategory::Barcode:        return "Barcode";
        case IODeviceCategory::Biometric:      return "Biometric";
        case IODeviceCategory::Custom:         return "Custom";
        case IODeviceCategory::Unknown:        break;
    }
    return "Unknown";
}

const char* IODeviceTransportToString(IODeviceTransport transport) {
    switch (transport) {
        case IODeviceTransport::USB:       return "USB";
        case IODeviceTransport::Network:   return "Network";
        case IODeviceTransport::Bluetooth: return "Bluetooth";
        case IODeviceTransport::Serial:    return "Serial";
        case IODeviceTransport::Parallel:  return "Parallel";
        case IODeviceTransport::PCI:       return "PCI";
        case IODeviceTransport::Virtual:   return "Virtual";
        case IODeviceTransport::Unknown:   break;
    }
    return "Unknown";
}

const char* IODeviceStateToString(IODeviceState state) {
    switch (state) {
        case IODeviceState::Disconnected: return "Disconnected";
        case IODeviceState::Connecting:   return "Connecting";
        case IODeviceState::Connected:    return "Connected";
        case IODeviceState::Ready:        return "Ready";
        case IODeviceState::Busy:         return "Busy";
        case IODeviceState::Paused:       return "Paused";
        case IODeviceState::Error:        return "Error";
        case IODeviceState::Offline:      return "Offline";
        case IODeviceState::Unknown:      break;
    }
    return "Unknown";
}

const char* IODeviceResultCodeToString(IODeviceResultCode code) {
    switch (code) {
        case IODeviceResultCode::Success:            return "Success";
        case IODeviceResultCode::NotImplemented:     return "NotImplemented";
        case IODeviceResultCode::NotSupported:       return "NotSupported";
        case IODeviceResultCode::InvalidArgument:    return "InvalidArgument";
        case IODeviceResultCode::InvalidState:       return "InvalidState";
        case IODeviceResultCode::DeviceNotFound:     return "DeviceNotFound";
        case IODeviceResultCode::DeviceBusy:         return "DeviceBusy";
        case IODeviceResultCode::AccessDenied:       return "AccessDenied";
        case IODeviceResultCode::ConnectionFailed:   return "ConnectionFailed";
        case IODeviceResultCode::CommunicationError: return "CommunicationError";
        case IODeviceResultCode::Timeout:            return "Timeout";
        case IODeviceResultCode::Cancelled:          return "Cancelled";
        case IODeviceResultCode::OutOfMemory:        return "OutOfMemory";
        case IODeviceResultCode::BackendUnavailable: return "BackendUnavailable";
        case IODeviceResultCode::BackendError:       return "BackendError";
        case IODeviceResultCode::HardwareError:      return "HardwareError";
        case IODeviceResultCode::SupplyEmpty:        return "SupplyEmpty";
        case IODeviceResultCode::MediaError:         return "MediaError";
        case IODeviceResultCode::IOError:            return "IOError";
        case IODeviceResultCode::Unknown:            break;
    }
    return "Unknown";
}

// ============================================================================
// SINGLETON
// ============================================================================

IODeviceManager& IODeviceManager::GetInstance() {
    static IODeviceManager instance;
    return instance;
}

IODeviceManager::~IODeviceManager() {
    Shutdown();
}

// ============================================================================
// LIFECYCLE
// ============================================================================

IODeviceResult IODeviceManager::Initialize() {
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        if (initialized) {
            return IODeviceResult::Ok();
        }
        initialized = true;
    }

    // Registers whichever backends this build was compiled with. Each
    // backend gets its own registration entry point (see
    // UltraCanvasIODeviceBackends.h) rather than self-registering from a
    // static initialiser, because a static-library build drops the static
    // initialisers of object files nothing else references.
    Internal::RegisterCompiledBackends(*this);

    return IODeviceResult::Ok();
}

void IODeviceManager::Shutdown() {
    std::vector<IODevicePtr> toRelease;

    {
        std::lock_guard<std::mutex> lock(registryMutex);
        toRelease.swap(devices);
        enumerators.clear();
        changeCallback = nullptr;
        initialized = false;
    }

    // Disconnect outside the lock: a backend teardown may join threads that
    // call back into the manager.
    for (const auto& device : toRelease) {
        if (device) {
            device->Disconnect();
        }
    }
}

bool IODeviceManager::IsInitialized() const {
    std::lock_guard<std::mutex> lock(registryMutex);
    return initialized;
}

// ============================================================================
// BACKEND REGISTRATION
// ============================================================================

void IODeviceManager::RegisterEnumerator(IODeviceCategory category,
                                         const std::string& backendName,
                                         IODeviceEnumerator enumerator) {
    if (!enumerator) {
        return;
    }

    std::lock_guard<std::mutex> lock(registryMutex);

    for (auto& entry : enumerators) {
        if (entry.category == category && entry.backendName == backendName) {
            entry.enumerate = std::move(enumerator);
            return;
        }
    }

    EnumeratorEntry entry;
    entry.category = category;
    entry.backendName = backendName;
    entry.enumerate = std::move(enumerator);
    enumerators.push_back(std::move(entry));
}

void IODeviceManager::UnregisterEnumerator(IODeviceCategory category,
                                           const std::string& backendName) {
    std::lock_guard<std::mutex> lock(registryMutex);
    enumerators.erase(
        std::remove_if(enumerators.begin(), enumerators.end(),
                       [&](const EnumeratorEntry& entry) {
                           return entry.category == category &&
                                  entry.backendName == backendName;
                       }),
        enumerators.end());
}

std::vector<std::string>
IODeviceManager::GetRegisteredBackends(IODeviceCategory category) const {
    std::lock_guard<std::mutex> lock(registryMutex);
    std::vector<std::string> names;
    for (const auto& entry : enumerators) {
        if (entry.category == category) {
            names.push_back(entry.backendName);
        }
    }
    return names;
}

// ============================================================================
// DISCOVERY
// ============================================================================

IODeviceResult IODeviceManager::EnumerateDevices(IODeviceCategory category) {
    // Copy the enumerators out before running them: a backend may take its
    // own locks or block on hardware, and holding registryMutex across that
    // would stall every other caller.
    std::vector<EnumeratorEntry> toRun;
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        for (const auto& entry : enumerators) {
            if (entry.category == category) {
                toRun.push_back(entry);
            }
        }
    }

    if (toRun.empty()) {
        return IODeviceResult::Error(
            IODeviceResultCode::BackendUnavailable,
            std::string("No backend registered for category ") +
                IODeviceCategoryToString(category));
    }

    std::vector<IODevicePtr> discovered;
    std::unordered_set<std::string> discoveredIds;
    std::string backendErrors;

    for (const auto& entry : toRun) {
        std::vector<IODevicePtr> found;
        try {
            found = entry.enumerate();
        } catch (const std::exception& e) {
            // One broken backend must not hide the devices the others found.
            backendErrors += entry.backendName + ": " + e.what() + "; ";
            continue;
        } catch (...) {
            backendErrors += entry.backendName + ": unknown exception; ";
            continue;
        }

        for (const auto& device : found) {
            if (!device) {
                continue;
            }
            const IODeviceId id = device->GetDeviceId();
            if (id.empty()) {
                continue;
            }
            // First backend to claim an id wins, so a device reachable
            // through two backends is registered once.
            if (discoveredIds.insert(id).second) {
                discovered.push_back(device);
            }
        }
    }

    std::vector<PendingChange> changes;
    std::vector<IODevicePtr> removed;
    size_t categoryCount = 0;

    {
        std::lock_guard<std::mutex> lock(registryMutex);

        std::unordered_set<std::string> knownIds;
        for (const auto& device : devices) {
            if (device && device->GetCategory() == category) {
                knownIds.insert(device->GetDeviceId());
            }
        }

        // Drop the ones that went away.
        auto gone = std::stable_partition(
            devices.begin(), devices.end(),
            [&](const IODevicePtr& device) {
                if (!device || device->GetCategory() != category) {
                    return true;    // keep: other category
                }
                return discoveredIds.count(device->GetDeviceId()) > 0;
            });

        for (auto it = gone; it != devices.end(); ++it) {
            if (*it) {
                changes.push_back({IODeviceChange::Removed, (*it)->GetDeviceInfo()});
                removed.push_back(*it);
            }
        }
        devices.erase(gone, devices.end());

        // Add the new ones. A device already registered keeps its existing
        // object, so an open session survives a re-enumeration.
        for (const auto& device : discovered) {
            if (knownIds.count(device->GetDeviceId()) == 0) {
                devices.push_back(device);
                changes.push_back({IODeviceChange::Added, device->GetDeviceInfo()});
            }
        }

        for (const auto& device : devices) {
            if (device && device->GetCategory() == category) {
                ++categoryCount;
            }
        }
    }

    for (const auto& device : removed) {
        device->Disconnect();
    }

    Notify(changes);

    IODeviceResult result = IODeviceResult::Ok();
    result.backendCode = static_cast<int>(categoryCount);
    if (!backendErrors.empty()) {
        result.message = "Some backends failed: " + backendErrors;
    }
    return result;
}

IODeviceResult IODeviceManager::EnumerateAllDevices() {
    std::vector<IODeviceCategory> categories;
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        for (const auto& entry : enumerators) {
            if (std::find(categories.begin(), categories.end(), entry.category) ==
                categories.end()) {
                categories.push_back(entry.category);
            }
        }
    }

    if (categories.empty()) {
        return IODeviceResult::Error(IODeviceResultCode::BackendUnavailable,
                                     "No device backends are registered");
    }

    std::string errors;
    for (IODeviceCategory category : categories) {
        IODeviceResult result = EnumerateDevices(category);
        if (!result.message.empty()) {
            errors += result.message;
        }
    }

    IODeviceResult result = IODeviceResult::Ok();
    result.message = errors;
    return result;
}

// ============================================================================
// REGISTRY
// ============================================================================

IODeviceResult IODeviceManager::RegisterDevice(const IODevicePtr& device) {
    if (!device) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "Cannot register a null device");
    }

    const IODeviceId id = device->GetDeviceId();
    if (id.empty()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "Cannot register a device with an empty id");
    }

    IODeviceInfo info;
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        for (const auto& existing : devices) {
            if (existing && existing->GetDeviceId() == id) {
                return IODeviceResult::Error(
                    IODeviceResultCode::InvalidArgument,
                    "A device is already registered under id '" + id + "'", id);
            }
        }
        devices.push_back(device);
        info = device->GetDeviceInfo();
    }

    Notify({{IODeviceChange::Added, info}});
    return IODeviceResult::Ok(id);
}

void IODeviceManager::UnregisterDevice(const IODeviceId& deviceId) {
    IODevicePtr removed;
    IODeviceInfo info;

    {
        std::lock_guard<std::mutex> lock(registryMutex);
        auto it = std::find_if(devices.begin(), devices.end(),
                               [&](const IODevicePtr& device) {
                                   return device && device->GetDeviceId() == deviceId;
                               });
        if (it == devices.end()) {
            return;
        }
        removed = *it;
        info = removed->GetDeviceInfo();
        devices.erase(it);
    }

    removed->Disconnect();
    Notify({{IODeviceChange::Removed, info}});
}

std::vector<IODevicePtr> IODeviceManager::GetDevices() const {
    std::lock_guard<std::mutex> lock(registryMutex);
    return devices;
}

std::vector<IODevicePtr> IODeviceManager::GetDevices(IODeviceCategory category) const {
    std::lock_guard<std::mutex> lock(registryMutex);
    std::vector<IODevicePtr> result;
    for (const auto& device : devices) {
        if (device && device->GetCategory() == category) {
            result.push_back(device);
        }
    }
    return result;
}

std::vector<IODeviceInfo>
IODeviceManager::GetDeviceInfos(IODeviceCategory category) const {
    std::vector<IODevicePtr> matching = GetDevices(category);
    std::vector<IODeviceInfo> infos;
    infos.reserve(matching.size());
    for (const auto& device : matching) {
        infos.push_back(device->GetDeviceInfo());
    }
    return infos;
}

IODevicePtr IODeviceManager::GetDeviceById(const IODeviceId& deviceId) const {
    std::lock_guard<std::mutex> lock(registryMutex);
    for (const auto& device : devices) {
        if (device && device->GetDeviceId() == deviceId) {
            return device;
        }
    }
    return nullptr;
}

IODevicePtr IODeviceManager::GetDevice(IODeviceCategory category, size_t index) const {
    std::vector<IODevicePtr> matching = GetDevices(category);
    if (index >= matching.size()) {
        return nullptr;
    }
    return matching[index];
}

size_t IODeviceManager::GetDeviceCount() const {
    std::lock_guard<std::mutex> lock(registryMutex);
    return devices.size();
}

size_t IODeviceManager::GetDeviceCount(IODeviceCategory category) const {
    std::lock_guard<std::mutex> lock(registryMutex);
    size_t count = 0;
    for (const auto& device : devices) {
        if (device && device->GetCategory() == category) {
            ++count;
        }
    }
    return count;
}

// ============================================================================
// NOTIFICATION
// ============================================================================

void IODeviceManager::SetDeviceChangeCallback(IODeviceChangeCallback callback) {
    std::lock_guard<std::mutex> lock(registryMutex);
    changeCallback = std::move(callback);
}

void IODeviceManager::Notify(const std::vector<PendingChange>& changes) {
    if (changes.empty()) {
        return;
    }

    IODeviceChangeCallback callback;
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        callback = changeCallback;
    }
    if (!callback) {
        return;
    }

    for (const auto& change : changes) {
        callback(change.change, change.info);
    }
}

} // namespace UltraCanvas
