// Apps/UltraCanvasStart/engine/StartSdk.h
// The prebuilt SDK: the artifact CI publishes for each platform
// (Docs/UltraCanvasSDK.md), how it is named, where it is found, and how an
// archive of it is unpacked into a prefix an application's CMake can use.
// Version: 0.3.0 - DownloadSdk: the archive from start.ultraos.eu/pub, which CI's
//                  publish-sdk-website job fills, checked against its SHA256SUMS;
//                  SdkDownloadPage gone
// Version: 0.2.0 - the SDK is downloaded from the UltraOS download page in the
//                  browser; DownloadSdk and the direct asset URL are gone
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "StartTypes.h"

#include <cstdint>
#include <functional>
#include <string>

namespace UltraCanvasStart {

// "UltraCanvas-SDK-Linux-0.9.147-x86_64", the way .github/workflows/build.yml
// names it: Linux / MacOS / Windows, the framework version, x86_64 / arm64.
std::string SdkArtifactName(Platform platform, const std::string& version,
                            const std::string& architecture);

// The archive's file name: .tar.xz on Linux, .tar.gz on macOS, .zip on Windows.
std::string SdkArchiveName(Platform platform, const std::string& version,
                           const std::string& architecture);

// Where the SDKs are published. Every release build of main uploads its six
// archives, with a SHA256SUMS beside them, to the UltraOS server
// (.github/workflows/build.yml, publish-sdk-website), which serves them as
//
//   https://start.ultraos.eu/pub/<version>/<archive>
//   https://start.ultraos.eu/pub/<version>/SHA256SUMS
//
// SdkDownloadBase is that address up to and with "pub/". SdkReleasePage is
// the GitHub release tagged v<version>, which carries the same archives
// (publish-sdk): the way in when the server cannot be reached.
std::string SdkDownloadBase();
std::string SdkDownloadUrl(Platform platform, const std::string& version,
                           const std::string& architecture,
                           const std::string& base = SdkDownloadBase());
std::string SdkChecksumsUrl(const std::string& version,
                            const std::string& base = SdkDownloadBase());
std::string SdkReleasePage(const std::string& version);

// The SHA-256 a SHA256SUMS file (sha256sum's output: "<64 hex>  <name>", a
// '*' before the name in binary mode) gives for `archiveName`, in lower
// case; empty when the file does not list it.
std::string ChecksumFor(const std::string& sha256sums, const std::string& archiveName);

// Whether this build can download: the core was built with UltraNet
// (ULTRACANVAS_HAS_NET). Without it the Framework step opens the address in
// the browser instead.
bool SdkDownloadAvailable();

struct SdkDownloadResult {
    bool ok = false;
    std::string archive;    // the downloaded archive (UTF-8), on success
    std::string sha256;     // its SHA-256, as SHA256SUMS gives it
    bool verified = false;  // false only when this build has no SHA-256
    std::string error;      // what went wrong, for a message box
};

// bytesNow, and bytesTotal or -1 when the server did not say. Called on the
// thread that runs DownloadSdk.
using SdkDownloadProgress = std::function<void(int64_t bytesNow, int64_t bytesTotal)>;

// Downloads this platform's archive of `version` into `folder` (UTF-8),
// blocking: SHA256SUMS first, then the archive into <archive>.part, whose
// SHA-256 must be the one SHA256SUMS lists before it is renamed to the
// archive's name. A version the server does not have, an archive the sums
// do not list, a transfer that fails and a checksum that differs each end
// with ok false, an error that says which, and no file left behind. `base`
// is SdkDownloadBase(); a test points it at a server of its own.
SdkDownloadResult DownloadSdk(Platform platform, const std::string& version,
                              const std::string& architecture, const std::string& folder,
                              const SdkDownloadProgress& progress = {},
                              const std::string& base = SdkDownloadBase());

// The step that unpacks `archive` into `destination` (both UTF-8): tar on
// Linux and macOS, tar as well on Windows 10 and later (bsdtar reads zip),
// run through RunProcessCaptured.
PlanStep UnpackStep(const std::string& archive, const std::string& destination);

// The folder an archive unpacks to beside itself (UTF-8): the archive's path
// without .zip, .tar.xz or .tar.gz - the artifact name, when the file kept it.
std::string SdkUnpackedFolder(const std::string& archive);

// The prefix an unpacked archive gives: destination/<artifact name>, where
// lib/cmake/UltraCanvas/UltraCanvasConfig.cmake lives. Empty if not found.
std::string FindSdkPrefix(const std::string& destination);

// The version of the framework this binary was built from, which is the SDK
// version that matches it.
std::string FrameworkVersion();

} // namespace UltraCanvasStart
