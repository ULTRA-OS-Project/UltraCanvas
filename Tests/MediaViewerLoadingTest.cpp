// Tests/MediaViewerLoadingTest.cpp
// A media viewer told to open a file once its window shows
// (OpenFileWhenShown) says at once that a file is on its way: a turning ring
// where it will show, its name and "loading" in the info bar, the name
// reported for the window title - never "No media", which means nothing was
// asked for. The file is opened after a frame of that has been drawn, and the
// loading state goes. A list with nothing to show ends it too, and a viewer
// asked for nothing still says "No media".
// Runs headless under Xvfb; skips when there is no display.
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
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

// ProcessTimers without the native wait RunOnce() would block on.
class TestApplication : public UltraCanvasApplication {
public:
    using UltraCanvasApplicationBase::ProcessTimers;
};

// Runs the timers until `done` holds or a second has passed.
bool PumpUntil(TestApplication& app, const std::function<bool()>& done) {
    for (int i = 0; i < 100; ++i) {
        if (done()) return true;
        app.ProcessTimers();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done();
}

std::string InfoText(UltraCanvasMediaViewer& viewer) {
    auto* label = dynamic_cast<UltraCanvasLabel*>(viewer.FindChildById("MV_InfoLabel"));
    return label ? label->GetText() : std::string();
}

bool RingShown(UltraCanvasMediaViewer& viewer) {
    auto* ring = viewer.FindChildById("MV_Loading");
    return ring && ring->IsVisible();
}

} // namespace

int main() {
    std::cout << "MediaViewerLoadingTest\n";
    if (!std::getenv("DISPLAY")) {
        std::cout << "SKIP: no DISPLAY\n";
        return 0;
    }
    TestApplication app;
    if (!app.Initialize("MediaViewerLoadingTest")) {
        std::cout << "SKIP: application would not initialise\n";
        return 0;
    }
    WindowConfig cfg;
    cfg.title = "MediaViewerLoadingTest";
    cfg.width = 800;
    cfg.height = 600;
    auto window = CreateWindow(cfg);
    if (!window) {
        std::cout << "SKIP: window could not be created\n";
        return 0;
    }
    auto viewer = CreateMediaViewer("Viewer", 0, 0, 800, 600);
    window->AddChild(viewer);
    std::vector<std::string> reported;
    viewer->onCurrentFileChanged = [&reported](const std::string& path) { reported.push_back(path); };
    window->Show();

    const fs::path media = PathFromUtf8(UC_MEDIA_DIR);
    const std::string gif = PathToUtf8(media / "images" / "dice.gif");

    std::cout << "\nNothing asked for\n";
    window->UpdateAndRender();
    Check(InfoText(*viewer) == "No media", "an empty viewer says No media");
    Check(!RingShown(*viewer), "and turns no ring");

    std::cout << "\nOpenFileWhenShown\n";
    viewer->OpenFileWhenShown(gif);
    Check(viewer->IsLoading() && viewer->GetLoadingPath() == gif, "the viewer is loading the file");
    Check(viewer->GetCount() == 0, "which is not opened yet");
    Check(InfoText(*viewer).find("dice.gif") == 0 &&
          InfoText(*viewer).find("loading") != std::string::npos,
          "the info bar names it, loading - not No media");
    Check(RingShown(*viewer), "a ring turns where it will show");
    Check(!reported.empty() && reported.back() == gif, "the window title is told its name");

    window->UpdateAndRender();   // a frame of the loading state
    {
        auto* ring = viewer->FindChildById("MV_Loading");
        auto* surface = viewer->FindChildById("MV_Surface");
        const Rect2Df r = ring ? ring->GetBounds() : Rect2Df();
        const Rect2Df s = surface ? surface->GetBounds() : Rect2Df();
        const float dx = (r.x + r.width / 2) - (s.x + s.width / 2);
        const float dy = (r.y + r.height / 2) - (s.y + s.height / 2);
        Check(ring && surface && r.width > 20 && s.width > 100 &&
              std::abs(dx) <= 1.0f && std::abs(dy) <= 1.0f,
              "the ring sits in the middle of the display");
    }
    const bool opened = PumpUntil(app, [&] { return !viewer->IsLoading(); });
    Check(opened && viewer->GetCurrentPath() == gif, "after a frame the file is opened");
    Check(!RingShown(*viewer), "and the ring is gone");
    Check(InfoText(*viewer).find("dice.gif") == 0 &&
          InfoText(*viewer).find("loading") == std::string::npos,
          "the info bar shows the file");

    std::cout << "\nNothing to show in the list\n";
    viewer->SetFilesWhenShown({ PathToUtf8(media / "no-such-file.xyz") });
    Check(viewer->IsLoading(), "loading");
    window->UpdateAndRender();
    PumpUntil(app, [&] { return !viewer->IsLoading(); });
    Check(!viewer->IsLoading() && !RingShown(*viewer), "a list with nothing to show ends the loading");
    Check(InfoText(*viewer) == "No media", "and the viewer says No media");

    std::cout << "\nReplaced before it opens\n";
    viewer->OpenFileWhenShown(gif);
    viewer->CloseFile();
    Check(!viewer->IsLoading() && !RingShown(*viewer), "CloseFile ends the loading");
    window->UpdateAndRender();
    PumpUntil(app, [] { return false; });   // whatever was scheduled runs, and must open nothing
    Check(viewer->GetCount() == 0, "and what was on its way is not opened after all");
    Check(!reported.empty() && reported.back().empty(), "the title is told nothing is shown");

    window->Close();
    std::cout << (g_failures ? "\nFAILED: " + std::to_string(g_failures) + "\n" : "\nAll passed\n");
    return g_failures ? 1 : 0;
}
