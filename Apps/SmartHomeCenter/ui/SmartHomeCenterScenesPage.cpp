// Apps/SmartHomeCenter/ui/SmartHomeCenterScenesPage.cpp
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SmartHomeCenterPages.h"
#include "SmartHomeCenterStyle.h"

#include "UltraCanvasListModel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextUtils.h"   // TryParseFloat / FormatFloatClassic

#include <algorithm>
#include <cctype>
#include <cmath>

using namespace UltraCanvas;
using namespace UltraCanvas::SmartHome;

namespace SmartHomeCenter {

namespace {

// What can be done to a device in a scene, by its kind: the words in the
// editor, the command, the parameter its value goes in ("" when it takes
// none) and, for those, how a person's number becomes that parameter.
struct ActionKind {
    const char* words;
    const char* command;
    const char* param;          // "" = no value
    const char* fixedValue;     // a value the action carries itself (Open: position 100)
    const char* hint;           // what to type
};

std::vector<ActionKind> ActionsFor(const DeviceView& v) {
    switch (v.kind) {
        case DeviceKind::Light: {
            std::vector<ActionKind> a = {{"Turn on", "turnOn", "", "", ""}, {"Turn off", "turnOff", "", "", ""}};
            if (std::find(v.info.Capabilities.begin(), v.info.Capabilities.end(), "dimmer") != v.info.Capabilities.end())
                a.push_back({"Brightness (%)", "setBrightness", "level", "", "0 to 100"});
            if (std::find(v.info.Capabilities.begin(), v.info.Capabilities.end(), "colourTemperature") != v.info.Capabilities.end())
                a.push_back({"Colour temperature (K)", "setColorTemp", "kelvin", "", "2200 to 6500"});
            return a;
        }
        case DeviceKind::Switch:
            return {{"Turn on", "turnOn", "", "", ""}, {"Turn off", "turnOff", "", "", ""}};
        case DeviceKind::Thermostat:
            return {{"Target (°C)", "setTargetTemp", "temperature", "", "10 to 30"},
                    {"Mode: heat", "setMode", "mode", "heat", ""},
                    {"Mode: auto", "setMode", "mode", "auto", ""},
                    {"Mode: off", "setMode", "mode", "off", ""}};
        case DeviceKind::Lock:
            return {{"Lock", "lock", "", "", ""}, {"Unlock", "unlock", "", "", ""}};
        case DeviceKind::Blind:
            return {{"Open", "setPosition", "position", "100", ""},
                    {"Close", "setPosition", "position", "0", ""},
                    {"Position (%)", "setPosition", "position", "", "0 to 100"}};
        default:
            return {};
    }
}

bool CanAct(const DeviceView& v) {
    return !ActionsFor(v).empty();
}

std::string NameOf(const std::string& deviceId) {
    const std::string name = SmartHomeAPI::Instance().GetDevice(deviceId).Name;
    return name.empty() ? deviceId + " (gone)" : name;
}

std::string Param(const SmartHomeCommand& c, const char* key) {
    auto it = c.Parameters.find(key);
    return it == c.Parameters.end() ? std::string() : it->second;
}

} // namespace

// The scene being edited: device, then what it does.
class SceneActionsModel : public IListModel {
public:
    void Set(const std::vector<SmartHomeCommand>& actions) { rows = actions; NotifyDataChanged(); }
    int GetRowCount() const override { return static_cast<int>(rows.size()); }
    int GetColumnCount() const override { return 2; }
    ListColumnDef GetColumnDef(int column) const override {
        static const ListColumnDef columns[] = {{"Device", 150}, {"Does", 190}};
        return columns[std::clamp(column, 0, 1)];
    }
    ListDataValue GetData(const ListIndex& index, ListDataRole role) const override {
        if (index.row < 0 || index.row >= static_cast<int>(rows.size()) || role != ListDataRole::DisplayRole) return {};
        const SmartHomeCommand& c = rows[static_cast<size_t>(index.row)];
        if (index.column == 0) return c.DeviceId.empty() ? std::string("-") : NameOf(c.DeviceId);
        return ScenesPage::DescribeAction(c);
    }
    bool SetData(const ListIndex&, ListDataRole, const ListDataValue&) override { return false; }

private:
    std::vector<SmartHomeCommand> rows;
};

std::string ScenesPage::DescribeAction(const SmartHomeCommand& c) {
    const std::string& cmd = c.Command;
    if (cmd == "turnOn") return "Turn on";
    if (cmd == "turnOff") return "Turn off";
    if (cmd == "toggle") return "Toggle";
    if (cmd == "lock") return "Lock";
    if (cmd == "unlock") return "Unlock";
    if (cmd == "setMode") return "Mode: " + Param(c, "mode");
    if (cmd == "setTargetTemp") {
        double t = 0;
        return TryParseFloat(Param(c, "temperature"), t) ? "Target " + FormatTemperature(static_cast<float>(t)) : "Target";
    }
    if (cmd == "setPosition") {
        const std::string p = Param(c, "position");
        if (p == "100") return "Open";
        if (p == "0") return "Close";
        return "Position " + p + " %";
    }
    if (cmd == "setBrightness") {
        double level = 0;
        if (!TryParseFloat(Param(c, "level"), level)) return "Brightness";
        return "Brightness " + FormatPercent(static_cast<int>(std::lround(level * 100.0 / 255.0)));
    }
    if (cmd == "setColorTemp") return "Colour temperature " + Param(c, "kelvin") + " K";
    if (cmd == "setLight") {
        if (Param(c, "on") == "false") return "Turn off";
        std::string text = "Turn on";
        double b = 0;
        if (TryParseFloat(Param(c, "brightness"), b) && b > 0)
            text += " at " + FormatPercent(static_cast<int>(std::lround(b * 100.0 / 255.0)));
        const std::string k = Param(c, "colorTemp");
        if (!k.empty() && k != "0") text += ", " + k + " K";
        return text;
    }
    if (cmd == "notify") return "Notify: " + Param(c, "message");
    if (cmd == "delay") return "Wait " + Param(c, "seconds") + " s";
    if (cmd == "activateScene") return "Scene: " + SmartHomeAPI::Instance().GetScene(Param(c, "sceneId")).Name;
    return cmd;
}

std::vector<SmartHomeCommand> ScenesPage::CaptureState(const std::vector<DeviceView>& devices) {
    auto& api = SmartHomeAPI::Instance();
    std::vector<SmartHomeCommand> actions;
    for (const DeviceView& v : devices) {
        if (!v.online) continue;
        const std::string& id = v.info.DeviceId;
        switch (v.kind) {
            case DeviceKind::Light: {
                const SmartHomeLightState light = api.GetLightState(id);
                if (!light.On) {
                    actions.push_back({id, "turnOff", {}});
                    break;
                }
                SmartHomeCommand c{id, "setLight", {{"on", "true"}, {"brightness", std::to_string(light.Brightness)}}};
                if (light.ColorTemp > 0) c.Parameters["colorTemp"] = std::to_string(light.ColorTemp);
                actions.push_back(c);
                break;
            }
            case DeviceKind::Switch:
                actions.push_back({id, v.on ? "turnOn" : "turnOff", {}});
                break;
            case DeviceKind::Thermostat: {
                const SmartHomeThermostatState t = api.GetThermostatState(id);
                actions.push_back({id, "setMode", {{"mode", t.Mode}}});
                if (t.Mode != "off")
                    actions.push_back({id, "setTargetTemp", {{"temperature", FormatFloatClassic(t.TargetTemperature, 4)}}});
                break;
            }
            case DeviceKind::Lock:
                actions.push_back({id, api.GetLockState(id).Locked ? "lock" : "unlock", {}});
                break;
            case DeviceKind::Blind:
                actions.push_back({id, "setPosition", {{"position", std::to_string(api.GetBlindPosition(id))}}});
                break;
            default:
                break;
        }
    }
    return actions;
}

ScenesPage::ScenesPage(PageHost& h) : host(h) {}
ScenesPage::~ScenesPage() = default;

std::shared_ptr<UltraCanvasContainer> ScenesPage::Build() {
    auto page = Row("shcScenesPage", 20, CSSLayout::AlignItems::Stretch);

    auto left = Column("shcScenesLeft", 14);
    Grow(left);
    auto tools = Row("shcScenesTools", 10);
    Fixed(tools, -1, 40);
    tools->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto add = MakeButton("shcSceneNew", "New scene", ButtonKind::Primary);
    add->SetIcon(host.IconsDir() + "plus.svg");
    add->SetIconSize(16, 16);
    add->SetIconSpacing(8);
    add->SetOnClick([this]() {
        SmartHomeScene scene;
        scene.Name = "New scene";
        scene.Icon = "scenes";
        Edit(scene, true);
    });
    tools->AddChild(add);
    auto capture = MakeButton("shcSceneCapture", "Capture current state", ButtonKind::Secondary);
    capture->SetIcon(host.IconsDir() + "capture.svg");
    capture->SetIconSize(16, 16);
    capture->SetIconSpacing(8);
    capture->SetTooltip("A new scene that puts every light, switch, thermostat, lock and blind back as it is now");
    capture->SetOnClick([this]() {
        SmartHomeScene scene;
        scene.Name = "Captured " + DateTimeText(NowSeconds());
        scene.Icon = "capture";
        scene.Actions = CaptureState(host.Devices());
        Edit(scene, true);
    });
    tools->AddChild(capture);
    left->AddChild(tools);

    cards = ScrollColumn("shcSceneCards", 0);
    cards->layout.SetFlex(CSSLayout::FlexDirection::Row, CSSLayout::FlexWrap::Wrap).SetFlexGap(14);
    cards->layout.SetFlexAlignContent(CSSLayout::AlignContent::Start);
    Grow(cards);
    left->AddChild(cards);
    page->AddChild(left);

    // The editor, beside the cards while a scene is open.
    editor = Column("shcSceneEditor", 10);
    Card(editor, 18);
    Fixed(editor, 420, -1);
    editor->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    editorTitle = Text("shcSceneEditTitle", "Edit scene", 17, kInk, true, 24);
    editor->AddChild(editorTitle);
    editor->AddChild(Caption("shcSceneNameCap", "NAME"));
    nameInput = MakeInput("shcSceneName", "", "What to call the scene");
    nameInput->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    editor->AddChild(nameInput);
    editor->AddChild(Caption("shcSceneActionsCap", "WHAT IT DOES, IN ORDER"));
    actionsModel = std::make_shared<SceneActionsModel>();
    actionsList = std::make_shared<UltraCanvasListView>("shcSceneActions", 0, 0, 380, 200);
    actionsList->SetModel(actionsModel);
    StyleTable(actionsList);
    actionsList->SetShowHeader(true);
    actionsList->SetRowHeight(30);
    actionsList->SetBorders(1.0f, kBorder);
    Grow(actionsList);
    editor->AddChild(actionsList);
    auto removeAction = MakeButton("shcSceneActionRemove", "Remove the selected action", ButtonKind::Quiet, 30);
    removeAction->SetFontSize(12);
    removeAction->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);
    removeAction->SetOnClick([this]() { RemoveAction(); });
    editor->AddChild(removeAction);

    editor->AddChild(Caption("shcSceneAddCap", "ADD AN ACTION"));
    deviceChoice = std::make_shared<UltraCanvasDropdown>("shcSceneDevice", 380, 34);
    Fixed(deviceChoice, -1, 34);
    deviceChoice->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    deviceChoice->onSelectionChanged = [this](int, const DropdownItem&) { FillWhatChoices(); };
    editor->AddChild(deviceChoice);
    auto addRow = Row("shcSceneAddRow", 8);
    Fixed(addRow, -1, 36);
    addRow->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    whatChoice = std::make_shared<UltraCanvasDropdown>("shcSceneWhat", 190, 34);
    Fixed(whatChoice, 190, 34);
    whatChoice->onSelectionChanged = [this](int, const DropdownItem& item) {
        valueInput->SetDisabled(item.tooltip.empty());
        valueInput->SetPlaceholder(item.tooltip);
    };
    addRow->AddChild(whatChoice);
    valueInput = MakeInput("shcSceneValue", "", "");
    Grow(valueInput);
    valueInput->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
    addRow->AddChild(valueInput);
    auto addAction = MakeButton("shcSceneActionAdd", "Add", ButtonKind::Secondary, 36);
    addAction->SetOnClick([this]() { AddAction(); });
    addRow->AddChild(addAction);
    editor->AddChild(addRow);

    auto buttons = Row("shcSceneEditButtons", 10);
    Fixed(buttons, -1, 40);
    buttons->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    buttons->AddChild(Spacer("shcSceneEditSpace"));
    auto cancel = MakeButton("shcSceneCancel", "Cancel", ButtonKind::Secondary);
    cancel->SetOnClick([this]() { CloseEditor(); });
    buttons->AddChild(cancel);
    auto save = MakeButton("shcSceneSave", "Save scene", ButtonKind::Primary);
    save->SetOnClick([this]() { Save(); });
    buttons->AddChild(save);
    editor->AddChild(buttons);
    editor->SetVisible(false);
    page->AddChild(editor);
    return page;
}

void ScenesPage::Refresh() {
    RebuildCards();
}

std::string ScenesPage::Subtitle() const {
    const size_t n = SmartHomeAPI::Instance().GetScenes().size();
    return std::to_string(n) + (n == 1 ? " scene" : " scenes") + "  ·  one click sets many devices at once";
}

void ScenesPage::RebuildCards() {
    std::vector<SmartHomeScene> scenes = SmartHomeAPI::Instance().GetScenes();
    std::sort(scenes.begin(), scenes.end(), [](const SmartHomeScene& a, const SmartHomeScene& b) { return a.Name < b.Name; });
    std::string signature;
    for (const auto& s : scenes) signature += s.SceneId + "=" + s.Name + ":" + std::to_string(s.Actions.size()) + ";";
    if (signature == cardsSignature) return;
    cardsSignature = signature;

    cards->ClearChildren();
    if (scenes.empty()) {
        cards->AddChild(Paragraph("shcScenesEmpty",
            "No scenes yet. Set the home the way you like it and press \"Capture current state\", or build one with "
            "\"New scene\".", 14, kMuted, 2));
    }
    int index = 0;
    for (const SmartHomeScene& scene : scenes) {
        const std::string key = std::to_string(index++);
        auto card = Column("shcSceneCard_" + key, 8);
        Card(card, 16);
        Fixed(card, 280, 168);

        auto top = Row("shcSceneTop_" + key, 12);
        Fixed(top, -1, 40);
        top->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        auto badge = Row("shcSceneBadge_" + key, 0);
        badge->layout.SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
        badge->SetBorderRadius(11);
        badge->SetBackgroundColor(kAccentSoft);
        Fixed(badge, 40, 40);
        auto icon = CreateImageElement("shcSceneIcon_" + key, 22, 22);
        icon->LoadFromFile(host.IconsDir() + (scene.Icon.empty() ? std::string("scenes") : scene.Icon) + ".svg");
        icon->SetFitMode(ImageFitMode::Contain);
        icon->SetTintColor(kAccent);
        Fixed(icon, 22, 22);
        badge->AddChild(icon);
        top->AddChild(badge);
        auto names = Column("shcSceneNames_" + key, 0);
        names->AddChild(Text("shcSceneName_" + key, scene.Name, 15, kInk, true, 22));
        names->AddChild(Text("shcSceneCount_" + key, std::to_string(scene.Actions.size()) +
                             (scene.Actions.size() == 1 ? " action" : " actions"), 12, kMuted, false, 16));
        Grow(names);
        top->AddChild(names);
        card->AddChild(top);

        // The first actions, as a person would say them.
        std::string summary;
        for (size_t i = 0; i < scene.Actions.size() && i < 3; ++i) {
            const SmartHomeCommand& a = scene.Actions[i];
            summary += (i ? ", " : "") + (a.DeviceId.empty() ? DescribeAction(a) : NameOf(a.DeviceId) + ": " + DescribeAction(a));
        }
        if (scene.Actions.size() > 3) summary += " …";
        card->AddChild(Paragraph("shcSceneSummary_" + key, summary.empty() ? "Does nothing yet" : summary, 12, kMuted, 2));

        auto buttons = Row("shcSceneButtons_" + key, 8);
        Fixed(buttons, -1, 32);
        buttons->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        auto run = MakeButton("shcSceneRun_" + key, "Run", ButtonKind::Primary, 32);
        run->SetIcon(host.IconsDir() + "play.svg");
        run->SetIconSize(14, 14);
        run->SetIconSpacing(6);
        const std::string id = scene.SceneId, name = scene.Name;
        run->SetOnClick([id]() { SmartHomeAPI::Instance().ActivateScene(id); });
        buttons->AddChild(run);
        buttons->AddChild(Spacer("shcSceneSpace_" + key));
        auto edit = MakeButton("shcSceneEdit_" + key, "Edit", ButtonKind::Secondary, 32);
        edit->SetOnClick([this, id]() {
            const SmartHomeScene current = SmartHomeAPI::Instance().GetScene(id);
            if (!current.SceneId.empty()) Edit(current, false);
        });
        buttons->AddChild(edit);
        auto remove = MakeButton("shcSceneDelete_" + key, "", ButtonKind::Quiet, 32);
        remove->SetIcon(host.IconsDir() + "trash.svg");
        remove->SetIconSize(16, 16);
        remove->SetPadding(0, 8);
        remove->SetTooltip("Delete the scene");
        remove->SetOnClick([this, id, name]() {
            UltraCanvasDialogManager::ShowConfirmation("Delete the scene \"" + name + "\"?", "Delete scene",
                [this, id, name](bool confirmed) {
                    if (!confirmed) return;
                    if (SmartHomeAPI::Instance().DeleteScene(id)) host.Note("Deleted the scene \"" + name + "\"");
                    if (editor->IsVisible() && editing.SceneId == id) CloseEditor();
                    RebuildCards();
                }, host.DialogParent());
        });
        buttons->AddChild(remove);
        card->AddChild(buttons);
        cards->AddChild(card);
    }
    cards->InvalidateLayout();
}

void ScenesPage::Edit(const SmartHomeScene& scene, bool isNew) {
    editing = scene;
    editingNew = isNew;
    editorTitle->SetText(isNew ? "New scene" : "Edit scene");
    nameInput->SetText(scene.Name);
    actionsModel->Set(editing.Actions);
    deviceChoice->ClearItems();
    deviceChoiceIds.clear();
    for (const DeviceView& v : host.Devices()) {
        if (!CanAct(v)) continue;
        deviceChoice->AddItem(v.info.Name + "  ·  " + RoomText(v.info.Room), v.info.DeviceId);
        deviceChoiceIds.push_back(v.info.DeviceId);
    }
    if (!deviceChoiceIds.empty()) deviceChoice->SetSelectedIndex(0, false);
    FillWhatChoices();
    editor->SetVisible(true);
    editor->GetParentContainer()->InvalidateLayout();
}

void ScenesPage::CloseEditor() {
    editor->SetVisible(false);
    editor->GetParentContainer()->InvalidateLayout();
}

void ScenesPage::FillWhatChoices() {
    whatChoice->ClearItems();
    const int index = deviceChoice->GetSelectedIndex();
    if (index < 0 || index >= static_cast<int>(deviceChoiceIds.size())) return;
    for (const DeviceView& v : host.Devices()) {
        if (v.info.DeviceId != deviceChoiceIds[static_cast<size_t>(index)]) continue;
        for (const ActionKind& a : ActionsFor(v)) {
            DropdownItem item;
            item.text = a.words;
            item.value = std::string(a.command) + "|" + a.param + "|" + a.fixedValue;
            item.tooltip = a.hint;   // "" = takes no value
            whatChoice->AddItem(item);
        }
    }
    if (whatChoice->GetItemCount() > 0) whatChoice->SetSelectedIndex(0, true);
}

void ScenesPage::AddAction() {
    const int deviceIndex = deviceChoice->GetSelectedIndex();
    const DropdownItem* what = whatChoice->GetSelectedItem();
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(deviceChoiceIds.size()) || !what) return;
    const std::string& v = what->value;
    const size_t a = v.find('|'), b = v.find('|', a + 1);
    const std::string command = v.substr(0, a), param = v.substr(a + 1, b - a - 1), fixed = v.substr(b + 1);
    SmartHomeCommand action{deviceChoiceIds[static_cast<size_t>(deviceIndex)], command, {}};
    if (!param.empty()) {
        std::string value = fixed;
        if (value.empty()) {
            // A person may type a comma; the command carries a dot.
            std::string typed = valueInput->GetText();
            std::replace(typed.begin(), typed.end(), ',', '.');
            double number = 0;
            if (!TryParseFloat(typed, number) || !std::isfinite(number)) {
                UltraCanvasDialogManager::ShowInformation("Type a number: " + what->tooltip + ".", "Add an action",
                                                          nullptr, host.DialogParent());
                return;
            }
            if (command == "setBrightness") number = std::clamp(number, 0.0, 100.0) * 255.0 / 100.0;
            if (command == "setPosition") number = std::clamp(number, 0.0, 100.0);
            value = command == "setTargetTemp" ? FormatFloatClassic(std::round(number * 2.0) / 2.0, 4)
                                               : std::to_string(static_cast<long>(std::lround(number)));
        }
        action.Parameters[param] = value;
    }
    editing.Actions.push_back(action);
    actionsModel->Set(editing.Actions);
    valueInput->SetText("");
}

void ScenesPage::RemoveAction() {
    const int row = actionsList->GetSelection() ? actionsList->GetSelection()->GetCurrentRow() : -1;
    if (row < 0 || row >= static_cast<int>(editing.Actions.size())) return;
    editing.Actions.erase(editing.Actions.begin() + row);
    actionsModel->Set(editing.Actions);
}

void ScenesPage::Save() {
    editing.Name = nameInput->GetText();
    if (editing.Name.empty()) {
        UltraCanvasDialogManager::ShowInformation("Give the scene a name.", "Save scene", nullptr, host.DialogParent());
        return;
    }
    auto& api = SmartHomeAPI::Instance();
    bool ok = false;
    if (editingNew) {
        // An id of its own that no scene has.
        std::string base = "scene.";
        for (char c : editing.Name) base += (std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '-');
        std::string id = base;
        for (int n = 2; !api.GetScene(id).SceneId.empty(); ++n) id = base + "-" + std::to_string(n);
        editing.SceneId = id;
        ok = api.CreateScene(editing);
    } else {
        ok = api.UpdateScene(editing);
    }
    if (!ok) {
        UltraCanvasDialogManager::ShowWarning("The scene could not be saved.", "Save scene", nullptr, host.DialogParent());
        return;
    }
    host.Note((editingNew ? "Created the scene \"" : "Saved the scene \"") + editing.Name + "\"");
    CloseEditor();
    RebuildCards();
}

} // namespace SmartHomeCenter
