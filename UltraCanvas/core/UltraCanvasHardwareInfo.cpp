// core/UltraCanvasHardwareInfo.cpp
// Platform-independent half of the hardware inventory: capture orchestration,
// snapshot caching, identifier masking, the display report and its text/JSON
// renderings. Every value shown here is read by a platform backend under
// OS/<Platform>/ - this file never touches the machine directly.
// Version: 0.1.0
// Last Modified: 2026-08-29
// Author: UltraCanvas Framework

#include "UltraCanvasHardwareInfoBackend.h"
#include "DataFormats/UltraCanvasJSON.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <thread>

namespace UltraCanvas {

// ===== LOCAL HELPERS =====
namespace {

int64_t NowUnixSeconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

int64_t NowMilliseconds() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string TrimCopy(const std::string& text) {
    size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string JoinStrings(const std::vector<std::string>& parts, const std::string& separator) {
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) result += separator;
        result += parts[i];
    }
    return result;
}

std::string FormatDouble(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return std::string(buffer);
}

// A snapshot is cached whole, with the flags it was captured under: a later
// request for a subset is served from it, a request for a category it does not
// carry re-probes. Guarded because a monitor thread and the UI thread commonly
// ask at the same time.
std::mutex               g_stateMutex;
HardwareInfoOptions      g_options;
HardwareSnapshot         g_cachedSnapshot;
HardwareQuery            g_cachedQuery = HardwareQuery::None;
int64_t                  g_cachedAtMs  = 0;

// Reduce a snapshot to the categories a caller asked for. A cached snapshot is
// often broader than the request; returning it whole would make Capture's answer
// depend on what some other caller happened to ask for a moment earlier.
void TrimSnapshotTo(HardwareSnapshot& snapshot, HardwareQuery what) {
    if (!HasQuery(what, HardwareQuery::System))    snapshot.system = SystemInfo();
    if (!HasQuery(what, HardwareQuery::CPU))       snapshot.cpu = CPUInfo();
    if (!HasQuery(what, HardwareQuery::Memory))    snapshot.memory = MemoryInfo();
    if (!HasQuery(what, HardwareQuery::GPU))       snapshot.gpus.clear();
    if (!HasQuery(what, HardwareQuery::NPU))       snapshot.npus.clear();
    if (!HasQuery(what, HardwareQuery::Storage))   snapshot.storage.clear();
    if (!HasQuery(what, HardwareQuery::Network))   snapshot.network.clear();
    if (!HasQuery(what, HardwareQuery::USB))     { snapshot.usbControllers.clear();
                                                   snapshot.usbDevices.clear(); }
    if (!HasQuery(what, HardwareQuery::Bluetooth)) snapshot.bluetoothAdapters.clear();
    snapshot.captured = what;
}

void MaskIdentifiersInPlace(HardwareSnapshot& snapshot) {
    for (auto& module : snapshot.memory.modules)
        module.serialNumber = UltraCanvasHardwareInfo::MaskIdentifier(module.serialNumber);
    for (auto& device : snapshot.storage)
        device.serialNumber = UltraCanvasHardwareInfo::MaskIdentifier(device.serialNumber);
    for (auto& interfaceInfo : snapshot.network) {
        interfaceInfo.macAddress = UltraCanvasHardwareInfo::MaskIdentifier(interfaceInfo.macAddress);
        if (interfaceInfo.wifi)
            interfaceInfo.wifi->bssid = UltraCanvasHardwareInfo::MaskIdentifier(interfaceInfo.wifi->bssid);
    }
    for (auto& device : snapshot.usbDevices)
        device.serialNumber = UltraCanvasHardwareInfo::MaskIdentifier(device.serialNumber);
    for (auto& adapter : snapshot.bluetoothAdapters) {
        adapter.address = UltraCanvasHardwareInfo::MaskIdentifier(adapter.address);
        for (auto& device : adapter.devices)
            device.address = UltraCanvasHardwareInfo::MaskIdentifier(device.address);
    }
}

} // namespace

// ===== STRUCT METHODS =====

std::string CPUCacheInfo::Describe() const {
    std::ostringstream out;
    out << "L" << level << " " << UltraCanvasHardwareInfo::ToString(type);
    out << ", ";
    if (instanceCount > 1)
        out << instanceCount << " x " << UltraCanvasHardwareInfo::FormatBytes(sizeBytes);
    else
        out << UltraCanvasHardwareInfo::FormatBytes(sizeBytes);
    if (associativity > 0) out << ", " << associativity << "-way";
    else if (associativity < 0) out << ", fully associative";
    if (lineSizeBytes > 0) out << ", " << lineSizeBytes << " B line";
    return out.str();
}

uint64_t CPUInfo::TotalCacheSize(int level) const {
    uint64_t total = 0;
    for (const auto& cache : caches) {
        if (cache.level != level) continue;
        total += cache.sizeBytes * static_cast<uint64_t>(cache.instanceCount > 0 ? cache.instanceCount : 1);
    }
    return total;
}

bool CPUInfo::HasInstructionSet(const std::string& name) const {
    return std::any_of(instructionSets.begin(), instructionSets.end(),
                       [&name](const std::string& entry) {
                           if (entry.size() != name.size()) return false;
                           for (size_t i = 0; i < entry.size(); ++i) {
                               if (std::tolower(static_cast<unsigned char>(entry[i])) !=
                                   std::tolower(static_cast<unsigned char>(name[i]))) return false;
                           }
                           return true;
                       });
}

// ===== OPTIONS =====

void UltraCanvasHardwareInfo::SetOptions(const HardwareInfoOptions& options) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_options = options;
    // The cache holds values shaped by the old options (masked or not, sensors
    // or not), so it cannot answer for the new ones.
    g_cachedQuery = HardwareQuery::None;
    g_cachedAtMs = 0;
}

HardwareInfoOptions UltraCanvasHardwareInfo::GetOptions() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_options;
}

// ===== BACKEND REFLECTION =====

std::string UltraCanvasHardwareInfo::GetBackendName() {
    return HardwareInfoBackend::BackendName();
}

bool UltraCanvasHardwareInfo::IsAvailable() {
    return HardwareInfoBackend::IsAvailable();
}

// ===== CAPTURE =====

HardwareSnapshot UltraCanvasHardwareInfo::Capture(HardwareQuery what, bool forceRefresh) {
    HardwareInfoOptions options;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        options = g_options;
        // The cached snapshot answers only when it covers every category asked
        // for - HasQuery would accept a single shared bit and hand back a
        // snapshot with the rest of the request missing.
        const bool cacheCoversRequest =
            (static_cast<uint32_t>(g_cachedQuery) & static_cast<uint32_t>(what)) ==
            static_cast<uint32_t>(what);
        if (!forceRefresh && options.cacheLifetimeMs > 0 && cacheCoversRequest &&
            (NowMilliseconds() - g_cachedAtMs) < options.cacheLifetimeMs) {
            HardwareSnapshot served = g_cachedSnapshot;
            TrimSnapshotTo(served, what);
            return served;
        }
    }

    const bool sensors = options.includeSensors && HasQuery(what, HardwareQuery::Sensors);

    HardwareSnapshot snapshot;
    snapshot.capturedAtUnixSeconds = NowUnixSeconds();
    snapshot.captured = what;

    if (HasQuery(what, HardwareQuery::System))
        HardwareInfoBackend::QuerySystem(snapshot.system, snapshot.warnings);
    if (HasQuery(what, HardwareQuery::CPU))
        HardwareInfoBackend::QueryCPU(snapshot.cpu, sensors, snapshot.warnings);
    if (HasQuery(what, HardwareQuery::Memory))
        HardwareInfoBackend::QueryMemory(snapshot.memory, snapshot.warnings);
    if (HasQuery(what, HardwareQuery::GPU))
        HardwareInfoBackend::QueryGPUs(snapshot.gpus, sensors, snapshot.warnings);
    if (HasQuery(what, HardwareQuery::NPU))
        HardwareInfoBackend::QueryNPUs(snapshot.npus, sensors, snapshot.warnings);
    if (HasQuery(what, HardwareQuery::Storage))
        HardwareInfoBackend::QueryStorage(snapshot.storage, sensors, snapshot.warnings);
    if (HasQuery(what, HardwareQuery::Network))
        HardwareInfoBackend::QueryNetwork(snapshot.network, snapshot.warnings);
    if (HasQuery(what, HardwareQuery::USB))
        HardwareInfoBackend::QueryUSB(snapshot.usbControllers, snapshot.usbDevices,
                                      options.includeUsbHubs, snapshot.warnings);
    if (HasQuery(what, HardwareQuery::Bluetooth))
        HardwareInfoBackend::QueryBluetooth(snapshot.bluetoothAdapters, snapshot.warnings);

    if (options.maskIdentifiers) MaskIdentifiersInPlace(snapshot);

    // Backends report what they find; duplicated warnings from repeated probes
    // (one per drive, say) would bury the distinct ones.
    std::sort(snapshot.warnings.begin(), snapshot.warnings.end());
    snapshot.warnings.erase(std::unique(snapshot.warnings.begin(), snapshot.warnings.end()),
                            snapshot.warnings.end());

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_cachedSnapshot = snapshot;
        g_cachedQuery = what;
        g_cachedAtMs = NowMilliseconds();
    }
    return snapshot;
}

void UltraCanvasHardwareInfo::RefreshSensors(HardwareSnapshot& snapshot) {
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (!g_options.includeSensors) return;
    }
    HardwareInfoBackend::RefreshSensors(snapshot);
    snapshot.capturedAtUnixSeconds = NowUnixSeconds();
}

// ===== SINGLE-CATEGORY CONVENIENCE =====

CPUInfo UltraCanvasHardwareInfo::GetCPU() {
    return Capture(HardwareQuery::CPU | HardwareQuery::Sensors).cpu;
}
MemoryInfo UltraCanvasHardwareInfo::GetMemory() {
    return Capture(HardwareQuery::Memory).memory;
}
SystemInfo UltraCanvasHardwareInfo::GetSystem() {
    return Capture(HardwareQuery::System).system;
}
std::vector<GPUInfo> UltraCanvasHardwareInfo::ListGPUs() {
    return Capture(HardwareQuery::GPU | HardwareQuery::Sensors).gpus;
}
std::vector<NPUInfo> UltraCanvasHardwareInfo::ListNPUs() {
    return Capture(HardwareQuery::NPU | HardwareQuery::Sensors).npus;
}
std::vector<StorageDeviceInfo> UltraCanvasHardwareInfo::ListStorageDevices() {
    return Capture(HardwareQuery::Storage | HardwareQuery::Sensors).storage;
}
std::vector<NetworkInterfaceInfo> UltraCanvasHardwareInfo::ListNetworkInterfaces() {
    return Capture(HardwareQuery::Network).network;
}
std::vector<USBDeviceInfo> UltraCanvasHardwareInfo::ListUSBDevices() {
    return Capture(HardwareQuery::USB).usbDevices;
}
std::vector<USBControllerInfo> UltraCanvasHardwareInfo::ListUSBControllers() {
    return Capture(HardwareQuery::USB).usbControllers;
}
std::vector<BluetoothAdapterInfo> UltraCanvasHardwareInfo::ListBluetoothAdapters() {
    return Capture(HardwareQuery::Bluetooth).bluetoothAdapters;
}

// ===== FORMATTING =====

std::string UltraCanvasHardwareInfo::FormatBytes(uint64_t bytes) {
    // Binary multiples with the decimal names users read on a spec sheet, which
    // is what every OS information panel shows.
    static const char* units[] = { "B", "KB", "MB", "GB", "TB", "PB" };
    if (bytes < 1024ull) return std::to_string(bytes) + " B";
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) { value /= 1024.0; ++unit; }
    const int decimals = (value >= 100.0 || unit <= 1) ? 0 : (value >= 10.0 ? 1 : 2);
    return FormatDouble(value, decimals) + " " + units[unit];
}

std::string UltraCanvasHardwareInfo::FormatFrequencyMHz(double megahertz) {
    if (megahertz <= 0.0) return "-";
    if (megahertz >= 1000.0) return FormatDouble(megahertz / 1000.0, 2) + " GHz";
    return FormatDouble(megahertz, 0) + " MHz";
}

std::string UltraCanvasHardwareInfo::FormatTemperature(double celsius) {
    return FormatDouble(celsius, 1) + " \xC2\xB0" "C";
}

std::string UltraCanvasHardwareInfo::FormatBitrateMbps(double megabitsPerSecond) {
    if (megabitsPerSecond <= 0.0) return "-";
    if (megabitsPerSecond >= 1000.0) return FormatDouble(megabitsPerSecond / 1000.0, 1) + " Gb/s";
    return FormatDouble(megabitsPerSecond, 0) + " Mb/s";
}

std::string UltraCanvasHardwareInfo::FormatDuration(uint64_t seconds) {
    const uint64_t days = seconds / 86400;
    const uint64_t hours = (seconds % 86400) / 3600;
    const uint64_t minutes = (seconds % 3600) / 60;
    char buffer[64];
    if (days > 0)
        std::snprintf(buffer, sizeof(buffer), "%llu d %02llu:%02llu",
                      static_cast<unsigned long long>(days),
                      static_cast<unsigned long long>(hours),
                      static_cast<unsigned long long>(minutes));
    else
        std::snprintf(buffer, sizeof(buffer), "%02llu:%02llu",
                      static_cast<unsigned long long>(hours),
                      static_cast<unsigned long long>(minutes));
    return std::string(buffer);
}

std::string UltraCanvasHardwareInfo::MaskIdentifier(const std::string& value) {
    const std::string trimmed = TrimCopy(value);
    if (trimmed.empty()) return trimmed;

    // A MAC address is masked by octet, keeping the last two: a tail of four
    // raw characters would cut an octet in half and read as corruption.
    const bool looksLikeMac = trimmed.size() == 17 &&
                              trimmed[2] == ':' && trimmed[5] == ':' && trimmed[8] == ':' &&
                              trimmed[11] == ':' && trimmed[14] == ':';
    if (looksLikeMac) return "**:**:**:**" + trimmed.substr(11);

    // Otherwise keep the tail: enough to tell two drives apart or match a
    // label, too little to identify the machine.
    const size_t keep = trimmed.size() <= 4 ? 1 : 4;
    std::string masked(trimmed.size() - keep, '*');
    masked += trimmed.substr(trimmed.size() - keep);
    return masked;
}

// ===== ENUM NAMES =====

std::string UltraCanvasHardwareInfo::ToString(HardwareCategory category) {
    switch (category) {
        case HardwareCategory::System:    return "System";
        case HardwareCategory::CPU:       return "Processor";
        case HardwareCategory::GPU:       return "Graphics";
        case HardwareCategory::NPU:       return "AI accelerator";
        case HardwareCategory::Memory:    return "Memory";
        case HardwareCategory::Storage:   return "Storage";
        case HardwareCategory::Network:   return "Network";
        case HardwareCategory::USB:       return "USB";
        case HardwareCategory::Bluetooth: return "Bluetooth";
    }
    return "Unknown";
}

std::string UltraCanvasHardwareInfo::ToString(CPUCacheType type) {
    switch (type) {
        case CPUCacheType::Data:        return "data";
        case CPUCacheType::Instruction: return "instruction";
        case CPUCacheType::Unified:     return "unified";
        case CPUCacheType::Trace:       return "trace";
        case CPUCacheType::Unknown:     break;
    }
    return "cache";
}

std::string UltraCanvasHardwareInfo::ToString(GPUKind kind) {
    switch (kind) {
        case GPUKind::Integrated: return "Integrated";
        case GPUKind::Discrete:   return "Discrete";
        case GPUKind::Virtual:    return "Virtual";
        case GPUKind::Software:   return "Software";
        case GPUKind::Unknown:    break;
    }
    return "Unknown";
}

std::string UltraCanvasHardwareInfo::ToString(StorageBus bus) {
    switch (bus) {
        case StorageBus::NVMe:        return "NVMe";
        case StorageBus::SATA:        return "SATA";
        case StorageBus::SAS:         return "SAS";
        case StorageBus::SCSI:        return "SCSI";
        case StorageBus::IDE:         return "IDE";
        case StorageBus::USB:         return "USB";
        case StorageBus::Thunderbolt: return "Thunderbolt";
        case StorageBus::MMC:         return "eMMC";
        case StorageBus::SD:          return "SD";
        case StorageBus::Virtual:     return "Virtual";
        case StorageBus::Unknown:     break;
    }
    return "Unknown";
}

std::string UltraCanvasHardwareInfo::ToString(StorageMedia media) {
    switch (media) {
        case StorageMedia::HDD:     return "Hard disk";
        case StorageMedia::SSD:     return "Solid state";
        case StorageMedia::Optical: return "Optical";
        case StorageMedia::Flash:   return "Flash";
        case StorageMedia::RAM:     return "RAM disk";
        case StorageMedia::Unknown: break;
    }
    return "Unknown";
}

std::string UltraCanvasHardwareInfo::ToString(NetworkLinkType type) {
    switch (type) {
        case NetworkLinkType::Ethernet:  return "Ethernet";
        case NetworkLinkType::WiFi:      return "Wi-Fi";
        case NetworkLinkType::Loopback:  return "Loopback";
        case NetworkLinkType::Bluetooth: return "Bluetooth";
        case NetworkLinkType::Cellular:  return "Cellular";
        case NetworkLinkType::Virtual:   return "Virtual";
        case NetworkLinkType::Bridge:    return "Bridge";
        case NetworkLinkType::Tunnel:    return "Tunnel";
        case NetworkLinkType::Unknown:   break;
    }
    return "Unknown";
}

// ===== REPORT =====
namespace {

void AddRow(HardwarePropertyGroup& group, const std::string& name,
            const std::string& value, const std::string& tooltip = std::string()) {
    // An empty value is a value the platform could not reach; a panel full of
    // blank rows reads as broken, so those rows are simply not emitted. What is
    // missing and why is in HardwareSnapshot::warnings.
    const std::string trimmed = TrimCopy(value);
    if (trimmed.empty()) return;
    // Drivers write "unknown" into sysfs attributes they cannot answer; that is
    // an absent value wearing a word, and it belongs in the same bin as empty.
    if (trimmed == "unknown" || trimmed == "Unknown" || trimmed == "n/a" || trimmed == "N/A") return;
    HardwareProperty property;
    property.name = name;
    property.value = value;
    property.tooltip = tooltip;
    group.properties.push_back(std::move(property));
}

void AddRow(HardwarePropertyGroup& group, const std::string& name, uint64_t bytes) {
    if (bytes == 0) return;
    AddRow(group, name, UltraCanvasHardwareInfo::FormatBytes(bytes));
}

void AddCount(HardwarePropertyGroup& group, const std::string& name, long long count) {
    if (count <= 0) return;
    AddRow(group, name, std::to_string(count));
}

HardwarePropertyGroup MakeGroup(const std::string& id, const std::string& title,
                                HardwareCategory category) {
    HardwarePropertyGroup group;
    group.id = id;
    group.title = title;
    group.category = category;
    return group;
}

std::string DescribeStorageDevice(const StorageDeviceInfo& device) {
    std::string title = device.model.empty() ? device.devicePath : device.model;
    if (title.empty()) title = "Storage device";
    return title;
}

HardwarePropertyGroup BuildSystemGroup(const SystemInfo& system) {
    auto group = MakeGroup("system", "System", HardwareCategory::System);
    AddRow(group, "Host name", system.hostName);
    AddRow(group, "Operating system", system.osName);
    AddRow(group, "OS version", system.osVersion);
    AddRow(group, "Kernel", system.kernelVersion);
    AddRow(group, "Architecture", system.architecture);
    AddRow(group, "Manufacturer", system.manufacturer);
    AddRow(group, "Model", system.productName);
    AddRow(group, "Chassis", system.chassisType);
    if (!system.boardVendor.empty() || !system.boardName.empty())
        AddRow(group, "Mainboard", TrimCopy(system.boardVendor + " " + system.boardName));
    if (!system.biosVendor.empty() || !system.biosVersion.empty())
        AddRow(group, "Firmware", TrimCopy(system.biosVendor + " " + system.biosVersion +
                                           (system.biosDate.empty() ? "" : " (" + system.biosDate + ")")));
    if (system.uptimeSeconds > 0)
        AddRow(group, "Uptime", UltraCanvasHardwareInfo::FormatDuration(system.uptimeSeconds));
    return group;
}

HardwarePropertyGroup BuildCPUGroup(const CPUInfo& cpu) {
    auto group = MakeGroup("cpu", "Processor", HardwareCategory::CPU);
    AddRow(group, "Model", cpu.model);
    AddRow(group, "Vendor", cpu.vendor);
    AddRow(group, "Architecture", cpu.architecture);
    AddRow(group, "Socket", cpu.socket);
    AddRow(group, "Stepping", cpu.stepping);
    if (cpu.packages > 1) AddCount(group, "Packages", cpu.packages);
    AddCount(group, "Cores", cpu.physicalCores);
    AddCount(group, "Threads", cpu.logicalCores);
    if (cpu.baseClockMHz > 0)
        AddRow(group, "Base clock", UltraCanvasHardwareInfo::FormatFrequencyMHz(cpu.baseClockMHz));
    if (cpu.maxClockMHz > 0)
        AddRow(group, "Max clock", UltraCanvasHardwareInfo::FormatFrequencyMHz(cpu.maxClockMHz));
    if (cpu.currentClockMHz > 0)
        AddRow(group, "Current clock", UltraCanvasHardwareInfo::FormatFrequencyMHz(cpu.currentClockMHz));
    if (cpu.temperatureC)
        AddRow(group, "Temperature", UltraCanvasHardwareInfo::FormatTemperature(*cpu.temperatureC));
    if (cpu.loadPercent)
        AddRow(group, "Load", FormatDouble(*cpu.loadPercent, 1) + " %");

    if (!cpu.coreGroups.empty()) {
        auto cores = MakeGroup("cpu.cores", "Core groups", HardwareCategory::CPU);
        for (const auto& coreGroup : cpu.coreGroups) {
            std::string value = std::to_string(coreGroup.physicalCores) + " cores";
            if (coreGroup.logicalCores > 0)
                value += " / " + std::to_string(coreGroup.logicalCores) + " threads";
            if (coreGroup.maxClockMHz > 0)
                value += ", up to " + UltraCanvasHardwareInfo::FormatFrequencyMHz(coreGroup.maxClockMHz);
            AddRow(cores, coreGroup.name, value);
        }
        group.subGroups.push_back(std::move(cores));
    }

    if (!cpu.caches.empty()) {
        auto caches = MakeGroup("cpu.cache", "Cache", HardwareCategory::CPU);
        for (const auto& cache : cpu.caches) {
            std::string name = "L" + std::to_string(cache.level) + " " +
                               UltraCanvasHardwareInfo::ToString(cache.type);
            std::string value = UltraCanvasHardwareInfo::FormatBytes(cache.sizeBytes);
            if (cache.instanceCount > 1)
                value = std::to_string(cache.instanceCount) + " x " + value + "  (" +
                        UltraCanvasHardwareInfo::FormatBytes(
                            cache.sizeBytes * static_cast<uint64_t>(cache.instanceCount)) + " total)";
            AddRow(caches, name, value, cache.Describe());
        }
        group.subGroups.push_back(std::move(caches));
    }

    if (!cpu.instructionSets.empty()) {
        auto features = MakeGroup("cpu.isa", "Instruction sets", HardwareCategory::CPU);
        AddRow(features, "Supported", JoinStrings(cpu.instructionSets, ", "));
        group.subGroups.push_back(std::move(features));
    }
    return group;
}

HardwarePropertyGroup BuildMemoryGroup(const MemoryInfo& memory) {
    auto group = MakeGroup("memory", "Memory", HardwareCategory::Memory);
    AddRow(group, "Installed", memory.totalBytes);
    AddRow(group, "Available", memory.availableBytes);
    AddRow(group, "In use", memory.usedBytes);
    if (memory.totalBytes > 0 && memory.usedBytes > 0) {
        const double percent = 100.0 * static_cast<double>(memory.usedBytes) /
                               static_cast<double>(memory.totalBytes);
        AddRow(group, "Usage", FormatDouble(percent, 1) + " %");
    }
    AddRow(group, "Swap total", memory.swapTotalBytes);
    AddRow(group, "Swap free", memory.swapFreeBytes);
    AddRow(group, "Page size", memory.pageSizeBytes);
    if (memory.slotsTotal > 0)
        AddRow(group, "Slots", std::to_string(memory.slotsUsed) + " of " +
                               std::to_string(memory.slotsTotal) + " populated");

    for (size_t i = 0; i < memory.modules.size(); ++i) {
        const auto& module = memory.modules[i];
        std::string title = module.locator.empty() ? ("Module " + std::to_string(i + 1)) : module.locator;
        auto moduleGroup = MakeGroup("memory.module." + std::to_string(i), title, HardwareCategory::Memory);
        AddRow(moduleGroup, "Size", module.sizeBytes);
        AddRow(moduleGroup, "Type", module.type);
        AddRow(moduleGroup, "Form factor", module.formFactor);
        if (module.speedMTs > 0)
            AddRow(moduleGroup, "Speed", std::to_string(module.speedMTs) + " MT/s");
        if (module.ratedSpeedMTs > 0 && module.ratedSpeedMTs != module.speedMTs)
            AddRow(moduleGroup, "Rated speed", std::to_string(module.ratedSpeedMTs) + " MT/s");
        AddRow(moduleGroup, "Manufacturer", module.manufacturer);
        AddRow(moduleGroup, "Part number", module.partNumber);
        AddRow(moduleGroup, "Serial number", module.serialNumber);
        if (module.dataWidthBits > 0) {
            std::string width = std::to_string(module.dataWidthBits) + " bit";
            if (module.totalWidthBits > module.dataWidthBits) width += " + ECC";
            AddRow(moduleGroup, "Data width", width);
        }
        if (module.voltageVolts > 0)
            AddRow(moduleGroup, "Voltage", FormatDouble(module.voltageVolts, 2) + " V");
        AddRow(moduleGroup, "Bank", module.bankLocator);
        group.subGroups.push_back(std::move(moduleGroup));
    }
    return group;
}

HardwarePropertyGroup BuildGPUGroup(const std::vector<GPUInfo>& gpus) {
    auto group = MakeGroup("gpu", "Graphics", HardwareCategory::GPU);
    for (size_t i = 0; i < gpus.size(); ++i) {
        const auto& gpu = gpus[i];
        std::string title = gpu.model.empty() ? (gpu.vendor.empty() ? "Graphics adapter" : gpu.vendor)
                                              : gpu.model;
        auto adapter = MakeGroup("gpu." + std::to_string(i), title, HardwareCategory::GPU);
        AddRow(adapter, "Vendor", gpu.vendor);
        AddRow(adapter, "Model", gpu.model);
        if (gpu.kind != GPUKind::Unknown)
            AddRow(adapter, "Type", UltraCanvasHardwareInfo::ToString(gpu.kind));
        AddRow(adapter, "Driver", TrimCopy(gpu.driverName + " " + gpu.driverVersion));
        AddRow(adapter, "Video memory", gpu.videoMemoryBytes);
        if (gpu.videoMemoryUsedBytes) AddRow(adapter, "Video memory in use", *gpu.videoMemoryUsedBytes);
        AddRow(adapter, "Shared memory", gpu.sharedMemoryBytes);
        AddCount(adapter, "Compute units", gpu.computeUnits);
        if (gpu.coreClockMHz > 0)
            AddRow(adapter, "Core clock", UltraCanvasHardwareInfo::FormatFrequencyMHz(gpu.coreClockMHz));
        if (gpu.temperatureC)
            AddRow(adapter, "Temperature", UltraCanvasHardwareInfo::FormatTemperature(*gpu.temperatureC));
        if (gpu.utilizationPercent)
            AddRow(adapter, "Utilisation", FormatDouble(*gpu.utilizationPercent, 0) + " %");
        AddRow(adapter, "Bus", gpu.busId);
        if (gpu.pciVendorId || gpu.pciDeviceId) {
            char ids[32];
            std::snprintf(ids, sizeof(ids), "%04X:%04X", gpu.pciVendorId, gpu.pciDeviceId);
            AddRow(adapter, "PCI ID", ids);
        }
        group.subGroups.push_back(std::move(adapter));
    }
    return group;
}

HardwarePropertyGroup BuildNPUGroup(const std::vector<NPUInfo>& npus) {
    auto group = MakeGroup("npu", "AI accelerators", HardwareCategory::NPU);
    for (size_t i = 0; i < npus.size(); ++i) {
        const auto& npu = npus[i];
        std::string title = npu.model.empty() ? (npu.vendor.empty() ? "Accelerator" : npu.vendor) : npu.model;
        auto device = MakeGroup("npu." + std::to_string(i), title, HardwareCategory::NPU);
        AddRow(device, "Vendor", npu.vendor);
        AddRow(device, "Model", npu.model);
        AddRow(device, "Placement", npu.integrated ? "Integrated" : "Discrete");
        AddRow(device, "Driver", TrimCopy(npu.driverName + " " + npu.driverVersion));
        AddRow(device, "Runtime", npu.runtime);
        if (npu.peakTOPS > 0) AddRow(device, "Peak throughput", FormatDouble(npu.peakTOPS, 1) + " TOPS");
        if (!npu.precisions.empty()) AddRow(device, "Precisions", JoinStrings(npu.precisions, ", "));
        if (npu.temperatureC)
            AddRow(device, "Temperature", UltraCanvasHardwareInfo::FormatTemperature(*npu.temperatureC));
        if (npu.utilizationPercent)
            AddRow(device, "Utilisation", FormatDouble(*npu.utilizationPercent, 0) + " %");
        AddRow(device, "Bus", npu.busId);
        group.subGroups.push_back(std::move(device));
    }
    return group;
}

HardwarePropertyGroup BuildStorageGroup(const std::vector<StorageDeviceInfo>& devices) {
    auto group = MakeGroup("storage", "Storage", HardwareCategory::Storage);
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& device = devices[i];
        auto item = MakeGroup("storage." + std::to_string(i), DescribeStorageDevice(device),
                              HardwareCategory::Storage);
        AddRow(item, "Device", device.devicePath);
        AddRow(item, "Model", device.model);
        AddRow(item, "Vendor", device.vendor);
        AddRow(item, "Capacity", device.capacityBytes);
        if (device.media != StorageMedia::Unknown)
            AddRow(item, "Media", UltraCanvasHardwareInfo::ToString(device.media));
        if (device.bus != StorageBus::Unknown)
            AddRow(item, "Bus", UltraCanvasHardwareInfo::ToString(device.bus));
        AddRow(item, "Connector", device.connector);
        AddRow(item, "Cache", device.cacheBytes);
        if (device.rotationRateRPM > 0)
            AddRow(item, "Rotation", std::to_string(device.rotationRateRPM) + " rpm");
        if (device.temperatureC)
            AddRow(item, "Temperature", UltraCanvasHardwareInfo::FormatTemperature(*device.temperatureC));
        if (device.healthPercent)
            AddRow(item, "Health", std::to_string(*device.healthPercent) + " %");
        if (device.powerOnHours)
            AddRow(item, "Power-on hours", std::to_string(*device.powerOnHours));
        AddRow(item, "Firmware", device.firmwareVersion);
        AddRow(item, "Serial number", device.serialNumber);
        AddRow(item, "Removable", device.removable ? "Yes" : "No");
        if (device.logicalSectorSize > 0)
            AddRow(item, "Sector size", std::to_string(device.logicalSectorSize) + " B logical / " +
                                        std::to_string(device.physicalSectorSize > 0
                                                       ? device.physicalSectorSize
                                                       : device.logicalSectorSize) + " B physical");
        if (!device.volumes.empty()) {
            auto volumes = MakeGroup("storage." + std::to_string(i) + ".volumes", "Volumes",
                                     HardwareCategory::Storage);
            for (const auto& volume : device.volumes) {
                std::string value = UltraCanvasHardwareInfo::FormatBytes(volume.totalBytes);
                if (volume.freeBytes > 0)
                    value += ", " + UltraCanvasHardwareInfo::FormatBytes(volume.freeBytes) + " free";
                if (!volume.fileSystem.empty()) value += ", " + volume.fileSystem;
                if (volume.readOnly) value += ", read-only";
                AddRow(volumes, volume.mountPoint.empty() ? volume.label : volume.mountPoint, value);
            }
            item.subGroups.push_back(std::move(volumes));
        }
        group.subGroups.push_back(std::move(item));
    }
    return group;
}

HardwarePropertyGroup BuildNetworkGroup(const std::vector<NetworkInterfaceInfo>& interfaces) {
    auto group = MakeGroup("network", "Network", HardwareCategory::Network);
    for (size_t i = 0; i < interfaces.size(); ++i) {
        const auto& adapter = interfaces[i];
        std::string title = adapter.description.empty() ? adapter.name
                                                        : adapter.name + " - " + adapter.description;
        auto item = MakeGroup("network." + std::to_string(i), title, HardwareCategory::Network);
        AddRow(item, "Type", UltraCanvasHardwareInfo::ToString(adapter.type));
        AddRow(item, "State", adapter.connected ? "Connected" : (adapter.up ? "Up, no carrier" : "Down"));
        if (adapter.linkSpeedMbps > 0)
            AddRow(item, "Link speed", UltraCanvasHardwareInfo::FormatBitrateMbps(adapter.linkSpeedMbps));
        AddRow(item, "Duplex", adapter.duplex);
        AddRow(item, "MAC address", adapter.macAddress);
        AddRow(item, "Driver", adapter.driver);
        if (adapter.mtu > 0) AddRow(item, "MTU", std::to_string(adapter.mtu));
        if (!adapter.ipv4Addresses.empty())
            AddRow(item, "IPv4", JoinStrings(adapter.ipv4Addresses, ", "));
        if (!adapter.ipv6Addresses.empty())
            AddRow(item, "IPv6", JoinStrings(adapter.ipv6Addresses, ", "));
        if (adapter.bytesReceived > 0) AddRow(item, "Received", adapter.bytesReceived);
        if (adapter.bytesSent > 0) AddRow(item, "Sent", adapter.bytesSent);

        if (adapter.wifi) {
            const auto& wifi = *adapter.wifi;
            auto wireless = MakeGroup("network." + std::to_string(i) + ".wifi", "Wi-Fi",
                                      HardwareCategory::Network);
            AddRow(wireless, "Status", wifi.connected ? "Associated" : "Not associated");
            AddRow(wireless, "Network (SSID)", wifi.ssid);
            AddRow(wireless, "Access point (BSSID)", wifi.bssid);
            AddRow(wireless, "Security", wifi.security);
            AddRow(wireless, "Standard", wifi.standard);
            AddRow(wireless, "Band", wifi.band);
            if (wifi.channel > 0) AddRow(wireless, "Channel", std::to_string(wifi.channel));
            if (wifi.frequencyMHz > 0)
                AddRow(wireless, "Frequency", std::to_string(wifi.frequencyMHz) + " MHz");
            if (wifi.signalPercent > 0)
                AddRow(wireless, "Signal", std::to_string(wifi.signalPercent) + " %" +
                                           (wifi.signalDbm != 0
                                            ? " (" + std::to_string(wifi.signalDbm) + " dBm)" : ""));
            else if (wifi.signalDbm != 0)
                AddRow(wireless, "Signal", std::to_string(wifi.signalDbm) + " dBm");
            if (wifi.txRateMbps > 0)
                AddRow(wireless, "Transmit rate", UltraCanvasHardwareInfo::FormatBitrateMbps(wifi.txRateMbps));
            if (wifi.rxRateMbps > 0)
                AddRow(wireless, "Receive rate", UltraCanvasHardwareInfo::FormatBitrateMbps(wifi.rxRateMbps));
            item.subGroups.push_back(std::move(wireless));
        }
        group.subGroups.push_back(std::move(item));
    }
    return group;
}

HardwarePropertyGroup BuildUSBGroup(const std::vector<USBControllerInfo>& controllers,
                                    const std::vector<USBDeviceInfo>& devices) {
    auto group = MakeGroup("usb", "USB", HardwareCategory::USB);
    if (!controllers.empty()) {
        auto controllerGroup = MakeGroup("usb.controllers", "Controllers", HardwareCategory::USB);
        for (const auto& controller : controllers) {
            std::string value = controller.version;
            if (controller.portCount > 0)
                value += (value.empty() ? "" : ", ") + std::to_string(controller.portCount) + " ports";
            if (!controller.busId.empty()) value += (value.empty() ? "" : ", ") + controller.busId;
            AddRow(controllerGroup, controller.name.empty()
                                        ? ("Bus " + std::to_string(controller.busNumber))
                                        : controller.name,
                   value.empty() ? "present" : value, controller.driver);
        }
        group.subGroups.push_back(std::move(controllerGroup));
    }

    auto connected = MakeGroup("usb.devices", "Connected devices", HardwareCategory::USB);
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& device = devices[i];
        std::string title = device.productName;
        if (title.empty()) {
            char ids[32];
            std::snprintf(ids, sizeof(ids), "%04X:%04X", device.vendorId, device.productId);
            title = std::string("USB device ") + ids;
        }
        auto item = MakeGroup("usb.device." + std::to_string(i), title, HardwareCategory::USB);
        AddRow(item, "Vendor", device.vendorName);
        AddRow(item, "Product", device.productName);
        char ids[32];
        std::snprintf(ids, sizeof(ids), "%04X:%04X", device.vendorId, device.productId);
        AddRow(item, "USB ID", ids);
        AddRow(item, "Class", device.deviceClass);
        AddRow(item, "Speed", device.speed);
        AddRow(item, "Port", device.portPath);
        if (device.maxPowerMilliAmps > 0)
            AddRow(item, "Bus power", std::to_string(device.maxPowerMilliAmps) + " mA");
        AddRow(item, "Serial number", device.serialNumber);
        if (device.isHub) AddRow(item, "Hub", "Yes");
        connected.subGroups.push_back(std::move(item));
    }
    if (connected.subGroups.empty())
        AddRow(connected, "Devices", "None connected");
    group.subGroups.push_back(std::move(connected));
    return group;
}

HardwarePropertyGroup BuildBluetoothGroup(const std::vector<BluetoothAdapterInfo>& adapters) {
    auto group = MakeGroup("bluetooth", "Bluetooth", HardwareCategory::Bluetooth);
    for (size_t i = 0; i < adapters.size(); ++i) {
        const auto& adapter = adapters[i];
        auto item = MakeGroup("bluetooth." + std::to_string(i),
                              adapter.name.empty() ? "Bluetooth adapter" : adapter.name,
                              HardwareCategory::Bluetooth);
        AddRow(item, "Address", adapter.address);
        AddRow(item, "Manufacturer", adapter.manufacturer);
        AddRow(item, "Version", adapter.version);
        AddRow(item, "Power", adapter.powered ? "On" : "Off");
        AddRow(item, "Discoverable", adapter.discoverable ? "Yes" : "No");
        for (size_t d = 0; d < adapter.devices.size(); ++d) {
            const auto& device = adapter.devices[d];
            auto deviceGroup = MakeGroup("bluetooth." + std::to_string(i) + ".device." + std::to_string(d),
                                         device.name.empty() ? device.address : device.name,
                                         HardwareCategory::Bluetooth);
            AddRow(deviceGroup, "Address", device.address);
            AddRow(deviceGroup, "Class", device.deviceClass);
            AddRow(deviceGroup, "Connected", device.connected ? "Yes" : "No");
            AddRow(deviceGroup, "Paired", device.paired ? "Yes" : "No");
            if (device.rssiDbm) AddRow(deviceGroup, "Signal", std::to_string(*device.rssiDbm) + " dBm");
            if (device.batteryPercent)
                AddRow(deviceGroup, "Battery", std::to_string(*device.batteryPercent) + " %");
            item.subGroups.push_back(std::move(deviceGroup));
        }
        if (adapter.devices.empty()) AddRow(item, "Devices", "None connected");
        group.subGroups.push_back(std::move(item));
    }
    return group;
}

void AppendGroupText(const HardwarePropertyGroup& group, int depth, std::ostringstream& out) {
    const std::string indent(static_cast<size_t>(depth) * 2, ' ');
    out << indent << group.title << "\n";
    size_t widest = 0;
    for (const auto& property : group.properties) widest = std::max(widest, property.name.size());
    for (const auto& property : group.properties) {
        out << indent << "  " << property.name
            << std::string(widest - property.name.size() + 2, ' ')
            << property.value << "\n";
    }
    for (const auto& child : group.subGroups) AppendGroupText(child, depth + 1, out);
}

JSONValue CacheToJSON(const CPUCacheInfo& cache) {
    JSONValue value = JSONValue::MakeObject();
    value.Set("level", static_cast<int64_t>(cache.level));
    value.Set("type", UltraCanvasHardwareInfo::ToString(cache.type));
    value.Set("sizeBytes", static_cast<int64_t>(cache.sizeBytes));
    value.Set("lineSizeBytes", static_cast<int64_t>(cache.lineSizeBytes));
    value.Set("associativity", static_cast<int64_t>(cache.associativity));
    value.Set("instanceCount", static_cast<int64_t>(cache.instanceCount));
    value.Set("sharedByLogicalCores", static_cast<int64_t>(cache.sharedByLogicalCores));
    return value;
}

void SetOptionalNumber(JSONValue& object, const std::string& key, const std::optional<double>& value) {
    if (value) object.Set(key, *value);
}

} // namespace

std::vector<HardwarePropertyGroup> UltraCanvasHardwareInfo::BuildReport(const HardwareSnapshot& snapshot) {
    std::vector<HardwarePropertyGroup> groups;
    if (snapshot.Has(HardwareQuery::System))  groups.push_back(BuildSystemGroup(snapshot.system));
    if (snapshot.Has(HardwareQuery::CPU))     groups.push_back(BuildCPUGroup(snapshot.cpu));
    if (snapshot.Has(HardwareQuery::GPU))     groups.push_back(BuildGPUGroup(snapshot.gpus));
    if (snapshot.Has(HardwareQuery::NPU) && !snapshot.npus.empty())
        groups.push_back(BuildNPUGroup(snapshot.npus));
    if (snapshot.Has(HardwareQuery::Memory))  groups.push_back(BuildMemoryGroup(snapshot.memory));
    if (snapshot.Has(HardwareQuery::Storage)) groups.push_back(BuildStorageGroup(snapshot.storage));
    if (snapshot.Has(HardwareQuery::Network)) groups.push_back(BuildNetworkGroup(snapshot.network));
    if (snapshot.Has(HardwareQuery::USB) &&
        !(snapshot.usbControllers.empty() && snapshot.usbDevices.empty()))
        groups.push_back(BuildUSBGroup(snapshot.usbControllers, snapshot.usbDevices));
    if (snapshot.Has(HardwareQuery::Bluetooth) && !snapshot.bluetoothAdapters.empty())
        groups.push_back(BuildBluetoothGroup(snapshot.bluetoothAdapters));

    // A group with neither rows nor children says nothing; drop it rather than
    // show an empty heading.
    groups.erase(std::remove_if(groups.begin(), groups.end(),
                                [](const HardwarePropertyGroup& group) {
                                    return group.properties.empty() && group.subGroups.empty();
                                }),
                 groups.end());

    if (!snapshot.warnings.empty()) {
        auto notes = MakeGroup("notes", "Notes", HardwareCategory::System);
        for (size_t i = 0; i < snapshot.warnings.size(); ++i)
            AddRow(notes, std::to_string(i + 1), snapshot.warnings[i]);
        groups.push_back(std::move(notes));
    }
    return groups;
}

std::string UltraCanvasHardwareInfo::ToText(const HardwareSnapshot& snapshot) {
    std::ostringstream out;
    for (const auto& group : BuildReport(snapshot)) {
        AppendGroupText(group, 0, out);
        out << "\n";
    }
    return out.str();
}

std::string UltraCanvasHardwareInfo::ToJSON(const HardwareSnapshot& snapshot) {
    JSONValue root = JSONValue::MakeObject();
    root.Set("backend", GetBackendName());
    root.Set("capturedAt", static_cast<int64_t>(snapshot.capturedAtUnixSeconds));

    if (snapshot.Has(HardwareQuery::System)) {
        JSONValue system = JSONValue::MakeObject();
        system.Set("hostName", snapshot.system.hostName);
        system.Set("osName", snapshot.system.osName);
        system.Set("osVersion", snapshot.system.osVersion);
        system.Set("kernelVersion", snapshot.system.kernelVersion);
        system.Set("architecture", snapshot.system.architecture);
        system.Set("manufacturer", snapshot.system.manufacturer);
        system.Set("productName", snapshot.system.productName);
        system.Set("boardVendor", snapshot.system.boardVendor);
        system.Set("boardName", snapshot.system.boardName);
        system.Set("biosVendor", snapshot.system.biosVendor);
        system.Set("biosVersion", snapshot.system.biosVersion);
        system.Set("biosDate", snapshot.system.biosDate);
        system.Set("chassisType", snapshot.system.chassisType);
        system.Set("uptimeSeconds", static_cast<int64_t>(snapshot.system.uptimeSeconds));
        root.Set("system", std::move(system));
    }

    if (snapshot.Has(HardwareQuery::CPU)) {
        const auto& cpu = snapshot.cpu;
        JSONValue value = JSONValue::MakeObject();
        value.Set("vendor", cpu.vendor);
        value.Set("model", cpu.model);
        value.Set("architecture", cpu.architecture);
        value.Set("socket", cpu.socket);
        value.Set("stepping", cpu.stepping);
        value.Set("packages", static_cast<int64_t>(cpu.packages));
        value.Set("physicalCores", static_cast<int64_t>(cpu.physicalCores));
        value.Set("logicalCores", static_cast<int64_t>(cpu.logicalCores));
        value.Set("baseClockMHz", cpu.baseClockMHz);
        value.Set("maxClockMHz", cpu.maxClockMHz);
        value.Set("currentClockMHz", cpu.currentClockMHz);
        SetOptionalNumber(value, "temperatureC", cpu.temperatureC);
        SetOptionalNumber(value, "loadPercent", cpu.loadPercent);
        JSONValue caches = JSONValue::MakeArray();
        for (const auto& cache : cpu.caches) caches.Append(CacheToJSON(cache));
        value.Set("caches", std::move(caches));
        JSONValue coreGroups = JSONValue::MakeArray();
        for (const auto& coreGroup : cpu.coreGroups) {
            JSONValue entry = JSONValue::MakeObject();
            entry.Set("name", coreGroup.name);
            entry.Set("physicalCores", static_cast<int64_t>(coreGroup.physicalCores));
            entry.Set("logicalCores", static_cast<int64_t>(coreGroup.logicalCores));
            entry.Set("maxClockMHz", coreGroup.maxClockMHz);
            coreGroups.Append(std::move(entry));
        }
        value.Set("coreGroups", std::move(coreGroups));
        JSONValue isa = JSONValue::MakeArray();
        for (const auto& feature : cpu.instructionSets) isa.Append(JSONValue(feature));
        value.Set("instructionSets", std::move(isa));
        root.Set("cpu", std::move(value));
    }

    if (snapshot.Has(HardwareQuery::Memory)) {
        const auto& memory = snapshot.memory;
        JSONValue value = JSONValue::MakeObject();
        value.Set("totalBytes", static_cast<int64_t>(memory.totalBytes));
        value.Set("availableBytes", static_cast<int64_t>(memory.availableBytes));
        value.Set("usedBytes", static_cast<int64_t>(memory.usedBytes));
        value.Set("swapTotalBytes", static_cast<int64_t>(memory.swapTotalBytes));
        value.Set("swapFreeBytes", static_cast<int64_t>(memory.swapFreeBytes));
        value.Set("pageSizeBytes", static_cast<int64_t>(memory.pageSizeBytes));
        value.Set("slotsTotal", static_cast<int64_t>(memory.slotsTotal));
        value.Set("slotsUsed", static_cast<int64_t>(memory.slotsUsed));
        JSONValue modules = JSONValue::MakeArray();
        for (const auto& module : memory.modules) {
            JSONValue entry = JSONValue::MakeObject();
            entry.Set("locator", module.locator);
            entry.Set("type", module.type);
            entry.Set("formFactor", module.formFactor);
            entry.Set("manufacturer", module.manufacturer);
            entry.Set("partNumber", module.partNumber);
            entry.Set("serialNumber", module.serialNumber);
            entry.Set("sizeBytes", static_cast<int64_t>(module.sizeBytes));
            entry.Set("speedMTs", static_cast<int64_t>(module.speedMTs));
            entry.Set("ratedSpeedMTs", static_cast<int64_t>(module.ratedSpeedMTs));
            entry.Set("dataWidthBits", static_cast<int64_t>(module.dataWidthBits));
            entry.Set("totalWidthBits", static_cast<int64_t>(module.totalWidthBits));
            modules.Append(std::move(entry));
        }
        value.Set("modules", std::move(modules));
        root.Set("memory", std::move(value));
    }

    if (snapshot.Has(HardwareQuery::GPU)) {
        JSONValue gpus = JSONValue::MakeArray();
        for (const auto& gpu : snapshot.gpus) {
            JSONValue entry = JSONValue::MakeObject();
            entry.Set("vendor", gpu.vendor);
            entry.Set("model", gpu.model);
            entry.Set("kind", ToString(gpu.kind));
            entry.Set("driverName", gpu.driverName);
            entry.Set("driverVersion", gpu.driverVersion);
            entry.Set("busId", gpu.busId);
            entry.Set("videoMemoryBytes", static_cast<int64_t>(gpu.videoMemoryBytes));
            entry.Set("sharedMemoryBytes", static_cast<int64_t>(gpu.sharedMemoryBytes));
            entry.Set("computeUnits", static_cast<int64_t>(gpu.computeUnits));
            entry.Set("coreClockMHz", gpu.coreClockMHz);
            SetOptionalNumber(entry, "temperatureC", gpu.temperatureC);
            SetOptionalNumber(entry, "utilizationPercent", gpu.utilizationPercent);
            gpus.Append(std::move(entry));
        }
        root.Set("gpus", std::move(gpus));
    }

    if (snapshot.Has(HardwareQuery::NPU)) {
        JSONValue npus = JSONValue::MakeArray();
        for (const auto& npu : snapshot.npus) {
            JSONValue entry = JSONValue::MakeObject();
            entry.Set("vendor", npu.vendor);
            entry.Set("model", npu.model);
            entry.Set("driverName", npu.driverName);
            entry.Set("driverVersion", npu.driverVersion);
            entry.Set("runtime", npu.runtime);
            entry.Set("busId", npu.busId);
            entry.Set("integrated", npu.integrated);
            entry.Set("peakTOPS", npu.peakTOPS);
            SetOptionalNumber(entry, "temperatureC", npu.temperatureC);
            SetOptionalNumber(entry, "utilizationPercent", npu.utilizationPercent);
            npus.Append(std::move(entry));
        }
        root.Set("npus", std::move(npus));
    }

    if (snapshot.Has(HardwareQuery::Storage)) {
        JSONValue devices = JSONValue::MakeArray();
        for (const auto& device : snapshot.storage) {
            JSONValue entry = JSONValue::MakeObject();
            entry.Set("devicePath", device.devicePath);
            entry.Set("model", device.model);
            entry.Set("vendor", device.vendor);
            entry.Set("firmwareVersion", device.firmwareVersion);
            entry.Set("serialNumber", device.serialNumber);
            entry.Set("bus", ToString(device.bus));
            entry.Set("media", ToString(device.media));
            entry.Set("connector", device.connector);
            entry.Set("capacityBytes", static_cast<int64_t>(device.capacityBytes));
            entry.Set("cacheBytes", static_cast<int64_t>(device.cacheBytes));
            entry.Set("rotationRateRPM", static_cast<int64_t>(device.rotationRateRPM));
            entry.Set("removable", device.removable);
            SetOptionalNumber(entry, "temperatureC", device.temperatureC);
            if (device.healthPercent) entry.Set("healthPercent", static_cast<int64_t>(*device.healthPercent));
            if (device.powerOnHours) entry.Set("powerOnHours", static_cast<int64_t>(*device.powerOnHours));
            JSONValue volumes = JSONValue::MakeArray();
            for (const auto& volume : device.volumes) {
                JSONValue item = JSONValue::MakeObject();
                item.Set("mountPoint", volume.mountPoint);
                item.Set("fileSystem", volume.fileSystem);
                item.Set("label", volume.label);
                item.Set("totalBytes", static_cast<int64_t>(volume.totalBytes));
                item.Set("freeBytes", static_cast<int64_t>(volume.freeBytes));
                item.Set("readOnly", volume.readOnly);
                volumes.Append(std::move(item));
            }
            entry.Set("volumes", std::move(volumes));
            devices.Append(std::move(entry));
        }
        root.Set("storage", std::move(devices));
    }

    if (snapshot.Has(HardwareQuery::Network)) {
        JSONValue interfaces = JSONValue::MakeArray();
        for (const auto& adapter : snapshot.network) {
            JSONValue entry = JSONValue::MakeObject();
            entry.Set("name", adapter.name);
            entry.Set("description", adapter.description);
            entry.Set("macAddress", adapter.macAddress);
            entry.Set("driver", adapter.driver);
            entry.Set("type", ToString(adapter.type));
            entry.Set("up", adapter.up);
            entry.Set("connected", adapter.connected);
            entry.Set("linkSpeedMbps", adapter.linkSpeedMbps);
            entry.Set("mtu", static_cast<int64_t>(adapter.mtu));
            JSONValue ipv4 = JSONValue::MakeArray();
            for (const auto& address : adapter.ipv4Addresses) ipv4.Append(JSONValue(address));
            entry.Set("ipv4", std::move(ipv4));
            JSONValue ipv6 = JSONValue::MakeArray();
            for (const auto& address : adapter.ipv6Addresses) ipv6.Append(JSONValue(address));
            entry.Set("ipv6", std::move(ipv6));
            if (adapter.wifi) {
                JSONValue wifi = JSONValue::MakeObject();
                wifi.Set("connected", adapter.wifi->connected);
                wifi.Set("ssid", adapter.wifi->ssid);
                wifi.Set("bssid", adapter.wifi->bssid);
                wifi.Set("security", adapter.wifi->security);
                wifi.Set("standard", adapter.wifi->standard);
                wifi.Set("band", adapter.wifi->band);
                wifi.Set("channel", static_cast<int64_t>(adapter.wifi->channel));
                wifi.Set("frequencyMHz", static_cast<int64_t>(adapter.wifi->frequencyMHz));
                wifi.Set("signalPercent", static_cast<int64_t>(adapter.wifi->signalPercent));
                wifi.Set("signalDbm", static_cast<int64_t>(adapter.wifi->signalDbm));
                wifi.Set("txRateMbps", adapter.wifi->txRateMbps);
                wifi.Set("rxRateMbps", adapter.wifi->rxRateMbps);
                entry.Set("wifi", std::move(wifi));
            }
            interfaces.Append(std::move(entry));
        }
        root.Set("network", std::move(interfaces));
    }

    if (snapshot.Has(HardwareQuery::USB)) {
        JSONValue controllers = JSONValue::MakeArray();
        for (const auto& controller : snapshot.usbControllers) {
            JSONValue entry = JSONValue::MakeObject();
            entry.Set("name", controller.name);
            entry.Set("version", controller.version);
            entry.Set("driver", controller.driver);
            entry.Set("busId", controller.busId);
            entry.Set("busNumber", static_cast<int64_t>(controller.busNumber));
            entry.Set("portCount", static_cast<int64_t>(controller.portCount));
            controllers.Append(std::move(entry));
        }
        root.Set("usbControllers", std::move(controllers));

        JSONValue devices = JSONValue::MakeArray();
        for (const auto& device : snapshot.usbDevices) {
            JSONValue entry = JSONValue::MakeObject();
            char ids[32];
            std::snprintf(ids, sizeof(ids), "%04X:%04X", device.vendorId, device.productId);
            entry.Set("id", ids);
            entry.Set("vendorName", device.vendorName);
            entry.Set("productName", device.productName);
            entry.Set("serialNumber", device.serialNumber);
            entry.Set("deviceClass", device.deviceClass);
            entry.Set("speed", device.speed);
            entry.Set("portPath", device.portPath);
            entry.Set("busNumber", static_cast<int64_t>(device.busNumber));
            entry.Set("deviceAddress", static_cast<int64_t>(device.deviceAddress));
            entry.Set("maxPowerMilliAmps", static_cast<int64_t>(device.maxPowerMilliAmps));
            entry.Set("isHub", device.isHub);
            devices.Append(std::move(entry));
        }
        root.Set("usbDevices", std::move(devices));
    }

    if (snapshot.Has(HardwareQuery::Bluetooth)) {
        JSONValue adapters = JSONValue::MakeArray();
        for (const auto& adapter : snapshot.bluetoothAdapters) {
            JSONValue entry = JSONValue::MakeObject();
            entry.Set("name", adapter.name);
            entry.Set("address", adapter.address);
            entry.Set("manufacturer", adapter.manufacturer);
            entry.Set("version", adapter.version);
            entry.Set("powered", adapter.powered);
            entry.Set("discoverable", adapter.discoverable);
            JSONValue devices = JSONValue::MakeArray();
            for (const auto& device : adapter.devices) {
                JSONValue item = JSONValue::MakeObject();
                item.Set("name", device.name);
                item.Set("address", device.address);
                item.Set("deviceClass", device.deviceClass);
                item.Set("paired", device.paired);
                item.Set("connected", device.connected);
                item.Set("trusted", device.trusted);
                if (device.rssiDbm) item.Set("rssiDbm", static_cast<int64_t>(*device.rssiDbm));
                if (device.batteryPercent)
                    item.Set("batteryPercent", static_cast<int64_t>(*device.batteryPercent));
                devices.Append(std::move(item));
            }
            entry.Set("devices", std::move(devices));
            adapters.Append(std::move(entry));
        }
        root.Set("bluetoothAdapters", std::move(adapters));
    }

    JSONValue warnings = JSONValue::MakeArray();
    for (const auto& warning : snapshot.warnings) warnings.Append(JSONValue(warning));
    root.Set("warnings", std::move(warnings));

    JSONSerializeOptions options;
    options.pretty = true;
    options.indentWidth = 2;
    return JSON::Serialize(root, options);
}

// ===== FALLBACK BACKEND =====
// Platforms without a native probe (WASM, Android, the BSDs) still answer, with
// what the C++ runtime itself knows plus a note naming what is unavailable and
// why - so a caller never has to special-case the platform it runs on.
#ifndef ULTRACANVAS_HARDWAREINFO_NATIVE
namespace HardwareInfoBackend {

namespace {
const char* kNoBackend =
    "No native hardware probe is compiled for this platform; only values the C++ "
    "runtime reports itself are available.";
}

std::string BackendName() { return "null"; }
bool IsAvailable() { return false; }

void QuerySystem(SystemInfo& out, std::vector<std::string>& warnings) {
    (void)out;
    warnings.push_back(kNoBackend);
}

void QueryCPU(CPUInfo& out, bool includeSensors, std::vector<std::string>& warnings) {
    (void)includeSensors;
    // std::thread::hardware_concurrency() is the one hardware fact the standard
    // library guarantees, and it is worth more than an empty panel.
    out.logicalCores = static_cast<int>(std::thread::hardware_concurrency());
    warnings.push_back(kNoBackend);
}

void QueryMemory(MemoryInfo& out, std::vector<std::string>& warnings) {
    (void)out;
    warnings.push_back(kNoBackend);
}

void QueryGPUs(std::vector<GPUInfo>& out, bool includeSensors, std::vector<std::string>& warnings) {
    (void)out; (void)includeSensors;
    warnings.push_back(kNoBackend);
}

void QueryNPUs(std::vector<NPUInfo>& out, bool includeSensors, std::vector<std::string>& warnings) {
    (void)out; (void)includeSensors;
    warnings.push_back(kNoBackend);
}

void QueryStorage(std::vector<StorageDeviceInfo>& out, bool includeSensors,
                  std::vector<std::string>& warnings) {
    (void)out; (void)includeSensors;
    warnings.push_back(kNoBackend);
}

void QueryNetwork(std::vector<NetworkInterfaceInfo>& out, std::vector<std::string>& warnings) {
    (void)out;
    warnings.push_back(kNoBackend);
}

void QueryUSB(std::vector<USBControllerInfo>& controllers, std::vector<USBDeviceInfo>& devices,
              bool includeHubs, std::vector<std::string>& warnings) {
    (void)controllers; (void)devices; (void)includeHubs;
    warnings.push_back(kNoBackend);
}

void QueryBluetooth(std::vector<BluetoothAdapterInfo>& out, std::vector<std::string>& warnings) {
    (void)out;
    warnings.push_back(kNoBackend);
}

void RefreshSensors(HardwareSnapshot& snapshot) { (void)snapshot; }

} // namespace HardwareInfoBackend
#endif // !ULTRACANVAS_HARDWAREINFO_NATIVE

} // namespace UltraCanvas
