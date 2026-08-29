// include/UltraCanvasHardwareInfo.h
// Read-only hardware inventory and sensor readings: CPU (caches, temperature),
// GPU, NPU, memory, storage (temperature, connector), network (Ethernet/Wi-Fi),
// USB controllers and attached devices, Bluetooth adapters and connections.
// Version: 0.1.0
// Last Modified: 2026-08-29
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASHARDWAREINFO_H
#define ULTRACANVASHARDWAREINFO_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== WHAT THIS MODULE IS (AND IS NOT) =====
// UltraCanvasHardwareInfo *describes* the machine the application runs on: an
// inventory of the parts plus the sensors that report on them. It never opens,
// configures or operates a device - that is IODeviceManager's job (scanners,
// cameras, printers, GPIO: things with a connect/configure/transfer lifecycle).
// Everything here is a read: no handles, no ownership, no side effects.

// ===== CATEGORY =====
enum class HardwareCategory {
    System,      // machine, board, firmware, OS
    CPU,
    GPU,
    NPU,         // neural / AI accelerators
    Memory,
    Storage,
    Network,
    USB,
    Bluetooth
};

// ===== QUERY SELECTOR =====
// Bit flags naming the categories a capture should fill. Probing costs differ by
// orders of magnitude (CPU topology is microseconds, a full USB tree walk is
// milliseconds), so a caller that only wants a status bar reading asks for one
// category rather than the whole machine.
enum class HardwareQuery : uint32_t {
    None      = 0,
    System    = 1u << 0,
    CPU       = 1u << 1,
    GPU       = 1u << 2,
    NPU       = 1u << 3,
    Memory    = 1u << 4,
    Storage   = 1u << 5,
    Network   = 1u << 6,
    USB       = 1u << 7,
    Bluetooth = 1u << 8,
    // Temperatures, utilisation and other live readings. Separated from the
    // inventory because they are the only part worth polling: RefreshSensors()
    // re-reads exactly these without walking the device trees again.
    Sensors   = 1u << 9,
    All       = 0x3FFu
};

inline HardwareQuery operator|(HardwareQuery a, HardwareQuery b) {
    return static_cast<HardwareQuery>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline HardwareQuery operator&(HardwareQuery a, HardwareQuery b) {
    return static_cast<HardwareQuery>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline HardwareQuery& operator|=(HardwareQuery& a, HardwareQuery b) { a = a | b; return a; }
inline bool HasQuery(HardwareQuery set, HardwareQuery flag) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(flag)) != 0;
}

// ===== CPU CACHE =====
enum class CPUCacheType { Unknown, Data, Instruction, Unified, Trace };

struct CPUCacheInfo {
    int          level        = 0;   // 1, 2, 3, 4
    CPUCacheType type         = CPUCacheType::Unknown;
    uint64_t     sizeBytes    = 0;   // size of ONE cache of this kind
    int          lineSizeBytes = 0;
    int          associativity = 0;  // 0 = unknown, -1 = fully associative
    int          instanceCount = 1;  // how many such caches the package holds
    int          sharedByLogicalCores = 0; // 0 = unknown
    std::string  Describe() const;   // "L2 unified, 8 x 1.25 MB, 12-way"
};

// ===== CPU CORE GROUP (hybrid / big.LITTLE topologies) =====
struct CPUCoreGroup {
    std::string name;              // "Performance", "Efficiency", "Cortex-A78"
    int    physicalCores = 0;
    int    logicalCores  = 0;
    double maxClockMHz   = 0.0;
};

// ===== CPU =====
struct CPUInfo {
    std::string vendor;            // "GenuineIntel", "AuthenticAMD", "Apple"
    std::string model;             // marketing string from the part itself
    std::string architecture;      // "x86_64", "aarch64", "riscv64"
    std::string socket;            // "LGA1700", "BGA", "" when unknown
    std::string stepping;          // family/model/stepping, as the platform words it

    int packages      = 1;
    int physicalCores = 0;
    int logicalCores  = 0;

    double baseClockMHz    = 0.0;
    double maxClockMHz     = 0.0;
    double currentClockMHz = 0.0;  // averaged over the online cores

    std::vector<CPUCacheInfo> caches;
    std::vector<CPUCoreGroup> coreGroups;   // empty on a uniform CPU
    std::vector<std::string>  instructionSets; // "AVX2", "AES", "NEON", "SVE"

    // Highest x86-64 psABI microarchitecture level the CPU satisfies (1..4);
    // 0 off x86. It is the actionable form of the list above: -march=x86-64-v<N>
    // means exactly that feature set, so a build targeting this level is
    // guaranteed to run here. Extensions outside every level - GFNI, VAES,
    // VPCLMULQDQ, SHA - are named in `instructionSets` and deliberately do not
    // raise it, because no -march=x86-64-vN emits them and only -march=native
    // drags them in.
    int x86MicroarchitectureLevel = 0;

    // Set when this process is not running natively on the CPU above - "x64
    // image on an ARM64 machine", "x86_64 image translated by Rosetta". Empty
    // when native. It matters here because the two halves of this struct then
    // describe different things: `model` and the core counts come from the
    // silicon, while `instructionSets` is what the emulator permits (Windows on
    // ARM offers no AVX-512 at all). A binary built with -march=native on the
    // build host and run here is the classic way that gap turns into an
    // illegal-instruction crash.
    std::string emulation;

    // Sensors (present only when the platform exposes them to an unprivileged
    // process; see HardwareSnapshot::warnings when they are missing).
    std::optional<double> temperatureC;
    std::optional<double> loadPercent;

    // Total size of every cache at `level` in the package (sizeBytes * instanceCount).
    uint64_t TotalCacheSize(int level) const;
    bool     HasInstructionSet(const std::string& name) const;
};

// ===== GPU =====
enum class GPUKind { Unknown, Integrated, Discrete, Virtual, Software };

struct GPUInfo {
    std::string vendor;            // "NVIDIA", "AMD", "Intel", "Apple"
    std::string model;
    std::string driverName;
    std::string driverVersion;
    std::string busId;             // "0000:01:00.0", "" when not on a bus
    uint16_t    pciVendorId = 0;
    uint16_t    pciDeviceId = 0;
    GPUKind     kind = GPUKind::Unknown;

    uint64_t videoMemoryBytes  = 0; // dedicated VRAM
    uint64_t sharedMemoryBytes = 0; // system memory the GPU may borrow
    int      computeUnits      = 0; // CU / SM / EU / core count, as reported
    double   coreClockMHz      = 0.0;

    std::optional<double> temperatureC;
    std::optional<double> utilizationPercent;
    std::optional<uint64_t> videoMemoryUsedBytes;
};

// ===== NPU / AI ACCELERATOR =====
struct NPUInfo {
    std::string vendor;
    std::string model;             // "Intel AI Boost", "AMD XDNA", "Apple Neural Engine"
    std::string driverName;
    std::string driverVersion;
    std::string busId;
    std::string runtime;           // "OpenVINO", "DirectML", "CoreML", "RKNN"
    uint16_t    pciVendorId = 0;
    uint16_t    pciDeviceId = 0;
    bool        integrated  = true;
    double      peakTOPS    = 0.0; // INT8 TOPS when the part is known, else 0
    std::vector<std::string> precisions; // "INT8", "FP16", "BF16"

    std::optional<double> temperatureC;
    std::optional<double> utilizationPercent;
};

// ===== MEMORY =====
struct MemoryModuleInfo {
    std::string locator;           // "DIMM_A1", "Channel-0"
    std::string bankLocator;
    std::string type;              // "DDR4", "DDR5", "LPDDR5"
    std::string formFactor;        // "DIMM", "SODIMM", "Row of chips"
    std::string manufacturer;
    std::string partNumber;
    std::string serialNumber;      // masked unless HardwareInfoOptions says otherwise
    uint64_t sizeBytes     = 0;
    int      speedMTs      = 0;    // configured speed, MT/s
    int      ratedSpeedMTs = 0;    // the module's own rating
    int      dataWidthBits = 0;
    int      totalWidthBits = 0;   // > dataWidth when ECC is present
    double   voltageVolts  = 0.0;
};

struct MemoryInfo {
    uint64_t totalBytes     = 0;
    uint64_t availableBytes = 0;
    uint64_t usedBytes      = 0;
    uint64_t swapTotalBytes = 0;
    uint64_t swapFreeBytes  = 0;
    uint64_t pageSizeBytes  = 0;

    // Per-module detail comes from firmware tables (SMBIOS/DMI, IORegistry) and
    // is often privileged; an empty list with a warning is the normal
    // unprivileged result, and totalBytes is still filled.
    std::vector<MemoryModuleInfo> modules;
    int slotsTotal = 0;
    int slotsUsed  = 0;
};

// ===== STORAGE =====
enum class StorageBus { Unknown, NVMe, SATA, SAS, SCSI, IDE, USB, Thunderbolt, MMC, SD, Virtual };
enum class StorageMedia { Unknown, HDD, SSD, Optical, Flash, RAM };

struct StorageVolumeInfo {
    std::string mountPoint;
    std::string fileSystem;
    std::string label;
    uint64_t totalBytes = 0;
    uint64_t freeBytes  = 0;
    bool     readOnly   = false;
};

struct StorageDeviceInfo {
    std::string devicePath;        // "/dev/nvme0n1", "\\\\.\\PhysicalDrive0", "disk0"
    std::string model;
    std::string vendor;
    std::string firmwareVersion;
    std::string serialNumber;      // masked unless HardwareInfoOptions says otherwise
    StorageBus   bus   = StorageBus::Unknown;
    StorageMedia media = StorageMedia::Unknown;

    // How the drive is physically attached, spelled out for a person:
    // "M.2 / PCIe 4.0 x4", "SATA 6 Gb/s", "USB 3.2 Gen 2 (10 Gb/s)".
    std::string connector;

    uint64_t capacityBytes = 0;
    uint64_t cacheBytes    = 0;    // on-drive DRAM buffer; 0 when not reported
    int      rotationRateRPM = 0;  // 0 on solid state
    bool     removable       = false;
    int      logicalSectorSize  = 0;
    int      physicalSectorSize = 0;

    std::optional<double>   temperatureC;
    std::optional<int>      healthPercent;   // 100 = as new (SMART wear inverted)
    std::optional<uint64_t> powerOnHours;

    std::vector<StorageVolumeInfo> volumes;
};

// ===== NETWORK =====
enum class NetworkLinkType { Unknown, Ethernet, WiFi, Loopback, Bluetooth, Cellular, Virtual, Bridge, Tunnel };

struct WiFiInfo {
    bool        connected = false;
    std::string ssid;
    std::string bssid;             // masked unless HardwareInfoOptions says otherwise
    std::string security;          // "WPA3-SAE", "WPA2-PSK", "Open"
    std::string standard;          // "802.11ax (Wi-Fi 6)"
    std::string band;              // "2.4 GHz", "5 GHz", "6 GHz"
    int    channel       = 0;
    int    frequencyMHz  = 0;
    int    signalPercent = 0;      // 0..100
    int    signalDbm     = 0;      // negative; 0 when unknown
    double txRateMbps    = 0.0;
    double rxRateMbps    = 0.0;
};

struct NetworkInterfaceInfo {
    std::string name;              // "eth0", "wlan0", "Ethernet 2"
    std::string description;       // adapter product name where the OS has one
    std::string macAddress;        // masked unless HardwareInfoOptions says otherwise
    std::string driver;
    NetworkLinkType type = NetworkLinkType::Unknown;

    bool   up        = false;      // administratively up
    bool   connected = false;      // carrier present
    double linkSpeedMbps = 0.0;
    std::string duplex;            // "full", "half"
    int    mtu = 0;

    std::vector<std::string> ipv4Addresses;
    std::vector<std::string> ipv6Addresses;
    std::optional<WiFiInfo>  wifi; // set on wireless interfaces only

    uint64_t bytesReceived = 0;
    uint64_t bytesSent     = 0;
};

// ===== USB =====
struct USBControllerInfo {
    std::string name;              // "xHCI Host Controller"
    std::string version;           // "USB 3.2"
    std::string driver;
    std::string busId;             // PCI address where applicable
    int busNumber = 0;
    int portCount = 0;
};

struct USBDeviceInfo {
    uint16_t    vendorId  = 0;
    uint16_t    productId = 0;
    std::string vendorName;
    std::string productName;
    std::string serialNumber;      // masked unless HardwareInfoOptions says otherwise
    std::string deviceClass;       // "Human Interface Device", "Mass Storage"
    std::string speed;             // "480 Mbit/s (High-Speed)"
    std::string portPath;          // "1-4.2" - which port on which bus
    int  busNumber     = 0;
    int  deviceAddress = 0;
    int  maxPowerMilliAmps = 0;
    bool isHub = false;
    int  parentDeviceIndex = -1;   // index into HardwareSnapshot::usbDevices, -1 = on a root port
};

// ===== BLUETOOTH =====
struct BluetoothDeviceInfo {
    std::string name;
    std::string address;           // masked unless HardwareInfoOptions says otherwise
    std::string deviceClass;       // "Audio/Headset", "Peripheral/Keyboard"
    bool paired    = false;
    bool connected = false;
    bool trusted   = false;
    std::optional<int> rssiDbm;
    std::optional<int> batteryPercent;
};

struct BluetoothAdapterInfo {
    std::string name;
    std::string address;           // masked unless HardwareInfoOptions says otherwise
    std::string manufacturer;
    std::string version;           // "5.3"
    bool powered      = false;
    bool discoverable = false;
    std::vector<BluetoothDeviceInfo> devices;
};

// ===== SYSTEM =====
struct SystemInfo {
    std::string hostName;
    std::string osName;            // "Ubuntu 24.04 LTS", "Windows 11 Pro", "macOS 15.3"
    std::string osVersion;
    std::string kernelVersion;
    std::string architecture;
    std::string manufacturer;      // system vendor from the firmware tables
    std::string productName;
    std::string boardVendor;
    std::string boardName;
    std::string biosVendor;
    std::string biosVersion;
    std::string biosDate;
    std::string chassisType;       // "Notebook", "Desktop", "Virtual Machine"
    uint64_t    uptimeSeconds = 0;
};

// ===== SNAPSHOT =====
// One consistent reading of the machine. Categories not named in the capture's
// HardwareQuery are left default-constructed; `captured` records which were
// actually filled, so an empty vector never has to be read as "no such hardware".
struct HardwareSnapshot {
    SystemInfo  system;
    CPUInfo     cpu;
    MemoryInfo  memory;
    std::vector<GPUInfo>              gpus;
    std::vector<NPUInfo>              npus;
    std::vector<StorageDeviceInfo>    storage;
    std::vector<NetworkInterfaceInfo> network;
    std::vector<USBControllerInfo>    usbControllers;
    std::vector<USBDeviceInfo>        usbDevices;
    std::vector<BluetoothAdapterInfo> bluetoothAdapters;

    HardwareQuery captured = HardwareQuery::None;
    // Why a value is missing, in the words a user can act on ("drive temperature
    // needs read access to /dev/sda"). Never an error channel: a capture that
    // reaches nothing still returns a snapshot.
    std::vector<std::string> warnings;
    int64_t capturedAtUnixSeconds = 0;

    bool Has(HardwareQuery category) const { return HasQuery(captured, category); }
};

// ===== OPTIONS =====
struct HardwareInfoOptions {
    // Serial numbers, MAC addresses and BSSIDs identify the machine and its
    // owner's network, and a hardware panel is exactly the sort of screen that
    // gets photographed or pasted into a bug report - so they are masked to
    // their last four characters unless a caller opts out deliberately.
    bool maskIdentifiers = true;
    // Read temperatures and utilisation. Off makes a capture cheaper.
    bool includeSensors = true;
    // List hubs and root controllers among the USB devices. Off by default:
    // "what is plugged in" rarely means the hubs it is plugged into.
    bool includeUsbHubs = false;
    // Reuse a snapshot younger than this instead of re-probing. 0 = never cache.
    int  cacheLifetimeMs = 2000;
};

// ===== REPORT MODEL =====
// The display-ready shape of a snapshot: nested groups of name/value rows, which
// is what a properties panel, a text dump and a JSON export all need. Building
// it once here keeps the three renderings from drifting apart.
struct HardwareProperty {
    std::string name;
    std::string value;
    std::string tooltip;           // optional longer form / raw value
};

struct HardwarePropertyGroup {
    std::string id;                // stable across refreshes ("cpu.cache.l3")
    std::string title;
    HardwareCategory category = HardwareCategory::System;
    std::vector<HardwareProperty>      properties;
    std::vector<HardwarePropertyGroup> subGroups;
};

// ===== HARDWARE INFO API =====
class UltraCanvasHardwareInfo {
public:
    // ----- Capture -----
    // Probe the machine. Honours the cache lifetime in the current options; pass
    // forceRefresh to bypass it.
    static HardwareSnapshot Capture(HardwareQuery what = HardwareQuery::All,
                                    bool forceRefresh = false);

    // Re-read only the live values (temperatures, utilisation, free memory, link
    // state) into an existing snapshot, leaving the inventory alone. This is the
    // call a monitor loop makes; it does not walk the device trees.
    static void RefreshSensors(HardwareSnapshot& snapshot);

    // ----- Single-category convenience -----
    static CPUInfo                           GetCPU();
    static MemoryInfo                        GetMemory();
    static SystemInfo                        GetSystem();
    static std::vector<GPUInfo>              ListGPUs();
    static std::vector<NPUInfo>              ListNPUs();
    static std::vector<StorageDeviceInfo>    ListStorageDevices();
    static std::vector<NetworkInterfaceInfo> ListNetworkInterfaces();
    static std::vector<USBDeviceInfo>        ListUSBDevices();
    static std::vector<USBControllerInfo>    ListUSBControllers();
    static std::vector<BluetoothAdapterInfo> ListBluetoothAdapters();

    // ----- Options -----
    static void SetOptions(const HardwareInfoOptions& options);
    // By value: the options are shared state behind a lock, and handing out a
    // reference to them would let a caller read one while another thread writes.
    static HardwareInfoOptions GetOptions();

    // ----- Backend reflection -----
    // "sysfs", "win32", "iokit" or "null" (the fallback that reports only what
    // the C++ runtime itself knows).
    static std::string GetBackendName();
    static bool        IsAvailable();

    // ----- Presentation -----
    // Flatten a snapshot into display groups (also what the panel element and the
    // exporters below consume).
    static std::vector<HardwarePropertyGroup> BuildReport(const HardwareSnapshot& snapshot);
    static std::string ToText(const HardwareSnapshot& snapshot);
    static std::string ToJSON(const HardwareSnapshot& snapshot);

    // ----- Formatting helpers (shared with the panel) -----
    static std::string FormatBytes(uint64_t bytes);            // "16.0 GB", "512 KB"
    static std::string FormatFrequencyMHz(double megahertz);   // "3.60 GHz"
    static std::string FormatTemperature(double celsius);      // "48.5 °C"
    static std::string FormatBitrateMbps(double megabitsPerSecond); // "2.5 Gb/s"
    static std::string FormatDuration(uint64_t seconds);       // "3 d 04:15"

    // ----- Enum names -----
    static std::string ToString(HardwareCategory category);
    static std::string ToString(CPUCacheType type);
    static std::string ToString(GPUKind kind);
    static std::string ToString(StorageBus bus);
    static std::string ToString(StorageMedia media);
    static std::string ToString(NetworkLinkType type);

    // Mask an identifier the way maskIdentifiers does ("****4F2C"). Exposed so an
    // application that assembles its own strings masks them identically.
    static std::string MaskIdentifier(const std::string& value);
};

} // namespace UltraCanvas

#endif // ULTRACANVASHARDWAREINFO_H
