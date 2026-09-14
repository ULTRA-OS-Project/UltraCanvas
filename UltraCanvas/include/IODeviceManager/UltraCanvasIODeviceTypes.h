// include/IODeviceManager/UltraCanvasIODeviceTypes.h
// Device-generic vocabulary shared by every IODeviceManager category
// (scanner, camera, printer, ...): identity, transport, lifecycle state and
// the result type every blocking operation returns.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Defensive: X11/Xlib.h defines `Success`, `None`, `Bool` and `Status` as
// macros, which would turn the enum-class members below into literal
// integers. UltraCanvas's Linux platform glue pulls X11 in transitively, so
// any TU including both an UltraCanvas header and this one would otherwise
// fail to compile. Same guard as UltraNetCore.h.
#ifdef Success
#undef Success
#endif
#ifdef None
#undef None
#endif
#ifdef Bool
#undef Bool
#endif
#ifdef Status
#undef Status
#endif

namespace UltraCanvas {

// ============================================================================
// DEVICE IDENTITY
// ============================================================================

// Stable, opaque identifier for a device within one IODeviceManager session.
// Its content is backend-defined (a SANE name, a CUPS destination, a V4L2
// node, a gphoto2 port) and callers must not parse it. It stays valid until
// the device is unregistered.
using IODeviceId = std::string;

// ============================================================================
// DEVICE CATEGORY
// ============================================================================

// What the device *is*. One category maps to one derived IODevice class
// (ScannerDevice, CameraDevice, PrinterDevice, ...).
enum class IODeviceCategory {
    Unknown,
    Scanner,
    Camera,
    Printer,
    Microphone,
    Speaker,
    Storage,
    NetworkAdapter,
    Serial,
    Bluetooth,
    GPIO,
    Barcode,
    Biometric,
    Custom
};

// ============================================================================
// TRANSPORT
// ============================================================================

// How the device is *reached*. Deliberately distinct from the per-category
// protocol enums (SANE/WIA/TWAIN for scanners, V4L2/PTP for cameras,
// CUPS/IPP for printers): a PTP camera and an IPP printer are both reached
// over USB or the network, and callers routinely want to filter on that
// without knowing the category's protocol vocabulary.
enum class IODeviceTransport {
    Unknown,
    USB,
    Network,
    Bluetooth,
    Serial,
    Parallel,
    PCI,
    Virtual         // software device, loopback, test double
};

// ============================================================================
// LIFECYCLE STATE
// ============================================================================

enum class IODeviceState {
    Unknown,
    Disconnected,   // known to exist, no session open
    Connecting,
    Connected,      // session open, not yet ready for work
    Ready,          // idle and accepting operations
    Busy,           // mid-operation
    Paused,
    Error,          // see IODevice::GetLastDeviceError()
    Offline         // enumerated previously, not reachable now
};

// ============================================================================
// RESULT
// ============================================================================

enum class IODeviceResultCode {
    Success,
    Unknown,
    NotImplemented,
    NotSupported,           // valid request, this device/backend cannot do it
    InvalidArgument,
    InvalidState,           // e.g. operation requires a connected device
    DeviceNotFound,
    DeviceBusy,
    AccessDenied,           // permissions: udev rule, TCC prompt, UAC
    ConnectionFailed,
    CommunicationError,
    Timeout,
    Cancelled,
    OutOfMemory,
    BackendUnavailable,     // driver/library/tool not installed
    BackendError,           // backend reported a failure of its own
    HardwareError,          // paper jam, lens error, lamp failure
    SupplyEmpty,            // out of ink, toner or paper
    MediaError,
    IOError
};

const char* IODeviceResultCodeToString(IODeviceResultCode code);

// Every blocking operation in IODeviceManager returns one of these. Mirrors
// UltraNetResult / UltraDbResult so the three modules read alike.
struct IODeviceResult {
    IODeviceResultCode code = IODeviceResultCode::Unknown;
    bool success = false;
    std::string message;        // human-readable, may be empty on success
    IODeviceId deviceId;        // device the operation ran against, if any

    // Backend's own error number, verbatim, for diagnostics only: an
    // SANE_Status, an HRESULT, a CUPS ipp_status_t, a gphoto2 return code.
    // Zero when the backend reported none. Never switch on this.
    int backendCode = 0;

    explicit operator bool() const { return success; }

    static IODeviceResult Ok(const IODeviceId& id = {}) {
        IODeviceResult r;
        r.code = IODeviceResultCode::Success;
        r.success = true;
        r.deviceId = id;
        return r;
    }

    static IODeviceResult Error(IODeviceResultCode c,
                                const std::string& msg,
                                const IODeviceId& id = {}) {
        IODeviceResult r;
        r.code = c;
        r.success = false;
        r.message = msg;
        r.deviceId = id;
        return r;
    }

    // As Error(), plus the backend's native status for the log.
    static IODeviceResult BackendError(IODeviceResultCode c,
                                       const std::string& msg,
                                       int nativeCode,
                                       const IODeviceId& id = {}) {
        IODeviceResult r = Error(c, msg, id);
        r.backendCode = nativeCode;
        return r;
    }
};

// ============================================================================
// DEVICE DESCRIPTION
// ============================================================================

// What enumeration returns: enough to show the device in a picker and to
// open it, without having opened it yet.
struct IODeviceInfo {
    IODeviceId deviceId;
    std::string name;               // display name
    std::string manufacturer;
    std::string model;
    std::string serialNumber;
    std::string description;
    std::string location;           // "USB port 3", "Reception", an IP

    IODeviceCategory category = IODeviceCategory::Unknown;
    IODeviceTransport transport = IODeviceTransport::Unknown;
    IODeviceState state = IODeviceState::Unknown;

    // Which backend produced this entry ("SANE", "V4L2", "CUPS",
    // "GutenPrint", "IPP", ...). Set by the enumerator; used to tell apart
    // two entries describing the same physical device.
    std::string backend;

    // Where the backend reaches it: a device node, a CUPS/IPP URI, a
    // gphoto2 port. Backend-defined; callers must not parse it.
    std::string connectionPath;

    // Backend-specific extras that have no home in the fields above.
    // Diagnostics and backend-internal use only — never load-bearing for
    // dispatch, which is what `backend` is for.
    std::map<std::string, std::string> attributes;

    bool IsValid() const { return !deviceId.empty(); }
};

// ============================================================================
// ENUM HELPERS
// ============================================================================

const char* IODeviceCategoryToString(IODeviceCategory category);
const char* IODeviceTransportToString(IODeviceTransport transport);
const char* IODeviceStateToString(IODeviceState state);

} // namespace UltraCanvas
