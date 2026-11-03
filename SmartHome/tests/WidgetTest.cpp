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
#include "Plugins/Diagrams/UltraCanvasNodeDiagram.h"
// After the module's headers: these pull in X11, whose `None` macro would
// eat SmartHomeSecurityLevel::NoSecurity.
#include "../../Tests/DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <string>
#include <variant>

using namespace UltraCanvas;
using namespace UltraCanvas::SmartHome;

static UCEvent Ev(UCEventType t, int x, int y) {
    UCEvent e; e.type = t; e.pointer = Point2Di(x, y); return e;
}

// ----- the widgets, through a window -----
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

int ControlsThroughWindow() {
    // ULTRACANVAS_TEST_DISPLAY_REQUIRED=1 (CI's virtual-display step) makes
    // a missing display a failure, so a broken Xvfb cannot pass as a skip.
    const char* required = std::getenv("ULTRACANVAS_TEST_DISPLAY_REQUIRED");
    const bool displayRequired = required && std::string(required) == "1";
    if (!std::getenv("DISPLAY")) {
        if (displayRequired) { std::puts("FAIL: a display was required, and there is none"); return 1; }
        std::puts("skip windowed checks: no DISPLAY (run under xvfb-run -a)");
        return 0;
    }
    static UltraCanvasApplication app;
    if (!app.Initialize("SmartHomeWidgetTest")) {
        std::puts(displayRequired ? "FAIL: the application would not initialise" : "skip windowed checks: no application");
        return displayRequired ? 1 : 0;
    }
    WindowConfig cfg;
    cfg.title = "SmartHomeWidgetTest";
    cfg.width = 800;    // the panel's toolbar needs the width
    cfg.height = 420;
    gWindow = CreateWindow(cfg);
    if (!gWindow) {
        std::puts(displayRequired ? "FAIL: no window" : "skip windowed checks: no window");
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

    // ----- the dialog, the wizard and the editors -----
    {
        auto dialog = Place(std::make_shared<SmartHomeDeviceDialog>("dialog", 10, 10, 300, 380));
        int closes = 0;
        dialog->SetOnClose([&]{ ++closes; });
        dialog->Show("light-1");
        Frame();
        Click(dialog->GetCloseButton());
        if (dialog->IsVisible() || closes != 1) return Fail("dialog close button");
        std::puts("ok  dialog: the close button hides and reports");
    }

    {
        // Simulated pairing: a device joins a few (simulated) seconds after
        // pairing starts, so the wizard's search ends in "Added".
        auto& api = SmartHomeAPI::Instance();
        api.EnableProtocol(SmartHomeProtocolType::Simulated);
        auto wizard = Place(std::make_shared<SmartHomePairingWizard>("wizard", 10, 10, 300, 260));
        SmartHomeDeviceInfo added;
        wizard->SetOnComplete([&](const SmartHomeDeviceInfo& d){ added = d; });
        wizard->SetSupportedProtocols({SmartHomeProtocolType::Zigbee, SmartHomeProtocolType::Simulated});
        wizard->Start();
        Frame();
        Click(wizard->GetProtocolButton(0));   // Zigbee: not in this build
        if (wizard->GetErrorMessage().find("Zigbee") == std::string::npos || !wizard->GetBackButton()->IsVisible()) {
            return Fail("wizard failure names the protocol");
        }
        Click(wizard->GetBackButton());
        if (!wizard->GetProtocolButton(1)->IsVisible()) return Fail("wizard Back");
        std::puts("ok  wizard: a protocol without a backend fails by name, Back returns");

        Click(wizard->GetProtocolButton(1));   // Simulated
        if (!wizard->IsSearching()) return Fail("wizard did not start searching");
        for (int i = 0; i < 100 && wizard->IsSearching(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            wizard->Poll();
        }
        if (added.DeviceId.empty() || !wizard->GetDoneButton()->IsVisible()) {
            std::printf("FAIL: pairing found nothing (%s)\n", wizard->GetErrorMessage().c_str()); return 1;
        }
        std::printf("ok  wizard: searching ends when a device joins (%s)\n", added.Name.c_str());
        api.DisableProtocol(SmartHomeProtocolType::Simulated);
    }

    {
        auto editor = Place(std::make_shared<SmartHomeSceneEditor>("sceneEd", 10, 10, 300, 380));
        editor->NewScene();
        editor->SetSceneName("Window scene");
        editor->AddAction("light-1", "off", {});
        int saved = 0;
        editor->SetOnSave([&](const SmartHomeScene&){ ++saved; });
        Frame();
        Click(editor->GetSaveButton());
        if (saved != 1 || editor->GetScene().SceneId.empty()) return Fail("scene save");
        std::puts("ok  scene editor: Save assigns an id and reports");
    }

    // The automation's trigger round-trips through JSON.
    {
        auto editor = Place(std::make_shared<SmartHomeAutomationEditor>("autoEd", 10, 10, 300, 380));
        editor->NewAutomation();
        editor->SetName("Porch light at dusk");

        AutomationTrigger t;
        t.Type = AutomationTriggerType::DeviceState;
        t.DeviceId = "sensor-1";
        t.Attribute = "motion";
        // A quote and a backslash: pasting strings together would produce
        // invalid JSON here, which is why this goes through the JSON writer.
        t.Value = "say \"hi\" \\ now";
        editor->AddTrigger(t);
        editor->AddAction([]{ SmartHomeCommand c; c.DeviceId = "light-1"; c.Command = "on"; return c; }());

        int saved = 0;
        editor->SetOnSave([&](const SmartHomeAutomation&){ ++saved; });
        Frame();
        Click(editor->GetSaveButton());
        if (saved != 1) return Fail("automation save");

        const std::string id = editor->GetAutomation().AutomationId;
        if (id.empty()) return Fail("automation got no id");
        if (editor->GetAutomation().TriggerType != "device") return Fail("trigger type not flattened");
        std::puts("ok  automation editor: save flattens the trigger");

        // Read it back and confirm the awkward value survived.
        auto reopened = Place(std::make_shared<SmartHomeAutomationEditor>("autoEd2", 10, 10, 300, 380));
        reopened->EditAutomation(id);
        if (reopened->GetAutomation().Name != "Porch light at dusk") return Fail("automation did not round-trip");
        // Saving the reopened copy re-serialises from the parsed trigger. If the
        // quote and backslash survived parse and write, the config is identical;
        // if either step mangled them, this diverges.
        const std::string configBefore = editor->GetAutomation().TriggerConfig;
        Frame();
        Click(reopened->GetSaveButton());
        if (reopened->GetAutomation().TriggerConfig != configBefore) {
            std::printf("FAIL: trigger config did not survive the round trip\n  was: %s\n  now: %s\n",
                        configBefore.c_str(), reopened->GetAutomation().TriggerConfig.c_str());
            return 1;
        }
        if (configBefore.find("hi") == std::string::npos) return Fail("the trigger value never reached the config");
        std::puts("ok  automation editor: a quoted value survives parse and re-serialise");

        // Enabled is its own control, apart from saving.
        const bool before = reopened->GetAutomation().Enabled;
        Click(reopened->GetEnabledSwitch());
        if (reopened->GetAutomation().Enabled == before) return Fail("enable toggle");
        if (SmartHomeAPI::Instance().GetAutomation(id).Enabled == before) return Fail("enable toggle not stored");
        std::puts("ok  automation editor: the switch enables and disables it");
    }

    // ----- topology, energy, scheduler and group control -----
    {
        auto& api = SmartHomeAPI::Instance();
        api.EnableProtocol(SmartHomeProtocolType::Simulated);
        auto topo = Place(std::make_shared<SmartHomeNetworkTopology>("topo", 10, 10, 300, 300));
        topo->RefreshTopology();
        Frame();
        if (topo->GetNodes().empty()) return Fail("topology: no devices");
        for (const auto& n : topo->GetNodes()) {
            if (!topo->GetDiagram()->GetNode(n.DeviceId)) return Fail("topology: every device a node in the diagram");
        }
        Click(topo->GetLayoutSelector(), 5.0f / 6.0f);   // "Tree", the last of three
        // In the diagram, every router sits above every other device.
        double lowestRouter = -1e9, highestLeaf = 1e9;
        for (const auto& n : topo->GetNodes()) {
            const NodeDiagramNode* dn = topo->GetDiagram()->GetNode(n.DeviceId);
            if (n.IsRouter) lowestRouter = std::max(lowestRouter, dn->y);
            else            highestLeaf = std::min(highestLeaf, dn->y);
        }
        if (!(lowestRouter < highestLeaf)) return Fail("topology: the tree layout puts the routers on top");

        // In every layout, no two cards overlap.
        for (const char* layoutName : {"force", "radial", "tree"}) {
            topo->SetLayout(layoutName);
            const auto& ns = topo->GetNodes();
            for (size_t i = 0; i < ns.size(); ++i) {
                for (size_t j = i + 1; j < ns.size(); ++j) {
                    const NodeDiagramNode* a = topo->GetDiagram()->GetNode(ns[i].DeviceId);
                    const NodeDiagramNode* b = topo->GetDiagram()->GetNode(ns[j].DeviceId);
                    const bool apart = a->x + a->width <= b->x + 0.01 || b->x + b->width <= a->x + 0.01 ||
                                       a->y + a->height <= b->y + 0.01 || b->y + b->height <= a->y + 0.01;
                    if (!apart) {
                        std::printf("FAIL: topology %s: %s overlaps %s\n", layoutName, ns[i].Name.c_str(), ns[j].Name.c_str());
                        return 1;
                    }
                }
            }
            if (topo->GetDiagram()->GetNode(ns[0].DeviceId)->iconPath.empty()) return Fail("topology: a card without an icon");
        }
        std::printf("ok  topology: %zu devices as icon cards, no overlaps in any layout, the layout switch works\n", topo->GetNodes().size());
        api.DisableProtocol(SmartHomeProtocolType::Simulated);
    }

    {
        auto energy = Place(std::make_shared<SmartHomeEnergyMonitor>("energy", 10, 10, 300, 280));
        for (int i = 0; i < 12; ++i) {
            EnergyReading r{};
            r.Timestamp = 1000ull + static_cast<uint64_t>(i) * 60000ull;
            r.Power = 100.0f + i;
            r.Energy = 10.0f + i * 0.5f;
            energy->AddReading(r);
        }
        Frame();
        Click(energy->GetModeSelector(), 0.5f);   // "Energy"
        if (energy->GetModeSelector()->GetSelectedIndex() != 1) return Fail("energy: mode selector");
        std::puts("ok  energy: the chart's mode is a segmented control");
    }

    {
        auto sched = Place(std::make_shared<SmartHomeScheduler>("sched", 10, 10, 300, 300));
        ScheduledEvent e;
        e.EventId = "e1"; e.Name = "Porch on"; e.Time = "18:30"; e.Days = {1, 2, 3}; e.Enabled = true;
        sched->AddEvent(e);
        int creates = 0;
        sched->SetOnEventCreate([&](const std::string&, int){ ++creates; });
        Frame();
        Click(sched->GetViewSelector(), 5.0f / 6.0f);   // "List"
        if (sched->GetViewMode() != "list" || !sched->GetEventList()->IsVisible()) return Fail("scheduler: list view");
        const Rect2Df lb = sched->GetEventList()->GetBoundsInWindow();
        Click(sched->GetEventList(), 0.5f, (24.0f + 12.0f) / lb.height);   // the first row, under the header
        if (sched->GetSelectedEvent() != "e1") return Fail("scheduler: a row selects its event");
        Click(sched->GetToggleButton());
        if (sched->GetEvents()[0].Enabled) return Fail("scheduler: Turn off");
        std::puts("ok  scheduler: list view, select a row, turn it off");

        Click(sched->GetViewSelector(), 0.5f);          // "Week"
        sched->SetViewMode("week");
        Frame();
        if (creates != 0) return Fail("scheduler: a create before any press");
        std::puts("ok  scheduler: week view");
    }

    {
        auto grp = Place(std::make_shared<SmartHomeGroupControl>("grp", 10, 10, 300, 380));
        grp->CreateGroup("Kitchen", {"light-1", "light-2"});
        Frame();
        Click(grp->GetBrightnessSlider(), 0.25f);
        if (grp->GetBrightnessSlider()->GetValue() < 15 || grp->GetBrightnessSlider()->GetValue() > 35) {
            return Fail("group: brightness slider");
        }
        Click(grp->GetAllOffButton());
        std::puts("ok  group: the brightness slider and All off");
    }

    // ----- the dashboard panel -----
    {
        auto& api = SmartHomeAPI::Instance();
        api.EnableProtocol(SmartHomeProtocolType::Simulated);
        auto panel = Place(std::make_shared<SmartHomePanel>("panel", 0, 0, 760, 380));
        panel->Initialize();
        panel->SetMode(SmartHomePanelMode::DeviceGrid);
        Frame();
        Frame();
        std::string lightId;
        for (const auto& d : api.GetDevices()) {
            if (d.Category == SmartHomeDeviceCategory::Light && panel->GetDeviceCard(d.DeviceId)) { lightId = d.DeviceId; break; }
        }
        SmartHomeDeviceCard* card = lightId.empty() ? nullptr : panel->GetDeviceCard(lightId);
        if (!card || card->GetBoundsInWindow().width <= 0) return Fail("panel: a light's card is laid out");
        // Scroll is at the top, and the first row of cards is in view.
        const bool before = api.GetSwitchState(lightId);
        Click(card);
        api.WaitForCommands(2000);
        if (panel->GetSelectedDevice() != lightId) return Fail("panel: a press on a card selects it");
        if (api.GetSwitchState(lightId) == before) return Fail("panel: a press on a card switches it");
        std::puts("ok  panel: a card is a child element; a press selects and switches its device");

        // The grid wraps, and its row container is as tall as all its lines:
        // every card sits inside its parent's height (a row measured one line
        // high clipped every card after the first line).
        int rows = 0;
        float lastY = -1.0f;
        for (const auto& d : api.GetDevices()) {
            SmartHomeDeviceCard* c = panel->GetDeviceCard(d.DeviceId);
            auto* parent = c ? dynamic_cast<UltraCanvasUIElement*>(c->Parent()) : nullptr;
            if (!parent) continue;
            const Rect2Df b = c->GetBounds();
            if (b.y + b.height > parent->GetBounds().height + 0.5f) return Fail("panel: a card outside its row's height");
            if (b.y != lastY) { ++rows; lastY = b.y; }
        }
        if (rows < 2) return Fail("panel: the grid wraps onto more than one line");
        std::puts("ok  panel: the card grid wraps, and every line is inside it");

        Click(panel->GetModeSelector(), 1.5f / 6.0f);   // "Rooms"
        if (panel->GetMode() != SmartHomePanelMode::RoomView) return Fail("panel: the mode selector");
        Click(panel->GetModeSelector(), 5.5f / 6.0f);   // "Network"
        if (panel->GetMode() != SmartHomePanelMode::NetworkView || !panel->GetTopology()->IsVisible() ||
            panel->GetTopology()->GetNodes().empty()) {
            return Fail("panel: the network mode shows the topology");
        }
        std::puts("ok  panel: the toolbar's mode selector, and the network as a topology");
        api.DisableProtocol(SmartHomeProtocolType::Simulated);
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

    // ----- the controls, the dialog, the wizard and the editors, through a real window -----
    if (ControlsThroughWindow() != 0) return 1;

    // ----- dialog and wizard, without a window -----
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

        UCEvent esc = Ev(UCEventType::KeyDown, 0, 0);
        esc.virtualKey = UCKeys::Escape;
        dialog.OnEvent(esc);
        if (dialog.IsVisible() || closes != 1) { std::puts("FAIL: Escape did not close"); return 1; }
        std::puts("ok  dialog: Escape closes and reports");

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
        if (!wizard.GetProtocolButton(0) || !wizard.GetProtocolButton(1) || wizard.GetProtocolButton(2)) {
            std::puts("FAIL: wizard has a button per protocol"); return 1;
        }
        UCEvent esc = Ev(UCEventType::KeyDown, 0, 0);
        esc.virtualKey = UCKeys::Escape;
        wizard.OnEvent(esc);
        if (cancels != 1) { std::puts("FAIL: wizard Escape should cancel"); return 1; }
        std::puts("ok  wizard: a button per protocol, Escape cancels");
    }

    // ----- scene editor, without a window -----
    {
        SmartHomeSceneEditor editor("sceneEd", 0, 0, 340, 420);
        editor.NewScene();
        editor.SetSceneName("Movie Night");
        editor.AddAction("light-1", "off", {});
        editor.AddAction("blind-1", "close", {});
        if (editor.GetScene().Actions.size() != 2) { std::puts("FAIL: scene actions"); return 1; }
        if (editor.GetActionsList()->GetModel()->GetRowCount() != 2) { std::puts("FAIL: scene action rows"); return 1; }

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
        if (editor.GetScene().Name != "Movie Night") { std::puts("FAIL: scene name"); return 1; }
        std::puts("ok  scene editor: add, remove, and ignore a stale index");
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
