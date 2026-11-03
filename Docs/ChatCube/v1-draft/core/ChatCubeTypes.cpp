// ChatCube/core/ChatCubeTypes.cpp
// Implementation of helper methods declared in ChatCubeTypes.h
// Version: 1.0.0
// Last Modified: 2026-04-13
// Author: UltraCanvas Framework

#include "ChatCubeTypes.h"
#include <sstream>
#include <iomanip>

namespace UltraCanvas {
namespace ChatCube {

// ============================================================================
// ChatAttachment::GetDisplaySize
// Returns a human-readable file size string, e.g. "7.23 MB" or "512 KB"
// Matches the format shown in the RISC OS ChatCube screenshots
// ============================================================================

std::string ChatAttachment::GetDisplaySize() const {
    if (FileSizeBytes <= 0) return "";

    constexpr int64_t KB = 1024;
    constexpr int64_t MB = 1024 * KB;
    constexpr int64_t GB = 1024 * MB;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if (FileSizeBytes >= GB) {
        oss << static_cast<double>(FileSizeBytes) / static_cast<double>(GB) << " GB";
    } else if (FileSizeBytes >= MB) {
        oss << static_cast<double>(FileSizeBytes) / static_cast<double>(MB) << " MB";
    } else if (FileSizeBytes >= KB) {
        oss << static_cast<double>(FileSizeBytes) / static_cast<double>(KB) << " KB";
    } else {
        oss.unsetf(std::ios::fixed);
        oss << FileSizeBytes << " B";
    }

    return oss.str();
}

} // namespace ChatCube
} // namespace UltraCanvas
