// ui/UltraCanvasSmartHomePanel.cpp
// The Smart Home dashboard: the devices, rooms and scenes of the home in one
// panel, in whichever of the six layouts the user has chosen.
//
// The panel reads through SmartHomeAPI and never touches SmartHomeManager, per
// the layering in STATUS.md: the facade is the application-facing surface, and
// a widget is an application like any other.
//
// Cards are held in maps rather than added as child elements, because the panel
// lays them out itself and rebuilds them whenever the filter changes; it
// forwards events to them and renders them by hand.
//
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHomePanel.h"

#include <algorithm>
#include <cctype>

namespace UltraCanvas {
namespace SmartHome {

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

constexpr float kToolbarHeight = 40.0f;

}  // namespace

SmartHomePanel::SmartHomePanel(const std::string& identifier,
                               float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

SmartHomePanel::~SmartHomePanel() = default;

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
    // The module pushes; the panel just re-reads. Anything finer would have to
    // guess which of the six layouts is showing the changed device.
    SMARTHOME_API.SetOnDeviceUpdate([this](const std::string&) { UpdateCardStates(); });
    SMARTHOME_API.SetOnDeviceStateChange(
        [this](const std::string&, SmartHomeDeviceState) { UpdateCardStates(); });
    SMARTHOME_API.SetOnDeviceDiscover(
        [this](const SmartHomeDeviceInfo&) { RefreshDevices(); });
    SMARTHOME_API.SetOnPairingProgress([this](int progress, const std::string& status) {
        pairingProgress = progress;
        pairingStatus = status;
        RequestRedraw();
    });
    SMARTHOME_API.SetOnPairingComplete([this](bool, const SmartHomeDeviceInfo&) {
        pairingMode = false;
        RefreshDevices();
    });
}

void SmartHomePanel::RefreshDevices() {
    allDevices = SMARTHOME_API.GetDevices();
    ApplyFilter();

    // Rebuild the cards for whatever survived the filter, keeping any card that
    // is still on screen so its animation and pressed state are not reset.
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

    LayoutCards();
    RequestRedraw();
}

void SmartHomePanel::RefreshScenes() {
    scenes = SMARTHOME_API.GetScenes();
    sceneCards.clear();
    for (const auto& scene : scenes) {
        sceneCards[scene.SceneId] = CreateSceneCard(scene);
    }
    LayoutCards();
    RequestRedraw();
}

void SmartHomePanel::RefreshAutomations() {
    automations = SMARTHOME_API.GetAutomations();
    RequestRedraw();
}

// ===== CONFIGURATION =====

void SmartHomePanel::SetConfig(const SmartHomePanelConfig& c) {
    config = c;
    ApplyFilter();
    LayoutCards();
    RequestRedraw();
}

SmartHomePanelConfig SmartHomePanel::GetConfig() const { return config; }

void SmartHomePanel::SetMode(SmartHomePanelMode mode) {
    if (config.Mode == mode) return;
    config.Mode = mode;
    scrollOffset = 0;
    LayoutCards();
    if (onPanelModeChange) onPanelModeChange(mode);
    RequestRedraw();
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
    RequestRedraw();
}

void SmartHomePanel::SetCustomColors(const Color& background, const Color& cardBackground,
                                     const Color& text, const Color& accent) {
    config.Theme        = SmartHomePanelTheme::Custom;
    colorBackground     = background;
    colorCardBackground = cardBackground;
    colorText           = text;
    colorAccent         = accent;
    RequestRedraw();
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
    RequestRedraw();
}

void SmartHomePanel::RemoveRoom(const std::string& roomName) {
    rooms.erase(roomName);
    roomIcons.erase(roomName);
    for (auto it = deviceRooms.begin(); it != deviceRooms.end();) {
        it = (it->second == roomName) ? deviceRooms.erase(it) : std::next(it);
    }
    RequestRedraw();
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
    RequestRedraw();
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
    pairingStatus = "Searching for devices…";
    SMARTHOME_API.StartPairing(protocol);
    RequestRedraw();
}

void SmartHomePanel::CancelPairing() {
    SMARTHOME_API.StopPairing();
    pairingMode = false;
    pairingProgress = 0;
    pairingStatus.clear();
    RequestRedraw();
}

// ===== CARDS =====

std::shared_ptr<SmartHomeDeviceCard> SmartHomePanel::CreateDeviceCard(
        const SmartHomeDeviceInfo& device) {
    auto card = std::make_shared<SmartHomeDeviceCard>(
        GetIdentifier() + ".card." + device.DeviceId,
        0, 0, static_cast<float>(config.CardSize), static_cast<float>(config.CardSize));
    card->SetDevice(device);
    card->SetShowName(config.ShowDeviceNames);
    card->SetShowStatus(config.ShowDeviceStatus);
    card->SetCompact(config.Mode == SmartHomePanelMode::DeviceList);

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
        GetIdentifier() + ".scene." + scene.SceneId, 0, 0, 140.0f, 48.0f);
    card->SetScene(scene);
    const std::string id = scene.SceneId;
    card->SetOnActivate([this, id] {
        SMARTHOME_API.ActivateScene(id);
        if (onSceneActivate) onSceneActivate(id);
        RefreshScenes();
    });
    return card;
}

void SmartHomePanel::LayoutCards() {
    const Rect2Df bounds = GetLocalBounds();
    const float spacing = static_cast<float>(config.GridSpacing);
    const float top = kToolbarHeight + spacing;

    if (config.Mode == SmartHomePanelMode::SceneView) {
        float y = top;
        for (const auto& scene : scenes) {
            auto it = sceneCards.find(scene.SceneId);
            if (it == sceneCards.end()) continue;
            it->second->SetBounds(Rect2Df(spacing, y - scrollOffset,
                                          bounds.width - 2 * spacing, 48.0f));
            y += 48.0f + spacing;
        }
        maxScroll = std::max(0.0f, y - bounds.height);
        return;
    }

    if (config.Mode == SmartHomePanelMode::DeviceList) {
        float y = top;
        const float rowHeight = 56.0f;
        for (const auto& device : filteredDevices) {
            auto it = deviceCards.find(device.DeviceId);
            if (it == deviceCards.end()) continue;
            it->second->SetBounds(Rect2Df(spacing, y - scrollOffset,
                                          bounds.width - 2 * spacing, rowHeight));
            y += rowHeight + spacing;
        }
        maxScroll = std::max(0.0f, y - bounds.height);
        return;
    }

    // Grid. Zero columns means "as many as fit", which is what the config's
    // GridColumns == 0 default asks for.
    const float cardSize = static_cast<float>(config.CardSize);
    int columns = config.GridColumns;
    if (columns <= 0) {
        columns = std::max(1, static_cast<int>((bounds.width - spacing) /
                                               (cardSize + spacing)));
    }

    int index = 0;
    for (const auto& device : filteredDevices) {
        auto it = deviceCards.find(device.DeviceId);
        if (it == deviceCards.end()) continue;
        const int row = index / columns;
        const int col = index % columns;
        it->second->SetBounds(Rect2Df(spacing + col * (cardSize + spacing),
                                      top + row * (cardSize + spacing) - scrollOffset,
                                      cardSize, cardSize));
        ++index;
    }
    const int rows = (index + columns - 1) / columns;
    maxScroll = std::max(0.0f, top + rows * (cardSize + spacing) - bounds.height);
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
    if (config.AnimateTransitions) {
        it->second->AnimateState(newState);
    }
    cardAnimations[deviceId] = 0.0f;
    RequestRedraw();
}

// These six exist because the header declares them. Layout is driven by
// LayoutCards() and drawing by the Render* family, so there is nothing for a
// separate per-mode construction step to do.
void SmartHomePanel::CreateToolbar()        {}
void SmartHomePanel::CreateDeviceGrid()     { LayoutCards(); }
void SmartHomePanel::CreateDeviceList()     { LayoutCards(); }
void SmartHomePanel::CreateRoomView()       { LayoutCards(); }
void SmartHomePanel::CreateSceneView()      { LayoutCards(); }
void SmartHomePanel::CreateAutomationView() {}
void SmartHomePanel::CreateNetworkView()    {}

// ===== RENDERING =====

void SmartHomePanel::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx || !IsVisible()) return;

    const Rect2Df bounds = GetLocalBounds();
    ctx->SetFillPaint(colorBackground);
    ctx->FillRectangle(Rect2Dd(bounds));

    RenderToolbar(ctx);

    switch (config.Mode) {
        case SmartHomePanelMode::DeviceGrid:     RenderDeviceGrid(ctx);     break;
        case SmartHomePanelMode::DeviceList:     RenderDeviceList(ctx);     break;
        case SmartHomePanelMode::RoomView:       RenderRoomView(ctx);       break;
        case SmartHomePanelMode::SceneView:      RenderSceneView(ctx);      break;
        case SmartHomePanelMode::AutomationView: RenderAutomationView(ctx); break;
        case SmartHomePanelMode::NetworkView:    RenderNetworkView(ctx);    break;
    }

    if (pairingMode) RenderPairingOverlay(ctx);
}

void SmartHomePanel::RenderToolbar(IRenderContext* ctx) {
    const Rect2Df bounds = GetLocalBounds();
    ctx->SetFillPaint(colorCardBackground);
    ctx->FillRectangle(Rect2Dd(0.0, 0.0, bounds.width, kToolbarHeight));

    ctx->SetTextPaint(colorText);
    ctx->SetFontSize(13.0);
    ctx->DrawTextInRect("Smart Home",
                        Rect2Dd(12.0, 12.0, bounds.width * 0.5, 18.0));

    ctx->SetTextPaint(colorTextSecondary);
    ctx->SetFontSize(11.0);
    ctx->DrawTextInRect(std::to_string(filteredDevices.size()) + " of " +
                            std::to_string(allDevices.size()) + " devices",
                        Rect2Dd(bounds.width * 0.5, 13.0,
                                bounds.width * 0.5 - 12.0, 16.0));
}

void SmartHomePanel::RenderDeviceGrid(IRenderContext* ctx) {
    if (filteredDevices.empty()) { RenderEmptyState(ctx); return; }
    for (const auto& device : filteredDevices) {
        auto it = deviceCards.find(device.DeviceId);
        if (it != deviceCards.end()) it->second->Render(ctx, GetLocalBounds());
    }
}

void SmartHomePanel::RenderDeviceList(IRenderContext* ctx) { RenderDeviceGrid(ctx); }

void SmartHomePanel::RenderRoomView(IRenderContext* ctx) {
    if (rooms.empty()) { RenderEmptyState(ctx); return; }

    const Rect2Df bounds = GetLocalBounds();
    float y = kToolbarHeight + 8.0f - scrollOffset;
    for (const auto& [room, members] : rooms) {
        ctx->SetTextPaint(colorText);
        ctx->SetFontSize(12.0);
        ctx->DrawTextInRect(room + "  (" + std::to_string(members.size()) + ")",
                            Rect2Dd(12.0, y, bounds.width - 24.0, 18.0));
        y += 22.0f;

        for (const auto& deviceId : members) {
            auto it = deviceCards.find(deviceId);
            if (it == deviceCards.end()) continue;   // filtered out
            it->second->SetBounds(Rect2Df(20.0f, y, bounds.width - 40.0f, 48.0f));
            it->second->Render(ctx, bounds);
            y += 52.0f;
        }
        y += 10.0f;
    }
}

void SmartHomePanel::RenderSceneView(IRenderContext* ctx) {
    if (scenes.empty()) { RenderEmptyState(ctx); return; }
    for (const auto& scene : scenes) {
        auto it = sceneCards.find(scene.SceneId);
        if (it != sceneCards.end()) it->second->Render(ctx, GetLocalBounds());
    }
}

void SmartHomePanel::RenderAutomationView(IRenderContext* ctx) {
    if (automations.empty()) { RenderEmptyState(ctx); return; }

    const Rect2Df bounds = GetLocalBounds();
    float y = kToolbarHeight + 8.0f - scrollOffset;
    for (const auto& automation : automations) {
        ctx->SetFillPaint(colorCardBackground);
        ctx->FillRoundedRectangle(Rect2Dd(12.0, y, bounds.width - 24.0, 44.0), 6.0);

        ctx->SetTextPaint(colorText);
        ctx->SetFontSize(12.0);
        ctx->DrawTextInRect(automation.Name, Rect2Dd(22.0, y + 8.0, bounds.width - 120.0, 16.0));

        ctx->SetTextPaint(automation.Enabled ? Color(76, 175, 80) : colorTextSecondary);
        ctx->SetFontSize(11.0);
        ctx->DrawTextInRect(automation.Enabled ? "enabled" : "disabled",
                            Rect2Dd(bounds.width - 92.0, y + 9.0, 70.0, 16.0));
        y += 52.0f;
    }
}

void SmartHomePanel::RenderNetworkView(IRenderContext* ctx) {
    // The topology widget draws this; until it has an implementation the panel
    // says so rather than showing an empty area that looks like a failure.
    const Rect2Df bounds = GetLocalBounds();
    ctx->SetTextPaint(colorTextSecondary);
    ctx->SetFontSize(12.0);
    ctx->DrawTextInRect("Network view needs SmartHomeNetworkTopology",
                        Rect2Dd(0.0, bounds.height / 2.0 - 8.0, bounds.width, 18.0));
}

void SmartHomePanel::RenderEmptyState(IRenderContext* ctx) {
    const Rect2Df bounds = GetLocalBounds();
    const bool filtered = !allDevices.empty();

    ctx->SetTextPaint(colorTextSecondary);
    ctx->SetFontSize(12.0);
    ctx->DrawTextInRect(filtered ? "No devices match this filter"
                                 : "No devices yet — pair one to get started",
                        Rect2Dd(0.0, bounds.height / 2.0 - 8.0, bounds.width, 18.0));
}

void SmartHomePanel::RenderPairingOverlay(IRenderContext* ctx) {
    const Rect2Df bounds = GetLocalBounds();

    ctx->SetFillPaint(Color(0, 0, 0, 140));
    ctx->FillRectangle(Rect2Dd(bounds));

    const double w = std::min<double>(320.0, bounds.width - 40.0);
    const double h = 120.0;
    const double x = (bounds.width - w) / 2.0;
    const double y = (bounds.height - h) / 2.0;

    ctx->SetFillPaint(colorCardBackground);
    ctx->FillRoundedRectangle(Rect2Dd(x, y, w, h), 8.0);

    ctx->SetTextPaint(colorText);
    ctx->SetFontSize(13.0);
    ctx->DrawTextInRect("Pairing over " + ProtocolTypeToString(pairingProtocol),
                        Rect2Dd(x + 16.0, y + 16.0, w - 32.0, 18.0));

    ctx->SetTextPaint(colorTextSecondary);
    ctx->SetFontSize(11.0);
    ctx->DrawTextInRect(pairingStatus, Rect2Dd(x + 16.0, y + 42.0, w - 32.0, 16.0));

    // Progress bar.
    const double barW = w - 32.0;
    ctx->SetFillPaint(Color(0xE0, 0xE0, 0xE0));
    ctx->FillRoundedRectangle(Rect2Dd(x + 16.0, y + 72.0, barW, 6.0), 3.0);
    ctx->SetFillPaint(colorAccent);
    ctx->FillRoundedRectangle(
        Rect2Dd(x + 16.0, y + 72.0, barW * (std::clamp(pairingProgress, 0, 100) / 100.0), 6.0),
        3.0);
}

// ===== EVENTS =====

bool SmartHomePanel::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    switch (event.type) {
        case UCEventType::MouseWheel: {
            if (maxScroll <= 0.0f) return false;
            scrollOffset = std::clamp(scrollOffset - event.wheelDelta * 20.0f,
                                      0.0f, maxScroll);
            LayoutCards();
            RequestRedraw();
            return true;
        }

        case UCEventType::KeyDown:
            if (event.virtualKey == UCKeys::Escape && pairingMode) {
                CancelPairing();
                return true;
            }
            return false;

        default:
            break;
    }

    // While the pairing sheet is up it takes every pointer event, so a click
    // meant for the sheet cannot reach a card behind it.
    if (pairingMode) {
        return event.type == UCEventType::MouseDown ||
               event.type == UCEventType::MouseUp ||
               event.type == UCEventType::TouchStart ||
               event.type == UCEventType::TouchEnd;
    }

    // Cards are not child elements, so the panel hands events down itself.
    // Scene cards first: in SceneView they are the only thing on screen.
    if (config.Mode == SmartHomePanelMode::SceneView) {
        for (auto& [id, card] : sceneCards) {
            if (card->OnEvent(TranslatedTo(*card, event))) return true;
        }
        return false;
    }
    for (auto& [id, card] : deviceCards) {
        if (card->OnEvent(TranslatedTo(*card, event))) return true;
    }
    return false;
}

// A card hit-tests against its own local bounds, so the pointer has to arrive
// in the card's coordinates rather than the panel's.
UCEvent SmartHomePanel::TranslatedTo(const UltraCanvasUIElement& child,
                                     const UCEvent& event) const {
    UCEvent local = event;
    const Rect2Df childBounds = child.GetBounds();
    local.pointer.x = event.pointer.x - static_cast<int>(childBounds.x);
    local.pointer.y = event.pointer.y - static_cast<int>(childBounds.y);
    return local;
}

void SmartHomePanel::HandleDeviceClick(const std::string& deviceId, int, int) {
    SelectDevice(deviceId);
}

void SmartHomePanel::HandleDeviceLongPress(const std::string& deviceId) {
    ShowDeviceDetails(deviceId);
}

void SmartHomePanel::HandleDragStart(const std::string& deviceId, int x, int y) {
    if (!config.EnableDragDrop) return;
    draggingDeviceId = deviceId;
    dragOffsetX = x;
    dragOffsetY = y;
}

void SmartHomePanel::HandleDragMove(int x, int y) {
    if (draggingDeviceId.empty()) return;
    dragOffsetX = x;
    dragOffsetY = y;
    RequestRedraw();
}

void SmartHomePanel::HandleDragEnd(int, int) {
    if (draggingDeviceId.empty()) return;
    draggingDeviceId.clear();
    LayoutCards();
    RequestRedraw();
}

// ===== FACTORY =====

std::shared_ptr<UltraCanvasUIElement> CreateSmartHomePanelElement() {
    return std::make_shared<SmartHomePanel>("SmartHomePanel", 0, 0, 800, 600);
}

}  // namespace SmartHome
}  // namespace UltraCanvas
