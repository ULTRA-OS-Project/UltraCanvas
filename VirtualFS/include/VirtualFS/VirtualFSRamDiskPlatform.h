// VirtualFS/include/VirtualFS/VirtualFSRamDiskPlatform.h
// Internal contract between VirtualFSRamDisk.cpp and the platform back ends.
//
// Not part of the public API - applications include VirtualFSRamDisk.h.
// Exactly one OS/<Platform>/VirtualFSRamDiskPlatform.cpp implements these.
// Version: 1.0.0
// Last Modified: 2026-08-31
// Author: ULTRA OS Framework
#pragma once

#include "VirtualFSRamDisk.h"

#include <string>
#include <vector>

namespace VirtualFS {
namespace RamDiskDetail {

// Prefix shared by every VirtualFS RAM disc, so PlatformList() can recognise
// discs left behind by a process that died before destroying them.
inline const char* MountPrefix() { return "ultravfs-"; }

/**
 * @brief Rejects names that could escape a directory or confuse a shell.
 *        Implemented once in core/VirtualFSRamDisk.cpp.
 */
bool IsValidName(const std::string& name);

/**
 * @brief Overwrites then removes a directory tree. Best-effort; see the
 *        implementation for why that is not a secure-erase guarantee.
 */
bool SecureWipeDirectory(const std::string& path);

/**
 * @brief Provisions the disc. @p outDisk arrives with name and
 *        requestedBytes already set; fill in mountPath, capacityBytes,
 *        backing and (where it applies) deviceId.
 */
VirtualFSResult PlatformCreate(const std::string& name,
                               uint64_t sizeBytes,
                               VirtualFSRamDisk& outDisk);

/**
 * @brief Unmounts and releases the disc. Must report Success when the mount
 *        is already gone, so cleaning up after a crash is idempotent.
 */
VirtualFSResult PlatformDestroy(const VirtualFSRamDisk& disk);

/**
 * @brief Lists this user's VirtualFS discs currently mounted.
 */
std::vector<VirtualFSRamDisk> PlatformList();

} // namespace RamDiskDetail
} // namespace VirtualFS
