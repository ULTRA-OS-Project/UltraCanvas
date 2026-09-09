// Apps/UltraCleaner/engine/UltraCleanerVolumes.h
// The drives UltraCleaner can show and clean, with how full each one is.
//
// The framework has no volume enumeration, so this lives with the cleaner
// for now; it is domain-neutral enough to graduate into UltraCanvas if a
// second caller ever wants it. Capacity itself comes from
// std::filesystem::space(), which is portable — only the list of mount
// points needs a per-platform answer.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCleaner {

struct VolumeInfo {
    std::string mountPoint;   // "/", "/home", "C:/"
    std::string name;         // what to show: label, or the mount point
    std::string filesystem;   // "ext4", "apfs", "NTFS" — "" when unknown
    uint64_t totalBytes = 0;
    uint64_t freeBytes = 0;   // free to this user
    bool isSystemVolume = false;

    uint64_t UsedBytes() const {
        return totalBytes > freeBytes ? totalBytes - freeBytes : 0;
    }
    // 0..100. Returns 0 for a volume whose size is unknown.
    double UsedPercent() const {
        return totalBytes == 0 ? 0.0
                               : 100.0 * double(UsedBytes()) / double(totalBytes);
    }
};

// Every mounted volume worth showing a person: real, writable filesystems,
// with the pseudo and container mounts (proc, sysfs, cgroup, snap loopbacks,
// …) left out. Sorted with the system volume first, then by size.
std::vector<VolumeInfo> ListVolumes();

} // namespace UltraCleaner
