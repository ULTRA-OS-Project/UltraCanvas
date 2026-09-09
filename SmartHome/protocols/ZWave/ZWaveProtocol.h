// ZWaveProtocol.h
// Z-Wave Protocol Implementation for UltraCanvas SmartHome
// Version: 1.0.0
// Last Modified: 2025-12-09
// Author: UltraCanvas Framework

#pragma once

#include "ISmartHomeProtocol.h"
#include "SmartHomeProtocolBase.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <chrono>

// Forward declarations for OpenZWave
#ifdef ULTRACANVAS_WITH_ZWAVE
namespace OpenZWave {
    class Manager;
    class Notification;
    class ValueID;
    class Options;
}
#endif

namespace UltraCanvas {
namespace SmartHome {

// ============================================================================
// Z-WAVE TYPES AND ENUMERATIONS
// ============================================================================

/**
 * @brief Z-Wave node types
 */
enum class ZWaveNodeType {
    Unknown,
    Controller,           ///< Primary/secondary controller
    StaticController,     ///< Static controller
    Slave,               ///< Routing slave
    RoutingSlave,        ///< Routing slave with routing capabilities
    EnhancedSlave        ///< Enhanced slave (Z-Wave Plus)
};

/**
 * @brief Z-Wave device roles
 */
enum class ZWaveDeviceRole {
    Unknown,
    CentralController,
    SubController,
    PortableController,
    ReportingController,
    PortableSlave,
    AlwaysOnSlave,
    SleepingReportingSlave,
    SleepingListeningSlave
};

/**
 * @brief Z-Wave security levels
 */
enum class ZWaveSecurityLevel {
    None,
    S0,                  ///< S0 security (legacy)
    S2Unauthenticated,   ///< S2 unauthenticated
    S2Authenticated,     ///< S2 authenticated
    S2AccessControl      ///< S2 access control (highest)
};

/**
 * @brief Z-Wave command classes
 */
enum class ZWaveCommandClass : uint8_t {
    NoOperation = 0x00,
    Basic = 0x20,
    ControllerReplication = 0x21,
    ApplicationStatus = 0x22,
    ZIPServices = 0x23,
    ZIPServer = 0x24,
    SwitchBinary = 0x25,
    SwitchMultilevel = 0x26,
    SwitchAll = 0x27,
    SwitchToggleBinary = 0x28,
    SwitchToggleMultilevel = 0x29,
    ChimneyFan = 0x2A,
    SceneActivation = 0x2B,
    SceneActuatorConf = 0x2C,
    SceneControllerConf = 0x2D,
    ZIPClient = 0x2E,
    ZIPAdvServices = 0x2F,
    SensorBinary = 0x30,
    SensorMultilevel = 0x31,
    Meter = 0x32,
    SwitchColor = 0x33,
    NetworkManagementInclusion = 0x34,
    MeterPulse = 0x35,
    BasicTariff = 0x36,
    HrvStatus = 0x37,
    HrvControl = 0x39,
    DCP = 0x3A,
    DCPConfig = 0x3B,
    MeterTableConfig = 0x3C,
    MeterTableMonitor = 0x3D,
    MeterTablePush = 0x3E,
    Prepayment = 0x3F,
    ThermostatMode = 0x40,
    PrepaymentEncapsulation = 0x41,
    ThermostatOperatingState = 0x42,
    ThermostatSetpoint = 0x43,
    ThermostatFanMode = 0x44,
    ThermostatFanState = 0x45,
    ClimateControlSchedule = 0x46,
    ThermostatSetback = 0x47,
    RateTblConfig = 0x48,
    RateTblMonitor = 0x49,
    TariffConfig = 0x4A,
    TariffTableMonitor = 0x4B,
    DoorLockLogging = 0x4C,
    NetworkManagementBasic = 0x4D,
    ScheduleEntryLock = 0x4E,
    ZIP6LoWPAN = 0x4F,
    BasicWindowCovering = 0x50,
    MtpWindowCovering = 0x51,
    NetworkManagementProxy = 0x52,
    Schedule = 0x53,
    NetworkManagementPrimary = 0x54,
    TransportService = 0x55,
    CRC16Encapsulation = 0x56,
    ApplicationCapability = 0x57,
    ZIPNaming = 0x58,
    Association = 0x85,
    AssociationGrpInfo = 0x59,
    Version = 0x86,
    Indicator = 0x87,
    Proprietary = 0x88,
    Language = 0x89,
    Time = 0x8A,
    TimeParameters = 0x8B,
    GeographicLocation = 0x8C,
    Configuration = 0x70,
    Alarm = 0x71,
    ManufacturerSpecific = 0x72,
    Powerlevel = 0x73,
    InclusionController = 0x74,
    Protection = 0x75,
    Lock = 0x76,
    NodeNaming = 0x77,
    FirmwareUpdate = 0x7A,
    GroupingName = 0x7B,
    RemoteAssociationActivate = 0x7C,
    RemoteAssociation = 0x7D,
    Battery = 0x80,
    Clock = 0x81,
    Hail = 0x82,
    WakeUp = 0x84,
    MultiChannel = 0x60,
    MultiChannelAssociation = 0x8E,
    DoorLock = 0x62,
    UserCode = 0x63,
    HumidityControlSetpoint = 0x64,
    DMX = 0x65,
    BarrierOperator = 0x66,
    NetworkManagementInstallationMaintenance = 0x67,
    ZIPGateway = 0x5F,
    ZIPPortal = 0x61,
    Mailbox = 0x69,
    WindowCovering = 0x6A,
    Irrigation = 0x6B,
    Supervision = 0x6C,
    HumidityControlMode = 0x6D,
    HumidityControlOperatingState = 0x6E,
    EntryControl = 0x6F,
    Security = 0x98,
    Security2 = 0x9F,
    SensorConfiguration = 0x9E,
    SimpleAvControl = 0x94,
    AvContentDirectoryMd = 0x95,
    AvRendererStatus = 0x96,
    AvContentSearchMd = 0x97,
    SilenceAlarm = 0x9D,
    SensorAlarm = 0x9C,
    MarkAntitheft = 0x5D,
    AntitheftUnlock = 0x7E,
    Antitheft = 0x5E,
    SoundSwitch = 0x79,
    CentralScene = 0x5B,
    IPAssociation = 0x5C,
    DeviceResetLocally = 0x5A,
    AntiTheft = 0x5D,
    ZWavePlusInfo = 0x5E,
    MultiCmd = 0x8F,
    MultiInstance = 0x60,
    EnergyProduction = 0x90,
    ManufacturerProprietary = 0x91,
    ScreenMd = 0x92,
    ScreenAttributes = 0x93,
    AuthenticationMediaWrite = 0xA1,
    Authentication = 0xA2,
    NonInteroperable = 0xF0
};

/**
 * @brief Z-Wave notification types
 */
enum class ZWaveNotificationType {
    ValueAdded,
    ValueRemoved,
    ValueChanged,
    ValueRefreshed,
    Group,
    NodeNew,
    NodeAdded,
    NodeRemoved,
    NodeProtocolInfo,
    NodeNaming,
    NodeEvent,
    PollingDisabled,
    PollingEnabled,
    SceneEvent,
    CreateButton,
    DeleteButton,
    ButtonOn,
    ButtonOff,
    DriverReady,
    DriverFailed,
    DriverReset,
    EssentialNodeQueriesComplete,
    NodeQueriesComplete,
    AwakeNodesQueried,
    AllNodesQueriedSomeDead,
    AllNodesQueried,
    Notification,
    DriverRemoved,
    ControllerCommand,
    NodeReset,
    UserAlerts,
    ManufacturerSpecificDBReady
};

/**
 * @brief Z-Wave value type
 */
enum class ZWaveValueType {
    Bool,
    Byte,
    Decimal,
    Int,
    List,
    Schedule,
    Short,
    String,
    Button,
    Raw,
    BitSet
};

/**
 * @brief Z-Wave value genre
 */
enum class ZWaveValueGenre {
    Basic,
    User,
    Config,
    System,
    Count
};

/**
 * @brief Z-Wave inclusion mode
 */
enum class ZWaveInclusionMode {
    Normal,
    NetworkWide,       ///< Network-wide inclusion (NWI)
    LowPower          ///< Low power inclusion
};

/**
 * @brief Z-Wave controller state
 */
enum class ZWaveControllerState {
    Normal,
    Starting,
    Cancel,
    Error,
    Waiting,
    Sleeping,
    InProgress,
    Completed,
    Failed,
    NodeOK,
    NodeFailed
};

// ============================================================================
// Z-WAVE VALUE REPRESENTATION
// ============================================================================

/**
 * @brief Represents a Z-Wave value
 */
struct ZWaveValue {
    uint32_t HomeId = 0;
    uint8_t NodeId = 0;
    ZWaveValueGenre Genre = ZWaveValueGenre::User;
    ZWaveCommandClass CommandClass = ZWaveCommandClass::NoOperation;
    uint8_t Instance = 0;
    uint16_t Index = 0;
    ZWaveValueType Type = ZWaveValueType::Bool;
    
    std::string Label;
    std::string Units;
    std::string Help;
    
    bool ReadOnly = false;
    bool WriteOnly = false;
    bool IsSet = false;
    bool IsPolled = false;
    
    // Value storage (use appropriate field based on Type)
    bool BoolValue = false;
    uint8_t ByteValue = 0;
    int32_t IntValue = 0;
    int16_t ShortValue = 0;
    float DecimalValue = 0.0f;
    std::string StringValue;
    std::vector<uint8_t> RawValue;
    std::vector<std::string> ListItems;
    int ListSelection = 0;
    
    // Unique identifier
    uint64_t GetId() const {
        return (static_cast<uint64_t>(HomeId) << 32) |
               (static_cast<uint64_t>(NodeId) << 24) |
               (static_cast<uint64_t>(CommandClass) << 16) |
               (static_cast<uint64_t>(Instance) << 8) |
               Index;
    }
};

/**
 * @brief Z-Wave node information
 */
struct ZWaveNode {
    uint32_t HomeId = 0;
    uint8_t NodeId = 0;
    
    ZWaveNodeType NodeType = ZWaveNodeType::Unknown;
    ZWaveDeviceRole Role = ZWaveDeviceRole::Unknown;
    ZWaveSecurityLevel Security = ZWaveSecurityLevel::None;
    
    std::string Name;
    std::string Location;
    std::string ManufacturerName;
    std::string ProductName;
    std::string ProductType;
    std::string ProductId;
    
    uint16_t ManufacturerId = 0;
    uint16_t ProductTypeId = 0;
    uint16_t ProductIdNum = 0;
    
    bool IsListening = false;      ///< Always on (not battery)
    bool IsFrequentListening = false;  ///< FLiRS device
    bool IsBeaming = false;        ///< Supports beaming
    bool IsRouting = false;        ///< Acts as router
    bool IsSecurityDevice = false;
    bool IsZWavePlus = false;
    
    bool IsAwake = false;
    bool IsFailed = false;
    bool IsReady = false;
    
    uint8_t BasicType = 0;
    uint8_t GenericType = 0;
    uint8_t SpecificType = 0;
    
    std::set<ZWaveCommandClass> CommandClasses;
    std::map<uint64_t, ZWaveValue> Values;
    
    // Neighbors
    std::vector<uint8_t> Neighbors;
    
    // Last communication
    std::chrono::system_clock::time_point LastSeen;
    std::chrono::system_clock::time_point LastWakeUp;
};

/**
 * @brief Z-Wave network statistics
 */
struct ZWaveNetworkStats {
    uint32_t HomeId = 0;
    uint8_t ControllerNodeId = 0;
    
    int TotalNodes = 0;
    int ActiveNodes = 0;
    int FailedNodes = 0;
    int SleepingNodes = 0;
    
    uint64_t MessagesSent = 0;
    uint64_t MessagesReceived = 0;
    uint64_t MessagesFailed = 0;
    
    std::chrono::system_clock::time_point LastActivity;
};

/**
 * @brief Z-Wave association group
 */
struct ZWaveAssociationGroup {
    uint8_t GroupIndex = 0;
    std::string Label;
    uint8_t MaxAssociations = 0;
    std::vector<uint8_t> Members;
    bool IsMultiChannel = false;
};

/**
 * @brief Z-Wave scene
 */
struct ZWaveScene {
    uint8_t SceneId = 0;
    std::string Label;
    std::vector<ZWaveValue> Values;
};

// ============================================================================
// CALLBACKS
// ============================================================================

using ZWaveNodeCallback = std::function<void(const ZWaveNode&)>;
using ZWaveValueCallback = std::function<void(const ZWaveValue&)>;
using ZWaveNotificationCallback = std::function<void(ZWaveNotificationType, const ZWaveNode*, const ZWaveValue*)>;
using ZWaveControllerCallback = std::function<void(ZWaveControllerState, const std::string&)>;
using ZWaveInclusionCallback = std::function<void(bool success, uint8_t nodeId, const std::string& message)>;

// ============================================================================
// Z-WAVE PROTOCOL CLASS
// ============================================================================

/**
 * @brief Z-Wave protocol implementation using OpenZWave
 * 
 * Provides complete Z-Wave network management including:
 * - Controller initialization and management
 * - Node inclusion/exclusion
 * - Command class support (switch, dimmer, thermostat, lock, sensors)
 * - Value read/write operations
 * - Network healing
 * - Security (S0/S2)
 * - Association groups
 * - Scenes
 */
class ZWaveProtocol : public SmartHomeProtocolBase {
public:
    ZWaveProtocol();
    virtual ~ZWaveProtocol();
    
    // ===== ISmartHomeProtocol Implementation =====
    
    bool Initialize() override;
    void Shutdown() override;
    bool IsInitialized() const override { return initialized; }
    
    SmartHomeProtocolType GetType() const override { return SmartHomeProtocolType::ZWave; }
    std::string GetName() const override { return "Z-Wave"; }
    std::string GetVersion() const override;
    
    bool StartDiscovery(int timeoutSeconds) override;
    void StopDiscovery() override;
    bool IsDiscovering() const override { return discovering; }
    std::vector<SmartHomeDeviceInfo> GetDiscoveredDevices() const override;
    
    bool PairDevice(const std::string& deviceId, 
                   const std::map<std::string, std::string>& params) override;
    bool UnpairDevice(const std::string& deviceId) override;
    
    bool SendCommand(const std::string& deviceId, const std::string& command,
                    const std::map<std::string, std::string>& params) override;
    
    bool GetDeviceState(const std::string& deviceId,
                       std::map<std::string, std::string>& state) override;
    
    std::vector<SmartHomeDeviceInfo> GetPairedDevices() override;
    
    // ===== Z-Wave Specific: Controller Management =====
    
    /**
     * @brief Set the controller device path
     * @param path Device path (e.g., "/dev/ttyUSB0", "/dev/ttyACM0")
     */
    void SetControllerPath(const std::string& path);
    
    /**
     * @brief Get the controller device path
     * @return Current controller path
     */
    std::string GetControllerPath() const { return controllerPath; }

    // ISmartHomeProtocol methods the backend had no declaration for.
    std::vector<SmartHomeDeviceCategory> GetSupportedDeviceCategories() const override;
    bool IsHardwareAvailable() const override;
    std::string GetHardwareInfo() const override;
    std::vector<std::string> GetAvailableAdapters() const override;
    bool SelectAdapter(const std::string& adapterId) override;
    bool FormNetwork(const std::string& networkName = "") override;
    bool JoinNetwork(const std::string& networkId) override;
    bool LeaveNetwork() override;
    NetworkTopology GetTopology() const override;

    // Pairing on Z-Wave is inclusion, and unpairing is exclusion.
    bool StartPairing(int timeoutSeconds = 60) override;
    void StopPairing() override;

    std::vector<std::shared_ptr<ISmartHomeDevice>> GetDevices() const override;
    std::shared_ptr<ISmartHomeDevice> GetDevice(const std::string& deviceId) const override;
    bool RemoveDevice(const std::string& deviceId) override;
    bool InterviewDevice(const std::string& deviceId) override;
    SmartHomeSecurityLevel GetSecurityLevel() const override;
    bool LoadConfig(const std::string& path) override;
    bool SaveConfig(const std::string& path) override;
    
    /**
     * @brief Set OpenZWave configuration path
     * @param path Path to OpenZWave config directory
     */
    void SetConfigPath(const std::string& path);
    
    /**
     * @brief Set user data path for persistence
     * @param path Path for storing network data
     */
    void SetUserPath(const std::string& path);
    
    /**
     * @brief Get home ID of the Z-Wave network
     * @return Home ID (0 if not initialized)
     */
    uint32_t GetHomeId() const { return homeId; }
    
    /**
     * @brief Get controller node ID
     * @return Node ID of the controller
     */
    uint8_t GetControllerNodeId() const { return controllerNodeId; }
    
    /**
     * @brief Check if controller is a primary controller
     * @return true if primary
     */
    bool IsPrimaryController() const;
    
    /**
     * @brief Check if controller is a static update controller (SUC)
     * @return true if SUC
     */
    bool IsStaticUpdateController() const;
    
    /**
     * @brief Soft reset the controller
     * @return true if successful
     */
    bool SoftResetController();
    
    /**
     * @brief Hard reset the controller (factory reset)
     * @return true if successful
     */
    bool HardResetController();
    
    // ===== Z-Wave Specific: Node Management =====
    
    /**
     * @brief Start inclusion mode to add new nodes
     * @param mode Inclusion mode
     * @param secure Use secure inclusion
     * @return true if inclusion started
     */
    bool StartInclusion(ZWaveInclusionMode mode = ZWaveInclusionMode::Normal,
                       bool secure = true);
    
    /**
     * @brief Stop inclusion mode
     * @return true if stopped
     */
    bool StopInclusion();
    
    /**
     * @brief Start exclusion mode to remove nodes
     * @return true if exclusion started
     */
    bool StartExclusion();
    
    /**
     * @brief Stop exclusion mode
     * @return true if stopped
     */
    bool StopExclusion();
    
    /**
     * @brief Check if a node has failed
     * @param nodeId Node ID
     * @return true if node is marked as failed
     */
    bool IsNodeFailed(uint8_t nodeId) const;
    
    /**
     * @brief Remove a failed node from the network
     * @param nodeId Node ID
     * @return true if removal started
     */
    bool RemoveFailedNode(uint8_t nodeId);
    
    /**
     * @brief Replace a failed node
     * @param nodeId Node ID to replace
     * @return true if replacement started
     */
    bool ReplaceFailedNode(uint8_t nodeId);
    
    /**
     * @brief Request node neighbor update
     * @param nodeId Node ID
     * @return true if request sent
     */
    bool RequestNodeNeighborUpdate(uint8_t nodeId);
    
    /**
     * @brief Refresh node information
     * @param nodeId Node ID
     * @return true if refresh started
     */
    bool RefreshNodeInfo(uint8_t nodeId);
    
    /**
     * @brief Request all configuration parameters
     * @param nodeId Node ID
     * @return true if request sent
     */
    bool RequestAllConfigParams(uint8_t nodeId);
    
    /**
     * @brief Get node information
     * @param nodeId Node ID
     * @return Node info (nullptr if not found)
     */
    const ZWaveNode* GetNode(uint8_t nodeId) const;
    
    /**
     * @brief Get all nodes
     * @return Map of node ID to node info
     */
    std::map<uint8_t, ZWaveNode> GetAllNodes() const;
    
    /**
     * @brief Set node name
     * @param nodeId Node ID
     * @param name New name
     * @return true if successful
     */
    bool SetNodeName(uint8_t nodeId, const std::string& name);
    
    /**
     * @brief Set node location
     * @param nodeId Node ID
     * @param location Location string
     * @return true if successful
     */
    bool SetNodeLocation(uint8_t nodeId, const std::string& location);
    
    // ===== Z-Wave Specific: Value Operations =====
    
    /**
     * @brief Get a value from a node
     * @param nodeId Node ID
     * @param commandClass Command class
     * @param instance Instance number
     * @param index Value index
     * @return Value (nullptr if not found)
     */
    const ZWaveValue* GetValue(uint8_t nodeId, ZWaveCommandClass commandClass,
                               uint8_t instance = 1, uint16_t index = 0) const;
    
    /**
     * @brief Set a boolean value
     * @param nodeId Node ID
     * @param commandClass Command class
     * @param instance Instance
     * @param index Index
     * @param value Value to set
     * @return true if successful
     */
    bool SetValueBool(uint8_t nodeId, ZWaveCommandClass commandClass,
                     uint8_t instance, uint16_t index, bool value);
    
    /**
     * @brief Set a byte value
     * @param nodeId Node ID
     * @param commandClass Command class
     * @param instance Instance
     * @param index Index
     * @param value Value to set
     * @return true if successful
     */
    bool SetValueByte(uint8_t nodeId, ZWaveCommandClass commandClass,
                     uint8_t instance, uint16_t index, uint8_t value);
    
    /**
     * @brief Set an integer value
     * @param nodeId Node ID
     * @param commandClass Command class
     * @param instance Instance
     * @param index Index
     * @param value Value to set
     * @return true if successful
     */
    bool SetValueInt(uint8_t nodeId, ZWaveCommandClass commandClass,
                    uint8_t instance, uint16_t index, int32_t value);
    
    /**
     * @brief Set a decimal (float) value
     * @param nodeId Node ID
     * @param commandClass Command class
     * @param instance Instance
     * @param index Index
     * @param value Value to set
     * @return true if successful
     */
    bool SetValueDecimal(uint8_t nodeId, ZWaveCommandClass commandClass,
                        uint8_t instance, uint16_t index, float value);
    
    /**
     * @brief Set a list selection value
     * @param nodeId Node ID
     * @param commandClass Command class
     * @param instance Instance
     * @param index Index
     * @param selection Selection string
     * @return true if successful
     */
    bool SetValueList(uint8_t nodeId, ZWaveCommandClass commandClass,
                     uint8_t instance, uint16_t index, const std::string& selection);
    
    /**
     * @brief Press a button value
     * @param nodeId Node ID
     * @param commandClass Command class
     * @param instance Instance
     * @param index Index
     * @return true if successful
     */
    bool PressButton(uint8_t nodeId, ZWaveCommandClass commandClass,
                    uint8_t instance, uint16_t index);
    
    /**
     * @brief Release a button value
     * @param nodeId Node ID
     * @param commandClass Command class
     * @param instance Instance
     * @param index Index
     * @return true if successful
     */
    bool ReleaseButton(uint8_t nodeId, ZWaveCommandClass commandClass,
                      uint8_t instance, uint16_t index);
    
    /**
     * @brief Refresh a value from the device
     * @param nodeId Node ID
     * @param commandClass Command class
     * @param instance Instance
     * @param index Index
     * @return true if refresh requested
     */
    bool RefreshValue(uint8_t nodeId, ZWaveCommandClass commandClass,
                     uint8_t instance, uint16_t index);
    
    // ===== Z-Wave Specific: Device Commands =====
    
    /**
     * @brief Turn switch on
     * @param nodeId Node ID
     * @param instance Instance (default 1)
     * @return true if command sent
     */
    bool SwitchOn(uint8_t nodeId, uint8_t instance = 1);
    
    /**
     * @brief Turn switch off
     * @param nodeId Node ID
     * @param instance Instance (default 1)
     * @return true if command sent
     */
    bool SwitchOff(uint8_t nodeId, uint8_t instance = 1);
    
    /**
     * @brief Set dimmer level
     * @param nodeId Node ID
     * @param level Level 0-99 (0=off, 99=full, 255=last non-zero)
     * @param instance Instance (default 1)
     * @return true if command sent
     */
    bool SetLevel(uint8_t nodeId, uint8_t level, uint8_t instance = 1);
    
    /**
     * @brief Set thermostat setpoint
     * @param nodeId Node ID
     * @param temperature Temperature in device units
     * @param setpointType Setpoint type index
     * @return true if command sent
     */
    bool SetThermostatSetpoint(uint8_t nodeId, float temperature, uint8_t setpointType = 1);
    
    /**
     * @brief Set thermostat mode
     * @param nodeId Node ID
     * @param mode Mode string (Off, Heat, Cool, Auto, etc.)
     * @return true if command sent
     */
    bool SetThermostatMode(uint8_t nodeId, const std::string& mode);
    
    /**
     * @brief Set thermostat fan mode
     * @param nodeId Node ID
     * @param mode Fan mode string (Auto Low, On Low, etc.)
     * @return true if command sent
     */
    bool SetThermostatFanMode(uint8_t nodeId, const std::string& mode);
    
    /**
     * @brief Lock door lock
     * @param nodeId Node ID
     * @return true if command sent
     */
    bool LockDoor(uint8_t nodeId);
    
    /**
     * @brief Unlock door lock
     * @param nodeId Node ID
     * @return true if command sent
     */
    bool UnlockDoor(uint8_t nodeId);
    
    /**
     * @brief Set color (RGBW)
     * @param nodeId Node ID
     * @param red Red component (0-255)
     * @param green Green component (0-255)
     * @param blue Blue component (0-255)
     * @param white White component (0-255, optional)
     * @return true if command sent
     */
    bool SetColor(uint8_t nodeId, uint8_t red, uint8_t green, 
                 uint8_t blue, uint8_t white = 0);
    
    // ===== Z-Wave Specific: Association Groups =====
    
    /**
     * @brief Get association groups for a node
     * @param nodeId Node ID
     * @return List of association groups
     */
    std::vector<ZWaveAssociationGroup> GetAssociationGroups(uint8_t nodeId) const;
    
    /**
     * @brief Add node to association group
     * @param nodeId Source node ID
     * @param groupIndex Group index
     * @param targetNodeId Target node ID
     * @return true if successful
     */
    bool AddAssociation(uint8_t nodeId, uint8_t groupIndex, uint8_t targetNodeId);
    
    /**
     * @brief Remove node from association group
     * @param nodeId Source node ID
     * @param groupIndex Group index
     * @param targetNodeId Target node ID
     * @return true if successful
     */
    bool RemoveAssociation(uint8_t nodeId, uint8_t groupIndex, uint8_t targetNodeId);
    
    // ===== Z-Wave Specific: Configuration =====
    
    /**
     * @brief Set configuration parameter
     * @param nodeId Node ID
     * @param paramId Parameter ID
     * @param value Value to set
     * @param size Value size in bytes (1, 2, or 4)
     * @return true if command sent
     */
    bool SetConfigParam(uint8_t nodeId, uint8_t paramId, int32_t value, uint8_t size = 4);
    
    /**
     * @brief Request configuration parameter value
     * @param nodeId Node ID
     * @param paramId Parameter ID
     * @return true if request sent
     */
    bool RequestConfigParam(uint8_t nodeId, uint8_t paramId);
    
    // ===== Z-Wave Specific: Network Operations =====
    
    /**
     * @brief Heal the network
     * @param doReturnRoutes Update return routes
     * @return true if healing started
     */
    bool HealNetwork(bool doReturnRoutes = true);
    
    /**
     * @brief Heal a specific node
     * @param nodeId Node ID
     * @param doReturnRoutes Update return routes
     * @return true if healing started
     */
    bool HealNode(uint8_t nodeId, bool doReturnRoutes = true);
    
    /**
     * @brief Get network statistics
     * @return Network stats
     */
    ZWaveNetworkStats GetNetworkStats() const;
    
    /**
     * @brief Test network connectivity
     * @param nodeId Node ID (0 for all nodes)
     * @param count Number of test frames
     * @return true if test started
     */
    bool TestNetwork(uint8_t nodeId, uint8_t count = 3);
    
    // ===== Z-Wave Specific: Scenes =====
    
    /**
     * @brief Create a new scene
     * @param label Scene label
     * @return Scene ID (0 on failure)
     */
    uint8_t CreateScene(const std::string& label);
    
    /**
     * @brief Delete a scene
     * @param sceneId Scene ID
     * @return true if successful
     */
    bool DeleteScene(uint8_t sceneId);
    
    /**
     * @brief Activate a scene
     * @param sceneId Scene ID
     * @return true if successful
     */
    bool ActivateScene(uint8_t sceneId);
    
    /**
     * @brief Get all scenes
     * @return List of scenes
     */
    std::vector<ZWaveScene> GetScenes() const;
    
    /**
     * @brief Add value to scene
     * @param sceneId Scene ID
     * @param value Value to add
     * @return true if successful
     */
    bool AddValueToScene(uint8_t sceneId, const ZWaveValue& value);
    
    /**
     * @brief Remove value from scene
     * @param sceneId Scene ID
     * @param valueId Value ID
     * @return true if successful
     */
    bool RemoveValueFromScene(uint8_t sceneId, uint64_t valueId);
    
    // ===== Z-Wave Specific: Polling =====
    
    /**
     * @brief Enable polling for a value
     * @param nodeId Node ID
     * @param commandClass Command class
     * @param instance Instance
     * @param index Index
     * @param intensity Poll intensity (1=least frequent)
     * @return true if successful
     */
    bool EnablePoll(uint8_t nodeId, ZWaveCommandClass commandClass,
                   uint8_t instance, uint16_t index, uint8_t intensity = 1);
    
    /**
     * @brief Disable polling for a value
     * @param nodeId Node ID
     * @param commandClass Command class
     * @param instance Instance
     * @param index Index
     * @return true if successful
     */
    bool DisablePoll(uint8_t nodeId, ZWaveCommandClass commandClass,
                    uint8_t instance, uint16_t index);
    
    /**
     * @brief Set poll interval
     * @param milliseconds Poll interval in ms
     */
    void SetPollInterval(int milliseconds);
    
    /**
     * @brief Get poll interval
     * @return Poll interval in ms
     */
    int GetPollInterval() const;
    
    // ===== Z-Wave Specific: Wake-Up =====
    
    /**
     * @brief Set wake-up interval for sleeping device
     * @param nodeId Node ID
     * @param seconds Interval in seconds
     * @return true if command queued
     */
    bool SetWakeUpInterval(uint8_t nodeId, uint32_t seconds);
    
    /**
     * @brief Get wake-up interval
     * @param nodeId Node ID
     * @return Interval in seconds (0 if not available)
     */
    uint32_t GetWakeUpInterval(uint8_t nodeId) const;
    
    // ===== Callbacks =====
    
    void SetOnNodeAdded(ZWaveNodeCallback callback) { onNodeAdded = callback; }
    void SetOnNodeRemoved(ZWaveNodeCallback callback) { onNodeRemoved = callback; }
    void SetOnNodeReady(ZWaveNodeCallback callback) { onNodeReady = callback; }
    void SetOnValueChanged(ZWaveValueCallback callback) { onValueChanged = callback; }
    void SetOnNotification(ZWaveNotificationCallback callback) { onNotification = callback; }
    void SetOnControllerState(ZWaveControllerCallback callback) { onControllerState = callback; }
    void SetOnInclusionResult(ZWaveInclusionCallback callback) { onInclusionResult = callback; }

private:
    // ===== OpenZWave Integration =====
    
#ifdef ULTRACANVAS_WITH_ZWAVE
    static void OnNotification(const OpenZWave::Notification* notification, void* context);
    void ProcessNotification(const OpenZWave::Notification* notification);
    
    void HandleValueAdded(const OpenZWave::Notification* notification);
    void HandleValueRemoved(const OpenZWave::Notification* notification);
    void HandleValueChanged(const OpenZWave::Notification* notification);
    void HandleNodeAdded(const OpenZWave::Notification* notification);
    void HandleNodeRemoved(const OpenZWave::Notification* notification);
    void HandleNodeEvent(const OpenZWave::Notification* notification);
    void HandleDriverReady(const OpenZWave::Notification* notification);
    void HandleDriverFailed(const OpenZWave::Notification* notification);
    void HandleAllNodesQueried(const OpenZWave::Notification* notification);
    void HandleControllerCommand(const OpenZWave::Notification* notification);
    
    ZWaveValue ConvertValue(const OpenZWave::ValueID& valueId) const;
    bool FindValueId(uint8_t nodeId, ZWaveCommandClass cc, uint8_t instance,
                    uint16_t index, OpenZWave::ValueID& outValueId) const;
#endif
    
    // ===== Helper Methods =====
    
    void UpdateNodeInfo(ZWaveNode& node);
    SmartHomeDeviceInfo ConvertToDeviceInfo(const ZWaveNode& node) const;
    SmartHomeDeviceCategory DetermineCategory(const ZWaveNode& node) const;
    std::string MakeDeviceId(uint8_t nodeId) const;
    uint8_t ParseNodeId(const std::string& deviceId) const;
    
    void NotifyNodeAdded(const ZWaveNode& node);
    void NotifyNodeRemoved(const ZWaveNode& node);
    void NotifyValueChanged(const ZWaveValue& value);
    
    // ===== State =====
    
    std::atomic<bool> initialized{false};
    std::atomic<bool> discovering{false};
    std::atomic<bool> networkReady{false};
    
    std::string controllerPath = "/dev/ttyUSB0";
    std::string configPath = "/etc/openzwave";
    std::string userPath = "/var/lib/openzwave";
    
    uint32_t homeId = 0;
    uint8_t controllerNodeId = 0;
    
    mutable std::mutex nodesMutex;
    std::map<uint8_t, ZWaveNode> nodes;
    
    mutable std::mutex discoveredMutex;
    std::vector<SmartHomeDeviceInfo> discoveredDevices;
    
    // Controller state
    std::atomic<ZWaveControllerState> controllerState{ZWaveControllerState::Normal};
    
    // Callbacks
    ZWaveNodeCallback onNodeAdded;
    ZWaveNodeCallback onNodeRemoved;
    ZWaveNodeCallback onNodeReady;
    ZWaveValueCallback onValueChanged;
    ZWaveNotificationCallback onNotification;
    ZWaveControllerCallback onControllerState;
    ZWaveInclusionCallback onInclusionResult;
    
    // Notification queue for thread-safe processing
    std::queue<std::function<void()>> notificationQueue;
    std::mutex queueMutex;
    std::thread processingThread;
    std::atomic<bool> stopProcessing{false};
    
    void ProcessNotificationQueue();
};

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

/**
 * @brief Create Z-Wave protocol instance
 * @return Protocol instance
 */
std::shared_ptr<ISmartHomeProtocol> CreateZWaveProtocol();

} // namespace SmartHome
} // namespace UltraCanvas
