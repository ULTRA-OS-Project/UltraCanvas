// UltraCanvasSmartHomePanel.h
// Smart Home Control Panel UI Component
// Version: 2.0.0 - a container of framework elements: a toolbar (mode,
//                  search, add), the cards as real children in a scrolling
//                  body, a pairing banner, the network as a topology widget
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasSmartHome.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasTextInput.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <functional>

namespace UltraCanvas {
namespace SmartHome {

// Forward declarations
class SmartHomeDeviceCard;
class SmartHomeSceneCard;
class SmartHomeNetworkTopology;
namespace Kit { class RowsModel; }

/**
 * @brief Panel display mode
 */
enum class SmartHomePanelMode {
    DeviceGrid,         ///< Grid of device cards
    DeviceList,         ///< List view of devices
    RoomView,           ///< Organized by rooms
    SceneView,          ///< Scene cards
    AutomationView,     ///< Automation rules
    NetworkView         ///< Network topology
};

/**
 * @brief Panel theme
 */
enum class SmartHomePanelTheme {
    Light,              ///< Light theme
    Dark,               ///< Dark theme
    System,             ///< Follow system theme
    Custom              ///< Custom colors
};

/**
 * @brief Device filter options
 */
struct SmartHomeDeviceFilter {
    std::vector<SmartHomeDeviceCategory> Categories;    ///< Category filter
    std::vector<SmartHomeProtocolType> Protocols;       ///< Protocol filter
    std::vector<SmartHomeDeviceState> States;           ///< State filter
    std::vector<std::string> Rooms;                     ///< Room filter
    std::string SearchText;                             ///< Text search
    bool ShowOfflineDevices = true;                     ///< Show offline devices
    bool ShowUnconfigured = true;                       ///< Show unconfigured devices
};

/**
 * @brief Panel configuration
 */
struct SmartHomePanelConfig {
    SmartHomePanelMode Mode = SmartHomePanelMode::DeviceGrid;
    SmartHomePanelTheme Theme = SmartHomePanelTheme::System;
    int CardSize = 120;                     ///< Device card size (pixels)
    int GridColumns = 0;                    ///< Grid columns (0 = auto)
    int GridSpacing = 16;                   ///< Spacing between cards
    bool ShowDeviceNames = true;            ///< Show device names
    bool ShowDeviceStatus = true;           ///< Show online/offline status
    bool ShowLastUpdated = false;           ///< Show last updated time
    bool EnableQuickActions = true;         ///< Enable quick toggle actions
    bool EnableDragDrop = true;             ///< Enable drag-drop reordering
    bool AnimateTransitions = true;         ///< Animate state changes
    SmartHomeDeviceFilter Filter;           ///< Default filter
};

/**
 * @brief Callbacks for panel events
 */
using OnDeviceSelected = std::function<void(const std::string& deviceId)>;
using OnDeviceAction = std::function<void(const std::string& deviceId, const std::string& action)>;
using OnSceneActivate = std::function<void(const std::string& sceneId)>;
using OnAutomationToggle = std::function<void(const std::string& automationId, bool enabled)>;
using OnAddDevice = std::function<void()>;
using OnPanelModeChange = std::function<void(SmartHomePanelMode mode)>;

/**
 * @brief Smart Home control panel UI component
 * 
 * Main dashboard panel for controlling smart home devices.
 * Displays devices in various layouts (grid, list, room-based),
 * provides quick actions, and shows real-time status updates.
 * 
 * Usage:
 * @code
 * auto panel = std::make_shared<SmartHomePanel>();
 * panel->SetBounds(0, 0, 800, 600);
 * panel->Initialize();
 * panel->RefreshDevices();
 * parent->AddChild(panel);
 * @endcode
 */
class SmartHomePanel : public UltraCanvasContainer {
public:
    SmartHomePanel(const std::string& identifier, float x, float y, float w, float h);
    SmartHomePanel(const std::string& identifier, float w, float h)
        : SmartHomePanel(identifier, -1, -1, w, h) {}
    explicit SmartHomePanel(const std::string& identifier)
        : SmartHomePanel(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomePanel();
    
    // ===== LIFECYCLE =====
    
    /**
     * @brief Initialize the panel
     * @return true if successful
     */
    bool Initialize();
    
    /**
     * @brief Refresh device list from SmartHomeAPI
     */
    void RefreshDevices();
    
    /**
     * @brief Refresh scenes list
     */
    void RefreshScenes();
    
    /**
     * @brief Refresh automations list
     */
    void RefreshAutomations();
    
    // ===== CONFIGURATION =====
    
    /**
     * @brief Set panel configuration
     * @param config Configuration options
     */
    void SetConfig(const SmartHomePanelConfig& config);
    
    /**
     * @brief Get current configuration
     * @return Current config
     */
    SmartHomePanelConfig GetConfig() const;
    
    /**
     * @brief Set display mode
     * @param mode Panel mode
     */
    void SetMode(SmartHomePanelMode mode);
    
    /**
     * @brief Get current mode
     * @return Current mode
     */
    SmartHomePanelMode GetMode() const;
    
    /**
     * @brief Set theme
     * @param theme Panel theme
     */
    void SetTheme(SmartHomePanelTheme theme);
    
    /**
     * @brief Set custom theme colors
     * @param background Background color
     * @param cardBackground Card background color
     * @param text Text color
     * @param accent Accent color
     */
    void SetCustomColors(const Color& background, const Color& cardBackground,
                         const Color& text, const Color& accent);
    
    // ===== FILTERING =====
    
    /**
     * @brief Set device filter
     * @param filter Filter options
     */
    void SetFilter(const SmartHomeDeviceFilter& filter);
    
    /**
     * @brief Get current filter
     * @return Current filter
     */
    SmartHomeDeviceFilter GetFilter() const;
    
    /**
     * @brief Filter by category
     * @param category Device category
     */
    void FilterByCategory(SmartHomeDeviceCategory category);
    
    /**
     * @brief Filter by room
     * @param room Room name
     */
    void FilterByRoom(const std::string& room);
    
    /**
     * @brief Filter by protocol
     * @param protocol Protocol type
     */
    void FilterByProtocol(SmartHomeProtocolType protocol);
    
    /**
     * @brief Search devices by name
     * @param searchText Search text
     */
    void SearchDevices(const std::string& searchText);
    
    /**
     * @brief Clear all filters
     */
    void ClearFilters();
    
    // ===== ROOM MANAGEMENT =====
    
    /**
     * @brief Add a room
     * @param roomName Room name
     * @param icon Room icon (optional)
     */
    void AddRoom(const std::string& roomName, const std::string& icon = "");
    
    /**
     * @brief Remove a room
     * @param roomName Room name
     */
    void RemoveRoom(const std::string& roomName);
    
    /**
     * @brief Assign device to room
     * @param deviceId Device ID
     * @param roomName Room name
     */
    void AssignDeviceToRoom(const std::string& deviceId, const std::string& roomName);
    
    /**
     * @brief Get all rooms
     * @return List of room names
     */
    std::vector<std::string> GetRooms() const;
    
    /**
     * @brief Get devices in room
     * @param roomName Room name
     * @return List of device IDs
     */
    std::vector<std::string> GetDevicesInRoom(const std::string& roomName) const;
    
    // ===== DEVICE INTERACTION =====
    
    /**
     * @brief Select a device
     * @param deviceId Device ID
     */
    void SelectDevice(const std::string& deviceId);
    
    /**
     * @brief Get selected device
     * @return Selected device ID (empty if none)
     */
    std::string GetSelectedDevice() const;
    
    /**
     * @brief Toggle device on/off
     * @param deviceId Device ID
     */
    void ToggleDevice(const std::string& deviceId);
    
    /**
     * @brief Show device details popup
     * @param deviceId Device ID
     */
    void ShowDeviceDetails(const std::string& deviceId);
    
    // ===== PAIRING =====
    
    /**
     * @brief Show add device wizard
     */
    void ShowAddDeviceWizard();
    
    /**
     * @brief Start device pairing
     * @param protocol Protocol to use
     */
    void StartPairing(SmartHomeProtocolType protocol);
    
    /**
     * @brief Cancel pairing
     */
    void CancelPairing();
    
    // ===== CALLBACKS =====
    
    void SetOnDeviceSelected(OnDeviceSelected callback) { onDeviceSelected = callback; }
    void SetOnDeviceAction(OnDeviceAction callback) { onDeviceAction = callback; }
    void SetOnSceneActivate(OnSceneActivate callback) { onSceneActivate = callback; }
    void SetOnAutomationToggle(OnAutomationToggle callback) { onAutomationToggle = callback; }
    void SetOnAddDevice(OnAddDevice callback) { onAddDevice = callback; }
    void SetOnPanelModeChange(OnPanelModeChange callback) { onPanelModeChange = callback; }
    
    // ===== UPDATES =====

    /**
     * @brief Apply what the module reported since the last call: devices
     * added or changed, pairing progress. The module calls back on its own
     * threads, so the panel only notes it there; its timer calls this on the
     * UI thread twice a second. A host without an event loop calls it itself.
     */
    void Poll();

    // ===== ELEMENTS =====
    // For a host that styles or drives them, and for tests.

    UltraCanvasSegmentedControl* GetModeSelector() const { return modeSelector.get(); }
    UltraCanvasTextInput* GetSearchInput() const { return searchInput.get(); }
    UltraCanvasButton* GetAddButton() const { return addButton.get(); }
    UltraCanvasButton* GetPairingCancelButton() const { return pairingCancel.get(); }
    SmartHomeDeviceCard* GetDeviceCard(const std::string& deviceId) const;
    SmartHomeSceneCard* GetSceneCard(const std::string& sceneId) const;
    UltraCanvasListView* GetAutomationList() const { return automationList.get(); }
    SmartHomeNetworkTopology* GetTopology() const { return topology.get(); }
    bool IsPairing() const { return pairingMode; }

    // Escape cancels pairing.
    bool OnEvent(const UCEvent& event) override;

protected:
    // ===== INTERNAL METHODS =====

    void SetupCallbacks();
    void RebuildBody();          // the body for the mode, from the filtered devices
    void UpdateCardStates();
    void UpdateCounts();
    void ShowPairing();
    void AnimateCardTransition(const std::string& deviceId, bool newState);
    void ApplyFilter();
    void SortDevices();

    std::shared_ptr<SmartHomeDeviceCard> CreateDeviceCard(const SmartHomeDeviceInfo& device);
    std::shared_ptr<SmartHomeSceneCard> CreateSceneCard(const SmartHomeScene& scene);
    std::shared_ptr<UltraCanvasContainer> CardFlow(const std::string& id);   // cards in rows that wrap

    void HandleDeviceLongPress(const std::string& deviceId);

    // ===== STATE =====

    SmartHomePanelConfig config;
    std::vector<SmartHomeDeviceInfo> allDevices;
    std::vector<SmartHomeDeviceInfo> filteredDevices;
    std::vector<SmartHomeScene> scenes;
    std::vector<SmartHomeAutomation> automations;

    std::map<std::string, std::shared_ptr<SmartHomeDeviceCard>> deviceCards;
    std::map<std::string, std::shared_ptr<SmartHomeSceneCard>> sceneCards;

    std::string selectedDeviceId;

    // Room organization (the panel's own rooms; none = the home's)
    std::map<std::string, std::vector<std::string>> rooms;  // room -> deviceIds
    std::map<std::string, std::string> deviceRooms;          // deviceId -> room
    std::map<std::string, std::string> roomIcons;            // room -> icon

    // Pairing state
    bool pairingMode = false;
    SmartHomeProtocolType pairingProtocol = SmartHomeProtocolType::Unknown;
    int pairingProgress = 0;
    std::string pairingStatus;

    // What the module reported from its threads, for Poll().
    std::atomic<bool> devicesChanged{false};
    std::atomic<bool> statesChanged{false};
    SmartHomeListenerId listenerId = 0;
    std::atomic<bool> pairingChanged{false};
    std::mutex pendingMutex;
    int pendingProgress = 0;
    std::string pendingStatus;
    bool pendingPairingDone = false;
    uint32_t pollTimer = 0;
    bool syncing = false;

    // Theme colors
    Color colorBackground = Color::FromRGBA(0xF5F5F5FF);
    Color colorCardBackground = Color::FromRGBA(0xFFFFFFFF);
    Color colorText = Color::FromRGBA(0x212121FF);
    Color colorTextSecondary = Color::FromRGBA(0x757575FF);
    Color colorAccent = Color::FromRGBA(0x2196F3FF);
    Color colorOnline = Color::FromRGBA(0x4CAF50FF);
    Color colorOffline = Color::FromRGBA(0x9E9E9EFF);
    Color colorError = Color::FromRGBA(0xF44336FF);

    // Elements
    std::shared_ptr<UltraCanvasContainer> toolbar;
    std::shared_ptr<UltraCanvasLabel> countLabel;
    std::shared_ptr<UltraCanvasSegmentedControl> modeSelector;
    std::shared_ptr<UltraCanvasTextInput> searchInput;
    std::shared_ptr<UltraCanvasButton> addButton;
    std::shared_ptr<UltraCanvasContainer> pairingBanner;
    std::shared_ptr<UltraCanvasLabel> pairingLabel;
    std::shared_ptr<UltraCanvasUIElement> pairingBar;
    std::shared_ptr<UltraCanvasButton> pairingCancel;
    std::shared_ptr<UltraCanvasContainer> body;              // scrolls
    // The rows of cards in the body. The cards outlive them (the maps keep
    // them), so they are emptied before they go: a card must not keep a
    // freed row as its parent.
    std::vector<std::shared_ptr<UltraCanvasContainer>> flows;
    std::shared_ptr<UltraCanvasLabel> emptyLabel;
    std::shared_ptr<Kit::RowsModel> automationModel;
    std::shared_ptr<UltraCanvasListView> automationList;
    std::shared_ptr<SmartHomeNetworkTopology> topology;

    // Callbacks
    OnDeviceSelected onDeviceSelected;
    OnDeviceAction onDeviceAction;
    OnSceneActivate onSceneActivate;
    OnAutomationToggle onAutomationToggle;
    OnAddDevice onAddDevice;
    OnPanelModeChange onPanelModeChange;
};

/**
 * @brief Device card widget
 * 
 * Individual card representing a smart home device.
 * Shows device icon, name, status, and provides quick actions.
 */
class SmartHomeDeviceCard : public UltraCanvasUIElement {
public:
    // UltraCanvasUIElement has no default constructor: every widget is
    // built with an id and its bounds, like the rest of the framework.
    SmartHomeDeviceCard(const std::string& identifier, float x, float y, float w, float h);
    SmartHomeDeviceCard(const std::string& identifier, float w, float h)
        : SmartHomeDeviceCard(identifier, -1, -1, w, h) {}
    explicit SmartHomeDeviceCard(const std::string& identifier)
        : SmartHomeDeviceCard(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomeDeviceCard();
    
    void SetDevice(const SmartHomeDeviceInfo& device);
    SmartHomeDeviceInfo GetDevice() const;
    
    void SetSelected(bool selected);
    bool IsSelected() const;
    
    void SetCompact(bool compact);
    void SetShowName(bool show);
    void SetShowStatus(bool show);
    
    void SetOnClick(std::function<void()> callback) { onClick = callback; }
    void SetOnLongPress(std::function<void()> callback) { onLongPress = callback; }
    void SetOnToggle(std::function<void(bool)> callback) { onToggle = callback; }
    
    void AnimateState(bool newState);
    void UpdateState();
    
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    void RenderLightCard(IRenderContext* context);
    void RenderSwitchCard(IRenderContext* context);
    void RenderThermostatCard(IRenderContext* context);
    void RenderLockCard(IRenderContext* context);
    void RenderSensorCard(IRenderContext* context);
    void RenderGenericCard(IRenderContext* context);
    
    std::string GetDeviceIcon() const;
    Color GetStateColor() const;
    
    SmartHomeDeviceInfo device;
    std::string iconPath;     // SmartHomeDeviceIconPath, worked out when the device is set
    bool selected = false;
    bool compact = false;
    bool showName = true;
    bool showStatus = true;
    bool pressed = false;
    float animationProgress = 0;
    bool animatingState = false;
    
    std::function<void()> onClick;
    std::function<void()> onLongPress;
    std::function<void(bool)> onToggle;
    
    uint64_t pressStartTime = 0;
};

/**
 * @brief Scene card widget
 */
class SmartHomeSceneCard : public UltraCanvasUIElement {
public:
    // UltraCanvasUIElement has no default constructor: every widget is
    // built with an id and its bounds, like the rest of the framework.
    SmartHomeSceneCard(const std::string& identifier, float x, float y, float w, float h);
    SmartHomeSceneCard(const std::string& identifier, float w, float h)
        : SmartHomeSceneCard(identifier, -1, -1, w, h) {}
    explicit SmartHomeSceneCard(const std::string& identifier)
        : SmartHomeSceneCard(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomeSceneCard();
    
    void SetScene(const SmartHomeScene& scene);
    SmartHomeScene GetScene() const;
    
    void SetOnActivate(std::function<void()> callback) { onActivate = callback; }
    
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    SmartHomeScene scene;
    bool pressed = false;
    std::function<void()> onActivate;
};

/**
 * @brief Factory function for SmartHomePanel
 */
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomePanelElement();

} // namespace SmartHome
} // namespace UltraCanvas
