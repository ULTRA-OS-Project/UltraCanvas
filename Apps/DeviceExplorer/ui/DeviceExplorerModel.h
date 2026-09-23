// Apps/DeviceExplorer/ui/DeviceExplorerModel.h
// What DeviceExplorer shows, with no UI in it: a snapshot of the devices
// IODeviceManager has registered, the grouping that turns that snapshot into
// the tree on the left, and the property sections the panel on the right
// lists for whatever is selected there.
//
// Kept free of every UltraCanvas UI header on purpose, so the window, the
// headless --list mode and the test all build the same tree from the same
// code, and the test needs nothing but the IODeviceManager sources to run.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "IODeviceManager/UltraCanvasIODeviceManager.h"
#include "IODeviceManager/UltraCanvasIODeviceTypes.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace DeviceExplorer {

// ============================================================================
// SNAPSHOT
// ============================================================================

// One registered device, as it was when the snapshot was taken. The info
// already carries the live state (IODevice::GetDeviceInfo folds it in).
struct DeviceRecord {
    UltraCanvas::IODeviceInfo info;
    bool connected = false;
    std::string lastError;          // empty when the device recorded none
};

// Everything the explorer knows about the machine's devices at one moment.
struct DeviceInventory {
    std::vector<DeviceRecord> devices;

    // Backends compiled into this build, per category, in registration order.
    // A category with a backend and no device is shown ("none found"); one
    // with no backend is not, because the explorer could never find anything
    // there and saying "none" would be a claim it cannot make.
    std::map<UltraCanvas::IODeviceCategory, std::vector<std::string>> backends;

    // What the last enumeration reported: backend failures, "no backends".
    std::string enumerationMessage;
    bool enumerated = false;        // false until a scan has run at least once
    int64_t capturedAt = 0;         // seconds since the epoch

    size_t CountIn(UltraCanvas::IODeviceCategory category) const;
};

// Reads the registry. Cheap: nothing is enumerated or opened.
DeviceInventory SnapshotInventory(const UltraCanvas::IODeviceManager& manager);

// Initialises the manager if needed, enumerates every category that has a
// backend, and snapshots the result. Blocking - SANE and CUPS can take
// seconds - so the window runs it on a worker thread.
DeviceInventory ScanInventory(UltraCanvas::IODeviceManager& manager);

// ============================================================================
// GROUPING (the tree)
// ============================================================================

enum class DeviceGrouping {
    Category,       // Printers / Cameras / Scanners ...
    Connection,     // USB / Network / Bluetooth ...
    Backend         // CUPS / V4L2 / SANE / eSCL ...
};

constexpr int kGroupingCount = 3;
const char* GroupingName(DeviceGrouping grouping);          // "Category"
DeviceGrouping GroupingFromIndex(int index);
bool ParseGrouping(const std::string& text, DeviceGrouping& grouping);

struct DeviceGroup {
    std::string key;                // stable: "category:Printer", "transport:USB"
    std::string title;              // "Printers", "USB", "CUPS"
    UltraCanvas::IODeviceCategory category = UltraCanvas::IODeviceCategory::Unknown;
    UltraCanvas::IODeviceTransport transport = UltraCanvas::IODeviceTransport::Unknown;
    std::vector<const DeviceRecord*> devices;   // into the inventory; sorted by name
};

// Groups the inventory's devices, keeping only those that match `filter`
// (case-insensitive substring of name, manufacturer, model, backend,
// location, category or transport; empty matches everything). Grouped by
// category, every category that has a backend gets a group even when it is
// empty and the filter is empty, so "no printers" is visible as such. The
// returned pointers are into `inventory` and live as long as it does.
std::vector<DeviceGroup> GroupDevices(const DeviceInventory& inventory,
                                      DeviceGrouping grouping,
                                      const std::string& filter = {});

bool MatchesFilter(const DeviceRecord& record, const std::string& filter);

// Names for display. Plural for category groups ("Printers"), singular for
// a device's own row ("Printer").
std::string CategoryDisplayName(UltraCanvas::IODeviceCategory category);
std::string CategoryGroupTitle(UltraCanvas::IODeviceCategory category);
std::string TransportDisplayName(UltraCanvas::IODeviceTransport transport);
std::string StateDisplayName(UltraCanvas::IODeviceState state);

// The label a device's tree row carries: its name, or model, or id.
std::string DeviceDisplayName(const UltraCanvas::IODeviceInfo& info);

// Stable tree node ids. A device id is backend-defined and may contain
// anything, so it is prefixed rather than used bare.
std::string DeviceNodeId(const UltraCanvas::IODeviceId& deviceId);
std::string GroupNodeId(const DeviceGroup& group);
extern const char* const kComputerNodeId;

// ============================================================================
// DETAILS (the panel on the right)
// ============================================================================

struct PropertyRow {
    std::string name;
    std::string value;
};

struct PropertySection {
    std::string title;
    std::vector<PropertyRow> rows;
};

// What the computer node describes: filled from UltraCanvasHardwareInfo by
// the window, left empty by the test.
struct MachineSummary {
    std::string hostName;
    std::string operatingSystem;
    std::string kernel;
    std::string manufacturer;
    std::string model;
};

// Serial numbers identify the owner's hardware; the window passes
// UltraCanvasHardwareInfo::MaskIdentifier here so the explorer masks them by
// the same rule the hardware panel does. Null shows them verbatim.
using IdentifierMask = std::function<std::string(const std::string&)>;

std::vector<PropertySection> DescribeDevice(const DeviceRecord& record,
                                            const IdentifierMask& mask = {});
std::vector<PropertySection> DescribeGroup(const DeviceGroup& group,
                                           DeviceGrouping grouping,
                                           const DeviceInventory& inventory);
std::vector<PropertySection> DescribeComputer(const DeviceInventory& inventory,
                                              const MachineSummary& machine,
                                              bool monitoring);

// The whole tree as indented text, for --list.
std::string FormatInventoryText(const DeviceInventory& inventory,
                                DeviceGrouping grouping,
                                const MachineSummary& machine,
                                bool details,
                                const IdentifierMask& mask = {});

} // namespace DeviceExplorer
