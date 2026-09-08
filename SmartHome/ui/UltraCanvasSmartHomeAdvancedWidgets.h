// UltraCanvasSmartHomeAdvancedWidgets.h
// Smart Home Advanced UI Widgets
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasSmartHome.h"
#include <memory>
#include <functional>
#include <vector>
#include <map>

namespace UltraCanvas {
namespace SmartHome {

// ============================================================================
// SCENE EDITOR
// ============================================================================

/**
 * @brief Scene editor widget
 * 
 * Allows creating and editing scenes with multiple device actions.
 */
class SmartHomeSceneEditor : public UltraCanvasUIElement {
public:
    // UltraCanvasUIElement has no default constructor: every widget is
    // built with an id and its bounds, like the rest of the framework.
    SmartHomeSceneEditor(const std::string& identifier, float x, float y, float w, float h);
    SmartHomeSceneEditor(const std::string& identifier, float w, float h)
        : SmartHomeSceneEditor(identifier, -1, -1, w, h) {}
    explicit SmartHomeSceneEditor(const std::string& identifier)
        : SmartHomeSceneEditor(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomeSceneEditor();
    
    /**
     * @brief Create new scene
     */
    void NewScene();
    
    /**
     * @brief Edit existing scene
     * @param sceneId Scene ID
     */
    void EditScene(const std::string& sceneId);
    
    /**
     * @brief Get edited scene
     * @return Scene data
     */
    SmartHomeScene GetScene() const { return scene; }
    
    /**
     * @brief Set scene name
     * @param name Scene name
     */
    void SetSceneName(const std::string& name);
    
    /**
     * @brief Add device action to scene
     * @param deviceId Device ID
     * @param action Action name
     * @param params Action parameters
     */
    void AddAction(const std::string& deviceId, const std::string& action,
                   const std::map<std::string, std::string>& params);
    
    /**
     * @brief Remove action from scene
     * @param index Action index
     */
    void RemoveAction(size_t index);
    
    /**
     * @brief Set scene icon
     * @param icon Icon name
     */
    void SetIcon(const std::string& icon);
    
    void SetOnSave(std::function<void(const SmartHomeScene&)> callback) { onSave = callback; }
    void SetOnCancel(std::function<void()> callback) { onCancel = callback; }
    
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    void RenderHeader(IRenderContext* context);
    void RenderNameInput(IRenderContext* context);
    void RenderActionsList(IRenderContext* context);
    void RenderDeviceSelector(IRenderContext* context);
    void RenderActionButtons(IRenderContext* context);
    
    void ShowDeviceSelector();
    void AddDeviceToScene(const std::string& deviceId);
    void SaveScene();
    
    SmartHomeScene scene;
    std::vector<SmartHomeDeviceInfo> availableDevices;
    
    bool showingDeviceSelector = false;
    int selectedActionIndex = -1;
    std::string editingName;
    bool editingNameActive = false;
    
    float scrollOffset = 0;
    
    std::function<void(const SmartHomeScene&)> onSave;
    std::function<void()> onCancel;
};

// ============================================================================
// AUTOMATION EDITOR
// ============================================================================

/**
 * @brief Trigger type for automations
 */
enum class AutomationTriggerType {
    DeviceState,    ///< Device state change
    Time,           ///< Scheduled time
    Sunrise,        ///< At sunrise
    Sunset,         ///< At sunset
    Location,       ///< Location-based (geofence)
    Manual          ///< Manual trigger only
};

/**
 * @brief Condition type for automations
 */
enum class AutomationConditionType {
    DeviceState,    ///< Device in specific state
    TimeRange,      ///< Within time range
    DayOfWeek,      ///< Specific days
    SensorValue,    ///< Sensor threshold
    WeatherCondition ///< Weather-based
};

/**
 * @brief Automation trigger definition
 */
struct AutomationTrigger {
    AutomationTriggerType Type;
    std::string DeviceId;           ///< For DeviceState trigger
    std::string Attribute;          ///< Attribute to watch
    std::string Value;              ///< Trigger value
    std::string Time;               ///< For Time trigger (HH:MM)
    int SunOffset = 0;              ///< Minutes offset from sun event
};

/**
 * @brief Automation condition definition
 */
struct AutomationCondition {
    AutomationConditionType Type;
    std::string DeviceId;
    std::string Attribute;
    std::string Operator;           ///< =, !=, <, >, <=, >=
    std::string Value;
    std::string StartTime;          ///< For TimeRange
    std::string EndTime;
    std::vector<int> Days;          ///< For DayOfWeek (0-6)
};

/**
 * @brief Automation editor widget
 * 
 * Visual editor for creating automation rules with triggers, 
 * conditions, and actions.
 */
class SmartHomeAutomationEditor : public UltraCanvasUIElement {
public:
    // UltraCanvasUIElement has no default constructor: every widget is
    // built with an id and its bounds, like the rest of the framework.
    SmartHomeAutomationEditor(const std::string& identifier, float x, float y, float w, float h);
    SmartHomeAutomationEditor(const std::string& identifier, float w, float h)
        : SmartHomeAutomationEditor(identifier, -1, -1, w, h) {}
    explicit SmartHomeAutomationEditor(const std::string& identifier)
        : SmartHomeAutomationEditor(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomeAutomationEditor();
    
    /**
     * @brief Create new automation
     */
    void NewAutomation();
    
    /**
     * @brief Edit existing automation
     * @param automationId Automation ID
     */
    void EditAutomation(const std::string& automationId);
    
    /**
     * @brief Get edited automation
     * @return Automation data
     */
    SmartHomeAutomation GetAutomation() const { return automation; }
    
    /**
     * @brief Set automation name
     * @param name Name
     */
    void SetName(const std::string& name);
    
    /**
     * @brief Add trigger
     * @param trigger Trigger definition
     */
    void AddTrigger(const AutomationTrigger& trigger);
    
    /**
     * @brief Add condition
     * @param condition Condition definition
     */
    void AddCondition(const AutomationCondition& condition);
    
    /**
     * @brief Add action
     * @param action Scene action
     */
    void AddAction(const SmartHomeCommand& action);
    
    void SetOnSave(std::function<void(const SmartHomeAutomation&)> callback) { onSave = callback; }
    void SetOnCancel(std::function<void()> callback) { onCancel = callback; }
    
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    void RenderHeader(IRenderContext* context);
    void RenderTriggerSection(IRenderContext* context);
    void RenderConditionSection(IRenderContext* context);
    void RenderActionSection(IRenderContext* context);
    void RenderButtons(IRenderContext* context);
    
    void RenderTriggerEditor(IRenderContext* context);
    void RenderConditionEditor(IRenderContext* context);
    void RenderActionEditor(IRenderContext* context);
    
    SmartHomeAutomation automation;
    std::vector<AutomationTrigger> triggers;
    std::vector<AutomationCondition> conditions;
    
    int editingSection = 0;  // 0=none, 1=trigger, 2=condition, 3=action
    int editingIndex = -1;
    
    float scrollOffset = 0;
    
    std::function<void(const SmartHomeAutomation&)> onSave;
    std::function<void()> onCancel;
};

// ============================================================================
// NETWORK TOPOLOGY VIEW
// ============================================================================

/**
 * @brief Network node for topology visualization
 */
struct NetworkNode {
    std::string DeviceId;
    std::string Name;
    SmartHomeProtocolType Protocol;
    SmartHomeDeviceState State;
    int SignalStrength;         ///< RSSI or LQI
    float X, Y;                 ///< Position in view
    bool IsRouter;              ///< Router/coordinator role
    bool IsSelected;
};

/**
 * @brief Network link for topology visualization
 */
struct NetworkLink {
    std::string SourceId;
    std::string TargetId;
    int LinkQuality;
    bool IsActive;
};

/**
 * @brief Network topology visualization widget
 * 
 * Displays smart home network as interactive graph showing
 * devices, connections, and signal quality.
 */
class SmartHomeNetworkTopology : public UltraCanvasUIElement {
public:
    // UltraCanvasUIElement has no default constructor: every widget is
    // built with an id and its bounds, like the rest of the framework.
    SmartHomeNetworkTopology(const std::string& identifier, float x, float y, float w, float h);
    SmartHomeNetworkTopology(const std::string& identifier, float w, float h)
        : SmartHomeNetworkTopology(identifier, -1, -1, w, h) {}
    explicit SmartHomeNetworkTopology(const std::string& identifier)
        : SmartHomeNetworkTopology(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomeNetworkTopology();
    
    /**
     * @brief Refresh network data
     */
    void RefreshTopology();
    
    /**
     * @brief Set protocol filter
     * @param protocol Protocol to show (Unknown = all)
     */
    void SetProtocolFilter(SmartHomeProtocolType protocol);
    
    /**
     * @brief Set layout algorithm
     * @param algorithm Layout name ("force", "radial", "tree")
     */
    void SetLayout(const std::string& algorithm);
    
    /**
     * @brief Enable/disable auto-refresh
     * @param enable Enable flag
     * @param intervalMs Refresh interval in milliseconds
     */
    void SetAutoRefresh(bool enable, int intervalMs = 5000);
    
    /**
     * @brief Get selected node
     * @return Device ID of selected node (empty if none)
     */
    std::string GetSelectedNode() const { return selectedNodeId; }
    
    void SetOnNodeSelected(std::function<void(const std::string&)> callback) { 
        onNodeSelected = callback; 
    }
    
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    void LoadNetworkData();
    void CalculateLayout();
    void ApplyForceLayout();
    void ApplyRadialLayout();
    void ApplyTreeLayout();
    
    void RenderLinks(IRenderContext* context);
    void RenderNodes(IRenderContext* context);
    void RenderLegend(IRenderContext* context);
    void RenderNodeInfo(IRenderContext* context);
    
    Color GetProtocolColor(SmartHomeProtocolType protocol) const;
    Color GetSignalColor(int signal) const;
    
    std::vector<NetworkNode> nodes;
    std::vector<NetworkLink> links;
    
    SmartHomeProtocolType protocolFilter = SmartHomeProtocolType::Unknown;
    std::string layoutAlgorithm = "force";
    
    std::string selectedNodeId;
    std::string hoveredNodeId;
    std::string draggingNodeId;
    
    float viewScale = 1.0f;
    float viewOffsetX = 0;
    float viewOffsetY = 0;
    
    bool autoRefresh = false;
    int refreshInterval = 5000;
    uint64_t lastRefresh = 0;
    
    std::function<void(const std::string&)> onNodeSelected;
};

// ============================================================================
// ENERGY MONITOR
// ============================================================================

/**
 * @brief Energy reading data point
 */
struct EnergyReading {
    uint64_t Timestamp;
    float Power;            ///< Watts
    float Voltage;          ///< Volts
    float Current;          ///< Amps
    float Energy;           ///< kWh cumulative
};

/**
 * @brief Energy statistics
 */
struct EnergyStats {
    float TotalEnergyToday;     ///< kWh today
    float TotalEnergyMonth;     ///< kWh this month
    float AveragePower;         ///< Average watts
    float PeakPower;            ///< Peak watts
    float EstimatedCost;        ///< Estimated cost
};

/**
 * @brief Energy monitoring widget
 * 
 * Displays power consumption and energy usage with charts
 * and statistics.
 */
class SmartHomeEnergyMonitor : public UltraCanvasUIElement {
public:
    // UltraCanvasUIElement has no default constructor: every widget is
    // built with an id and its bounds, like the rest of the framework.
    SmartHomeEnergyMonitor(const std::string& identifier, float x, float y, float w, float h);
    SmartHomeEnergyMonitor(const std::string& identifier, float w, float h)
        : SmartHomeEnergyMonitor(identifier, -1, -1, w, h) {}
    explicit SmartHomeEnergyMonitor(const std::string& identifier)
        : SmartHomeEnergyMonitor(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomeEnergyMonitor();
    
    /**
     * @brief Set device to monitor
     * @param deviceId Device ID (empty for whole-home)
     */
    void SetDevice(const std::string& deviceId);
    
    /**
     * @brief Set time range for chart
     * @param hours Hours of history to show
     */
    void SetTimeRange(int hours);
    
    /**
     * @brief Set electricity cost rate
     * @param costPerKwh Cost per kWh
     * @param currency Currency symbol
     */
    void SetCostRate(float costPerKwh, const std::string& currency = "$");
    
    /**
     * @brief Add energy reading
     * @param reading Energy data point
     */
    void AddReading(const EnergyReading& reading);
    
    /**
     * @brief Get current statistics
     * @return Energy stats
     */
    EnergyStats GetStats() const { return stats; }
    
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    void RenderCurrentPower(IRenderContext* context);
    void RenderEnergyChart(IRenderContext* context);
    void RenderStatistics(IRenderContext* context);
    void RenderDeviceBreakdown(IRenderContext* context);
    
    void CalculateStats();
    
    std::string deviceId;
    std::vector<EnergyReading> readings;
    EnergyStats stats;
    
    int timeRangeHours = 24;
    float costPerKwh = 0.12f;
    std::string currency = "$";
    
    int chartMode = 0;  // 0=power, 1=energy, 2=cost
};

// ============================================================================
// SCHEDULER
// ============================================================================

/**
 * @brief Scheduled event
 */
struct ScheduledEvent {
    std::string EventId;
    std::string Name;
    std::string DeviceId;           ///< Device or scene ID
    bool IsScene;                   ///< True if scene, false if device
    std::string Action;
    std::map<std::string, std::string> Params;
    std::string Time;               ///< HH:MM
    std::vector<int> Days;          ///< Days of week (0=Sun, 6=Sat)
    bool Enabled;
    uint64_t NextRun;               ///< Next scheduled run timestamp
};

/**
 * @brief Schedule editor/viewer widget
 * 
 * Calendar-style view for managing scheduled events.
 */
class SmartHomeScheduler : public UltraCanvasUIElement {
public:
    // UltraCanvasUIElement has no default constructor: every widget is
    // built with an id and its bounds, like the rest of the framework.
    SmartHomeScheduler(const std::string& identifier, float x, float y, float w, float h);
    SmartHomeScheduler(const std::string& identifier, float w, float h)
        : SmartHomeScheduler(identifier, -1, -1, w, h) {}
    explicit SmartHomeScheduler(const std::string& identifier)
        : SmartHomeScheduler(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomeScheduler();
    
    /**
     * @brief Refresh scheduled events
     */
    void RefreshSchedule();
    
    /**
     * @brief Add scheduled event
     * @param event Event data
     */
    void AddEvent(const ScheduledEvent& event);
    
    /**
     * @brief Remove scheduled event
     * @param eventId Event ID
     */
    void RemoveEvent(const std::string& eventId);
    
    /**
     * @brief Toggle event enabled state
     * @param eventId Event ID
     */
    void ToggleEvent(const std::string& eventId);
    
    /**
     * @brief Set view mode
     * @param mode View mode ("day", "week", "list")
     */
    void SetViewMode(const std::string& mode);
    
    void SetOnEventSelected(std::function<void(const std::string&)> callback) {
        onEventSelected = callback;
    }
    void SetOnEventCreate(std::function<void(const std::string&, int)> callback) {
        onEventCreate = callback;
    }
    
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    void RenderDayView(IRenderContext* context);
    void RenderWeekView(IRenderContext* context);
    void RenderListView(IRenderContext* context);
    void RenderTimeGrid(IRenderContext* context);
    void RenderEvents(IRenderContext* context);
    
    std::vector<ScheduledEvent> events;
    std::string viewMode = "week";
    
    int selectedDay = 0;        ///< 0-6 for week view
    std::string selectedEventId;
    
    float scrollOffset = 0;
    
    std::function<void(const std::string&)> onEventSelected;
    std::function<void(const std::string&, int)> onEventCreate;
};

// ============================================================================
// GROUP CONTROL
// ============================================================================

/**
 * @brief Device group for batch control
 */
struct DeviceGroup {
    std::string GroupId;
    std::string Name;
    std::string Icon;
    std::vector<std::string> DeviceIds;
    SmartHomeDeviceCategory PrimaryCategory;
};

/**
 * @brief Group control widget
 * 
 * Controls multiple devices simultaneously as a group.
 */
class SmartHomeGroupControl : public UltraCanvasUIElement {
public:
    // UltraCanvasUIElement has no default constructor: every widget is
    // built with an id and its bounds, like the rest of the framework.
    SmartHomeGroupControl(const std::string& identifier, float x, float y, float w, float h);
    SmartHomeGroupControl(const std::string& identifier, float w, float h)
        : SmartHomeGroupControl(identifier, -1, -1, w, h) {}
    explicit SmartHomeGroupControl(const std::string& identifier)
        : SmartHomeGroupControl(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomeGroupControl();
    
    /**
     * @brief Set group to control
     * @param groupId Group ID
     */
    void SetGroup(const std::string& groupId);
    
    /**
     * @brief Create new group
     * @param name Group name
     * @param deviceIds Device IDs to include
     */
    void CreateGroup(const std::string& name, const std::vector<std::string>& deviceIds);
    
    /**
     * @brief Add device to group
     * @param deviceId Device ID
     */
    void AddDevice(const std::string& deviceId);
    
    /**
     * @brief Remove device from group
     * @param deviceId Device ID
     */
    void RemoveDevice(const std::string& deviceId);
    
    /**
     * @brief Turn all devices on
     */
    void AllOn();
    
    /**
     * @brief Turn all devices off
     */
    void AllOff();
    
    /**
     * @brief Set brightness for all lights in group
     * @param brightness Brightness 0-100
     */
    void SetGroupBrightness(uint8_t brightness);
    
    /**
     * @brief Set color temperature for all lights
     * @param colorTemp Color temperature in Kelvin
     */
    void SetGroupColorTemp(uint16_t colorTemp);
    
    void SetOnGroupChanged(std::function<void(const DeviceGroup&)> callback) {
        onGroupChanged = callback;
    }
    
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    void RenderGroupHeader(IRenderContext* context);
    void RenderMasterControls(IRenderContext* context);
    void RenderDeviceList(IRenderContext* context);
    void RenderAddDeviceButton(IRenderContext* context);
    
    void UpdateGroupState();
    void SendGroupCommand(const std::string& command, 
                         const std::map<std::string, std::string>& params);
    
    DeviceGroup group;
    std::vector<SmartHomeDeviceInfo> devices;
    
    uint8_t masterBrightness = 100;
    uint16_t masterColorTemp = 4000;
    bool masterOn = false;
    
    int dragging = 0;
    float scrollOffset = 0;
    
    std::function<void(const DeviceGroup&)> onGroupChanged;
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeSceneEditorElement();
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeAutomationEditorElement();
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeNetworkTopologyElement();
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeEnergyMonitorElement();
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeSchedulerElement();
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeGroupControlElement();

} // namespace SmartHome
} // namespace UltraCanvas
