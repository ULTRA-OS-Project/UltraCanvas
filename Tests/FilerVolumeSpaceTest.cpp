// Tests/FilerVolumeSpaceTest.cpp
// The numbers and the wording of the Computer page's drive cards
// (Apps/UltraFiler/UltraFilerVolumeSpace.h): the used share the pie chart is
// cut by, and the "232.9 GB free of 476.2 GB" line under it. A card that
// says a full disk is 0% used, or "0 bytes free of 0 bytes" for a volume
// that could not be read, defeats showing a number at all.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework
#include "UltraFilerVolumeSpace.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "  ok  " : " FAIL ", what);
    if (!ok) ++failures;
}

void CheckEq(const std::string& got, const std::string& want, const char* what) {
    const bool ok = got == want;
    std::printf("%s  %s", ok ? "  ok  " : " FAIL ", what);
    if (!ok) std::printf("  (got \"%s\", want \"%s\")", got.c_str(), want.c_str());
    std::printf("\n");
    if (!ok) ++failures;
}

VolumeSpace Known(uint64_t total, uint64_t free) {
    VolumeSpace s;
    s.path = "/";
    s.totalBytes = total;
    s.freeBytes = free;
    s.known = true;
    return s;
}

} // namespace

int main() {
    constexpr uint64_t kGiB = 1024ull * 1024ull * 1024ull;

    // ===== USED BYTES AND SHARE =====
    {
        const VolumeSpace half = Known(400 * kGiB, 200 * kGiB);
        Check(VolumeUsedBytes(half) == 200 * kGiB, "used = total - free");
        Check(VolumeUsedPercent(half) > 49.99 && VolumeUsedPercent(half) < 50.01,
              "half full reads as 50%");

        const VolumeSpace full = Known(100 * kGiB, 0);
        Check(VolumeUsedPercent(full) > 99.99, "no room left reads as 100%");

        const VolumeSpace empty = Known(100 * kGiB, 100 * kGiB);
        Check(VolumeUsedPercent(empty) < 0.01, "nothing used reads as 0%");

        VolumeSpace unknown;
        unknown.path = "/nowhere";
        Check(VolumeUsedBytes(unknown) == 0 && VolumeUsedPercent(unknown) == 0.0,
              "an unread volume has no used share");

        // A filesystem answering with more available than its capacity (a
        // quota-limited share does) must not go negative or above 100%.
        VolumeSpace odd = Known(10 * kGiB, 12 * kGiB);
        Check(VolumeUsedBytes(odd) == 0, "free above total clamps used to 0");
        Check(VolumeUsedPercent(odd) == 0.0, "free above total clamps share to 0%");
    }

    // ===== SIZE WORDING =====
    {
        CheckEq(FormatVolumeBytes(0), "0 bytes", "0 bytes");
        CheckEq(FormatVolumeBytes(1023), "1023 bytes", "below a kilobyte in bytes");
        CheckEq(FormatVolumeBytes(1536), "2 KB", "kilobytes rounded whole");
        CheckEq(FormatVolumeBytes(512ull * 1024ull * 1024ull), "512.0 MB", "megabytes with one decimal");
        CheckEq(FormatVolumeBytes(476ull * kGiB + kGiB / 5), "476.2 GB", "gigabytes with one decimal");
        CheckEq(FormatVolumeBytes(1843ull * kGiB), "1.8 TB", "terabytes with one decimal");
    }

    // ===== THE CARD'S LINES =====
    {
        const VolumeSpace disk = Known(476ull * kGiB + kGiB / 5, 232ull * kGiB + 9 * kGiB / 10);
        CheckEq(DescribeVolumeSpace(disk), "232.9 GB free of 476.2 GB", "free-of line");
        CheckEq(DescribeVolumeUsage(disk), "51% used", "usage caption");

        VolumeSpace unknown;
        unknown.path = "/nowhere";
        CheckEq(DescribeVolumeSpace(unknown), "Size not available", "unread volume's line");
        CheckEq(DescribeVolumeUsage(unknown), "", "unread volume has no usage caption");
    }

    // ===== ASKING THE FILESYSTEM =====
    {
        // The folder this test runs in is on some volume: its sizes are
        // readable and consistent. A path that does not exist is not an
        // error to throw - it is a volume that is not there.
        const std::string here = std::filesystem::current_path().string();
        const VolumeSpace space = QueryVolumeSpace(here);
        Check(space.known, "the current folder's volume can be read");
        Check(space.path == here, "the answer names the path it was asked for");
        Check(space.totalBytes > 0, "a real volume has a capacity");
        Check(space.freeBytes <= space.totalBytes, "free never exceeds total");

        const VolumeSpace missing = QueryVolumeSpace(here + "/no-such-folder-for-this-test");
        Check(!missing.known, "a missing path answers unknown, not an exception");
        Check(missing.totalBytes == 0 && missing.freeBytes == 0, "an unknown volume carries no sizes");
    }

    if (failures) {
        std::printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    std::printf("\nall checks passed\n");
    return 0;
}
