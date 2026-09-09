// ZigbeeProtocol.h
// Zigbee Protocol Implementation
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework

#pragma once

#include "SmartHomeProtocolBase.h"
#include "ISmartHomeProtocol.h"
#include <memory>
#include <vector>
#include <map>
#include <array>

namespace UltraCanvas {
namespace SmartHome {

/**
 * @brief Zigbee device type
 */
enum class ZigbeeDeviceType {
    Coordinator,        ///< Network coordinator
    Router,             ///< Router (mains powered)
    EndDevice,          ///< End device (can be sleepy)
    Unknown
};

/**
 * @brief Zigbee cluster definition
 */
struct ZigbeeCluster {
    uint16_t ClusterId = 0;                     ///< Cluster ID
    std::string Name;                           ///< Cluster name
    bool IsServer = true;                       ///< Server or client cluster
    std::vector<uint16_t> Attributes;           ///< Attribute IDs
    std::vector<uint8_t> Commands;              ///< Command IDs
};

/**
 * @brief Zigbee endpoint definition
 */
struct ZigbeeEndpoint {
    uint8_t EndpointId = 0;                     ///< Endpoint number
    uint16_t ProfileId = 0;                     ///< Application profile ID
    uint16_t DeviceId = 0;                      ///< Device identifier
    uint8_t DeviceVersion = 0;                  ///< Device version
    std::vector<ZigbeeCluster> InputClusters;   ///< Input (server) clusters
    std::vector<ZigbeeCluster> OutputClusters;  ///< Output (client) clusters
};

/**
 * @brief Zigbee node (device) information
 */
struct ZigbeeNode {
    uint16_t NwkAddress = 0;                    ///< Network address
    uint64_t IeeeAddress = 0;                   ///< IEEE (MAC) address
    std::string DeviceId;                       ///< UltraCanvas device ID
    ZigbeeDeviceType Type = ZigbeeDeviceType::Unknown;
    uint16_t ManufacturerCode = 0;              ///< Manufacturer code
    std::string ManufacturerName;               ///< Manufacturer name
    std::string ModelIdentifier;                ///< Model ID
    std::string DateCode;                       ///< Manufacturing date
    uint8_t PowerSource = 0;                    ///< Power source type
    std::vector<ZigbeeEndpoint> Endpoints;      ///< Device endpoints
    bool IsOnline = false;                      ///< Online status
    uint8_t Lqi = 0;                            ///< Link quality indicator
    int8_t Rssi = 0;                            ///< RSSI (dBm)
    uint32_t LastSeen = 0;                      ///< Last seen timestamp
};

/**
 * @brief Zigbee network parameters
 */
struct ZigbeeNetworkParams {
    uint64_t ExtendedPanId = 0;                 ///< Extended PAN ID
    uint16_t PanId = 0;                         ///< PAN ID
    uint8_t Channel = 0;                        ///< Current channel (11-26)
    uint32_t ChannelMask = 0;                   ///< Channel mask
    std::array<uint8_t, 16> NetworkKey;         ///< Network key
    uint8_t SecurityLevel = 0;                  ///< Security level
    uint8_t NetworkUpdateId = 0;                ///< Network update ID
    uint16_t TrustCenterAddress = 0;            ///< Trust center address
};

/**
 * @brief Zigbee binding entry
 */
struct ZigbeeBinding {
    uint64_t SourceAddress = 0;                 ///< Source IEEE address
    uint8_t SourceEndpoint = 0;                 ///< Source endpoint
    uint16_t ClusterId = 0;                     ///< Cluster ID
    uint64_t DestAddress = 0;                   ///< Destination IEEE address
    uint8_t DestEndpoint = 0;                   ///< Destination endpoint
    uint16_t GroupAddress = 0;                  ///< Group address (if group binding)
};

/**
 * @brief Zigbee attribute value
 */
struct ZigbeeAttributeValue {
    uint16_t ClusterId = 0;
    uint16_t AttributeId = 0;
    uint8_t DataType = 0;
    std::vector<uint8_t> Value;
    uint8_t Status = 0;
};

/**
 * @brief Zigbee protocol implementation
 * 
 * Implements the Zigbee 3.0 protocol for smart home device communication.
 * Supports acting as coordinator, router, or end device.
 * 
 * Key features:
 * - Network formation and joining
 * - Device pairing via permit join or Touchlink
 * - ZCL attribute read/write/report
 * - Group and scene management
 * - OTA updates
 * - Green Power proxy
 */
class ZigbeeProtocol : public SmartHomeProtocolBase, public IZigbeeProtocol {
public:
    ZigbeeProtocol();
    virtual ~ZigbeeProtocol();
    
    // ===== LIFECYCLE =====
    
    // Both SmartHomeProtocolBase and IZigbeeProtocol override GetType();
    // under virtual inheritance neither dominates, so the concrete class has
    // to name the winner itself.
    SmartHomeProtocolType GetType() const override {
        return SmartHomeProtocolType::Zigbee;
    }

    // The remaining ISmartHomeProtocol members. Zigbee addresses devices by
    // network address and endpoint rather than by device object, so the
    // info-level view is the one that carries anything.
    std::string GetHardwareInfo() const override;
    bool GetDeviceState(const std::string& deviceId,
                        std::map<std::string, std::string>& state) override;
    bool SetChannel(int channel) override;
    std::vector<SmartHomeDeviceInfo> GetPairedDevices() override;
    bool PairDevice(const std::string& deviceId,
                    const std::map<std::string, std::string>& params) override;
    bool UnpairDevice(const std::string& deviceId) override;
    std::vector<std::shared_ptr<ISmartHomeDevice>> GetDevices() const override;
    std::shared_ptr<ISmartHomeDevice> GetDevice(const std::string& deviceId) const override;

    bool Initialize() override;
    void Shutdown() override;
    
    // ===== HARDWARE =====
    
    bool IsHardwareAvailable() const override;
    std::vector<std::string> GetAvailableAdapters() const override;
    bool SelectAdapter(const std::string& adapterId) override;
    
    // ===== NETWORK =====
    
    bool FormNetwork(const std::string& networkName) override;
    bool JoinNetwork(const std::string& networkId) override;
    bool LeaveNetwork() override;
    NetworkTopology GetTopology() const override;
    
    // ===== DISCOVERY =====
    
    bool StartDiscovery(int timeoutSeconds = 30) override;
    void StopDiscovery() override;
    
    // ===== PAIRING =====
    
    bool StartPairing(int timeoutSeconds = 120) override;
    void StopPairing() override;
    bool PermitJoin(int timeoutSeconds) override;
    
    // ===== DEVICE OPERATIONS =====
    
    bool RemoveDevice(const std::string& deviceId) override;
    bool InterviewDevice(const std::string& deviceId) override;
    
    // ===== COMMANDS =====
    
    bool SendCommand(const std::string& deviceId,
                     const std::string& command,
                     const std::map<std::string, std::string>& params) override;
    
    bool SendGroupCommand(uint16_t groupId,
                          const std::string& command,
                          const std::map<std::string, std::string>& params) override;
    
    // ===== GROUPS =====
    
    bool CreateGroup(uint16_t groupId, const std::string& name) override;
    bool DeleteGroup(uint16_t groupId) override;
    bool AddToGroup(const std::string& deviceId, uint16_t groupId) override;
    bool RemoveFromGroup(const std::string& deviceId, uint16_t groupId) override;
    std::vector<uint16_t> GetGroups() const override;
    std::vector<std::string> GetGroupMembers(uint16_t groupId) const override;
    
    // ===== BINDING =====
    
    bool BindDevices(const std::string& sourceId, const std::string& targetId) override;
    bool UnbindDevices(const std::string& sourceId, const std::string& targetId) override;
    std::vector<std::string> GetBindings(const std::string& deviceId) const override;
    
    // ===== OTA =====
    
    bool StartOTAUpdate(const std::string& deviceId, const std::string& firmwarePath) override;
    int GetOTAProgress(const std::string& deviceId) const override;
    bool CancelOTAUpdate(const std::string& deviceId) override;
    
    // ===== SECURITY =====
    
    SmartHomeSecurityLevel GetSecurityLevel() const override;
    bool SetNetworkKey(const std::vector<uint8_t>& key) override;
    
    // ===== CONFIGURATION =====
    
    bool LoadConfig(const std::string& path) override;
    bool SaveConfig(const std::string& path) override;
    
    std::vector<SmartHomeDeviceCategory> GetSupportedDeviceCategories() const override;
    
    // ===== ZIGBEE-SPECIFIC (IZigbeeProtocol) =====
    
    uint16_t GetPanId() const override;
    uint64_t GetExtendedPanId() const override;
    uint8_t GetChannel() const override;
    
    bool TouchLink(int timeoutSeconds) override;
    bool FactoryReset(const std::string& deviceId) override;
    
    bool ReadAttribute(const std::string& deviceId, uint8_t endpoint,
                       uint16_t clusterId, uint16_t attributeId,
                       ZigbeeAttributeValue& outValue);
    
    bool WriteAttribute(const std::string& deviceId, uint8_t endpoint,
                        uint16_t clusterId, uint16_t attributeId,
                        const std::vector<uint8_t>& value);
    
    bool ConfigureReporting(const std::string& deviceId, uint8_t endpoint,
                            uint16_t clusterId, uint16_t attributeId,
                            uint16_t minInterval, uint16_t maxInterval,
                            uint16_t reportableChange);
    
    bool SendZCLCommand(const std::string& deviceId, uint8_t endpoint,
                        uint16_t clusterId, uint8_t commandId,
                        const std::vector<uint8_t>& payload,
                        bool clusterSpecific = true);
    
    // ===== ADDITIONAL ZIGBEE METHODS =====
    
    // Network management
    ZigbeeNetworkParams GetNetworkParams() const;
    bool SetNetworkParams(const ZigbeeNetworkParams& params);
    bool ChangeChannel(uint8_t newChannel);
    bool UpdateNetworkKey(const std::array<uint8_t, 16>& newKey);
    
    // Node management
    ZigbeeNode GetZigbeeNode(const std::string& deviceId) const;
    std::vector<ZigbeeNode> GetAllZigbeeNodes() const;
    uint16_t GetNodeNetworkAddress(const std::string& deviceId) const;
    uint64_t GetNodeIeeeAddress(const std::string& deviceId) const;
    
    // Endpoint and cluster access
    std::vector<ZigbeeEndpoint> GetEndpoints(const std::string& deviceId) const;
    std::vector<ZigbeeCluster> GetClusters(const std::string& deviceId, uint8_t endpoint) const;
    
    // Binding management
    bool CreateBinding(const ZigbeeBinding& binding);
    bool RemoveBinding(const ZigbeeBinding& binding);
    std::vector<ZigbeeBinding> GetBindingTable(const std::string& deviceId) const;
    
    // Scenes
    bool AddScene(uint16_t groupId, uint8_t sceneId, const std::string& name,
                  const std::vector<ZigbeeAttributeValue>& extensionFields);
    bool RemoveScene(uint16_t groupId, uint8_t sceneId);
    bool RecallScene(uint16_t groupId, uint8_t sceneId);
    bool StoreScene(const std::string& deviceId, uint16_t groupId, uint8_t sceneId);
    
    // Coordinator functions
    bool IsCoordinator() const;
    bool SetInstallCode(const std::string& ieeeAddress, const std::vector<uint8_t>& installCode);
    
    // Green Power
    bool EnableGreenPowerProxy(bool enable);
    bool AddGreenPowerDevice(uint32_t srcId, const std::vector<uint8_t>& key);
    bool RemoveGreenPowerDevice(uint32_t srcId);
    
    // Diagnostics
    uint8_t GetLqi(const std::string& deviceId) const;
    int8_t GetRssi(const std::string& deviceId) const;
    std::vector<std::pair<std::string, std::string>> GetNeighborTable() const;
    std::vector<std::pair<std::string, std::string>> GetRoutingTable() const;
    
private:
    // ===== INTERNAL METHODS =====
    
    bool InitializeStack();
    void ShutdownStack();
    
    void ProcessZigbee();
    void NetworkThread();
    
    void OnDeviceJoined(uint64_t ieeeAddress, uint16_t nwkAddress);
    void OnDeviceLeft(uint64_t ieeeAddress);
    void OnDeviceAnnounce(uint64_t ieeeAddress, uint16_t nwkAddress);
    void OnAttributeReport(uint64_t srcAddress, uint8_t endpoint,
                           uint16_t clusterId, const ZigbeeAttributeValue& value);
    void OnZCLResponse(uint64_t srcAddress, uint8_t endpoint,
                       uint16_t clusterId, uint8_t commandId,
                       const std::vector<uint8_t>& payload);
    
    SmartHomeDeviceInfo NodeToDeviceInfo(const ZigbeeNode& node) const;
    SmartHomeDeviceCategory DetermineDeviceCategory(const ZigbeeNode& node) const;
    std::string IeeeAddressToString(uint64_t addr) const;
    uint64_t StringToIeeeAddress(const std::string& str) const;
    
    // The interview runs step by step against the node table, by device id,
    // because each answer arrives on the stack's receive thread.
    void InterviewNode(const std::string& deviceId);
    void DiscoverEndpoints(const std::string& deviceId);
    void DiscoverClusters(const std::string& deviceId, uint8_t endpoint);
    static std::string ZclStringToStd(const ZigbeeAttributeValue& value);
    
    // Called by the stack for every message a node sends: records link
    // quality and the last-seen time, and returns the node's IEEE address
    // (0 if the network address is not one this coordinator knows).
    uint64_t NoteHeardFrom(uint16_t nwkAddress, uint8_t lqi, int8_t rssi);
    // Network address of a known node, for ZDO requests addressed by IEEE.
    bool NwkForIeee(uint64_t ieeeAddress, uint16_t& nwkAddress) const;
    
    // Called by the stack when the NCP reports the network up (with the
    // parameters it is running) or down. On start-up an NCP that still holds
    // a network from last time reports it up without anyone forming it.
    void OnNetworkUp(const ZigbeeNetworkParams& params, uint64_t coordinatorIeee);
    void OnNetworkDown();
    SmartHomeNetworkInfo MakeNetworkInfo() const;
    static std::string HexString(uint64_t value, int digits);
    
    // Drops a node from the tables and the paired-device list. RemoveDevice
    // asks the device to leave first; a device that left on its own is just
    // forgotten.
    void ForgetDevice(const std::string& deviceId);
    
    // ZCL command helpers
    bool SendOnOff(const std::string& deviceId, uint8_t endpoint, uint8_t command);
    bool SendLevelControl(const std::string& deviceId, uint8_t endpoint, 
                          uint8_t level, uint16_t transitionTime);
    bool SendColorControl(const std::string& deviceId, uint8_t endpoint,
                          uint16_t hue, uint8_t saturation, uint16_t transitionTime);
    
    // ===== ZIGBEE STACK WRAPPERS =====
    
    // These would wrap actual Zigbee stack (zigbee2mqtt, EZSP, etc.)
    class ZigbeeStack;
    std::unique_ptr<ZigbeeStack> stack;
    
    // ===== STATE =====
    
    ZigbeeNetworkParams networkParams;
    std::string networkName_;                       // as given to FormNetwork
    uint64_t coordinatorIeee_ = 0;                  // reported by the stack
    std::map<std::string, ZigbeeNode> zigbeeNodes;  // deviceId -> node
    std::map<uint64_t, std::string> ieeeToDeviceId; // IEEE address -> deviceId
    std::map<uint16_t, std::string> nwkToDeviceId;  // Network address -> deviceId
    mutable std::mutex nodeMutex;
    
    // Groups
    std::map<uint16_t, std::string> groups;
    std::map<uint16_t, std::vector<std::string>> groupMembers;
    
    // Scenes
    struct SceneEntry {
        uint16_t GroupId;
        uint8_t SceneId;
        std::string Name;
        std::vector<ZigbeeAttributeValue> ExtensionFields;
    };
    std::map<uint32_t, SceneEntry> scenes;  // (groupId << 8 | sceneId) -> scene
    
    // Bindings
    std::map<std::string, std::vector<ZigbeeBinding>> deviceBindings;
    
    // OTA state
    std::map<std::string, int> otaProgress;
    
    // Green Power
    bool greenPowerEnabled = false;
    std::map<uint32_t, std::vector<uint8_t>> greenPowerDevices;  // srcId -> key
    
    // Processing
    std::thread processThread;
    std::thread networkThread;
    std::atomic<bool> running{false};
    std::atomic<bool> permitJoinActive{false};
    int permitJoinTimeout = 0;
    
    // Configuration
    std::string selectedAdapter;
    std::string storagePath;
};

/**
 * @brief Factory function for Zigbee protocol
 */
std::shared_ptr<ISmartHomeProtocol> CreateZigbeeProtocol();

// ===== ZIGBEE CONSTANTS =====

namespace ZigbeeProfiles {
    constexpr uint16_t HomeAutomation = 0x0104;
    constexpr uint16_t SmartEnergy = 0x0109;
    constexpr uint16_t GreenPower = 0xA1E0;
    constexpr uint16_t LightLink = 0xC05E;
}

namespace ZigbeeClusters {
    // General
    constexpr uint16_t Basic = 0x0000;
    constexpr uint16_t PowerConfiguration = 0x0001;
    constexpr uint16_t Identify = 0x0003;
    constexpr uint16_t Groups = 0x0004;
    constexpr uint16_t Scenes = 0x0005;
    constexpr uint16_t OnOff = 0x0006;
    constexpr uint16_t LevelControl = 0x0008;
    constexpr uint16_t Alarms = 0x0009;
    constexpr uint16_t Time = 0x000A;
    constexpr uint16_t OTAUpgrade = 0x0019;
    
    // Lighting
    constexpr uint16_t ColorControl = 0x0300;
    constexpr uint16_t BallastConfiguration = 0x0301;
    
    // HVAC
    constexpr uint16_t Thermostat = 0x0201;
    constexpr uint16_t FanControl = 0x0202;
    constexpr uint16_t ThermostatUI = 0x0204;
    
    // Closures
    constexpr uint16_t DoorLock = 0x0101;
    constexpr uint16_t WindowCovering = 0x0102;
    
    // Measurement
    constexpr uint16_t IlluminanceMeasurement = 0x0400;
    constexpr uint16_t TemperatureMeasurement = 0x0402;
    constexpr uint16_t PressureMeasurement = 0x0403;
    constexpr uint16_t RelativeHumidity = 0x0405;
    constexpr uint16_t OccupancySensing = 0x0406;
    
    // Security
    constexpr uint16_t IASZone = 0x0500;
    constexpr uint16_t IASACE = 0x0501;
    constexpr uint16_t IASWD = 0x0502;
    
    // Smart Energy
    constexpr uint16_t Metering = 0x0702;
    constexpr uint16_t ElectricalMeasurement = 0x0B04;
}

namespace ZigbeeDeviceIds {
    // Lighting
    constexpr uint16_t OnOffLight = 0x0100;
    constexpr uint16_t DimmableLight = 0x0101;
    constexpr uint16_t ColorDimmableLight = 0x0102;
    constexpr uint16_t OnOffLightSwitch = 0x0103;
    constexpr uint16_t DimmerSwitch = 0x0104;
    constexpr uint16_t ColorDimmerSwitch = 0x0105;
    constexpr uint16_t LightSensor = 0x0106;
    constexpr uint16_t OccupancySensor = 0x0107;
    
    // Closures
    constexpr uint16_t DoorLock = 0x000A;
    constexpr uint16_t WindowCovering = 0x0202;
    
    // HVAC
    constexpr uint16_t Thermostat = 0x0301;
    constexpr uint16_t TemperatureSensor = 0x0302;
    
    // IAS
    constexpr uint16_t IASControlEquipment = 0x0400;
    constexpr uint16_t IASZone = 0x0402;
    
    // Smart plugs
    constexpr uint16_t SmartPlug = 0x0051;
    constexpr uint16_t MainsPowerOutlet = 0x0009;
}

} // namespace SmartHome
} // namespace UltraCanvas
