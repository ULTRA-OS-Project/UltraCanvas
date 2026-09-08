// ui/UltraCanvasSmartHomeEditors.cpp
// The scene editor and the automation editor.
//
// Both edit data the manager already stores, so they read and write through
// SmartHomeAPI and hand the finished object to their onSave callback; whether
// it is then persisted is the host's decision.
//
// SmartHomeAutomation carries its trigger as a type string plus a
// TriggerConfig the public header documents as JSON, while the editor works in
// the structured AutomationTrigger the widgets header declares. The two are
// bridged here with UltraCanvasJSON rather than by pasting strings together,
// so a config containing a quote or a backslash survives the round trip.
//
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHomeAdvancedWidgets.h"
#include "DataFormats/UltraCanvasJSON.h"

#include <algorithm>

namespace UltraCanvas {
namespace SmartHome {

namespace {

constexpr double kPad = 14.0;
constexpr double kRow = 26.0;

const Color kText    (0x21, 0x21, 0x21);
const Color kTextDim (0x75, 0x75, 0x75);
const Color kTrack   (0xE0, 0xE0, 0xE0);
const Color kAccent  (33, 150, 243);
const Color kDanger  (0xF4, 0x43, 0x36);
const Color kSheet   (0xFF, 0xFF, 0xFF);

void Button(IRenderContext* ctx, const Rect2Dd& r, const std::string& label,
            const Color& fill, const Color& textColor) {
    ctx->SetFillPaint(fill);
    ctx->FillRoundedRectangle(r, r.height / 2.0);
    ctx->SetTextPaint(textColor);
    ctx->SetFontSize(11.0);
    ctx->DrawTextInRect(label, Rect2Dd(r.x + 10.0, r.y + r.height / 2.0 - 7.0,
                                       r.width - 20.0, 14.0));
}

bool Hit(const Rect2Dd& r, int x, int y) {
    return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

const char* TriggerTypeName(AutomationTriggerType t) {
    switch (t) {
        case AutomationTriggerType::DeviceState: return "device";
        case AutomationTriggerType::Time:        return "time";
        case AutomationTriggerType::Sunrise:     return "sunrise";
        case AutomationTriggerType::Sunset:      return "sunset";
        case AutomationTriggerType::Location:    return "location";
        case AutomationTriggerType::Manual:      return "manual";
    }
    return "manual";
}

// One trigger as JSON. Only the fields that mean something for the type are
// written, so a time trigger does not carry an empty device id.
JSONValue TriggerToJson(const AutomationTrigger& t) {
    JSONValue o = JSONValue::MakeObject();
    o.Set("type", std::string(TriggerTypeName(t.Type)));
    switch (t.Type) {
        case AutomationTriggerType::DeviceState:
            o.Set("deviceId", t.DeviceId);
            o.Set("attribute", t.Attribute);
            o.Set("value", t.Value);
            break;
        case AutomationTriggerType::Time:
            o.Set("time", t.Time);
            break;
        case AutomationTriggerType::Sunrise:
        case AutomationTriggerType::Sunset:
            o.Set("offsetMinutes", t.SunOffset);
            break;
        default:
            break;
    }
    return o;
}

std::string ConditionToString(const AutomationCondition& c) {
    switch (c.Type) {
        case AutomationConditionType::TimeRange:
            return "time between " + c.StartTime + " and " + c.EndTime;
        case AutomationConditionType::DayOfWeek: {
            static const char* kDays[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
            std::string out = "on ";
            for (size_t i = 0; i < c.Days.size(); ++i) {
                const int d = c.Days[i];
                if (d < 0 || d > 6) continue;
                if (i) out += ",";
                out += kDays[d];
            }
            return out;
        }
        default:
            return c.DeviceId + " " + c.Attribute + " " + c.Operator + " " + c.Value;
    }
}

std::string DescribeTrigger(const AutomationTrigger& t) {
    switch (t.Type) {
        case AutomationTriggerType::DeviceState:
            return t.DeviceId + " " + t.Attribute + " = " + t.Value;
        case AutomationTriggerType::Time:     return "at " + t.Time;
        case AutomationTriggerType::Sunrise:  return "at sunrise" +
            (t.SunOffset ? " " + std::to_string(t.SunOffset) + " min" : "");
        case AutomationTriggerType::Sunset:   return "at sunset" +
            (t.SunOffset ? " " + std::to_string(t.SunOffset) + " min" : "");
        case AutomationTriggerType::Location: return "on arriving or leaving";
        case AutomationTriggerType::Manual:   return "when run by hand";
    }
    return {};
}

}  // namespace

// ============================================================================
// Scene editor
// ============================================================================

SmartHomeSceneEditor::SmartHomeSceneEditor(const std::string& identifier,
                                           float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

SmartHomeSceneEditor::~SmartHomeSceneEditor() = default;

void SmartHomeSceneEditor::NewScene() {
    scene = SmartHomeScene{};
    editingName.clear();
    editingNameActive = false;
    selectedActionIndex = -1;
    showingDeviceSelector = false;
    scrollOffset = 0.0f;
    availableDevices = SMARTHOME_API.GetDevices();
    RequestRedraw();
}

void SmartHomeSceneEditor::EditScene(const std::string& sceneId) {
    scene = SMARTHOME_API.GetScene(sceneId);
    editingName = scene.Name;
    editingNameActive = false;
    selectedActionIndex = -1;
    showingDeviceSelector = false;
    scrollOffset = 0.0f;
    availableDevices = SMARTHOME_API.GetDevices();
    RequestRedraw();
}

void SmartHomeSceneEditor::SetSceneName(const std::string& name) {
    scene.Name = name;
    editingName = name;
    RequestRedraw();
}

void SmartHomeSceneEditor::SetIcon(const std::string& icon) {
    scene.Icon = icon;
    RequestRedraw();
}

void SmartHomeSceneEditor::AddAction(const std::string& deviceId, const std::string& action,
                                     const std::map<std::string, std::string>& params) {
    SmartHomeCommand cmd;
    cmd.DeviceId = deviceId;
    cmd.Command = action;
    cmd.Parameters = params;
    scene.Actions.push_back(std::move(cmd));
    RequestRedraw();
}

void SmartHomeSceneEditor::RemoveAction(size_t index) {
    if (index >= scene.Actions.size()) return;   // a stale index must not erase
    scene.Actions.erase(scene.Actions.begin() + static_cast<long>(index));
    if (selectedActionIndex >= static_cast<int>(scene.Actions.size())) {
        selectedActionIndex = -1;
    }
    RequestRedraw();
}

void SmartHomeSceneEditor::ShowDeviceSelector() {
    availableDevices = SMARTHOME_API.GetDevices();
    showingDeviceSelector = true;
    RequestRedraw();
}

void SmartHomeSceneEditor::AddDeviceToScene(const std::string& deviceId) {
    // A scene records what each device should do; "on" is the useful default
    // and the host can refine it from the action list.
    AddAction(deviceId, "on", {});
    showingDeviceSelector = false;
    RequestRedraw();
}

void SmartHomeSceneEditor::SaveScene() {
    if (!editingName.empty()) scene.Name = editingName;
    if (scene.Name.empty()) scene.Name = "Untitled scene";

    // An id is what tells create from update; without one the manager would
    // add a second scene every time this was saved.
    if (scene.SceneId.empty()) {
        scene.SceneId = "scene-" + std::to_string(SMARTHOME_API.GetScenes().size() + 1);
        SMARTHOME_API.CreateScene(scene);
    } else {
        SMARTHOME_API.UpdateScene(scene);
    }
    if (onSave) onSave(scene);
    RequestRedraw();
}

void SmartHomeSceneEditor::RenderHeader(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(kText);
    ctx->SetFontSize(14.0);
    ctx->DrawTextInRect(scene.SceneId.empty() ? "New scene" : "Edit scene",
                        Rect2Dd(kPad, kPad, b.width - 2 * kPad, 18.0));
}

void SmartHomeSceneEditor::RenderNameInput(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const Rect2Dd field(kPad, kPad + 26.0, b.width - 2 * kPad, kRow);
    ctx->SetFillPaint(kSheet);
    ctx->FillRoundedRectangle(field, 5.0);
    ctx->SetStrokePaint(editingNameActive ? kAccent : kTrack);
    ctx->SetStrokeWidth(editingNameActive ? 2.0 : 1.0);
    ctx->DrawRoundedRectangle(field, 5.0);

    ctx->SetTextPaint(editingName.empty() ? kTextDim : kText);
    ctx->SetFontSize(11.0);
    ctx->DrawTextInRect(editingName.empty() ? "Scene name" : editingName,
                        Rect2Dd(field.x + 8.0, field.y + 6.0, field.width - 16.0, 14.0));
}

void SmartHomeSceneEditor::RenderActionsList(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    double y = kPad + 62.0 - scrollOffset;
    ctx->SetFontSize(11.0);

    if (scene.Actions.empty()) {
        ctx->SetTextPaint(kTextDim);
        ctx->DrawTextInRect("No devices in this scene yet.",
                            Rect2Dd(kPad, y + 4.0, b.width - 2 * kPad, 16.0));
        return;
    }

    for (size_t i = 0; i < scene.Actions.size(); ++i) {
        if (y > b.height - 60.0) break;   // clip against the button row
        const bool chosen = static_cast<int>(i) == selectedActionIndex;
        ctx->SetFillPaint(chosen ? kAccent : kTrack);
        ctx->FillRoundedRectangle(Rect2Dd(kPad, y, b.width - 2 * kPad, kRow), 5.0);
        ctx->SetTextPaint(chosen ? Colors::White : kText);
        ctx->DrawTextInRect(scene.Actions[i].DeviceId + " → " + scene.Actions[i].Command,
                            Rect2Dd(kPad + 8.0, y + 6.0, b.width - 2 * kPad - 40.0, 14.0));
        ctx->SetTextPaint(chosen ? Colors::White : kTextDim);
        ctx->DrawTextInRect("×", Rect2Dd(b.width - kPad - 20.0, y + 6.0, 14.0, 14.0));
        y += kRow + 6.0;
    }
}

void SmartHomeSceneEditor::RenderDeviceSelector(IRenderContext* ctx) {
    if (!showingDeviceSelector) return;
    const Rect2Df b = GetLocalBounds();

    ctx->SetFillPaint(Color(0, 0, 0, 120));
    ctx->FillRectangle(Rect2Dd(b));

    const double w = b.width - 40.0;
    const double h = std::min<double>(b.height - 60.0, 40.0 + availableDevices.size() * 30.0);
    const double x = 20.0;
    const double y = (b.height - h) / 2.0;

    ctx->SetFillPaint(kSheet);
    ctx->FillRoundedRectangle(Rect2Dd(x, y, w, h), 8.0);
    ctx->SetTextPaint(kText);
    ctx->SetFontSize(12.0);
    ctx->DrawTextInRect("Add a device", Rect2Dd(x + 12.0, y + 10.0, w - 24.0, 16.0));

    ctx->SetFontSize(11.0);
    double ry = y + 34.0;
    for (const auto& device : availableDevices) {
        if (ry + 26.0 > y + h) break;
        ctx->SetFillPaint(kTrack);
        ctx->FillRoundedRectangle(Rect2Dd(x + 12.0, ry, w - 24.0, 24.0), 5.0);
        ctx->SetTextPaint(kText);
        ctx->DrawTextInRect(device.Name.empty() ? device.DeviceId : device.Name,
                            Rect2Dd(x + 20.0, ry + 5.0, w - 40.0, 14.0));
        ry += 28.0;
    }
}

void SmartHomeSceneEditor::RenderActionButtons(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const double y = b.height - kPad - 26.0;
    Button(ctx, Rect2Dd(kPad, y, 96.0, 26.0), "Add device", kTrack, kText);
    Button(ctx, Rect2Dd(b.width - kPad - 74.0, y, 74.0, 26.0), "Save", kAccent, Colors::White);
    Button(ctx, Rect2Dd(b.width - kPad - 158.0, y, 76.0, 26.0), "Cancel", kTrack, kText);
}

void SmartHomeSceneEditor::Render(IRenderContext* ctx, const Rect2Df&) {
    if (!ctx || !IsVisible()) return;
    ctx->SetFillPaint(Color(0xFA, 0xFA, 0xFA));
    ctx->FillRectangle(Rect2Dd(GetLocalBounds()));
    RenderHeader(ctx);
    RenderNameInput(ctx);
    RenderActionsList(ctx);
    RenderActionButtons(ctx);
    RenderDeviceSelector(ctx);
}

bool SmartHomeSceneEditor::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    const Rect2Df b = GetLocalBounds();
    const int x = event.pointer.x;
    const int y = event.pointer.y;

    // Typing goes to the name field while it has focus.
    if (event.type == UCEventType::TextInput && editingNameActive) {
        editingName += event.text;
        RequestRedraw();
        return true;
    }
    if (event.type == UCEventType::KeyDown && editingNameActive) {
        if (event.virtualKey == UCKeys::Backspace) {
            if (!editingName.empty()) editingName.pop_back();
            RequestRedraw();
            return true;
        }
        if (event.virtualKey == UCKeys::Return || event.virtualKey == UCKeys::Escape) {
            editingNameActive = false;
            RequestRedraw();
            return true;
        }
    }

    if (event.type != UCEventType::MouseDown && event.type != UCEventType::TouchStart) {
        return false;
    }

    // The selector is modal: while it is up nothing behind it is clickable.
    if (showingDeviceSelector) {
        const double w = b.width - 40.0;
        const double h = std::min<double>(b.height - 60.0,
                                          40.0 + availableDevices.size() * 30.0);
        const double sy = (b.height - h) / 2.0;
        const int index = static_cast<int>((y - (sy + 34.0)) / 28.0);
        if (index >= 0 && index < static_cast<int>(availableDevices.size()) &&
            x > 20.0 && x < 20.0 + w) {
            AddDeviceToScene(availableDevices[static_cast<size_t>(index)].DeviceId);
        } else {
            showingDeviceSelector = false;   // a click outside dismisses it
            RequestRedraw();
        }
        return true;
    }

    const double by = b.height - kPad - 26.0;
    if (Hit(Rect2Dd(kPad, by, 96.0, 26.0), x, y))                       { ShowDeviceSelector(); return true; }
    if (Hit(Rect2Dd(b.width - kPad - 74.0, by, 74.0, 26.0), x, y))      { SaveScene(); return true; }
    if (Hit(Rect2Dd(b.width - kPad - 158.0, by, 76.0, 26.0), x, y)) {
        if (onCancel) onCancel();
        return true;
    }

    if (Hit(Rect2Dd(kPad, kPad + 26.0, b.width - 2 * kPad, kRow), x, y)) {
        editingNameActive = true;
        RequestRedraw();
        return true;
    }
    editingNameActive = false;

    // Action rows: the × removes, anywhere else selects.
    const int index = static_cast<int>((y - (kPad + 62.0 - scrollOffset)) / (kRow + 6.0));
    if (index >= 0 && index < static_cast<int>(scene.Actions.size())) {
        if (x >= b.width - kPad - 22.0) RemoveAction(static_cast<size_t>(index));
        else                            selectedActionIndex = index;
        RequestRedraw();
        return true;
    }
    return true;
}

// ============================================================================
// Automation editor
// ============================================================================

SmartHomeAutomationEditor::SmartHomeAutomationEditor(const std::string& identifier,
                                                     float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

SmartHomeAutomationEditor::~SmartHomeAutomationEditor() = default;

void SmartHomeAutomationEditor::NewAutomation() {
    automation = SmartHomeAutomation{};
    triggers.clear();
    conditions.clear();
    editingSection = 0;
    editingIndex = -1;
    scrollOffset = 0.0f;
    RequestRedraw();
}

void SmartHomeAutomationEditor::EditAutomation(const std::string& automationId) {
    automation = SMARTHOME_API.GetAutomation(automationId);
    triggers.clear();
    conditions.clear();

    // The stored form is a type plus a JSON config; rebuild what of it the
    // structured editor can represent. Anything it cannot is left in
    // automation.TriggerConfig untouched, so saving does not discard it.
    if (!automation.TriggerConfig.empty()) {
        JSONParseResult status;
        const JSONValue o = JSON::Parse(automation.TriggerConfig, &status);
        if (status.success) {
            AutomationTrigger t;
            const std::string type = o.Get("type").GetString(automation.TriggerType);
            if      (type == "device")  t.Type = AutomationTriggerType::DeviceState;
            else if (type == "time")    t.Type = AutomationTriggerType::Time;
            else if (type == "sunrise") t.Type = AutomationTriggerType::Sunrise;
            else if (type == "sunset")  t.Type = AutomationTriggerType::Sunset;
            else if (type == "location")t.Type = AutomationTriggerType::Location;
            else                        t.Type = AutomationTriggerType::Manual;

            t.DeviceId  = o.Get("deviceId").GetString("");
            t.Attribute = o.Get("attribute").GetString("");
            t.Value     = o.Get("value").GetString("");
            t.Time      = o.Get("time").GetString("");
            t.SunOffset = static_cast<int>(o.Get("offsetMinutes").GetInteger(0));
            triggers.push_back(t);
        }
    }
    editingSection = 0;
    editingIndex = -1;
    RequestRedraw();
}

void SmartHomeAutomationEditor::SetName(const std::string& name) {
    automation.Name = name;
    RequestRedraw();
}

void SmartHomeAutomationEditor::AddTrigger(const AutomationTrigger& trigger) {
    triggers.push_back(trigger);
    RequestRedraw();
}

void SmartHomeAutomationEditor::AddCondition(const AutomationCondition& condition) {
    conditions.push_back(condition);
    RequestRedraw();
}

void SmartHomeAutomationEditor::AddAction(const SmartHomeCommand& action) {
    automation.Actions.push_back(action);
    RequestRedraw();
}

void SmartHomeAutomationEditor::RenderHeader(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(kText);
    ctx->SetFontSize(14.0);
    ctx->DrawTextInRect(automation.AutomationId.empty() ? "New automation"
                                                        : "Edit automation",
                        Rect2Dd(kPad, kPad, b.width - 2 * kPad, 18.0));
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(11.0);
    ctx->DrawTextInRect(automation.Name.empty() ? "Unnamed" : automation.Name,
                        Rect2Dd(kPad, kPad + 20.0, b.width - 2 * kPad, 14.0));
}

void SmartHomeAutomationEditor::RenderTriggerSection(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    double y = kPad + 44.0 - scrollOffset;

    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(10.0);
    ctx->DrawTextInRect("WHEN", Rect2Dd(kPad, y, b.width - 2 * kPad, 12.0));
    y += 14.0;

    ctx->SetFontSize(11.0);
    if (triggers.empty()) {
        ctx->SetTextPaint(kTextDim);
        ctx->DrawTextInRect("Nothing will start this automation.",
                            Rect2Dd(kPad, y + 4.0, b.width - 2 * kPad, 14.0));
        return;
    }
    for (const auto& t : triggers) {
        ctx->SetFillPaint(kTrack);
        ctx->FillRoundedRectangle(Rect2Dd(kPad, y, b.width - 2 * kPad, kRow), 5.0);
        ctx->SetTextPaint(kText);
        ctx->DrawTextInRect(DescribeTrigger(t),
                            Rect2Dd(kPad + 8.0, y + 6.0, b.width - 2 * kPad - 16.0, 14.0));
        y += kRow + 4.0;
    }
}

void SmartHomeAutomationEditor::RenderConditionSection(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    double y = kPad + 44.0 + 18.0 + std::max<size_t>(triggers.size(), 1) * (kRow + 4.0)
               - scrollOffset;

    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(10.0);
    ctx->DrawTextInRect("AND IF", Rect2Dd(kPad, y, b.width - 2 * kPad, 12.0));
    y += 14.0;

    ctx->SetFontSize(11.0);
    if (conditions.empty()) {
        ctx->SetTextPaint(kTextDim);
        ctx->DrawTextInRect("Always.", Rect2Dd(kPad, y + 4.0, b.width - 2 * kPad, 14.0));
        return;
    }
    for (const auto& c : conditions) {
        ctx->SetFillPaint(kTrack);
        ctx->FillRoundedRectangle(Rect2Dd(kPad, y, b.width - 2 * kPad, kRow), 5.0);
        ctx->SetTextPaint(kText);
        ctx->DrawTextInRect(ConditionToString(c),
                            Rect2Dd(kPad + 8.0, y + 6.0, b.width - 2 * kPad - 16.0, 14.0));
        y += kRow + 4.0;
    }
}

void SmartHomeAutomationEditor::RenderActionSection(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    double y = b.height - kPad - 26.0 - 12.0
               - std::max<size_t>(automation.Actions.size(), 1) * (kRow + 4.0);

    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(10.0);
    ctx->DrawTextInRect("THEN", Rect2Dd(kPad, y, b.width - 2 * kPad, 12.0));
    y += 14.0;

    ctx->SetFontSize(11.0);
    if (automation.Actions.empty()) {
        ctx->SetTextPaint(kTextDim);
        ctx->DrawTextInRect("Nothing happens yet.",
                            Rect2Dd(kPad, y + 4.0, b.width - 2 * kPad, 14.0));
        return;
    }
    for (const auto& a : automation.Actions) {
        ctx->SetFillPaint(kTrack);
        ctx->FillRoundedRectangle(Rect2Dd(kPad, y, b.width - 2 * kPad, kRow), 5.0);
        ctx->SetTextPaint(kText);
        ctx->DrawTextInRect(a.DeviceId + " → " + a.Command,
                            Rect2Dd(kPad + 8.0, y + 6.0, b.width - 2 * kPad - 16.0, 14.0));
        y += kRow + 4.0;
    }
}

void SmartHomeAutomationEditor::RenderButtons(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const double y = b.height - kPad - 26.0;
    Button(ctx, Rect2Dd(kPad, y, 86.0, 26.0),
           automation.Enabled ? "Enabled" : "Disabled",
           automation.Enabled ? kAccent : kTrack,
           automation.Enabled ? Colors::White : kTextDim);
    Button(ctx, Rect2Dd(b.width - kPad - 74.0, y, 74.0, 26.0), "Save", kAccent, Colors::White);
    Button(ctx, Rect2Dd(b.width - kPad - 158.0, y, 76.0, 26.0), "Cancel", kTrack, kText);
}

// The three per-item editors are reached by tapping a row. Nothing is drawn for
// them yet — the row list is the editor's current surface — but the section is
// tracked so a host can put its own form up in response.
void SmartHomeAutomationEditor::RenderTriggerEditor(IRenderContext*)   {}
void SmartHomeAutomationEditor::RenderConditionEditor(IRenderContext*) {}
void SmartHomeAutomationEditor::RenderActionEditor(IRenderContext*)    {}

void SmartHomeAutomationEditor::Render(IRenderContext* ctx, const Rect2Df&) {
    if (!ctx || !IsVisible()) return;
    ctx->SetFillPaint(Color(0xFA, 0xFA, 0xFA));
    ctx->FillRectangle(Rect2Dd(GetLocalBounds()));
    RenderHeader(ctx);
    RenderTriggerSection(ctx);
    RenderConditionSection(ctx);
    RenderActionSection(ctx);
    RenderButtons(ctx);

    switch (editingSection) {
        case 1: RenderTriggerEditor(ctx);   break;
        case 2: RenderConditionEditor(ctx); break;
        case 3: RenderActionEditor(ctx);    break;
        default: break;
    }
}

bool SmartHomeAutomationEditor::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    const Rect2Df b = GetLocalBounds();
    const int x = event.pointer.x;
    const int y = event.pointer.y;

    if (event.type != UCEventType::MouseDown && event.type != UCEventType::TouchStart) {
        return false;
    }
    if (!Contains(Point2Df(static_cast<float>(x), static_cast<float>(y)))) return false;

    const double by = b.height - kPad - 26.0;

    if (Hit(Rect2Dd(kPad, by, 86.0, 26.0), x, y)) {
        automation.Enabled = !automation.Enabled;
        if (!automation.AutomationId.empty()) {
            SMARTHOME_API.EnableAutomation(automation.AutomationId, automation.Enabled);
        }
        RequestRedraw();
        return true;
    }

    if (Hit(Rect2Dd(b.width - kPad - 74.0, by, 74.0, 26.0), x, y)) {
        // Flatten the structured trigger back into the stored representation.
        if (!triggers.empty()) {
            automation.TriggerType = TriggerTypeName(triggers.front().Type);
            automation.TriggerConfig = JSON::Serialize(TriggerToJson(triggers.front()));
        }
        automation.Conditions.clear();
        for (const auto& c : conditions) automation.Conditions.push_back(ConditionToString(c));
        if (automation.Name.empty()) automation.Name = "Untitled automation";

        if (automation.AutomationId.empty()) {
            automation.AutomationId =
                "automation-" + std::to_string(SMARTHOME_API.GetAutomations().size() + 1);
            SMARTHOME_API.CreateAutomation(automation);
        } else {
            SMARTHOME_API.UpdateAutomation(automation);
        }
        if (onSave) onSave(automation);
        RequestRedraw();
        return true;
    }

    if (Hit(Rect2Dd(b.width - kPad - 158.0, by, 76.0, 26.0), x, y)) {
        if (onCancel) onCancel();
        return true;
    }

    // Which band was tapped decides which section a host should open.
    const double triggerTop = kPad + 44.0;
    const double conditionTop = triggerTop + 18.0 +
                                std::max<size_t>(triggers.size(), 1) * (kRow + 4.0);
    const double actionTop = b.height - kPad - 38.0 -
                             std::max<size_t>(automation.Actions.size(), 1) * (kRow + 4.0);
    if (y >= actionTop)          editingSection = 3;
    else if (y >= conditionTop)  editingSection = 2;
    else if (y >= triggerTop)    editingSection = 1;
    else                         editingSection = 0;
    RequestRedraw();
    return true;
}

// ===== FACTORIES =====

std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeSceneEditorElement() {
    return std::make_shared<SmartHomeSceneEditor>("SmartHomeSceneEditor", 0, 0, 340, 420);
}
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeAutomationEditorElement() {
    return std::make_shared<SmartHomeAutomationEditor>("SmartHomeAutomationEditor", 0, 0, 340, 420);
}

}  // namespace SmartHome
}  // namespace UltraCanvas
