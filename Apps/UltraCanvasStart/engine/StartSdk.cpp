// Apps/UltraCanvasStart/engine/StartSdk.cpp
// Version: 0.3.0 - DownloadSdk from start.ultraos.eu/pub, checked against SHA256SUMS
// Version: 0.2.0 - SdkDownloadPage is the UltraOS download page; no download here
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartSdk.h"

#include "UltraCanvasUtils.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCrypt/UltraCryptCore.h"
#if defined(ULTRACANVAS_HAS_NET) && ULTRACANVAS_HAS_NET
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"
#endif

#include <cctype>
#include <filesystem>
#include <sstream>

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

std::string SdkDownloadBase() {
    return "https://start.ultraos.eu/pub/";
}

std::string SdkDownloadUrl(Platform platform, const std::string& version,
                           const std::string& architecture, const std::string& base) {
    return base + version + "/" + SdkArchiveName(platform, version, architecture);
}

std::string SdkChecksumsUrl(const std::string& version, const std::string& base) {
    return base + version + "/SHA256SUMS";
}

std::string ChecksumFor(const std::string& sha256sums, const std::string& archiveName) {
    std::istringstream lines(sha256sums);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // "<64 hex><space><space or *><name>"
        if (line.size() < 66 || line[64] != ' ') continue;
        std::string hash = line.substr(0, 64);
        bool hex = true;
        for (char& c : hash) {
            if (!std::isxdigit(static_cast<unsigned char>(c))) { hex = false; break; }
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (!hex) continue;
        std::size_t name = 65;
        if (line[name] == ' ' || line[name] == '*') ++name;
        if (line.compare(name, std::string::npos, archiveName) == 0) return hash;
    }
    return {};
}

bool SdkDownloadAvailable() {
#if defined(ULTRACANVAS_HAS_NET) && ULTRACANVAS_HAS_NET
    return true;
#else
    return false;
#endif
}

SdkDownloadResult DownloadSdk(Platform platform, const std::string& version,
                              const std::string& architecture, const std::string& folder,
                              const SdkDownloadProgress& progress, const std::string& base) {
    SdkDownloadResult out;
    const std::string archiveName = SdkArchiveName(platform, version, architecture);
    const std::string archiveUrl = SdkDownloadUrl(platform, version, architecture, base);
    const std::string sumsUrl = SdkChecksumsUrl(version, base);
#if defined(ULTRACANVAS_HAS_NET) && ULTRACANVAS_HAS_NET
    std::error_code ec;
    const fs::path dir = UltraCanvas::PathFromUtf8(folder);
    if (folder.empty() || !fs::is_directory(dir, ec)) {
        out.error = "The folder " + folder + " does not exist.";
        return out;
    }
    if (!UltraNet_IsInitialized()) {
        const UltraNetResult init = UltraNet_Initialize();
        if (!init.success) {
            out.error = "The network library could not start: " + init.message;
            return out;
        }
    }
    // Plain http:// only for a test's own server; the real one is https://,
    // and a redirect must not take the download off TLS.
    const bool https = base.rfind("https://", 0) == 0;

    // 1. SHA256SUMS: says the version is there, and what the archive must be.
    UltraNetResponse sumsResponse;
    UltraNetHttpOptions sumsOptions;
    sumsOptions.httpsOnlyRedirects = https;
    sumsOptions.maxReceiveSize = 64 * 1024;
    const UltraNetResult sums = UltraNet_HttpGet(sumsUrl, sumsResponse, sumsOptions);
    if (!sums.success) {
        if (sums.httpStatus == 404) {
            out.error = "UltraCanvas " + version + " is not on " + base + " (yet): " + sumsUrl +
                        " answered 404.\n\nA release is uploaded a few minutes after its build; "
                        "the same archive is on " + SdkReleasePage(version) + ".";
        } else {
            out.error = "Could not reach " + sumsUrl + ": " + sums.message;
        }
        return out;
    }
    const std::string expected = ChecksumFor(sumsResponse.GetBodyAsString(), archiveName);
    if (expected.empty()) {
        out.error = sumsUrl + " does not list " + archiveName +
                    ": this platform's SDK is missing from version " + version + ".";
        return out;
    }

    // 2. The archive, into <name>.part until it is known to be whole.
    const fs::path target = dir / UltraCanvas::PathFromUtf8(archiveName);
    const fs::path part = dir / UltraCanvas::PathFromUtf8(archiveName + ".part");
    UltraNetHttpRequest request;
    request.url = archiveUrl;
    request.options.httpsOnlyRedirects = https;
    request.options.outputFilePath = UltraCanvas::PathToUtf8(part);
    request.onDownloadProgress = progress;
    UltraNetResponse archiveResponse;
    const UltraNetResult got = UltraNet_HttpRequest(request, archiveResponse);
    if (!got.success) {
        fs::remove(part, ec);
        out.error = got.httpStatus == 404
            ? archiveUrl + " answered 404, although SHA256SUMS lists it."
            : "The download of " + archiveUrl + " failed: " + got.message;
        return out;
    }

    // 3. The checksum. Without SHA-256 in this build (UltraCrypt without
    //    libsodium) the archive is kept, and the result says it is unchecked.
    out.sha256 = expected;
    if (UltraCrypt_IsHashAvailable(UltraCryptHashAlgorithm::SHA256)) {
        std::vector<uint8_t> digest;
        if (!UltraCrypt_HashFile(UltraCryptHashAlgorithm::SHA256, UltraCanvas::PathToUtf8(part), digest)) {
            fs::remove(part, ec);
            out.error = "Could not read the downloaded " + archiveName + " to check it.";
            return out;
        }
        const std::string actual = UltraCrypt_ToHex(digest);
        std::string lower = actual;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower != expected) {
            fs::remove(part, ec);
            out.error = "The downloaded " + archiveName + " is not the published one: its SHA-256 is " +
                        lower + ", SHA256SUMS says " + expected + ". It was deleted; try again.";
            return out;
        }
        out.verified = true;
    }

    // 4. Whole: under its own name, over an older copy.
    fs::remove(target, ec);
    fs::rename(part, target, ec);
    if (ec) {
        out.error = "Could not rename " + UltraCanvas::PathToUtf8(part) + " to " + archiveName +
                    ": " + ec.message();
        fs::remove(part, ec);
        return out;
    }
    out.ok = true;
    out.archive = UltraCanvas::PathToUtf8(target);
    return out;
#else
    (void)progress;
    (void)folder;
    out.error = "This build of UltraCanvasStart has no network library: download " + archiveUrl +
                " in the browser and unpack it with Unpack...";
    return out;
#endif
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
