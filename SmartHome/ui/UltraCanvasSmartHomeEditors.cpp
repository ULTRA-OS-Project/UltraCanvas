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
// Both are containers of framework elements: a text input for the name,
// list views for the actions and triggers, a dropdown, a switch, buttons.
//
// Version: 2.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHomeAdvancedWidgets.h"
#include "SmartHomeWidgetKit.h"
#include "DataFormats/UltraCanvasJSON.h"

#include <algorithm>

namespace UltraCanvas {
namespace SmartHome {

namespace {

using namespace Kit;

// The editor's own column on its light background.
void AsEditor(UltraCanvasContainer& c) {
    AsColumn(c, 8, 14.0f);
    c.SetBackgroundColor(kPanelBg);
}

std::shared_ptr<UltraCanvasTextInput> NameInput(const std::string& id, const std::string& placeholder) {
    auto input = CreateTextInput(id, 0, 0, 200, 28);
    input->SetPlaceholder(placeholder);
    Height(*input, 28);
    return input;
}

std::shared_ptr<UltraCanvasListView> List(const std::string& id, const std::shared_ptr<RowsModel>& model,
                                          const std::string& accessibleName) {
    auto list = std::make_shared<UltraCanvasListView>(id, 0, 0, 200, 80);
    list->SetModel(model);
    list->SetShowHeader(false);
    list->SetRowHeight(24);
    list->SetAccessibleName(accessibleName);
    // As wide as the editor: a constructed width would hold it at 200.
    list->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(80));
    return list;
}

std::shared_ptr<UltraCanvasContainer> Gap(const std::string& id) {
    auto s = CreateContainer(id);
    Plain(*s);
    Grow(*s);
    return s;
}

// The device's name where the home knows it, its id where not.
std::string DeviceName(const std::string& id) {
    const SmartHomeDeviceInfo info = SMARTHOME_API.GetDevice(id);
    return info.Name.empty() ? id : info.Name;
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
            return DeviceName(t.DeviceId) + " " + t.Attribute + " = " + t.Value;
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
    : UltraCanvasContainer(identifier, x, y, w, h) {
    AsEditor(*this);

    title = Label(identifier + "_Title", "New scene", 14, kText, true);
    AddChild(title);
    nameInput = NameInput(identifier + "_Name", "Scene name");
    AddChild(nameInput);

    AddChild(Label(identifier + "_ActionsCaption", "What it does", 10, kTextDim, true));
    actionsModel = std::make_shared<RowsModel>(std::vector<ListColumnDef>{{"Device", 170}, {"Does", 120}});
    actionsList = List(identifier + "_Actions", actionsModel, "Scene actions");
    Grow(*actionsList);
    AddChild(actionsList);

    auto addRow = Row(identifier + "_AddRow", 6, 28);
    deviceChoice = CreateDropdown(identifier + "_Device", 0, 0, 160, 28);
    Grow(*deviceChoice);
    addButton = Kit::Button(identifier + "_Add", "Add");
    Size(*addButton, 56, 28);
    addButton->SetOnClick([this]() { AddChosenDevice(); });
    removeButton = Kit::Button(identifier + "_Remove", "Remove");
    Size(*removeButton, 72, 28);
    removeButton->SetOnClick([this]() {
        const int row = actionsList->GetSelection() ? actionsList->GetSelection()->GetCurrentRow() : -1;
        if (row >= 0) RemoveAction(static_cast<size_t>(row));
    });
    addRow->AddChild(deviceChoice);
    addRow->AddChild(addButton);
    addRow->AddChild(removeButton);
    AddChild(addRow);

    auto buttons = Row(identifier + "_Buttons", 8, 28);
    buttons->AddChild(Gap(identifier + "_ButtonsGap"));
    cancelButton = Kit::Button(identifier + "_Cancel", "Cancel");
    Size(*cancelButton, 76, 28);
    cancelButton->SetOnClick([this]() { if (onCancel) onCancel(); });
    saveButton = Kit::Button(identifier + "_Save", "Save", true);
    Size(*saveButton, 76, 28);
    saveButton->SetOnClick([this]() { SaveScene(); });
    buttons->AddChild(cancelButton);
    buttons->AddChild(saveButton);
    AddChild(buttons);

    Refresh();
}

SmartHomeSceneEditor::~SmartHomeSceneEditor() = default;

void SmartHomeSceneEditor::NewScene() {
    scene = SmartHomeScene{};
    nameInput->SetText("");
    FillDevices();
    Refresh();
}

void SmartHomeSceneEditor::EditScene(const std::string& sceneId) {
    scene = SMARTHOME_API.GetScene(sceneId);
    nameInput->SetText(scene.Name);
    FillDevices();
    Refresh();
}

SmartHomeScene SmartHomeSceneEditor::GetScene() const {
    SmartHomeScene s = scene;
    if (!nameInput->GetText().empty()) s.Name = nameInput->GetText();
    return s;
}

void SmartHomeSceneEditor::SetSceneName(const std::string& name) {
    scene.Name = name;
    nameInput->SetText(name);
}

void SmartHomeSceneEditor::SetIcon(const std::string& icon) {
    scene.Icon = icon;
}

void SmartHomeSceneEditor::AddAction(const std::string& deviceId, const std::string& action,
                                     const std::map<std::string, std::string>& params) {
    SmartHomeCommand cmd;
    cmd.DeviceId = deviceId;
    cmd.Command = action;
    cmd.Parameters = params;
    scene.Actions.push_back(std::move(cmd));
    Refresh();
}

void SmartHomeSceneEditor::RemoveAction(size_t index) {
    if (index >= scene.Actions.size()) return;   // a stale index must not erase
    scene.Actions.erase(scene.Actions.begin() + static_cast<long>(index));
    Refresh();
}

void SmartHomeSceneEditor::FillDevices() {
    availableDevices = SMARTHOME_API.GetDevices();
    deviceChoice->ClearItems();
    for (const auto& d : availableDevices) deviceChoice->AddItem(d.Name.empty() ? d.DeviceId : d.Name, d.DeviceId);
    if (!availableDevices.empty()) deviceChoice->SetSelectedIndex(0, false);
}

void SmartHomeSceneEditor::AddChosenDevice() {
    const int index = deviceChoice->GetSelectedIndex();
    if (index < 0 || index >= static_cast<int>(availableDevices.size())) return;
    // A scene records what each device should do; "on" is the useful default
    // and the host can refine it from the action list.
    AddAction(availableDevices[static_cast<size_t>(index)].DeviceId, "on", {});
}

void SmartHomeSceneEditor::Refresh() {
    title->SetText(scene.SceneId.empty() ? "New scene" : "Edit scene");
    std::vector<std::vector<std::string>> rows;
    for (const auto& a : scene.Actions) rows.push_back({DeviceName(a.DeviceId), a.Command});
    actionsModel->Set(std::move(rows));
    removeButton->SetDisabled(scene.Actions.empty());
    RequestRedraw();
}

void SmartHomeSceneEditor::SaveScene() {
    scene = GetScene();
    if (scene.Name.empty()) scene.Name = "Untitled scene";

    // An id is what tells create from update; without one the manager would
    // add a second scene every time this was saved.
    if (scene.SceneId.empty()) {
        scene.SceneId = "scene-" + std::to_string(SMARTHOME_API.GetScenes().size() + 1);
        SMARTHOME_API.CreateScene(scene);
    } else {
        SMARTHOME_API.UpdateScene(scene);
    }
    Refresh();
    if (onSave) onSave(scene);
}

// ============================================================================
// Automation editor
// ============================================================================

SmartHomeAutomationEditor::SmartHomeAutomationEditor(const std::string& identifier,
                                                     float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    AsEditor(*this);

    title = Label(identifier + "_Title", "New automation", 14, kText, true);
    AddChild(title);
    nameInput = NameInput(identifier + "_Name", "Automation name");
    AddChild(nameInput);

    auto section = [&](const std::string& key, const std::string& caption,
                       std::shared_ptr<RowsModel>& model, std::shared_ptr<UltraCanvasListView>& list) {
        AddChild(Label(identifier + "_" + key + "Caption", caption, 10, kTextDim, true));
        model = std::make_shared<RowsModel>(std::vector<ListColumnDef>{{"", 300}});
        list = List(identifier + "_" + key, model, caption);
        Grow(*list);
        AddChild(list);
    };
    section("Triggers", "WHEN", triggerModel, triggerList);
    section("Conditions", "AND IF", conditionModel, conditionList);
    section("Actions", "THEN", actionModel, actionList);

    auto buttons = Row(identifier + "_Buttons", 8, 28);
    enabledSwitch = Kit::Switch(identifier + "_Enabled", true);
    enabledSwitch->onStateChanged = [this](CheckedState, CheckedState now) {
        if (syncing) return;
        automation.Enabled = now == CheckedState::Checked;
        if (!automation.AutomationId.empty()) {
            SMARTHOME_API.EnableAutomation(automation.AutomationId, automation.Enabled);
        }
    };
    buttons->AddChild(enabledSwitch);
    buttons->AddChild(Label(identifier + "_EnabledLabel", "Enabled", 11, kText));
    buttons->AddChild(Gap(identifier + "_ButtonsGap"));
    cancelButton = Kit::Button(identifier + "_Cancel", "Cancel");
    Size(*cancelButton, 76, 28);
    cancelButton->SetOnClick([this]() { if (onCancel) onCancel(); });
    saveButton = Kit::Button(identifier + "_Save", "Save", true);
    Size(*saveButton, 76, 28);
    saveButton->SetOnClick([this]() { Save(); });
    buttons->AddChild(cancelButton);
    buttons->AddChild(saveButton);
    AddChild(buttons);

    Refresh();
}

SmartHomeAutomationEditor::~SmartHomeAutomationEditor() = default;

void SmartHomeAutomationEditor::NewAutomation() {
    automation = SmartHomeAutomation{};
    triggers.clear();
    conditions.clear();
    nameInput->SetText("");
    Refresh();
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
    nameInput->SetText(automation.Name);
    Refresh();
}

SmartHomeAutomation SmartHomeAutomationEditor::GetAutomation() const {
    SmartHomeAutomation a = automation;
    if (!nameInput->GetText().empty()) a.Name = nameInput->GetText();
    return a;
}

void SmartHomeAutomationEditor::SetName(const std::string& name) {
    automation.Name = name;
    nameInput->SetText(name);
}

void SmartHomeAutomationEditor::AddTrigger(const AutomationTrigger& trigger) {
    triggers.push_back(trigger);
    Refresh();
}

void SmartHomeAutomationEditor::AddCondition(const AutomationCondition& condition) {
    conditions.push_back(condition);
    Refresh();
}

void SmartHomeAutomationEditor::AddAction(const SmartHomeCommand& action) {
    automation.Actions.push_back(action);
    Refresh();
}

void SmartHomeAutomationEditor::Refresh() {
    syncing = true;
    title->SetText(automation.AutomationId.empty() ? "New automation" : "Edit automation");
    enabledSwitch->SetChecked(automation.Enabled);

    std::vector<std::vector<std::string>> rows;
    for (const auto& t : triggers) rows.push_back({DescribeTrigger(t)});
    if (rows.empty()) rows.push_back({"Nothing will start this automation."});
    triggerModel->Set(std::move(rows));

    rows.clear();
    for (const auto& c : conditions) rows.push_back({ConditionToString(c)});
    if (rows.empty()) rows.push_back({"Always."});
    conditionModel->Set(std::move(rows));

    rows.clear();
    for (const auto& a : automation.Actions) rows.push_back({DeviceName(a.DeviceId) + " -> " + a.Command});
    if (rows.empty()) rows.push_back({"Nothing happens yet."});
    actionModel->Set(std::move(rows));
    syncing = false;
    RequestRedraw();
}

void SmartHomeAutomationEditor::Save() {
    automation = GetAutomation();
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
    Refresh();
    if (onSave) onSave(automation);
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
