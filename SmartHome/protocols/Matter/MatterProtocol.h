// MatterProtocol.h
// Matter (CHIP) Protocol Implementation
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework

#pragma once

#include "SmartHomeProtocolBase.h"
#include "ISmartHomeProtocol.h"
#include <memory>
#include <vector>
#include <map>
#include <thread>
#include <atomic>

namespace UltraCanvas {
namespace SmartHome {

/**
 * @brief Matter fabric information
 */
struct MatterFabric {
    uint64_t FabricId = 0;              ///< Fabric identifier
    uint16_t VendorId = 0;              ///< Vendor ID
    std::string FabricLabel;            ///< User-friendly label
    std::string RootCertificate;        ///< Root CA certificate (PEM)
    bool IsActive = false;              ///< Active fabric flag
    uint64_t CreatedAt = 0;             ///< Creation timestamp
};

/**
 * @brief Matter node information
 */
struct MatterNode {
    uint64_t NodeId = 0;                ///< Node ID within fabric
    uint64_t FabricId = 0;              ///< Fabric this node belongs to
    std::string DeviceId;               ///< UltraCanvas device ID
    uint16_t VendorId = 0;              ///< Device vendor ID
    uint16_t ProductId = 0;             ///< Device product ID
    std::string VendorName;             ///< Vendor name
    std::string ProductName;            ///< Product name
    std::string SerialNumber;           ///< Serial number
    std::string SoftwareVersion;        ///< Software version
    std::vector<uint32_t> Endpoints;    ///< Available endpoints
    std::vector<uint32_t> DeviceTypes;  ///< Device type IDs
    bool SupportsOTA = false;           ///< OTA update support
    bool IsOnline = false;              ///< Online status
};

/**
 * @brief Matter commissioning parameters
 */
struct MatterCommissioningParams {
    std::string SetupCode;              ///< Manual pairing code (11 or 21 digits)
    std::string QRCode;                 ///< QR code payload
    uint16_t Discriminator = 0;         ///< Discriminator for discovery
    uint32_t SetupPinCode = 0;          ///< Setup PIN code
    uint16_t VendorId = 0;              ///< Expected vendor ID (optional)
    uint16_t ProductId = 0;             ///< Expected product ID (optional)
    bool UseThreadNetwork = false;      ///< Commission over Thread
    bool UseBLETransport = true;        ///< Use BLE for commissioning
    bool UseWiFiTransport = true;       ///< Use WiFi for commissioning
    int TimeoutSeconds = 120;           ///< Commissioning timeout
};

/**
 * @brief Matter cluster attribute
 */
struct MatterAttribute {
    uint32_t ClusterId = 0;             ///< Cluster ID
    uint32_t AttributeId = 0;           ///< Attribute ID
    std::string Value;                  ///< Attribute value (serialized)
    std::string DataType;               ///< Data type name
    bool Writable = false;              ///< Can be written
    bool Reportable = false;            ///< Supports reporting
};

/**
 * @brief Matter protocol implementation using connectedhomeip SDK
 * 
 * Implements the Matter (formerly CHIP/Project Connected Home over IP) protocol
 * for smart home device communication. Supports multi-fabric, multi-admin
 * architecture with secure commissioning.
 * 
 * Key features:
 * - BLE and WiFi commissioning
 * - Thread border router support
 * - Multi-fabric (multiple controllers)
 * - OTA firmware updates
 * - Binding and groups
 * - Secure communication (CASE/PASE)
 */
class MatterProtocol : public SmartHomeProtocolBase, public IMatterProtocol {
public:
    MatterProtocol();
    virtual ~MatterProtocol();
    
    // ===== LIFECYCLE (ISmartHomeProtocol) =====
    
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
    
    int GetSecurityLevel() const override;
    bool SetNetworkKey(const std::vector<uint8_t>& key) override;
    
    // ===== CONFIGURATION =====
    
    bool LoadConfig(const std::string& path) override;
    bool SaveConfig(const std::string& path) override;
    
    std::vector<SmartHomeDeviceCategory> GetSupportedDeviceCategories() const override;
    
    // ===== MATTER-SPECIFIC (IMatterProtocol) =====
    
    bool AddFabric(const MatterFabric& fabric) override;
    bool RemoveFabric(uint64_t fabricId) override;
    std::vector<MatterFabric> GetFabrics() const override;
    
    bool OpenCommissioningWindow(const std::string& deviceId, int timeoutSeconds) override;
    bool CloseCommissioningWindow(const std::string& deviceId) override;
    
    std::string GenerateSetupCode(const std::string& deviceId) const override;
    std::string GenerateQRCode(const std::string& deviceId) const override;
    
    bool CommissionWithCode(const std::string& setupCode) override;
    bool CommissionWithQR(const std::string& qrPayload) override;
    bool CommissionWithParams(const MatterCommissioningParams& params);
    
    bool ReadAttribute(const std::string& deviceId, uint16_t endpoint,
                       uint32_t clusterId, uint32_t attributeId,
                       std::string& outValue) override;
    
    bool WriteAttribute(const std::string& deviceId, uint16_t endpoint,
                        uint32_t clusterId, uint32_t attributeId,
                        const std::string& value) override;
    
    bool SubscribeAttribute(const std::string& deviceId, uint16_t endpoint,
                            uint32_t clusterId, uint32_t attributeId,
                            OnAttributeChange callback) override;
    
    bool InvokeCommand(const std::string& deviceId, uint16_t endpoint,
                       uint32_t clusterId, uint32_t commandId,
                       const std::map<std::string, std::string>& fields) override;
    
    // ===== MATTER NODE ACCESS =====
    
    MatterNode GetMatterNode(const std::string& deviceId) const;
    std::vector<MatterNode> GetAllMatterNodes() const;
    
    // ===== THREAD INTEGRATION =====
    
    bool SetThreadDataset(const std::vector<uint8_t>& dataset);
    std::vector<uint8_t> GetThreadDataset() const;
    bool IsThreadBorderRouter() const;

private:
    // ===== INTERNAL METHODS =====
    
    bool InitializeStack();
    void ShutdownStack();
    
    bool InitializeController();
    void ShutdownController();
    
    void DiscoveryThread();
    void CommissioningThread();
    void EventLoopThread();
    
    void ProcessDiscoveredCommissionable(const std::string& instanceName,
                                          uint16_t discriminator,
                                          uint16_t vendorId,
                                          uint16_t productId);
    
    void OnCommissioningComplete(uint64_t nodeId, bool success, const std::string& error);
    void OnDeviceConnected(uint64_t nodeId);
    void OnDeviceDisconnected(uint64_t nodeId);
    void OnAttributeChanged(uint64_t nodeId, uint16_t endpoint,
                            uint32_t clusterId, uint32_t attributeId,
                            const std::string& value);
    
    SmartHomeDeviceInfo NodeToDeviceInfo(const MatterNode& node) const;
    SmartHomeDeviceCategory DetermineDeviceCategory(const std::vector<uint32_t>& deviceTypes) const;
    
    std::string CommandToCluster(const std::string& command,
                                  uint32_t& clusterId,
                                  uint32_t& commandId) const;
    
    // ===== MATTER SDK WRAPPER =====
    
    // Forward declaration for SDK wrapper
    class MatterSDKWrapper;
    std::unique_ptr<MatterSDKWrapper> sdkWrapper;
    
    // ===== INTERNAL CALLBACKS =====
    
    void OnCommissioningComplete(uint64_t nodeId, bool success, const std::string& error);
    void UpdateNodeAttribute(uint64_t nodeId, const std::string& attrName, const std::string& value);
    
    // ===== STATE =====
    
    std::vector<MatterFabric> fabrics;
    std::map<std::string, MatterNode> matterNodes;  // deviceId -> MatterNode
    std::map<uint64_t, std::string> nodeIdToDeviceId;  // nodeId -> deviceId
    mutable std::mutex matterMutex;
    
    // Groups
    std::map<uint16_t, std::string> groups;  // groupId -> name
    std::map<uint16_t, std::vector<std::string>> groupMembers;  // groupId -> deviceIds
    
    // Bindings
    std::map<std::string, std::vector<std::string>> bindings;  // sourceId -> targetIds
    
    // Active commissioning
    MatterCommissioningParams activeCommissioningParams;
    std::atomic<bool> commissioningInProgress{false};
    
    // Threads
    std::thread discoveryThread;
    std::thread commissioningThread;
    std::thread eventLoopThread;
    std::atomic<bool> running{false};
    
    // Thread dataset for Thread network commissioning
    std::vector<uint8_t> threadDataset;
    
    // Attribute subscriptions
    std::map<std::string, OnAttributeChange> attributeSubscriptions;
    
    // OTA state
    std::map<std::string, int> otaProgress;  // deviceId -> progress %
    
    // Configuration
    std::string storagePath;
    std::string selectedAdapter;
};

/**
 * @brief Factory function for Matter protocol
 */
std::shared_ptr<ISmartHomeProtocol> CreateMatterProtocol();

} // namespace SmartHome
} // namespace UltraCanvas
