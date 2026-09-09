// Apps/UltraFiler/UltraFilerVolumeSpace.h
// How full a mounted volume is - what the Computer page's drive cards show.
// QueryVolumeSpace asks the filesystem (std::filesystem::space) for one
// mount point; the rest turns the answer into the numbers a card carries:
// the used share for the pie chart, and "232.9 GB free of 476.2 GB" for the
// line under it.
//
// Kept apart from the window, without a framework dependency, so the sums
// and the wording can be tested on their own — and so the query can run on
// a worker thread: a network share that stopped answering makes
// std::filesystem::space wait out a timeout, which must never happen on the
// UI thread.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace UltraCanvas {

// The sizes of one volume. `known` is false while nothing could be read:
// the mount point vanished between listing and asking, or the filesystem
// reports no capacity (a pseudo filesystem, a share that is not answering).
struct VolumeSpace {
    std::string path;          // mount point / drive root, as listed
    uint64_t totalBytes = 0;   // capacity
    uint64_t freeBytes = 0;    // available to this user (not the root-reserved part)
    bool known = false;
};

// Reads the sizes of the volume mounted at `path`. Never throws: a failure
// answers with `known == false`. May block for as long as the filesystem
// takes to answer - see the file comment.
inline VolumeSpace QueryVolumeSpace(const std::string& path) {
    VolumeSpace space;
    space.path = path;
    std::error_code ec;
    const std::filesystem::space_info info = std::filesystem::space(path, ec);
    if (ec || info.capacity == 0 ||
        info.capacity == static_cast<uintmax_t>(-1)) {
        return space;
    }
    space.totalBytes = static_cast<uint64_t>(info.capacity);
    // `available` is what this user can still write; `free` also counts the
    // blocks reserved for root, which no file of the user will ever occupy.
    const uintmax_t available =
            info.available == static_cast<uintmax_t>(-1) ? 0 : info.available;
    space.freeBytes = static_cast<uint64_t>(
            available > info.capacity ? info.capacity : available);
    space.known = true;
    return space;
}

// The occupied bytes: everything the user cannot write into.
inline uint64_t VolumeUsedBytes(const VolumeSpace& space) {
    if (!space.known || space.totalBytes < space.freeBytes) return 0;
    return space.totalBytes - space.freeBytes;
}

// How full the volume is, 0..100. An unknown volume reads as empty.
inline double VolumeUsedPercent(const VolumeSpace& space) {
    if (!space.known || space.totalBytes == 0) return 0.0;
    const double percent =
            100.0 * static_cast<double>(VolumeUsedBytes(space)) /
            static_cast<double>(space.totalBytes);
    return percent < 0.0 ? 0.0 : (percent > 100.0 ? 100.0 : percent);
}

// A size the way a drive is talked about: one decimal from a megabyte up
// ("476.2 GB", "1.8 TB", "512.0 MB"), whole numbers below it. Binary units,
// like the sizes the file display shows, so the two never disagree about the
// same folder.
inline std::string FormatVolumeBytes(uint64_t bytes) {
    constexpr double kKilo = 1024.0;
    constexpr double kMega = kKilo * 1024.0;
    constexpr double kGiga = kMega * 1024.0;
    constexpr double kTera = kGiga * 1024.0;
    const double value = static_cast<double>(bytes);
    char buffer[48];
    if (value >= kTera) {
        std::snprintf(buffer, sizeof buffer, "%.1f TB", value / kTera);
    } else if (value >= kGiga) {
        std::snprintf(buffer, sizeof buffer, "%.1f GB", value / kGiga);
    } else if (value >= kMega) {
        std::snprintf(buffer, sizeof buffer, "%.1f MB", value / kMega);
    } else if (value >= kKilo) {
        std::snprintf(buffer, sizeof buffer, "%.0f KB", value / kKilo);
    } else {
        std::snprintf(buffer, sizeof buffer, "%llu bytes",
                      static_cast<unsigned long long>(bytes));
    }
    return buffer;
}

// The line under a drive's pie chart: "232.9 GB free of 476.2 GB", or what
// stands in for it while the sizes are not known.
inline std::string DescribeVolumeSpace(const VolumeSpace& space) {
    if (!space.known) return "Size not available";
    return FormatVolumeBytes(space.freeBytes) + " free of " +
           FormatVolumeBytes(space.totalBytes);
}

// The share as a caption: "62% used".
inline std::string DescribeVolumeUsage(const VolumeSpace& space) {
    if (!space.known) return "";
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "%.0f%% used", VolumeUsedPercent(space));
    return buffer;
}

} // namespace UltraCanvas
