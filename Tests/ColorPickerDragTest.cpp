// Tests/ColorPickerDragTest.cpp
// Dragging the colour picker's markers.
//
// The marker of the saturation/value area is centred on the colour's pixel,
// so at an edge or corner most of it lies outside the area - and only the
// area was hit-tested, so a press on the visible marker there did nothing.
// A press on the marker also jumped the colour to the pointer instead of
// grabbing the marker where it was hit. And the picker never captured the
// mouse: once a drag left the picker, the moves (and the release) went to
// whatever was underneath, so the marker stopped short of the edge it was
// dragged past and the drag carried on afterwards with no button down.
//
// Every event goes through the application's own dispatch, as the backend
// delivers it, at window coordinates. The area's geometry is not public, so
// the test measures it from two presses.
//
// Opens a real window, so it runs under Xvfb (xvfb-run -a) and skips itself
// without a DISPLAY.
// Version: 1.0.0
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

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

// Where the picker sits in the window.
constexpr float PickerX = 20.0f, PickerY = 20.0f, PickerW = 290.0f;

void Send(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
          UCEventType type, float localX, float localY,
          UCMouseButton button = UCMouseButton::Left) {
    UCEvent e;
    e.type = type;
    e.button = (type == UCEventType::MouseMove) ? UCMouseButton::NoneButton : button;
    e.targetWindow = window;
    e.nativeWindowHandle = window->GetNativeHandle();
    const Point2Di p(static_cast<int>(std::lround(PickerX + localX)),
                     static_cast<int>(std::lround(PickerY + localY)));
    e.pointerWindow = p;
    e.pointer = p;
    app.DispatchEvent(e);
}

bool Near(float a, float b, float tolerance = 1e-3f) { return std::fabs(a - b) <= tolerance; }

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Colour Picker Drag Suite"              << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("ColorPickerDragTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "ColorPickerDragTest";
    cfg.width = 340;
    cfg.height = 560;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();   // UpdateAndRender() is a no-op on an unmapped window
    DisplayTest::ActivateWindow(app, window);

    // Sized so the hue ring takes the full inner width: its centre, which is
    // the SV square's, is then (W / 2, W / 2) - the padding on either side.
    // Created at that height: the window's layout keeps the size an element
    // was created with, so a later SetSize() would not hold.
    const float pickerH = CreateColorPicker("probe", 0, 0, PickerW, 470)
                              ->PreferredHeightForWidth(PickerW);
    auto picker = CreateColorPicker("picker", Color(0x85, 0xFF, 0xFB, 0xFF), PickerX, PickerY,
                                    PickerW, pickerH);
    picker->SetBackgroundColor(Color(255, 0, 0, 255));
    window->AddChild(picker);
    DisplayTest::Frame(window, {picker});

    int finished = 0;
    picker->onColorChanged = [&finished](const Color&) { ++finished; };

    auto click = [&](float x, float y) {
        Send(app, window, UCEventType::MouseDown, x, y);
        Send(app, window, UCEventType::MouseUp, x, y);
    };

    // ----- Measure the SV area: two presses away from the marker -----
    const float cx = PickerW * 0.5f, cy = PickerW * 0.5f;
    picker->SetHSV(178.0f, 1.0f, 1.0f, false);         // marker in the top-right corner
    click(cx, cy);
    const float s0 = picker->GetSaturation(), v0 = picker->GetValue();
    click(cx + 24.0f, cy + 24.0f);                       // 34 px from the marker at (cx, cy)
    const float s1 = picker->GetSaturation(), v1 = picker->GetValue();
    TEST("a press inside the SV square picks saturation and value",
         s0 > 0.3f && s0 < 0.7f && v0 > 0.3f && v0 < 0.7f && s1 > s0 && v1 < v0);
    if (!(s1 > s0 && v1 < v0)) {
        std::cerr << "Cannot measure the SV area; stopping." << std::endl;
        return 1;
    }
    const float spanX = 24.0f / (s1 - s0);              // width - 1
    const float spanY = 24.0f / (v0 - v1);              // height - 1
    const float left = cx - s0 * spanX;
    const float top = cy - (1.0f - v0) * spanY;
    auto markerAt = [&](float s, float v) {
        return Point2Df(left + s * spanX, top + (1.0f - v) * spanY);
    };

    // ----- The marker is grabbed where it overhangs the area -----
    // The screenshot's case: black, marker in the bottom-left corner, three
    // quarters of it outside the square.
    picker->SetHSV(178.0f, 0.0f, 0.0f, false);
    Point2Df m = markerAt(0.0f, 0.0f);
    Send(app, window, UCEventType::MouseDown, m.x - 5.0f, m.y + 5.0f);
    TEST("a press on the marker's overhang outside the square starts a drag",
         app.GetCapturedElement() == picker.get());
    TEST("grabbing the marker does not change the colour",
         Near(picker->GetSaturation(), 0.0f) && Near(picker->GetValue(), 0.0f));
    Send(app, window, UCEventType::MouseMove, m.x - 5.0f + 30.0f, m.y + 5.0f - 30.0f);
    TEST("the grabbed marker follows the pointer",
         Near(picker->GetSaturation(), 30.0f / spanX, 0.01f) &&
         Near(picker->GetValue(), 30.0f / spanY, 0.01f));
    Send(app, window, UCEventType::MouseUp, m.x - 5.0f + 30.0f, m.y + 5.0f - 30.0f);
    TEST("the release ends the drag and gives up the mouse",
         app.GetCapturedElement() == nullptr);

    // ----- A press on the marker inside the area keeps the colour -----
    picker->SetHSV(178.0f, 0.5f, 0.5f, false);
    m = markerAt(0.5f, 0.5f);
    Send(app, window, UCEventType::MouseDown, m.x + 3.0f, m.y - 2.0f);
    TEST("a press a few pixels off the marker's centre does not move it",
         Near(picker->GetSaturation(), 0.5f) && Near(picker->GetValue(), 0.5f));
    Send(app, window, UCEventType::MouseUp, m.x + 3.0f, m.y - 2.0f);

    // ----- A press beside the area, away from the marker, picks the edge -----
    picker->SetHSV(178.0f, 0.5f, 0.5f, false);
    click(left - 4.0f, top + spanY * 0.25f);
    TEST("a press just outside the square's edge picks that edge",
         Near(picker->GetSaturation(), 0.0f) && Near(picker->GetValue(), 0.75f, 0.01f));

    // ----- A drag past the picker keeps following the pointer -----
    picker->SetHSV(178.0f, 1.0f, 1.0f, false);
    const float rowY = top + spanY * 0.5f;
    Send(app, window, UCEventType::MouseDown, cx, rowY);
    Send(app, window, UCEventType::MouseMove, -12.0f, rowY);    // left of the picker
    TEST("a drag past the picker's left edge reaches saturation 0",
         Near(picker->GetSaturation(), 0.0f));
    Send(app, window, UCEventType::MouseMove, PickerW + 15.0f, rowY);  // right of it
    TEST("a drag past the picker's right edge reaches saturation 1",
         Near(picker->GetSaturation(), 1.0f));

    // ----- A release outside the picker ends the drag -----
    finished = 0;
    Send(app, window, UCEventType::MouseUp, -12.0f, rowY);
    TEST("a release outside the picker reaches it", finished == 1);
    const float sAfter = picker->GetSaturation(), vAfter = picker->GetValue();
    Send(app, window, UCEventType::MouseMove, cx + 10.0f, cy + 30.0f);
    TEST("after that release, moving over the square no longer drags",
         Near(picker->GetSaturation(), sAfter) && Near(picker->GetValue(), vAfter));

    // ----- The hue ring keeps its drag outside the picker too -----
    picker->SetHSV(178.0f, 1.0f, 1.0f, false);
    const float ringMid = (cx - 10.0f) - 13.0f;        // outer radius minus half the ring
    Send(app, window, UCEventType::MouseDown, cx - ringMid, cy);
    TEST("a press on the ring's left picks red", Near(picker->GetHue(), 0.0f, 1.0f) ||
                                                 Near(picker->GetHue(), 360.0f, 1.0f));
    // Up and to the left of the centre, outside the picker: 45 degrees.
    Send(app, window, UCEventType::MouseMove, -15.0f, cy - (cx + 15.0f));
    TEST("a ring drag outside the picker still turns the hue",
         Near(picker->GetHue(), 45.0f, 1.0f));
    Send(app, window, UCEventType::MouseUp, -15.0f, cy - (cx + 15.0f));

    // ----- An Adjust (right) press edits the background, never grabbing -----
    // The marker on screen is the foreground's; the background's is not drawn.
    picker->SetHSV(178.0f, 0.5f, 0.5f, false);
    m = markerAt(0.5f, 0.5f);
    Send(app, window, UCEventType::MouseDown, m.x + 6.0f, m.y, UCMouseButton::Right);
    Send(app, window, UCEventType::MouseUp, m.x + 6.0f, m.y, UCMouseButton::Right);
    float bh, bs, bv;
    RGBToHSV(picker->GetBackgroundColor(), bh, bs, bv);
    TEST("a right press beside the marker sets the background to that point",
         Near(bs, 0.5f + 6.0f / spanX, 0.01f) && Near(bv, 0.5f, 0.01f));
    TEST("the foreground is left as it was",
         Near(picker->GetSaturation(), 0.5f) && Near(picker->GetValue(), 0.5f));

    std::cerr << "========================================" << std::endl;
    std::cerr << "Tests: " << testCount << ", failed: " << failCount << std::endl;
    return failCount == 0 ? 0 : 1;
}
