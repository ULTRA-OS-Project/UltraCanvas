// VirtualFS/include/VirtualFS/VirtualFSRamDisk.h
// OS-visible RAM disc provisioning for VirtualFS
// Version: 1.0.0
// Last Modified: 2026-08-31
// Author: ULTRA OS Framework
#pragma once

#include "VirtualFSTypes.h"

#include <string>
#include <vector>

namespace VirtualFS {

/**
 * @file VirtualFSRamDisk.h
 * @brief Creates RAM discs that the operating system can see.
 *
 * This is deliberately NOT an in-process memory filesystem. A RAM disc made
 * here is a real mount point: `fopen()`, other processes and the platform
 * file manager can all reach it. VirtualFS does not implement the disc
 * itself - implementing one means shipping a kernel driver - it drives the
 * facility each platform already provides:
 *
 * | Platform | Mechanism                          | True RAM? | Privileges |
 * |----------|------------------------------------|-----------|------------|
 * | Linux    | `/dev/shm` (tmpfs)                 | Yes       | None       |
 * | macOS    | `hdiutil attach ram://` + erase    | Yes       | None       |
 * | Windows  | ImDisk driver, when installed      | Yes       | Admin      |
 * | Windows  | `%TEMP%` + secure wipe (fallback)  | **No**    | None       |
 *
 * Windows has no built-in RAM disc, so the fallback is on real storage.
 * Always check `VirtualFSRamDisk::IsTrueRam()` before treating a disc as
 * memory-only; the backing is reported, never silently substituted.
 *
 * Discs are private to the calling user (mode 0700 and equivalents).
 *
 * @warning A RAM disc does not survive a reboot, and on most platforms not
 *          even a crash of this process (the mount is released on
 *          destruction). Never put the only copy of anything there.
 */

// ============================================================================
// TYPES
// ============================================================================

/**
 * @enum VirtualFSRamDiskBacking
 * @brief What is actually storing the bytes.
 */
enum class VirtualFSRamDiskBacking {
    None = 0,       // Not created
    Tmpfs = 1,      // Linux: a directory on /dev/shm (true RAM)
    HdiUtil = 2,    // macOS: hdiutil ram:// device (true RAM)
    ImDisk = 3,     // Windows: ImDisk virtual disk (true RAM)
    DiskFallback = 4 // Windows: ordinary storage, wiped on destroy (NOT RAM)
};

/**
 * @brief Human-readable name for a backing, for logs and UI.
 */
const char* VirtualFSRamDiskBackingToString(VirtualFSRamDiskBacking backing);

/**
 * @struct VirtualFSRamDisk
 * @brief A provisioned RAM disc.
 *
 * Obtained from VirtualFS_CreateRamDisk() and passed back to
 * VirtualFS_DestroyRamDisk(). Treat it as an opaque handle: the fields are
 * readable for diagnostics, but only the functions below may change them.
 */
struct VirtualFSRamDisk {
    std::string name;                   // Caller-supplied label
    std::string mountPath;              // Where it is mounted (a real path)
    uint64_t requestedBytes = 0;        // Size asked for
    uint64_t capacityBytes = 0;         // Size actually available (0 = unknown)
    VirtualFSRamDiskBacking backing = VirtualFSRamDiskBacking::None;

    // Platform handle: the /dev/diskN node on macOS, the drive letter on
    // Windows. Empty on Linux, where the directory path is the whole story.
    std::string deviceId;

    /**
     * @brief True when the bytes really do live in memory.
     *
     * False for the Windows disk fallback. Check this before storing
     * anything that must not reach persistent storage.
     */
    bool IsTrueRam() const {
        return backing == VirtualFSRamDiskBacking::Tmpfs ||
               backing == VirtualFSRamDiskBacking::HdiUtil ||
               backing == VirtualFSRamDiskBacking::ImDisk;
    }

    /** @brief True when this handle refers to a live disc. */
    bool IsValid() const {
        return backing != VirtualFSRamDiskBacking::None && !mountPath.empty();
    }

    explicit operator bool() const { return IsValid(); }
};

// ============================================================================
// CAPABILITY QUERY
// ============================================================================

/**
 * @brief Reports whether this platform can provide a true RAM disc.
 *
 * False on Windows without an ImDisk installation - VirtualFS_CreateRamDisk()
 * still succeeds there, but returns a DiskFallback disc.
 */
bool VirtualFS_IsTrueRamDiskAvailable();

/**
 * @brief The backing VirtualFS_CreateRamDisk() would choose right now.
 *
 * Lets callers warn before creating anything (e.g. "no RAM disc driver
 * installed; scratch data will be written to disk").
 */
VirtualFSRamDiskBacking VirtualFS_GetPreferredRamDiskBacking();

// ============================================================================
// LIFECYCLE
// ============================================================================

/**
 * @brief Creates an OS-visible RAM disc.
 *
 * The disc is private to the calling user. On success @p outDisk describes
 * where it landed and what is backing it; check IsTrueRam() if that matters.
 *
 * On Linux @p sizeBytes is advisory: the disc is a directory on the shared
 * /dev/shm tmpfs, which has one global size limit, and setting a private
 * quota needs root. capacityBytes then reports the space actually free on
 * that tmpfs. macOS and ImDisk discs are sized exactly.
 *
 * @param name       Label; also part of the mount path, so keep it to
 *                   [A-Za-z0-9._-]. Must be unique per user.
 * @param sizeBytes  Requested capacity. Must be > 0.
 * @param outDisk    Receives the disc on success; untouched on failure.
 * @return Success, InvalidArgument for a bad name or size, AlreadyExists if
 *         a disc of that name is already mounted, AccessDenied, or Error.
 */
VirtualFSResult VirtualFS_CreateRamDisk(const std::string& name,
                                        uint64_t sizeBytes,
                                        VirtualFSRamDisk& outDisk);

/**
 * @brief Unmounts a RAM disc and releases its memory.
 *
 * Everything on the disc is destroyed. For a DiskFallback disc the contents
 * are overwritten before being unlinked, since they were on real storage.
 *
 * @param disk Disc to destroy; reset to an invalid handle on success.
 * @return Success, InvalidArgument for an invalid handle, or Error. Reports
 *         Success if the mount is already gone.
 */
VirtualFSResult VirtualFS_DestroyRamDisk(VirtualFSRamDisk& disk);

/**
 * @brief Lists this user's VirtualFS RAM discs, including ones this process
 *        did not create (e.g. leaked by a process that crashed).
 *
 * Useful at startup to clean up after an unclean shutdown.
 */
std::vector<VirtualFSRamDisk> VirtualFS_ListRamDisks();

// ============================================================================
// INTEGRATION
// ============================================================================

/**
 * @brief Points the VirtualFS temp directory at a RAM disc.
 *
 * Equivalent to VirtualFS_SetTempDirectory(disk.mountPath), but refuses a
 * disc that is not live. Any temp file VirtualFS still needs - such as a
 * nested archive for a provider without MemoryOpen support - then lands in
 * memory rather than on disk.
 *
 * @return Success, or InvalidArgument for an invalid disc.
 */
VirtualFSResult VirtualFS_UseRamDiskForTemp(const VirtualFSRamDisk& disk);

} // namespace VirtualFS
