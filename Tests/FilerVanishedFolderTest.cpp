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
//
// Then the same through the folder watch itself, as UltraFiler meets it: a
// subfolder deleted on disk is reported through onFolderChangedOnDisk with
// the folder shown - what UltraFiler's folder tree re-syncs from; until it
// existed, a subfolder that went that way (another program, or a delete here
// whose folder Windows kept listing while a locked file in it was released)
// stayed in the tree - and the shown folder deleted on disk moves the
// display up and reports the folder that went. The watch only runs inside
// the application's timers, so this part needs an application: on Linux a
// display (it skips without one, or fails with
// ULTRACANVAS_TEST_DISPLAY_REQUIRED=1).
// Version: 1.1.0 - the folder watch's news: onFolderChangedOnDisk
// Version: 1.0.0
// Last Modified: 2026-11-05
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasFileRemoval.h"
#include "UltraCanvasFolderWatcher.h"
#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
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

    // The UI thread's timers and posted tasks without the native wait - the
    // folder watch hands its news over on a timer.
    class TestApplication : public UltraCanvasApplication {
    public:
        using UltraCanvasApplicationBase::ProcessPostedTasks;
        using UltraCanvasApplicationBase::ProcessTimers;
        void MarkRunning() { running = true; }
    };

    bool PumpUntil(TestApplication& app, const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            app.ProcessTimers();
            app.ProcessPostedTasks();
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return done();
    }

    void TestFolderWatchNews(const fs::path& root) {
        std::cout << "\n-- Through the folder watch --\n";
        const char* required = std::getenv("ULTRACANVAS_TEST_DISPLAY_REQUIRED");
        const bool displayRequired = required && *required && std::string(required) != "0";
#if !defined(_WIN32) && !defined(_WIN64) && !defined(__APPLE__)
        if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) {
            if (displayRequired) Check(false, "a display was required, and there is none");
            else std::cout << "  [SKIP] no display: the application's timers cannot run\n";
            return;
        }
#endif
        TestApplication app;
        if (!app.Initialize("FilerVanishedFolderTest")) {
            if (displayRequired) Check(false, "the application was required to start, and did not");
            else std::cout << "  [SKIP] the application would not start\n";
            return;
        }
        app.MarkRunning();

        std::error_code ec;
        const fs::path shown = root / "Downloads";
        const fs::path sub = shown / "UCDemo-Windows-0.9.445-x86_64";
        fs::create_directories(sub, ec);
        std::ofstream(sub / "UltraCanvasDemo.exe") << "x";

        auto filer = std::make_shared<UltraCanvasFilerWidget>("watched", 0, 0, 400, 300);
        std::vector<std::string> news;
        filer->onFolderChangedOnDisk = [&news](const std::string& f) { news.push_back(f); };
        filer->SetPath(PathToUtf8(shown));
        // Let anything the listing itself stirred settle before counting.
        const auto settled = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
        while (std::chrono::steady_clock::now() < settled) {
            app.ProcessTimers();
            app.ProcessPostedTasks();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        news.clear();

        // A subfolder goes, by another hand than this display's.
        RemoveFileSystemTree(sub, ec);
        Check(!fs::exists(sub, ec), "setup: the subfolder is deleted on disk");
        Check(PumpUntil(app, [&]() { return !news.empty(); }),
              "the folder watch reports the shown folder changed on disk");
        Check(!news.empty() && SameFolder(news.front(), shown),
              "and names the folder shown - what a folder tree re-syncs");
        bool stillListed = false;
        for (const FilerEntry& e : filer->GetEntries())
            stillListed = stillListed || e.name == "UCDemo-Windows-0.9.445-x86_64";
        Check(!stillListed, "the display no longer lists the subfolder");

        // The shown folder itself goes.
        news.clear();
        {
            UltraCanvasFolderWatchHold hold({PathToUtf8(shown)});
            RemoveFileSystemTree(shown, ec);
        }
        Check(PumpUntil(app, [&]() { return SameFolder(filer->GetPath(), root); }),
              "the shown folder deleted on disk: the display moves up");
        Check(PumpUntil(app, [&]() {
                  for (const std::string& f : news) if (SameFolder(f, shown)) return true;
                  return false;
              }),
              "and the folder that went is reported, for the tree to drop");
        filer.reset();
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
    TestFolderWatchNews(root);
    RemoveFileSystemTree(root, ec);
    std::cout << "\n" << (g_failures ? "FAILED: " : "OK: ") << g_failures << " failure(s)\n";
    return g_failures == 0 ? 0 : 1;
}
