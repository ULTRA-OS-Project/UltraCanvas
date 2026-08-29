// OS/MSWindows/UltraCanvasWindowsHardwareInfo.cpp
// Windows backend for UltraCanvasHardwareInfo, built on documented Win32 only -
// no COM, no WMI, no elevation. Inventory comes from the registry, the SMBIOS
// table (GetSystemFirmwareTable), the processor topology API, storage IOCTLs,
// IP Helper, WLAN, SetupAPI and the Bluetooth API. Anything a header may be too
// old to declare is guarded with __has_include and degrades to a warning.
// Version: 0.1.0
// Last Modified: 2026-08-29
// Author: UltraCanvas Framework

// The storage temperature IOCTL and the processor-group topology API are gated
// on a Windows 10 header floor. Raise it before any Windows header is pulled in,
// and keep NTDDI_VERSION consistent with it the way the SDK derives it by
// default - sdkddkver.h errors on a mismatch when a host build already asked for
// a higher _WIN32_WINNT. Same shape as UltraCanvasSpellCheckSupport.cpp.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00
#endif
#if !defined(NTDDI_VERSION) || (NTDDI_VERSION >> 16) < _WIN32_WINNT
#  undef NTDDI_VERSION
#  define NTDDI_VERSION (_WIN32_WINNT << 16)
#endif

#include "UltraCanvasHardwareInfoBackend.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
// winsock2.h must precede windows.h so the ancient winsock.h is never pulled in;
// iphlpapi.h and the Inet*A helpers both build on the v2 declarations.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <objbase.h>
#include <winioctl.h>
#include <iphlpapi.h>
#if __has_include(<ipifcons.h>)
    #include <ipifcons.h>
#endif

#if __has_include(<wlanapi.h>)
    #include <wlanapi.h>
    #define ULTRACANVAS_HAS_WLANAPI 1
#endif
#if __has_include(<setupapi.h>) && __has_include(<devguid.h>)
    #include <devguid.h>
    #include <setupapi.h>
    #define ULTRACANVAS_HAS_SETUPAPI 1
#endif
#if __has_include(<bluetoothapis.h>)
    #include <bluetoothapis.h>
    #define ULTRACANVAS_HAS_BLUETOOTHAPI 1
#endif

namespace UltraCanvas {
namespace HardwareInfoBackend {
namespace {

// ===== STRINGS AND REGISTRY =====

std::string WideToUtf8(const wchar_t* text) {
    if (!text || !*text) return std::string();
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return std::string();
    std::string result(static_cast<size_t>(length - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    return result;
}

std::string Trim(const std::string& text) {
    size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string RegReadString(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    wchar_t buffer[1024];
    DWORD size = sizeof(buffer);
    if (::RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ | RRF_RT_REG_MULTI_SZ,
                       nullptr, buffer, &size) != ERROR_SUCCESS)
        return std::string();
    return Trim(WideToUtf8(buffer));
}

uint64_t RegReadDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (::RegGetValueW(root, subKey, valueName, RRF_RT_REG_DWORD, nullptr, &value, &size)
        != ERROR_SUCCESS)
        return 0;
    return value;
}

uint64_t RegReadQword(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    uint64_t value = 0;
    DWORD size = sizeof(value);
    if (::RegGetValueW(root, subKey, valueName, RRF_RT_REG_QWORD, nullptr, &value, &size)
        != ERROR_SUCCESS)
        return 0;
    return value;
}

std::vector<std::wstring> RegSubKeys(HKEY root, const wchar_t* subKey) {
    std::vector<std::wstring> names;
    HKEY key = nullptr;
    if (::RegOpenKeyExW(root, subKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return names;
    for (DWORD index = 0;; ++index) {
        wchar_t name[256];
        DWORD length = static_cast<DWORD>(std::size(name));
        if (::RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr)
            != ERROR_SUCCESS) break;
        names.emplace_back(name, length);
    }
    ::RegCloseKey(key);
    return names;
}

// ===== SMBIOS =====
// GetSystemFirmwareTable('RSMB') hands the whole SMBIOS table to any process, so
// the memory-module detail that needs root on Linux is simply available here.

struct SmbiosStructure {
    uint8_t type = 0;
    std::vector<uint8_t> data;
    std::vector<std::string> strings;

    uint8_t  Byte(size_t offset) const { return offset < data.size() ? data[offset] : 0; }
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

const std::vector<SmbiosStructure>& SmbiosTable() {
    static std::vector<SmbiosStructure> structures;
    static bool loaded = false;
    if (loaded) return structures;
    loaded = true;

    const DWORD signature = 0x52534D42; // 'RSMB'
    const UINT size = ::GetSystemFirmwareTable(signature, 0, nullptr, 0);
    if (size == 0) return structures;
    std::vector<uint8_t> buffer(size);
    if (::GetSystemFirmwareTable(signature, 0, buffer.data(), size) != size) return structures;
    if (buffer.size() <= 8) return structures;

    // RawSMBIOSData: 4 header bytes, a DWORD length, then the table itself.
    const uint8_t* table = buffer.data() + 8;
    const size_t tableLength = buffer.size() - 8;
    size_t cursor = 0;
    while (cursor + 4 <= tableLength) {
        SmbiosStructure structure;
        structure.type = table[cursor];
        const size_t formattedLength = table[cursor + 1];
        if (formattedLength < 4 || cursor + formattedLength > tableLength) break;
        structure.data.assign(table + cursor, table + cursor + formattedLength);

        size_t stringCursor = cursor + formattedLength;
        while (stringCursor < tableLength) {
            std::string text;
            while (stringCursor < tableLength && table[stringCursor] != 0)
                text.push_back(static_cast<char>(table[stringCursor++]));
            ++stringCursor;
            if (text.empty()) break;
            structure.strings.push_back(Trim(text));
        }
        // A structure with no strings still carries the double terminator.
        if (stringCursor >= tableLength) { structures.push_back(std::move(structure)); break; }
        structures.push_back(std::move(structure));
        cursor = stringCursor;
        if (structures.size() > 4096) break;   // malformed table guard
    }
    return structures;
}

std::string SmbiosMemoryType(uint8_t code) {
    switch (code) {
        case 0x0F: return "SDRAM";
        case 0x12: return "DDR";
        case 0x13: return "DDR2";
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
        case 0x0D: return "SODIMM";
        case 0x0F: return "FB-DIMM";
        case 0x10: return "Die";
        default:   return std::string();
    }
}

std::string SmbiosChassisType(uint8_t code) {
    switch (code & 0x7F) {
        case 0x03: return "Desktop";
        case 0x04: return "Low profile desktop";
        case 0x06: return "Mini tower";
        case 0x07: return "Tower";
        case 0x08: return "Portable";
        case 0x09: return "Laptop";
        case 0x0A: return "Notebook";
        case 0x0E: return "Sub notebook";
        case 0x11: return "Main server chassis";
        case 0x1E: return "Tablet";
        case 0x1F: return "Convertible";
        case 0x20: return "Detachable";
        case 0x23: return "Mini PC";
        default:   return std::string();
    }
}

} // namespace

// ===== BACKEND IDENTITY =====

std::string BackendName() { return "win32"; }
bool IsAvailable() { return true; }

// ===== SYSTEM =====

void QuerySystem(SystemInfo& out, std::vector<std::string>& warnings) {
    wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD nameLength = static_cast<DWORD>(std::size(computerName));
    if (::GetComputerNameW(computerName, &nameLength)) out.hostName = WideToUtf8(computerName);

    const wchar_t* currentVersion = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    out.osName = RegReadString(HKEY_LOCAL_MACHINE, currentVersion, L"ProductName");
    const std::string display = RegReadString(HKEY_LOCAL_MACHINE, currentVersion, L"DisplayVersion");
    const std::string build = RegReadString(HKEY_LOCAL_MACHINE, currentVersion, L"CurrentBuild");
    out.osVersion = display.empty() ? build : display + " (build " + build + ")";
    out.kernelVersion = build.empty() ? std::string() : "Windows NT build " + build;

    SYSTEM_INFO systemInfo{};
    ::GetNativeSystemInfo(&systemInfo);
    switch (systemInfo.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: out.architecture = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: out.architecture = "aarch64"; break;
        case PROCESSOR_ARCHITECTURE_ARM:   out.architecture = "arm"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: out.architecture = "x86"; break;
        default: break;
    }

    const wchar_t* biosKey = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    out.manufacturer = RegReadString(HKEY_LOCAL_MACHINE, biosKey, L"SystemManufacturer");
    out.productName  = RegReadString(HKEY_LOCAL_MACHINE, biosKey, L"SystemProductName");
    out.boardVendor  = RegReadString(HKEY_LOCAL_MACHINE, biosKey, L"BaseBoardManufacturer");
    out.boardName    = RegReadString(HKEY_LOCAL_MACHINE, biosKey, L"BaseBoardProduct");
    out.biosVendor   = RegReadString(HKEY_LOCAL_MACHINE, biosKey, L"BIOSVendor");
    out.biosVersion  = RegReadString(HKEY_LOCAL_MACHINE, biosKey, L"BIOSVersion");
    out.biosDate     = RegReadString(HKEY_LOCAL_MACHINE, biosKey, L"BIOSReleaseDate");

    for (const auto& structure : SmbiosTable()) {
        if (structure.type != 3) continue;   // System Enclosure
        out.chassisType = SmbiosChassisType(structure.Byte(0x05));
        break;
    }

    out.uptimeSeconds = ::GetTickCount64() / 1000ull;

    if (out.manufacturer.empty())
        warnings.push_back("System identity is unavailable: the firmware wrote no manufacturer or "
                           "model into HARDWARE\\DESCRIPTION\\System\\BIOS.");
}

// ===== CPU =====
namespace {

// CallNtPowerInformation's per-processor block. Declared here rather than
// included: the struct lives in different headers across SDK and MinGW
// versions, and the call itself is resolved at run time from powrprof.dll, so
// the backend links nothing extra.
struct ProcessorPowerInformation {
    ULONG Number;
    ULONG MaxMhz;
    ULONG CurrentMhz;
    ULONG MhzLimit;
    ULONG MaxIdleState;
    ULONG CurrentIdleState;
};

using CallNtPowerInformationFn = LONG(WINAPI*)(int, PVOID, ULONG, PVOID, ULONG);

bool ReadProcessorClocks(double& outMaxMHz, double& outCurrentMHz, int logicalCores) {
    if (logicalCores <= 0) return false;
    static HMODULE powerModule = ::LoadLibraryW(L"powrprof.dll");
    if (!powerModule) return false;
    // GetProcAddress returns a generic FARPROC; go through void* so GCC does not
    // flag the cast between incompatible function types.
    static auto callNtPowerInformation = reinterpret_cast<CallNtPowerInformationFn>(
        reinterpret_cast<void*>(::GetProcAddress(powerModule, "CallNtPowerInformation")));
    if (!callNtPowerInformation) return false;

    std::vector<ProcessorPowerInformation> blocks(static_cast<size_t>(logicalCores));
    const ULONG bytes = static_cast<ULONG>(blocks.size() * sizeof(ProcessorPowerInformation));
    // 11 == ProcessorInformation in POWER_INFORMATION_LEVEL.
    if (callNtPowerInformation(11, nullptr, 0, blocks.data(), bytes) != 0) return false;

    double maximum = 0.0, current = 0.0;
    for (const auto& block : blocks) {
        maximum = std::max(maximum, static_cast<double>(block.MaxMhz));
        current += static_cast<double>(block.CurrentMhz);
    }
    outMaxMHz = maximum;
    outCurrentMHz = current / static_cast<double>(blocks.size());
    return true;
}

template <typename Relationship>
concept HasEfficiencyClass = requires(Relationship relationship) { relationship.EfficiencyClass; };

// The member access has to sit inside a template for the discarded branch to
// go uninstantiated; a plain `if constexpr` in a non-template function still
// has to parse both arms against the concrete struct.
template <typename Relationship>
int EfficiencyClassOf(const Relationship& relationship) {
    if constexpr (HasEfficiencyClass<Relationship>)
        return static_cast<int>(relationship.EfficiencyClass);
    else
        return 0;
}

int CountSetBits(ULONG_PTR mask) {
    int count = 0;
    while (mask) { count += static_cast<int>(mask & 1u); mask >>= 1; }
    return count;
}

void ReadTopologyAndCaches(CPUInfo& out) {
    DWORD length = 0;
    ::GetLogicalProcessorInformationEx(RelationAll, nullptr, &length);
    if (length == 0) return;
    std::vector<uint8_t> buffer(length);
    if (!::GetLogicalProcessorInformationEx(
            RelationAll,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &length))
        return;

    struct CacheKey {
        int level; int type; uint64_t size; int line; int assoc;
        bool operator<(const CacheKey& other) const {
            return std::tie(level, type, size, line, assoc) <
                   std::tie(other.level, other.type, other.size, other.line, other.assoc);
        }
    };
    std::map<CacheKey, int> cacheInstances;
    std::map<int, std::pair<int, int>> coresByEfficiency;   // class -> (physical, logical)
    int physicalCores = 0, logicalCores = 0, packages = 0;

    size_t offset = 0;
    while (offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= buffer.size()) {
        auto* record = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
        if (record->Size == 0) break;
        switch (record->Relationship) {
            case RelationProcessorCore: {
                ++physicalCores;
                int threads = 0;
                for (WORD group = 0; group < record->Processor.GroupCount; ++group)
                    threads += CountSetBits(record->Processor.GroupMask[group].Mask);
                logicalCores += threads;
                // EfficiencyClass (the hybrid P/E tier) was added to
                // PROCESSOR_RELATIONSHIP for Windows 10; older MinGW headers
                // still describe the Windows 7 struct, so ask the type itself.
                auto& bucket = coresByEfficiency[EfficiencyClassOf(record->Processor)];
                bucket.first += 1;
                bucket.second += threads;
                break;
            }
            case RelationProcessorPackage:
                ++packages;
                break;
            case RelationCache: {
                CacheKey key;
                key.level = record->Cache.Level;
                switch (record->Cache.Type) {
                    case CacheUnified:     key.type = static_cast<int>(CPUCacheType::Unified); break;
                    case CacheInstruction: key.type = static_cast<int>(CPUCacheType::Instruction); break;
                    case CacheData:        key.type = static_cast<int>(CPUCacheType::Data); break;
                    case CacheTrace:       key.type = static_cast<int>(CPUCacheType::Trace); break;
                    default:               key.type = static_cast<int>(CPUCacheType::Unknown); break;
                }
                key.size = record->Cache.CacheSize;
                key.line = record->Cache.LineSize;
                key.assoc = record->Cache.Associativity == CACHE_FULLY_ASSOCIATIVE
                                ? -1 : record->Cache.Associativity;
                if (key.level > 0 && key.size > 0) ++cacheInstances[key];
                break;
            }
            default:
                break;
        }
        offset += record->Size;
    }

    out.physicalCores = physicalCores;
    out.logicalCores = logicalCores;
    out.packages = packages > 0 ? packages : 1;

    for (const auto& entry : cacheInstances) {
        CPUCacheInfo cache;
        cache.level = entry.first.level;
        cache.type = static_cast<CPUCacheType>(entry.first.type);
        cache.sizeBytes = entry.first.size;
        cache.lineSizeBytes = entry.first.line;
        cache.associativity = entry.first.assoc;
        cache.instanceCount = entry.second;
        out.caches.push_back(cache);
    }
    std::sort(out.caches.begin(), out.caches.end(),
              [](const CPUCacheInfo& a, const CPUCacheInfo& b) {
                  if (a.level != b.level) return a.level < b.level;
                  return static_cast<int>(a.type) < static_cast<int>(b.type);
              });

    // More than one efficiency class means a hybrid package; the highest class
    // number is the performance tier.
    if (coresByEfficiency.size() > 1) {
        for (auto it = coresByEfficiency.rbegin(); it != coresByEfficiency.rend(); ++it) {
            CPUCoreGroup group;
            group.name = (it == coresByEfficiency.rbegin()) ? "Performance cores" : "Efficiency cores";
            group.physicalCores = it->second.first;
            group.logicalCores = it->second.second;
            out.coreGroups.push_back(std::move(group));
        }
    }
}

// Older MinGW-w64 winnt.h predates these; the values are fixed by the ABI, so
// define the missing ones rather than compiling the feature out - an #ifdef that
// drops the entry silently under-reports the CPU instead of failing the build.
// Same fix, same values as OS/MSWindows/UltraCanvasWindowsDiagnostics.cpp.
#ifndef PF_ARM_NEON_INSTRUCTIONS_AVAILABLE
#define PF_ARM_NEON_INSTRUCTIONS_AVAILABLE 19
#endif
#ifndef PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE
#define PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE 30
#endif

// x86 features come from CPUID through the shared detector, not from
// IsProcessorFeaturePresent: Win32 has PF_* constants for only a handful of
// extensions and none for GFNI, VAES or VPCLMULQDQ - the VEX-encoded ones a
// -march=native build picks up without needing AVX-512, and the ones that
// therefore fault first on an older machine. Under emulation CPUID is answered
// by the emulator, so it still describes what this process may execute.
//
// ARM64 has no CPUID; there the Win32 query is the only source, so the PF_*
// table stays for it.
void ReadInstructionSets(CPUInfo& out) {
    X86CpuFeatures x86Features;
    if (ReadX86CpuFeatures(x86Features)) {
        AppendX86FeatureNames(x86Features, out.instructionSets);
        out.x86MicroarchitectureLevel = X86MicroarchitectureLevel(x86Features);
        return;
    }
    struct Feature { DWORD id; const char* name; };
    static const Feature kArmFeatures[] = {
        { PF_ARM_NEON_INSTRUCTIONS_AVAILABLE,        "NEON" },
        { PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE,   "ARMv8 Crypto" }
    };
    for (const auto& feature : kArmFeatures)
        if (::IsProcessorFeaturePresent(feature.id)) out.instructionSets.push_back(feature.name);
}

// "x64 image on an ARM64 machine" and friends, or empty when the process runs
// natively. Resolved at run time: IsWow64Process2 arrived in Windows 10 1511,
// and importing it statically would keep the library off anything older.
std::string EmulationDescription() {
    using IsWow64Process2Func = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
    if (!kernel32) return std::string();
    auto isWow64Process2 = reinterpret_cast<IsWow64Process2Func>(
        reinterpret_cast<void*>(::GetProcAddress(kernel32, "IsWow64Process2")));
    if (!isWow64Process2) return std::string();

    USHORT processMachine = 0, nativeMachine = 0;
    if (!isWow64Process2(::GetCurrentProcess(), &processMachine, &nativeMachine))
        return std::string();
    // IMAGE_FILE_MACHINE_UNKNOWN for the process means "not under WOW64": the
    // image matches the machine, and there is nothing to report.
    if (processMachine == IMAGE_FILE_MACHINE_UNKNOWN) return std::string();

    auto MachineName = [](USHORT machine) -> const char* {
        switch (machine) {
            case IMAGE_FILE_MACHINE_ARM64: return "ARM64";
            case IMAGE_FILE_MACHINE_AMD64: return "x64";
            case IMAGE_FILE_MACHINE_I386:  return "x86";
            case IMAGE_FILE_MACHINE_ARMNT: return "ARM32";
            default: return nullptr;
        }
    };
    const char* native = MachineName(nativeMachine);
    const char* image = MachineName(processMachine);
    if (!native) return std::string();
    return std::string(image ? image : "unknown") + " image on a " + native + " machine";
}

} // namespace

void QueryCPU(CPUInfo& out, bool includeSensors, std::vector<std::string>& warnings) {
    const wchar_t* processorKey = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    out.model = RegReadString(HKEY_LOCAL_MACHINE, processorKey, L"ProcessorNameString");
    out.vendor = RegReadString(HKEY_LOCAL_MACHINE, processorKey, L"VendorIdentifier");
    out.stepping = RegReadString(HKEY_LOCAL_MACHINE, processorKey, L"Identifier");

    SYSTEM_INFO systemInfo{};
    ::GetNativeSystemInfo(&systemInfo);
    switch (systemInfo.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: out.architecture = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: out.architecture = "aarch64"; break;
        case PROCESSOR_ARCHITECTURE_ARM:   out.architecture = "arm"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: out.architecture = "x86"; break;
        default: break;
    }

    ReadTopologyAndCaches(out);
    if (out.logicalCores <= 0)
        out.logicalCores = static_cast<int>(::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    if (out.physicalCores <= 0) out.physicalCores = out.logicalCores;

    // The registry's ~MHz is the nominal (base) clock the firmware reported.
    out.baseClockMHz = static_cast<double>(RegReadDword(HKEY_LOCAL_MACHINE, processorKey, L"~MHz"));
    double maxMHz = 0.0, currentMHz = 0.0;
    if (ReadProcessorClocks(maxMHz, currentMHz, out.logicalCores)) {
        out.maxClockMHz = maxMHz;
        out.currentClockMHz = currentMHz;
        for (auto& group : out.coreGroups)
            if (group.maxClockMHz <= 0.0) group.maxClockMHz = maxMHz;
    }
    ReadInstructionSets(out);

    out.emulation = EmulationDescription();
    if (!out.emulation.empty())
        warnings.push_back("This process is not running natively (" + out.emulation + "): the model "
                           "and core counts describe the machine, but the instruction sets are the "
                           "ones the emulator permits, which are a subset (Windows on ARM offers no "
                           "AVX-512 at all).");

    if (includeSensors)
        warnings.push_back("CPU temperature is unavailable on Windows through this backend: the "
                           "reading is exposed only through WMI (root\\WMI, "
                           "MSAcpi_ThermalZoneTemperature), which needs COM.");
}

// ===== MEMORY =====

void QueryMemory(MemoryInfo& out, std::vector<std::string>& warnings) {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (::GlobalMemoryStatusEx(&status)) {
        out.totalBytes = status.ullTotalPhys;
        out.availableBytes = status.ullAvailPhys;
        out.usedBytes = status.ullTotalPhys - status.ullAvailPhys;
        // Windows reports a commit limit rather than a swap file size; the
        // difference from physical memory is what the page file adds.
        if (status.ullTotalPageFile > status.ullTotalPhys) {
            out.swapTotalBytes = status.ullTotalPageFile - status.ullTotalPhys;
            const uint64_t availablePageFile =
                status.ullAvailPageFile > status.ullAvailPhys
                    ? status.ullAvailPageFile - status.ullAvailPhys : 0;
            out.swapFreeBytes = std::min(out.swapTotalBytes, availablePageFile);
        }
    }
    SYSTEM_INFO systemInfo{};
    ::GetSystemInfo(&systemInfo);
    out.pageSizeBytes = systemInfo.dwPageSize;

    bool sawModules = false;
    for (const auto& structure : SmbiosTable()) {
        if (structure.type != 17) continue;   // Memory Device
        sawModules = true;
        ++out.slotsTotal;

        MemoryModuleInfo module;
        const uint16_t sizeField = structure.Word(0x0C);
        if (sizeField == 0 || sizeField == 0xFFFF) continue;
        if (sizeField == 0x7FFF)
            module.sizeBytes = static_cast<uint64_t>(structure.DWord(0x1C)) * 1024ull * 1024ull;
        else if (sizeField & 0x8000)
            module.sizeBytes = static_cast<uint64_t>(sizeField & 0x7FFF) * 1024ull;
        else
            module.sizeBytes = static_cast<uint64_t>(sizeField) * 1024ull * 1024ull;
        if (module.sizeBytes == 0) continue;
        ++out.slotsUsed;

        module.totalWidthBits = structure.Word(0x08) == 0xFFFF ? 0 : structure.Word(0x08);
        module.dataWidthBits  = structure.Word(0x0A) == 0xFFFF ? 0 : structure.Word(0x0A);
        module.formFactor     = SmbiosFormFactor(structure.Byte(0x0E));
        module.locator        = structure.String(0x10);
        module.bankLocator    = structure.String(0x11);
        module.type           = SmbiosMemoryType(structure.Byte(0x12));
        module.ratedSpeedMTs  = structure.Word(0x15);
        module.manufacturer   = structure.String(0x17);
        module.serialNumber   = structure.String(0x18);
        module.partNumber     = structure.String(0x1A);
        module.speedMTs       = structure.Word(0x20);
        const uint16_t configuredVoltageMilliVolts = structure.Word(0x26);
        if (configuredVoltageMilliVolts > 0)
            module.voltageVolts = configuredVoltageMilliVolts / 1000.0;
        if (module.ratedSpeedMTs == 0xFFFF)
            module.ratedSpeedMTs = static_cast<int>(structure.DWord(0x54));
        if (module.speedMTs == 0xFFFF)
            module.speedMTs = static_cast<int>(structure.DWord(0x58));
        if (module.speedMTs == 0) module.speedMTs = module.ratedSpeedMTs;
        out.modules.push_back(std::move(module));
    }
    if (!sawModules)
        warnings.push_back("Per-module memory detail is unavailable: this firmware exports no "
                           "SMBIOS memory-device records.");
}

// ===== GRAPHICS AND ACCELERATORS =====
namespace {

// Every display adapter has a class key under Control\Class\{class GUID}\NNNN
// holding the driver's own description, version and reported memory size. This
// is the same data the display control panel shows, without COM.
void ReadDeviceClass(const wchar_t* classGuid,
                     const std::function<void(const std::wstring& keyPath,
                                              const std::string& description,
                                              const std::string& driverVersion,
                                              const std::string& provider,
                                              const std::string& matchingDeviceId)>& visit) {
    const std::wstring root = std::wstring(L"SYSTEM\\CurrentControlSet\\Control\\Class\\") + classGuid;
    for (const auto& subKey : RegSubKeys(HKEY_LOCAL_MACHINE, root.c_str())) {
        // Only the numbered instance keys are adapters; skip "Properties" etc.
        if (subKey.size() != 4 || !iswdigit(subKey[0])) continue;
        const std::wstring keyPath = root + L"\\" + subKey;
        const std::string description = RegReadString(HKEY_LOCAL_MACHINE, keyPath.c_str(), L"DriverDesc");
        if (description.empty()) continue;
        visit(keyPath,
              description,
              RegReadString(HKEY_LOCAL_MACHINE, keyPath.c_str(), L"DriverVersion"),
              RegReadString(HKEY_LOCAL_MACHINE, keyPath.c_str(), L"ProviderName"),
              RegReadString(HKEY_LOCAL_MACHINE, keyPath.c_str(), L"MatchingDeviceId"));
    }
}

// "pci\ven_10de&dev_2504&subsys_..." -> 0x10DE, 0x2504
void ParseMatchingDeviceId(const std::string& id, uint16_t& vendorId, uint16_t& deviceId) {
    std::string lower;
    lower.reserve(id.size());
    for (char character : id) lower.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(character))));
    const size_t vendorPosition = lower.find("ven_");
    if (vendorPosition != std::string::npos && vendorPosition + 8 <= lower.size())
        vendorId = static_cast<uint16_t>(std::strtoul(lower.substr(vendorPosition + 4, 4).c_str(), nullptr, 16));
    const size_t devicePosition = lower.find("dev_");
    if (devicePosition != std::string::npos && devicePosition + 8 <= lower.size())
        deviceId = static_cast<uint16_t>(std::strtoul(lower.substr(devicePosition + 4, 4).c_str(), nullptr, 16));
}

} // namespace

void QueryGPUs(std::vector<GPUInfo>& out, bool includeSensors, std::vector<std::string>& warnings) {
    ReadDeviceClass(L"{4d36e968-e325-11ce-bfc1-08002be10318}",
        [&out](const std::wstring& keyPath, const std::string& description,
               const std::string& driverVersion, const std::string& provider,
               const std::string& matchingDeviceId) {
            GPUInfo gpu;
            gpu.model = description;
            gpu.vendor = provider;
            gpu.driverVersion = driverVersion;
            ParseMatchingDeviceId(matchingDeviceId, gpu.pciVendorId, gpu.pciDeviceId);
            gpu.videoMemoryBytes = RegReadQword(HKEY_LOCAL_MACHINE, keyPath.c_str(),
                                                L"HardwareInformation.qwMemorySize");
            if (gpu.videoMemoryBytes == 0)
                gpu.videoMemoryBytes = RegReadDword(HKEY_LOCAL_MACHINE, keyPath.c_str(),
                                                    L"HardwareInformation.MemorySize");
            switch (gpu.pciVendorId) {
                case 0x10DE: gpu.vendor = "NVIDIA"; gpu.kind = GPUKind::Discrete; break;
                case 0x1002: gpu.vendor = "AMD"; break;
                case 0x8086: gpu.vendor = "Intel"; gpu.kind = GPUKind::Integrated; break;
                case 0x1414: gpu.vendor = "Microsoft"; gpu.kind = GPUKind::Software; break;
                default: break;
            }
            out.push_back(std::move(gpu));
        });

    if (includeSensors)
        warnings.push_back("GPU temperature and utilisation are unavailable on Windows through this "
                           "backend: they come from the vendor's own library (NVML, ADLX) or from "
                           "performance counters.");
    if (out.empty())
        warnings.push_back("No display adapter is registered under the display device class.");
}

void QueryNPUs(std::vector<NPUInfo>& out, bool includeSensors, std::vector<std::string>& warnings) {
    (void)includeSensors; (void)warnings;
    // Windows registers NPUs under the Compute Accelerator device class, which
    // is what the "Neural processors" node in Device Manager shows.
    ReadDeviceClass(L"{f01a9d53-3ff6-48d2-9f97-c8a7004be10c}",
        [&out](const std::wstring&, const std::string& description,
               const std::string& driverVersion, const std::string& provider,
               const std::string& matchingDeviceId) {
            NPUInfo npu;
            npu.model = description;
            npu.vendor = provider;
            npu.driverVersion = driverVersion;
            npu.runtime = "DirectML / Windows ML";
            ParseMatchingDeviceId(matchingDeviceId, npu.pciVendorId, npu.pciDeviceId);
            out.push_back(std::move(npu));
        });
}

// ===== STORAGE =====
namespace {

StorageBus MapBusType(STORAGE_BUS_TYPE bus) {
    switch (bus) {
        case BusTypeAta:      return StorageBus::IDE;
        case BusTypeAtapi:    return StorageBus::IDE;
        case BusTypeSata:     return StorageBus::SATA;
        case BusTypeSas:      return StorageBus::SAS;
        case BusTypeScsi:     return StorageBus::SCSI;
        case BusTypeUsb:      return StorageBus::USB;
        case BusTypeNvme:     return StorageBus::NVMe;
        case BusTypeSd:       return StorageBus::SD;
        case BusTypeMmc:      return StorageBus::MMC;
        case BusTypeVirtual:  return StorageBus::Virtual;
        case BusTypeFileBackedVirtual: return StorageBus::Virtual;
        default:              return StorageBus::Unknown;
    }
}

std::string DescriptorString(const STORAGE_DEVICE_DESCRIPTOR* descriptor, DWORD offset) {
    if (offset == 0) return std::string();
    return Trim(reinterpret_cast<const char*>(descriptor) + offset);
}

// Which physical drive each mounted volume lives on, so a drive can list the
// volumes carved out of it.
std::map<DWORD, std::vector<StorageVolumeInfo>> ReadVolumesByDisk() {
    std::map<DWORD, std::vector<StorageVolumeInfo>> volumesByDisk;
    wchar_t driveStrings[512] = {0};
    const DWORD length = ::GetLogicalDriveStringsW(static_cast<DWORD>(std::size(driveStrings)),
                                                   driveStrings);
    if (length == 0 || length > std::size(driveStrings)) return volumesByDisk;

    for (const wchar_t* root = driveStrings; *root; root += wcslen(root) + 1) {
        if (::GetDriveTypeW(root) == DRIVE_REMOTE) continue;

        std::wstring devicePath = L"\\\\.\\";
        devicePath += root;
        if (!devicePath.empty() && devicePath.back() == L'\\') devicePath.pop_back();
        HANDLE volume = ::CreateFileW(devicePath.c_str(), 0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, 0, nullptr);
        if (volume == INVALID_HANDLE_VALUE) continue;

        uint8_t extentBuffer[1024] = {0};
        DWORD returned = 0;
        const BOOL gotExtents = ::DeviceIoControl(volume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                                                  nullptr, 0, extentBuffer, sizeof(extentBuffer),
                                                  &returned, nullptr);
        ::CloseHandle(volume);
        if (!gotExtents) continue;
        const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(extentBuffer);
        if (extents->NumberOfDiskExtents == 0) continue;

        StorageVolumeInfo info;
        info.mountPoint = WideToUtf8(root);
        wchar_t label[MAX_PATH + 1] = {0};
        wchar_t fileSystem[64] = {0};
        DWORD flags = 0;
        if (::GetVolumeInformationW(root, label, static_cast<DWORD>(std::size(label)), nullptr,
                                    nullptr, &flags, fileSystem,
                                    static_cast<DWORD>(std::size(fileSystem)))) {
            info.label = WideToUtf8(label);
            info.fileSystem = WideToUtf8(fileSystem);
            info.readOnly = (flags & FILE_READ_ONLY_VOLUME) != 0;
        }
        ULARGE_INTEGER available{}, total{}, free{};
        if (::GetDiskFreeSpaceExW(root, &available, &total, &free)) {
            info.totalBytes = total.QuadPart;
            info.freeBytes = available.QuadPart;
        }
        volumesByDisk[extents->Extents[0].DiskNumber].push_back(std::move(info));
    }
    return volumesByDisk;
}

bool ReadDriveTemperature(HANDLE drive, double& outCelsius) {
#if defined(NTDDI_WIN10_RS3) && (NTDDI_VERSION >= NTDDI_WIN10_RS3)
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceTemperatureProperty;
    query.QueryType = PropertyStandardQuery;
    uint8_t buffer[2048] = {0};
    DWORD returned = 0;
    if (!::DeviceIoControl(drive, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                           buffer, sizeof(buffer), &returned, nullptr))
        return false;
    const auto* descriptor = reinterpret_cast<const STORAGE_TEMPERATURE_DATA_DESCRIPTOR*>(buffer);
    if (descriptor->InfoCount == 0) return false;
    const SHORT temperature = descriptor->TemperatureInfo[0].Temperature;
    if (temperature == SHRT_MIN || temperature == 0) return false;
    outCelsius = static_cast<double>(temperature);
    return true;
#else
    (void)drive; (void)outCelsius;
    return false;
#endif
}

} // namespace

void QueryStorage(std::vector<StorageDeviceInfo>& out, bool includeSensors,
                  std::vector<std::string>& warnings) {
    const auto volumesByDisk = ReadVolumesByDisk();
    bool sawTemperature = false;

    for (DWORD number = 0; number < 32; ++number) {
        wchar_t path[64];
        ::swprintf(path, std::size(path), L"\\\\.\\PhysicalDrive%lu", static_cast<unsigned long>(number));
        // Zero access rights: enough for the query IOCTLs, and the only form
        // that opens without administrator rights.
        HANDLE drive = ::CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                     OPEN_EXISTING, 0, nullptr);
        if (drive == INVALID_HANDLE_VALUE) continue;

        StorageDeviceInfo device;
        char narrowPath[64];
        ::snprintf(narrowPath, sizeof(narrowPath), "\\\\.\\PhysicalDrive%lu",
                   static_cast<unsigned long>(number));
        device.devicePath = narrowPath;

        STORAGE_PROPERTY_QUERY query{};
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        uint8_t descriptorBuffer[4096] = {0};
        DWORD returned = 0;
        if (::DeviceIoControl(drive, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                              descriptorBuffer, sizeof(descriptorBuffer), &returned, nullptr)) {
            const auto* descriptor =
                reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(descriptorBuffer);
            device.vendor = DescriptorString(descriptor, descriptor->VendorIdOffset);
            device.model = DescriptorString(descriptor, descriptor->ProductIdOffset);
            device.firmwareVersion = DescriptorString(descriptor, descriptor->ProductRevisionOffset);
            device.serialNumber = DescriptorString(descriptor, descriptor->SerialNumberOffset);
            device.removable = descriptor->RemovableMedia != FALSE;
            device.bus = MapBusType(descriptor->BusType);
        }
        if (device.model.empty()) device.model = "Disk " + std::to_string(number);

        GET_LENGTH_INFORMATION lengthInformation{};
        if (::DeviceIoControl(drive, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0,
                              &lengthInformation, sizeof(lengthInformation), &returned, nullptr))
            device.capacityBytes = static_cast<uint64_t>(lengthInformation.Length.QuadPart);

        STORAGE_PROPERTY_QUERY seekQuery{};
        seekQuery.PropertyId = StorageDeviceSeekPenaltyProperty;
        seekQuery.QueryType = PropertyStandardQuery;
        DEVICE_SEEK_PENALTY_DESCRIPTOR seekDescriptor{};
        if (::DeviceIoControl(drive, IOCTL_STORAGE_QUERY_PROPERTY, &seekQuery, sizeof(seekQuery),
                              &seekDescriptor, sizeof(seekDescriptor), &returned, nullptr))
            device.media = seekDescriptor.IncursSeekPenalty ? StorageMedia::HDD : StorageMedia::SSD;
        else if (device.bus == StorageBus::NVMe)
            device.media = StorageMedia::SSD;

        DISK_GEOMETRY geometry{};
        if (::DeviceIoControl(drive, IOCTL_DISK_GET_DRIVE_GEOMETRY, nullptr, 0,
                              &geometry, sizeof(geometry), &returned, nullptr))
            device.logicalSectorSize = static_cast<int>(geometry.BytesPerSector);

        switch (device.bus) {
            case StorageBus::NVMe: device.connector = "PCIe (NVMe)"; break;
            case StorageBus::SATA: device.connector = "SATA"; break;
            case StorageBus::USB:  device.connector = "USB"; break;
            case StorageBus::SD:   device.connector = "SD card"; break;
            case StorageBus::MMC:  device.connector = "eMMC"; break;
            default: break;
        }

        if (includeSensors) {
            double celsius = 0.0;
            if (ReadDriveTemperature(drive, celsius)) {
                device.temperatureC = celsius;
                sawTemperature = true;
            }
        }
        ::CloseHandle(drive);

        auto volumes = volumesByDisk.find(number);
        if (volumes != volumesByDisk.end()) device.volumes = volumes->second;
        out.push_back(std::move(device));
    }

    if (includeSensors && !out.empty() && !sawTemperature)
        warnings.push_back("Drive temperatures are unavailable: the storage driver answered no "
                           "temperature query (the drive or its driver does not report one).");
}

// ===== NETWORK =====
namespace {

#ifdef ULTRACANVAS_HAS_WLANAPI
// The DOT11_PHY_TYPE and DOT11_AUTH_ALGORITHM enumerators are compared by value
// rather than by name: the newer members (VHT, HE, EHT, WPA3) are missing from
// older MinGW and SDK headers, and the values are fixed by the standard.
std::string Dot11PhyTypeName(DOT11_PHY_TYPE type) {
    switch (static_cast<int>(type)) {
        case 1:  return "802.11 FHSS";
        case 2:  return "802.11 DSSS";
        case 3:  return "802.11 infrared";
        case 4:  return "802.11a";
        case 5:  return "802.11b";
        case 6:  return "802.11g";
        case 7:  return "802.11n (Wi-Fi 4)";
        case 8:  return "802.11ac (Wi-Fi 5)";
        case 9:  return "802.11ad";
        case 10: return "802.11ax (Wi-Fi 6)";
        case 11: return "802.11be (Wi-Fi 7)";
        default: return std::string();
    }
}

std::string SecurityName(DOT11_AUTH_ALGORITHM algorithm) {
    switch (static_cast<int>(algorithm)) {
        case 1:  return "Open";
        case 2:  return "WEP shared key";
        case 3:  return "WPA Enterprise";
        case 4:  return "WPA Personal";
        case 5:  return "WPA none";
        case 6:  return "WPA2 Enterprise";
        case 7:  return "WPA2 Personal";
        case 8:  return "WPA3 Enterprise 192-bit";
        case 9:  return "WPA3 Personal (SAE)";
        case 10: return "Enhanced Open (OWE)";
        case 11: return "WPA3 Enterprise";
        default: return std::string();
    }
}

// SSID and signal for every associated wireless interface, keyed by the
// adapter GUID that GetAdaptersAddresses reports.
std::map<std::string, WiFiInfo> ReadWiFiConnections(std::vector<std::string>& warnings) {
    std::map<std::string, WiFiInfo> connections;
    DWORD negotiatedVersion = 0;
    HANDLE client = nullptr;
    if (::WlanOpenHandle(2, nullptr, &negotiatedVersion, &client) != ERROR_SUCCESS) return connections;

    WLAN_INTERFACE_INFO_LIST* interfaces = nullptr;
    if (::WlanEnumInterfaces(client, nullptr, &interfaces) == ERROR_SUCCESS && interfaces) {
        for (DWORD index = 0; index < interfaces->dwNumberOfItems; ++index) {
            const WLAN_INTERFACE_INFO& interfaceInfo = interfaces->InterfaceInfo[index];
            wchar_t guidText[64] = {0};
            ::StringFromGUID2(interfaceInfo.InterfaceGuid, guidText,
                              static_cast<int>(std::size(guidText)));
            WiFiInfo wifi;

            DWORD dataSize = 0;
            void* data = nullptr;
            WLAN_OPCODE_VALUE_TYPE opcodeType = wlan_opcode_value_type_invalid;
            if (::WlanQueryInterface(client, &interfaceInfo.InterfaceGuid,
                                     wlan_intf_opcode_current_connection, nullptr,
                                     &dataSize, &data, &opcodeType) == ERROR_SUCCESS && data) {
                const auto* attributes = static_cast<const WLAN_CONNECTION_ATTRIBUTES*>(data);
                const auto& association = attributes->wlanAssociationAttributes;
                wifi.connected = attributes->isState == wlan_interface_state_connected;
                wifi.ssid.assign(reinterpret_cast<const char*>(association.dot11Ssid.ucSSID),
                                 association.dot11Ssid.uSSIDLength);
                char bssid[18];
                ::snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x",
                           association.dot11Bssid[0], association.dot11Bssid[1],
                           association.dot11Bssid[2], association.dot11Bssid[3],
                           association.dot11Bssid[4], association.dot11Bssid[5]);
                wifi.bssid = bssid;
                wifi.signalPercent = static_cast<int>(association.wlanSignalQuality);
                wifi.txRateMbps = association.ulTxRate / 1000.0;
                wifi.rxRateMbps = association.ulRxRate / 1000.0;
                wifi.standard = Dot11PhyTypeName(association.dot11PhyType);
                wifi.security = SecurityName(attributes->wlanSecurityAttributes.dot11AuthAlgorithm);
                ::WlanFreeMemory(data);
            }
            connections[WideToUtf8(guidText)] = std::move(wifi);
        }
        ::WlanFreeMemory(interfaces);
    }
    ::WlanCloseHandle(client, nullptr);
    if (connections.empty())
        warnings.push_back("No wireless interface is reported by the WLAN service.");
    return connections;
}
#endif // ULTRACANVAS_HAS_WLANAPI

} // namespace

void QueryNetwork(std::vector<NetworkInterfaceInfo>& out, std::vector<std::string>& warnings) {
    ULONG bufferSize = 32 * 1024;
    std::vector<uint8_t> buffer(bufferSize);
    ULONG result = ::GetAdaptersAddresses(AF_UNSPEC,
                                          GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                          GAA_FLAG_SKIP_DNS_SERVER,
                                          nullptr,
                                          reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),
                                          &bufferSize);
    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(bufferSize);
        result = ::GetAdaptersAddresses(AF_UNSPEC,
                                        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                        GAA_FLAG_SKIP_DNS_SERVER,
                                        nullptr,
                                        reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),
                                        &bufferSize);
    }
    if (result != NO_ERROR) {
        warnings.push_back("Network interfaces are unavailable: GetAdaptersAddresses failed.");
        return;
    }

#ifdef ULTRACANVAS_HAS_WLANAPI
    const auto wifiConnections = ReadWiFiConnections(warnings);
#endif

    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
         adapter; adapter = adapter->Next) {
        NetworkInterfaceInfo info;
        info.name = adapter->AdapterName ? adapter->AdapterName : "";
        info.description = WideToUtf8(adapter->Description);
        info.mtu = static_cast<int>(adapter->Mtu);
        info.up = adapter->OperStatus == IfOperStatusUp;
        info.connected = info.up;
        info.linkSpeedMbps = static_cast<double>(adapter->TransmitLinkSpeed) / 1000000.0;
        if (adapter->FriendlyName) {
            const std::string friendlyName = WideToUtf8(adapter->FriendlyName);
            if (!friendlyName.empty()) info.name = friendlyName;
        }

        switch (adapter->IfType) {
            case IF_TYPE_ETHERNET_CSMACD: info.type = NetworkLinkType::Ethernet; break;
            case IF_TYPE_IEEE80211:       info.type = NetworkLinkType::WiFi; break;
            case IF_TYPE_SOFTWARE_LOOPBACK: info.type = NetworkLinkType::Loopback; break;
            case IF_TYPE_TUNNEL:          info.type = NetworkLinkType::Tunnel; break;
            case IF_TYPE_PPP:             info.type = NetworkLinkType::Cellular; break;
            default:                      info.type = NetworkLinkType::Unknown; break;
        }

        if (adapter->PhysicalAddressLength == 6) {
            char mac[18];
            ::snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                       adapter->PhysicalAddress[0], adapter->PhysicalAddress[1],
                       adapter->PhysicalAddress[2], adapter->PhysicalAddress[3],
                       adapter->PhysicalAddress[4], adapter->PhysicalAddress[5]);
            info.macAddress = mac;
        }

        for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            char text[NI_MAXHOST] = {0};
            const sockaddr* address = unicast->Address.lpSockaddr;
            if (!address) continue;
            if (address->sa_family == AF_INET) {
                const auto* in4 = reinterpret_cast<const sockaddr_in*>(address);
                if (::InetNtopA(AF_INET, &in4->sin_addr, text, sizeof(text)))
                    info.ipv4Addresses.push_back(text);
            } else if (address->sa_family == AF_INET6) {
                const auto* in6 = reinterpret_cast<const sockaddr_in6*>(address);
                if (::InetNtopA(AF_INET6, &in6->sin6_addr, text, sizeof(text)))
                    info.ipv6Addresses.push_back(text);
            }
        }

#ifdef ULTRACANVAS_HAS_WLANAPI
        if (info.type == NetworkLinkType::WiFi && adapter->AdapterName) {
            // GetAdaptersAddresses spells the GUID without braces; WLAN's
            // StringFromGUID2 adds them.
            const std::string braced = std::string("{") + adapter->AdapterName + "}";
            auto connection = wifiConnections.find(braced);
            if (connection != wifiConnections.end()) info.wifi = connection->second;
        }
#endif
        if (info.type == NetworkLinkType::WiFi && !info.wifi) {
            info.wifi = WiFiInfo{};
#ifndef ULTRACANVAS_HAS_WLANAPI
            warnings.push_back("Wi-Fi network details are unavailable: this build was compiled "
                               "without the WLAN API headers.");
#endif
        }
        out.push_back(std::move(info));
    }
}

// ===== USB =====

void QueryUSB(std::vector<USBControllerInfo>& controllers, std::vector<USBDeviceInfo>& devices,
              bool includeHubs, std::vector<std::string>& warnings) {
#ifdef ULTRACANVAS_HAS_SETUPAPI
    auto ReadProperty = [](HDEVINFO set, SP_DEVINFO_DATA& data, DWORD property) -> std::string {
        wchar_t buffer[512] = {0};
        DWORD type = 0;
        if (!::SetupDiGetDeviceRegistryPropertyW(set, &data, property, &type,
                                                 reinterpret_cast<PBYTE>(buffer),
                                                 sizeof(buffer), nullptr))
            return std::string();
        return Trim(WideToUtf8(buffer));
    };

    // GUID_DEVCLASS_USB covers hubs and host controllers; the devices a person
    // plugged in are enumerated by the "USB" enumerator instead.
    HDEVINFO deviceSet = ::SetupDiGetClassDevsW(nullptr, L"USB", nullptr,
                                                DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (deviceSet == INVALID_HANDLE_VALUE) {
        warnings.push_back("USB devices are unavailable: the device enumerator could not be opened.");
        return;
    }

    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);
    for (DWORD index = 0; ::SetupDiEnumDeviceInfo(deviceSet, index, &data); ++index) {
        wchar_t instanceId[512] = {0};
        if (!::SetupDiGetDeviceInstanceIdW(deviceSet, &data, instanceId,
                                           static_cast<DWORD>(std::size(instanceId)), nullptr))
            continue;
        const std::string id = WideToUtf8(instanceId);

        USBDeviceInfo device;
        ParseMatchingDeviceId(id, device.vendorId, device.productId);
        device.productName = ReadProperty(deviceSet, data, SPDRP_DEVICEDESC);
        device.vendorName = ReadProperty(deviceSet, data, SPDRP_MFG);
        device.deviceClass = ReadProperty(deviceSet, data, SPDRP_CLASS);
        device.portPath = ReadProperty(deviceSet, data, SPDRP_LOCATION_INFORMATION);
        // The instance path's last element is the device's serial when the
        // device reports one, and a bus-generated id (starting with '&') when
        // it does not.
        const size_t lastSeparator = id.find_last_of('\\');
        if (lastSeparator != std::string::npos) {
            const std::string tail = id.substr(lastSeparator + 1);
            if (!tail.empty() && tail.find('&') == std::string::npos) device.serialNumber = tail;
        }

        const bool isHub = device.deviceClass == "USB" &&
                           device.productName.find("Hub") != std::string::npos;
        device.isHub = isHub;

        if (device.productName.find("Host Controller") != std::string::npos) {
            USBControllerInfo controller;
            controller.name = device.productName;
            controller.driver = device.vendorName;
            controller.busId = device.portPath;
            controllers.push_back(std::move(controller));
            continue;
        }
        if (isHub && !includeHubs) continue;
        devices.push_back(std::move(device));
    }
    ::SetupDiDestroyDeviceInfoList(deviceSet);

    warnings.push_back("USB link speed and bus power are not reported by this backend: they come "
                       "from the hub driver's per-port IOCTLs rather than from the device registry.");
#else
    (void)controllers; (void)devices; (void)includeHubs;
    warnings.push_back("USB devices are unavailable: this build was compiled without the SetupAPI "
                       "headers.");
#endif
}

// ===== BLUETOOTH =====

void QueryBluetooth(std::vector<BluetoothAdapterInfo>& out, std::vector<std::string>& warnings) {
#ifdef ULTRACANVAS_HAS_BLUETOOTHAPI
    auto FormatAddress = [](const BLUETOOTH_ADDRESS& address) {
        char text[18];
        ::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
                   address.rgBytes[5], address.rgBytes[4], address.rgBytes[3],
                   address.rgBytes[2], address.rgBytes[1], address.rgBytes[0]);
        return std::string(text);
    };

    BLUETOOTH_FIND_RADIO_PARAMS findParams{};
    findParams.dwSize = sizeof(findParams);
    HANDLE radio = nullptr;
    HBLUETOOTH_RADIO_FIND find = ::BluetoothFindFirstRadio(&findParams, &radio);
    if (!find) {
        warnings.push_back("No Bluetooth radio is present.");
        return;
    }

    do {
        BLUETOOTH_RADIO_INFO radioInfo{};
        radioInfo.dwSize = sizeof(radioInfo);
        BluetoothAdapterInfo adapter;
        if (::BluetoothGetRadioInfo(radio, &radioInfo) == ERROR_SUCCESS) {
            adapter.name = WideToUtf8(radioInfo.szName);
            adapter.address = FormatAddress(radioInfo.address);
        }
        adapter.powered = ::BluetoothIsConnectable(radio) != FALSE ||
                          ::BluetoothIsDiscoverable(radio) != FALSE;
        adapter.discoverable = ::BluetoothIsDiscoverable(radio) != FALSE;

        BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams{};
        searchParams.dwSize = sizeof(searchParams);
        searchParams.fReturnAuthenticated = TRUE;
        searchParams.fReturnRemembered = TRUE;
        searchParams.fReturnConnected = TRUE;
        searchParams.fReturnUnknown = FALSE;
        searchParams.fIssueInquiry = FALSE;   // never scan: a probe must not touch the air
        searchParams.cTimeoutMultiplier = 0;
        searchParams.hRadio = radio;

        BLUETOOTH_DEVICE_INFO deviceInfo{};
        deviceInfo.dwSize = sizeof(deviceInfo);
        HBLUETOOTH_DEVICE_FIND deviceFind = ::BluetoothFindFirstDevice(&searchParams, &deviceInfo);
        if (deviceFind) {
            do {
                BluetoothDeviceInfo device;
                device.name = WideToUtf8(deviceInfo.szName);
                device.address = FormatAddress(deviceInfo.Address);
                device.connected = deviceInfo.fConnected != FALSE;
                device.paired = deviceInfo.fAuthenticated != FALSE;
                device.trusted = deviceInfo.fRemembered != FALSE;
                adapter.devices.push_back(std::move(device));
                deviceInfo = BLUETOOTH_DEVICE_INFO{};
                deviceInfo.dwSize = sizeof(deviceInfo);
            } while (::BluetoothFindNextDevice(deviceFind, &deviceInfo));
            ::BluetoothFindDeviceClose(deviceFind);
        }
        out.push_back(std::move(adapter));
        ::CloseHandle(radio);
    } while (::BluetoothFindNextRadio(find, &radio));
    ::BluetoothFindRadioClose(find);
#else
    (void)out;
    warnings.push_back("Bluetooth is unavailable: this build was compiled without the Bluetooth "
                       "API headers.");
#endif
}

// ===== SENSOR REFRESH =====

void RefreshSensors(HardwareSnapshot& snapshot) {
    if (snapshot.Has(HardwareQuery::CPU)) {
        double maxMHz = 0.0, currentMHz = 0.0;
        if (ReadProcessorClocks(maxMHz, currentMHz, snapshot.cpu.logicalCores))
            snapshot.cpu.currentClockMHz = currentMHz;
    }

    if (snapshot.Has(HardwareQuery::Memory)) {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (::GlobalMemoryStatusEx(&status)) {
            snapshot.memory.availableBytes = status.ullAvailPhys;
            snapshot.memory.usedBytes = status.ullTotalPhys - status.ullAvailPhys;
        }
    }

    for (auto& device : snapshot.storage) {
        const size_t digits = device.devicePath.find_last_not_of("0123456789");
        if (digits == std::string::npos || digits + 1 >= device.devicePath.size()) continue;
        const std::wstring path = L"\\\\.\\PhysicalDrive" +
                                  std::wstring(device.devicePath.begin() + static_cast<long>(digits) + 1,
                                               device.devicePath.end());
        HANDLE drive = ::CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                     OPEN_EXISTING, 0, nullptr);
        if (drive == INVALID_HANDLE_VALUE) continue;
        double celsius = 0.0;
        if (ReadDriveTemperature(drive, celsius)) device.temperatureC = celsius;
        ::CloseHandle(drive);
    }

#ifdef ULTRACANVAS_HAS_WLANAPI
    if (!snapshot.network.empty()) {
        std::vector<std::string> ignored;
        const auto connections = ReadWiFiConnections(ignored);
        for (auto& adapter : snapshot.network) {
            if (!adapter.wifi) continue;
            for (const auto& connection : connections) {
                if (connection.second.bssid.empty()) continue;
                if (connection.second.ssid == adapter.wifi->ssid || adapter.wifi->ssid.empty()) {
                    adapter.wifi->signalPercent = connection.second.signalPercent;
                    adapter.wifi->txRateMbps = connection.second.txRateMbps;
                    adapter.wifi->rxRateMbps = connection.second.rxRateMbps;
                    break;
                }
            }
        }
    }
#endif
}

} // namespace HardwareInfoBackend
} // namespace UltraCanvas
