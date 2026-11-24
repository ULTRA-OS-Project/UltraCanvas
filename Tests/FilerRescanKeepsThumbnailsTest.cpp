// Tests/FilerRescanKeepsThumbnailsTest.cpp
// A rescan of the folder already shown - a file deleted, a file changed, the
// folder watch noticing either - keeps the thumbnails of the files that did
// not change. It used to drop them all: deleting one file redrew the whole
// folder with type glyphs, and the thumbnails came back a moment later.
//
//   * deleted: every other file's thumbnail is still held right after the
//     rescan, none is decoded again;
//   * changed (new content, new time): only that file is decoded again, in
//     the background, and its slot is finished again afterwards;
//   * another folder: everything is let go, as before.
//
// Runs on a display (Xvfb in CI); skips without one.
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasWindow.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

int Skip(const std::string& why) {
    if (DisplayTest::DisplayRequired()) {
        std::cout << "FAIL: a display was required, and " << why << "\n";
        return 1;
    }
    std::cout << "SKIP: " << why << "\n";
    return 0;
}

// Draws the window until `done` holds or ten seconds have passed: each frame
// asks the workers for the tiles it shows, and they finish in the background.
bool RenderUntil(UltraCanvasWindow& window, const std::function<bool()>& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        window.UpdateAndRender();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    return done();
}

} // namespace

int main() {
    std::cout << "FilerRescanKeepsThumbnailsTest\n";
    if (!std::getenv("DISPLAY")) return Skip("there is no DISPLAY");
    UltraCanvasApplication app;
    if (!app.Initialize("FilerRescanKeepsThumbnailsTest")) return Skip("the application would not initialise");
    WindowConfig cfg;
    cfg.title = "FilerRescanKeepsThumbnailsTest";
    cfg.width = 900;
    cfg.height = 600;
    auto window = CreateWindow(cfg);
    if (!window) return Skip("the window could not be created");
    window->Show();

    // Six pictures, and a seventh, different one to change a file with.
    const fs::path images = PathFromUtf8(UC_MEDIA_DIR) / "images";
    const fs::path picture = images / "3d-boxes.png";
    const fs::path other = images / "OCR-demo.png";
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) / "FilerRescanKeepsThumbnailsTest";
    fs::remove_all(root, ec);
    fs::create_directories(root / "folder", ec);
    fs::create_directories(root / "elsewhere", ec);
    const int count = 6;
    for (int i = 0; i < count; ++i)
        fs::copy_file(picture, root / "folder" / ("picture" + std::to_string(i) + ".png"), ec);
    fs::copy_file(picture, root / "elsewhere" / "picture.png", ec);

    // Made here, not read from the user's disk cache, and not written to it.
    UltraCanvasFilerWidget::SetThumbnailDiskCacheEnabled(false);
    auto filer = std::make_shared<UltraCanvasFilerWidget>("filer", 0, 0, 900, 600);
    filer->SetFolderWatchEnabled(false);
    filer->SetHoverIconMenuEnabled(false);
    filer->SetViewType(FilerViewType::ThumbnailsMedium);
    window->AddChild(filer);
    filer->SetPath(PathToUtf8(root / "folder"));

    auto held = [&] { return filer->GetThumbnailCacheStats().entries; };
    auto pending = [&] {
        const auto s = filer->GetThumbnailCacheStats();
        return s.pendingEntries + s.inFlightEntries;
    };

    std::cout << "\nThe folder's thumbnails\n";
    Check(RenderUntil(*window, [&] { return held() == static_cast<size_t>(count); }),
          "all " + std::to_string(count) + " thumbnails are made");

    std::cout << "\nOne file deleted\n";
    fs::remove(root / "folder" / "picture2.png", ec);
    filer->Refresh();
    Check(held() == static_cast<size_t>(count - 1),
          "right after the rescan the other " + std::to_string(count - 1) +
          " thumbnails are still held (was: none)");
    Check(pending() == 0, "and none of them is made again");

    std::cout << "\nOne file changed\n";
    const fs::path changed = root / "folder" / "picture4.png";
    fs::copy_file(other, changed, fs::copy_options::overwrite_existing, ec);
    fs::last_write_time(changed, fs::last_write_time(changed, ec) + std::chrono::seconds(10), ec);
    filer->Refresh();
    Check(held() == static_cast<size_t>(count - 2),
          "the changed file's thumbnail is made again, the others are kept");
    window->UpdateAndRender();
    Check(RenderUntil(*window, [&] { return held() == static_cast<size_t>(count - 1) && pending() == 0; }),
          "and the new one is in after a moment");

    std::cout << "\nAnother folder\n";
    filer->SetPath(PathToUtf8(root / "elsewhere"));
    Check(held() == 0, "the thumbnails of the folder left are let go");

    window->Close();
    fs::remove_all(root, ec);
    std::cout << (g_failures ? "\nFAILED: " + std::to_string(g_failures) + "\n" : "\nAll passed\n");
    return g_failures ? 1 : 0;
}
