// UltraCanvasSmartHomePanel.h
// Smart Home Control Panel UI Component
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasSmartHome.h"
#include <memory>
#include <vector>
#include <functional>

namespace UltraCanvas {
namespace SmartHome {

// Forward declarations
class SmartHomeDeviceCard;
class SmartHomeSceneCard;
class SmartHomeRoomView;

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
class SmartHomePanel : public UltraCanvasUIElement {
public:
    // UltraCanvasUIElement has no default constructor: every widget is
    // built with an id and its bounds, like the rest of the framework.
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
    
    // ===== UltraCanvasUIElement OVERRIDES =====
    
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

protected:
    // ===== INTERNAL METHODS =====
    
    void SetupCallbacks();
    void CreateToolbar();
    void CreateDeviceGrid();
    void CreateDeviceList();
    void CreateRoomView();
    void CreateSceneView();
    void CreateAutomationView();
    void CreateNetworkView();
    
    void LayoutCards();
    void UpdateCardStates();
    void AnimateCardTransition(const std::string& deviceId, bool newState);
    
    void ApplyFilter();
    void SortDevices();
    
    void RenderToolbar(IRenderContext* context);
    void RenderDeviceGrid(IRenderContext* context);
    void RenderDeviceList(IRenderContext* context);
    void RenderRoomView(IRenderContext* context);
    void RenderSceneView(IRenderContext* context);
    void RenderAutomationView(IRenderContext* context);
    void RenderNetworkView(IRenderContext* context);
    void RenderEmptyState(IRenderContext* context);
    void RenderPairingOverlay(IRenderContext* context);
    
    std::shared_ptr<SmartHomeDeviceCard> CreateDeviceCard(const SmartHomeDeviceInfo& device);
    std::shared_ptr<SmartHomeSceneCard> CreateSceneCard(const SmartHomeScene& scene);
    
    // Cards are laid out by this panel rather than added as child elements,
    // so events must be rebased into a card's own coordinates before it can
    // hit-test them against its local bounds.
    UCEvent TranslatedTo(const UltraCanvasUIElement& child, const UCEvent& event) const;

    void HandleDeviceClick(const std::string& deviceId, int x, int y);
    void HandleDeviceLongPress(const std::string& deviceId);
    void HandleDragStart(const std::string& deviceId, int x, int y);
    void HandleDragMove(int x, int y);
    void HandleDragEnd(int x, int y);
    
    // ===== STATE =====
    
    SmartHomePanelConfig config;
    
    std::vector<SmartHomeDeviceInfo> allDevices;
    std::vector<SmartHomeDeviceInfo> filteredDevices;
    std::vector<SmartHomeScene> scenes;
    std::vector<SmartHomeAutomation> automations;
    
    std::map<std::string, std::shared_ptr<SmartHomeDeviceCard>> deviceCards;
    std::map<std::string, std::shared_ptr<SmartHomeSceneCard>> sceneCards;
    
    std::string selectedDeviceId;
    std::string hoveredDeviceId;
    std::string draggingDeviceId;
    int dragOffsetX = 0, dragOffsetY = 0;
    
    // Room organization
    std::map<std::string, std::vector<std::string>> rooms;  // room -> deviceIds
    std::map<std::string, std::string> deviceRooms;          // deviceId -> room
    std::map<std::string, std::string> roomIcons;            // room -> icon
    
    // Scroll state
    float scrollOffset = 0;
    float scrollVelocity = 0;
    float maxScroll = 0;
    
    // Pairing state
    bool pairingMode = false;
    SmartHomeProtocolType pairingProtocol = SmartHomeProtocolType::Unknown;
    int pairingProgress = 0;
    std::string pairingStatus;
    
    // Theme colors
    Color colorBackground = Color::FromRGBA(0xF5F5F5FF);
    Color colorCardBackground = Color::FromRGBA(0xFFFFFFFF);
    Color colorText = Color::FromRGBA(0x212121FF);
    Color colorTextSecondary = Color::FromRGBA(0x757575FF);
    Color colorAccent = Color::FromRGBA(0x2196F3FF);
    Color colorOnline = Color::FromRGBA(0x4CAF50FF);
    Color colorOffline = Color::FromRGBA(0x9E9E9EFF);
    Color colorError = Color::FromRGBA(0xF44336FF);
    
    // Callbacks
    OnDeviceSelected onDeviceSelected;
    OnDeviceAction onDeviceAction;
    OnSceneActivate onSceneActivate;
    OnAutomationToggle onAutomationToggle;
    OnAddDevice onAddDevice;
    OnPanelModeChange onPanelModeChange;
    
    // Animation state
    std::map<std::string, float> cardAnimations;  // deviceId -> animation progress
    
    // Touch handling
    int activeTouchId = -1;
    float touchStartX = 0, touchStartY = 0;
    float touchLastX = 0, touchLastY = 0;
    bool touchMoved = false;
    uint64_t touchStartTime = 0;
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
