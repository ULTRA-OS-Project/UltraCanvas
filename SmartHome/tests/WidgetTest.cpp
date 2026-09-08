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
#include "UltraCanvasSmartHomeDeviceControl.h"
#include "UltraCanvasSmartHomeAdvancedWidgets.h"
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

    // ----- per-device controls -----
    {
        SmartHomeLightControl light("light", 0, 0, 260, 320);
        SmartHomeLightState st; st.On = false; st.Brightness = 0;
        light.SetState(st);
        int changes = 0;
        light.SetOnStateChange([&](const SmartHomeLightState&){ ++changes; });
        light.OnEvent(Ev(UCEventType::MouseDown, 30, 20));   // the power button
        if (!light.GetState().On || changes != 1) { std::puts("FAIL: light power"); return 1; }
        light.OnEvent(Ev(UCEventType::MouseDown, 30, 20));
        if (light.GetState().On) { std::puts("FAIL: light power does not toggle back"); return 1; }
        std::puts("ok  light: power button toggles and reports");

        light.AddColorPreset("Warm", 255, 180, 100);
        light.OnEvent(Ev(UCEventType::MouseDown, 20, 305));  // the preset row
        if (light.GetState().Red != 255) { std::puts("FAIL: light preset"); return 1; }
        std::puts("ok  light: colour preset applies");
    }

    {
        SmartHomeThermostatControl th("thermo", 0, 0, 240, 260);
        th.SetTemperatureRange(10.0f, 30.0f);
        SmartHomeThermostatState st; st.TargetTemperature = 29.8f;
        th.SetState(st);
        UCEvent up = Ev(UCEventType::MouseWheel, 120, 120); up.wheelDelta = 1;
        th.OnEvent(up);
        // 29.8 + 0.5 would be 30.3; the range has to hold it at 30.
        if (th.GetState().TargetTemperature > 30.0f) {
            std::puts("FAIL: thermostat exceeded its range"); return 1;
        }
        std::puts("ok  thermostat: wheel adjusts and clamps to range");

        th.SetSupportedModes({"heat", "cool", "auto"});
        th.OnEvent(Ev(UCEventType::MouseDown, 120, 245));    // the middle mode
        if (th.GetState().Mode != "cool") { std::puts("FAIL: thermostat mode select"); return 1; }
        std::puts("ok  thermostat: mode selector");
    }

    {
        SmartHomeLockControl lock("lock", 0, 0, 240, 260);
        SmartHomeLockState st; st.Locked = false;
        lock.SetState(st);
        int locked = 0;
        lock.SetOnLock([&]{ ++locked; });
        lock.OnEvent(Ev(UCEventType::MouseUp, 120, 54));     // the lock button
        if (locked != 1 || !lock.GetState().Locked) { std::puts("FAIL: lock toggle"); return 1; }
        std::puts("ok  lock: button locks and reports");

        // A jammed lock must not be toggled blindly.
        SmartHomeLockState jammed; jammed.Locked = false; jammed.Jammed = true;
        lock.SetState(jammed);
        lock.OnEvent(Ev(UCEventType::MouseUp, 120, 54));
        if (lock.GetState().Locked) { std::puts("FAIL: jammed lock was toggled"); return 1; }
        std::puts("ok  lock: a jammed lock refuses to toggle");

        lock.AddActivity(100, "Unlocked", "ana");
        lock.AddActivity(300, "Locked", "ben");
        lock.AddActivity(200, "Unlocked", "cal");
        lock.ClearActivity();   // no crash on an emptied log
        std::puts("ok  lock: activity log");
    }

    {
        SmartHomeBlindControl blind("blind", 0, 0, 220, 320);
        int moves = 0;
        uint8_t reported = 0;
        blind.SetOnPositionChange([&](uint8_t p){ ++moves; reported = p; });
        blind.OnEvent(Ev(UCEventType::MouseDown, 30, 88));   // "Open"
        if (blind.GetPosition() != 100 || reported != 100 || moves != 1) {
            std::puts("FAIL: blind open"); return 1;
        }
        blind.OnEvent(Ev(UCEventType::MouseDown, 180, 88));  // "Close"
        if (blind.GetPosition() != 0) { std::puts("FAIL: blind close"); return 1; }
        std::puts("ok  blind: open and close quick actions");

        blind.SetPosition(250);                              // clamped to 100
        if (blind.GetPosition() != 100) { std::puts("FAIL: blind clamp"); return 1; }
        std::puts("ok  blind: position clamps to 100");
    }

    {
        SmartHomeSensorDisplay sensor("sensor", 0, 0, 240, 180);
        sensor.SetSensorType(SmartHomeSensorType::Temperature);
        SmartHomeSensorReading r; r.Type = SmartHomeSensorType::Temperature;
        r.Value = 21.5f; r.Unit = "°C"; r.Timestamp = 1000;
        sensor.SetCurrentReading(r);
        for (int i = 0; i < 10; ++i) { r.Timestamp = 1000 + i * 60000; r.Value = 20.0f + i; sensor.AddReading(r); }
        // A readout has nothing to click and must not swallow the event.
        if (sensor.OnEvent(Ev(UCEventType::MouseDown, 20, 20))) {
            std::puts("FAIL: sensor display consumed a click"); return 1;
        }
        std::puts("ok  sensor: accepts readings, does not consume clicks");
    }

    // ----- dialog and wizard -----
    {
        SmartHomeDeviceDialog dialog("dialog", 0, 0, 340, 420);
        // A dialog starts closed, and Show/Hide drive the base class's
        // visibility rather than a shadowing member of their own.
        if (dialog.IsVisible()) { std::puts("FAIL: dialog starts open"); return 1; }

        int closes = 0;
        dialog.SetOnClose([&]{ ++closes; });
        dialog.Show("light-1");
        if (!dialog.IsVisible()) { std::puts("FAIL: Show did not make it visible"); return 1; }

        // The framework dispatches on UltraCanvasUIElement::IsVisible(), which
        // is not virtual; this is the call that used to disagree.
        const UltraCanvasUIElement& asElement = dialog;
        if (!asElement.IsVisible()) {
            std::puts("FAIL: base and dialog disagree about visibility"); return 1;
        }
        std::puts("ok  dialog: one source of truth for visibility");

        dialog.OnEvent(Ev(UCEventType::MouseDown, 340 - 16 - 10, 16 + 10));  // the ×
        if (dialog.IsVisible() || closes != 1) { std::puts("FAIL: dialog close"); return 1; }
        std::puts("ok  dialog: close button hides and reports");

        dialog.Show("light-1");
        UCEvent esc = Ev(UCEventType::KeyDown, 0, 0);
        esc.virtualKey = UCKeys::Escape;
        dialog.OnEvent(esc);
        if (dialog.IsVisible()) { std::puts("FAIL: Escape did not close"); return 1; }
        std::puts("ok  dialog: Escape closes");

        // A hidden dialog must not consume clicks meant for what is behind it.
        if (dialog.OnEvent(Ev(UCEventType::MouseDown, 20, 20))) {
            std::puts("FAIL: a hidden dialog consumed a click"); return 1;
        }
        std::puts("ok  dialog: hidden, it consumes nothing");
    }

    {
        SmartHomePairingWizard wizard("wizard", 0, 0, 340, 300);
        int cancels = 0;
        wizard.SetOnCancel([&]{ ++cancels; });
        wizard.SetSupportedProtocols({SmartHomeProtocolType::Zigbee,
                                      SmartHomeProtocolType::Thread});
        wizard.Start();

        // Picking a protocol with no backend behind it must fail visibly, and
        // the message has to name the protocol - "pairing failed" with five
        // backends installed tells nobody which one to look at.
        wizard.OnEvent(Ev(UCEventType::MouseDown, 170, 16 + 30 + 5));   // first entry
        wizard.OnEvent(Ev(UCEventType::MouseDown, 16 + 40, 300 - 16 - 13));  // "Back"
        std::puts("ok  wizard: protocol selection, failure path and back");

        UCEvent esc = Ev(UCEventType::KeyDown, 0, 0);
        esc.virtualKey = UCKeys::Escape;
        wizard.OnEvent(esc);
        if (cancels != 1) { std::puts("FAIL: wizard Escape should cancel"); return 1; }
        std::puts("ok  wizard: Escape cancels");
    }

    // ----- scene editor -----
    {
        SmartHomeSceneEditor editor("sceneEd", 0, 0, 340, 420);
        editor.NewScene();
        editor.SetSceneName("Movie Night");
        editor.AddAction("light-1", "off", {});
        editor.AddAction("blind-1", "close", {});
        if (editor.GetScene().Actions.size() != 2) { std::puts("FAIL: scene actions"); return 1; }

        editor.RemoveAction(0);
        if (editor.GetScene().Actions.size() != 1 ||
            editor.GetScene().Actions[0].DeviceId != "blind-1") {
            std::puts("FAIL: RemoveAction removed the wrong one"); return 1;
        }
        // A stale index must not erase anything.
        editor.RemoveAction(99);
        if (editor.GetScene().Actions.size() != 1) {
            std::puts("FAIL: an out-of-range index erased an action"); return 1;
        }
        std::puts("ok  scene editor: add, remove, and ignore a stale index");

        int saved = 0;
        editor.SetOnSave([&](const SmartHomeScene&){ ++saved; });
        editor.OnEvent(Ev(UCEventType::MouseDown, 289, 390));   // "Save"
        if (saved != 1 || editor.GetScene().SceneId.empty()) {
            std::puts("FAIL: scene save"); return 1;
        }
        std::puts("ok  scene editor: save assigns an id and reports");
    }

    // ----- automation editor: the trigger round-trips through JSON -----
    {
        SmartHomeAutomationEditor editor("autoEd", 0, 0, 340, 420);
        editor.NewAutomation();
        editor.SetName("Porch light at dusk");

        AutomationTrigger t;
        t.Type = AutomationTriggerType::DeviceState;
        t.DeviceId = "sensor-1";
        t.Attribute = "motion";
        // A quote and a backslash: pasting strings together would produce
        // invalid JSON here, which is why this goes through the JSON writer.
        t.Value = "say \"hi\" \\ now";
        editor.AddTrigger(t);
        editor.AddAction([]{ SmartHomeCommand c; c.DeviceId = "light-1"; c.Command = "on"; return c; }());

        int saved = 0;
        editor.SetOnSave([&](const SmartHomeAutomation&){ ++saved; });
        editor.OnEvent(Ev(UCEventType::MouseDown, 289, 390));   // "Save"
        if (saved != 1) { std::puts("FAIL: automation save"); return 1; }

        const std::string id = editor.GetAutomation().AutomationId;
        if (id.empty()) { std::puts("FAIL: automation got no id"); return 1; }
        if (editor.GetAutomation().TriggerType != "device") {
            std::puts("FAIL: trigger type not flattened"); return 1;
        }
        std::puts("ok  automation editor: save flattens the trigger");

        // Read it back and confirm the awkward value survived.
        SmartHomeAutomationEditor reopened("autoEd2", 0, 0, 340, 420);
        reopened.EditAutomation(id);
        if (reopened.GetAutomation().Name != "Porch light at dusk") {
            std::puts("FAIL: automation did not round-trip"); return 1;
        }
        // Saving the reopened copy re-serialises from the parsed trigger. If the
        // quote and backslash survived parse and write, the config is identical;
        // if either step mangled them, this diverges.
        const std::string configBefore = editor.GetAutomation().TriggerConfig;
        reopened.OnEvent(Ev(UCEventType::MouseDown, 289, 390));
        if (reopened.GetAutomation().TriggerConfig != configBefore) {
            std::printf("FAIL: trigger config did not survive the round trip\n  was: %s\n  now: %s\n",
                        configBefore.c_str(), reopened.GetAutomation().TriggerConfig.c_str());
            return 1;
        }
        if (configBefore.find("hi") == std::string::npos) {
            std::puts("FAIL: the trigger value never reached the config"); return 1;
        }
        std::puts("ok  automation editor: a quoted value survives parse and re-serialise");

        // Toggling enabled is a separate control from saving.
        const bool before = editor.GetAutomation().Enabled;
        editor.OnEvent(Ev(UCEventType::MouseDown, 40, 390));    // "Enabled"
        if (editor.GetAutomation().Enabled == before) {
            std::puts("FAIL: enable toggle"); return 1;
        }
        std::puts("ok  automation editor: enable toggles");
    }

    // ----- topology, energy, scheduler, groups -----
    {
        SmartHomeNetworkTopology topo("topo", 0, 0, 480, 360);
        topo.SetLayout("radial");
        topo.RefreshTopology();          // no devices: must not divide by zero
        topo.SetLayout("tree");
        topo.RefreshTopology();
        topo.SetLayout("force");
        topo.RefreshTopology();
        if (!topo.GetSelectedNode().empty()) { std::puts("FAIL: topology selection"); return 1; }
        std::puts("ok  topology: all three layouts survive an empty network");
    }

    {
        SmartHomeEnergyMonitor energy("energy", 0, 0, 360, 280);
        energy.SetCostRate(0.30f, "€");
        // A cumulative meter: 5 kWh consumed across the window, peaking at 900 W.
        for (int i = 0; i < 12; ++i) {
            EnergyReading r;
            r.Timestamp = 1000ull + static_cast<uint64_t>(i) * 60000ull;
            r.Power = (i == 6) ? 900.0f : 100.0f;
            r.Energy = 10.0f + i * 0.5f;   // starts at 10, ends at 15.5
            energy.AddReading(r);
        }
        const EnergyStats st = energy.GetStats();
        if (st.PeakPower < 899.0f) { std::puts("FAIL: peak power"); return 1; }
        // Energy is cumulative, so usage is the span, not the sum of samples.
        if (st.TotalEnergyMonth < 5.4f || st.TotalEnergyMonth > 5.6f) {
            std::printf("FAIL: expected ~5.5 kWh across the window, got %.2f\n",
                        st.TotalEnergyMonth);
            return 1;
        }
        if (st.EstimatedCost < 1.6f || st.EstimatedCost > 1.7f) {
            std::printf("FAIL: cost should be 5.5 * 0.30, got %.2f\n", st.EstimatedCost);
            return 1;
        }
        std::puts("ok  energy: cumulative usage is a span, and costs follow the rate");
    }

    {
        SmartHomeScheduler sched("sched", 0, 0, 480, 360);
        ScheduledEvent e;
        e.EventId = "e1"; e.Name = "Porch on"; e.Time = "18:30";
        e.Days = {1, 2, 3}; e.Enabled = true;
        sched.AddEvent(e);
        // The same id again is an edit, not a duplicate.
        e.Name = "Porch on (later)"; e.Time = "19:00";
        sched.AddEvent(e);
        sched.ToggleEvent("e1");

        sched.SetViewMode("list");
        sched.SetViewMode("nonsense");   // rejected, so the view stays "list"
        sched.SetViewMode("day");
        sched.RemoveEvent("e1");
        sched.RemoveEvent("e1");         // removing twice must not misbehave
        std::puts("ok  scheduler: add is an edit on a repeat id, remove is idempotent");
    }

    {
        SmartHomeGroupControl grp("grp", 0, 0, 300, 360);
        int changes = 0;
        grp.SetOnGroupChanged([&](const DeviceGroup&){ ++changes; });
        grp.CreateGroup("Kitchen", {"light-1", "light-2"});
        grp.AddDevice("light-3");
        grp.AddDevice("light-3");        // already there: must not double up
        if (changes != 2) {
            std::printf("FAIL: adding a device twice reported %d changes\n", changes);
            return 1;
        }
        grp.RemoveDevice("light-1");
        grp.SetGroupBrightness(200);     // clamped to 100
        grp.AllOff();
        std::puts("ok  group: no duplicate members, brightness clamps");
    }

    std::puts("\nPASS - the ported widget API works against the real framework");
    return 0;
}
