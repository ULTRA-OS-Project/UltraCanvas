// Tests/VirtualFSRamDiskTest.cpp
// Test for OS-visible RAM disc provisioning (VirtualFS).
//
// A VirtualFS RAM disc is a real mount point, not an in-process structure,
// so the properties worth testing are the ones that distinguish it from a
// scratch directory. This test verifies:
//   1. The disc is created, is a real directory, and ordinary file I/O
//      through <cstdio> reaches it - i.e. the OS really can see it.
//   2. It is private to the calling user (0700 on POSIX).
//   3. The reported backing is honest: on Linux it must be true RAM, and
//      the mount must actually be tmpfs.
//   4. Destroy is idempotent and takes the contents with it.
//   5. Name validation rejects paths that could escape the mount root.
//   6. VirtualFS_UseRamDiskForTemp() redirects the manager, and destroying
//      the disc moves the temp directory back off it.
// Version: 1.0.0
// Last Modified: 2026-08-31
// Author: UltraCanvas Framework

#include "VirtualFS/VirtualFS.h"
#include "VirtualFS/VirtualFSRamDisk.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <sys/statfs.h>
#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif
#endif

namespace fs = std::filesystem;
using namespace VirtualFS;

static int failures = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (cond) {                                                         \
            std::printf("  PASS  %s\n", msg);                               \
        } else {                                                            \
            std::printf("  FAIL  %s (line %d)\n", msg, __LINE__);           \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

int main() {
    std::printf("VirtualFS RAM disc test\n");
    std::printf("  preferred backing: %s\n",
                VirtualFSRamDiskBackingToString(
                    VirtualFS_GetPreferredRamDiskBacking()));
    std::printf("  true RAM available: %s\n\n",
                VirtualFS_IsTrueRamDiskAvailable() ? "yes" : "no");

    VirtualFS_Initialize();

    // --- 1. Create ---------------------------------------------------------
    VirtualFSRamDisk disk;
    const VirtualFSResult created =
        VirtualFS_CreateRamDisk("unittest", 4ull * 1024 * 1024, disk);

    if (created != VirtualFSResult::Success) {
        std::printf("  FAIL  could not create RAM disc (%s)\n",
                    VirtualFSResultToString(created).c_str());
        return 1;
    }
    std::printf("  mounted at: %s (%s)\n\n", disk.mountPath.c_str(),
                VirtualFSRamDiskBackingToString(disk.backing));

    CHECK(disk.IsValid(), "created disc reports valid");
    CHECK(fs::is_directory(disk.mountPath), "mount path is a real directory");

    // --- 2. The OS can see it: write through plain stdio, not VirtualFS ----
    const std::string probe = disk.mountPath + "/probe.txt";
    const char* payload = "ultravfs ram disc probe";
    {
        std::FILE* f = std::fopen(probe.c_str(), "wb");
        CHECK(f != nullptr, "plain fopen() can create a file on the disc");
        if (f) {
            std::fwrite(payload, 1, std::strlen(payload), f);
            std::fclose(f);
        }
    }

    char readBack[64] = {};
    {
        std::FILE* f = std::fopen(probe.c_str(), "rb");
        if (f) {
            std::fread(readBack, 1, sizeof(readBack) - 1, f);
            std::fclose(f);
        }
    }
    CHECK(std::strcmp(readBack, payload) == 0,
          "content written through the OS reads back intact");

    // --- 3. Privacy and backing honesty ------------------------------------
#if !defined(_WIN32)
    struct stat st {};
    if (::stat(disk.mountPath.c_str(), &st) == 0) {
        const mode_t perms = st.st_mode & 07777;
        CHECK(perms == 0700, "disc is private to the current user (0700)");
        CHECK((perms & 0077) == 0, "no group or world access");
    } else {
        CHECK(false, "could not stat the mount path");
    }
#endif

#if defined(__linux__)
    CHECK(disk.backing == VirtualFSRamDiskBacking::Tmpfs,
          "Linux disc is tmpfs-backed");
    CHECK(disk.IsTrueRam(), "Linux disc reports true RAM backing");

    struct statfs fsInfo {};
    if (::statfs(disk.mountPath.c_str(), &fsInfo) == 0) {
        CHECK(fsInfo.f_type == static_cast<decltype(fsInfo.f_type)>(TMPFS_MAGIC),
              "mount really is tmpfs, not a plain directory");
    } else {
        CHECK(false, "could not statfs the mount path");
    }
#endif

    // A disc that claims true RAM must not be the Windows disk fallback.
    CHECK(disk.IsTrueRam() ==
              (disk.backing != VirtualFSRamDiskBacking::DiskFallback),
          "IsTrueRam() agrees with the reported backing");

    // --- 4. Listing finds it -----------------------------------------------
    bool listed = false;
    for (const auto& d : VirtualFS_ListRamDisks()) {
        if (d.name == "unittest") {
            listed = true;
        }
    }
    CHECK(listed, "live disc appears in VirtualFS_ListRamDisks()");

    // --- 5. Name validation -------------------------------------------------
    VirtualFSRamDisk bad;
    CHECK(VirtualFS_CreateRamDisk("../escape", 1024, bad) ==
              VirtualFSResult::InvalidArgument,
          "a name containing '..' is rejected");
    CHECK(VirtualFS_CreateRamDisk("has/slash", 1024, bad) ==
              VirtualFSResult::InvalidArgument,
          "a name containing '/' is rejected");
    CHECK(VirtualFS_CreateRamDisk("", 1024, bad) ==
              VirtualFSResult::InvalidArgument,
          "an empty name is rejected");
    CHECK(VirtualFS_CreateRamDisk("zerosize", 0, bad) ==
              VirtualFSResult::InvalidArgument,
          "a zero size is rejected");

    VirtualFSRamDisk duplicate;
    CHECK(VirtualFS_CreateRamDisk("unittest", 1024 * 1024, duplicate) ==
              VirtualFSResult::AlreadyExists,
          "creating a second disc with the same name is refused");

    // --- 6. Temp redirection ------------------------------------------------
    const std::string originalTemp =
        VirtualFS_GetTempDirectory();

    CHECK(VirtualFS_UseRamDiskForTemp(disk) == VirtualFSResult::Success,
          "temp directory redirects onto the disc");
    CHECK(VirtualFS_GetTempDirectory() == disk.mountPath,
          "manager reports the disc as its temp directory");

    VirtualFSRamDisk invalid;
    CHECK(VirtualFS_UseRamDiskForTemp(invalid) == VirtualFSResult::InvalidArgument,
          "redirecting onto an invalid disc is refused");

    // --- 7. Destroy ---------------------------------------------------------
    const std::string mountPath = disk.mountPath;
    CHECK(VirtualFS_DestroyRamDisk(disk) == VirtualFSResult::Success,
          "disc is destroyed");
    CHECK(!fs::exists(mountPath), "mount path is gone after destroy");
    CHECK(!fs::exists(probe), "disc contents went with it");
    CHECK(!disk.IsValid(), "handle is reset after destroy");

    // Destroying left the manager pointed somewhere usable, not at a mount
    // that no longer exists.
    const std::string tempAfter =
        VirtualFS_GetTempDirectory();
    CHECK(tempAfter != mountPath,
          "temp directory moved off the disc when it was destroyed");
    CHECK(fs::is_directory(tempAfter), "temp directory is usable again");

    VirtualFSRamDisk stale;
    stale.name = "unittest";
    stale.mountPath = mountPath;
    stale.backing = VirtualFSRamDiskBacking::Tmpfs;
    CHECK(VirtualFS_DestroyRamDisk(stale) == VirtualFSResult::Success,
          "destroying an already-gone disc is idempotent");

    VirtualFS_Shutdown();
    (void)originalTemp;

    std::printf("\n%s (%d failure%s)\n",
                failures == 0 ? "ALL PASSED" : "FAILURES",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
