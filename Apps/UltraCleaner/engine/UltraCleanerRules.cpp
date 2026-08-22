// Apps/UltraCleaner/engine/UltraCleanerRules.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerRules.h"

namespace UltraCleaner {
namespace {

using C = CleanCategory;
using M = RuleMode;
namespace P = PlatformMask;

// Sockets and lock directories the session is actively using. They live in
// the shared temp directory next to real junk, and removing one takes the
// desktop session down with it.
const std::vector<std::string> kPosixTempExcludes = {
    ".X11-unix", ".XIM-unix", ".ICE-unix", ".font-unix", ".Test-unix",
    ".X*-lock", "systemd-private-*", "snap-private-tmp", "snap.*",
    "ssh-*", "dbus-*", "pulse-*", ".esd-*", "gpg-*", "tmux-*",
    "wayland-*", "dotnet-diagnostic-*", "pipewire-*", ".XauthFile*",
};

std::vector<CleanRule> MakeRules() {
    std::vector<CleanRule> rules;
    rules.reserve(64);
    auto Add = [&rules](CleanRule rule) { rules.push_back(std::move(rule)); };

    // ===================================================================
    // Temporary files
    // ===================================================================
    Add({ "temp.user.posix", C::TemporaryFiles,
          "Temporary files",
          { "{TEMP}" },
          M::ClearDirectoryContents, {}, kPosixTempExcludes,
          /*minAgeDays=*/1, /*maxDepth=*/1, /*safe=*/true, P::Posix });

    Add({ "temp.system.posix", C::TemporaryFiles,
          "Shared temporary files",
          { "{SYSTEMTEMP}" },
          M::ClearDirectoryContents, {}, kPosixTempExcludes,
          /*minAgeDays=*/7, /*maxDepth=*/1, /*safe=*/false, P::Posix });

    Add({ "temp.user.windows", C::TemporaryFiles,
          "Temporary files",
          { "{TEMP}" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/1, /*maxDepth=*/1, /*safe=*/true, P::Windows });

    // C:\Windows\Temp belongs to the system: without an elevated process most
    // of it will refuse to go, so it is offered but not pre-ticked.
    Add({ "temp.system.windows", C::TemporaryFiles,
          "Windows temporary files (needs administrator)",
          { "{WINDIR}/Temp" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/1, /*maxDepth=*/1, /*safe=*/false, P::Windows });

    Add({ "temp.dsstore", C::TemporaryFiles,
          "Finder .DS_Store files",
          { "{HOME}" },
          M::RemoveMatchingFiles, { ".DS_Store" }, { "Library", ".Trash" },
          /*minAgeDays=*/0, /*maxDepth=*/6, /*safe=*/false, P::MacOS });

    // ===================================================================
    // Application caches
    // ===================================================================
    Add({ "cache.xdg", C::ApplicationCaches,
          "Application cache",
          { "{CACHE}" },
          M::ClearDirectoryContents, {},
          // Thumbnails and the package-manager caches have rules of their own,
          // so they are excluded here rather than counted twice.
          { "thumbnails", "pip", "yarn", "composer", "go-build", "mozilla",
            "google-chrome", "chromium", "BraveSoftware", "microsoft-edge",
            "vivaldi", "opera" },
          /*minAgeDays=*/7, /*maxDepth=*/1, /*safe=*/true, P::Linux });

    Add({ "cache.macos", C::ApplicationCaches,
          "Application cache",
          { "{HOME}/Library/Caches" },
          M::ClearDirectoryContents, {},
          { "com.apple.QuickLook.thumbnailcache", "pip", "Yarn", "Firefox",
            "Google", "Microsoft Edge", "BraveSoftware", "com.apple.dt.Xcode" },
          /*minAgeDays=*/7, /*maxDepth=*/1, /*safe=*/true, P::MacOS });

    Add({ "cache.macos.containers", C::ApplicationCaches,
          "Sandboxed application cache",
          { "{HOME}/Library/Containers/*/Data/Library/Caches" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/7, /*maxDepth=*/1, /*safe=*/true, P::MacOS });

    Add({ "cache.flatpak", C::ApplicationCaches,
          "Flatpak application cache",
          { "{HOME}/.var/app/*/cache" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/7, /*maxDepth=*/1, /*safe=*/true, P::Linux });

    Add({ "cache.windows.inet", C::ApplicationCaches,
          "Internet file cache",
          { "{LOCALAPPDATA}/Microsoft/Windows/INetCache" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/1, /*maxDepth=*/1, /*safe=*/true, P::Windows });

    Add({ "cache.windows.shaders", C::ApplicationCaches,
          "Graphics shader cache",
          { "{LOCALAPPDATA}/D3DSCache",
            "{LOCALAPPDATA}/NVIDIA/DXCache",
            "{LOCALAPPDATA}/NVIDIA/GLCache",
            "{LOCALAPPDATA}/AMD/DxCache",
            "{LOCALAPPDATA}/Intel/ShaderCache" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Windows });

    Add({ "cache.posix.shaders", C::ApplicationCaches,
          "Graphics shader cache",
          { "{CACHE}/mesa_shader_cache", "{CACHE}/nvidia/GLCache",
            "{CACHE}/radv_builtin_shaders" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Linux });

    // ===================================================================
    // Browser caches — profile layout differs per platform, so one row each
    // ===================================================================
    Add({ "browser.chromium.linux", C::BrowserCaches,
          "Chrome / Chromium cache",
          { "{CACHE}/google-chrome/*/Cache", "{CACHE}/google-chrome/*/Code Cache",
            "{CACHE}/google-chrome/*/GPUCache",
            "{CACHE}/chromium/*/Cache", "{CACHE}/chromium/*/Code Cache",
            "{CACHE}/chromium/*/GPUCache",
            "{CACHE}/BraveSoftware/Brave-Browser/*/Cache",
            "{CACHE}/microsoft-edge/*/Cache",
            "{CACHE}/vivaldi/*/Cache", "{CACHE}/opera/*/Cache" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Linux });

    Add({ "browser.firefox.linux", C::BrowserCaches,
          "Firefox cache",
          { "{CACHE}/mozilla/firefox/*/cache2" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Linux });

    Add({ "browser.chromium.macos", C::BrowserCaches,
          "Chrome / Chromium cache",
          { "{HOME}/Library/Caches/Google/Chrome/*/Cache",
            "{HOME}/Library/Caches/Google/Chrome/*/Code Cache",
            "{HOME}/Library/Caches/Chromium/*/Cache",
            "{HOME}/Library/Caches/BraveSoftware/Brave-Browser/*/Cache",
            "{HOME}/Library/Caches/Microsoft Edge/*/Cache" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::MacOS });

    Add({ "browser.firefox.macos", C::BrowserCaches,
          "Firefox cache",
          { "{HOME}/Library/Caches/Firefox/Profiles/*/cache2" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::MacOS });

    Add({ "browser.safari.macos", C::BrowserCaches,
          "Safari cache",
          { "{HOME}/Library/Caches/com.apple.Safari" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::MacOS });

    Add({ "browser.chromium.windows", C::BrowserCaches,
          "Chrome / Chromium cache",
          { "{LOCALAPPDATA}/Google/Chrome/User Data/*/Cache",
            "{LOCALAPPDATA}/Google/Chrome/User Data/*/Code Cache",
            "{LOCALAPPDATA}/Google/Chrome/User Data/*/GPUCache",
            "{LOCALAPPDATA}/Chromium/User Data/*/Cache",
            "{LOCALAPPDATA}/BraveSoftware/Brave-Browser/User Data/*/Cache",
            "{LOCALAPPDATA}/Vivaldi/User Data/*/Cache" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Windows });

    Add({ "browser.edge.windows", C::BrowserCaches,
          "Microsoft Edge cache",
          { "{LOCALAPPDATA}/Microsoft/Edge/User Data/*/Cache",
            "{LOCALAPPDATA}/Microsoft/Edge/User Data/*/Code Cache" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Windows });

    Add({ "browser.firefox.windows", C::BrowserCaches,
          "Firefox cache",
          { "{LOCALAPPDATA}/Mozilla/Firefox/Profiles/*/cache2" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Windows });

    // ===================================================================
    // Thumbnails
    // ===================================================================
    Add({ "thumbnails.linux", C::Thumbnails,
          "Thumbnail cache",
          { "{CACHE}/thumbnails" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Linux });

    Add({ "thumbnails.macos", C::Thumbnails,
          "Quick Look thumbnail cache",
          { "{HOME}/Library/Caches/com.apple.QuickLook.thumbnailcache" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::MacOS });

    Add({ "thumbnails.windows", C::Thumbnails,
          "Explorer thumbnail and icon cache",
          { "{LOCALAPPDATA}/Microsoft/Windows/Explorer" },
          M::RemoveMatchingFiles, { "thumbcache_*.db", "iconcache_*.db" }, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Windows });

    // ===================================================================
    // Logs
    // ===================================================================
    Add({ "logs.macos", C::Logs,
          "Application logs",
          { "{HOME}/Library/Logs" },
          M::ClearDirectoryContents, {}, { "DiagnosticReports" },
          /*minAgeDays=*/7, /*maxDepth=*/1, /*safe=*/true, P::MacOS });

    Add({ "logs.linux.state", C::Logs,
          "Application logs",
          { "{STATE}" },
          M::RemoveMatchingFiles, { "*.log", "*.log.old", "*.log.[0-9]" }, {},
          /*minAgeDays=*/7, /*maxDepth=*/3, /*safe=*/true, P::Linux });

    Add({ "logs.linux.session", C::Logs,
          "Session error log",
          { "{HOME}" },
          M::RemoveMatchingFiles, { ".xsession-errors", ".xsession-errors.old" }, {},
          /*minAgeDays=*/1, /*maxDepth=*/1, /*safe=*/true, P::Linux });

    Add({ "logs.windows", C::Logs,
          "Windows component logs (needs administrator)",
          { "{WINDIR}/Logs/CBS", "{WINDIR}/Logs/DISM" },
          M::RemoveMatchingFiles, { "*.log", "*.cab" }, {},
          /*minAgeDays=*/30, /*maxDepth=*/2, /*safe=*/false, P::Windows });

    // ===================================================================
    // Crash reports
    // ===================================================================
    Add({ "crash.macos", C::CrashReports,
          "Diagnostic reports",
          { "{HOME}/Library/Logs/DiagnosticReports",
            "{HOME}/Library/Application Support/CrashReporter" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/1, /*maxDepth=*/1, /*safe=*/true, P::MacOS });

    Add({ "crash.linux.core", C::CrashReports,
          "Core dumps",
          { "{HOME}" },
          M::RemoveMatchingFiles, { "core", "core.[0-9]*" }, {},
          /*minAgeDays=*/0, /*maxDepth=*/2, /*safe=*/true, P::Linux });

    Add({ "crash.linux.apport", C::CrashReports,
          "Crash reports",
          { "{CACHE}/apport" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/1, /*maxDepth=*/1, /*safe=*/true, P::Linux });

    Add({ "crash.windows", C::CrashReports,
          "Crash dumps and error reports",
          { "{LOCALAPPDATA}/CrashDumps",
            "{LOCALAPPDATA}/Microsoft/Windows/WER/ReportArchive",
            "{LOCALAPPDATA}/Microsoft/Windows/WER/ReportQueue" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/1, /*maxDepth=*/1, /*safe=*/true, P::Windows });

    // ===================================================================
    // Package manager caches — re-downloadable, so safe, but the ones that
    // cost a long rebuild or a large download are left unticked.
    // ===================================================================
    Add({ "package.npm.posix", C::PackageCaches,
          "npm package cache",
          { "{HOME}/.npm/_cacache" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Posix });

    Add({ "package.npm.windows", C::PackageCaches,
          "npm package cache",
          { "{LOCALAPPDATA}/npm-cache/_cacache" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Windows });

    Add({ "package.yarn.posix", C::PackageCaches,
          "Yarn package cache",
          { "{CACHE}/yarn", "{HOME}/Library/Caches/Yarn" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Posix });

    Add({ "package.yarn.windows", C::PackageCaches,
          "Yarn package cache",
          { "{LOCALAPPDATA}/Yarn/Cache" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Windows });

    Add({ "package.pip.posix", C::PackageCaches,
          "pip download cache",
          { "{CACHE}/pip", "{HOME}/Library/Caches/pip" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Posix });

    Add({ "package.pip.windows", C::PackageCaches,
          "pip download cache",
          { "{LOCALAPPDATA}/pip/Cache" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Windows });

    Add({ "package.gradle", C::PackageCaches,
          "Gradle build cache",
          { "{HOME}/.gradle/caches/build-cache-1",
            "{HOME}/.gradle/caches/transforms-3",
            "{HOME}/.gradle/caches/transforms-4" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::All });

    Add({ "package.cargo", C::PackageCaches,
          "Cargo registry cache",
          { "{HOME}/.cargo/registry/cache", "{HOME}/.cargo/registry/src" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::All });

    Add({ "package.go", C::PackageCaches,
          "Go module download cache",
          { "{HOME}/go/pkg/mod/cache/download", "{CACHE}/go-build" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/false, P::All });

    Add({ "package.composer", C::PackageCaches,
          "Composer cache",
          { "{CACHE}/composer", "{HOME}/.composer/cache" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::All });

    Add({ "package.maven", C::PackageCaches,
          "Maven repository",
          { "{HOME}/.m2/repository" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/false, P::All });

    Add({ "package.nuget", C::PackageCaches,
          "NuGet package cache",
          { "{HOME}/.nuget/packages" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/false, P::All });

    // ===================================================================
    // Developer leftovers
    // ===================================================================
    Add({ "dev.xcode.derived", C::DeveloperJunk,
          "Xcode derived data",
          { "{HOME}/Library/Developer/Xcode/DerivedData" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::MacOS });

    Add({ "dev.xcode.archives", C::DeveloperJunk,
          "Xcode archives",
          { "{HOME}/Library/Developer/Xcode/Archives" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/30, /*maxDepth=*/1, /*safe=*/false, P::MacOS });

    Add({ "dev.simulator", C::DeveloperJunk,
          "iOS Simulator cache",
          { "{HOME}/Library/Developer/CoreSimulator/Caches" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::MacOS });

    Add({ "dev.jetbrains.posix", C::DeveloperJunk,
          "JetBrains IDE caches",
          { "{CACHE}/JetBrains", "{HOME}/Library/Caches/JetBrains" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::Posix });

    Add({ "dev.jetbrains.windows", C::DeveloperJunk,
          "JetBrains IDE caches",
          { "{LOCALAPPDATA}/JetBrains" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/false, P::Windows });

    Add({ "dev.vscode", C::DeveloperJunk,
          "Visual Studio Code caches",
          { "{DATA}/Code/Cache", "{DATA}/Code/CachedData",
            "{DATA}/Code/CachedExtensionVSIXs",
            "{APPDATA}/Code/Cache", "{APPDATA}/Code/CachedData" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::All });

    // ===================================================================
    // Installers and update payloads
    // ===================================================================
    Add({ "installer.windows.update", C::InstallerLeftovers,
          "Windows Update downloads (needs administrator)",
          { "{WINDIR}/SoftwareDistribution/Download" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/7, /*maxDepth=*/1, /*safe=*/false, P::Windows });

    Add({ "installer.macos.ios", C::InstallerLeftovers,
          "iOS software update downloads",
          { "{HOME}/Library/iTunes/iPhone Software Updates" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/true, P::MacOS });

    Add({ "installer.downloads", C::InstallerLeftovers,
          "Old installer downloads",
          { "{DOWNLOADS}" },
          M::RemoveMatchingFiles,
          { "*.dmg", "*.pkg", "*.msi", "*.exe", "*.appimage", "*.deb", "*.rpm" }, {},
          /*minAgeDays=*/60, /*maxDepth=*/1, /*safe=*/false, P::All });

    // ===================================================================
    // Broken shortcuts
    // ===================================================================
    Add({ "links.home", C::BrokenLinks,
          "Broken symbolic links",
          { "{HOME}/Desktop", "{DOWNLOADS}", "{DATA}/applications" },
          M::RemoveDanglingSymlinks, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/2, /*safe=*/true, P::Posix });

    // ===================================================================
    // Trash — always last, and never pre-ticked: emptying it is permanent.
    // ===================================================================
    Add({ "trash.linux", C::TrashBin,
          "Trash",
          { "{TRASH}/files", "{TRASH}/info" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/false, P::Linux });

    Add({ "trash.macos", C::TrashBin,
          "Trash",
          { "{HOME}/.Trash" },
          M::ClearDirectoryContents, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/false, P::MacOS });

    Add({ "trash.windows", C::TrashBin,
          "Recycle Bin",
          {}, M::WindowsRecycleBin, {}, {},
          /*minAgeDays=*/0, /*maxDepth=*/1, /*safe=*/false, P::Windows });

    return rules;
}

} // namespace

unsigned PlatformBit(CleanerPlatform platform) {
    switch (platform) {
        case CleanerPlatform::Linux:   return PlatformMask::Linux;
        case CleanerPlatform::MacOS:   return PlatformMask::MacOS;
        case CleanerPlatform::Windows: return PlatformMask::Windows;
        default:                       return 0u;
    }
}

const std::vector<CleanRule>& BuiltinRules() {
    static const std::vector<CleanRule> rules = MakeRules();
    return rules;
}

std::vector<CleanRule> RulesForPlatform(CleanerPlatform platform) {
    const unsigned bit = PlatformBit(platform);
    std::vector<CleanRule> selected;
    if (bit == 0u) return selected;
    for (const auto& rule : BuiltinRules()) {
        if (rule.platforms & bit) selected.push_back(rule);
    }
    return selected;
}

std::vector<CleanRule> RulesForCurrentPlatform() {
    return RulesForPlatform(CurrentPlatform());
}

const CleanRule* FindRule(const std::string& id) {
    for (const auto& rule : BuiltinRules()) {
        if (rule.id == id) return &rule;
    }
    return nullptr;
}

} // namespace UltraCleaner
