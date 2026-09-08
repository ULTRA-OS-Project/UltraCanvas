// driver/smarthome/protocols/ISmartHomeProtocol.h
// Smart Home Protocol Interface - Abstract Base for All Protocol Implementations
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasSmartHome.h"
#include "ISmartHomeDevice.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

namespace UltraCanvas {
namespace SmartHome {

// ===== PROTOCOL CAPABILITY FLAGS =====

enum class ProtocolCapability : uint32_t {
    None                = 0,
    Discovery           = 1 << 0,
    Pairing             = 1 << 1,
    Commissioning       = 1 << 2,
    Mesh                = 1 << 3,
    Binding             = 1 << 4,
    Groups              = 1 << 5,
    Scenes              = 1 << 6,
    OTA                 = 1 << 7,
    Security            = 1 << 8,
    Encryption          = 1 << 9,
    MultiFabric         = 1 << 10,  // Matter multi-admin
    BorderRouter        = 1 << 11,  // Thread border router
    Bridge              = 1 << 12,  // Protocol bridging
    LocalControl        = 1 << 13,
    CloudControl        = 1 << 14
};

inline ProtocolCapability operator|(ProtocolCapability a, ProtocolCapability b) {
    return static_cast<ProtocolCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ProtocolCapability operator&(ProtocolCapability a, ProtocolCapability b) {
    return static_cast<ProtocolCapability>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool HasProtocolCapability(ProtocolCapability caps, ProtocolCapability check) {
    return (static_cast<uint32_t>(caps) & static_cast<uint32_t>(check)) != 0;
}

// ===== PROTOCOL STATE =====

enum class ProtocolState {
    Uninitialized,
    Initializing,
    Ready,
    Discovering,
    Pairing,
    Error,
    Shutdown
};

// ===== NETWORK TOPOLOGY =====

struct NetworkNode {
    std::string NodeId;
    std::string DeviceId;
    std::string ParentId;       // For mesh networks
    int Depth = 0;              // Distance from coordinator/border router
    int LinkQuality = 0;        // 0-255 or percentage
    bool IsRouter = false;      // Routing capable
    bool IsCoordinator = false;
    bool IsBorderRouter = false;
    std::vector<std::string> Neighbors;
};

struct NetworkTopology {
    std::string NetworkId;
    std::vector<NetworkNode> Nodes;
    int RouterCount = 0;
    int EndDeviceCount = 0;
    int MaxDepth = 0;
};

// ===== PROTOCOL CALLBACKS =====

using OnProtocolStateChange = std::function<void(ProtocolState state)>;
using OnProtocolDeviceDiscover = std::function<void(const SmartHomeDeviceInfo& device)>;
using OnProtocolDeviceJoin = std::function<void(const SmartHomeDeviceInfo& device)>;
using OnProtocolDeviceLeave = std::function<void(const std::string& deviceId)>;
using OnProtocolDeviceUpdate = std::function<void(const std::string& deviceId)>;
using OnProtocolNetworkForm = std::function<void(const SmartHomeNetworkInfo& network)>;
using OnProtocolError = std::function<void(int code, const std::string& message)>;

// ===== SMART HOME PROTOCOL INTERFACE =====

class ISmartHomeProtocol {
public:
    virtual ~ISmartHomeProtocol() = default;
    
    // ===== IDENTIFICATION =====
    virtual SmartHomeProtocolType GetType() const = 0;
    virtual std::string GetName() const = 0;
    virtual std::string GetVersion() const = 0;
    
    // ===== LIFECYCLE =====
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual bool IsInitialized() const = 0;
    virtual ProtocolState GetState() const = 0;
    
    // ===== CAPABILITIES =====
    virtual ProtocolCapability GetCapabilities() const = 0;
    virtual bool HasCapability(ProtocolCapability capability) const = 0;
    virtual std::vector<SmartHomeDeviceCategory> GetSupportedDeviceCategories() const = 0;
    
    // ===== HARDWARE DETECTION =====
    virtual bool IsHardwareAvailable() const = 0;
    virtual std::string GetHardwareInfo() const = 0;
    virtual std::vector<std::string> GetAvailableAdapters() const = 0;
    virtual bool SelectAdapter(const std::string& adapterId) = 0;
    
    // ===== NETWORK MANAGEMENT =====
    // Backends and the shipped examples all pass a network name; the default
    // keeps 'form me a network, any name' callers working.
    virtual bool FormNetwork(const std::string& networkName = "") = 0;
    virtual bool JoinNetwork(const std::string& networkId) = 0;
    virtual bool LeaveNetwork() = 0;
    virtual bool HasNetwork() const = 0;
    virtual SmartHomeNetworkInfo GetNetworkInfo() const = 0;
    virtual NetworkTopology GetTopology() const = 0;
    
    // ===== DISCOVERY =====
    virtual bool StartDiscovery(int durationSeconds = 60) = 0;
    virtual void StopDiscovery() = 0;
    virtual bool IsDiscovering() const = 0;
    virtual std::vector<SmartHomeDeviceInfo> GetDiscoveredDevices() const = 0;
    
    // ===== PAIRING / COMMISSIONING =====
    virtual bool StartPairing(int timeoutSeconds = 60) = 0;
    virtual void StopPairing() = 0;
    virtual bool IsPairing() const = 0;
    
    // For Matter commissioning with setup code
    virtual bool CommissionWithCode(const std::string& setupCode) { return false; }
    virtual bool CommissionWithQR(const std::string& qrPayload) { return false; }
    
    // For Zigbee/Z-Wave permit join
    virtual bool PermitJoin(int timeoutSeconds = 60) { return StartPairing(timeoutSeconds); }
    
    // ===== DEVICE MANAGEMENT =====
    virtual std::vector<std::shared_ptr<ISmartHomeDevice>> GetDevices() const = 0;
    virtual std::shared_ptr<ISmartHomeDevice> GetDevice(const std::string& deviceId) const = 0;
    virtual bool RemoveDevice(const std::string& deviceId) = 0;
    virtual bool InterviewDevice(const std::string& deviceId) = 0;  // Re-query device info

    // Info-level view of the same devices. Every backend implements these; the
    // shared_ptr API above is the object-level view of the same registry.
    virtual std::vector<SmartHomeDeviceInfo> GetPairedDevices() = 0;
    virtual bool PairDevice(const std::string& deviceId,
                            const std::map<std::string, std::string>& params) = 0;
    virtual bool UnpairDevice(const std::string& deviceId) = 0;
    virtual bool GetDeviceState(const std::string& deviceId,
                                std::map<std::string, std::string>& state) = 0;
    
    // ===== COMMAND EXECUTION =====
    virtual bool SendCommand(const std::string& deviceId, 
                            const std::string& command,
                            const std::map<std::string, std::string>& params = {}) = 0;
    
    // ===== GROUPS =====
    virtual bool SupportsGroups() const { return HasCapability(ProtocolCapability::Groups); }
    virtual bool CreateGroup(const std::string& groupId, const std::string& name) { return false; }
    virtual bool DeleteGroup(const std::string& groupId) { return false; }
    virtual bool AddDeviceToGroup(const std::string& deviceId, const std::string& groupId) { return false; }
    virtual bool RemoveDeviceFromGroup(const std::string& deviceId, const std::string& groupId) { return false; }
    virtual bool SendGroupCommand(const std::string& groupId,
                                  const std::string& command,
                                  const std::map<std::string, std::string>& params = {}) { return false; }
    
    // ===== BINDING (Zigbee/Thread/Matter) =====
    virtual bool SupportsBinding() const { return HasCapability(ProtocolCapability::Binding); }
    virtual bool BindDevices(const std::string& sourceDeviceId, 
                            const std::string& targetDeviceId,
                            const std::string& cluster = "") { return false; }
    virtual bool UnbindDevices(const std::string& sourceDeviceId,
                              const std::string& targetDeviceId,
                              const std::string& cluster = "") { return false; }
    
    // ===== OTA UPDATES =====
    virtual bool SupportsOTA() const { return HasCapability(ProtocolCapability::OTA); }

    // Optional capability surfaces. Backends that advertise the matching
    // capability override these; the defaults keep backends without it
    // (Z-Wave, KNX) concrete rather than abstract.

    // --- Groups (SupportsGroups) ---
    virtual bool CreateGroup(uint16_t groupId, const std::string& name) { return false; }
    virtual bool DeleteGroup(uint16_t groupId) { return false; }
    virtual bool AddToGroup(const std::string& deviceId, uint16_t groupId) { return false; }
    virtual bool RemoveFromGroup(const std::string& deviceId, uint16_t groupId) { return false; }
    virtual std::vector<uint16_t> GetGroups() const { return {}; }
    virtual std::vector<std::string> GetGroupMembers(uint16_t groupId) const { return {}; }
    virtual bool SendGroupCommand(uint16_t groupId, const std::string& command,
                                  const std::map<std::string, std::string>& params) { return false; }

    // --- Binding (SupportsBinding) ---
    virtual bool BindDevices(const std::string& sourceId, const std::string& targetId) { return false; }
    virtual bool UnbindDevices(const std::string& sourceId, const std::string& targetId) { return false; }
    virtual std::vector<std::string> GetBindings(const std::string& deviceId) const { return {}; }

    // --- OTA firmware update (SupportsOTA) ---
    virtual bool StartOTAUpdate(const std::string& deviceId, const std::string& imagePath) { return false; }
    virtual int GetOTAProgress(const std::string& deviceId) const { return -1; }
    virtual bool CancelOTAUpdate(const std::string& deviceId) { return false; }
    virtual std::vector<std::string> GetDevicesWithUpdates() const { return {}; }
    virtual bool StartDeviceUpdate(const std::string& deviceId) { return false; }
    virtual int GetUpdateProgress(const std::string& deviceId) const { return -1; }
    
    // ===== SECURITY =====
    virtual SmartHomeSecurityLevel GetSecurityLevel() const = 0;
    virtual bool SetNetworkKey(const std::vector<uint8_t>& key) { return false; }
    virtual bool RotateNetworkKey() { return false; }
    
    // ===== CALLBACKS =====
    virtual void SetOnStateChange(OnProtocolStateChange callback) = 0;
    virtual void SetOnDeviceDiscover(OnProtocolDeviceDiscover callback) = 0;
    virtual void SetOnDeviceJoin(OnProtocolDeviceJoin callback) = 0;
    virtual void SetOnDeviceLeave(OnProtocolDeviceLeave callback) = 0;
    virtual void SetOnDeviceUpdate(OnProtocolDeviceUpdate callback) = 0;
    virtual void SetOnNetworkForm(OnProtocolNetworkForm callback) = 0;
    virtual void SetOnError(OnProtocolError callback) = 0;
    
    // ===== CONFIGURATION =====
    virtual bool LoadConfig(const std::string& path) = 0;
    virtual bool SaveConfig(const std::string& path) = 0;
    
    // ===== DEBUG / DIAGNOSTICS =====
    virtual std::map<std::string, std::string> GetDiagnostics() const = 0;
    virtual void SetLogLevel(int level) = 0;
    
    // ===== RAW ACCESS (for advanced use) =====
    virtual bool SendRaw(const std::vector<uint8_t>& data) { return false; }
    virtual std::vector<uint8_t> ReceiveRaw() { return {}; }
};

// ===== PROTOCOL-SPECIFIC BASE CLASSES =====

class IMatterProtocol : public ISmartHomeProtocol {
public:
    virtual SmartHomeProtocolType GetType() const override { return SmartHomeProtocolType::Matter; }
    
    // Matter-specific features. Fabric objects and attribute subscriptions take
    // Matter types declared in MatterProtocol.h, so they stay backend-only;
    // what is expressible in core types lives here.
    virtual bool OpenCommissioningWindow(const std::string& deviceId,
                                         int timeoutSeconds = 180) = 0;
    virtual bool CloseCommissioningWindow(const std::string& deviceId) = 0;

    virtual std::string GenerateSetupCode(const std::string& deviceId) const = 0;
    virtual std::string GenerateQRCode(const std::string& deviceId) const = 0;

    virtual bool ReadAttribute(const std::string& deviceId, uint16_t endpoint,
                               uint32_t clusterId, uint32_t attributeId,
                               std::string& outValue) = 0;
    virtual bool WriteAttribute(const std::string& deviceId, uint16_t endpoint,
                                uint32_t clusterId, uint32_t attributeId,
                                const std::string& value) = 0;
    virtual bool InvokeCommand(const std::string& deviceId, uint16_t endpoint,
                               uint32_t clusterId, uint32_t commandId,
                               const std::map<std::string, std::string>& fields) = 0;
};

class IThreadProtocol : public ISmartHomeProtocol {
public:
    virtual SmartHomeProtocolType GetType() const override { return SmartHomeProtocolType::Thread; }
    
    // Thread-specific features, carrying the protocol's own widths.
    virtual bool IsBorderRouter() const = 0;
    virtual bool EnableBorderRouter(bool enable) = 0;

    virtual uint64_t GetExtendedPanId() const = 0;
    virtual uint16_t GetPanId() const = 0;
    virtual uint16_t GetChannel() const = 0;

    // Operational and pending datasets, as raw Thread TLVs.
    virtual std::vector<uint8_t> GetActiveDataset() const = 0;
    virtual bool SetActiveDataset(const std::vector<uint8_t>& dataset) = 0;
    virtual std::vector<uint8_t> GetPendingDataset() const = 0;
    virtual bool SetPendingDataset(const std::vector<uint8_t>& dataset) = 0;

    // Commissioner role: a joiner is authorised by EUI-64 plus its PSKd.
    virtual bool StartCommissioner() = 0;
    virtual void StopCommissioner() = 0;
    virtual bool IsCommissionerActive() const = 0;
    virtual bool AddJoiner(const std::string& eui64, const std::string& pskd,
                           uint32_t timeout) = 0;
    virtual bool RemoveJoiner(const std::string& eui64) = 0;

    virtual std::string GetMeshLocalAddress() const = 0;
    virtual std::vector<std::string> GetIPv6Addresses() const = 0;
};

class IZigbeeProtocol : public ISmartHomeProtocol {
public:
    virtual SmartHomeProtocolType GetType() const override { return SmartHomeProtocolType::Zigbee; }
    
    // Zigbee-specific features. These carry the protocol's own widths: a PAN ID
    // is 16 bits, an extended PAN ID 64, a channel 8. The ZCL attribute calls
    // (ReadAttribute / WriteAttribute / ConfigureReporting / SendZCLCommand)
    // are deliberately NOT here: they take Zigbee cluster types declared in
    // ZigbeeProtocol.h, which this header must not depend on.
    virtual uint16_t GetPanId() const = 0;
    virtual uint64_t GetExtendedPanId() const = 0;
    virtual uint8_t GetChannel() const = 0;
    virtual bool SetChannel(int channel) = 0;

    virtual bool TouchLink(int durationSeconds = 60) = 0;  // Zigbee Light Link
    virtual bool FactoryReset(const std::string& deviceId) = 0;
};

class IZWaveProtocol : public ISmartHomeProtocol {
public:
    virtual SmartHomeProtocolType GetType() const override { return SmartHomeProtocolType::ZWave; }
    
    // Z-Wave-specific features
    virtual uint32_t GetHomeId() const = 0;
    virtual uint8_t GetNodeId() const = 0;
    
    virtual bool AddNodeSecure() = 0;  // S2 inclusion
    virtual bool RemoveFailedNode(uint8_t nodeId) = 0;
    virtual bool ReplaceFailedNode(uint8_t nodeId) = 0;
    
    virtual bool HealNetwork() = 0;
    virtual bool HealNode(uint8_t nodeId) = 0;
    
    virtual std::vector<uint8_t> GetCommandClasses(uint8_t nodeId) const = 0;
};

class IKNXProtocol : public ISmartHomeProtocol {
public:
    virtual SmartHomeProtocolType GetType() const override { return SmartHomeProtocolType::KNX; }
    
    // KNX-specific features
    virtual bool ConnectTunnel(const std::string& gatewayIp, int port = 3671) = 0;
    virtual bool ConnectRouting(const std::string& multicastIp = "224.0.23.12") = 0;
    virtual void Disconnect() = 0;
    
    virtual bool SendGroupWrite(const std::string& groupAddress, 
                                const std::vector<uint8_t>& data) = 0;
    virtual bool SendGroupRead(const std::string& groupAddress) = 0;
    
    virtual bool LoadGroupAddresses(const std::string& etsExportPath) = 0;
};

// ===== FACTORY FUNCTION =====

std::shared_ptr<ISmartHomeProtocol> CreateSmartHomeProtocol(SmartHomeProtocolType type);

// ===== PROTOCOL REGISTRATION =====

using ProtocolFactory = std::function<std::shared_ptr<ISmartHomeProtocol>()>;

void RegisterProtocolFactory(SmartHomeProtocolType type, ProtocolFactory factory);
void UnregisterProtocolFactory(SmartHomeProtocolType type);

} // namespace SmartHome
} // namespace UltraCanvas
