// VirtualFS/include/VirtualFS/VirtualFSRamDiskPlatform.h
// Internal contract between VirtualFSRamDisk.cpp and the platform back ends.
//
// Not part of the public API - applications include VirtualFSRamDisk.h.
// Exactly one OS/<Platform>/VirtualFSRamDiskPlatform.cpp implements these.
// Version: 1.2.0 - PlatformPreferredBacking, asked once by the core
// Last Modified: 2026-10-13
// Author: ULTRA OS Framework
#pragma once

#include "VirtualFSRamDisk.h"

#include <cstddef>
#include <string>
#include <vector>

namespace VirtualFS {
namespace RamDiskDetail {

// Prefix shared by every VirtualFS RAM disc, so PlatformList() can recognise
// discs left behind by a process that died before destroying them.
inline const char* MountPrefix() { return "ultravfs-"; }

// The longest name a disc can have where nothing narrower applies: the name
// is part of a path, and this keeps it a short one.
constexpr std::size_t kMaxNameLength = 64;

/**
 * @brief The longest name this back end can keep, at most kMaxNameLength.
 *        Narrower where the name has to fit somewhere smaller - on Windows
 *        an ImDisk disc's name lives in its NTFS volume label.
 */
std::size_t PlatformMaxNameLength();

/**
 * @brief Rejects names that could escape a directory or confuse a shell,
 *        and names longer than PlatformMaxNameLength().
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

/**
 * @brief Asks the system which backing a disc made now would get.
 *
 * This is the probe itself, and it can be slow: on Windows it launches
 * `where imdisk` and on macOS `command -v hdiutil` through a shell, which
 * takes seconds on a loaded machine. The core calls it once per process
 * from VirtualFS_GetPreferredRamDiskBacking() and keeps the answer; the
 * back ends read that, not this, when they need it again.
 */
VirtualFSRamDiskBacking PlatformPreferredBacking();

} // namespace RamDiskDetail
} // namespace VirtualFS
