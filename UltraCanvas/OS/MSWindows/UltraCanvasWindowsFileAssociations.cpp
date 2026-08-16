// OS/MSWindows/UltraCanvasWindowsFileAssociations.cpp
// Windows backend of UltraCanvasFileAssociations — phase 2 placeholder.
// Default open works (ShellExecuteEx "open" verb, per file, like a
// double-click in Explorer) and so does launching a user-picked executable;
// candidate enumeration (SHAssocEnumHandlers) and the native SHOpenWithDialog
// come with phase P2 of UltraCanvasFileAssociationsProposal.md, so
// ResolveFile currently reports no applications and the Filer menu shows
// only its manual entries and the "Other application…" picker here.
// Version: 1.0.0
// Last Modified: 2026-08-16
// Author: UltraCanvas Framework

#include "UltraCanvasFileAssociationsBackend.h"
#include "UltraCanvasUtils.h"   // Utf8ToWide, LaunchDetachedProcess

#include <windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <filesystem>

namespace UltraCanvas {
namespace FileAssociationsBackend {

namespace {

    bool ShellOpen(const std::wstring& file, const std::wstring& directory,
                   std::string& outError, const std::string& errorSubject) {
        SHELLEXECUTEINFOW info = {};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        info.lpVerb = L"open";
        info.lpFile = file.c_str();
        info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
        info.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&info)) return true;
        if (!outError.empty()) outError += "\n";
        outError += "Could not open \"" + errorSubject + "\".";
        return false;
    }

} // namespace

bool RefreshGlobalIndex() { return false; }

std::vector<FileAssociationApp> ResolveFile(const std::string&) {
    return {};   // enumeration lands with phase P2 (SHAssocEnumHandlers)
}

bool LaunchDefault(const std::vector<std::string>& paths, std::string& outError) {
    bool allOk = true;
    for (const std::string& path : paths) {
        const std::wstring dirW =
                Utf8ToWide(PathToUtf8(PathFromUtf8(path).parent_path()));
        if (!ShellOpen(Utf8ToWide(path), dirW, outError, path)) allOk = false;
    }
    return allOk;
}

bool LaunchWith(const FileAssociationApp& app, const std::vector<std::string>&,
                std::string& outError) {
    outError = "Launching \"" + app.name + "\" is not supported on this "
               "platform yet.";
    return false;
}

bool LaunchWithPath(const std::string& applicationPath,
                    const std::vector<std::string>& paths, std::string& outError) {
    std::vector<std::string> argv{applicationPath};
    argv.insert(argv.end(), paths.begin(), paths.end());
    const std::string workingDir = paths.empty()
            ? std::string()
            : PathToUtf8(PathFromUtf8(paths[0]).parent_path());
    return LaunchDetachedProcess(argv, workingDir, outError);
}

FileAssociations::ApplicationFilter GetApplicationFilter() {
    return {"Applications", {"exe", "com", "bat", "cmd"}};
}

std::string GetApplicationsDirectory() {
    const char* programFiles = std::getenv("ProgramFiles");
    std::error_code ec;
    if (programFiles && std::filesystem::is_directory(programFiles, ec) && !ec)
        return programFiles;
    return {};
}

} // namespace FileAssociationsBackend
} // namespace UltraCanvas
