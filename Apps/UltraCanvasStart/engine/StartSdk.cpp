// Apps/UltraCanvasStart/engine/StartSdk.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartSdk.h"

#include "UltraCanvasUtils.h"
#include "UltraCanvasPathUtf8.h"

#if defined(ULTRACANVASSTART_HAS_NET)
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"
#endif

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
    return SdkArtifactName(platform, version, architecture) +
           (platform == Platform::Windows ? ".zip" : ".tar.gz");
}

std::string SdkDownloadPage() {
    return std::string("https://github.com/") + kRepository + "/actions/workflows/build.yml?query=branch%3Amain";
}

std::string SdkReleasePage(const std::string& version) {
    return std::string("https://github.com/") + kRepository + "/releases/tag/v" + version;
}

std::string SdkReleaseAssetUrl(Platform platform, const std::string& version,
                               const std::string& architecture) {
    return std::string("https://github.com/") + kRepository + "/releases/download/v" +
           version + "/" + SdkArchiveName(platform, version, architecture);
}

bool SdkDownloadAvailable() {
#if defined(ULTRACANVASSTART_HAS_NET)
    return true;
#else
    return false;
#endif
}

bool DownloadSdk(const std::string& url, const std::string& localPath, std::string& error) {
#if defined(ULTRACANVASSTART_HAS_NET)
    if (!UltraNet_IsInitialized()) {
        const UltraNetResult started = UltraNet_Initialize();
        if (!started.success) {
            error = "The network module could not start: " + started.message;
            return false;
        }
    }
    UltraNetHttpOptions options = UltraNetHttpOptions::Default();
    options.followRedirects = true;
    const UltraNetResult result = UltraNet_HttpDownloadFile(url, localPath, options);
    if (result.success) return true;
    error = result.message;
    if (result.httpStatus == 404) {
        error = "Nothing at " + url + " yet: the release of this version may still be building. "
                "Try again later, or download the archive of the same name from " + SdkDownloadPage() +
                " and unpack it by hand.";
    }
    return false;
#else
    (void)url;
    (void)localPath;
    error = "This build of UltraCanvasStart has no UltraNet; download the archive in a browser from " +
            SdkDownloadPage();
    return false;
#endif
}

PlanStep UnpackStep(const std::string& archive, const std::string& destination) {
    PlanStep step;
    step.kind = StepKind::Unpack;
    step.title = "Unpack the SDK";
    step.description = "Unpacks " + archive + " into " + destination;
    // GNU tar on Linux and bsdtar on macOS and Windows 10+ all read a .tar.gz;
    // bsdtar reads a .zip as well, and Windows ships it as tar.exe.
    step.argv = { "tar", "-xf", archive, "-C", destination };
    return step;
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
