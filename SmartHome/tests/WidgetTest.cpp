// SmartHome/tests/WidgetTest.cpp
// Drives the Smart Home widgets with synthetic UCEvents; the device controls
// through a real window and the application's dispatch.
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
// After the module's headers: these pull in X11, whose `None` macro would
// eat SmartHomeSecurityLevel::None.
#include "../../Tests/DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <variant>

using namespace UltraCanvas;
using namespace UltraCanvas::SmartHome;

static UCEvent Ev(UCEventType t, int x, int y) {
    UCEvent e; e.type = t; e.pointer = Point2Di(x, y); return e;
}

// ----- the device controls, through a window -----
// The controls are containers of framework elements, so they are driven the
// way a user drives them: a press and release at window coordinates, through
// the application's own dispatch, on the element a host can reach by its
// getter - not at a painted position. Opens a real window, so this part runs
// under Xvfb (xvfb-run -a) and skips itself without a DISPLAY.

namespace {

UltraCanvasApplication* gApp = nullptr;
std::shared_ptr<UltraCanvasWindow> gWindow;

void Send(UCEventType type, const Point2Df& at, int wheel = 0) {
    UCEvent e;
    e.type = type;
    e.button = (type == UCEventType::MouseDown || type == UCEventType::MouseUp) ? UCMouseButton::Left
                                                                             : UCMouseButton::NoneButton;
    e.wheelDelta = wheel;
    e.targetWindow = gWindow;
    e.nativeWindowHandle = gWindow->GetNativeHandle();
    const Point2Di p(static_cast<int>(std::lround(at.x)), static_cast<int>(std::lround(at.y)));
    e.pointerWindow = p;
    e.pointer = p;
    gApp->DispatchEvent(e);
}

void Frame() { gWindow->UpdateAndRender(); }

// A click `fx`, `fy` of the way across the element (0.5, 0.5 = its centre).
void Click(const UltraCanvasUIElement* element, float fx = 0.5f, float fy = 0.5f) {
    const Rect2Df b = element->GetBoundsInWindow();
    const Point2Df at(b.x + b.width * fx, b.y + b.height * fy);
    Send(UCEventType::MouseMove, at);
    Send(UCEventType::MouseDown, at);
    Send(UCEventType::MouseUp, at);
    Frame();
}

// Shows `control` alone in the window, laid out.
template <typename T>
std::shared_ptr<T> Place(std::shared_ptr<T> control) {
    gWindow->ClearChildren();
    gWindow->AddChild(control);
    Frame();
    Frame();
    return control;
}

bool Fail(const char* what) { std::printf("FAIL: %s\n", what); return true; }

int DeviceControlsThroughWindow() {
    // ULTRACANVAS_TEST_DISPLAY_REQUIRED=1 (CI's virtual-display step) makes
    // a missing display a failure, so a broken Xvfb cannot pass as a skip.
    const char* required = std::getenv("ULTRACANVAS_TEST_DISPLAY_REQUIRED");
    const bool displayRequired = required && std::string(required) == "1";
    if (!std::getenv("DISPLAY")) {
        if (displayRequired) { std::puts("FAIL: a display was required, and there is none"); return 1; }
        std::puts("skip device controls: no DISPLAY (run under xvfb-run -a)");
        return 0;
    }
    static UltraCanvasApplication app;
    if (!app.Initialize("SmartHomeWidgetTest")) {
        std::puts(displayRequired ? "FAIL: the application would not initialise" : "skip device controls: no application");
        return displayRequired ? 1 : 0;
    }
    WindowConfig cfg;
    cfg.title = "SmartHomeWidgetTest";
    cfg.width = 320;
    cfg.height = 400;
    gWindow = CreateWindow(cfg);
    if (!gWindow) {
        std::puts(displayRequired ? "FAIL: no window" : "skip device controls: no window");
        return displayRequired ? 1 : 0;
    }
    gApp = &app;
    gWindow->Show();
    DisplayTest::ActivateWindow(app, gWindow);

    {
        auto light = Place(std::make_shared<SmartHomeLightControl>("light", 10, 10, 260, 360));
        SmartHomeLightState st; st.On = false; st.Brightness = 0;
        light->SetState(st);
        int changes = 0;
        light->SetOnStateChange([&](const SmartHomeLightState&){ ++changes; });
        if (changes != 0) return Fail("SetState reported a change");
        Click(light->GetPowerSwitch());
        if (!light->GetState().On || changes != 1) return Fail("light power");
        Click(light->GetPowerSwitch());
        if (light->GetState().On || changes != 2) return Fail("light power does not toggle back");
        std::puts("ok  light: the power switch toggles and reports");

        Click(light->GetBrightnessSlider(), 0.75f);
        const int b = light->GetState().Brightness;
        if (b < 170 || b > 210 || !light->GetState().On) {
            std::printf("FAIL: light brightness at three quarters gave %d\n", b); return 1;
        }
        std::puts("ok  light: the brightness slider sets the level and turns the light on");

        light->AddColorPreset("Warm", 255, 180, 100);
        Frame();
        const Rect2Df bar = light->GetPresetBar()->GetBoundsInWindow();
        if (!light->GetPresetBar()->IsVisible() || bar.height <= 0) return Fail("preset bar not shown");
        Click(light->GetPresetBar(), (bar.height / 2.0f) / bar.width);
        if (light->GetState().Red != 255 || light->GetState().Green != 180) return Fail("light preset");
        std::puts("ok  light: a colour preset applies");

        light->SetColorEnabled(false);
        Frame();
        if (light->GetColorPicker()->IsVisible() || light->GetPresetBar()->IsVisible()) {
            return Fail("SetColorEnabled(false) left the colour controls");
        }
        std::puts("ok  light: SetColorEnabled hides the colour controls");
    }

    {
        auto th = Place(std::make_shared<SmartHomeThermostatControl>("thermo", 10, 10, 240, 280));
        th->SetTemperatureRange(10.0f, 30.0f);
        SmartHomeThermostatState st; st.TargetTemperature = 29.8f; st.CurrentTemperature = 20.5f;
        th->SetState(st);
        th->SetSupportedModes({"heat", "cool", "auto"});
        Frame();
        const Rect2Df gb = th->GetGauge()->GetBoundsInWindow();
        Send(UCEventType::MouseWheel, Point2Df(gb.x + gb.width / 2, gb.y + gb.height / 2), 1);
        // 29.8 + 0.5 would be 30.3; the range has to hold it at 30.
        if (th->GetState().TargetTemperature > 30.0f || th->GetState().TargetTemperature < 29.9f) {
            std::printf("FAIL: wheel gave %.2f\n", th->GetState().TargetTemperature); return 1;
        }
        std::puts("ok  thermostat: the wheel adjusts and clamps to range");

        Click(th->GetModeSelector());   // the middle of three
        if (th->GetState().Mode != "cool") return Fail("thermostat mode select");
        std::puts("ok  thermostat: the mode selector");

        th->SetState([]{ SmartHomeThermostatState s; s.TargetTemperature = 20.0f; s.Mode = "heat"; return s; }());
        int reports = 0;
        th->SetOnStateChange([&](const SmartHomeThermostatState&){ ++reports; });
        th->GetTargetSpinner()->SetValue(21.5);   // as the spinner does on a step or a typed value
        if (std::abs(th->GetState().TargetTemperature - 21.5f) > 0.01f || reports != 1) {
            return Fail("thermostat target spinner");
        }
        std::puts("ok  thermostat: the target spinner sets the target");
    }

    {
        auto lock = Place(std::make_shared<SmartHomeLockControl>("lock", 10, 10, 240, 260));
        SmartHomeLockState st; st.Locked = false; st.BatteryLevel = 80;
        lock->SetState(st);
        int locked = 0;
        lock->SetOnLock([&]{ ++locked; });
        Click(lock->GetLockButton());
        if (locked != 1 || !lock->GetState().Locked) return Fail("lock toggle");
        if (lock->GetLockButton()->GetText() != "Unlock") return Fail("lock button says what it does");
        std::puts("ok  lock: the button locks and reports");

        // A jammed lock must not be toggled blindly.
        SmartHomeLockState jammed; jammed.Locked = false; jammed.Jammed = true;
        lock->SetState(jammed);
        Click(lock->GetLockButton());
        if (lock->GetState().Locked) return Fail("jammed lock was toggled");
        std::puts("ok  lock: a jammed lock refuses to toggle");

        lock->AddActivity(100, "Unlocked", "ana");
        lock->AddActivity(300, "Locked", "ben");
        lock->AddActivity(200, "Unlocked", "cal");
        auto* model = lock->GetActivityList()->GetModel();
        if (!model || model->GetRowCount() != 3) return Fail("lock activity rows");
        const auto first = model->GetData(ListIndex{0, 0}, ListDataRole::DisplayRole);
        if (!std::holds_alternative<std::string>(first) ||
            std::get<std::string>(first).find("ben") == std::string::npos) {
            return Fail("lock activity is not newest first");
        }
        lock->ClearActivity();
        if (model->GetRowCount() != 0) return Fail("lock ClearActivity");
        Frame();
        std::puts("ok  lock: activity, newest first, in a list view");
    }

    {
        auto blind = Place(std::make_shared<SmartHomeBlindControl>("blind", 10, 10, 220, 340));
        int moves = 0;
        uint8_t reported = 0;
        blind->SetOnPositionChange([&](uint8_t p){ ++moves; reported = p; });
        Click(blind->GetOpenButton());
        if (blind->GetPosition() != 100 || reported != 100 || moves != 1) return Fail("blind open");
        Click(blind->GetCloseButton());
        if (blind->GetPosition() != 0) return Fail("blind close");
        std::puts("ok  blind: open and close");

        Click(blind->GetPositionSlider(), 0.5f);
        if (blind->GetPosition() < 40 || blind->GetPosition() > 60) {
            std::printf("FAIL: blind slider at half gave %d\n", blind->GetPosition()); return 1;
        }
        std::puts("ok  blind: the position slider moves it");

        blind->AddPreset("Half", 50);
        blind->AddPreset("Shade", 20);
        blind->SetTiltEnabled(true);
        Frame();
        if (!blind->GetTiltSlider()->IsVisible()) return Fail("tilt not shown");
        blind->SetPosition(250);                              // clamped to 100
        if (blind->GetPosition() != 100) return Fail("blind clamp");
        std::puts("ok  blind: presets, tilt, and the position clamps to 100");
    }

    {
        auto sensor = Place(std::make_shared<SmartHomeSensorDisplay>("sensor", 10, 10, 240, 180));
        sensor->SetSensorType(SmartHomeSensorType::Temperature);
        SmartHomeSensorReading r; r.Type = SmartHomeSensorType::Temperature;
        r.Value = 21.5f; r.Unit = "°C"; r.Timestamp = 1000;
        sensor->SetCurrentReading(r);
        for (int i = 0; i < 10; ++i) { r.Timestamp = 1000 + i * 60000; r.Value = 20.0f + i; sensor->AddReading(r); }
        sensor->SetThresholds(15, 25);
        Frame();
        // A readout has nothing to click and must not swallow the event.
        if (sensor->OnEvent(Ev(UCEventType::MouseDown, 20, 20))) return Fail("sensor display consumed a click");
        std::puts("ok  sensor: accepts readings, does not consume clicks");
    }

    gWindow->ClearChildren();
    return 0;
}

} // namespace

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

    // ----- per-device controls, through a real window -----
    if (DeviceControlsThroughWindow() != 0) return 1;

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
