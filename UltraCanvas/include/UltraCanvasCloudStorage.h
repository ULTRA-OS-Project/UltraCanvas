// include/UltraCanvasCloudStorage.h
// Cloud-storage folder discovery (OneDrive / Google Drive / Dropbox / iCloud)
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework
//
// Separate from UltraCanvasUtils.h (where the companion
// GetWellKnownUserFolders() lives) because the implementation reads the
// Dropbox configuration through UltraCanvasJSON, and Utils is compiled
// standalone - without the framework library - by several test targets.

#pragma once

#include <string>
#include <vector>

namespace UltraCanvas {

    // The cloud-storage folders that are actually present on this machine —
    // OneDrive (the personal account and every business tenant), Google
    // Drive, Dropbox (personal and business) and iCloud Drive — for the
    // "Cloud Storage" section of a file manager's places list.
    //
    // Each provider is asked where it put its folder rather than guessed at:
    // the OneDrive environment variables, the Google Drive mount recorded in
    // the registry (a virtual drive letter as much as a folder) and the
    // Dropbox info.json on Windows; the per-provider folders macOS 12+ keeps
    // under ~/Library/CloudStorage; the sync-client defaults and the GVFS
    // mount table (GNOME Online Accounts) on Linux. Only folders that exist
    // right now are returned, each once, in the canonical OneDrive, Google
    // Drive, Dropbox, iCloud Drive order. Nothing is mounted, signed in to or
    // contacted: a client that is installed but signed out simply has no
    // folder and is not listed.
    enum class CloudStorageKind { OneDrive, GoogleDrive, Dropbox, ICloudDrive };
    struct CloudStorageInfo {
        CloudStorageKind kind;
        std::string path;    // absolute path of an existing directory
        std::string label;   // display name ("OneDrive - Contoso", "iCloud Drive")
    };
    std::vector<CloudStorageInfo> GetCloudStorageFolders();

} // namespace UltraCanvas
