// Tests/FilerVanishedFolderTest.cpp
// A folder display whose folder is deleted from under it - from the other
// pane of a split view, or by another program - moves up to the nearest
// folder above it that is still there, instead of listing a folder that no
// longer exists. Refresh() is what the folder watch calls when it notices,
// so that is what is called here.
//
// Guarded: one folder gone, and several levels at once (to the nearest that
// is left); onPathChanged hears the move; a folder still there, a path that
// never was a folder, and a file list are left where they are. The scratch
// folder has a Thai name, so on Windows every step goes through UTF-16.
// (A volume that goes - a stick pulled out - is waited for instead; that
// needs a device to unplug and is not reproduced here.)
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework

#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasFileRemoval.h"
#include "UltraCanvasFolderWatcher.h"
#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

    int g_failures = 0;

    void Check(bool condition, const std::string& what) {
        std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
        if (!condition) ++g_failures;
    }

    // Deletes `folder` the way the Filer's own delete does: watches held off
    // it first (the display's own watch included), then removed.
    void DeleteFolder(const fs::path& folder) {
        UltraCanvasFolderWatchHold hold({PathToUtf8(folder)});
        std::error_code ec;
        RemoveFileSystemTree(folder, ec);
    }

    bool SameFolder(const std::string& a, const fs::path& b) {
        return PathFromUtf8(a).lexically_normal() == b.lexically_normal();
    }

} // namespace

int main() {
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() /
            PathFromUtf8("uc-filer-vanished \xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2");
    RemoveFileSystemTree(root, ec);
    const fs::path lei = root / "Lei";
    const fs::path album = lei / "Album";
    const fs::path year = album / "2024";
    fs::create_directories(year, ec);
    std::ofstream(lei / "keep.txt") << "x";
    std::ofstream(year / "photo.jpg") << "x";

    auto filer = std::make_shared<UltraCanvasFilerWidget>("vanished", 0, 0, 400, 300);
    std::vector<std::string> reported;
    filer->onPathChanged = [&reported](const std::string& p) { reported.push_back(p); };

    std::cout << "-- The folder shown is deleted --\n";
    filer->SetPath(PathToUtf8(album));
    reported.clear();
    DeleteFolder(album);
    Check(!fs::exists(album, ec), "setup: the folder is gone");
    filer->Refresh();
    Check(SameFolder(filer->GetPath(), lei), "the display moves up to the folder above it");
    Check(reported.size() == 1 && SameFolder(reported.back(), lei),
          "and onPathChanged reports the move");
    bool listsKeep = false;
    for (const FilerEntry& e : filer->GetEntries()) listsKeep = listsKeep || e.name == "keep.txt";
    Check(listsKeep, "and lists that folder");

    std::cout << "\n-- Several levels go at once --\n";
    fs::create_directories(year, ec);
    filer->SetPath(PathToUtf8(year));
    DeleteFolder(lei);
    filer->Refresh();
    Check(SameFolder(filer->GetPath(), root),
          "the display moves up to the nearest folder still there");

    std::cout << "\n-- What is left alone --\n";
    fs::create_directories(album, ec);
    filer->SetPath(PathToUtf8(album));
    reported.clear();
    filer->Refresh();
    Check(SameFolder(filer->GetPath(), album) && reported.empty(),
          "a folder still there is refreshed, not left");

    const fs::path never = root / "never-existed" / "deeper";
    filer->SetPath(PathToUtf8(never));
    reported.clear();
    filer->Refresh();
    Check(SameFolder(filer->GetPath(), never) && reported.empty(),
          "a path that never listed as a folder is not \"moved up\" from");

    filer->SetPath(PathToUtf8(album));
    filer->ShowFileList({PathToUtf8(lei / "keep.txt")});
    DeleteFolder(album);
    reported.clear();
    filer->Refresh();
    Check(reported.empty(), "a file list does not move anywhere");

    filer.reset();
    RemoveFileSystemTree(root, ec);
    std::cout << "\n" << (g_failures ? "FAILED: " : "OK: ") << g_failures << " failure(s)\n";
    return g_failures == 0 ? 0 : 1;
}
