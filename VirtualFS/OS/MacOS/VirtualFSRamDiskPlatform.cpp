// VirtualFS/OS/MacOS/VirtualFSRamDiskPlatform.cpp
// macOS RAM disc back end - hdiutil ram:// device formatted as APFS/HFS+.
//
// macOS exposes RAM-backed block devices through hdiutil, and a normal user
// may both attach one and erase a volume onto it, so no privilege escalation
// is needed:
//
//     hdiutil attach -nomount ram://<512-byte blocks>   -> /dev/diskN
//     diskutil erasevolume HFS+ <name> /dev/diskN       -> /Volumes/<name>
//
// Unlike the Linux back end this gives an exactly sized volume, because the
// device is created with a fixed block count.
// Version: 1.0.0
// Last Modified: 2026-08-31
// Author: ULTRA OS Framework

#include "VirtualFS/VirtualFSRamDiskPlatform.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace VirtualFS {
namespace RamDiskDetail {

namespace {

// Runs a command, returning its trimmed stdout. exitCode receives the status.
std::string RunCommand(const std::string& command, int& exitCode) {
    exitCode = -1;
    std::FILE* pipe = ::popen(command.c_str(), "r");
    if (!pipe) {
        return {};
    }

    std::string output;
    std::array<char, 256> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        output += buffer.data();
    }
    exitCode = ::pclose(pipe);

    while (!output.empty() &&
           (output.back() == '\n' || output.back() == '\r' || output.back() == ' ')) {
        output.pop_back();
    }
    return output;
}

// Volume names carry the shared prefix so PlatformList() can spot discs left
// behind by a process that died before detaching them.
std::string VolumeNameFor(const std::string& name) {
    return std::string(MountPrefix()) + name;
}

std::string MountPathFor(const std::string& name) {
    return "/Volumes/" + VolumeNameFor(name);
}

std::string NameFromVolumeName(const std::string& volumeName) {
    const std::string prefix = MountPrefix();
    if (volumeName.size() <= prefix.size() ||
        volumeName.compare(0, prefix.size(), prefix) != 0) {
        return {};
    }
    return volumeName.substr(prefix.size());
}

bool HaveTool(const char* tool) {
    int exitCode = 0;
    RunCommand(std::string("command -v ") + tool + " >/dev/null 2>&1", exitCode);
    return exitCode == 0;
}

} // namespace

VirtualFSResult PlatformCreate(const std::string& name,
                               uint64_t sizeBytes,
                               VirtualFSRamDisk& outDisk) {
    if (!HaveTool("hdiutil") || !HaveTool("diskutil")) {
        return VirtualFSResult::NotSupported;
    }

    const std::string mountPath = MountPathFor(name);
    std::error_code ec;
    if (std::filesystem::exists(mountPath, ec)) {
        return VirtualFSResult::AlreadyExists;
    }

    // ram:// takes a count of 512-byte blocks, rounded up so a request is
    // never silently truncated.
    const uint64_t blocks = (sizeBytes + 511) / 512;

    int exitCode = 0;
    const std::string device = RunCommand(
        "hdiutil attach -nomount ram://" + std::to_string(blocks) + " 2>/dev/null",
        exitCode);

    if (exitCode != 0 || device.empty() || device.compare(0, 5, "/dev/") != 0) {
        return VirtualFSResult::Error;
    }

    // The device is raw until a filesystem is written onto it.
    const std::string volumeName = VolumeNameFor(name);
    RunCommand("diskutil erasevolume HFS+ '" + volumeName + "' " + device +
                   " >/dev/null 2>&1",
               exitCode);

    if (exitCode != 0) {
        // Leaving an attached but unformatted device behind would leak the
        // memory for the life of the login session.
        int detachCode = 0;
        RunCommand("hdiutil detach " + device + " -force >/dev/null 2>&1", detachCode);
        return VirtualFSResult::Error;
    }

    // erasevolume mounts world-readable; the disc is meant to be private.
    ::chmod(mountPath.c_str(), S_IRWXU);

    outDisk.mountPath = mountPath;
    outDisk.capacityBytes = blocks * 512;
    outDisk.backing = VirtualFSRamDiskBacking::HdiUtil;
    outDisk.deviceId = device;
    return VirtualFSResult::Success;
}

VirtualFSResult PlatformDestroy(const VirtualFSRamDisk& disk) {
    std::error_code ec;
    const bool mounted = std::filesystem::exists(disk.mountPath, ec);

    if (!mounted && disk.deviceId.empty()) {
        return VirtualFSResult::Success;  // already gone
    }

    int exitCode = 0;
    if (!disk.deviceId.empty()) {
        // Detaching the device releases the pages; ejecting the volume alone
        // would leave the RAM device attached.
        RunCommand("hdiutil detach " + disk.deviceId + " -force >/dev/null 2>&1",
                   exitCode);
    } else {
        RunCommand("diskutil eject '" + disk.mountPath + "' >/dev/null 2>&1", exitCode);
    }

    if (exitCode != 0 && std::filesystem::exists(disk.mountPath, ec)) {
        return VirtualFSResult::Error;
    }
    return VirtualFSResult::Success;
}

std::vector<VirtualFSRamDisk> PlatformList() {
    std::vector<VirtualFSRamDisk> discs;

    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator("/Volumes", ec);
         !ec && it != std::filesystem::directory_iterator(); ++it) {
        const std::string volumeName = it->path().filename().string();
        const std::string name = NameFromVolumeName(volumeName);
        if (name.empty()) {
            continue;
        }

        VirtualFSRamDisk disk;
        disk.name = name;
        disk.mountPath = it->path().string();
        disk.backing = VirtualFSRamDiskBacking::HdiUtil;

        // The device node is not recoverable from the mount point alone;
        // PlatformDestroy() falls back to ejecting by path.
        std::error_code sizeEc;
        const auto space = std::filesystem::space(disk.mountPath, sizeEc);
        disk.capacityBytes = sizeEc ? 0 : space.capacity;

        discs.push_back(disk);
    }
    return discs;
}

} // namespace RamDiskDetail

VirtualFSRamDiskBacking VirtualFS_GetPreferredRamDiskBacking() {
    return (RamDiskDetail::HaveTool("hdiutil") && RamDiskDetail::HaveTool("diskutil"))
               ? VirtualFSRamDiskBacking::HdiUtil
               : VirtualFSRamDiskBacking::None;
}

} // namespace VirtualFS
