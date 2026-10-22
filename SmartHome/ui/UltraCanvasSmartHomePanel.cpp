// ui/UltraCanvasSmartHomePanel.cpp
// The Smart Home dashboard: the devices, rooms and scenes of the home in one
// panel, in whichever of the six layouts the user has chosen.
//
// The panel reads through SmartHomeAPI and never touches SmartHomeManager, per
// the layering in STATUS.md: the facade is the application-facing surface, and
// a widget is an application like any other.
//
// It is a container of framework elements: a toolbar (title, count, a
// segmented control for the mode, a search field, "Add device"), a pairing
// banner, and a scrolling body whose children are the device and scene cards
// themselves - laid out by flex in rows that wrap, given their events by the
// window like any element. The automations are a list view; the network is
// a SmartHomeNetworkTopology.
//
// The module calls back on its own threads. The panel only notes the change
// there and applies it in Poll(), on the UI thread, from an application
// timer: elements are not to be added or removed from another thread.
//
// Version: 2.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHomePanel.h"
#include "UltraCanvasSmartHomeAdvancedWidgets.h"
#include "SmartHomeWidgetKit.h"

#include "UltraCanvasApplication.h"
#include "Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h"

#include <algorithm>
#include <cctype>

namespace UltraCanvas {
namespace SmartHome {

using namespace Kit;

namespace {

std::string Lowered(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

template <typename T>
bool ContainsValue(const std::vector<T>& haystack, const T& needle) {
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

// A row of cards that wraps, in a column of its own: as a direct child of
// the scrolling body it is measured one line high.
std::shared_ptr<UltraCanvasContainer> Section(const std::shared_ptr<UltraCanvasContainer>& flow) {
    auto section = CreateContainer(flow->GetIdentifier() + "Section");
    AsColumn(*section, 0, 0);
    section->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    section->layoutItem.SetFlexShrink(0);
    section->AddChild(flow);
    return section;
}

const SmartHomePanelMode kModes[] = {
    SmartHomePanelMode::DeviceGrid, SmartHomePanelMode::RoomView, SmartHomePanelMode::DeviceList,
    SmartHomePanelMode::SceneView, SmartHomePanelMode::AutomationView, SmartHomePanelMode::NetworkView,
};

int ModeIndex(SmartHomePanelMode mode) {
    for (int i = 0; i < 6; ++i) if (kModes[i] == mode) return i;
    return 0;
}

}  // namespace

SmartHomePanel::SmartHomePanel(const std::string& identifier,
                               float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    layout.SetFlexColumn().SetFlexGap(0).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    Plain(*this);
    SetBackgroundColor(colorBackground);

    toolbar = Row(identifier + "_Toolbar", 10, 44);
    toolbar->SetPadding(0, 12);
    toolbar->SetBackgroundColor(colorCardBackground);
    countLabel = Label(identifier + "_Count", "", 11, colorTextSecondary);
    Size(*countLabel, 124, 19);
    // Six views; the host names the page, so the toolbar spends its width here.
    modeSelector = CreateSegmentedControl(identifier + "_Mode", 0, 0, 552, 28);
    SegmentedControlStyle modeStyle;
    modeStyle.fontSize = 11.0f;
    modeStyle.selectedColor = colorAccent;
    modeSelector->SetStyle(modeStyle);
    for (const char* m : {"Devices", "Rooms", "List", "Scenes", "Automations", "Network"}) modeSelector->AddSegment(m);
    modeSelector->SetWidthMode(SegmentWidthMode::FitContent);   // "Automations" needs more than "List"
    Size(*modeSelector, 552, 28);
    modeSelector->onSegmentSelected = [this](int index) {
        if (!syncing && index >= 0 && index < 6) SetMode(kModes[index]);
    };
    searchInput = CreateTextInput(identifier + "_Search", 0, 0, 160, 28);
    searchInput->SetPlaceholder("Search devices");
    Grow(*searchInput);
    searchInput->onTextChanged = [this](const std::string& text) { SearchDevices(text); };
    addButton = Kit::Button(identifier + "_Add", "Add device", true);
    Size(*addButton, 100, 28);
    addButton->SetOnClick([this]() { ShowAddDeviceWizard(); });
    toolbar->AddChild(countLabel);
    toolbar->AddChild(modeSelector);
    toolbar->AddChild(searchInput);
    toolbar->AddChild(addButton);
    AddChild(toolbar);

    pairingBanner = Row(identifier + "_Pairing", 10, 40);
    pairingBanner->SetPadding(0, 12);
    pairingBanner->SetBackgroundColor(Color(0xE3, 0xF2, 0xFD));
    pairingLabel = Label(identifier + "_PairingText", "", 12, colorText);
    Grow(*pairingLabel);
    auto bar = CreateGaugeDiagramElement(identifier + "_PairingProgress", 0, 0, 160, 8);
    bar->SetMode(GaugeMode::LinearBar);
    bar->SetOrientation(GaugeOrientation::Horizontal);
    bar->SetMinValue(0);
    bar->SetMaxValue(100);
    bar->SetShowLabels(false);
    bar->SetGaugeColor(colorAccent);
    bar->SetTrackColor(kTrack);
    bar->SetBackgroundColor(Color(0xE3, 0xF2, 0xFD));
    Size(*bar, 160, 8);
    pairingBar = bar;
    pairingCancel = Kit::Button(identifier + "_PairingCancel", "Cancel");
    Size(*pairingCancel, 80, 28);
    pairingCancel->SetOnClick([this]() { CancelPairing(); });
    pairingBanner->AddChild(pairingLabel);
    pairingBanner->AddChild(bar);
    pairingBanner->AddChild(pairingCancel);
    pairingBanner->SetVisible(false);
    AddChild(pairingBanner);

    body = CreateScrollableContainer(identifier + "_Body", 0, 0, 0, 0, false, false);
    ContainerStyle scroll = body->GetContainerStyle();
    scroll.autoShowHorizontalScrollbar = false;
    body->SetContainerStyle(scroll);
    body->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    body->SetPadding(static_cast<float>(config.GridSpacing));
    body->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(100));
    Grow(*body);
    AddChild(body);

    emptyLabel = Label(identifier + "_Empty", "", 12, colorTextSecondary);
    emptyLabel->SetAlignment(TextAlignment::Center);

    automationModel = std::make_shared<RowsModel>(std::vector<ListColumnDef>{{"Automation", 300}, {"", 90}});
    automationList = std::make_shared<UltraCanvasListView>(identifier + "_Automations", 0, 0, 300, 200);
    automationList->SetModel(automationModel);
    automationList->SetShowHeader(false);
    automationList->SetRowHeight(28);
    automationList->SetAccessibleName("Automations");
    automationList->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(200));
    // A double-click turns an automation on or off.
    automationList->onItemDoubleClicked = [this](int row) {
        if (row < 0 || row >= static_cast<int>(automations.size())) return;
        SmartHomeAutomation& a = automations[static_cast<size_t>(row)];
        a.Enabled = !a.Enabled;
        SMARTHOME_API.EnableAutomation(a.AutomationId, a.Enabled);
        if (onAutomationToggle) onAutomationToggle(a.AutomationId, a.Enabled);
        RefreshAutomations();
    };

    topology = std::make_shared<SmartHomeNetworkTopology>(identifier + "_Network");
    topology->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(200));
    Grow(*topology);
    topology->SetVisible(false);
    AddChild(topology);

    UpdateCounts();
}

SmartHomePanel::~SmartHomePanel() {
    if (listenerId != 0) SMARTHOME_API.RemoveListener(listenerId);
    if (pollTimer) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(pollTimer);
    }
}

// ===== LIFECYCLE =====

bool SmartHomePanel::Initialize() {
    if (!SMARTHOME_API.IsInitialized() && !SMARTHOME_API.Initialize()) {
        return false;
    }
    SetupCallbacks();
    RefreshDevices();
    RefreshScenes();
    RefreshAutomations();
    return true;
}

void SmartHomePanel::SetupCallbacks() {
    // The module pushes, from its own threads; the panel notes what changed
    // and Poll() re-reads on the UI thread. Anything finer would have to
    // guess which of the six layouts is showing the changed device.
    // A listener, not the SetOn* slots, so the application hosting the
    // panel keeps its own callbacks.
    SmartHomeListener l;
    l.DeviceUpdate = [this](const std::string&) { statesChanged = true; };
    l.DeviceStateChange = [this](const std::string&, SmartHomeDeviceState) { statesChanged = true; };
    l.DeviceDiscover = [this](const SmartHomeDeviceInfo&) { devicesChanged = true; };
    l.PairingProgress = [this](int progress, const std::string& status) {
        std::lock_guard<std::mutex> lock(pendingMutex);
        pendingProgress = progress;
        pendingStatus = status;
        pairingChanged = true;
    };
    l.PairingComplete = [this](bool, const SmartHomeDeviceInfo&) {
        std::lock_guard<std::mutex> lock(pendingMutex);
        pendingPairingDone = true;
        pairingChanged = true;
        devicesChanged = true;
    };
    if (listenerId != 0) SMARTHOME_API.RemoveListener(listenerId);
    listenerId = SMARTHOME_API.AddListener(std::move(l));
    if (!pollTimer) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
            pollTimer = app->StartTimer(500, /*periodic=*/true, [this](TimerId) { Poll(); });
        }
    }
}

void SmartHomePanel::Poll() {
    if (pairingChanged.exchange(false)) {
        std::lock_guard<std::mutex> lock(pendingMutex);
        pairingProgress = pendingProgress;
        pairingStatus = pendingStatus;
        if (pendingPairingDone) pairingMode = false;
        pendingPairingDone = false;
    }
    if (devicesChanged.exchange(false)) {
        statesChanged = false;
        RefreshDevices();
    } else if (statesChanged.exchange(false)) {
        UpdateCardStates();
    }
    ShowPairing();
}

void SmartHomePanel::RefreshDevices() {
    allDevices = SMARTHOME_API.GetDevices();
    ApplyFilter();

    // Keep the card of a device still shown, so its animation and pressed
    // state are not reset; make cards for the rest.
    std::map<std::string, std::shared_ptr<SmartHomeDeviceCard>> kept;
    for (const auto& device : filteredDevices) {
        auto it = deviceCards.find(device.DeviceId);
        if (it != deviceCards.end()) {
            it->second->SetDevice(device);
            kept[device.DeviceId] = it->second;
        } else {
            kept[device.DeviceId] = CreateDeviceCard(device);
        }
    }
    deviceCards.swap(kept);
    if (config.Mode == SmartHomePanelMode::NetworkView) topology->RefreshTopology();
    RebuildBody();
}

void SmartHomePanel::RefreshScenes() {
    scenes = SMARTHOME_API.GetScenes();
    sceneCards.clear();
    for (const auto& scene : scenes) {
        sceneCards[scene.SceneId] = CreateSceneCard(scene);
    }
    if (config.Mode == SmartHomePanelMode::SceneView) RebuildBody();
}

void SmartHomePanel::RefreshAutomations() {
    automations = SMARTHOME_API.GetAutomations();
    std::vector<std::vector<std::string>> rows;
    for (const auto& a : automations) rows.push_back({a.Name, a.Enabled ? "enabled" : "disabled"});
    automationModel->Set(std::move(rows));
    if (config.Mode == SmartHomePanelMode::AutomationView) RebuildBody();
}

// ===== CONFIGURATION =====

void SmartHomePanel::SetConfig(const SmartHomePanelConfig& c) {
    config = c;
    body->SetPadding(static_cast<float>(config.GridSpacing));
    for (auto& [id, card] : deviceCards) {
        card->SetShowName(config.ShowDeviceNames);
        card->SetShowStatus(config.ShowDeviceStatus);
    }
    RefreshDevices();
}

SmartHomePanelConfig SmartHomePanel::GetConfig() const { return config; }

void SmartHomePanel::SetMode(SmartHomePanelMode mode) {
    if (config.Mode == mode) return;
    config.Mode = mode;
    if (mode == SmartHomePanelMode::NetworkView) topology->RefreshTopology();
    RebuildBody();
    if (onPanelModeChange) onPanelModeChange(mode);
}

SmartHomePanelMode SmartHomePanel::GetMode() const { return config.Mode; }

void SmartHomePanel::SetTheme(SmartHomePanelTheme theme) {
    config.Theme = theme;
    if (theme == SmartHomePanelTheme::Dark) {
        colorBackground     = Color(0x21, 0x21, 0x21);
        colorCardBackground = Color(0x30, 0x30, 0x30);
        colorText           = Color(0xEE, 0xEE, 0xEE);
        colorTextSecondary  = Color(0x9E, 0x9E, 0x9E);
    } else if (theme == SmartHomePanelTheme::Light) {
        colorBackground     = Color(0xF5, 0xF5, 0xF5);
        colorCardBackground = Colors::White;
        colorText           = Color(0x21, 0x21, 0x21);
        colorTextSecondary  = Color(0x75, 0x75, 0x75);
    }
    // System and Custom leave the current colours alone: System because the
    // host decides, Custom because SetCustomColors already did.
    SetBackgroundColor(colorBackground);
    toolbar->SetBackgroundColor(colorCardBackground);
    countLabel->SetTextColor(colorTextSecondary);
    RequestRedraw();
}

void SmartHomePanel::SetCustomColors(const Color& background, const Color& cardBackground,
                                     const Color& text, const Color& accent) {
    config.Theme        = SmartHomePanelTheme::Custom;
    colorBackground     = background;
    colorCardBackground = cardBackground;
    colorText           = text;
    colorAccent         = accent;
    SetTheme(SmartHomePanelTheme::Custom);
}

// ===== FILTERING =====

void SmartHomePanel::SetFilter(const SmartHomeDeviceFilter& filter) {
    config.Filter = filter;
    RefreshDevices();
}

SmartHomeDeviceFilter SmartHomePanel::GetFilter() const { return config.Filter; }

void SmartHomePanel::FilterByCategory(SmartHomeDeviceCategory category) {
    config.Filter.Categories = {category};
    RefreshDevices();
}

void SmartHomePanel::FilterByRoom(const std::string& room) {
    config.Filter.Rooms = {room};
    RefreshDevices();
}

void SmartHomePanel::FilterByProtocol(SmartHomeProtocolType protocol) {
    config.Filter.Protocols = {protocol};
    RefreshDevices();
}

void SmartHomePanel::SearchDevices(const std::string& searchText) {
    config.Filter.SearchText = searchText;
    RefreshDevices();
}

void SmartHomePanel::ClearFilters() {
    config.Filter = SmartHomeDeviceFilter{};
    RefreshDevices();
}

void SmartHomePanel::ApplyFilter() {
    const SmartHomeDeviceFilter& f = config.Filter;
    const std::string needle = Lowered(f.SearchText);

    filteredDevices.clear();
    for (const auto& d : allDevices) {
        if (!f.Categories.empty() && !ContainsValue(f.Categories, d.Category)) continue;
        if (!f.Protocols.empty()  && !ContainsValue(f.Protocols,  d.Protocol)) continue;
        if (!f.States.empty()     && !ContainsValue(f.States,     d.State))    continue;

        if (!f.Rooms.empty()) {
            auto it = deviceRooms.find(d.DeviceId);
            if (it == deviceRooms.end() || !ContainsValue(f.Rooms, it->second)) continue;
        }
        if (!f.ShowOfflineDevices && d.State == SmartHomeDeviceState::Offline) continue;
        if (!needle.empty() && Lowered(d.Name).find(needle) == std::string::npos) continue;

        filteredDevices.push_back(d);
    }
    SortDevices();
}

void SmartHomePanel::SortDevices() {
    // Online first, then by name: a dashboard is read top-left first, and an
    // unreachable device is the least useful thing to put there.
    std::stable_sort(filteredDevices.begin(), filteredDevices.end(),
                     [](const SmartHomeDeviceInfo& a, const SmartHomeDeviceInfo& b) {
                         const bool aUp = a.State == SmartHomeDeviceState::Online;
                         const bool bUp = b.State == SmartHomeDeviceState::Online;
                         if (aUp != bUp) return aUp;
                         return a.Name < b.Name;
                     });
}

// ===== ROOMS =====

void SmartHomePanel::AddRoom(const std::string& roomName, const std::string& icon) {
    rooms.emplace(roomName, std::vector<std::string>{});
    if (!icon.empty()) roomIcons[roomName] = icon;
    if (config.Mode == SmartHomePanelMode::RoomView) RebuildBody();
}

void SmartHomePanel::RemoveRoom(const std::string& roomName) {
    rooms.erase(roomName);
    roomIcons.erase(roomName);
    for (auto it = deviceRooms.begin(); it != deviceRooms.end();) {
        it = (it->second == roomName) ? deviceRooms.erase(it) : std::next(it);
    }
    if (config.Mode == SmartHomePanelMode::RoomView) RebuildBody();
}

void SmartHomePanel::AssignDeviceToRoom(const std::string& deviceId,
                                        const std::string& roomName) {
    // Take it out of whichever room it was in first, or it ends up in both.
    auto previous = deviceRooms.find(deviceId);
    if (previous != deviceRooms.end()) {
        auto& members = rooms[previous->second];
        members.erase(std::remove(members.begin(), members.end(), deviceId), members.end());
    }
    deviceRooms[deviceId] = roomName;
    auto& members = rooms[roomName];
    if (!ContainsValue(members, deviceId)) members.push_back(deviceId);
    if (config.Mode == SmartHomePanelMode::RoomView) RebuildBody();
}

std::vector<std::string> SmartHomePanel::GetRooms() const {
    std::vector<std::string> names;
    names.reserve(rooms.size());
    for (const auto& [name, _] : rooms) names.push_back(name);
    return names;
}

std::vector<std::string> SmartHomePanel::GetDevicesInRoom(const std::string& roomName) const {
    auto it = rooms.find(roomName);
    return it == rooms.end() ? std::vector<std::string>{} : it->second;
}

// ===== DEVICE INTERACTION =====

void SmartHomePanel::SelectDevice(const std::string& deviceId) {
    if (selectedDeviceId == deviceId) return;
    if (auto it = deviceCards.find(selectedDeviceId); it != deviceCards.end()) {
        it->second->SetSelected(false);
    }
    selectedDeviceId = deviceId;
    if (auto it = deviceCards.find(deviceId); it != deviceCards.end()) {
        it->second->SetSelected(true);
    }
    if (onDeviceSelected) onDeviceSelected(deviceId);
    RequestRedraw();
}

std::string SmartHomePanel::GetSelectedDevice() const { return selectedDeviceId; }

void SmartHomePanel::ToggleDevice(const std::string& deviceId) {
    const bool wasOn = SMARTHOME_API.GetSwitchState(deviceId);
    SMARTHOME_API.SetSwitchState(deviceId, !wasOn);
    AnimateCardTransition(deviceId, !wasOn);
    if (onDeviceAction) onDeviceAction(deviceId, wasOn ? "off" : "on");
}

void SmartHomePanel::ShowDeviceDetails(const std::string& deviceId) {
    SelectDevice(deviceId);
    if (onDeviceAction) onDeviceAction(deviceId, "details");
}

// ===== PAIRING =====

void SmartHomePanel::ShowAddDeviceWizard() {
    if (onAddDevice) onAddDevice();
}

void SmartHomePanel::StartPairing(SmartHomeProtocolType protocol) {
    pairingMode = true;
    pairingProtocol = protocol;
    pairingProgress = 0;
    pairingStatus = "Searching for devices...";
    SMARTHOME_API.StartPairing(protocol);
    ShowPairing();
}

void SmartHomePanel::CancelPairing() {
    SMARTHOME_API.StopPairing();
    pairingMode = false;
    pairingProgress = 0;
    pairingStatus.clear();
    ShowPairing();
}

void SmartHomePanel::ShowPairing() {
    if (pairingBanner->IsVisible() != pairingMode) pairingBanner->SetVisible(pairingMode);
    if (!pairingMode) return;
    pairingLabel->SetText("Pairing over " + ProtocolTypeToString(pairingProtocol) +
                          (pairingStatus.empty() ? std::string() : " - " + pairingStatus));
    static_cast<UltraCanvasGaugeDiagramElement*>(pairingBar.get())->SetValue(std::clamp(pairingProgress, 0, 100));
}

// ===== CARDS =====

std::shared_ptr<SmartHomeDeviceCard> SmartHomePanel::CreateDeviceCard(
        const SmartHomeDeviceInfo& device) {
    auto card = std::make_shared<SmartHomeDeviceCard>(
        GetIdentifier() + "_Card_" + device.DeviceId,
        0, 0, static_cast<float>(config.CardSize), static_cast<float>(config.CardSize));
    card->SetDevice(device);
    card->SetShowName(config.ShowDeviceNames);
    card->SetShowStatus(config.ShowDeviceStatus);

    const std::string id = device.DeviceId;
    card->SetOnClick([this, id] {
        SelectDevice(id);
        if (config.EnableQuickActions) ToggleDevice(id);
    });
    card->SetOnLongPress([this, id] { HandleDeviceLongPress(id); });
    return card;
}

std::shared_ptr<SmartHomeSceneCard> SmartHomePanel::CreateSceneCard(
        const SmartHomeScene& scene) {
    auto card = std::make_shared<SmartHomeSceneCard>(
        GetIdentifier() + "_Scene_" + scene.SceneId, 0, 0, 160.0f, 48.0f);
    card->SetScene(scene);
    Size(*card, 160.0f, 48.0f);
    const std::string id = scene.SceneId;
    card->SetOnActivate([this, id] {
        SMARTHOME_API.ActivateScene(id);
        if (onSceneActivate) onSceneActivate(id);
    });
    return card;
}

SmartHomeDeviceCard* SmartHomePanel::GetDeviceCard(const std::string& deviceId) const {
    auto it = deviceCards.find(deviceId);
    return it == deviceCards.end() ? nullptr : it->second.get();
}

SmartHomeSceneCard* SmartHomePanel::GetSceneCard(const std::string& sceneId) const {
    auto it = sceneCards.find(sceneId);
    return it == sceneCards.end() ? nullptr : it->second.get();
}

std::shared_ptr<UltraCanvasContainer> SmartHomePanel::CardFlow(const std::string& id) {
    auto flow = CreateContainer(id);
    flow->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap)
        .SetFlexGap(static_cast<float>(config.GridSpacing));
    flow->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    flow->layoutItem.SetFlexShrink(0);
    Plain(*flow);
    flows.push_back(flow);
    return flow;
}

// The body for the mode: the cards in rows that wrap, a list of compact
// cards, a heading and a row of cards per room, the scenes, the
// automations' list - or, for the network, the topology instead of the body.
void SmartHomePanel::RebuildBody() {
    syncing = true;
    modeSelector->SetSelectedIndex(ModeIndex(config.Mode));
    syncing = false;
    UpdateCounts();

    const bool network = config.Mode == SmartHomePanelMode::NetworkView;
    body->SetVisible(!network);
    topology->SetVisible(network);
    for (auto& flow : flows) flow->ClearChildren();
    flows.clear();
    body->ClearChildren();
    if (network) { InvalidateLayout(); RequestRedraw(); return; }

    const float cardSize = static_cast<float>(config.CardSize);
    auto placeCard = [&](const std::shared_ptr<SmartHomeDeviceCard>& card, bool compact) {
        card->SetCompact(compact);
        if (compact) Height(*card, 56.0f);
        else         Size(*card, cardSize, cardSize);
    };
    auto empty = [&](const std::string& text) {
        emptyLabel->SetText(text);
        body->AddChild(emptyLabel);
    };
    const std::string noDevices = allDevices.empty() ? "No devices yet - pair one to get started"
                                                     : "No devices match this filter";

    switch (config.Mode) {
        case SmartHomePanelMode::DeviceGrid: {
            if (filteredDevices.empty()) { empty(noDevices); break; }
            auto flow = CardFlow(GetIdentifier() + "_Grid");
            for (const auto& device : filteredDevices) {
                auto it = deviceCards.find(device.DeviceId);
                if (it == deviceCards.end()) continue;
                placeCard(it->second, false);
                flow->AddChild(it->second);
            }
            body->AddChild(Section(flow));
            break;
        }
        case SmartHomePanelMode::DeviceList: {
            if (filteredDevices.empty()) { empty(noDevices); break; }
            for (const auto& device : filteredDevices) {
                auto it = deviceCards.find(device.DeviceId);
                if (it == deviceCards.end()) continue;
                placeCard(it->second, true);
                body->AddChild(it->second);
            }
            break;
        }
        case SmartHomePanelMode::RoomView: {
            // The panel's own rooms (AddRoom) when it was given some;
            // otherwise the home's, from the module.
            std::vector<std::pair<std::string, std::vector<std::string>>> grouped(rooms.begin(), rooms.end());
            if (grouped.empty()) {
                for (const auto& room : SMARTHOME_API.GetRooms()) {
                    std::vector<std::string> members;
                    for (const auto& device : filteredDevices)
                        if (device.RoomId == room.RoomId) members.push_back(device.DeviceId);
                    if (!members.empty()) grouped.emplace_back(room.Name, std::move(members));
                }
                std::vector<std::string> unplaced;
                for (const auto& device : filteredDevices)
                    if (device.RoomId.empty()) unplaced.push_back(device.DeviceId);
                if (!unplaced.empty()) grouped.emplace_back("Not in a room", std::move(unplaced));
            }
            int n = 0;
            for (const auto& [room, members] : grouped) {
                const std::string key = GetIdentifier() + "_Room" + std::to_string(n++);
                auto flow = CardFlow(key + "Cards");
                for (const auto& deviceId : members) {
                    auto it = deviceCards.find(deviceId);
                    if (it == deviceCards.end()) continue;   // filtered out
                    placeCard(it->second, false);
                    flow->AddChild(it->second);
                }
                if (flow->GetChildren().empty()) continue;
                body->AddChild(Label(key + "Title", room + "  (" + std::to_string(flow->GetChildren().size()) + ")",
                                     12, colorText, true));
                body->AddChild(Section(flow));
            }
            if (body->GetChildren().empty()) empty(noDevices);
            break;
        }
        case SmartHomePanelMode::SceneView: {
            if (scenes.empty()) { empty("No scenes yet"); break; }
            auto flow = CardFlow(GetIdentifier() + "_Scenes");
            for (const auto& scene : scenes) {
                auto it = sceneCards.find(scene.SceneId);
                if (it != sceneCards.end()) flow->AddChild(it->second);
            }
            body->AddChild(Section(flow));
            break;
        }
        case SmartHomePanelMode::AutomationView:
            if (automations.empty()) { empty("No automations yet"); break; }
            Grow(*automationList);
            body->AddChild(automationList);
            break;
        case SmartHomePanelMode::NetworkView:
            break;
    }
    InvalidateLayout();
    RequestRedraw();
}

void SmartHomePanel::UpdateCounts() {
    countLabel->SetText(std::to_string(filteredDevices.size()) + " of " +
                        std::to_string(allDevices.size()) + " devices");
}

void SmartHomePanel::UpdateCardStates() {
    for (auto& [id, card] : deviceCards) {
        card->SetDevice(SMARTHOME_API.GetDevice(id));
        card->UpdateState();
    }
    RequestRedraw();
}

void SmartHomePanel::AnimateCardTransition(const std::string& deviceId, bool newState) {
    auto it = deviceCards.find(deviceId);
    if (it == deviceCards.end()) return;
    if (config.AnimateTransitions) it->second->AnimateState(newState);
    RequestRedraw();
}

// ===== EVENTS =====

bool SmartHomePanel::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (event.type == UCEventType::KeyDown && event.virtualKey == UCKeys::Escape && pairingMode) {
        CancelPairing();
        return true;
    }
    return UltraCanvasContainer::OnEvent(event);
}

void SmartHomePanel::HandleDeviceLongPress(const std::string& deviceId) {
    ShowDeviceDetails(deviceId);
}

// ===== FACTORY =====

std::shared_ptr<UltraCanvasUIElement> CreateSmartHomePanelElement() {
    return std::make_shared<SmartHomePanel>("SmartHomePanel", 0, 0, 800, 600);
}

}  // namespace SmartHome
}  // namespace UltraCanvas
