// VirtualFS/core/VirtualFSRamDisk.cpp
// Platform-independent half of RAM disc provisioning.
//
// Name validation, the secure wipe used by the Windows fallback, and the
// VirtualFS integration point live here. Everything that actually talks to
// a mount lives in OS/<Platform>/VirtualFSRamDiskPlatform.cpp.
// Version: 1.0.0
// Last Modified: 2026-08-31
// Author: ULTRA OS Framework

#include "VirtualFS/VirtualFSRamDisk.h"
#include "VirtualFS/VirtualFSRamDiskPlatform.h"
#include "VirtualFS/VirtualFS.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace VirtualFS {

// ============================================================================
// SHARED HELPERS (visible to the platform back ends)
// ============================================================================

bool RamDiskDetail::IsValidName(const std::string& name) {
    // The name becomes part of a real path, so keep it to characters that
    // cannot escape a directory or confuse a shell on any platform.
    if (name.empty() || name.size() > 64) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    });
}

bool RamDiskDetail::SecureWipeDirectory(const std::string& path) {
    // Only meaningful for the Windows disk fallback: RAM-backed discs release
    // their pages on unmount, but the fallback wrote to real storage.
    //
    // This is a best-effort overwrite. On a journalling or copy-on-write
    // filesystem, or on an SSD doing wear levelling, overwriting a file in
    // place does not reliably destroy the old blocks. It raises the cost of
    // casual recovery; it is not a guarantee, which is exactly why the
    // fallback reports IsTrueRam() == false.
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return true;
    }

    bool allWiped = true;
    std::vector<char> zeros(64 * 1024, 0);

    for (auto it = std::filesystem::recursive_directory_iterator(path, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const uintmax_t size = std::filesystem::file_size(it->path(), ec);
        if (ec) {
            allWiped = false;
            ec.clear();
            continue;
        }

        std::ofstream out(it->path(), std::ios::binary | std::ios::in);
        if (!out) {
            allWiped = false;
            continue;
        }
        for (uintmax_t written = 0; written < size; ) {
            const std::streamsize chunk = static_cast<std::streamsize>(
                std::min<uintmax_t>(zeros.size(), size - written));
            if (!out.write(zeros.data(), chunk)) {
                allWiped = false;
                break;
            }
            written += static_cast<uintmax_t>(chunk);
        }
        out.flush();
    }

    std::filesystem::remove_all(path, ec);
    return allWiped && !ec;
}

// ============================================================================
// PUBLIC API
// ============================================================================

const char* VirtualFSRamDiskBackingToString(VirtualFSRamDiskBacking backing) {
    switch (backing) {
        case VirtualFSRamDiskBacking::None:         return "None";
        case VirtualFSRamDiskBacking::Tmpfs:        return "tmpfs (/dev/shm)";
        case VirtualFSRamDiskBacking::HdiUtil:      return "hdiutil ram://";
        case VirtualFSRamDiskBacking::ImDisk:       return "ImDisk";
        case VirtualFSRamDiskBacking::DiskFallback: return "disk fallback (not RAM)";
    }
    return "Unknown";
}

bool VirtualFS_IsTrueRamDiskAvailable() {
    const VirtualFSRamDiskBacking backing = VirtualFS_GetPreferredRamDiskBacking();
    return backing != VirtualFSRamDiskBacking::None &&
           backing != VirtualFSRamDiskBacking::DiskFallback;
}

VirtualFSResult VirtualFS_CreateRamDisk(const std::string& name,
                                        uint64_t sizeBytes,
                                        VirtualFSRamDisk& outDisk) {
    if (!RamDiskDetail::IsValidName(name)) {
        return VirtualFSResult::InvalidArgument;
    }
    if (sizeBytes == 0) {
        return VirtualFSResult::InvalidArgument;
    }

    VirtualFSRamDisk disk;
    disk.name = name;
    disk.requestedBytes = sizeBytes;

    const VirtualFSResult result = RamDiskDetail::PlatformCreate(name, sizeBytes, disk);
    if (result != VirtualFSResult::Success) {
        return result;
    }

    outDisk = disk;
    return VirtualFSResult::Success;
}

VirtualFSResult VirtualFS_DestroyRamDisk(VirtualFSRamDisk& disk) {
    if (!disk.IsValid()) {
        return VirtualFSResult::InvalidArgument;
    }

    // If the manager is still pointed at this disc, move it off before the
    // mount disappears - otherwise every later temp write fails.
    if (VirtualFS_GetTempDirectory() == disk.mountPath) {
        std::error_code ec;
        const auto fallback = std::filesystem::temp_directory_path(ec);
        VirtualFS_SetTempDirectory(ec ? std::string(".") : fallback.string());
    }

    const VirtualFSResult result = RamDiskDetail::PlatformDestroy(disk);
    if (result == VirtualFSResult::Success) {
        disk = VirtualFSRamDisk{};
    }
    return result;
}

std::vector<VirtualFSRamDisk> VirtualFS_ListRamDisks() {
    return RamDiskDetail::PlatformList();
}

VirtualFSResult VirtualFS_UseRamDiskForTemp(const VirtualFSRamDisk& disk) {
    if (!disk.IsValid()) {
        return VirtualFSResult::InvalidArgument;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(disk.mountPath, ec)) {
        return VirtualFSResult::NotFound;
    }
    VirtualFS_SetTempDirectory(disk.mountPath);
    return VirtualFSResult::Success;
}

} // namespace VirtualFS
