// core/UltraCanvasCloudStorage.cpp
// Cloud-storage folder discovery (OneDrive / Google Drive / Dropbox / iCloud)
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework
//
// Deliberately NOT part of UltraCanvasUtils.cpp: reading the Dropbox info.json
// pulls in UltraCanvasJSON, and Utils sits at the bottom of the stack - several
// standalone test targets compile it directly (for Trim()) without linking the
// framework library, and a JSON dependency there leaves every one of them with
// undefined references.

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <winreg.h>    // RegGetValueW (the Google Drive mount point)
#else
#include <sys/types.h>
#include <unistd.h>    // getuid (the GVFS mount directory)
#endif

#include "UltraCanvasCloudStorage.h"
#include "UltraCanvasUtils.h"           // PathFromUtf8 / Utf8ToWide / WideToUtf8
#include "DataFormats/UltraCanvasJSON.h"  // the Dropbox info.json
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace UltraCanvas {

    namespace {
        // The provider's own name, used where the folder on disk carries no
        // usable one of its own (a Google Drive letter, a GVFS mount).
        const char* CloudStorageDisplayName(CloudStorageKind kind) {
            switch (kind) {
                case CloudStorageKind::OneDrive:     return "OneDrive";
                case CloudStorageKind::GoogleDrive:  return "Google Drive";
                case CloudStorageKind::Dropbox:      return "Dropbox";
                case CloudStorageKind::ICloudDrive:  return "iCloud Drive";
            }
            return "Cloud Storage";
        }

        // Appends `p` when it is an existing directory that is not listed
        // already (the same folder is often found twice - an environment
        // variable and the default location below the profile, say). An empty
        // `label` means "use the folder's own name".
        void AddCloudFolder(std::vector<CloudStorageInfo>& out, CloudStorageKind kind,
                            const std::filesystem::path& p,
                            const std::string& label = std::string()) {
            std::error_code ec;
            if (p.empty() || !std::filesystem::is_directory(p, ec) || ec) return;
            const std::string path = p.string();
            for (const CloudStorageInfo& c : out)
                if (c.path == path) return;
            std::string name = label.empty() ? p.filename().string() : label;
            if (name.empty()) name = CloudStorageDisplayName(kind);
            out.push_back({kind, path, name});
        }

        // An environment variable as a path. Read wide on Windows: a profile
        // path holding characters outside the system code page
        // ("C:\Users\Ελένη\OneDrive") comes back mangled through the narrow
        // CRT, and a mangled path simply fails the is_directory() test.
        std::filesystem::path CloudEnvPath(const char* name) {
#if defined(_WIN32) || defined(_WIN64)
            const wchar_t* value = ::_wgetenv(Utf8ToWide(name).c_str());
            return value ? std::filesystem::path(value) : std::filesystem::path();
#else
            const char* value = std::getenv(name);
            return value ? std::filesystem::path(value) : std::filesystem::path();
#endif
        }

        // The account folders recorded in a Dropbox info.json:
        //   {"personal": {"path": "C:\\Users\\me\\Dropbox", ...}, "business": {…}}
        // The client rewrites this file whenever an account is linked or the
        // folder is moved, so it is the only place a relocated - or a second,
        // business - Dropbox folder can be found.
        void AddDropboxFromInfoJson(std::vector<CloudStorageInfo>& out,
                                    const std::filesystem::path& infoJson) {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(infoJson, ec) || ec) return;
            JSONParseResult result;
            const JSONValue root = JSON::ParseFile(infoJson.string(), &result);
            if (!result.success || !root.IsObject()) return;
            for (const JSONValue::Member& account : root.GetMembers()) {
                const std::string path = account.second.Get("path").GetString();
                if (path.empty()) continue;
                // "personal" stays plain "Dropbox"; a second account is named
                // after its section so the two are told apart - but only when
                // its folder is not already named for it, which is what the
                // client does by default ("Dropbox (Acme Inc)").
                std::string label = PathFromUtf8(path).filename().string();
                if (label.empty()) label = CloudStorageDisplayName(CloudStorageKind::Dropbox);
                if (!account.first.empty() && account.first != "personal" &&
                    label == CloudStorageDisplayName(CloudStorageKind::Dropbox)) {
                    std::string suffix = account.first;
                    suffix[0] = static_cast<char>(std::toupper(
                            static_cast<unsigned char>(suffix[0])));
                    label += " (" + suffix + ")";
                }
                AddCloudFolder(out, CloudStorageKind::Dropbox, PathFromUtf8(path), label);
            }
        }

#if defined(_WIN32) || defined(_WIN64)
        // Where Google Drive for desktop mounted, as its installer recorded
        // it. The value is usually a bare drive letter ("G:"), which needs the
        // separator appended to name the root rather than the process's
        // current directory on that drive.
        std::filesystem::path GoogleDriveMountFromRegistry() {
            wchar_t buffer[512] = {};
            DWORD size = sizeof(buffer);
            if (::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Google\\DriveFS",
                               L"DefaultMountPoint", RRF_RT_REG_SZ, nullptr,
                               buffer, &size) != ERROR_SUCCESS)
                return {};
            std::wstring value(buffer);
            if (value.empty()) return {};
            if (value.size() == 2 && value[1] == L':') value += L'\\';
            return std::filesystem::path(value);
        }

        // A default Google Drive install mounts a virtual drive labelled
        // "Google Drive" instead of putting a folder in the profile. Only the
        // letters that are actually mounted are looked at, and only the fixed
        // ones - GetLogicalDrives() and GetDriveTypeW() both answer from the
        // mount table, so no optical drive is spun up and no disconnected
        // network mapping is waited out.
        void AddGoogleDriveVolumes(std::vector<CloudStorageInfo>& out) {
            const DWORD mask = ::GetLogicalDrives();
            for (int i = 0; i < 26; ++i) {
                if (!(mask & (DWORD(1) << i))) continue;
                const wchar_t root[4] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', L'\0'};
                if (::GetDriveTypeW(root) != DRIVE_FIXED) continue;
                wchar_t volume[MAX_PATH + 1] = {};
                if (!::GetVolumeInformationW(root, volume, MAX_PATH + 1, nullptr,
                                             nullptr, nullptr, nullptr, 0))
                    continue;
                const std::string label = WideToUtf8(volume);
                if (label.rfind("Google Drive", 0) != 0) continue;
                AddCloudFolder(out, CloudStorageKind::GoogleDrive, root, label);
            }
        }
#endif
    }

    std::vector<CloudStorageInfo> GetCloudStorageFolders() {
        std::vector<CloudStorageInfo> folders;

#if defined(_WIN32) || defined(_WIN64)
        // OneDrive publishes its roots as environment variables: OneDrive is
        // the account currently signed in, OneDriveConsumer the personal one
        // and OneDriveCommercial the work/school tenant. The profile default
        // covers a setup old enough not to set them.
        for (const char* var : {"OneDrive", "OneDriveConsumer", "OneDriveCommercial"})
            AddCloudFolder(folders, CloudStorageKind::OneDrive, CloudEnvPath(var));
        const std::filesystem::path profile = CloudEnvPath("USERPROFILE");
        if (!profile.empty())
            AddCloudFolder(folders, CloudStorageKind::OneDrive, profile / "OneDrive");

        AddCloudFolder(folders, CloudStorageKind::GoogleDrive,
                       GoogleDriveMountFromRegistry(),
                       CloudStorageDisplayName(CloudStorageKind::GoogleDrive));
        AddGoogleDriveVolumes(folders);
        if (!profile.empty()) {
            // "Google Drive" is the classic Backup-and-Sync folder, "My Drive"
            // the folder-mode layout of Drive for desktop.
            AddCloudFolder(folders, CloudStorageKind::GoogleDrive, profile / "Google Drive");
            AddCloudFolder(folders, CloudStorageKind::GoogleDrive, profile / "My Drive");
        }

        // The client writes info.json below LOCALAPPDATA; older versions used
        // the roaming APPDATA, and both are read so a long-lived profile that
        // still carries only the old one is found.
        for (const char* var : {"LOCALAPPDATA", "APPDATA"}) {
            const std::filesystem::path appData = CloudEnvPath(var);
            if (!appData.empty())
                AddDropboxFromInfoJson(folders, appData / "Dropbox" / "info.json");
        }
        if (!profile.empty())
            AddCloudFolder(folders, CloudStorageKind::Dropbox, profile / "Dropbox");

        // iCloud for Windows mirrors iCloud Drive into the profile.
        if (!profile.empty())
            AddCloudFolder(folders, CloudStorageKind::ICloudDrive, profile / "iCloudDrive",
                           CloudStorageDisplayName(CloudStorageKind::ICloudDrive));
#else
        const char* homeEnv = std::getenv("HOME");
        const std::string home = homeEnv ? std::string(homeEnv) : std::string();
        if (home.empty()) return folders;
        const std::filesystem::path homePath(home);
#if defined(__APPLE__)
        // macOS 12+ gives every File Provider extension its own folder under
        // ~/Library/CloudStorage, named "<Provider>-<account>": "OneDrive-
        // Personal", "OneDrive-Contoso", "GoogleDrive-me@gmail.com",
        // "Dropbox". This is the only place a modern install appears, and it
        // is what Finder's sidebar lists.
        {
            std::error_code ec;
            const std::filesystem::path cloudStorage = homePath / "Library" / "CloudStorage";
            for (std::filesystem::directory_iterator it(cloudStorage, ec), end;
                 it != end && !ec; it.increment(ec)) {
                const std::string name = it->path().filename().string();
                CloudStorageKind kind;
                if (name.rfind("OneDrive", 0) == 0)          kind = CloudStorageKind::OneDrive;
                else if (name.rfind("GoogleDrive", 0) == 0)  kind = CloudStorageKind::GoogleDrive;
                else if (name.rfind("Dropbox", 0) == 0)      kind = CloudStorageKind::Dropbox;
                else if (name.rfind("iCloudDrive", 0) == 0)  kind = CloudStorageKind::ICloudDrive;
                else continue;
                // "GoogleDrive-me@gmail.com" reads as "Google Drive - me@gmail.com".
                std::string label = CloudStorageDisplayName(kind);
                const size_t dash = name.find('-');
                if (dash != std::string::npos && dash + 1 < name.size())
                    label += " - " + name.substr(dash + 1);
                AddCloudFolder(folders, kind, it->path(), label);
            }
        }
        // Pre-CloudStorage installs, still in place on older systems.
        AddCloudFolder(folders, CloudStorageKind::OneDrive, homePath / "OneDrive");
        AddCloudFolder(folders, CloudStorageKind::GoogleDrive, homePath / "Google Drive");
        AddCloudFolder(folders, CloudStorageKind::GoogleDrive, "/Volumes/GoogleDrive");
        AddCloudFolder(folders, CloudStorageKind::Dropbox, homePath / "Dropbox");
        AddDropboxFromInfoJson(folders, homePath / ".dropbox" / "info.json");
        AddCloudFolder(folders, CloudStorageKind::ICloudDrive,
                       homePath / "Library" / "Mobile Documents" / "com~apple~CloudDocs",
                       CloudStorageDisplayName(CloudStorageKind::ICloudDrive));
#else
        // GNOME Online Accounts mounts a cloud drive through GVFS, one
        // directory per account below $XDG_RUNTIME_DIR/gvfs named after the
        // backend ("google-drive:host=gmail.com,user=me"). Listing that one
        // directory is a FUSE round-trip to the already-running gvfs daemon;
        // it does not reach the network.
        {
            std::filesystem::path gvfs;
            if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"))
                gvfs = std::filesystem::path(runtime) / "gvfs";
            else
                gvfs = std::filesystem::path("/run/user") /
                       std::to_string(static_cast<unsigned long>(::getuid())) / "gvfs";
            std::error_code ec;
            for (std::filesystem::directory_iterator it(gvfs, ec), end;
                 it != end && !ec; it.increment(ec)) {
                const std::string name = it->path().filename().string();
                CloudStorageKind kind;
                if (name.rfind("google-drive:", 0) == 0)  kind = CloudStorageKind::GoogleDrive;
                else if (name.rfind("onedrive:", 0) == 0) kind = CloudStorageKind::OneDrive;
                else if (name.rfind("dropbox:", 0) == 0)  kind = CloudStorageKind::Dropbox;
                else continue;
                // The account is in the mount name: "…,user=me" -> "… (me)".
                std::string label = CloudStorageDisplayName(kind);
                const size_t userPos = name.find("user=");
                if (userPos != std::string::npos) {
                    std::string user = name.substr(userPos + 5);
                    const size_t comma = user.find(',');
                    if (comma != std::string::npos) user.resize(comma);
                    if (!user.empty()) label += " (" + user + ")";
                }
                AddCloudFolder(folders, kind, it->path(), label);
            }
        }
        // The defaults of the sync clients that run natively on Linux.
        AddCloudFolder(folders, CloudStorageKind::OneDrive, homePath / "OneDrive");
        for (const char* name : {"GoogleDrive", "Google Drive", "google-drive"})
            AddCloudFolder(folders, CloudStorageKind::GoogleDrive, homePath / name,
                           CloudStorageDisplayName(CloudStorageKind::GoogleDrive));
        AddCloudFolder(folders, CloudStorageKind::Dropbox, homePath / "Dropbox");
        AddDropboxFromInfoJson(folders, homePath / ".dropbox" / "info.json");
#endif
#endif

        // One canonical order everywhere, whatever order the probes ran in;
        // stable, so two accounts of the same provider keep the order they
        // were discovered in.
        std::stable_sort(folders.begin(), folders.end(),
                         [](const CloudStorageInfo& a, const CloudStorageInfo& b) {
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        });
        return folders;
    }

} // namespace UltraCanvas
