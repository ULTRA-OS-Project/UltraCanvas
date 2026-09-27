// Apps/UltraFiler/UltraFilerRamDisks.cpp
// The RAM discs behind "+ Drive > RAM disc...". See UltraFilerRamDisks.h.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraFilerRamDisks.h"

#include "UltraCanvasHardwareInfo.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#ifdef ULTRAFILER_HAS_RAMDISK
#include "VirtualFS/VirtualFSRamDisk.h"
#endif

#include <algorithm>
#include <filesystem>
#include <set>

namespace UltraCanvas {
namespace UltraFilerRamDisks {

namespace fs = std::filesystem;

namespace {

// Paths compared as the filesystem spells them, without a trailing
// separator: "R:\" from the drive list and "R:" typed elsewhere are one disc.
std::string NormalizedRoot(const std::string& path) {
    std::string p = PathToUtf8(PathFromUtf8(path).lexically_normal());
    while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    return p;
}

#ifdef ULTRAFILER_HAS_RAMDISK

using VirtualFS::VirtualFSRamDisk;
using VirtualFS::VirtualFSRamDiskBacking;
using VirtualFS::VirtualFSResult;

RamDisc FromVirtualFS(const VirtualFSRamDisk& disk) {
    RamDisc disc;
    disc.name = disk.name;
    disc.mountPath = disk.mountPath;
    disc.trueRam = disk.IsTrueRam();
    // A tmpfs directory reports the space free on the whole /dev/shm, and the
    // Windows fallback reports the size it was asked for without holding to
    // it: neither is the size of this disc, so neither is shown as one.
    const bool sized = disk.backing == VirtualFSRamDiskBacking::HdiUtil ||
                       disk.backing == VirtualFSRamDiskBacking::ImDisk;
    disc.sizeBytes = sized ? disk.capacityBytes : 0;
    return disc;
}

std::string CreateErrorText(VirtualFSResult result, const std::string& name,
                            uint64_t sizeBytes) {
    switch (result) {
        case VirtualFSResult::InvalidArgument:
            return "\"" + name + "\" cannot name a RAM disc. Use letters, digits, "
                   "'.', '_' and '-' only.";
        case VirtualFSResult::AlreadyExists:
            return "A RAM disc named \"" + name + "\" already exists.";
        case VirtualFSResult::DiskFull:
            return "There is not enough free memory for a RAM disc of " +
                   FormatSize(sizeBytes) + ".";
        case VirtualFSResult::AccessDenied:
            return "The system refused to create the RAM disc (access denied).";
        case VirtualFSResult::NotSupported:
            return "This system has no RAM disc facility UltraFiler can use.";
        default:
            return "The RAM disc could not be created (" +
                   VirtualFS::VirtualFSResultToString(result) + ").";
    }
}

#endif  // ULTRAFILER_HAS_RAMDISK

}  // namespace

bool Available() {
#ifdef ULTRAFILER_HAS_RAMDISK
    return VirtualFS::VirtualFS_GetPreferredRamDiskBacking() !=
           VirtualFSRamDiskBacking::None;
#else
    return false;
#endif
}

bool TrueRamAvailable() {
#ifdef ULTRAFILER_HAS_RAMDISK
    return VirtualFS::VirtualFS_IsTrueRamDiskAvailable();
#else
    return false;
#endif
}

bool SizeIsEnforced() {
#ifdef ULTRAFILER_HAS_RAMDISK
    const VirtualFSRamDiskBacking backing = VirtualFS::VirtualFS_GetPreferredRamDiskBacking();
    return backing == VirtualFSRamDiskBacking::HdiUtil ||
           backing == VirtualFSRamDiskBacking::ImDisk;
#else
    return false;
#endif
}

uint64_t LargestPossibleBytes() {
    uint64_t largest = UltraCanvasHardwareInfo::GetMemory().availableBytes;
#ifdef __linux__
    // Every disc is a directory on /dev/shm, so that mount's free space is
    // the real ceiling - it is usually half the memory, not all of it.
    std::error_code ec;
    const fs::space_info shm = fs::space("/dev/shm", ec);
    if (!ec && shm.available > 0 && (largest == 0 || shm.available < largest))
        largest = shm.available;
#endif
    return largest;
}

std::vector<RamDisc> List() {
    std::vector<RamDisc> discs;
#ifdef ULTRAFILER_HAS_RAMDISK
    for (const VirtualFSRamDisk& disk : VirtualFS::VirtualFS_ListRamDisks())
        discs.push_back(FromVirtualFS(disk));
    std::sort(discs.begin(), discs.end(),
              [](const RamDisc& a, const RamDisc& b) { return a.name < b.name; });
#endif
    return discs;
}

std::string SuggestName() {
    std::set<std::string> taken;
    for (const RamDisc& disc : List()) taken.insert(disc.name);
    for (int i = 1;; ++i) {
        std::string name = "RAM" + std::to_string(i);
        if (!taken.count(name)) return name;
    }
}

bool IsValidName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    });
}

bool Create(const std::string& name, uint64_t sizeBytes, RamDisc& outDisc,
            std::string& outError) {
#ifdef ULTRAFILER_HAS_RAMDISK
    VirtualFSRamDisk disk;
    const VirtualFSResult result = VirtualFS::VirtualFS_CreateRamDisk(name, sizeBytes, disk);
    if (result != VirtualFSResult::Success) {
        outError = CreateErrorText(result, name, sizeBytes);
        return false;
    }
    outDisc = FromVirtualFS(disk);
    return true;
#else
    (void)name;
    (void)sizeBytes;
    (void)outDisc;
    outError = "This build of UltraFiler was made without VirtualFS, so it "
               "cannot create RAM discs.";
    return false;
#endif
}

bool Eject(const std::string& mountPath, std::string& outError) {
#ifdef ULTRAFILER_HAS_RAMDISK
    // The handle comes from the list rather than from Create(): a disc made
    // by an earlier run of UltraFiler is ejected the same way as a new one.
    const std::string root = NormalizedRoot(mountPath);
    for (VirtualFSRamDisk& disk : VirtualFS::VirtualFS_ListRamDisks()) {
        if (NormalizedRoot(disk.mountPath) != root) continue;
        const VirtualFSResult result = VirtualFS::VirtualFS_DestroyRamDisk(disk);
        if (result == VirtualFSResult::Success) return true;
        outError = "The RAM disc \"" + disk.name + "\" could not be ejected (" +
                   VirtualFS::VirtualFSResultToString(result) + "). A program "
                   "may still have a file open on it.";
        return false;
    }
    outError = "\"" + mountPath + "\" is not a RAM disc (any more).";
    return false;
#else
    (void)mountPath;
    outError = "This build of UltraFiler was made without VirtualFS, so it "
               "cannot eject RAM discs.";
    return false;
#endif
}

std::string FormatSize(uint64_t bytes) {
    constexpr uint64_t kMiB = 1024ull * 1024;
    constexpr uint64_t kGiB = kMiB * 1024;
    // Whole numbers and tenths by integer arithmetic: this is shown, never
    // parsed, and needs no locale or floating point to get "1.5 GB".
    if (bytes >= kGiB) {
        const uint64_t tenths = (bytes * 10 + kGiB / 2) / kGiB;
        std::string text = std::to_string(tenths / 10);
        if (tenths % 10) text += "." + std::to_string(tenths % 10);
        return text + " GB";
    }
    return std::to_string((bytes + kMiB / 2) / kMiB) + " MB";
}

} // namespace UltraFilerRamDisks
} // namespace UltraCanvas
