// Apps/UltraCanvasStart/engine/StartSdk.h
// The prebuilt SDK: the artifact CI publishes for each platform
// (Docs/UltraCanvasSDK.md), how it is named, where it is found, and how an
// archive of it is unpacked into a prefix an application's CMake can use.
// Version: 0.2.0 - the SDK is downloaded from the UltraOS download page in the
//                  browser; DownloadSdk and the direct asset URL are gone
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "StartTypes.h"

#include <string>

namespace UltraCanvasStart {

// "UltraCanvas-SDK-Linux-0.9.147-x86_64", the way .github/workflows/build.yml
// names it: Linux / MacOS / Windows, the framework version, x86_64 / arm64.
std::string SdkArtifactName(Platform platform, const std::string& version,
                            const std::string& architecture);

// The archive's file name: .tar.xz on Linux, .tar.gz on macOS, .zip on Windows.
std::string SdkArchiveName(Platform platform, const std::string& version,
                           const std::string& architecture);

// Where the SDKs are published. SdkDownloadPage is the UltraOS download
// page, opened in the browser: the programmer picks the archive SdkArchiveName
// names and the Framework step unpacks it. The page decides how files are
// served, so nothing here depends on an address per archive. SdkReleasePage
// is the GitHub release tagged v<version>, where every release build of main
// attaches the same six archives (.github/workflows/build.yml, publish-sdk).
std::string SdkDownloadPage();
std::string SdkReleasePage(const std::string& version);

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
