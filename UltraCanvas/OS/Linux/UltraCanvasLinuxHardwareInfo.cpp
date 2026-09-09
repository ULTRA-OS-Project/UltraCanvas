// OS/Linux/UltraCanvasLinuxHardwareInfo.cpp
// Linux backend for UltraCanvasHardwareInfo. Everything is read from procfs and
// sysfs - no daemon, no helper process, no extra library - so the probe works on
// a minimal system and inside a container, and degrades to a warning wherever
// the kernel keeps a value behind root (SMBIOS tables, SMART, ATA IDENTIFY).
// Version: 0.1.0
// Last Modified: 2026-08-29
// Author: UltraCanvas Framework

#include "UltraCanvasHardwareInfoBackend.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <limits.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>

#if __has_include(<linux/wireless.h>)
    #include <linux/wireless.h>
    #define ULTRACANVAS_HAS_WIRELESS_EXT 1
#endif
#if __has_include(<linux/hdreg.h>)
    #include <linux/hdreg.h>
    #define ULTRACANVAS_HAS_HDREG 1
#endif

namespace UltraCanvas {
namespace HardwareInfoBackend {
namespace {

// ===== SYSFS / PROCFS PRIMITIVES =====

std::string Trim(const std::string& text) {
    size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string ReadTextFile(const std::string& path) {
    std::ifstream stream(path);
    if (!stream) return std::string();
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return Trim(buffer.str());
}

std::vector<std::string> ReadLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream stream(path);
    if (!stream) return lines;
    std::string line;
    while (std::getline(stream, line)) lines.push_back(line);
    return lines;
}

bool PathExists(const std::string& path) {
    return ::access(path.c_str(), F_OK) == 0;
}

uint64_t ReadUnsigned(const std::string& path, uint64_t fallback = 0) {
    const std::string text = ReadTextFile(path);
    if (text.empty()) return fallback;
    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 0);
    if (end == text.c_str() || errno != 0) return fallback;
    return static_cast<uint64_t>(value);
}

std::vector<std::string> ListDirectory(const std::string& path) {
    std::vector<std::string> entries;
    DIR* dir = ::opendir(path.c_str());
    if (!dir) return entries;
    while (dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        entries.push_back(name);
    }
    ::closedir(dir);
    std::sort(entries.begin(), entries.end());
    return entries;
}

std::string ResolvePath(const std::string& path) {
    char resolved[PATH_MAX];
    if (::realpath(path.c_str(), resolved)) return std::string(resolved);
    return std::string();
}

std::string BaseName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool StartsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

std::string ToUpperCopy(std::string text) {
    for (char& character : text)
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    return text;
}

// "0-3,8" -> {0,1,2,3,8}
std::vector<int> ParseCPUList(const std::string& list) {
    std::vector<int> values;
    std::stringstream stream(list);
    std::string part;
    while (std::getline(stream, part, ',')) {
        const size_t dash = part.find('-');
        if (dash == std::string::npos) {
            if (!Trim(part).empty()) values.push_back(std::atoi(part.c_str()));
        } else {
            const int from = std::atoi(part.substr(0, dash).c_str());
            const int to = std::atoi(part.substr(dash + 1).c_str());
            for (int value = from; value <= to; ++value) values.push_back(value);
        }
    }
    return values;
}

// sysfs cache sizes are written "32K", "1280K", "16M".
uint64_t ParseSizeWithSuffix(const std::string& text) {
    if (text.empty()) return 0;
    const uint64_t number = std::strtoull(text.c_str(), nullptr, 10);
    const char suffix = static_cast<char>(std::toupper(static_cast<unsigned char>(text.back())));
    switch (suffix) {
        case 'K': return number * 1024ull;
        case 'M': return number * 1024ull * 1024ull;
        case 'G': return number * 1024ull * 1024ull * 1024ull;
        default:  return number;
    }
}

// ===== pci.ids / usb.ids =====
// The same file format backs both: a vendor line at column 0, its devices
// indented by one tab. Loaded once, on the first lookup that needs it, and only
// when the kernel did not already hand us a name.
struct IdsDatabase {
    bool loaded = false;
    std::unordered_map<uint32_t, std::string> vendors;                 // vendorId
    std::unordered_map<uint32_t, std::string> devices;                 // vendorId << 16 | deviceId
};

IdsDatabase& PciIds() { static IdsDatabase database; return database; }
IdsDatabase& UsbIds() { static IdsDatabase database; return database; }

void LoadIdsFile(IdsDatabase& database, const std::vector<std::string>& candidatePaths) {
    if (database.loaded) return;
    database.loaded = true;
    for (const auto& path : candidatePaths) {
        std::ifstream stream(path);
        if (!stream) continue;
        std::string line;
        uint32_t currentVendor = 0;
        bool haveVendor = false;
        while (std::getline(stream, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (line[0] != '\t') {
                if (line.size() < 6) continue;
                char* end = nullptr;
                const unsigned long id = std::strtoul(line.substr(0, 4).c_str(), &end, 16);
                if (end == nullptr || *end != '\0') continue;
                currentVendor = static_cast<uint32_t>(id);
                haveVendor = true;
                database.vendors[currentVendor] = Trim(line.substr(4));
            } else if (line.size() > 1 && line[1] != '\t' && haveVendor) {
                if (line.size() < 7) continue;
                char* end = nullptr;
                const unsigned long id = std::strtoul(line.substr(1, 4).c_str(), &end, 16);
                if (end == nullptr || *end != '\0') continue;
                database.devices[(currentVendor << 16) | static_cast<uint32_t>(id)] =
                    Trim(line.substr(5));
            }
        }
        return; // first readable file wins
    }
}

void LookupPciNames(uint16_t vendorId, uint16_t deviceId,
                    std::string& vendorName, std::string& deviceName) {
    LoadIdsFile(PciIds(), { "/usr/share/hwdata/pci.ids",
                            "/usr/share/misc/pci.ids",
                            "/usr/share/pci.ids" });
    auto& database = PciIds();
    auto vendorIt = database.vendors.find(vendorId);
    if (vendorIt != database.vendors.end()) vendorName = vendorIt->second;
    auto deviceIt = database.devices.find((static_cast<uint32_t>(vendorId) << 16) | deviceId);
    if (deviceIt != database.devices.end()) deviceName = deviceIt->second;
}

void LookupUsbNames(uint16_t vendorId, uint16_t productId,
                    std::string& vendorName, std::string& productName) {
    LoadIdsFile(UsbIds(), { "/usr/share/hwdata/usb.ids",
                            "/usr/share/misc/usb.ids",
                            "/var/lib/usbutils/usb.ids" });
    auto& database = UsbIds();
    auto vendorIt = database.vendors.find(vendorId);
    if (vendorIt != database.vendors.end()) vendorName = vendorIt->second;
    auto productIt = database.devices.find((static_cast<uint32_t>(vendorId) << 16) | productId);
    if (productIt != database.devices.end()) productName = productIt->second;
}

uint16_t ParseHexId(const std::string& text) {
    if (text.empty()) return 0;
    return static_cast<uint16_t>(std::strtoul(text.c_str(), nullptr, 16));
}

// ===== hwmon =====
// One pass over /sys/class/hwmon, keyed by the real path of the device each
// sensor belongs to. Drive and GPU temperatures are then a lookup on any
// ancestor of the device's own sysfs path, which is how a drive under an NVMe
// controller and a GPU under its PCI function both resolve.
struct HwmonEntry {
    std::string name;        // "coretemp", "nvme", "drivetemp", "amdgpu"
    std::string devicePath;  // resolved sysfs path of the owning device, may be empty
    std::string hwmonPath;
};

const std::vector<HwmonEntry>& HwmonEntries() {
    static std::vector<HwmonEntry> entries;
    static bool scanned = false;
    if (scanned) return entries;
    scanned = true;
    const std::string root = "/sys/class/hwmon";
    for (const auto& name : ListDirectory(root)) {
        HwmonEntry entry;
        entry.hwmonPath = root + "/" + name;
        entry.name = ReadTextFile(entry.hwmonPath + "/name");
        entry.devicePath = ResolvePath(entry.hwmonPath + "/device");
        entries.push_back(std::move(entry));
    }
    return entries;
}

// First temperature input of a hwmon node, in degrees Celsius. Prefers a label
// naming a package/composite sensor over a per-core one.
bool ReadHwmonTemperature(const std::string& hwmonPath, double& outCelsius) {
    struct Candidate { int index; bool preferred; };
    std::vector<Candidate> candidates;
    for (int index = 1; index <= 8; ++index) {
        const std::string input = hwmonPath + "/temp" + std::to_string(index) + "_input";
        if (!PathExists(input)) continue;
        const std::string label = ToUpperCopy(
            ReadTextFile(hwmonPath + "/temp" + std::to_string(index) + "_label"));
        const bool preferred = label.find("PACKAGE") != std::string::npos ||
                               label.find("COMPOSITE") != std::string::npos ||
                               label.find("TCTL") != std::string::npos ||
                               label.find("TDIE") != std::string::npos ||
                               label.find("EDGE") != std::string::npos;
        candidates.push_back({ index, preferred });
    }
    if (candidates.empty()) return false;
    int chosen = candidates.front().index;
    for (const auto& candidate : candidates) {
        if (candidate.preferred) { chosen = candidate.index; break; }
    }
    const std::string text = ReadTextFile(hwmonPath + "/temp" + std::to_string(chosen) + "_input");
    if (text.empty()) return false;
    outCelsius = std::strtod(text.c_str(), nullptr) / 1000.0;
    return true;
}

// Temperature of the hwmon node owned by `devicePath` or any of its ancestors.
bool TemperatureForDevice(const std::string& devicePath, double& outCelsius) {
    if (devicePath.empty()) return false;
    for (const auto& entry : HwmonEntries()) {
        if (entry.devicePath.empty()) continue;
        if (StartsWith(devicePath, entry.devicePath)) {
            if (ReadHwmonTemperature(entry.hwmonPath, outCelsius)) return true;
        }
    }
    return false;
}

bool ReadCPUTemperature(double& outCelsius) {
    static const char* kCpuSensors[] = { "coretemp", "k10temp", "zenpower", "cpu_thermal",
                                         "soc_thermal", "cpu-thermal", "acpitz" };
    for (const char* sensor : kCpuSensors) {
        for (const auto& entry : HwmonEntries()) {
            if (entry.name != sensor) continue;
            if (ReadHwmonTemperature(entry.hwmonPath, outCelsius)) return true;
        }
    }
    // Thermal zones are the ARM/embedded path, where hwmon is often absent.
    for (const auto& zone : ListDirectory("/sys/class/thermal")) {
        if (!StartsWith(zone, "thermal_zone")) continue;
        const std::string path = "/sys/class/thermal/" + zone;
        const std::string type = ToUpperCopy(ReadTextFile(path + "/type"));
        if (type.find("CPU") == std::string::npos && type.find("PKG") == std::string::npos &&
            type.find("SOC") == std::string::npos) continue;
        const std::string text = ReadTextFile(path + "/temp");
        if (text.empty()) continue;
        outCelsius = std::strtod(text.c_str(), nullptr) / 1000.0;
        return true;
    }
    return false;
}

} // namespace

// ===== BACKEND IDENTITY =====

std::string BackendName() { return "sysfs"; }
bool IsAvailable() { return PathExists("/sys/devices/system/cpu") || PathExists("/proc/cpuinfo"); }

// ===== SYSTEM =====

void QuerySystem(SystemInfo& out, std::vector<std::string>& warnings) {
    char hostName[256] = {0};
    if (::gethostname(hostName, sizeof(hostName) - 1) == 0) out.hostName = hostName;

    utsname uts{};
    if (::uname(&uts) == 0) {
        out.kernelVersion = std::string(uts.sysname) + " " + uts.release;
        out.architecture = uts.machine;
    }

    // /etc/os-release is the distribution's own name for itself; without it the
    // kernel name is all there is.
    for (const auto& line : ReadLines("/etc/os-release")) {
        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        if (key == "PRETTY_NAME") out.osName = value;
        else if (key == "VERSION_ID") out.osVersion = value;
    }
    if (out.osName.empty()) out.osName = uts.sysname;

    const std::string dmi = "/sys/class/dmi/id";
    out.manufacturer = ReadTextFile(dmi + "/sys_vendor");
    out.productName  = ReadTextFile(dmi + "/product_name");
    out.boardVendor  = ReadTextFile(dmi + "/board_vendor");
    out.boardName    = ReadTextFile(dmi + "/board_name");
    out.biosVendor   = ReadTextFile(dmi + "/bios_vendor");
    out.biosVersion  = ReadTextFile(dmi + "/bios_version");
    out.biosDate     = ReadTextFile(dmi + "/bios_date");

    static const char* kChassisTypes[] = {
        "Undefined", "Other", "Unknown", "Desktop", "Low Profile Desktop", "Pizza Box",
        "Mini Tower", "Tower", "Portable", "Laptop", "Notebook", "Hand Held",
        "Docking Station", "All In One", "Sub Notebook", "Space-saving", "Lunch Box",
        "Main Server Chassis", "Expansion Chassis", "Sub Chassis", "Bus Expansion Chassis",
        "Peripheral Chassis", "RAID Chassis", "Rack Mount Chassis", "Sealed-case PC",
        "Multi-system", "CompactPCI", "AdvancedTCA", "Blade", "Blade Enclosure", "Tablet",
        "Convertible", "Detachable", "IoT Gateway", "Embedded PC", "Mini PC", "Stick PC"
    };
    const uint64_t chassis = ReadUnsigned(dmi + "/chassis_type", 0);
    if (chassis > 0 && chassis < sizeof(kChassisTypes) / sizeof(kChassisTypes[0]))
        out.chassisType = kChassisTypes[chassis];
    // A hypervisor names itself in the product string; say so plainly rather
    // than reporting a "Desktop" that has no case.
    const std::string product = ToUpperCopy(out.productName);
    if (product.find("VIRTUAL") != std::string::npos || product.find("KVM") != std::string::npos ||
        product.find("VMWARE") != std::string::npos || product.find("QEMU") != std::string::npos)
        out.chassisType = "Virtual machine";

    const std::string uptime = ReadTextFile("/proc/uptime");
    if (!uptime.empty()) out.uptimeSeconds = static_cast<uint64_t>(std::strtod(uptime.c_str(), nullptr));

    if (out.manufacturer.empty() && out.productName.empty())
        warnings.push_back("System identity (vendor, model, firmware) is unavailable: this kernel "
                           "exposes no DMI tables under /sys/class/dmi/id.");
}

// ===== CPU =====
namespace {

struct CpuInfoFields {
    std::map<std::string, std::string> firstBlock; // fields of the first logical CPU
    std::vector<double> clocksMHz;                 // "cpu MHz" per logical CPU
    std::set<std::pair<int, int>> physicalCores;   // (package, core)
    int logicalCount = 0;
};

CpuInfoFields ReadProcCpuInfo() {
    CpuInfoFields fields;
    int package = 0;
    int core = -1;
    bool haveCore = false;
    bool firstBlockDone = false;
    std::map<std::string, std::string> current;

    auto flushBlock = [&]() {
        if (current.empty()) return;
        ++fields.logicalCount;
        if (!firstBlockDone) { fields.firstBlock = current; firstBlockDone = true; }
        if (haveCore) fields.physicalCores.insert({ package, core });
        current.clear();
        haveCore = false;
        package = 0;
        core = -1;
    };

    for (const auto& line : ReadLines("/proc/cpuinfo")) {
        if (Trim(line).empty()) { flushBlock(); continue; }
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, colon));
        const std::string value = Trim(line.substr(colon + 1));
        current[key] = value;
        if (key == "cpu MHz") fields.clocksMHz.push_back(std::strtod(value.c_str(), nullptr));
        else if (key == "physical id") package = std::atoi(value.c_str());
        else if (key == "core id") { core = std::atoi(value.c_str()); haveCore = true; }
    }
    flushBlock();
    return fields;
}

// The flags worth naming on a panel: the ones an application actually branches
// on. The full flag list is hundreds of entries of kernel trivia.
std::vector<std::string> SelectInstructionSets(const std::string& flags) {
    static const std::pair<const char*, const char*> kInteresting[] = {
        { "sse2", "SSE2" }, { "sse4_1", "SSE4.1" }, { "sse4_2", "SSE4.2" },
        { "avx", "AVX" }, { "avx2", "AVX2" }, { "avx512f", "AVX-512" },
        { "avx512bw", "AVX-512 BW" }, { "avx_vnni", "AVX-VNNI" }, { "amx_tile", "AMX" },
        { "aes", "AES-NI" }, { "sha_ni", "SHA-NI" }, { "vaes", "VAES" },
        { "pclmulqdq", "PCLMULQDQ" }, { "rdrand", "RDRAND" }, { "f16c", "F16C" },
        { "fma", "FMA3" }, { "bmi2", "BMI2" }, { "vmx", "VT-x" }, { "svm", "AMD-V" },
        // AArch64 spells its capabilities out in the same field.
        { "asimd", "NEON" }, { "neon", "NEON" }, { "sve", "SVE" }, { "sve2", "SVE2" },
        { "crc32", "CRC32" }, { "sha2", "SHA2" }, { "sha3", "SHA3" }, { "i8mm", "I8MM" },
        { "bf16", "BF16" }, { "dotprod", "DotProd" }
    };
    std::set<std::string> present;
    std::stringstream stream(flags);
    std::string flag;
    while (stream >> flag) present.insert(flag);

    std::vector<std::string> result;
    for (const auto& entry : kInteresting) {
        if (present.count(entry.first) &&
            std::find(result.begin(), result.end(), entry.second) == result.end())
            result.push_back(entry.second);
    }
    return result;
}

void ReadCPUCaches(CPUInfo& out) {
    // Aggregate identical caches: one L1d per core is reported by every core,
    // and the panel wants "8 x 48 KB", not eight identical rows. Caches that
    // share a cpu list are the same physical cache.
    struct Key {
        int level; int type; uint64_t size; int line; int assoc;
        bool operator<(const Key& other) const {
            return std::tie(level, type, size, line, assoc) <
                   std::tie(other.level, other.type, other.size, other.line, other.assoc);
        }
    };
    std::map<Key, std::set<std::string>> instances;

    for (const auto& cpuName : ListDirectory("/sys/devices/system/cpu")) {
        if (!StartsWith(cpuName, "cpu") || cpuName.size() < 4 ||
            !std::isdigit(static_cast<unsigned char>(cpuName[3]))) continue;
        const std::string cacheRoot = "/sys/devices/system/cpu/" + cpuName + "/cache";
        for (const auto& indexName : ListDirectory(cacheRoot)) {
            if (!StartsWith(indexName, "index")) continue;
            const std::string path = cacheRoot + "/" + indexName;
            const std::string typeText = ReadTextFile(path + "/type");
            CPUCacheType type = CPUCacheType::Unknown;
            if (typeText == "Data") type = CPUCacheType::Data;
            else if (typeText == "Instruction") type = CPUCacheType::Instruction;
            else if (typeText == "Unified") type = CPUCacheType::Unified;

            Key key;
            key.level = static_cast<int>(ReadUnsigned(path + "/level", 0));
            key.type  = static_cast<int>(type);
            key.size  = ParseSizeWithSuffix(ReadTextFile(path + "/size"));
            key.line  = static_cast<int>(ReadUnsigned(path + "/coherency_line_size", 0));
            key.assoc = static_cast<int>(ReadUnsigned(path + "/ways_of_associativity", 0));
            if (key.level == 0 || key.size == 0) continue;

            std::string sharedList = ReadTextFile(path + "/shared_cpu_list");
            if (sharedList.empty()) sharedList = cpuName;
            instances[key].insert(sharedList);
        }
    }

    for (const auto& entry : instances) {
        CPUCacheInfo cache;
        cache.level = entry.first.level;
        cache.type = static_cast<CPUCacheType>(entry.first.type);
        cache.sizeBytes = entry.first.size;
        cache.lineSizeBytes = entry.first.line;
        cache.associativity = entry.first.assoc;
        cache.instanceCount = static_cast<int>(entry.second.size());
        cache.sharedByLogicalCores = static_cast<int>(ParseCPUList(*entry.second.begin()).size());
        out.caches.push_back(cache);
    }
    std::sort(out.caches.begin(), out.caches.end(),
              [](const CPUCacheInfo& a, const CPUCacheInfo& b) {
                  if (a.level != b.level) return a.level < b.level;
                  return static_cast<int>(a.type) < static_cast<int>(b.type);
              });
}

double ReadMaxFrequencyMHz(const std::string& cpuName) {
    const std::string cpufreq = "/sys/devices/system/cpu/" + cpuName + "/cpufreq";
    const uint64_t kilohertz = ReadUnsigned(cpufreq + "/cpuinfo_max_freq", 0);
    return kilohertz > 0 ? static_cast<double>(kilohertz) / 1000.0 : 0.0;
}

// Hybrid packages (Intel P/E, Arm big.LITTLE) present themselves either as named
// PMUs or simply as cores with different maximum frequencies.
void ReadCoreGroups(CPUInfo& out) {
    struct NamedSet { const char* path; const char* label; };
    static const NamedSet kHybridPmus[] = {
        { "/sys/devices/cpu_core/cpus", "Performance" },
        { "/sys/devices/cpu_atom/cpus", "Efficiency" }
    };
    for (const auto& pmu : kHybridPmus) {
        const std::string list = ReadTextFile(pmu.path);
        if (list.empty()) continue;
        const std::vector<int> cpus = ParseCPUList(list);
        if (cpus.empty()) continue;
        CPUCoreGroup group;
        group.name = pmu.label;
        group.logicalCores = static_cast<int>(cpus.size());
        std::set<std::pair<int, int>> cores;
        for (int cpu : cpus) {
            const std::string topology = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology";
            cores.insert({ static_cast<int>(ReadUnsigned(topology + "/physical_package_id", 0)),
                           static_cast<int>(ReadUnsigned(topology + "/core_id", cpu)) });
            group.maxClockMHz = std::max(group.maxClockMHz,
                                         ReadMaxFrequencyMHz("cpu" + std::to_string(cpu)));
        }
        group.physicalCores = static_cast<int>(cores.size());
        out.coreGroups.push_back(std::move(group));
    }
    if (!out.coreGroups.empty()) return;

    // Fallback: distinct maximum frequencies across the online CPUs.
    std::map<long, std::pair<int, std::set<std::pair<int, int>>>> byFrequency;
    for (const auto& cpuName : ListDirectory("/sys/devices/system/cpu")) {
        if (!StartsWith(cpuName, "cpu") || cpuName.size() < 4 ||
            !std::isdigit(static_cast<unsigned char>(cpuName[3]))) continue;
        const double megahertz = ReadMaxFrequencyMHz(cpuName);
        if (megahertz <= 0) continue;
        const std::string topology = "/sys/devices/system/cpu/" + cpuName + "/topology";
        auto& bucket = byFrequency[static_cast<long>(megahertz)];
        ++bucket.first;
        bucket.second.insert({ static_cast<int>(ReadUnsigned(topology + "/physical_package_id", 0)),
                               static_cast<int>(ReadUnsigned(topology + "/core_id", 0)) });
    }
    if (byFrequency.size() < 2) return;
    for (auto it = byFrequency.rbegin(); it != byFrequency.rend(); ++it) {
        CPUCoreGroup group;
        group.maxClockMHz = static_cast<double>(it->first);
        group.logicalCores = it->second.first;
        group.physicalCores = static_cast<int>(it->second.second.size());
        group.name = (it == byFrequency.rbegin()) ? "Performance cores" : "Efficiency cores";
        out.coreGroups.push_back(std::move(group));
    }
}

double ReadCurrentClockMHz() {
    double total = 0.0;
    int counted = 0;
    for (const auto& cpuName : ListDirectory("/sys/devices/system/cpu")) {
        if (!StartsWith(cpuName, "cpu") || cpuName.size() < 4 ||
            !std::isdigit(static_cast<unsigned char>(cpuName[3]))) continue;
        const uint64_t kilohertz =
            ReadUnsigned("/sys/devices/system/cpu/" + cpuName + "/cpufreq/scaling_cur_freq", 0);
        if (kilohertz == 0) continue;
        total += static_cast<double>(kilohertz) / 1000.0;
        ++counted;
    }
    return counted > 0 ? total / counted : 0.0;
}

double ReadLoadPercent(int logicalCores) {
    if (logicalCores <= 0) return 0.0;
    const std::string loadavg = ReadTextFile("/proc/loadavg");
    if (loadavg.empty()) return 0.0;
    const double oneMinute = std::strtod(loadavg.c_str(), nullptr);
    return 100.0 * oneMinute / static_cast<double>(logicalCores);
}

} // namespace

void QueryCPU(CPUInfo& out, bool includeSensors, std::vector<std::string>& warnings) {
    const CpuInfoFields fields = ReadProcCpuInfo();
    const auto& first = fields.firstBlock;
    auto field = [&first](const char* key) -> std::string {
        auto it = first.find(key);
        return it == first.end() ? std::string() : it->second;
    };

    out.vendor = field("vendor_id");
    out.model  = field("model name");
    if (out.model.empty()) out.model = field("Model");         // some ARM kernels
    if (out.model.empty()) out.model = field("Processor");
    if (out.vendor.empty()) out.vendor = field("CPU implementer");

    utsname uts{};
    if (::uname(&uts) == 0) out.architecture = uts.machine;

    if (!field("cpu family").empty()) {
        out.stepping = "family " + field("cpu family") + ", model " + field("model") +
                       ", stepping " + field("stepping");
    } else if (!field("CPU part").empty()) {
        out.stepping = "part " + field("CPU part") + ", revision " + field("CPU revision");
    }

    // Topology from sysfs, which is authoritative; /proc/cpuinfo is the fallback
    // for kernels that publish no topology directory (some ARM boards).
    std::set<std::pair<int, int>> physicalCores;
    std::set<int> packages;
    int logical = 0;
    for (const auto& cpuName : ListDirectory("/sys/devices/system/cpu")) {
        if (!StartsWith(cpuName, "cpu") || cpuName.size() < 4 ||
            !std::isdigit(static_cast<unsigned char>(cpuName[3]))) continue;
        const std::string topology = "/sys/devices/system/cpu/" + cpuName + "/topology";
        ++logical;
        if (!PathExists(topology + "/core_id")) continue;
        const int package = static_cast<int>(ReadUnsigned(topology + "/physical_package_id", 0));
        const int core = static_cast<int>(ReadUnsigned(topology + "/core_id", 0));
        physicalCores.insert({ package, core });
        packages.insert(package);
    }
    out.logicalCores = logical > 0 ? logical : fields.logicalCount;
    if (out.logicalCores <= 0) out.logicalCores = static_cast<int>(::sysconf(_SC_NPROCESSORS_ONLN));
    out.physicalCores = physicalCores.empty() ? static_cast<int>(fields.physicalCores.size())
                                              : static_cast<int>(physicalCores.size());
    if (out.physicalCores <= 0) out.physicalCores = out.logicalCores;
    out.packages = packages.empty() ? 1 : static_cast<int>(packages.size());

    ReadCPUCaches(out);
    ReadCoreGroups(out);
    out.instructionSets = SelectInstructionSets(field("flags") + " " + field("Features"));

    const uint64_t baseKilohertz = ReadUnsigned("/sys/devices/system/cpu/cpu0/cpufreq/base_frequency", 0);
    if (baseKilohertz > 0) out.baseClockMHz = static_cast<double>(baseKilohertz) / 1000.0;
    for (const auto& cpuName : ListDirectory("/sys/devices/system/cpu")) {
        if (!StartsWith(cpuName, "cpu") || cpuName.size() < 4 ||
            !std::isdigit(static_cast<unsigned char>(cpuName[3]))) continue;
        out.maxClockMHz = std::max(out.maxClockMHz, ReadMaxFrequencyMHz(cpuName));
    }
    out.currentClockMHz = ReadCurrentClockMHz();
    if (out.currentClockMHz <= 0.0 && !fields.clocksMHz.empty()) {
        double total = 0.0;
        for (double value : fields.clocksMHz) total += value;
        out.currentClockMHz = total / static_cast<double>(fields.clocksMHz.size());
    }

    if (includeSensors) {
        double celsius = 0.0;
        if (ReadCPUTemperature(celsius)) out.temperatureC = celsius;
        else warnings.push_back("CPU temperature is unavailable: no coretemp/k10temp hwmon node and "
                                "no CPU thermal zone is exported by this kernel.");
        const double load = ReadLoadPercent(out.logicalCores);
        if (load > 0.0) out.loadPercent = load;
    }
    if (out.caches.empty())
        warnings.push_back("CPU cache sizes are unavailable: this kernel exports no "
                           "/sys/devices/system/cpu/cpu0/cache entries (common under virtualisation).");
}

// ===== MEMORY =====
namespace {

// One SMBIOS "Memory Device" (type 17) record, as the kernel hands it out under
// /sys/firmware/dmi/entries. Root-only on every distribution, which is why the
// totals above never depend on it.
struct SmbiosRecord {
    std::vector<uint8_t> data;
    std::vector<std::string> strings;

    uint8_t  Byte(size_t offset) const {
        return offset < data.size() ? data[offset] : 0;
    }
    uint16_t Word(size_t offset) const {
        return offset + 1 < data.size()
                   ? static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8)) : 0;
    }
    uint32_t DWord(size_t offset) const {
        if (offset + 3 >= data.size()) return 0;
        return static_cast<uint32_t>(data[offset]) |
               (static_cast<uint32_t>(data[offset + 1]) << 8) |
               (static_cast<uint32_t>(data[offset + 2]) << 16) |
               (static_cast<uint32_t>(data[offset + 3]) << 24);
    }
    std::string String(size_t offset) const {
        const uint8_t index = Byte(offset);
        if (index == 0 || index > strings.size()) return std::string();
        return strings[index - 1];
    }
};

bool ReadSmbiosRecord(const std::string& rawPath, SmbiosRecord& out) {
    std::ifstream stream(rawPath, std::ios::binary);
    if (!stream) return false;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
    if (bytes.size() < 4) return false;
    const size_t formattedLength = bytes[1];
    if (formattedLength > bytes.size()) return false;
    out.data.assign(bytes.begin(), bytes.begin() + static_cast<long>(formattedLength));

    // The string table follows the formatted area: NUL-terminated entries, the
    // whole table closed by an empty one.
    size_t cursor = formattedLength;
    while (cursor < bytes.size()) {
        std::string text;
        while (cursor < bytes.size() && bytes[cursor] != 0)
            text.push_back(static_cast<char>(bytes[cursor++]));
        ++cursor;
        if (text.empty()) break;
        out.strings.push_back(Trim(text));
    }
    return true;
}

std::string SmbiosMemoryType(uint8_t code) {
    switch (code) {
        case 0x0F: return "SDRAM";
        case 0x11: return "RDRAM";
        case 0x12: return "DDR";
        case 0x13: return "DDR2";
        case 0x14: return "DDR2 FB-DIMM";
        case 0x18: return "DDR3";
        case 0x1A: return "DDR4";
        case 0x1B: return "LPDDR";
        case 0x1C: return "LPDDR2";
        case 0x1D: return "LPDDR3";
        case 0x1E: return "LPDDR4";
        case 0x20: return "HBM";
        case 0x21: return "HBM2";
        case 0x22: return "DDR5";
        case 0x23: return "LPDDR5";
        case 0x24: return "HBM3";
        default:   return std::string();
    }
}

std::string SmbiosFormFactor(uint8_t code) {
    switch (code) {
        case 0x03: return "SIMM";
        case 0x05: return "Chip";
        case 0x09: return "DIMM";
        case 0x0B: return "Row of chips";
        case 0x0C: return "RIMM";
        case 0x0D: return "SODIMM";
        case 0x0F: return "FB-DIMM";
        case 0x10: return "Die";
        default:   return std::string();
    }
}

void ReadMemoryModules(MemoryInfo& out, std::vector<std::string>& warnings) {
    const std::string root = "/sys/firmware/dmi/entries";
    const std::vector<std::string> entries = ListDirectory(root);
    if (entries.empty()) {
        warnings.push_back("Per-module memory detail (type, speed, part number) is unavailable: "
                           "the SMBIOS tables under /sys/firmware/dmi/entries are not exported.");
        return;
    }
    bool sawType17 = false;
    bool readAny = false;
    for (const auto& entry : entries) {
        if (!StartsWith(entry, "17-")) continue;
        sawType17 = true;
        SmbiosRecord record;
        if (!ReadSmbiosRecord(root + "/" + entry + "/raw", record)) continue;
        readAny = true;
        ++out.slotsTotal;

        MemoryModuleInfo module;
        const uint16_t sizeField = record.Word(0x0C);
        if (sizeField == 0xFFFF || sizeField == 0) {
            // 0 means the slot is empty; 0xFFFF means the firmware does not know.
        } else if (sizeField == 0x7FFF) {
            module.sizeBytes = static_cast<uint64_t>(record.DWord(0x1C)) * 1024ull * 1024ull;
        } else if (sizeField & 0x8000) {
            module.sizeBytes = static_cast<uint64_t>(sizeField & 0x7FFF) * 1024ull;
        } else {
            module.sizeBytes = static_cast<uint64_t>(sizeField) * 1024ull * 1024ull;
        }
        if (module.sizeBytes == 0) continue;   // empty slot: counted, not listed
        ++out.slotsUsed;

        module.totalWidthBits = record.Word(0x08) == 0xFFFF ? 0 : record.Word(0x08);
        module.dataWidthBits  = record.Word(0x0A) == 0xFFFF ? 0 : record.Word(0x0A);
        module.formFactor     = SmbiosFormFactor(record.Byte(0x0E));
        module.locator        = record.String(0x10);
        module.bankLocator    = record.String(0x11);
        module.type           = SmbiosMemoryType(record.Byte(0x12));
        module.ratedSpeedMTs  = record.Word(0x15);
        module.manufacturer   = record.String(0x17);
        module.serialNumber   = record.String(0x18);
        module.partNumber     = record.String(0x1A);
        module.speedMTs       = record.Word(0x20);
        const uint16_t configuredVoltageMilliVolts = record.Word(0x26);
        if (configuredVoltageMilliVolts > 0)
            module.voltageVolts = configuredVoltageMilliVolts / 1000.0;
        // DDR5 exceeds the 16-bit MT/s fields; SMBIOS 3.3 moves those to
        // 32-bit extended fields and writes 0xFFFF in the originals.
        if (module.ratedSpeedMTs == 0xFFFF)
            module.ratedSpeedMTs = static_cast<int>(record.DWord(0x54));
        if (module.speedMTs == 0xFFFF)
            module.speedMTs = static_cast<int>(record.DWord(0x58));
        if (module.speedMTs == 0) module.speedMTs = module.ratedSpeedMTs;
        out.modules.push_back(std::move(module));
    }
    if (sawType17 && !readAny)
        warnings.push_back("Per-module memory detail (type, speed, part number) needs read access "
                           "to /sys/firmware/dmi/entries, which is restricted to root.");
}

} // namespace

void QueryMemory(MemoryInfo& out, std::vector<std::string>& warnings) {
    for (const auto& line : ReadLines("/proc/meminfo")) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = line.substr(0, colon);
        const uint64_t kilobytes = std::strtoull(Trim(line.substr(colon + 1)).c_str(), nullptr, 10);
        const uint64_t bytes = kilobytes * 1024ull;
        if (key == "MemTotal") out.totalBytes = bytes;
        else if (key == "MemAvailable") out.availableBytes = bytes;
        else if (key == "SwapTotal") out.swapTotalBytes = bytes;
        else if (key == "SwapFree") out.swapFreeBytes = bytes;
    }
    if (out.totalBytes == 0) {
        struct sysinfo info {};
        if (::sysinfo(&info) == 0) {
            out.totalBytes = static_cast<uint64_t>(info.totalram) * info.mem_unit;
            out.availableBytes = static_cast<uint64_t>(info.freeram) * info.mem_unit;
        }
    }
    if (out.totalBytes >= out.availableBytes) out.usedBytes = out.totalBytes - out.availableBytes;
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pageSize > 0) out.pageSizeBytes = static_cast<uint64_t>(pageSize);

    ReadMemoryModules(out, warnings);
}

// ===== GRAPHICS AND ACCELERATORS =====
namespace {

std::string ReadDriverName(const std::string& deviceDirectory) {
    const std::string resolved = ResolvePath(deviceDirectory + "/driver");
    return resolved.empty() ? std::string() : BaseName(resolved);
}

// "0000:01:00.0" -> the bus number, "01". An integrated GPU sits on bus 00 of
// the root complex; a discrete card is behind a bridge on a higher bus.
bool IsOnRootBus(const std::string& busId) {
    return busId.size() > 7 && busId.compare(5, 2, "00") == 0;
}

// amdgpu writes its clock table as "0: 500Mhz\n1: 2100Mhz *", the active state
// marked with an asterisk.
double ReadActiveClockMHz(const std::string& path) {
    for (const auto& line : ReadLines(path)) {
        if (line.find('*') == std::string::npos) continue;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        return std::strtod(Trim(line.substr(colon + 1)).c_str(), nullptr);
    }
    return 0.0;
}

void FillPciIdentity(const std::string& deviceDirectory, uint16_t& vendorId, uint16_t& deviceId,
                     std::string& vendorName, std::string& modelName, std::string& busId) {
    vendorId = ParseHexId(ReadTextFile(deviceDirectory + "/vendor"));
    deviceId = ParseHexId(ReadTextFile(deviceDirectory + "/device"));
    const std::string resolved = ResolvePath(deviceDirectory);
    if (!resolved.empty()) busId = BaseName(resolved);
    if (vendorId != 0) LookupPciNames(vendorId, deviceId, vendorName, modelName);
    if (vendorName.empty()) {
        switch (vendorId) {
            case 0x10DE: vendorName = "NVIDIA"; break;
            case 0x1002: vendorName = "AMD"; break;
            case 0x8086: vendorName = "Intel"; break;
            case 0x1AF4: vendorName = "Red Hat (virtio)"; break;
            case 0x15AD: vendorName = "VMware"; break;
            case 0x1234: vendorName = "QEMU"; break;
            default: break;
        }
    }
}

GPUKind ClassifyGPU(const std::string& driver, const std::string& busId, uint16_t vendorId) {
    if (vendorId == 0x1AF4 || vendorId == 0x15AD || vendorId == 0x1234 || vendorId == 0x1B36)
        return GPUKind::Virtual;
    static const char* kIntegratedDrivers[] = { "i915", "xe", "v3d", "vc4", "panfrost", "lima",
                                                "msm", "etnaviv", "rockchip", "sun4i-drm",
                                                "imx-drm", "meson-drm", "vgem" };
    for (const char* name : kIntegratedDrivers)
        if (driver == name) return GPUKind::Integrated;
    if (busId.empty()) return GPUKind::Integrated;      // SoC display engine, not on PCI
    return IsOnRootBus(busId) ? GPUKind::Integrated : GPUKind::Discrete;
}

} // namespace

void QueryGPUs(std::vector<GPUInfo>& out, bool includeSensors, std::vector<std::string>& warnings) {
    std::set<std::string> seenDevicePaths;

    for (const auto& cardName : ListDirectory("/sys/class/drm")) {
        // "card0" is the device; "card0-HDMI-A-1" is one of its connectors.
        if (!StartsWith(cardName, "card") || cardName.find('-') != std::string::npos) continue;
        const std::string cardPath = "/sys/class/drm/" + cardName;
        const std::string devicePath = cardPath + "/device";
        const std::string resolvedDevice = ResolvePath(devicePath);
        if (!resolvedDevice.empty()) seenDevicePaths.insert(resolvedDevice);

        GPUInfo gpu;
        FillPciIdentity(devicePath, gpu.pciVendorId, gpu.pciDeviceId, gpu.vendor, gpu.model, gpu.busId);
        gpu.driverName = ReadDriverName(devicePath);
        gpu.kind = ClassifyGPU(gpu.driverName, gpu.busId, gpu.pciVendorId);
        gpu.videoMemoryBytes = ReadUnsigned(devicePath + "/mem_info_vram_total", 0);
        const uint64_t usedVideoMemory = ReadUnsigned(devicePath + "/mem_info_vram_used", 0);
        if (usedVideoMemory > 0) gpu.videoMemoryUsedBytes = usedVideoMemory;
        gpu.sharedMemoryBytes = ReadUnsigned(devicePath + "/mem_info_gtt_total", 0);
        gpu.coreClockMHz = ReadActiveClockMHz(devicePath + "/pp_dpm_sclk");

        if (includeSensors) {
            double celsius = 0.0;
            if (TemperatureForDevice(resolvedDevice, celsius)) gpu.temperatureC = celsius;
            const std::string busy = ReadTextFile(devicePath + "/gpu_busy_percent");
            if (!busy.empty()) gpu.utilizationPercent = std::strtod(busy.c_str(), nullptr);
        }
        if (gpu.model.empty() && gpu.vendor.empty()) gpu.model = cardName;
        out.push_back(std::move(gpu));
    }

    // A display controller with no DRM driver bound (a passed-through card, a
    // server BMC) still belongs on the list.
    for (const auto& address : ListDirectory("/sys/bus/pci/devices")) {
        const std::string devicePath = "/sys/bus/pci/devices/" + address;
        const uint64_t pciClass = ReadUnsigned(devicePath + "/class", 0);
        if ((pciClass >> 16) != 0x03) continue;
        const std::string resolved = ResolvePath(devicePath);
        if (seenDevicePaths.count(resolved)) continue;
        GPUInfo gpu;
        FillPciIdentity(devicePath, gpu.pciVendorId, gpu.pciDeviceId, gpu.vendor, gpu.model, gpu.busId);
        gpu.driverName = ReadDriverName(devicePath);
        gpu.kind = ClassifyGPU(gpu.driverName, gpu.busId, gpu.pciVendorId);
        out.push_back(std::move(gpu));
    }

    if (out.empty())
        warnings.push_back("No graphics adapter was found: neither /sys/class/drm nor the PCI bus "
                           "lists a display controller (headless or fully paravirtualised system).");
}

void QueryNPUs(std::vector<NPUInfo>& out, bool includeSensors, std::vector<std::string>& warnings) {
    (void)warnings;
    // Which userspace stack drives the part, so an application knows what to
    // load. Deliberately no TOPS table: a number nobody can verify from the
    // machine itself would be a guess presented as a reading.
    auto RuntimeForDriver = [](const std::string& driver) -> std::string {
        if (driver == "intel_vpu" || driver == "ivpu") return "OpenVINO / Level Zero";
        if (driver == "amdxdna") return "Ryzen AI (XDNA)";
        if (driver == "habanalabs") return "SynapseAI";
        if (driver == "rknpu") return "RKNN";
        if (driver == "qaic") return "Qualcomm AIC";
        return std::string();
    };

    std::set<std::string> seenDevicePaths;

    // The kernel's accel subsystem (drivers/accel) is where compute-only
    // accelerators register - the NPU equivalent of /sys/class/drm.
    for (const auto& accelName : ListDirectory("/sys/class/accel")) {
        const std::string devicePath = "/sys/class/accel/" + accelName + "/device";
        const std::string resolved = ResolvePath(devicePath);
        if (!resolved.empty()) seenDevicePaths.insert(resolved);
        NPUInfo npu;
        FillPciIdentity(devicePath, npu.pciVendorId, npu.pciDeviceId, npu.vendor, npu.model, npu.busId);
        npu.driverName = ReadDriverName(devicePath);
        npu.runtime = RuntimeForDriver(npu.driverName);
        npu.integrated = npu.busId.empty() || IsOnRootBus(npu.busId);
        if (npu.model.empty()) npu.model = accelName;
        if (includeSensors) {
            double celsius = 0.0;
            if (TemperatureForDevice(resolved, celsius)) npu.temperatureC = celsius;
        }
        out.push_back(std::move(npu));
    }

    // PCI class 12h ("processing accelerators") catches parts whose driver is
    // not loaded, and older NPUs that predate the accel subsystem.
    for (const auto& address : ListDirectory("/sys/bus/pci/devices")) {
        const std::string devicePath = "/sys/bus/pci/devices/" + address;
        const uint64_t pciClass = ReadUnsigned(devicePath + "/class", 0);
        if ((pciClass >> 16) != 0x12) continue;
        const std::string resolved = ResolvePath(devicePath);
        if (seenDevicePaths.count(resolved)) continue;
        NPUInfo npu;
        FillPciIdentity(devicePath, npu.pciVendorId, npu.pciDeviceId, npu.vendor, npu.model, npu.busId);
        npu.driverName = ReadDriverName(devicePath);
        npu.runtime = RuntimeForDriver(npu.driverName);
        npu.integrated = IsOnRootBus(npu.busId);
        if (includeSensors) {
            double celsius = 0.0;
            if (TemperatureForDevice(resolved, celsius)) npu.temperatureC = celsius;
        }
        out.push_back(std::move(npu));
    }
}

// ===== STORAGE =====
namespace {

struct MountEntry {
    std::string source;      // "/dev/sda2"
    std::string mountPoint;
    std::string fileSystem;
    std::string options;
};

const std::vector<MountEntry>& Mounts() {
    static std::vector<MountEntry> mounts;
    static bool loaded = false;
    if (loaded) return mounts;
    loaded = true;
    for (const auto& line : ReadLines("/proc/self/mounts")) {
        std::istringstream stream(line);
        MountEntry entry;
        if (!(stream >> entry.source >> entry.mountPoint >> entry.fileSystem >> entry.options))
            continue;
        if (!StartsWith(entry.source, "/dev/")) continue;
        // The kernel escapes spaces in mount points as \040.
        size_t position = 0;
        while ((position = entry.mountPoint.find("\\040", position)) != std::string::npos)
            entry.mountPoint.replace(position, 4, " ");
        mounts.push_back(std::move(entry));
    }
    return mounts;
}

// True when `partition` is a partition of, or is, the whole device `disk`
// ("sda2" of "sda", "nvme0n1p3" of "nvme0n1").
bool BelongsToDisk(const std::string& partition, const std::string& disk) {
    if (partition == disk) return true;
    if (!StartsWith(partition, disk)) return false;
    std::string suffix = partition.substr(disk.size());
    if (suffix.empty()) return false;
    if (suffix[0] == 'p') suffix.erase(0, 1);
    if (suffix.empty()) return false;
    return std::all_of(suffix.begin(), suffix.end(),
                       [](unsigned char character) { return std::isdigit(character) != 0; });
}

std::string AncestorWithFile(const std::string& startPath, const std::string& fileName) {
    std::string path = startPath;
    while (path.size() > 1) {
        if (PathExists(path + "/" + fileName)) return path;
        const size_t slash = path.find_last_of('/');
        if (slash == std::string::npos || slash == 0) break;
        path = path.substr(0, slash);
    }
    return std::string();
}

// SCSI/SATA drives publish their serial in VPD page 0x80: a four-byte header
// followed by the ASCII serial.
std::string ReadVpdSerial(const std::string& deviceDirectory) {
    std::ifstream stream(deviceDirectory + "/vpd_pg80", std::ios::binary);
    if (!stream) return std::string();
    std::vector<char> bytes((std::istreambuf_iterator<char>(stream)),
                            std::istreambuf_iterator<char>());
    if (bytes.size() < 5) return std::string();
    const size_t length = static_cast<size_t>(static_cast<unsigned char>(bytes[2])) << 8 |
                          static_cast<unsigned char>(bytes[3]);
    if (length == 0 || 4 + length > bytes.size()) return std::string();
    return Trim(std::string(bytes.begin() + 4, bytes.begin() + 4 + static_cast<long>(length)));
}

std::string PcieGenerationFromLinkSpeed(const std::string& linkSpeed) {
    // "8.0 GT/s PCIe" -> "3.0"
    const double transfers = std::strtod(linkSpeed.c_str(), nullptr);
    if (transfers >= 63.0) return "6.0";
    if (transfers >= 31.0) return "5.0";
    if (transfers >= 15.0) return "4.0";
    if (transfers >= 7.0)  return "3.0";
    if (transfers >= 4.0)  return "2.0";
    if (transfers >= 2.0)  return "1.0";
    return std::string();
}

std::string DescribeUsbSpeed(double megabitsPerSecond) {
    if (megabitsPerSecond >= 20000) return "USB 3.2 Gen 2x2 (20 Gb/s)";
    if (megabitsPerSecond >= 10000) return "USB 3.1 Gen 2 (10 Gb/s)";
    if (megabitsPerSecond >= 5000)  return "USB 3.0 (5 Gb/s)";
    if (megabitsPerSecond >= 480)   return "USB 2.0 High-Speed (480 Mb/s)";
    if (megabitsPerSecond >= 12)    return "USB 1.1 Full-Speed (12 Mb/s)";
    if (megabitsPerSecond > 0)      return "USB 1.0 Low-Speed (1.5 Mb/s)";
    return "USB";
}

std::string DescribeSataLink(const std::string& blockRealPath) {
    // Find which ata port the drive hangs off, then that port's negotiated speed.
    const size_t ataPosition = blockRealPath.find("/ata");
    if (ataPosition == std::string::npos) return "SATA";
    size_t end = ataPosition + 4;
    while (end < blockRealPath.size() && std::isdigit(static_cast<unsigned char>(blockRealPath[end])))
        ++end;
    const std::string portDirectory = blockRealPath.substr(0, end);
    for (const auto& linkName : ListDirectory("/sys/class/ata_link")) {
        const std::string resolved = ResolvePath("/sys/class/ata_link/" + linkName);
        if (resolved.empty() || !StartsWith(resolved, portDirectory + "/")) continue;
        const std::string speed = ReadTextFile("/sys/class/ata_link/" + linkName + "/sata_spd");
        if (!speed.empty() && speed != "<unknown>") return "SATA " + speed;
    }
    return "SATA";
}

// The on-drive DRAM buffer is not in sysfs; ATA IDENTIFY reports it, and that
// needs the block device open, which is root-only on a normal system.
uint64_t ReadAtaCacheBytes(const std::string& devicePath, bool& permissionDenied) {
    permissionDenied = false;
#ifdef ULTRACANVAS_HAS_HDREG
    const int descriptor = ::open(devicePath.c_str(), O_RDONLY | O_NONBLOCK);
    if (descriptor < 0) {
        permissionDenied = (errno == EACCES || errno == EPERM);
        return 0;
    }
    struct hd_driveid identity {};
    const int result = ::ioctl(descriptor, HDIO_GET_IDENTITY, &identity);
    ::close(descriptor);
    if (result != 0) return 0;
    return static_cast<uint64_t>(identity.buf_size) * 512ull;
#else
    (void)devicePath;
    return 0;
#endif
}

void FillVolumes(StorageDeviceInfo& device, const std::string& blockName) {
    for (const auto& mount : Mounts()) {
        const std::string source = BaseName(mount.source);
        if (!BelongsToDisk(source, blockName)) continue;
        StorageVolumeInfo volume;
        volume.mountPoint = mount.mountPoint;
        volume.fileSystem = mount.fileSystem;
        volume.readOnly = StartsWith(mount.options, "ro,") || mount.options == "ro";
        struct statvfs stats {};
        if (::statvfs(mount.mountPoint.c_str(), &stats) == 0) {
            volume.totalBytes = static_cast<uint64_t>(stats.f_blocks) * stats.f_frsize;
            volume.freeBytes = static_cast<uint64_t>(stats.f_bavail) * stats.f_frsize;
        }
        device.volumes.push_back(std::move(volume));
    }
}

} // namespace

void QueryStorage(std::vector<StorageDeviceInfo>& out, bool includeSensors,
                  std::vector<std::string>& warnings) {
    static const char* kSkippedPrefixes[] = { "loop", "ram", "zram", "dm-", "md", "fd", "zd" };
    bool cachePermissionDenied = false;
    bool sawTemperature = false;

    for (const auto& blockName : ListDirectory("/sys/block")) {
        bool skip = false;
        for (const char* prefix : kSkippedPrefixes)
            if (StartsWith(blockName, prefix)) { skip = true; break; }
        if (skip) continue;

        const std::string blockPath = "/sys/block/" + blockName;
        const std::string blockRealPath = ResolvePath(blockPath);
        const std::string deviceDirectory = blockPath + "/device";
        if (!PathExists(deviceDirectory)) continue;

        StorageDeviceInfo device;
        device.devicePath = "/dev/" + blockName;
        device.capacityBytes = ReadUnsigned(blockPath + "/size", 0) * 512ull;
        device.removable = ReadUnsigned(blockPath + "/removable", 0) != 0;
        device.logicalSectorSize = static_cast<int>(ReadUnsigned(blockPath + "/queue/logical_block_size", 0));
        device.physicalSectorSize = static_cast<int>(ReadUnsigned(blockPath + "/queue/physical_block_size", 0));

        const bool rotational = ReadUnsigned(blockPath + "/queue/rotational", 0) != 0;
        if (StartsWith(blockName, "sr")) device.media = StorageMedia::Optical;
        else if (rotational) device.media = StorageMedia::HDD;
        else device.media = StorageMedia::SSD;
        if (rotational) {
            const uint64_t rpm = ReadUnsigned(deviceDirectory + "/queue/rotation_rate", 0);
            if (rpm > 1) device.rotationRateRPM = static_cast<int>(rpm);
        }

        // Bus and identity differ per transport; the sysfs path names the
        // transport the device is reached through.
        if (StartsWith(blockName, "nvme")) {
            device.bus = StorageBus::NVMe;
            device.model = ReadTextFile(deviceDirectory + "/model");
            device.serialNumber = ReadTextFile(deviceDirectory + "/serial");
            device.firmwareVersion = ReadTextFile(deviceDirectory + "/firmware_rev");
            const std::string pciDirectory = ResolvePath(deviceDirectory + "/device");
            if (!pciDirectory.empty()) {
                const std::string generation =
                    PcieGenerationFromLinkSpeed(ReadTextFile(pciDirectory + "/current_link_speed"));
                const uint64_t width = ReadUnsigned(pciDirectory + "/current_link_width", 0);
                if (!generation.empty() && width > 0)
                    device.connector = "PCIe " + generation + " x" + std::to_string(width) + " (NVMe)";
                else
                    device.connector = "PCIe (NVMe)";
            } else {
                device.connector = "NVMe";
            }
        } else if (StartsWith(blockName, "mmcblk")) {
            device.bus = StorageBus::MMC;
            device.media = StorageMedia::Flash;
            device.model = ReadTextFile(deviceDirectory + "/name");
            device.serialNumber = ReadTextFile(deviceDirectory + "/serial");
            device.firmwareVersion = ReadTextFile(deviceDirectory + "/fwrev");
            device.vendor = ReadTextFile(deviceDirectory + "/manfid");
            device.connector = ReadUnsigned(deviceDirectory + "/../../removable", 0) ? "SD card" : "eMMC";
        } else {
            device.model = ReadTextFile(deviceDirectory + "/model");
            device.vendor = ReadTextFile(deviceDirectory + "/vendor");
            device.firmwareVersion = ReadTextFile(deviceDirectory + "/rev");
            // A virtio disk's "vendor" attribute is the PCI vendor id, not a
            // name; resolve it rather than showing "0x1af4" as a manufacturer.
            if (StartsWith(device.vendor, "0x")) {
                std::string vendorName, modelName;
                LookupPciNames(ParseHexId(device.vendor.substr(2)), 0, vendorName, modelName);
                device.vendor = vendorName;
            }
            device.serialNumber = ReadVpdSerial(deviceDirectory);
            if (blockRealPath.find("/usb") != std::string::npos) {
                device.bus = StorageBus::USB;
                const std::string usbDevice = AncestorWithFile(blockRealPath, "idVendor");
                const double megabits = usbDevice.empty()
                                            ? 0.0
                                            : std::strtod(ReadTextFile(usbDevice + "/speed").c_str(), nullptr);
                device.connector = DescribeUsbSpeed(megabits);
            } else if (blockRealPath.find("/ata") != std::string::npos) {
                device.bus = StorageBus::SATA;
                device.connector = DescribeSataLink(blockRealPath);
            } else if (blockRealPath.find("/virtio") != std::string::npos) {
                device.bus = StorageBus::Virtual;
                device.media = StorageMedia::Unknown;
                device.connector = "virtio";
            } else {
                device.bus = StorageBus::SCSI;
                device.connector = "SCSI";
            }
            if (device.vendor == "ATA") device.vendor.clear();  // the transport, not the maker
        }

        if (device.bus == StorageBus::SATA || device.bus == StorageBus::SCSI) {
            bool denied = false;
            device.cacheBytes = ReadAtaCacheBytes(device.devicePath, denied);
            cachePermissionDenied = cachePermissionDenied || denied;
        }

        if (includeSensors) {
            double celsius = 0.0;
            const std::string resolvedDevice = ResolvePath(deviceDirectory);
            if (TemperatureForDevice(resolvedDevice, celsius) ||
                TemperatureForDevice(blockRealPath, celsius)) {
                device.temperatureC = celsius;
                sawTemperature = true;
            }
        }

        FillVolumes(device, blockName);
        out.push_back(std::move(device));
    }

    if (cachePermissionDenied)
        warnings.push_back("Drive cache size is unavailable: reading it needs ATA IDENTIFY on the "
                           "block device, which requires root (try the disk group or run as root).");
    if (includeSensors && !out.empty() && !sawTemperature)
        warnings.push_back("Drive temperatures are unavailable: no nvme or drivetemp hwmon node is "
                           "present (for SATA drives, load the drivetemp module).");
    if (includeSensors && !out.empty())
        warnings.push_back("Drive health and power-on hours come from SMART, which needs privileged "
                           "access to the device and is not read by this backend.");
}

// ===== NETWORK =====
namespace {

std::string FormatSocketAddress(const sockaddr* address, bool& isIPv4) {
    char text[INET6_ADDRSTRLEN] = {0};
    if (address->sa_family == AF_INET) {
        isIPv4 = true;
        const auto* in4 = reinterpret_cast<const sockaddr_in*>(address);
        if (!::inet_ntop(AF_INET, &in4->sin_addr, text, sizeof(text))) return std::string();
        return text;
    }
    if (address->sa_family == AF_INET6) {
        isIPv4 = false;
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(address);
        if (!::inet_ntop(AF_INET6, &in6->sin6_addr, text, sizeof(text))) return std::string();
        return text;
    }
    return std::string();
}

NetworkLinkType ClassifyInterface(const std::string& name, const std::string& sysPath) {
    if (PathExists(sysPath + "/wireless") || PathExists(sysPath + "/phy80211"))
        return NetworkLinkType::WiFi;
    if (PathExists(sysPath + "/bridge")) return NetworkLinkType::Bridge;
    const uint64_t arpType = ReadUnsigned(sysPath + "/type", 0);
    if (arpType == 772) return NetworkLinkType::Loopback;
    if (arpType == 768 || arpType == 776 || arpType == 65534) return NetworkLinkType::Tunnel;
    if (StartsWith(name, "wwan") || StartsWith(name, "wwp")) return NetworkLinkType::Cellular;
    if (StartsWith(name, "bnep")) return NetworkLinkType::Bluetooth;
    if (!PathExists(sysPath + "/device")) return NetworkLinkType::Virtual;
    if (arpType == 1) return NetworkLinkType::Ethernet;
    return NetworkLinkType::Unknown;
}

int ChannelForFrequency(int megahertz) {
    if (megahertz == 2484) return 14;
    if (megahertz >= 2412 && megahertz <= 2472) return (megahertz - 2407) / 5;
    if (megahertz >= 5150 && megahertz <= 5895) return (megahertz - 5000) / 5;
    if (megahertz >= 5955 && megahertz <= 7115) return (megahertz - 5950) / 5;
    return 0;
}

std::string BandForFrequency(int megahertz) {
    if (megahertz >= 5925) return "6 GHz";
    if (megahertz >= 4900) return "5 GHz";
    if (megahertz >= 2400) return "2.4 GHz";
    return std::string();
}

// /proc/net/wireless: "wlan0: 0000   62.  -48.  -256  0 0 0 0 0 0"
bool ReadWirelessSignal(const std::string& name, int& outQualityPercent, int& outLevelDbm) {
    for (const auto& line : ReadLines("/proc/net/wireless")) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        if (Trim(line.substr(0, colon)) != name) continue;
        std::istringstream stream(line.substr(colon + 1));
        std::string status, quality, level;
        if (!(stream >> status >> quality >> level)) return false;
        const double qualityValue = std::strtod(quality.c_str(), nullptr);
        const double levelValue = std::strtod(level.c_str(), nullptr);
        // The quality maximum is driver-defined; 70 is what mac80211 reports.
        outQualityPercent = static_cast<int>(std::min(100.0, qualityValue * 100.0 / 70.0));
        outLevelDbm = static_cast<int>(levelValue);
        return true;
    }
    return false;
}

bool ReadWiFiDetails(const std::string& name, WiFiInfo& out) {
#ifdef ULTRACANVAS_HAS_WIRELESS_EXT
    const int socketDescriptor = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketDescriptor < 0) return false;
    bool any = false;

    iwreq request{};
    std::strncpy(request.ifr_name, name.c_str(), IFNAMSIZ - 1);
    char essid[IW_ESSID_MAX_SIZE + 1] = {0};
    request.u.essid.pointer = essid;
    request.u.essid.length = IW_ESSID_MAX_SIZE;
    request.u.essid.flags = 0;
    if (::ioctl(socketDescriptor, SIOCGIWESSID, &request) == 0 && essid[0] != '\0') {
        out.ssid = essid;
        out.connected = true;
        any = true;
    }

    iwreq apRequest{};
    std::strncpy(apRequest.ifr_name, name.c_str(), IFNAMSIZ - 1);
    if (::ioctl(socketDescriptor, SIOCGIWAP, &apRequest) == 0) {
        const unsigned char* mac =
            reinterpret_cast<const unsigned char*>(apRequest.u.ap_addr.sa_data);
        char text[18];
        std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        if (std::string(text) != "00:00:00:00:00:00") { out.bssid = text; any = true; }
    }

    iwreq freqRequest{};
    std::strncpy(freqRequest.ifr_name, name.c_str(), IFNAMSIZ - 1);
    if (::ioctl(socketDescriptor, SIOCGIWFREQ, &freqRequest) == 0) {
        double hertz = static_cast<double>(freqRequest.u.freq.m);
        for (int exponent = 0; exponent < freqRequest.u.freq.e; ++exponent) hertz *= 10.0;
        if (hertz > 1000000.0) {   // a frequency, not a channel number
            out.frequencyMHz = static_cast<int>(hertz / 1000000.0);
            out.channel = ChannelForFrequency(out.frequencyMHz);
            out.band = BandForFrequency(out.frequencyMHz);
            any = true;
        }
    }

    iwreq rateRequest{};
    std::strncpy(rateRequest.ifr_name, name.c_str(), IFNAMSIZ - 1);
    if (::ioctl(socketDescriptor, SIOCGIWRATE, &rateRequest) == 0 && rateRequest.u.bitrate.value > 0) {
        out.txRateMbps = static_cast<double>(rateRequest.u.bitrate.value) / 1000000.0;
        any = true;
    }
    ::close(socketDescriptor);
    return any;
#else
    (void)name; (void)out;
    return false;
#endif
}

} // namespace

void QueryNetwork(std::vector<NetworkInterfaceInfo>& out, std::vector<std::string>& warnings) {
    std::map<std::string, std::pair<std::vector<std::string>, std::vector<std::string>>> addresses;
    ifaddrs* interfaceList = nullptr;
    if (::getifaddrs(&interfaceList) == 0) {
        for (ifaddrs* entry = interfaceList; entry; entry = entry->ifa_next) {
            if (!entry->ifa_addr || !entry->ifa_name) continue;
            bool isIPv4 = false;
            const std::string text = FormatSocketAddress(entry->ifa_addr, isIPv4);
            if (text.empty()) continue;
            auto& bucket = addresses[entry->ifa_name];
            (isIPv4 ? bucket.first : bucket.second).push_back(text);
        }
        ::freeifaddrs(interfaceList);
    }

    bool wirelessDetailMissing = false;

    for (const auto& name : ListDirectory("/sys/class/net")) {
        const std::string sysPath = "/sys/class/net/" + name;
        NetworkInterfaceInfo adapter;
        adapter.name = name;
        adapter.type = ClassifyInterface(name, sysPath);
        adapter.macAddress = ReadTextFile(sysPath + "/address");
        adapter.mtu = static_cast<int>(ReadUnsigned(sysPath + "/mtu", 0));
        adapter.up = ReadTextFile(sysPath + "/operstate") != "down";
        adapter.connected = ReadUnsigned(sysPath + "/carrier", 0) != 0;
        adapter.duplex = ReadTextFile(sysPath + "/duplex");
        adapter.bytesReceived = ReadUnsigned(sysPath + "/statistics/rx_bytes", 0);
        adapter.bytesSent = ReadUnsigned(sysPath + "/statistics/tx_bytes", 0);
        // speed reads -1 (or EINVAL) on links with no fixed rate, wireless included.
        const std::string speedText = ReadTextFile(sysPath + "/speed");
        if (!speedText.empty() && speedText[0] != '-')
            adapter.linkSpeedMbps = std::strtod(speedText.c_str(), nullptr);
        adapter.driver = ReadDriverName(sysPath + "/device");

        const std::string deviceDirectory = sysPath + "/device";
        if (PathExists(deviceDirectory + "/vendor")) {
            uint16_t vendorId = 0, deviceId = 0;
            std::string vendorName, modelName, busId;
            FillPciIdentity(deviceDirectory, vendorId, deviceId, vendorName, modelName, busId);
            adapter.description = Trim(vendorName.empty() ? modelName : vendorName + " " + modelName);
        } else if (PathExists(deviceDirectory + "/idVendor")) {
            std::string vendorName, productName;
            LookupUsbNames(ParseHexId(ReadTextFile(deviceDirectory + "/idVendor")),
                           ParseHexId(ReadTextFile(deviceDirectory + "/idProduct")),
                           vendorName, productName);
            adapter.description = Trim(vendorName + " " + productName);
        }

        auto addressIt = addresses.find(name);
        if (addressIt != addresses.end()) {
            adapter.ipv4Addresses = addressIt->second.first;
            adapter.ipv6Addresses = addressIt->second.second;
        }

        if (adapter.type == NetworkLinkType::WiFi) {
            WiFiInfo wifi;
            const bool haveDetails = ReadWiFiDetails(name, wifi);
            int qualityPercent = 0, levelDbm = 0;
            if (ReadWirelessSignal(name, qualityPercent, levelDbm)) {
                wifi.signalPercent = qualityPercent;
                wifi.signalDbm = levelDbm;
            }
            if (!haveDetails && wifi.ssid.empty()) wirelessDetailMissing = true;
            if (adapter.connected && !wifi.ssid.empty()) wifi.connected = true;
            adapter.wifi = wifi;
        }
        out.push_back(std::move(adapter));
    }

    if (wirelessDetailMissing)
        warnings.push_back("Wi-Fi network details (SSID, access point, rate) are unavailable: this "
                           "kernel exposes no wireless-extension compatibility layer, and reading "
                           "them over nl80211 is not implemented by this backend.");
    if (out.empty())
        warnings.push_back("No network interface was found under /sys/class/net.");
}

// ===== USB =====
namespace {

std::string UsbClassName(uint8_t code) {
    switch (code) {
        case 0x01: return "Audio";
        case 0x02: return "Communications";
        case 0x03: return "Human Interface Device";
        case 0x05: return "Physical";
        case 0x06: return "Imaging";
        case 0x07: return "Printer";
        case 0x08: return "Mass Storage";
        case 0x09: return "Hub";
        case 0x0A: return "CDC Data";
        case 0x0B: return "Smart Card";
        case 0x0D: return "Content Security";
        case 0x0E: return "Video";
        case 0x0F: return "Personal Healthcare";
        case 0x10: return "Audio/Video";
        case 0x11: return "Billboard";
        case 0xDC: return "Diagnostic";
        case 0xE0: return "Wireless Controller";
        case 0xEF: return "Miscellaneous";
        case 0xFE: return "Application Specific";
        case 0xFF: return "Vendor Specific";
        default:   return std::string();
    }
}

std::string UsbVersionName(const std::string& raw) {
    const double version = std::strtod(Trim(raw).c_str(), nullptr);
    if (version >= 3.2) return "USB 3.2";
    if (version >= 3.1) return "USB 3.1";
    if (version >= 3.0) return "USB 3.0";
    if (version >= 2.0) return "USB 2.0";
    if (version >= 1.1) return "USB 1.1";
    if (version > 0.0)  return "USB 1.0";
    return std::string();
}

// A device directory is "1-4.2"; an interface is "1-4.2:1.0"; a root hub is
// "usb1". Only the first kind is a device a person plugged in.
bool IsUsbDeviceDirectory(const std::string& name) {
    return name.find(':') == std::string::npos && !StartsWith(name, "usb") &&
           name.find('-') != std::string::npos;
}

} // namespace

void QueryUSB(std::vector<USBControllerInfo>& controllers, std::vector<USBDeviceInfo>& devices,
              bool includeHubs, std::vector<std::string>& warnings) {
    const std::string root = "/sys/bus/usb/devices";
    const std::vector<std::string> entries = ListDirectory(root);
    if (entries.empty()) {
        warnings.push_back("USB is unavailable: no devices are exported under /sys/bus/usb/devices "
                           "(no USB support in this kernel, or none in this container).");
        return;
    }

    for (const auto& name : entries) {
        if (!StartsWith(name, "usb")) continue;
        const std::string path = root + "/" + name;
        USBControllerInfo controller;
        controller.name = ReadTextFile(path + "/product");
        if (controller.name.empty()) controller.name = "USB host controller " + name.substr(3);
        controller.version = UsbVersionName(ReadTextFile(path + "/version"));
        controller.busNumber = static_cast<int>(ReadUnsigned(path + "/busnum", 0));
        controller.portCount = static_cast<int>(ReadUnsigned(path + "/maxchild", 0));
        const std::string resolved = ResolvePath(path);
        if (!resolved.empty()) {
            const size_t slash = resolved.find_last_of('/');
            if (slash != std::string::npos) {
                const std::string parent = resolved.substr(0, slash);
                controller.busId = BaseName(parent);
                controller.driver = ReadDriverName(parent);
            }
        }
        controllers.push_back(std::move(controller));
    }

    std::map<std::string, size_t> indexByPort;
    for (const auto& name : entries) {
        if (!IsUsbDeviceDirectory(name)) continue;
        const std::string path = root + "/" + name;
        USBDeviceInfo device;
        device.vendorId = ParseHexId(ReadTextFile(path + "/idVendor"));
        device.productId = ParseHexId(ReadTextFile(path + "/idProduct"));
        device.vendorName = ReadTextFile(path + "/manufacturer");
        device.productName = ReadTextFile(path + "/product");
        device.serialNumber = ReadTextFile(path + "/serial");
        device.busNumber = static_cast<int>(ReadUnsigned(path + "/busnum", 0));
        device.deviceAddress = static_cast<int>(ReadUnsigned(path + "/devnum", 0));
        device.portPath = name;
        device.maxPowerMilliAmps = static_cast<int>(std::strtol(ReadTextFile(path + "/bMaxPower").c_str(),
                                                                nullptr, 10));
        const uint8_t deviceClass = static_cast<uint8_t>(
            std::strtoul(ReadTextFile(path + "/bDeviceClass").c_str(), nullptr, 16));
        device.isHub = (deviceClass == 0x09);
        device.deviceClass = UsbClassName(deviceClass);
        if (deviceClass == 0x00) {
            // Class 0 means "look at the interfaces"; the first one is what the
            // device is for as far as a person is concerned.
            const uint8_t interfaceClass = static_cast<uint8_t>(
                std::strtoul(ReadTextFile(path + "/" + name + ":1.0/bInterfaceClass").c_str(),
                             nullptr, 16));
            device.deviceClass = UsbClassName(interfaceClass);
        }
        device.speed = DescribeUsbSpeed(std::strtod(ReadTextFile(path + "/speed").c_str(), nullptr));

        if (device.vendorName.empty() || device.productName.empty()) {
            std::string vendorName, productName;
            LookupUsbNames(device.vendorId, device.productId, vendorName, productName);
            if (device.vendorName.empty()) device.vendorName = vendorName;
            if (device.productName.empty()) device.productName = productName;
        }

        if (device.isHub && !includeHubs) continue;
        indexByPort[name] = devices.size();
        devices.push_back(std::move(device));
    }

    // Link each device to the hub port it hangs off, so a caller can render the
    // tree the way the cables actually run.
    for (auto& device : devices) {
        const size_t dot = device.portPath.find_last_of('.');
        if (dot == std::string::npos) continue;
        auto parent = indexByPort.find(device.portPath.substr(0, dot));
        if (parent != indexByPort.end()) device.parentDeviceIndex = static_cast<int>(parent->second);
    }
}

// ===== BLUETOOTH =====

void QueryBluetooth(std::vector<BluetoothAdapterInfo>& out, std::vector<std::string>& warnings) {
    const std::string root = "/sys/class/bluetooth";
    const std::vector<std::string> entries = ListDirectory(root);
    if (entries.empty()) {
        warnings.push_back("No Bluetooth adapter is present (nothing is registered under "
                           "/sys/class/bluetooth).");
        return;
    }

    // rfkill is what an adapter's power switch actually is; the class directory
    // says nothing about whether the radio is on.
    std::map<std::string, bool> poweredByDevice;
    for (const auto& killName : ListDirectory("/sys/class/rfkill")) {
        const std::string killPath = "/sys/class/rfkill/" + killName;
        if (ReadTextFile(killPath + "/type") != "bluetooth") continue;
        const bool blocked = ReadUnsigned(killPath + "/soft", 0) != 0 ||
                             ReadUnsigned(killPath + "/hard", 0) != 0;
        const std::string resolved = ResolvePath(killPath + "/device");
        poweredByDevice[resolved.empty() ? killName : BaseName(resolved)] = !blocked;
    }

    for (const auto& name : entries) {
        if (name.find(':') != std::string::npos) continue;   // a connection, handled below
        const std::string path = root + "/" + name;
        BluetoothAdapterInfo adapter;
        adapter.name = ReadTextFile(path + "/name");
        if (adapter.name.empty()) adapter.name = name;
        adapter.address = ReadTextFile(path + "/address");
        auto powered = poweredByDevice.find(name);
        adapter.powered = powered == poweredByDevice.end() ? true : powered->second;

        // Connections register as "hci0:<handle>" siblings of the adapter.
        for (const auto& connectionName : entries) {
            if (!StartsWith(connectionName, name + ":")) continue;
            const std::string connectionPath = root + "/" + connectionName;
            BluetoothDeviceInfo device;
            device.address = ReadTextFile(connectionPath + "/address");
            device.name = ReadTextFile(connectionPath + "/name");
            if (device.name.empty()) device.name = device.address;
            device.deviceClass = ReadTextFile(connectionPath + "/type");
            device.connected = true;
            adapter.devices.push_back(std::move(device));
        }
        out.push_back(std::move(adapter));
    }

    warnings.push_back("Bluetooth pairing state, device names and battery level come from BlueZ over "
                       "D-Bus; sysfs reports only the adapters and their live connections.");
}

// ===== SENSOR REFRESH =====

void RefreshSensors(HardwareSnapshot& snapshot) {
    if (snapshot.Has(HardwareQuery::CPU)) {
        double celsius = 0.0;
        if (ReadCPUTemperature(celsius)) snapshot.cpu.temperatureC = celsius;
        const double clock = ReadCurrentClockMHz();
        if (clock > 0.0) snapshot.cpu.currentClockMHz = clock;
        const double load = ReadLoadPercent(snapshot.cpu.logicalCores);
        if (load > 0.0) snapshot.cpu.loadPercent = load;
    }

    if (snapshot.Has(HardwareQuery::Memory)) {
        MemoryInfo refreshed;
        std::vector<std::string> ignored;
        for (const auto& line : ReadLines("/proc/meminfo")) {
            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            const std::string key = line.substr(0, colon);
            const uint64_t bytes =
                std::strtoull(Trim(line.substr(colon + 1)).c_str(), nullptr, 10) * 1024ull;
            if (key == "MemAvailable") snapshot.memory.availableBytes = bytes;
            else if (key == "SwapFree") snapshot.memory.swapFreeBytes = bytes;
        }
        if (snapshot.memory.totalBytes >= snapshot.memory.availableBytes)
            snapshot.memory.usedBytes = snapshot.memory.totalBytes - snapshot.memory.availableBytes;
        (void)refreshed; (void)ignored;
    }

    for (auto& gpu : snapshot.gpus) {
        if (gpu.busId.empty()) continue;
        const std::string devicePath = "/sys/bus/pci/devices/" + gpu.busId;
        double celsius = 0.0;
        if (TemperatureForDevice(ResolvePath(devicePath), celsius)) gpu.temperatureC = celsius;
        const std::string busy = ReadTextFile(devicePath + "/gpu_busy_percent");
        if (!busy.empty()) gpu.utilizationPercent = std::strtod(busy.c_str(), nullptr);
    }

    for (auto& npu : snapshot.npus) {
        if (npu.busId.empty()) continue;
        double celsius = 0.0;
        if (TemperatureForDevice(ResolvePath("/sys/bus/pci/devices/" + npu.busId), celsius))
            npu.temperatureC = celsius;
    }

    for (auto& device : snapshot.storage) {
        const std::string blockName = BaseName(device.devicePath);
        if (blockName.empty()) continue;
        double celsius = 0.0;
        if (TemperatureForDevice(ResolvePath("/sys/block/" + blockName + "/device"), celsius) ||
            TemperatureForDevice(ResolvePath("/sys/block/" + blockName), celsius))
            device.temperatureC = celsius;
    }

    for (auto& adapter : snapshot.network) {
        const std::string sysPath = "/sys/class/net/" + adapter.name;
        adapter.connected = ReadUnsigned(sysPath + "/carrier", 0) != 0;
        adapter.up = ReadTextFile(sysPath + "/operstate") != "down";
        adapter.bytesReceived = ReadUnsigned(sysPath + "/statistics/rx_bytes", adapter.bytesReceived);
        adapter.bytesSent = ReadUnsigned(sysPath + "/statistics/tx_bytes", adapter.bytesSent);
        if (adapter.wifi) {
            int qualityPercent = 0, levelDbm = 0;
            if (ReadWirelessSignal(adapter.name, qualityPercent, levelDbm)) {
                adapter.wifi->signalPercent = qualityPercent;
                adapter.wifi->signalDbm = levelDbm;
            }
        }
    }
}

} // namespace HardwareInfoBackend
} // namespace UltraCanvas
