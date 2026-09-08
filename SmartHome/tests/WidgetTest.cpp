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

    // ----- the dashboard itself -----
    SmartHomePanel panel("panel", 0, 0, 800, 600);
    if (!panel.Initialize()) { std::puts("FAIL: panel Initialize"); return 1; }
    std::puts("ok  SmartHomePanel::Initialize() through the facade");

    SmartHomePanelMode seen = SmartHomePanelMode::DeviceGrid;
    int modeChanges = 0;
    panel.SetOnPanelModeChange([&](SmartHomePanelMode m){ seen = m; ++modeChanges; });
    panel.SetMode(SmartHomePanelMode::SceneView);
    if (modeChanges != 1 || seen != SmartHomePanelMode::SceneView) {
        std::puts("FAIL: mode change callback"); return 1;
    }
    // Setting the same mode again must not re-fire.
    panel.SetMode(SmartHomePanelMode::SceneView);
    if (modeChanges != 1) { std::puts("FAIL: mode change refired"); return 1; }
    std::puts("ok  SetMode fires once, and not on a no-op");

    panel.AddRoom("Kitchen");
    panel.AddRoom("Hall");
    panel.AssignDeviceToRoom("light-1", "Kitchen");
    if (panel.GetRooms().size() != 2) { std::puts("FAIL: GetRooms"); return 1; }
    if (panel.GetDevicesInRoom("Kitchen").size() != 1) { std::puts("FAIL: room members"); return 1; }

    // Reassigning must move the device, not list it in both rooms.
    panel.AssignDeviceToRoom("light-1", "Hall");
    if (!panel.GetDevicesInRoom("Kitchen").empty() ||
        panel.GetDevicesInRoom("Hall").size() != 1) {
        std::puts("FAIL: reassigning a device left it in both rooms"); return 1;
    }
    std::puts("ok  rooms: assign, reassign, membership");

    panel.RemoveRoom("Hall");
    if (panel.GetRooms().size() != 1) { std::puts("FAIL: RemoveRoom"); return 1; }
    std::puts("ok  RemoveRoom");

    panel.SearchDevices("hall");
    if (panel.GetFilter().SearchText != "hall") { std::puts("FAIL: SearchDevices"); return 1; }
    panel.ClearFilters();
    if (!panel.GetFilter().SearchText.empty()) { std::puts("FAIL: ClearFilters"); return 1; }
    std::puts("ok  search and clear filters");

    std::puts("\nPASS - the ported widget API works against the real framework");
    return 0;
}
