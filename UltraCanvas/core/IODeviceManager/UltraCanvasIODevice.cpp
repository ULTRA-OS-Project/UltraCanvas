// core/IODeviceManager/UltraCanvasIODevice.cpp
// IODevice base: lifecycle state machine, error slot and locking shared by
// every device backend.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "../../include/IODeviceManager/UltraCanvasIODevice.h"

namespace UltraCanvas {

IODevice::IODevice(const IODeviceInfo& info)
    : deviceInfo(info) {
    if (deviceInfo.state == IODeviceState::Unknown) {
        deviceInfo.state = IODeviceState::Disconnected;
    }
    state = deviceInfo.state;
}

// Deliberately does not call Disconnect(): the derived object is already
// destroyed by the time this runs, so DoDisconnect() would dispatch into a
// dead object — and, being pure virtual, would terminate. Each concrete
// backend releases its own handles in its own destructor.
IODevice::~IODevice() = default;

// ===== IDENTITY =====

IODeviceId IODevice::GetDeviceId() const {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    return deviceInfo.deviceId;
}

std::string IODevice::GetDeviceName() const {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    return deviceInfo.name;
}

IODeviceCategory IODevice::GetCategory() const {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    return deviceInfo.category;
}

IODeviceTransport IODevice::GetTransport() const {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    return deviceInfo.transport;
}

std::string IODevice::GetBackendName() const {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    return deviceInfo.backend;
}

IODeviceInfo IODevice::GetDeviceInfo() const {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    IODeviceInfo info = deviceInfo;
    info.state = state;
    return info;
}

// ===== LIFECYCLE =====

IODeviceResult IODevice::Connect() {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    if (connected) {
        return IODeviceResult::Ok(deviceInfo.deviceId);
    }

    state = IODeviceState::Connecting;
    lastError = IODeviceResult();

    IODeviceResult result = DoConnect();

    if (result.success) {
        connected = true;
        // A backend that moved itself past Connecting (straight to Ready, or
        // to Busy because it started work) keeps its own choice.
        if (state == IODeviceState::Connecting) {
            state = IODeviceState::Ready;
        }
        if (result.deviceId.empty()) {
            result.deviceId = deviceInfo.deviceId;
        }
    } else {
        connected = false;
        state = IODeviceState::Error;
        if (result.deviceId.empty()) {
            result.deviceId = deviceInfo.deviceId;
        }
        lastError = result;
    }

    return result;
}

void IODevice::Disconnect() {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    // Always run the backend teardown, even when we never reached connected:
    // DoConnect() may have failed halfway and left handles open.
    DoDisconnect();

    connected = false;
    state = IODeviceState::Disconnected;
}

bool IODevice::IsConnected() const {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    return connected;
}

IODeviceState IODevice::GetState() const {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    return state;
}

// ===== ERRORS =====

IODeviceResult IODevice::GetLastError() const {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    return lastError;
}

void IODevice::ClearLastError() {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    lastError = IODeviceResult();
}

// ===== STATE HELPERS FOR BACKENDS =====

void IODevice::SetState(IODeviceState newState) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    state = newState;
}

void IODevice::SetLastError(const IODeviceResult& error) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    lastError = error;
}

IODeviceResult IODevice::Fail(IODeviceResultCode code, const std::string& message) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    IODeviceResult result = IODeviceResult::Error(code, message, deviceInfo.deviceId);
    lastError = result;
    state = IODeviceState::Error;
    return result;
}

IODeviceResult IODevice::RequireConnected() const {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    if (!connected) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidState,
                                     "Device is not connected",
                                     deviceInfo.deviceId);
    }
    return IODeviceResult::Ok(deviceInfo.deviceId);
}

void IODevice::UpdateDeviceInfo(const IODeviceInfo& info) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    // The identity a device was registered under is fixed: the manager
    // indexes by it, so letting a backend rewrite it mid-session would
    // orphan the registry entry.
    IODeviceId id = deviceInfo.deviceId;
    deviceInfo = info;
    deviceInfo.deviceId = id;
}

} // namespace UltraCanvas
