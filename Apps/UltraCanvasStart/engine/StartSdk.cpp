// Apps/UltraCanvasStart/engine/StartSdk.cpp
// Version: 0.2.0 - SdkDownloadPage is the UltraOS download page; no download here
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartSdk.h"

#include "UltraCanvasUtils.h"
#include "UltraCanvasPathUtf8.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace UltraCanvasStart {

namespace {
constexpr const char* kRepository = "ULTRA-OS-Project/UltraCanvas";

std::string SdkOsName(Platform platform) {
    switch (platform) {
        case Platform::Linux:   return "Linux";
        case Platform::MacOS:   return "MacOS";
        case Platform::Windows: return "Windows";
        default:                return "Unknown";
    }
}
} // namespace

std::string SdkArtifactName(Platform platform, const std::string& version,
                            const std::string& architecture) {
    return "UltraCanvas-SDK-" + SdkOsName(platform) + "-" + version + "-" + architecture;
}

std::string SdkArchiveName(Platform platform, const std::string& version,
                           const std::string& architecture) {
    // The extension the workflow's "Pack the SDK" step gives each platform.
    switch (platform) {
        case Platform::Windows: return SdkArtifactName(platform, version, architecture) + ".zip";
        case Platform::Linux:   return SdkArtifactName(platform, version, architecture) + ".tar.xz";
        default:                return SdkArtifactName(platform, version, architecture) + ".tar.gz";
    }
}

std::string SdkDownloadPage() {
    return "https://ultraos.eu/download-file/";
}

std::string SdkReleasePage(const std::string& version) {
    return std::string("https://github.com/") + kRepository + "/releases/tag/v" + version;
}

PlanStep UnpackStep(const std::string& archive, const std::string& destination) {
    PlanStep step;
    step.kind = StepKind::Unpack;
    step.title = "Unpack the SDK";
    step.description = "Unpacks " + archive + " into " + destination;
    // GNU tar on Linux and bsdtar on macOS and Windows 10+ read a .tar.gz
    // and a .tar.xz by their contents (-xf, no compression flag); bsdtar
    // reads a .zip as well, and Windows ships it as tar.exe.
    step.argv = { "tar", "-xf", archive, "-C", destination };
    return step;
}

std::string SdkUnpackedFolder(const std::string& archive) {
    for (const char* ext : { ".tar.xz", ".tar.gz", ".zip" }) {
        const std::string suffix = ext;
        if (archive.size() > suffix.size() &&
            archive.compare(archive.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return archive.substr(0, archive.size() - suffix.size());
        }
    }
    return archive;
}

std::string FindSdkPrefix(const std::string& destination) {
    std::error_code ec;
    const fs::path root = UltraCanvas::PathFromUtf8(destination);
    if (!fs::is_directory(root, ec)) return {};
    const fs::path config = UltraCanvas::PathFromUtf8("lib/cmake/UltraCanvas/UltraCanvasConfig.cmake");
    if (fs::is_regular_file(root / config, ec)) return UltraCanvas::PathToUtf8(root);
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (entry.is_directory(ec) && fs::is_regular_file(entry.path() / config, ec)) {
            return UltraCanvas::PathToUtf8(entry.path());
        }
    }
    return {};
}

std::string FrameworkVersion() {
    return UltraCanvas::versionString;
}

} // namespace UltraCanvasStart
