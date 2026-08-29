// OS/MacOS/UltraCanvasMacOSHardwareInfo.cpp
// macOS backend for UltraCanvasHardwareInfo, built on sysctl and the IOKit
// registry - both C APIs, so this file is plain C++ rather than Objective-C++.
// Apple keeps thermal sensors behind private frameworks (SMC on Intel,
// IOHIDEventSystem on Apple Silicon) and Wi-Fi association behind CoreWLAN, so
// those values are reported as warnings rather than guessed at.
// Version: 0.1.0
// Last Modified: 2026-08-29
// Author: UltraCanvas Framework

#include "UltraCanvasHardwareInfoBackend.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <mach/mach.h>
#include <mach/mach_host.h>

namespace UltraCanvas {
namespace HardwareInfoBackend {
namespace {

// ===== sysctl =====

std::string SysctlString(const char* name) {
    size_t length = 0;
    if (::sysctlbyname(name, nullptr, &length, nullptr, 0) != 0 || length == 0) return std::string();
    std::string value(length, '\0');
    if (::sysctlbyname(name, value.data(), &length, nullptr, 0) != 0) return std::string();
    while (!value.empty() && (value.back() == '\0' || value.back() == '\n')) value.pop_back();
    return value;
}

uint64_t SysctlUnsigned(const char* name, uint64_t fallback = 0) {
    uint64_t value = 0;
    size_t length = sizeof(value);
    if (::sysctlbyname(name, &value, &length, nullptr, 0) == 0) {
        if (length == sizeof(uint32_t)) return *reinterpret_cast<uint32_t*>(&value);
        return value;
    }
    return fallback;
}

bool SysctlExists(const char* name) {
    size_t length = 0;
    return ::sysctlbyname(name, nullptr, &length, nullptr, 0) == 0;
}

std::string Trim(const std::string& text) {
    size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// ===== CoreFoundation =====

std::string CFStringToStd(CFStringRef text) {
    if (!text) return std::string();
    const CFIndex length = CFStringGetLength(text);
    const CFIndex maximum = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string value(static_cast<size_t>(maximum), '\0');
    if (!CFStringGetCString(text, value.data(), maximum, kCFStringEncodingUTF8)) return std::string();
    value.resize(std::strlen(value.c_str()));
    return value;
}

// IOKit hands strings back as CFString, as NUL-terminated CFData ("model"), and
// numbers as CFNumber or little-endian CFData ("vendor-id"). One reader covers
// all four so callers never have to care which a given key uses.
std::string CFTypeToString(CFTypeRef value) {
    if (!value) return std::string();
    const CFTypeID type = CFGetTypeID(value);
    if (type == CFStringGetTypeID()) return Trim(CFStringToStd(static_cast<CFStringRef>(value)));
    if (type == CFDataGetTypeID()) {
        auto data = static_cast<CFDataRef>(value);
        const CFIndex length = CFDataGetLength(data);
        const auto* bytes = reinterpret_cast<const char*>(CFDataGetBytePtr(data));
        std::string text(bytes, static_cast<size_t>(length));
        const size_t terminator = text.find('\0');
        if (terminator != std::string::npos) text.resize(terminator);
        return Trim(text);
    }
    if (type == CFNumberGetTypeID()) {
        long long number = 0;
        CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberLongLongType, &number);
        return std::to_string(number);
    }
    if (type == CFBooleanGetTypeID())
        return CFBooleanGetValue(static_cast<CFBooleanRef>(value)) ? "true" : "false";
    return std::string();
}

uint64_t CFTypeToUnsigned(CFTypeRef value) {
    if (!value) return 0;
    const CFTypeID type = CFGetTypeID(value);
    if (type == CFNumberGetTypeID()) {
        long long number = 0;
        CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberLongLongType, &number);
        return number < 0 ? 0 : static_cast<uint64_t>(number);
    }
    if (type == CFDataGetTypeID()) {
        auto data = static_cast<CFDataRef>(value);
        const CFIndex length = std::min<CFIndex>(CFDataGetLength(data), 8);
        const uint8_t* bytes = CFDataGetBytePtr(data);
        uint64_t number = 0;
        for (CFIndex index = 0; index < length; ++index)
            number |= static_cast<uint64_t>(bytes[index]) << (8 * index);   // little-endian
        return number;
    }
    if (type == CFBooleanGetTypeID())
        return CFBooleanGetValue(static_cast<CFBooleanRef>(value)) ? 1u : 0u;
    return 0;
}

bool CFTypeToBool(CFTypeRef value) {
    if (!value) return false;
    if (CFGetTypeID(value) == CFBooleanGetTypeID())
        return CFBooleanGetValue(static_cast<CFBooleanRef>(value));
    return CFTypeToUnsigned(value) != 0;
}

// Reads one property of a registry entry. The caller owns nothing: the value is
// converted and released here.
std::string EntryString(io_registry_entry_t entry, const char* key) {
    CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
    if (!name) return std::string();
    CFTypeRef value = IORegistryEntryCreateCFProperty(entry, name, kCFAllocatorDefault, 0);
    CFRelease(name);
    const std::string result = CFTypeToString(value);
    if (value) CFRelease(value);
    return result;
}

uint64_t EntryUnsigned(io_registry_entry_t entry, const char* key) {
    CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
    if (!name) return 0;
    CFTypeRef value = IORegistryEntryCreateCFProperty(entry, name, kCFAllocatorDefault, 0);
    CFRelease(name);
    const uint64_t result = CFTypeToUnsigned(value);
    if (value) CFRelease(value);
    return result;
}

bool EntryBool(io_registry_entry_t entry, const char* key) {
    CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
    if (!name) return false;
    CFTypeRef value = IORegistryEntryCreateCFProperty(entry, name, kCFAllocatorDefault, 0);
    CFRelease(name);
    const bool result = CFTypeToBool(value);
    if (value) CFRelease(value);
    return result;
}

// One entry of a CFDictionary-valued property ("Device Characteristics").
std::string EntryDictionaryString(io_registry_entry_t entry, const char* dictionaryKey,
                                  const char* valueKey) {
    CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, dictionaryKey,
                                                 kCFStringEncodingUTF8);
    if (!name) return std::string();
    CFTypeRef property = IORegistryEntryCreateCFProperty(entry, name, kCFAllocatorDefault, 0);
    CFRelease(name);
    if (!property) return std::string();
    std::string result;
    if (CFGetTypeID(property) == CFDictionaryGetTypeID()) {
        CFStringRef inner = CFStringCreateWithCString(kCFAllocatorDefault, valueKey,
                                                      kCFStringEncodingUTF8);
        if (inner) {
            CFTypeRef value = CFDictionaryGetValue(static_cast<CFDictionaryRef>(property), inner);
            result = CFTypeToString(value);   // borrowed: not released
            CFRelease(inner);
        }
    }
    CFRelease(property);
    return result;
}

// Walks up the service plane until an ancestor carries `key`, and returns that
// ancestor. Storage characteristics live on the device, not on the media.
io_registry_entry_t AncestorWithProperty(io_registry_entry_t start, const char* key) {
    io_registry_entry_t current = start;
    IOObjectRetain(current);
    for (int depth = 0; depth < 16; ++depth) {
        CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
        if (!name) break;
        CFTypeRef value = IORegistryEntryCreateCFProperty(current, name, kCFAllocatorDefault, 0);
        CFRelease(name);
        if (value) { CFRelease(value); return current; }
        io_registry_entry_t parent = 0;
        if (IORegistryEntryGetParentEntry(current, kIOServicePlane, &parent) != KERN_SUCCESS) break;
        IOObjectRelease(current);
        current = parent;
    }
    IOObjectRelease(current);
    return 0;
}

// Iterates every service of a class, calling `visit` for each.
template <typename Visitor>
void ForEachService(const char* className, Visitor visit) {
    CFMutableDictionaryRef matching = IOServiceMatching(className);
    if (!matching) return;
    io_iterator_t iterator = 0;
    if (IOServiceGetMatchingServices(kIOMasterPortDefault, matching, &iterator) != KERN_SUCCESS)
        return;
    io_object_t service = 0;
    while ((service = IOIteratorNext(iterator)) != 0) {
        visit(service);
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
}

} // namespace

// ===== BACKEND IDENTITY =====

std::string BackendName() { return "iokit"; }
bool IsAvailable() { return true; }

// ===== SYSTEM =====

void QuerySystem(SystemInfo& out, std::vector<std::string>& warnings) {
    out.hostName = SysctlString("kern.hostname");
    out.osName = "macOS";
    out.osVersion = SysctlString("kern.osproductversion");
    if (!out.osVersion.empty()) out.osName = "macOS " + out.osVersion;
    out.kernelVersion = "Darwin " + SysctlString("kern.osrelease");
    out.manufacturer = "Apple Inc.";
    out.productName = SysctlString("hw.model");

    utsname uts{};
    if (::uname(&uts) == 0) out.architecture = uts.machine;

    // The model identifier is the only chassis hint macOS offers.
    const std::string model = out.productName;
    if (model.rfind("MacBook", 0) == 0) out.chassisType = "Notebook";
    else if (model.rfind("Macmini", 0) == 0) out.chassisType = "Mini PC";
    else if (model.rfind("iMac", 0) == 0) out.chassisType = "All in one";
    else if (model.rfind("MacPro", 0) == 0) out.chassisType = "Tower";
    else if (model.rfind("Mac", 0) == 0 && model.find("Virtual") != std::string::npos)
        out.chassisType = "Virtual machine";
    else if (model.rfind("VMware", 0) == 0 || model.rfind("Parallels", 0) == 0)
        out.chassisType = "Virtual machine";

    io_registry_entry_t platform = IOServiceGetMatchingService(
        kIOMasterPortDefault, IOServiceMatching("IOPlatformExpertDevice"));
    if (platform) {
        out.boardVendor = "Apple Inc.";
        out.boardName = EntryString(platform, "board-id");
        if (out.boardName.empty()) out.boardName = EntryString(platform, "target-type");
        IOObjectRelease(platform);
    }
    out.biosVendor = "Apple Inc.";
    // The boot ROM version is what "firmware" means on a Mac; the device tree
    // carries it where other platforms would have a BIOS version.
    io_registry_entry_t rom = IORegistryEntryFromPath(kIOMasterPortDefault, "IODeviceTree:/rom");
    if (rom) {
        out.biosVersion = EntryString(rom, "version");
        IOObjectRelease(rom);
    }

    timeval bootTime{};
    size_t bootSize = sizeof(bootTime);
    if (::sysctlbyname("kern.boottime", &bootTime, &bootSize, nullptr, 0) == 0 && bootTime.tv_sec > 0) {
        const time_t now = ::time(nullptr);
        if (now > bootTime.tv_sec) out.uptimeSeconds = static_cast<uint64_t>(now - bootTime.tv_sec);
    }
    if (out.productName.empty())
        warnings.push_back("System model is unavailable: hw.model returned nothing.");
}

// ===== CPU =====

void QueryCPU(CPUInfo& out, bool includeSensors, std::vector<std::string>& warnings) {
    out.model = SysctlString("machdep.cpu.brand_string");
    out.vendor = SysctlString("machdep.cpu.vendor");
    if (out.vendor.empty()) out.vendor = "Apple";
    utsname uts{};
    if (::uname(&uts) == 0) out.architecture = uts.machine;

    out.physicalCores = static_cast<int>(SysctlUnsigned("hw.physicalcpu", 0));
    out.logicalCores = static_cast<int>(SysctlUnsigned("hw.logicalcpu", 0));
    out.packages = static_cast<int>(SysctlUnsigned("hw.packages", 1));
    if (out.logicalCores <= 0) out.logicalCores = static_cast<int>(SysctlUnsigned("hw.ncpu", 0));

    const uint64_t family = SysctlUnsigned("machdep.cpu.family", 0);
    const uint64_t cpuModel = SysctlUnsigned("machdep.cpu.model", 0);
    const uint64_t stepping = SysctlUnsigned("machdep.cpu.stepping", 0);
    if (family > 0)
        out.stepping = "family " + std::to_string(family) + ", model " + std::to_string(cpuModel) +
                       ", stepping " + std::to_string(stepping);

    // hw.cpufrequency exists on Intel only; Apple Silicon publishes per-tier
    // frequencies through the performance-level nodes instead.
    const uint64_t hertz = SysctlUnsigned("hw.cpufrequency", 0);
    if (hertz > 0) out.baseClockMHz = static_cast<double>(hertz) / 1000000.0;
    const uint64_t maxHertz = SysctlUnsigned("hw.cpufrequency_max", 0);
    if (maxHertz > 0) out.maxClockMHz = static_cast<double>(maxHertz) / 1000000.0;

    auto AddCache = [&out](int level, CPUCacheType type, const char* sysctlName, int instances) {
        const uint64_t size = SysctlUnsigned(sysctlName, 0);
        if (size == 0) return;
        CPUCacheInfo cache;
        cache.level = level;
        cache.type = type;
        cache.sizeBytes = size;
        cache.instanceCount = instances > 0 ? instances : 1;
        cache.lineSizeBytes = static_cast<int>(SysctlUnsigned("hw.cachelinesize", 0));
        out.caches.push_back(cache);
    };
    const int coreCount = out.physicalCores > 0 ? out.physicalCores : 1;
    AddCache(1, CPUCacheType::Instruction, "hw.l1icachesize", coreCount);
    AddCache(1, CPUCacheType::Data, "hw.l1dcachesize", coreCount);
    AddCache(2, CPUCacheType::Unified, "hw.l2cachesize", 1);
    AddCache(3, CPUCacheType::Unified, "hw.l3cachesize", 1);

    // Apple Silicon describes its P and E clusters as performance levels.
    const uint64_t levels = SysctlUnsigned("hw.nperflevels", 0);
    for (uint64_t level = 0; level < levels && level < 4; ++level) {
        const std::string prefix = "hw.perflevel" + std::to_string(level) + ".";
        CPUCoreGroup group;
        group.name = SysctlString((prefix + "name").c_str());
        if (group.name.empty()) group.name = level == 0 ? "Performance" : "Efficiency";
        group.physicalCores = static_cast<int>(SysctlUnsigned((prefix + "physicalcpu").c_str(), 0));
        group.logicalCores = static_cast<int>(SysctlUnsigned((prefix + "logicalcpu").c_str(), 0));
        if (group.physicalCores > 0) out.coreGroups.push_back(std::move(group));
    }

    // Intel Macs go through the shared CPUID detector rather than the
    // machdep.cpu.features string: the string omits the VEX-encoded extensions
    // (GFNI, VAES, VPCLMULQDQ) that a -march=native build faults on, and CPUID
    // yields the psABI level with them.
    X86CpuFeatures x86Features;
    if (ReadX86CpuFeatures(x86Features)) {
        AppendX86FeatureNames(x86Features, out.instructionSets);
        out.x86MicroarchitectureLevel = X86MicroarchitectureLevel(x86Features);
    }

    // Apple Silicon answers a boolean sysctl per architectural feature.
    static const std::pair<const char*, const char*> kArmFeatures[] = {
        { "hw.optional.AdvSIMD", "NEON" },
        { "hw.optional.arm.FEAT_AES", "AES" },
        { "hw.optional.arm.FEAT_SHA256", "SHA2" },
        { "hw.optional.arm.FEAT_SHA3", "SHA3" },
        { "hw.optional.arm.FEAT_FP16", "FP16" },
        { "hw.optional.arm.FEAT_DotProd", "DotProd" },
        { "hw.optional.arm.FEAT_I8MM", "I8MM" },
        { "hw.optional.arm.FEAT_BF16", "BF16" },
        { "hw.optional.arm.FEAT_SME", "SME" }
    };
    for (const auto& feature : kArmFeatures)
        if (SysctlUnsigned(feature.first, 0) != 0) out.instructionSets.push_back(feature.second);

    // Rosetta 2's own flag. Same class of answer as IsWow64Process2 on Windows:
    // the model and core counts below describe the silicon, while the feature
    // list is what the translator permits (no AVX at all under Rosetta).
    if (SysctlUnsigned("sysctl.proc_translated", 0) != 0) {
        out.emulation = "x86_64 image translated by Rosetta on Apple Silicon";
        warnings.push_back("This process is not running natively (" + out.emulation + "): the model "
                           "and core counts describe the machine, but the instruction sets are the "
                           "ones the translator permits, which are a subset (Rosetta implements no "
                           "AVX).");
    }

    if (includeSensors)
        warnings.push_back("CPU temperature is unavailable on macOS: Apple exposes it only through "
                           "private interfaces (the SMC on Intel, IOHIDEventSystem on Apple Silicon).");

    double loadAverage[3] = {0, 0, 0};
    if (::getloadavg(loadAverage, 3) > 0 && out.logicalCores > 0)
        out.loadPercent = 100.0 * loadAverage[0] / static_cast<double>(out.logicalCores);
}

// ===== MEMORY =====

void QueryMemory(MemoryInfo& out, std::vector<std::string>& warnings) {
    out.totalBytes = SysctlUnsigned("hw.memsize", 0);
    out.pageSizeBytes = SysctlUnsigned("hw.pagesize", 0);

    vm_statistics64_data_t stats{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&stats), &count) == KERN_SUCCESS &&
        out.pageSizeBytes > 0) {
        // "Available" on macOS is free plus what the compressor and the file
        // cache would give back under pressure - the number Activity Monitor
        // works from, rather than the much smaller free-page count.
        const uint64_t reclaimable = static_cast<uint64_t>(stats.free_count) +
                                     static_cast<uint64_t>(stats.inactive_count) +
                                     static_cast<uint64_t>(stats.purgeable_count);
        out.availableBytes = reclaimable * out.pageSizeBytes;
        if (out.totalBytes >= out.availableBytes) out.usedBytes = out.totalBytes - out.availableBytes;
    }

    xsw_usage swap{};
    size_t swapSize = sizeof(swap);
    if (::sysctlbyname("vm.swapusage", &swap, &swapSize, nullptr, 0) == 0) {
        out.swapTotalBytes = swap.xsu_total;
        out.swapFreeBytes = swap.xsu_avail;
    }

    warnings.push_back("Per-module memory detail is unavailable on macOS: the memory is soldered on "
                       "Apple Silicon, and the SMBIOS tables Intel Macs carry are not exported to "
                       "unprivileged processes.");
}

// ===== GRAPHICS AND ACCELERATORS =====

void QueryGPUs(std::vector<GPUInfo>& out, bool includeSensors, std::vector<std::string>& warnings) {
    // Intel Macs and eGPUs present the adapter as a PCI display device; Apple
    // Silicon has no PCI GPU and is read from the accelerator service instead.
    ForEachService("IOPCIDevice", [&out](io_object_t service) {
        const uint64_t classCode = EntryUnsigned(service, "class-code");
        if ((classCode >> 16) != 0x03) return;   // not a display controller
        GPUInfo gpu;
        gpu.model = EntryString(service, "model");
        gpu.pciVendorId = static_cast<uint16_t>(EntryUnsigned(service, "vendor-id"));
        gpu.pciDeviceId = static_cast<uint16_t>(EntryUnsigned(service, "device-id"));
        switch (gpu.pciVendorId) {
            case 0x10DE: gpu.vendor = "NVIDIA"; gpu.kind = GPUKind::Discrete; break;
            case 0x1002: gpu.vendor = "AMD"; gpu.kind = GPUKind::Discrete; break;
            case 0x8086: gpu.vendor = "Intel"; gpu.kind = GPUKind::Integrated; break;
            case 0x106B: gpu.vendor = "Apple"; gpu.kind = GPUKind::Integrated; break;
            default: break;
        }
        uint64_t videoMemory = EntryUnsigned(service, "VRAM,totalsize");
        if (videoMemory == 0)
            videoMemory = EntryUnsigned(service, "VRAM,totalMB") * 1024ull * 1024ull;
        gpu.videoMemoryBytes = videoMemory;
        out.push_back(std::move(gpu));
    });

    ForEachService("IOAccelerator", [&out](io_object_t service) {
        const std::string model = EntryString(service, "model");
        const uint64_t cores = EntryUnsigned(service, "gpu-core-count");
        if (model.empty() && cores == 0) return;
        // Skip an accelerator that only wraps a PCI GPU already listed above.
        for (const auto& existing : out)
            if (!model.empty() && existing.model == model) return;
        GPUInfo gpu;
        gpu.model = model.empty() ? "Apple GPU" : model;
        gpu.vendor = "Apple";
        gpu.kind = GPUKind::Integrated;
        gpu.computeUnits = static_cast<int>(cores);
        // Apple Silicon shares one memory pool with the CPU.
        gpu.sharedMemoryBytes = SysctlUnsigned("hw.memsize", 0);
        out.push_back(std::move(gpu));
    });

    if (includeSensors)
        warnings.push_back("GPU temperature and utilisation are unavailable on macOS: Apple exposes "
                           "them only through private interfaces.");
    if (out.empty())
        warnings.push_back("No graphics adapter was found in the IOKit registry.");
}

void QueryNPUs(std::vector<NPUInfo>& out, bool includeSensors, std::vector<std::string>& warnings) {
    (void)includeSensors; (void)warnings;
    // The Neural Engine's driver class has changed name across generations, so
    // try each; the part is only reported when IOKit actually shows it.
    static const char* kNeuralEngineClasses[] = {
        "AppleNeuralEngine", "AppleH11ANEInterface", "AppleANEInterface"
    };
    for (const char* className : kNeuralEngineClasses) {
        bool found = false;
        ForEachService(className, [&out, &found](io_object_t service) {
            NPUInfo npu;
            npu.vendor = "Apple";
            npu.model = "Apple Neural Engine";
            npu.runtime = "Core ML";
            npu.integrated = true;
            const uint64_t cores = EntryUnsigned(service, "ane-core-count");
            if (cores > 0) npu.precisions.push_back(std::to_string(cores) + "-core");
            out.push_back(std::move(npu));
            found = true;
        });
        if (found) break;
    }
}

// ===== STORAGE =====

void QueryStorage(std::vector<StorageDeviceInfo>& out, bool includeSensors,
                  std::vector<std::string>& warnings) {
    // Volumes first, so each drive can list the ones carved out of it.
    std::map<std::string, std::vector<StorageVolumeInfo>> volumesByDevice;
    struct statfs* mounts = nullptr;
    const int mountCount = ::getmntinfo(&mounts, MNT_NOWAIT);
    for (int index = 0; index < mountCount; ++index) {
        const std::string source = mounts[index].f_mntfromname;
        if (source.rfind("/dev/", 0) != 0) continue;
        StorageVolumeInfo volume;
        volume.mountPoint = mounts[index].f_mntonname;
        volume.fileSystem = mounts[index].f_fstypename;
        volume.readOnly = (mounts[index].f_flags & MNT_RDONLY) != 0;
        volume.totalBytes = static_cast<uint64_t>(mounts[index].f_blocks) * mounts[index].f_bsize;
        volume.freeBytes = static_cast<uint64_t>(mounts[index].f_bavail) * mounts[index].f_bsize;

        // "/dev/disk3s1s1" belongs to whole disk "disk3".
        std::string whole = source.substr(5);
        const size_t sliceStart = whole.find('s', 4);
        if (sliceStart != std::string::npos) whole = whole.substr(0, sliceStart);
        volumesByDevice[whole].push_back(std::move(volume));
    }

    ForEachService("IOMedia", [&out, &volumesByDevice](io_object_t service) {
        if (!EntryBool(service, "Whole")) return;   // a slice, not the drive
        StorageDeviceInfo device;
        const std::string bsdName = EntryString(service, "BSD Name");
        if (bsdName.empty()) return;
        device.devicePath = "/dev/" + bsdName;
        device.capacityBytes = EntryUnsigned(service, "Size");
        device.removable = EntryBool(service, "Removable") || EntryBool(service, "Ejectable");

        io_registry_entry_t characteristics = AncestorWithProperty(service, "Device Characteristics");
        if (characteristics) {
            device.model = EntryDictionaryString(characteristics, "Device Characteristics", "Product Name");
            device.vendor = EntryDictionaryString(characteristics, "Device Characteristics", "Vendor Name");
            device.firmwareVersion = EntryDictionaryString(characteristics, "Device Characteristics",
                                                           "Product Revision Level");
            device.serialNumber = EntryDictionaryString(characteristics, "Device Characteristics",
                                                        "Serial Number");
            const std::string medium = EntryDictionaryString(characteristics, "Device Characteristics",
                                                             "Medium Type");
            if (medium == "Solid State") device.media = StorageMedia::SSD;
            else if (medium == "Rotational") device.media = StorageMedia::HDD;

            const std::string interconnect =
                EntryDictionaryString(characteristics, "Protocol Characteristics", "Physical Interconnect");
            const std::string location =
                EntryDictionaryString(characteristics, "Protocol Characteristics",
                                      "Physical Interconnect Location");
            if (interconnect == "PCI-Express" || interconnect == "Apple Fabric") {
                device.bus = StorageBus::NVMe;
                device.connector = interconnect == "Apple Fabric" ? "Apple Fabric (NVMe)"
                                                                  : "PCIe (NVMe)";
            } else if (interconnect == "SATA") {
                device.bus = StorageBus::SATA;
                device.connector = "SATA";
            } else if (interconnect == "USB") {
                device.bus = StorageBus::USB;
                device.connector = "USB";
            } else if (interconnect == "Secure Digital") {
                device.bus = StorageBus::SD;
                device.connector = "SD card";
            } else if (!interconnect.empty()) {
                device.connector = interconnect;
            }
            if (!location.empty() && !device.connector.empty())
                device.connector += " (" + location + ")";
            IOObjectRelease(characteristics);
        }
        if (device.model.empty()) device.model = bsdName;

        auto volumes = volumesByDevice.find(bsdName);
        if (volumes != volumesByDevice.end()) device.volumes = volumes->second;
        out.push_back(std::move(device));
    });

    if (includeSensors && !out.empty())
        warnings.push_back("Drive temperature and health are unavailable on macOS: SMART data is "
                           "reachable only through a privileged driver interface.");
}

// ===== NETWORK =====
namespace {

// macOS reports Wi-Fi interfaces with the same link type as Ethernet; the
// 802.11 driver's BSD name is what tells them apart.
std::vector<std::string> WirelessInterfaceNames() {
    std::vector<std::string> names;
    ForEachService("IO80211Interface", [&names](io_object_t service) {
        const std::string bsdName = EntryString(service, "BSD Name");
        if (!bsdName.empty()) names.push_back(bsdName);
    });
    if (names.empty()) {
        ForEachService("IO80211InterfaceMonitor", [&names](io_object_t service) {
            const std::string bsdName = EntryString(service, "BSD Name");
            if (!bsdName.empty()) names.push_back(bsdName);
        });
    }
    return names;
}

} // namespace

void QueryNetwork(std::vector<NetworkInterfaceInfo>& out, std::vector<std::string>& warnings) {
    const std::vector<std::string> wirelessNames = WirelessInterfaceNames();

    ifaddrs* interfaceList = nullptr;
    if (::getifaddrs(&interfaceList) != 0) {
        warnings.push_back("Network interfaces are unavailable: getifaddrs failed.");
        return;
    }

    std::map<std::string, size_t> indexByName;
    for (ifaddrs* entry = interfaceList; entry; entry = entry->ifa_next) {
        if (!entry->ifa_name) continue;
        const std::string name = entry->ifa_name;
        if (indexByName.find(name) == indexByName.end()) {
            NetworkInterfaceInfo adapter;
            adapter.name = name;
            adapter.up = (entry->ifa_flags & IFF_UP) != 0;
            adapter.connected = (entry->ifa_flags & IFF_RUNNING) != 0;
            if (std::find(wirelessNames.begin(), wirelessNames.end(), name) != wirelessNames.end())
                adapter.type = NetworkLinkType::WiFi;
            else if ((entry->ifa_flags & IFF_LOOPBACK) != 0)
                adapter.type = NetworkLinkType::Loopback;
            indexByName[name] = out.size();
            out.push_back(std::move(adapter));
        }
        NetworkInterfaceInfo& adapter = out[indexByName[name]];
        if (!entry->ifa_addr) continue;

        if (entry->ifa_addr->sa_family == AF_LINK) {
            const auto* link = reinterpret_cast<const sockaddr_dl*>(entry->ifa_addr);
            if (link->sdl_alen == 6) {
                const auto* mac = reinterpret_cast<const unsigned char*>(LLADDR(link));
                char text[18];
                std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
                              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                adapter.macAddress = text;
            }
            if (adapter.type == NetworkLinkType::Unknown) {
                switch (link->sdl_type) {
                    case IFT_ETHER: adapter.type = NetworkLinkType::Ethernet; break;
                    case IFT_LOOP:  adapter.type = NetworkLinkType::Loopback; break;
                    case IFT_BRIDGE: adapter.type = NetworkLinkType::Bridge; break;
                    case IFT_GIF:
                    case IFT_STF:   adapter.type = NetworkLinkType::Tunnel; break;
                    default: break;
                }
            }
            if (entry->ifa_data) {
                const auto* data = static_cast<const if_data*>(entry->ifa_data);
                adapter.mtu = static_cast<int>(data->ifi_mtu);
                adapter.linkSpeedMbps = static_cast<double>(data->ifi_baudrate) / 1000000.0;
                adapter.bytesReceived = data->ifi_ibytes;
                adapter.bytesSent = data->ifi_obytes;
            }
        } else if (entry->ifa_addr->sa_family == AF_INET) {
            char text[INET_ADDRSTRLEN] = {0};
            const auto* in4 = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
            if (::inet_ntop(AF_INET, &in4->sin_addr, text, sizeof(text)))
                adapter.ipv4Addresses.push_back(text);
        } else if (entry->ifa_addr->sa_family == AF_INET6) {
            char text[INET6_ADDRSTRLEN] = {0};
            const auto* in6 = reinterpret_cast<const sockaddr_in6*>(entry->ifa_addr);
            if (::inet_ntop(AF_INET6, &in6->sin6_addr, text, sizeof(text)))
                adapter.ipv6Addresses.push_back(text);
        }
    }
    ::freeifaddrs(interfaceList);

    for (auto& adapter : out)
        if (adapter.type == NetworkLinkType::WiFi && !adapter.wifi) adapter.wifi = WiFiInfo{};

    if (!wirelessNames.empty())
        warnings.push_back("Wi-Fi network details (SSID, access point, rate) need the CoreWLAN "
                           "framework, which this backend does not link.");
}

// ===== USB =====

void QueryUSB(std::vector<USBControllerInfo>& controllers, std::vector<USBDeviceInfo>& devices,
              bool includeHubs, std::vector<std::string>& warnings) {
    (void)warnings;
    auto SpeedName = [](uint64_t code) -> std::string {
        switch (code) {
            case 0: return "1.5 Mb/s (Low-Speed)";
            case 1: return "12 Mb/s (Full-Speed)";
            case 2: return "480 Mb/s (High-Speed)";
            case 3: return "5 Gb/s (SuperSpeed)";
            case 4: return "10 Gb/s (SuperSpeed+)";
            case 5: return "20 Gb/s (SuperSpeed+ 20 Gb/s)";
            default: return std::string();
        }
    };

    auto ReadDevices = [&](const char* className) {
        ForEachService(className, [&](io_object_t service) {
            USBDeviceInfo device;
            device.vendorId = static_cast<uint16_t>(EntryUnsigned(service, "idVendor"));
            device.productId = static_cast<uint16_t>(EntryUnsigned(service, "idProduct"));
            device.vendorName = EntryString(service, "USB Vendor Name");
            device.productName = EntryString(service, "USB Product Name");
            device.serialNumber = EntryString(service, "USB Serial Number");
            device.speed = SpeedName(EntryUnsigned(service, "Device Speed"));
            const uint64_t deviceClass = EntryUnsigned(service, "bDeviceClass");
            device.isHub = deviceClass == 0x09;
            if (device.isHub) device.deviceClass = "Hub";
            const uint64_t location = EntryUnsigned(service, "locationID");
            if (location != 0) {
                char text[16];
                std::snprintf(text, sizeof(text), "0x%08llx",
                              static_cast<unsigned long long>(location));
                device.portPath = text;
                device.busNumber = static_cast<int>((location >> 24) & 0xFF);
            }
            if (device.isHub && !includeHubs) return;
            devices.push_back(std::move(device));
        });
    };
    ReadDevices("IOUSBHostDevice");
    if (devices.empty()) ReadDevices("IOUSBDevice");   // pre-Catalina naming

    ForEachService("IOUSBHostController", [&controllers](io_object_t service) {
        USBControllerInfo controller;
        controller.name = EntryString(service, "IOClass");
        if (controller.name.empty()) controller.name = "USB host controller";
        controller.portCount = static_cast<int>(EntryUnsigned(service, "PortCount"));
        controllers.push_back(std::move(controller));
    });
}

// ===== BLUETOOTH =====

void QueryBluetooth(std::vector<BluetoothAdapterInfo>& out, std::vector<std::string>& warnings) {
    ForEachService("IOBluetoothHCIController", [&out](io_object_t service) {
        BluetoothAdapterInfo adapter;
        adapter.name = EntryString(service, "HCIControllerName");
        if (adapter.name.empty()) adapter.name = "Bluetooth controller";
        adapter.manufacturer = EntryString(service, "ControllerVendor");
        adapter.powered = EntryUnsigned(service, "HCIControllerPowerState") != 0;

        CFStringRef key = CFSTR("ControllerAddress");
        CFTypeRef value = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
        if (value) {
            if (CFGetTypeID(value) == CFDataGetTypeID()) {
                auto data = static_cast<CFDataRef>(value);
                if (CFDataGetLength(data) >= 6) {
                    const uint8_t* bytes = CFDataGetBytePtr(data);
                    char text[18];
                    std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
                                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
                    adapter.address = text;
                }
            }
            CFRelease(value);
        }
        out.push_back(std::move(adapter));
    });

    if (out.empty())
        warnings.push_back("No Bluetooth controller is present.");
    else
        warnings.push_back("Paired and connected Bluetooth devices need the IOBluetooth framework, "
                           "which this backend does not link.");
}

// ===== SENSOR REFRESH =====

void RefreshSensors(HardwareSnapshot& snapshot) {
    if (snapshot.Has(HardwareQuery::Memory) && snapshot.memory.pageSizeBytes > 0) {
        vm_statistics64_data_t stats{};
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                              reinterpret_cast<host_info64_t>(&stats), &count) == KERN_SUCCESS) {
            const uint64_t reclaimable = static_cast<uint64_t>(stats.free_count) +
                                         static_cast<uint64_t>(stats.inactive_count) +
                                         static_cast<uint64_t>(stats.purgeable_count);
            snapshot.memory.availableBytes = reclaimable * snapshot.memory.pageSizeBytes;
            if (snapshot.memory.totalBytes >= snapshot.memory.availableBytes)
                snapshot.memory.usedBytes =
                    snapshot.memory.totalBytes - snapshot.memory.availableBytes;
        }
    }

    if (snapshot.Has(HardwareQuery::CPU) && snapshot.cpu.logicalCores > 0) {
        double loadAverage[3] = {0, 0, 0};
        if (::getloadavg(loadAverage, 3) > 0)
            snapshot.cpu.loadPercent =
                100.0 * loadAverage[0] / static_cast<double>(snapshot.cpu.logicalCores);
    }

    if (snapshot.network.empty()) return;
    ifaddrs* interfaceList = nullptr;
    if (::getifaddrs(&interfaceList) != 0) return;
    for (ifaddrs* entry = interfaceList; entry; entry = entry->ifa_next) {
        if (!entry->ifa_name || !entry->ifa_addr || entry->ifa_addr->sa_family != AF_LINK) continue;
        for (auto& adapter : snapshot.network) {
            if (adapter.name != entry->ifa_name) continue;
            adapter.up = (entry->ifa_flags & IFF_UP) != 0;
            adapter.connected = (entry->ifa_flags & IFF_RUNNING) != 0;
            if (entry->ifa_data) {
                const auto* data = static_cast<const if_data*>(entry->ifa_data);
                adapter.bytesReceived = data->ifi_ibytes;
                adapter.bytesSent = data->ifi_obytes;
            }
        }
    }
    ::freeifaddrs(interfaceList);
}

} // namespace HardwareInfoBackend
} // namespace UltraCanvas
