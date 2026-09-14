// include/IODeviceManager/UltraCanvasIODevice.h
// Abstract base for every device IODeviceManager operates. Category-specific
// classes (ScannerDevice, CameraDevice, PrinterDevice) derive from this and
// add the operations that only make sense for their category.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODeviceTypes.h"

#include <memory>
#include <mutex>
#include <string>

namespace UltraCanvas {

// ============================================================================
// IODevice
// ============================================================================
//
// Lifecycle is non-virtual public / virtual protected (NVI): callers use
// Connect() and Disconnect(); backends implement DoConnect() and
// DoDisconnect(). The base owns the state machine, the error slot and the
// locking, so no backend has to reimplement them and get them subtly
// different.
//
// The base destructor deliberately does NOT call Disconnect(): by the time
// it runs the derived object is already destroyed, so a virtual call from
// here would dispatch into a dead object (or, for a pure virtual, terminate).
// Every concrete backend is responsible for releasing its own handles in its
// own destructor — see the note on ~IODevice() below.
//
class IODevice {
public:
    virtual ~IODevice();

    IODevice(const IODevice&) = delete;
    IODevice& operator=(const IODevice&) = delete;

    // ===== IDENTITY =====

    IODeviceId GetDeviceId() const;
    std::string GetDeviceName() const;
    IODeviceCategory GetCategory() const;
    IODeviceTransport GetTransport() const;
    std::string GetBackendName() const;

    // Snapshot of the description this device was created from, with the
    // live state folded in.
    IODeviceInfo GetDeviceInfo() const;

    // ===== LIFECYCLE =====

    // Opens a session with the device. Idempotent: connecting an already
    // connected device succeeds without touching the backend.
    IODeviceResult Connect();

    // Closes the session. Idempotent, and safe to call on a device that
    // failed to connect. Never throws.
    void Disconnect();

    bool IsConnected() const;
    IODeviceState GetState() const;

    // ===== ERRORS =====

    // The last failure recorded by this device. Cleared by a successful
    // Connect(); backends may clear it at the start of their operations.
    IODeviceResult GetLastError() const;
    void ClearLastError();

protected:
    explicit IODevice(const IODeviceInfo& info);

    // ===== BACKEND HOOKS =====

    // Open/close the backend session. DoDisconnect() must not throw and
    // must tolerate being called when DoConnect() never succeeded.
    virtual IODeviceResult DoConnect() = 0;
    virtual void DoDisconnect() = 0;

    // ===== STATE HELPERS FOR BACKENDS =====

    void SetState(IODeviceState state);
    void SetLastError(const IODeviceResult& error);

    // Records `error`, moves the device to Error state and returns it, so a
    // backend can `return Fail(...)` in one line.
    IODeviceResult Fail(IODeviceResultCode code, const std::string& message);

    // Guards a backend operation: fails unless the device is connected.
    // Returns an Ok() result when the device is usable.
    IODeviceResult RequireConnected() const;

    // Mutable copy of the description, for backends that learn more about
    // the device once it is open (model, serial, capabilities).
    void UpdateDeviceInfo(const IODeviceInfo& info);

    // Recursive because the backend hooks below are invoked with it held,
    // and a backend legitimately calls SetState()/Fail() both from inside
    // those hooks and from its own worker threads, where it is not held.
    mutable std::recursive_mutex deviceMutex;

private:
    IODeviceInfo deviceInfo;
    IODeviceState state = IODeviceState::Disconnected;
    IODeviceResult lastError;
    bool connected = false;
};

using IODevicePtr = std::shared_ptr<IODevice>;

} // namespace UltraCanvas
