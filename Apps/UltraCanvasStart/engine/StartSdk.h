// Apps/UltraCanvasStart/engine/StartSdk.h
// The prebuilt SDK: the artifact CI publishes for each platform
// (Docs/UltraCanvasSDK.md), how it is named, where it is found, and how an
// archive of it is unpacked into a prefix an application's CMake can use.
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

// The archive's file name: .tar.gz on Linux and macOS, .zip on Windows.
std::string SdkArchiveName(Platform platform, const std::string& version,
                           const std::string& architecture);

// Where the SDKs are published. Every release build of main attaches the
// six archives to the GitHub release tagged v<version>
// (.github/workflows/build.yml, publish-sdk), so SdkReleaseAssetUrl is a
// fixed address anyone can fetch; SdkReleasePage is that release's page.
// SdkDownloadPage is the Actions page, where the same archives are workflow
// artifacts for a signed-in browser - the fallback while a release build is
// still running, or for a pull request's build.
std::string SdkReleasePage(const std::string& version);
std::string SdkReleaseAssetUrl(Platform platform, const std::string& version,
                               const std::string& architecture);
std::string SdkDownloadPage();

// Whether an UltraNet download is compiled in.
bool SdkDownloadAvailable();

// Downloads `url` to `localPath` (UTF-8), starting UltraNet if nothing has
// yet. False with `error` set when the download is unavailable or failed.
bool DownloadSdk(const std::string& url, const std::string& localPath, std::string& error);

// The step that unpacks `archive` into `destination` (both UTF-8): tar on
// Linux and macOS, tar as well on Windows 10 and later (bsdtar reads zip),
// run through RunProcessCaptured.
PlanStep UnpackStep(const std::string& archive, const std::string& destination);

// The prefix an unpacked archive gives: destination/<artifact name>, where
// lib/cmake/UltraCanvas/UltraCanvasConfig.cmake lives. Empty if not found.
std::string FindSdkPrefix(const std::string& destination);

// The version of the framework this binary was built from, which is the SDK
// version that matches it.
std::string FrameworkVersion();

} // namespace UltraCanvasStart
