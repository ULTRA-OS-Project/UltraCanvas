// Tests/FilerRetireTest.cpp
// Retiring a folder display (UltraCanvasFilerWidget::RequestBackgroundStop /
// BackgroundStopped): what UltraFiler does with a closed tab, so that a worker
// busy with a slow file cannot freeze the window while the tab closes. The
// display is told to stop, kept, and destroyed once every worker thread it
// started has returned - its destructor's waits are then instant.
//
// Guarded here: a display that started workers (a folder with subfolders
// starts the prefetch) reports them gone after the request, without anyone
// joining them; and the destruction that follows does not wait.
// Version: 1.0.0
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework

#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// Polls until `done` says yes or `limit` has passed.
template <typename F>
bool WaitFor(F done, std::chrono::milliseconds limit) {
    const auto until = std::chrono::steady_clock::now() + limit;
    while (!done()) {
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

} // namespace

int main() {
    std::cout << "-- A display that has started nothing --\n";
    {
        auto filer = std::make_shared<UltraCanvasFilerWidget>("retire-idle", 0, 0, 400, 300);
        Check(filer->BackgroundStopped(), "has no worker to wait for");
        filer->RequestBackgroundStop();
        Check(filer->BackgroundStopped(), "and still none after the request");
    }

    std::cout << "\n-- A display on a folder with subfolders --\n";
    const fs::path dir = fs::temp_directory_path() / "uc-filer-retire-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    for (int i = 0; i < 24; ++i) {
        const fs::path sub = dir / ("folder " + std::to_string(i));
        fs::create_directories(sub, ec);
        std::ofstream(sub / "file.txt") << "x";
    }
    {
        auto filer = std::make_shared<UltraCanvasFilerWidget>("retire-busy", 0, 0, 400, 300);
        filer->SetFolderPrefetchEnabled(true);
        filer->SetPath(PathToUtf8(dir));
        Check(!filer->GetEntries().empty(), "the folder is listed");

        filer->RequestBackgroundStop();
        Check(WaitFor([&]() { return filer->BackgroundStopped(); },
                      std::chrono::seconds(10)),
              "every worker has returned after the request, unjoined");

        const auto started = std::chrono::steady_clock::now();
        filer.reset();
        const auto took = std::chrono::steady_clock::now() - started;
        Check(took < std::chrono::seconds(1), "and destroying it then does not wait");
    }
    fs::remove_all(dir, ec);

    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : "FAILURES: ")
              << (g_failures == 0 ? std::string() : std::to_string(g_failures)) << "\n";
    return g_failures == 0 ? 0 : 1;
}
