// Apps/UltraCleaner/engine/UltraCleanerVolumes.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerVolumes.h"

#include "UltraCleanerPaths.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/mount.h>
#include <sys/param.h>
#endif

namespace UltraCleaner {
namespace {

namespace fs = std::filesystem;

// Fills totalBytes / freeBytes from the filesystem. Returns false when the
// mount point cannot be queried, which is normal for something that
// disappeared between listing and asking.
bool ReadCapacity(VolumeInfo& volume) {
    std::error_code ec;
    const fs::space_info space = fs::space(volume.mountPoint, ec);
    if (ec || space.capacity == 0 ||
        space.capacity == static_cast<uintmax_t>(-1)) {
        return false;
    }
    volume.totalBytes = static_cast<uint64_t>(space.capacity);
    volume.freeBytes = static_cast<uint64_t>(space.available);
    return true;
}

#if !defined(_WIN32) && !defined(_WIN64)
// Filesystems that are not storage: showing them as "drives" would be noise.
bool IsPseudoFilesystem(const std::string& type) {
    static const char* pseudo[] = {
        "proc", "sysfs", "devtmpfs", "devpts", "cgroup", "cgroup2", "tmpfs",
        "securityfs", "pstore", "efivarfs", "bpf", "debugfs", "tracefs",
        "configfs", "fusectl", "hugetlbfs", "mqueue", "ramfs", "autofs",
        "binfmt_misc", "squashfs", "nsfs", "overlay", "devfs", "ctfs",
        "objfs", "fdescfs"
    };
    for (const char* name : pseudo) {
        if (type == name) return true;
    }
    return false;
}

// What to call a mount in the drive row. The root has no last path component
// to name it by, and "/" tells a user nothing, so it is "System" — the same
// word on every POSIX platform, since the same drive should not be labelled
// differently depending on where the app is running.
std::string DisplayNameForMount(const std::string& mountPoint) {
    if (mountPoint == "/") return "System";
    const std::string leaf = fs::path(mountPoint).filename().string();
    return leaf.empty() ? mountPoint : leaf;
}
#endif

} // namespace

std::vector<VolumeInfo> ListVolumes() {
    std::vector<VolumeInfo> volumes;

#if defined(_WIN32) || defined(_WIN64)
    wchar_t buffer[512] = {};
    const DWORD length = GetLogicalDriveStringsW(
        static_cast<DWORD>(std::size(buffer)), buffer);
    for (const wchar_t* drive = buffer;
         drive < buffer + length && *drive;
         drive += wcslen(drive) + 1) {
        const UINT type = GetDriveTypeW(drive);
        // A CD-ROM with no disc, or a mapped drive that is gone, cannot be
        // cleaned and only clutters the view.
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE && type != DRIVE_RAMDISK) {
            continue;
        }
        char narrow[8] = {};
        WideCharToMultiByte(CP_UTF8, 0, drive, -1, narrow, sizeof narrow, nullptr, nullptr);

        VolumeInfo volume;
        volume.mountPoint = narrow;
        std::replace(volume.mountPoint.begin(), volume.mountPoint.end(), '\\', '/');

        wchar_t label[MAX_PATH + 1] = {};
        wchar_t filesystem[64] = {};
        if (GetVolumeInformationW(drive, label, MAX_PATH, nullptr, nullptr,
                                  nullptr, filesystem, 64)) {
            char narrowLabel[MAX_PATH * 2] = {};
            char narrowFs[128] = {};
            WideCharToMultiByte(CP_UTF8, 0, label, -1, narrowLabel,
                                sizeof narrowLabel, nullptr, nullptr);
            WideCharToMultiByte(CP_UTF8, 0, filesystem, -1, narrowFs,
                                sizeof narrowFs, nullptr, nullptr);
            volume.name = narrowLabel;
            volume.filesystem = narrowFs;
        }
        if (volume.name.empty()) volume.name = volume.mountPoint;

        const std::string windowsDir = WindowsDir();
        volume.isSystemVolume =
            !windowsDir.empty() && IsPathInside(windowsDir, volume.mountPoint);

        if (ReadCapacity(volume)) volumes.push_back(std::move(volume));
    }

#elif defined(__APPLE__)
    struct statfs* mounts = nullptr;
    const int count = getmntinfo(&mounts, MNT_NOWAIT);
    for (int i = 0; i < count; ++i) {
        if ((mounts[i].f_flags & MNT_DONTBROWSE) != 0) continue;
        VolumeInfo volume;
        volume.mountPoint = mounts[i].f_mntonname;
        volume.filesystem = mounts[i].f_fstypename;
        if (IsPseudoFilesystem(volume.filesystem)) continue;
        volume.name = DisplayNameForMount(volume.mountPoint);
        volume.isSystemVolume = volume.mountPoint == "/";
        if (ReadCapacity(volume)) volumes.push_back(std::move(volume));
    }

#else
    // /proc/self/mounts reflects this process's namespace, which is what the
    // user's paths actually resolve through.
    std::ifstream mounts("/proc/self/mounts");
    std::string line;
    while (std::getline(mounts, line)) {
        std::istringstream fields(line);
        std::string device, mountPoint, type;
        if (!(fields >> device >> mountPoint >> type)) continue;
        if (IsPseudoFilesystem(type)) continue;
        // Mount points arrive with spaces escaped as \040.
        std::string::size_type escape = 0;
        while ((escape = mountPoint.find("\\040", escape)) != std::string::npos) {
            mountPoint.replace(escape, 4, " ");
            ++escape;
        }
        // Only device-backed mounts, plus the root whatever backs it.
        if (mountPoint != "/" && device.rfind("/dev/", 0) != 0) continue;

        VolumeInfo volume;
        volume.mountPoint = mountPoint;
        volume.filesystem = type;
        volume.name = DisplayNameForMount(mountPoint);
        volume.isSystemVolume = mountPoint == "/";
        if (ReadCapacity(volume)) volumes.push_back(std::move(volume));
    }
#endif

    // A mount point can be listed more than once (a remount leaves both
    // entries in /proc/self/mounts); the later one wins, so keep the last.
    std::set<std::string> seen;
    std::vector<VolumeInfo> unique;
    unique.reserve(volumes.size());
    for (auto it = volumes.rbegin(); it != volumes.rend(); ++it) {
        if (seen.insert(it->mountPoint).second) unique.push_back(std::move(*it));
    }
    volumes = std::move(unique);

    // The system volume first — it is the one a cleanup usually targets —
    // then the rest largest first, so the biggest drive is easiest to find.
    std::sort(volumes.begin(), volumes.end(),
              [](const VolumeInfo& a, const VolumeInfo& b) {
                  if (a.isSystemVolume != b.isSystemVolume) return a.isSystemVolume;
                  return a.totalBytes > b.totalBytes;
              });
    return volumes;
}

} // namespace UltraCleaner
