// Tests/FilerPaneDragTest.cpp
// Files dragged from one file display to another display of the same window -
// UltraFiler's split view - land where they were dropped.
//
// A drag that leaves the display it started in keeps going over the rest of
// the window, and a release over another element hands the files to it. Over
// a second file display that hand-over used to ignore where the pointer was:
// files released on a folder tile were copied into the folder the display
// showed, not into the folder under the pointer, and the display gave no sign
// of where they would go while they were dragged over it. Dragged across from
// the right-hand pane onto a folder of the left-hand one, three screenshots
// looked as if nothing had happened - they had been copied, out of sight,
// into the wrong folder, without the question a move asks first.
//
// Every press, move and release here goes through the application's own
// dispatch, as the backend delivers them. Where an entry sits is found by
// clicking across the display and reading the selection back, so the test
// does not depend on the layout of a view.
//
// Opens a real window, so it runs under Xvfb (xvfb-run -a) and skips itself
// without a DISPLAY.
// Version: 1.0.0
// Last Modified: 2026-10-10
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;
namespace fs = std::filesystem;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

// The UI thread's work without the native wait: a file operation finishes on
// a timer, and RunOnce() would block on the display once no timer is left.
// Marked running as Run() would mark it - ProcessEvents() discards the queue
// of an application that is not, and a drop inside the window is queued.
class TestApplication : public UltraCanvasApplication {
public:
    using UltraCanvasApplicationBase::ProcessPostedTasks;
    using UltraCanvasApplicationBase::ProcessTimers;
    void MarkRunning() { running = true; }
};

void Send(TestApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
          UCEventType type, const Point2Di& p, bool ctrl = false) {
    UCEvent e;
    e.type = type;
    e.button = UCMouseButton::Left;
    e.ctrl = ctrl;
    e.targetWindow = window;
    e.nativeWindowHandle = window->GetNativeHandle();
    e.pointerWindow = p;
    e.pointer = p;
    app.DispatchEvent(e);
}

// Runs the queued events, timers and posted tasks until `done` says so or two
// seconds have passed.
bool PumpUntil(TestApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
               const std::function<bool()>& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        app.ProcessEvents();
        app.ProcessTimers();
        app.ProcessPostedTasks();
        window->UpdateAndRender();
        if (done()) return true;
    }
    return done();
}

void Frame(const std::shared_ptr<UltraCanvasWindow>& window,
           const std::shared_ptr<UltraCanvasUIElement>& root) {
    for (int i = 0; i < 2; ++i) {
        root->RequestRedraw();
        window->UpdateAndRender();
    }
}

// Where `name` sits in `filer`, in window coordinates: the middle of the
// points whose click selects it. (-1, -1) when no click does.
Point2Di FindEntry(TestApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
                   UltraCanvasFilerWidget& filer, const std::string& name) {
    const Rect2Df b = filer.GetBoundsInWindow();
    int minX = 1 << 20, minY = 1 << 20, maxX = -1, maxY = -1;
    for (int y = static_cast<int>(b.y) + 4; y < static_cast<int>(b.y + b.height) - 4; y += 6) {
        for (int x = static_cast<int>(b.x) + 4; x < static_cast<int>(b.x + b.width) - 4; x += 6) {
            filer.ClearSelection();
            Send(app, window, UCEventType::MouseDown, Point2Di(x, y));
            Send(app, window, UCEventType::MouseUp, Point2Di(x, y));
            const std::vector<FilerEntry> picked = filer.GetSelectedEntries();
            if (picked.size() == 1 && picked.front().name == name) {
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
        }
    }
    filer.ClearSelection();
    if (maxX < 0) return Point2Di(-1, -1);
    return Point2Di((minX + maxX) / 2, (minY + maxY) / 2);
}

// A point of `filer` that no entry covers: the bottom-right corner, inside
// the scroll area.
Point2Di EmptySpot(const UltraCanvasFilerWidget& filer) {
    const Rect2Df b = filer.GetBoundsInWindow();
    return Point2Di(static_cast<int>(b.x + b.width) - 40, static_cast<int>(b.y + b.height) - 40);
}

// Picks `from` up and carries it to `to` in a few steps, without releasing.
void DragTo(TestApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
            const Point2Di& from, const Point2Di& to) {
    Send(app, window, UCEventType::MouseDown, from);
    Send(app, window, UCEventType::MouseMove, Point2Di(from.x - 12, from.y));   // past the slop
    for (int step = 1; step <= 4; ++step) {
        Send(app, window, UCEventType::MouseMove,
             Point2Di(from.x + (to.x - from.x) * step / 4, from.y + (to.y - from.y) * step / 4));
    }
}

// Answers the open drop question with the button labelled `label`.
bool Answer(const std::string& label) {
    auto dialog = UltraCanvasDialogManager::GetCurrentModalDialog();
    if (!dialog) return false;
    auto* button = dynamic_cast<UltraCanvasButton*>(
            dialog->FindChildById("DialogBtn_Custom_" + label));
    if (!button || !button->onClick) return false;
    button->onClick();
    return true;
}

bool Exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

size_t DiskCount(const fs::path& folder) {
    std::error_code ec;
    size_t n = 0;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) ++n;
    return n;
}

void Touch(const fs::path& p) {
    std::ofstream(p) << p.filename().string();   // path-string-ok: ASCII test name
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Filer Pane Drag Suite"                 << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    TestApplication app;
    if (!app.Initialize("FilerPaneDragTest")) SKIP_ALL("application would not initialise");
    app.MarkRunning();

    WindowConfig cfg;
    cfg.title = "FilerPaneDragTest";
    cfg.width = 900;
    cfg.height = 540;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();   // UpdateAndRender() is a no-op on an unmapped window

    // left/  Target/  Other/          right/  a.txt  b.txt  c.txt  d.txt  e.txt
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) / "FilerPaneDragTest";
    fs::remove_all(root, ec);
    const fs::path left = root / "left";
    const fs::path right = root / "right";
    fs::create_directories(left / "Target", ec);
    fs::create_directories(left / "Other", ec);
    fs::create_directories(right, ec);
    for (const char* name : {"a.txt", "b.txt", "c.txt", "d.txt", "e.txt"}) Touch(right / name);

    auto outer = CreateContainer("outer", 0, 0, 900, 540);
    window->AddChild(outer);
    auto leftFiler = std::make_shared<UltraCanvasFilerWidget>("left", 10, 10, 430, 400);
    auto rightFiler = std::make_shared<UltraCanvasFilerWidget>("right", 460, 10, 430, 400);
    // Below both: an element that is not a file display, recording the drag
    // events it is handed.
    std::vector<UCEventType> sinkEvents;
    std::vector<std::string> sinkDropped;
    auto sink = CreateContainer("sink", 10, 430, 880, 100);
    sink->SetEventCallback([&sinkEvents, &sinkDropped](const UCEvent& e) {
        if (!e.IsDragEvent()) return false;
        sinkEvents.push_back(e.type);
        if (e.type == UCEventType::Drop) sinkDropped = e.droppedFiles;
        return true;
    });
    for (auto* filer : {leftFiler.get(), rightFiler.get()}) {
        filer->SetHoverIconMenuEnabled(false);
        filer->SetFolderWatchEnabled(false);
    }
    // What UltraFiler does with a changed folder: the other display re-reads
    // it when it shows it (a move empties the folder it came from, and only
    // the display that ran the paste knows). Each report is recorded - the
    // folder a paste went into is reported once the paste is through.
    std::vector<std::string> reported;
    leftFiler->onFolderModified = [&reported, peer = rightFiler.get()](const std::string& folder) {
        reported.push_back(folder);
        if (peer->GetPath() == folder) peer->Refresh();
    };
    rightFiler->onFolderModified = [&reported, peer = leftFiler.get()](const std::string& folder) {
        reported.push_back(folder);
        if (peer->GetPath() == folder) peer->Refresh();
    };
    // Waits for the paste into `folder` to be through, and for both displays
    // to list what their folders now hold, so the next drag picks up an
    // entry where it really is.
    auto waitForPaste = [&](const fs::path& folder) {
        const std::string wanted = PathToUtf8(folder);
        PumpUntil(app, window, [&]() {
            return std::find(reported.begin(), reported.end(), wanted) != reported.end() &&
                   leftFiler->GetEntries().size() == DiskCount(left) &&
                   rightFiler->GetEntries().size() == DiskCount(right);
        });
        reported.clear();
        Frame(window, outer);
    };
    outer->AddChild(leftFiler);
    outer->AddChild(rightFiler);
    outer->AddChild(sink);
    leftFiler->SetPath(PathToUtf8(left));
    rightFiler->SetPath(PathToUtf8(right));
    Frame(window, outer);
    DisplayTest::ActivateWindow(app, window);

    // Looked up again before each drag: a drop changes a folder's date, and
    // with it where the folder sorts.
    Point2Di target = FindEntry(app, window, *leftFiler, "Target");
    Point2Di other = FindEntry(app, window, *leftFiler, "Other");
    TEST("the left display shows its two folders", target.x >= 0 && other.x >= 0);
    if (target.x < 0 || other.x < 0) {
        fs::remove_all(root, ec);
        std::cerr << std::endl << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
        return 1;
    }

    // ----- A file dropped on a folder of the other display goes into it -----
    Point2Di a = FindEntry(app, window, *rightFiler, "a.txt");
    TEST("the right display shows a.txt", a.x >= 0);
    DragTo(app, window, a, target);
    TEST("over a folder of the other display, that folder is where the drop goes",
         leftFiler->GetIncomingDropFolder() == PathToUtf8(left / "Target"));
    Send(app, window, UCEventType::MouseMove, EmptySpot(*leftFiler));
    TEST("over its empty space, the folder it shows is",
         leftFiler->GetIncomingDropFolder() == PathToUtf8(left));
    Send(app, window, UCEventType::MouseMove, target);
    Send(app, window, UCEventType::MouseUp, target);
    TEST("the release takes the other display's drop state away",
         leftFiler->GetIncomingDropFolder().empty());
    waitForPaste(left / "Target");
    TEST("a file released on a folder of the other display lands in that folder",
         Exists(left / "Target" / "a.txt"));
    TEST("and not in the folder that display shows", !Exists(left / "a.txt"));
    TEST("a plain drop moves it, as a drop on a folder of its own display does",
         !Exists(right / "a.txt"));

    // ----- Dropped on empty space: into the folder the display shows -----
    Point2Di b = FindEntry(app, window, *rightFiler, "b.txt");
    DragTo(app, window, b, EmptySpot(*leftFiler));
    Send(app, window, UCEventType::MouseUp, EmptySpot(*leftFiler));
    waitForPaste(left);
    TEST("a file released on the other display's empty space lands in its folder",
         Exists(left / "b.txt") && !Exists(right / "b.txt"));

    // ----- Ctrl at the release copies -----
    other = FindEntry(app, window, *leftFiler, "Other");
    Point2Di c = FindEntry(app, window, *rightFiler, "c.txt");
    DragTo(app, window, c, other);
    Send(app, window, UCEventType::MouseUp, other, /*ctrl=*/true);
    waitForPaste(left / "Other");
    TEST("Ctrl at the release copies into the folder under the pointer",
         Exists(left / "Other" / "c.txt") && Exists(right / "c.txt"));

    // ----- The question a move asks is asked across the split too -----
    // UltraFiler asks before a dropped move by default (Settings > Handling
    // > File operations); the display the files land in asks it.
    UltraCanvasDialogManager::SetUseNativeDialogs(false);
    leftFiler->SetDropConfirmation(FilerDropConfirmation::MoveOnly);
    target = FindEntry(app, window, *leftFiler, "Target");
    Point2Di e = FindEntry(app, window, *rightFiler, "e.txt");
    DragTo(app, window, e, target);
    Send(app, window, UCEventType::MouseUp, target);
    PumpUntil(app, window, [&]() { return UltraCanvasDialogManager::GetCurrentModalDialog() != nullptr; });
    auto question = UltraCanvasDialogManager::GetCurrentModalDialog();
    // The facts under the question name where the file is going.
    TEST("a move onto a folder of the other display asks first",
         question && question->GetDetails().find(PathToUtf8(left / "Target")) != std::string::npos);
    TEST("nothing moves while the question is open",
         Exists(right / "e.txt") && !Exists(left / "Target" / "e.txt"));
    TEST("Cancel is offered", Answer("Cancel"));
    PumpUntil(app, window, [&]() { return UltraCanvasDialogManager::GetCurrentModalDialog() == nullptr; });
    TEST("Cancel leaves the file where it was",
         Exists(right / "e.txt") && !Exists(left / "Target" / "e.txt"));

    Frame(window, outer);
    target = FindEntry(app, window, *leftFiler, "Target");
    e = FindEntry(app, window, *rightFiler, "e.txt");
    DragTo(app, window, e, target);
    Send(app, window, UCEventType::MouseUp, target);
    PumpUntil(app, window, [&]() { return UltraCanvasDialogManager::GetCurrentModalDialog() != nullptr; });
    TEST("Move is offered", Answer("Move"));
    waitForPaste(left / "Target");
    TEST("answered with Move, the file moves into the folder it was dropped on",
         Exists(left / "Target" / "e.txt") && !Exists(right / "e.txt"));
    leftFiler->SetDropConfirmation(FilerDropConfirmation::NeverConfirm);

    // ----- Leaving the other display takes its drop state away -----
    target = FindEntry(app, window, *leftFiler, "Target");
    Point2Di d = FindEntry(app, window, *rightFiler, "d.txt");
    DragTo(app, window, d, target);
    TEST("the folder under the pointer is the drop target again",
         leftFiler->GetIncomingDropFolder() == PathToUtf8(left / "Target"));
    const Rect2Df sinkBounds = sink->GetBoundsInWindow();
    const Point2Di overSink(static_cast<int>(sinkBounds.x + 100),
                            static_cast<int>(sinkBounds.y + sinkBounds.height / 2));
    Send(app, window, UCEventType::MouseMove, overSink);
    TEST("moving on to another element leaves the display without a drop target",
         leftFiler->GetIncomingDropFolder().empty());

    // ----- Any other element hears about the drag as it passes -----
    TEST("an element the drag passes over is told it entered and is passed over",
         std::find(sinkEvents.begin(), sinkEvents.end(), UCEventType::DragEnter) != sinkEvents.end() &&
         std::find(sinkEvents.begin(), sinkEvents.end(), UCEventType::DragOver) != sinkEvents.end());
    Send(app, window, UCEventType::MouseMove, target);
    TEST("and that it left",
         !sinkEvents.empty() && sinkEvents.back() == UCEventType::DragLeave);
    sinkEvents.clear();
    Send(app, window, UCEventType::MouseMove, overSink);
    Send(app, window, UCEventType::MouseUp, overSink);
    PumpUntil(app, window, [&]() { return !sinkDropped.empty(); });
    TEST("released over it, it is handed the files as a drop",
         sinkDropped.size() == 1 && sinkDropped.front() == PathToUtf8(right / "d.txt"));
    TEST("the file itself stays where it was", Exists(right / "d.txt"));

    fs::remove_all(root, ec);
    std::cerr << std::endl << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
