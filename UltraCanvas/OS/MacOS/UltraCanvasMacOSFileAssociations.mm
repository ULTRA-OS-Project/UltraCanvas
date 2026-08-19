// OS/MacOS/UltraCanvasMacOSFileAssociations.mm
// macOS backend of UltraCanvasFileAssociations — phase 3 placeholder.
// Default open works (/usr/bin/open, Finder double-click semantics) and so
// does launching a user-picked application (`open -a` for .app bundles, a
// direct detached exec otherwise); candidate enumeration via NSWorkspace
// (URLsForApplicationsToOpenURL:) comes with phase P3 of
// UltraCanvasFileAssociationsProposal.md, so ResolveFile currently reports
// no applications and the Filer menu shows only its manual entries and the
// "Other application…" picker here.
// Version: 1.0.0
// Last Modified: 2026-08-16
// Author: UltraCanvas Framework

#include "UltraCanvasFileAssociationsBackend.h"
#include "UltraCanvasUtils.h"   // LaunchDetachedProcess

#include <filesystem>

namespace fs = std::filesystem;

namespace UltraCanvas {
namespace FileAssociationsBackend {

namespace {

    bool IsAppBundle(const std::string& path) {
        return path.size() >= 4 &&
               path.compare(path.size() - 4, 4, ".app") == 0;
    }

} // namespace

bool RefreshGlobalIndex() { return false; }

std::vector<FileAssociationApp> ResolveFile(const std::string&) {
    return {};   // enumeration lands with phase P3 (NSWorkspace)
}

bool LaunchDefault(const std::vector<std::string>& paths, std::string& outError) {
    std::vector<std::string> argv{"open"};
    argv.insert(argv.end(), paths.begin(), paths.end());
    const std::string workingDir = paths.empty()
            ? std::string() : fs::path(paths[0]).parent_path().string();
    return LaunchDetachedProcess(argv, workingDir, outError);
}

bool LaunchWith(const FileAssociationApp& app, const std::vector<std::string>&,
                std::string& outError) {
    outError = "Launching \"" + app.name + "\" is not supported on this "
               "platform yet.";
    return false;
}

bool LaunchWithPath(const std::string& applicationPath,
                    const std::vector<std::string>& paths, std::string& outError) {
    std::vector<std::string> argv;
    if (IsAppBundle(applicationPath)) {
        // `open -a <bundle> <files>` is how Finder hands files to a bundle.
        argv = {"open", "-a", applicationPath};
    } else {
        argv = {applicationPath};
    }
    argv.insert(argv.end(), paths.begin(), paths.end());
    const std::string workingDir = paths.empty()
            ? std::string() : fs::path(paths[0]).parent_path().string();
    return LaunchDetachedProcess(argv, workingDir, outError);
}

FileAssociations::ApplicationFilter GetApplicationFilter() {
    return {"Applications", {"app"}};
}

std::string GetApplicationsDirectory() {
    std::error_code ec;
    return fs::is_directory("/Applications", ec) ? "/Applications" : std::string();
}

} // namespace FileAssociationsBackend
} // namespace UltraCanvas
