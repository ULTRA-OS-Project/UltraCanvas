// Apps/UltraCleaner/engine/UltraCleanerTypes.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerTypes.h"

#include <algorithm>
#include <cstdio>

namespace UltraCleaner {

CleanerPlatform CurrentPlatform() {
#if defined(_WIN32) || defined(_WIN64)
    return CleanerPlatform::Windows;
#elif defined(__APPLE__)
    return CleanerPlatform::MacOS;
#elif defined(__linux__)
    return CleanerPlatform::Linux;
#else
    return CleanerPlatform::Unknown;
#endif
}

std::string PlatformName(CleanerPlatform platform) {
    switch (platform) {
        case CleanerPlatform::Linux:   return "Linux";
        case CleanerPlatform::MacOS:   return "macOS";
        case CleanerPlatform::Windows: return "Windows";
        default:                       return "Unknown";
    }
}

const char* CategoryKey(CleanCategory category) {
    switch (category) {
        case CleanCategory::TemporaryFiles:     return "temp";
        case CleanCategory::ApplicationCaches:  return "appcache";
        case CleanCategory::BrowserCaches:      return "browsercache";
        case CleanCategory::Logs:               return "logs";
        case CleanCategory::CrashReports:       return "crash";
        case CleanCategory::Thumbnails:         return "thumbnails";
        case CleanCategory::TrashBin:           return "trash";
        case CleanCategory::PackageCaches:      return "packagecache";
        case CleanCategory::DeveloperJunk:      return "devjunk";
        case CleanCategory::InstallerLeftovers: return "installers";
        case CleanCategory::BrokenLinks:        return "brokenlinks";
        default:                                return "unknown";
    }
}

std::string CategoryTitle(CleanCategory category) {
    switch (category) {
        case CleanCategory::TemporaryFiles:     return "Temporary files";
        case CleanCategory::ApplicationCaches:  return "Application caches";
        case CleanCategory::BrowserCaches:      return "Browser caches";
        case CleanCategory::Logs:               return "Log files";
        case CleanCategory::CrashReports:       return "Crash reports";
        case CleanCategory::Thumbnails:         return "Thumbnail caches";
        case CleanCategory::TrashBin:           return "Trash";
        case CleanCategory::PackageCaches:      return "Package manager caches";
        case CleanCategory::DeveloperJunk:      return "Developer leftovers";
        case CleanCategory::InstallerLeftovers: return "Installers and updates";
        case CleanCategory::BrokenLinks:        return "Broken shortcuts";
        default:                                return "Unknown";
    }
}

std::string CategoryDescription(CleanCategory category) {
    switch (category) {
        case CleanCategory::TemporaryFiles:
            return "Scratch files the system and applications left in the "
                   "temporary directories. Recreated on demand.";
        case CleanCategory::ApplicationCaches:
            return "On-disk caches applications rebuild by themselves. "
                   "Removing them costs one slower start.";
        case CleanCategory::BrowserCaches:
            return "Cached pages, images and scripts. Logins, bookmarks and "
                   "history are not touched.";
        case CleanCategory::Logs:
            return "Diagnostic logs written by applications. Keep them if you "
                   "are chasing a bug.";
        case CleanCategory::CrashReports:
            return "Crash dumps and diagnostic reports from applications that "
                   "stopped unexpectedly.";
        case CleanCategory::Thumbnails:
            return "Preview images the file manager generated. Regenerated the "
                   "next time a folder is opened.";
        case CleanCategory::TrashBin:
            return "Files already sent to the trash. Emptying it is permanent.";
        case CleanCategory::PackageCaches:
            return "Downloaded packages kept by npm, pip, gradle, cargo and "
                   "friends. Re-downloaded when needed.";
        case CleanCategory::DeveloperJunk:
            return "Build output, derived data and simulator caches. Rebuilt "
                   "by the next build, which takes longer.";
        case CleanCategory::InstallerLeftovers:
            return "Installer packages and update payloads that have already "
                   "been applied.";
        case CleanCategory::BrokenLinks:
            return "Symbolic links whose target no longer exists.";
        default:
            return "";
    }
}

const std::vector<CleanCategory>& AllCategories() {
    static const std::vector<CleanCategory> categories = {
        CleanCategory::TemporaryFiles,
        CleanCategory::ApplicationCaches,
        CleanCategory::BrowserCaches,
        CleanCategory::Thumbnails,
        CleanCategory::Logs,
        CleanCategory::CrashReports,
        CleanCategory::PackageCaches,
        CleanCategory::DeveloperJunk,
        CleanCategory::InstallerLeftovers,
        CleanCategory::BrokenLinks,
        CleanCategory::TrashBin,
    };
    return categories;
}

const CategorySummary* ScanReport::Find(CleanCategory category) const {
    for (const auto& summary : categories) {
        if (summary.category == category) return &summary;
    }
    return nullptr;
}

void SummarizeReport(ScanReport& report) {
    report.categories.clear();
    report.totalBytes = 0;
    report.totalItems = report.items.size();

    for (CleanCategory category : AllCategories()) {
        CategorySummary summary;
        summary.category = category;
        for (const auto& item : report.items) {
            if (item.category != category) continue;
            summary.totalBytes += item.sizeBytes;
            ++summary.itemCount;
            if (!item.safeByDefault) summary.safeByDefault = false;
        }
        report.totalBytes += summary.totalBytes;
        if (summary.itemCount > 0) report.categories.push_back(summary);
    }
}

std::string FormatByteSize(uint64_t bytes) {
    if (bytes < 1000) {
        return std::to_string(bytes) + (bytes == 1 ? " byte" : " bytes");
    }
    static const char* units[] = { "KB", "MB", "GB", "TB", "PB" };
    double value = static_cast<double>(bytes) / 1000.0;
    int unit = 0;
    while (value >= 1000.0 && unit < 4) {
        value /= 1000.0;
        ++unit;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f %s", value, units[unit]);
    return buffer;
}

} // namespace UltraCleaner
