// Tests/TimecodeFieldElementTest.cpp
// UltraCanvasTimecodeField in a real window: typed digits filling from the
// right, commit on Enter, an entry that is not a timecode, Esc restoring the
// value, relative entries, stepping a segment with the arrow keys and the
// wheel, Tab between segments, the maximum, a click selecting a segment, the
// accessible value text, and the readout variant. Needs a display; skips
// itself without one (runs under Xvfb).
// Version: 1.0.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasTimecodeField.h"
#include "UltraCanvasWindow.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

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

UCEvent KeyEvent(UCKeys key, char character = 0, bool ctrl = false, bool shift = false) {
    UCEvent event;
    event.type = UCEventType::KeyDown;
    event.virtualKey = key;
    event.character = character;
    event.ctrl = ctrl;
    event.shift = shift;
    return event;
}

UCEvent Typed(char character) { return KeyEvent(UCKeys::Unknown, character); }

// `x`, `y` in window coordinates; the field sits at (10, 10). The dispatcher
// hands an element its pointer in element-local coordinates, so the helper
// maps the way the window would.
UCEvent MouseEvent(UCEventType type, float x, float y) {
    UCEvent event;
    event.type = type;
    event.button = UCMouseButton::Left;
    event.pointerWindow = Point2Di(static_cast<int>(x), static_cast<int>(y));
    event.pointer = Point2Di(static_cast<int>(x) - 10, static_cast<int>(y) - 10);
    event.pointerGlobal = event.pointerWindow;
    return event;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   TimecodeField Element Suite"          << std::endl;
    std::cerr << "========================================" << std::endl;
    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");
    UltraCanvasApplication app;
    if (!app.Initialize("TimecodeFieldElementTest")) SKIP_ALL("application would not initialise");
    WindowConfig cfg;
    cfg.title = "TimecodeFieldElementTest";
    cfg.width = 400;
    cfg.height = 120;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    auto field = std::make_shared<UltraCanvasTimecodeField>("Timecode", 10, 10, 140, 28);
    window->AddChild(field);
    field->SetFrameRate(25.0);
    std::vector<long long> committed;
    field->onCommit = [&](long long frames) { committed.push_back(frames); };
    DisplayTest::Frame(window, {field});

    std::cerr << "\n--- Value ---" << std::endl;
    TEST("starts at zero", field->GetFrames() == 0 && field->GetText() == "00:00:00:00");
    field->SetFrames(17 * 25 + 12);
    TEST("frames show as timecode", field->GetText() == "00:00:17:12");
    field->SetSeconds(1.5);
    TEST("seconds round to the frame grid", field->GetFrames() == 38 && std::abs(field->GetSeconds() - 1.52) < 1e-9);
    field->SetFrames(0);

    if (!DisplayTest::FocusElement(app, window, field)) SKIP_ALL("the window could not be focused");

    std::cerr << "\n--- Typing ---" << std::endl;
    for (char c : std::string("1712")) field->OnEvent(Typed(c));
    TEST("digits fill from the right", field->IsEditing() && field->GetText() == "00:00:17:12");
    TEST("nothing committed yet", committed.empty() && field->GetFrames() == 0);
    field->OnEvent(KeyEvent(UCKeys::Return));
    TEST("Enter commits", !field->IsEditing() && field->GetFrames() == 17 * 25 + 12 && committed.size() == 1 && committed[0] == 437);
    for (char c : std::string("7500")) field->OnEvent(Typed(c));
    TEST("75 seconds is invalid while typing", field->IsEditing() && field->IsInvalid() && field->GetText() == "00:00:75:00");
    field->OnEvent(KeyEvent(UCKeys::Return));
    TEST("Enter does not commit an invalid entry", field->IsEditing() && field->IsInvalid() && field->GetFrames() == 437);
    field->OnEvent(KeyEvent(UCKeys::Escape));
    TEST("Esc restores the last valid value", !field->IsEditing() && !field->IsInvalid() && field->GetText() == "00:00:17:12");
    for (char c : std::string("30")) field->OnEvent(Typed(c));
    TEST("frame 30 at 25 fps is invalid", field->IsInvalid());
    field->OnEvent(KeyEvent(UCKeys::Backspace));
    TEST("Backspace removes the last digit and clears the fault", !field->IsInvalid() && field->GetText() == "00:00:00:03");
    field->OnEvent(KeyEvent(UCKeys::Escape));
    for (char c : std::string("123456789")) field->OnEvent(Typed(c));
    TEST("the ninth digit pushes the first out", field->GetText() == "23:45:67:89" || field->GetText() == "23:45:67:89");
    field->OnEvent(KeyEvent(UCKeys::Escape));

    std::cerr << "\n--- Relative entries ---" << std::endl;
    field->OnEvent(Typed('+'));
    for (char c : std::string("12")) field->OnEvent(Typed(c));
    TEST("a plus entry shows its sign", field->GetText() == "+00:00:00:12");
    field->OnEvent(KeyEvent(UCKeys::Return));
    TEST("+12 is twelve frames later", field->GetFrames() == 449 && committed.back() == 449);
    field->OnEvent(Typed('-'));
    for (char c : std::string("100")) field->OnEvent(Typed(c));
    field->OnEvent(KeyEvent(UCKeys::Return));
    TEST("-1:00 is a second earlier", field->GetFrames() == 424);
    field->OnEvent(Typed('-'));
    for (char c : std::string("5000")) field->OnEvent(Typed(c));
    TEST("an offset below zero is invalid", field->IsInvalid());
    field->OnEvent(KeyEvent(UCKeys::Escape));

    std::cerr << "\n--- Segments ---" << std::endl;
    field->SelectSegment(3);
    field->OnEvent(KeyEvent(UCKeys::Up));
    TEST("Up steps the frames segment", field->GetFrames() == 425 && committed.back() == 425);
    field->OnEvent(KeyEvent(UCKeys::Tab));
    TEST("Tab wraps to the hours segment", field->GetSelectedSegment() == 0);
    field->OnEvent(KeyEvent(UCKeys::Tab, 0, false, true));
    TEST("Shift+Tab goes back", field->GetSelectedSegment() == 3);
    field->OnEvent(KeyEvent(UCKeys::Left));
    field->OnEvent(KeyEvent(UCKeys::Down));
    TEST("Down on seconds steps a second", field->GetSelectedSegment() == 2 && field->GetFrames() == 400);
    field->OnEvent(KeyEvent(UCKeys::Left));
    field->OnEvent(KeyEvent(UCKeys::Up));
    TEST("Up on minutes steps a minute", field->GetFrames() == 400 + 60 * 25);
    field->SetFrames(0);
    field->SelectSegment(3);
    field->OnEvent(KeyEvent(UCKeys::Down));
    TEST("no step below zero", field->GetFrames() == 0);
    field->SetMaximumFrames(30);
    field->SetFrames(29);
    field->OnEvent(KeyEvent(UCKeys::Up));
    field->OnEvent(KeyEvent(UCKeys::Up));
    TEST("the maximum stops a step", field->GetFrames() == 30);
    for (char c : std::string("45")) field->OnEvent(Typed(c));
    TEST("an entry past the maximum is invalid", field->IsInvalid());
    field->OnEvent(KeyEvent(UCKeys::Escape));
    field->SetMaximumFrames(0);
    UCEvent wheel = MouseEvent(UCEventType::MouseWheel, 120.0f, 24.0f);
    wheel.wheelDelta = 1;
    field->OnEvent(wheel);
    TEST("the wheel steps the selected segment", field->GetFrames() == 31);

    std::cerr << "\n--- Mouse and accessibility ---" << std::endl;
    DisplayTest::Frame(window, {field});
    field->OnEvent(MouseEvent(UCEventType::MouseDown, 10.0f + 140.0f - 20.0f, 24.0f));
    TEST("a click near the right end selects the frames segment", field->GetSelectedSegment() == 3);
    field->OnEvent(MouseEvent(UCEventType::MouseDown, 10.0f + 30.0f, 24.0f));
    TEST("a click near the left end selects the hours segment", field->GetSelectedSegment() == 0);
    TEST("the accessible value is the timecode", field->GetAccessibleValueText() == field->GetText());
    TEST("a timecode can be set as text", field->SetAccessibleValueText("1:00") && field->GetFrames() == 25);
    TEST("a word cannot", !field->SetAccessibleValueText("later"));

    std::cerr << "\n--- Readout ---" << std::endl;
    auto readout = std::make_shared<UltraCanvasTimecodeField>("Readout", 160, 10, 200, 40);
    readout->SetReadout(true);
    readout->SetFrameRate(25.0);
    readout->SetSeconds(72.32);
    window->AddChild(readout);
    DisplayTest::Frame(window, {field, readout});
    TEST("the readout shows its time", readout->GetText() == "00:01:12:08");
    TEST("and takes no focus", !readout->AcceptsFocus());
    readout->OnEvent(Typed('1'));
    TEST("nor input", !readout->IsEditing());

    // the window as a PPM for a human to look at: the field mid-entry, the readout
    if (const char* dir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR")) {
        DisplayTest::FocusElement(app, window, field);
        field->SetFrames(437);
        field->OnEvent(Typed('+'));
        field->OnEvent(Typed('1'));
        field->OnEvent(Typed('2'));
        auto selected = std::make_shared<UltraCanvasTimecodeField>("Selected", 10, 60, 140, 28);
        selected->SetFrameRate(25.0);
        selected->SetFrames(4 * 25 + 12);
        selected->SelectSegment(3);
        window->AddChild(selected);
        auto invalid = std::make_shared<UltraCanvasTimecodeField>("Invalid", 160, 60, 140, 28);
        invalid->SetFrameRate(25.0);
        window->AddChild(invalid);
        DisplayTest::Frame(window, {field, readout, selected, invalid});
        DisplayTest::FocusElement(app, window, invalid);
        for (char c : std::string("9412")) invalid->OnEvent(Typed(c));
        DisplayTest::Frame(window, {field, readout, selected, invalid});
        const std::string path = std::string(dir) + "/timecode-field.ppm";
        std::ofstream out(path, std::ios::binary);
        out << "P6\n" << cfg.width << " " << cfg.height << "\n255\n";
        for (int y = 0; y < cfg.height; ++y) {
            for (int x = 0; x < cfg.width; ++x) {
                Color px;
                if (!window->GetPixelColor(x, y, px)) px = Colors::Black;
                out.put(static_cast<char>(px.r)).put(static_cast<char>(px.g)).put(static_cast<char>(px.b));
            }
        }
        std::cerr << "wrote " << path << std::endl;
    }

    std::cerr << "\n" << testCount - failCount << " of " << testCount << " checks passed" << std::endl;
    return failCount ? 1 : 0;
}
