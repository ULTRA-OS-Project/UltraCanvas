// VirtualFS/OS/Linux/VirtualFSRamDiskPlatform.cpp
// Linux RAM disc back end - a private directory on the /dev/shm tmpfs.
//
// Linux already gives every user a RAM-backed filesystem at /dev/shm, so a
// disc here is a 0700 directory on it. That needs no privileges and no
// driver, which is why it is preferred over mounting a private tmpfs
// (`mount -t tmpfs` requires CAP_SYS_ADMIN).
//
// The trade-off is sizing: /dev/shm is one shared mount with one global
// limit, so a per-disc quota is not available without root. capacityBytes
// therefore reports the space free on the tmpfs, and the requested size is
// only checked for feasibility up front.
// Version: 1.0.0
// Last Modified: 2026-08-31
// Author: ULTRA OS Framework

#include "VirtualFS/VirtualFSRamDiskPlatform.h"

#include <cerrno>
#include <filesystem>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <unistd.h>

// Defined in <linux/magic.h>, repeated here to avoid a kernel-header
// dependency in a portable module.
#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif

namespace VirtualFS {
namespace RamDiskDetail {

namespace {

const char* kShmRoot = "/dev/shm";

// Discs are per-user, so the uid goes in the path: two users asking for
// "scratch" get separate discs rather than a permissions failure.
std::string MountPathFor(const std::string& name) {
    return std::string(kShmRoot) + "/" + MountPrefix() +
           std::to_string(static_cast<unsigned long>(getuid())) + "-" + name;
}

// Recovers the disc name from a mount path, or "" if the path is not ours.
std::string NameFromMountPath(const std::string& path) {
    const std::string prefix = std::string(MountPrefix()) +
        std::to_string(static_cast<unsigned long>(getuid())) + "-";
    const std::string leaf = std::filesystem::path(path).filename().string();
    if (leaf.size() <= prefix.size() || leaf.compare(0, prefix.size(), prefix) != 0) {
        return {};
    }
    return leaf.substr(prefix.size());
}

// True when /dev/shm is really a tmpfs. If a system has it mounted as
// something else, we must not claim the disc is RAM-backed.
bool ShmIsTmpfs() {
    struct statfs fsInfo {};
    if (statfs(kShmRoot, &fsInfo) != 0) {
        return false;
    }
    return fsInfo.f_type == static_cast<decltype(fsInfo.f_type)>(TMPFS_MAGIC);
}

uint64_t ShmFreeBytes() {
    struct statfs fsInfo {};
    if (statfs(kShmRoot, &fsInfo) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(fsInfo.f_bavail) *
           static_cast<uint64_t>(fsInfo.f_bsize);
}

} // namespace

VirtualFSResult PlatformCreate(const std::string& name,
                               uint64_t sizeBytes,
                               VirtualFSRamDisk& outDisk) {
    if (!ShmIsTmpfs()) {
        // Without a tmpfs there is no RAM disc to hand out, and silently
        // using ordinary storage would break the IsTrueRam() contract.
        return VirtualFSResult::NotSupported;
    }

    const std::string mountPath = MountPathFor(name);

    std::error_code ec;
    if (std::filesystem::exists(mountPath, ec)) {
        return VirtualFSResult::AlreadyExists;
    }

    const uint64_t freeBytes = ShmFreeBytes();
    if (freeBytes > 0 && sizeBytes > freeBytes) {
        return VirtualFSResult::DiskFull;
    }

    // mkdir directly with 0700 rather than create_directory + permissions:
    // that would leave a window where the disc is world-readable.
    if (::mkdir(mountPath.c_str(), S_IRWXU) != 0) {
        return (errno == EACCES || errno == EPERM) ? VirtualFSResult::AccessDenied
                                                   : VirtualFSResult::Error;
    }

    outDisk.mountPath = mountPath;
    outDisk.capacityBytes = ShmFreeBytes();
    outDisk.backing = VirtualFSRamDiskBacking::Tmpfs;
    outDisk.deviceId.clear();  // a directory, not a device node
    return VirtualFSResult::Success;
}

VirtualFSResult PlatformDestroy(const VirtualFSRamDisk& disk) {
    std::error_code ec;
    if (!std::filesystem::exists(disk.mountPath, ec)) {
        return VirtualFSResult::Success;  // already gone - destroying is idempotent
    }

    // tmpfs pages are freed on unlink, so no wipe pass is needed here.
    std::filesystem::remove_all(disk.mountPath, ec);
    return ec ? VirtualFSResult::Error : VirtualFSResult::Success;
}

std::vector<VirtualFSRamDisk> PlatformList() {
    std::vector<VirtualFSRamDisk> discs;
    if (!ShmIsTmpfs()) {
        return discs;
    }

    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(kShmRoot, ec);
         !ec && it != std::filesystem::directory_iterator(); ++it) {
        if (!it->is_directory(ec)) {
            continue;
        }
        const std::string path = it->path().string();
        const std::string name = NameFromMountPath(path);
        if (name.empty()) {
            continue;
        }

        VirtualFSRamDisk disk;
        disk.name = name;
        disk.mountPath = path;
        disk.backing = VirtualFSRamDiskBacking::Tmpfs;
        disk.capacityBytes = ShmFreeBytes();
        discs.push_back(disk);
    }
    return discs;
}

} // namespace RamDiskDetail

VirtualFSRamDiskBacking VirtualFS_GetPreferredRamDiskBacking() {
    return RamDiskDetail::ShmIsTmpfs() ? VirtualFSRamDiskBacking::Tmpfs
                                       : VirtualFSRamDiskBacking::None;
}

} // namespace VirtualFS
