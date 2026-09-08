// SmartHome/tests/WidgetTest.cpp
// Drives the ported Smart Home widgets with synthetic UCEvents.
//
// The widget headers were written against a different element API — a
// `UIElement` base, a one-argument Render, and a separate virtual per event
// kind. This proves the port onto the real framework: UltraCanvasUIElement,
// Render(ctx, dirtyRect), and one OnEvent(UCEvent) that serves mouse and touch
// alike, since the framework carries both in the same pointer fields.
//
// Needs the UltraCanvas library, so build it the way the demo is built and add:
//   SmartHome/ui/UltraCanvasSmartHomeDeviceCard.cpp
//   SmartHome/core/UltraCanvasSmartHomeManager.cpp
//   SmartHome/core/SmartHomeProtocolRegistry.cpp
//
// Author: UltraCanvas Framework
#include "UltraCanvasSmartHomePanel.h"
#include <cstdio>

using namespace UltraCanvas;
using namespace UltraCanvas::SmartHome;

static UCEvent Ev(UCEventType t, int x, int y) {
    UCEvent e; e.type = t; e.pointer = Point2Di(x, y); return e;
}

int main() {
    SmartHomeDeviceCard card("card1", 0, 0, 120, 120);

    SmartHomeDeviceInfo info;
    info.DeviceId = "light-1";
    info.Name     = "Hall light";
    info.Category = SmartHomeDeviceCategory::Light;
    info.State    = SmartHomeDeviceState::Online;
    card.SetDevice(info);
    if (card.GetDevice().Name != "Hall light") { std::puts("FAIL: SetDevice"); return 1; }
    std::puts("ok  constructed on UltraCanvasUIElement, SetDevice/GetDevice");

    int clicks = 0;
    card.SetOnClick([&]{ ++clicks; });

    // A press and release inside the card is a click.
    card.OnEvent(Ev(UCEventType::MouseDown, 60, 60));
    card.OnEvent(Ev(UCEventType::MouseUp,   60, 60));
    if (clicks != 1) { std::printf("FAIL: expected 1 click, got %d\n", clicks); return 1; }
    std::puts("ok  OnEvent(MouseDown/MouseUp) -> onClick");

    // Released outside: cancelled, not a click.
    card.OnEvent(Ev(UCEventType::MouseDown, 60, 60));
    card.OnEvent(Ev(UCEventType::MouseUp,   900, 900));
    if (clicks != 1) { std::puts("FAIL: release outside should cancel"); return 1; }
    std::puts("ok  release outside the card cancels");

    // Touch takes the same path - the framework unifies the pointer fields.
    card.OnEvent(Ev(UCEventType::TouchStart, 60, 60));
    card.OnEvent(Ev(UCEventType::TouchEnd,   60, 60));
    if (clicks != 2) { std::puts("FAIL: touch should click too"); return 1; }
    std::puts("ok  TouchStart/TouchEnd take the same path");

    // A press that never landed on the card is ignored.
    if (card.OnEvent(Ev(UCEventType::MouseDown, 900, 900))) {
        std::puts("FAIL: event outside should not be consumed"); return 1;
    }
    std::puts("ok  events outside the card are not consumed");

    SmartHomeSceneCard scene("scene1", 0, 0, 140, 48);
    SmartHomeScene s; s.SceneId = "movie"; s.Name = "Movie Night";
    scene.SetScene(s);
    int activations = 0;
    scene.SetOnActivate([&]{ ++activations; });
    scene.OnEvent(Ev(UCEventType::MouseDown, 20, 20));
    scene.OnEvent(Ev(UCEventType::MouseUp,   20, 20));
    if (activations != 1) { std::puts("FAIL: scene activate"); return 1; }
    std::puts("ok  SmartHomeSceneCard activates");

    std::puts("\nPASS - the ported widget API works against the real framework");
    return 0;
}
