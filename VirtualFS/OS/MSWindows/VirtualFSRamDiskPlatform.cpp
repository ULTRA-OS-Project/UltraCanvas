// VirtualFS/OS/MSWindows/VirtualFSRamDiskPlatform.cpp
// Windows RAM disc back end - ImDisk when installed, disk fallback otherwise.
//
// Windows ships no RAM disc facility. Every real option is a third-party
// kernel driver, so this back end detects one rather than depending on it:
//
//   1. ImDisk, if imdisk.exe is on PATH:
//        imdisk -a -s <bytes> -m <drive>: -p "/fs:ntfs /q /y"
//      True RAM backing. Creating the disc needs administrator rights, so
//      this succeeds only in an elevated process.
//
//   2. Otherwise a directory under %TEMP%, wiped on destroy. This is NOT
//      RAM - it is ordinary storage - and reports
//      VirtualFSRamDiskBacking::DiskFallback so callers can tell. Anything
//      that must never reach persistent storage has to check IsTrueRam().
//
// The fallback exists so that calling code has one code path on all
// platforms; it is not a security equivalent.
// Version: 1.0.0
// Last Modified: 2026-08-31
// Author: ULTRA OS Framework

#include "VirtualFS/VirtualFSRamDiskPlatform.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace VirtualFS {
namespace RamDiskDetail {

namespace {

int RunCommand(const std::string& command) {
    // _popen keeps this free of CreateProcess plumbing; output is discarded
    // because the exit code is all that matters here.
    std::FILE* pipe = ::_popen((command + " >NUL 2>&1").c_str(), "r");
    if (!pipe) {
        return -1;
    }
    return ::_pclose(pipe);
}

bool HaveImDisk() {
    return RunCommand("where imdisk") == 0;
}

// ImDisk discs are addressed by drive letter. Picks the highest free letter,
// working down from Z: so it does not collide with mapped network drives.
char FindFreeDriveLetter() {
    const DWORD mask = ::GetLogicalDrives();
    for (int i = 25; i >= 3; --i) {  // Z: down to D:
        if ((mask & (1UL << i)) == 0) {
            return static_cast<char>('A' + i);
        }
    }
    return '\0';
}

std::string TempRoot() {
    char buffer[MAX_PATH + 1] = {};
    const DWORD length = ::GetTempPathA(MAX_PATH, buffer);
    if (length == 0 || length > MAX_PATH) {
        return ".";
    }
    return std::string(buffer, length);
}

std::string FallbackPathFor(const std::string& name) {
    return TempRoot() + MountPrefix() + name;
}

// ImDisk discs live on a drive letter, which carries no name. The NTFS
// volume label does, so it is set to the prefixed disc name on creation and
// read back here - that is what makes duplicate detection and listing work
// for ImDisk discs, not just for fallback directories.
std::string VolumeLabelFor(const std::string& name) {
    return std::string(MountPrefix()) + name;
}

std::string ReadVolumeLabel(const std::string& driveRoot) {
    char label[MAX_PATH + 1] = {};
    if (!::GetVolumeInformationA(driveRoot.c_str(), label, MAX_PATH,
                                 nullptr, nullptr, nullptr, nullptr, 0)) {
        return {};
    }
    return std::string(label);
}

std::string NameFromVolumeLabel(const std::string& label) {
    const std::string prefix = MountPrefix();
    if (label.size() <= prefix.size() ||
        label.compare(0, prefix.size(), prefix) != 0) {
        return {};
    }
    return label.substr(prefix.size());
}

// Finds the drive letter hosting the ImDisk disc with this name, or '\0'.
char FindImDiskDrive(const std::string& name) {
    const std::string wanted = VolumeLabelFor(name);
    const DWORD mask = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if ((mask & (1UL << i)) == 0) {
            continue;
        }
        const std::string root = std::string(1, static_cast<char>('A' + i)) + ":\\";
        if (ReadVolumeLabel(root) == wanted) {
            return static_cast<char>('A' + i);
        }
    }
    return '\0';
}

std::string NameFromFallbackPath(const std::string& path) {
    const std::string prefix = MountPrefix();
    const std::string leaf = std::filesystem::path(path).filename().string();
    if (leaf.size() <= prefix.size() || leaf.compare(0, prefix.size(), prefix) != 0) {
        return {};
    }
    return leaf.substr(prefix.size());
}

// Marks a directory as accessible only to its owner. Windows has no chmod,
// so this strips inherited ACEs and grants the current user alone.
void RestrictToCurrentUser(const std::string& path) {
    // icacls is the least invasive way to do this without pulling the whole
    // security API into a portable module.
    RunCommand("icacls \"" + path + "\" /inheritance:r /grant:r \"%USERNAME%\":(OI)(CI)F");
}

} // namespace

VirtualFSResult PlatformCreate(const std::string& name,
                               uint64_t sizeBytes,
                               VirtualFSRamDisk& outDisk) {
    // --- Preferred: a real RAM disc through ImDisk ---------------------
    if (HaveImDisk()) {
        if (FindImDiskDrive(name) != '\0') {
            return VirtualFSResult::AlreadyExists;
        }

        const char letter = FindFreeDriveLetter();
        if (letter != '\0') {
            const std::string drive = std::string(1, letter) + ":";
            // /v: stamps the volume label that FindImDiskDrive() looks for.
            const int status = RunCommand(
                "imdisk -a -s " + std::to_string(sizeBytes) +
                " -m " + drive + " -p \"/fs:ntfs /q /y /v:" +
                VolumeLabelFor(name) + "\"");

            if (status == 0) {
                const std::string mountPath = drive + "\\";
                RestrictToCurrentUser(mountPath);

                outDisk.mountPath = mountPath;
                outDisk.capacityBytes = sizeBytes;
                outDisk.backing = VirtualFSRamDiskBacking::ImDisk;
                outDisk.deviceId = drive;
                return VirtualFSResult::Success;
            }
            // Falls through: imdisk.exe exists but the call failed, which is
            // usually a non-elevated process. The fallback still works.
        }
    }

    // --- Fallback: ordinary storage, wiped on destroy ------------------
    const std::string path = FallbackPathFor(name);

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return VirtualFSResult::AlreadyExists;
    }
    if (!std::filesystem::create_directories(path, ec) || ec) {
        return VirtualFSResult::Error;
    }
    RestrictToCurrentUser(path);

    outDisk.mountPath = path;
    outDisk.capacityBytes = sizeBytes;  // advisory: not actually enforced
    outDisk.backing = VirtualFSRamDiskBacking::DiskFallback;
    outDisk.deviceId.clear();
    return VirtualFSResult::Success;
}

VirtualFSResult PlatformDestroy(const VirtualFSRamDisk& disk) {
    if (disk.backing == VirtualFSRamDiskBacking::ImDisk) {
        if (disk.deviceId.empty()) {
            return VirtualFSResult::InvalidArgument;
        }
        // -D forces removal even while handles are open; the disc is being
        // torn down either way and a leaked RAM disc is worse.
        const int status = RunCommand("imdisk -D -m " + disk.deviceId);
        return status == 0 ? VirtualFSResult::Success : VirtualFSResult::Error;
    }

    std::error_code ec;
    if (!std::filesystem::exists(disk.mountPath, ec)) {
        return VirtualFSResult::Success;  // already gone
    }
    // The fallback wrote to real storage, so overwrite before unlinking.
    return SecureWipeDirectory(disk.mountPath) ? VirtualFSResult::Success
                                               : VirtualFSResult::Error;
}

std::vector<VirtualFSRamDisk> PlatformList() {
    std::vector<VirtualFSRamDisk> discs;

    // ImDisk discs, found by their volume label.
    const DWORD mask = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if ((mask & (1UL << i)) == 0) {
            continue;
        }
        const std::string drive = std::string(1, static_cast<char>('A' + i)) + ":";
        const std::string root = drive + "\\";
        const std::string name = NameFromVolumeLabel(ReadVolumeLabel(root));
        if (name.empty()) {
            continue;
        }

        VirtualFSRamDisk disk;
        disk.name = name;
        disk.mountPath = root;
        disk.backing = VirtualFSRamDiskBacking::ImDisk;
        disk.deviceId = drive;

        std::error_code spaceEc;
        const auto space = std::filesystem::space(root, spaceEc);
        disk.capacityBytes = spaceEc ? 0 : space.capacity;

        discs.push_back(disk);
    }

    // Fallback directories, which can also outlive a crash.
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(TempRoot(), ec);
         !ec && it != std::filesystem::directory_iterator(); ++it) {
        if (!it->is_directory(ec)) {
            continue;
        }
        const std::string name = NameFromFallbackPath(it->path().string());
        if (name.empty()) {
            continue;
        }

        VirtualFSRamDisk disk;
        disk.name = name;
        disk.mountPath = it->path().string();
        disk.backing = VirtualFSRamDiskBacking::DiskFallback;
        discs.push_back(disk);
    }
    return discs;
}

} // namespace RamDiskDetail

VirtualFSRamDiskBacking VirtualFS_GetPreferredRamDiskBacking() {
    return RamDiskDetail::HaveImDisk() ? VirtualFSRamDiskBacking::ImDisk
                                       : VirtualFSRamDiskBacking::DiskFallback;
}

} // namespace VirtualFS
