// ThreadProtocol.h
// Thread Protocol Implementation (OpenThread)
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework

#pragma once

#include "SmartHomeProtocolBase.h"
#include "ISmartHomeProtocol.h"
#include <memory>
#include <vector>
#include <array>

namespace UltraCanvas {
namespace SmartHome {

/**
 * @brief Thread network operational dataset
 */
struct ThreadDataset {
    std::string NetworkName;                    ///< Network name (max 16 chars)
    uint16_t PanId = 0;                         ///< PAN ID
    uint64_t ExtendedPanId = 0;                 ///< Extended PAN ID
    uint16_t Channel = 0;                       ///< Thread channel (11-26)
    std::array<uint8_t, 16> NetworkKey;         ///< 128-bit network key
    std::array<uint8_t, 16> MeshLocalPrefix;    ///< Mesh-local prefix
    std::array<uint8_t, 32> PSKc;               ///< Pre-Shared Key for Commissioner
    uint32_t ActiveTimestamp = 0;               ///< Dataset timestamp
    uint32_t SecurityPolicy = 0;                ///< Security policy flags
    uint8_t ChannelMask[4] = {0};               ///< Channel mask
};

/**
 * @brief Thread device role
 */
enum class ThreadDeviceRole {
    Disabled,           ///< Thread stack disabled
    Detached,           ///< Not attached to network
    Child,              ///< End device (sleepy or not)
    Router,             ///< Router
    Leader              ///< Network leader
};

/**
 * @brief Thread router information
 */
struct ThreadRouter {
    uint16_t Rloc16 = 0;                        ///< Router RLOC16
    uint8_t RouterId = 0;                       ///< Router ID
    uint64_t ExtAddress = 0;                    ///< Extended address
    bool IsLeader = false;                      ///< Is network leader
    int8_t LinkQualityIn = 0;                   ///< Incoming link quality
    int8_t LinkQualityOut = 0;                  ///< Outgoing link quality
    uint8_t Age = 0;                            ///< Entry age
    bool IsLinkEstablished = false;             ///< Link established
};

/**
 * @brief Thread child information
 */
struct ThreadChild {
    uint16_t Rloc16 = 0;                        ///< Child RLOC16
    uint64_t ExtAddress = 0;                    ///< Extended address
    std::string DeviceId;                       ///< UltraCanvas device ID
    bool IsRxOnWhenIdle = false;                ///< Receiver always on
    bool IsFullThreadDevice = false;            ///< Full Thread device (FTD)
    bool IsFullNetworkData = false;             ///< Has full network data
    uint32_t Timeout = 0;                       ///< Child timeout (seconds)
    uint32_t Age = 0;                           ///< Entry age
    uint32_t LastHeard = 0;                     ///< Last heard timestamp
};

/**
 * @brief Thread network diagnostics
 */
struct ThreadDiagnostics {
    uint32_t PartitionId = 0;                   ///< Network partition ID
    uint8_t LeaderWeight = 0;                   ///< Leader weight
    uint8_t LeaderRouterId = 0;                 ///< Leader router ID
    uint16_t LeaderRloc16 = 0;                  ///< Leader RLOC16
    uint8_t NetworkDataVersion = 0;             ///< Network data version
    uint8_t StableDataVersion = 0;              ///< Stable network data version
    uint16_t RouterCount = 0;                   ///< Number of routers
    uint16_t ChildCount = 0;                    ///< Number of children
    uint32_t Uptime = 0;                        ///< Network uptime (seconds)
};

/**
 * @brief Thread protocol implementation using OpenThread
 * 
 * Implements the Thread mesh networking protocol for low-power,
 * IPv6-based communication. Designed for battery-powered devices
 * and integrates with Matter over Thread.
 * 
 * Key features:
 * - IPv6 mesh networking
 * - Border Router support
 * - Sleepy End Device (SED) support
 * - Multicast and service discovery
 * - Secure commissioning
 */
class ThreadProtocol : public SmartHomeProtocolBase, public IThreadProtocol {
public:
    ThreadProtocol();
    virtual ~ThreadProtocol();
    
    // ===== LIFECYCLE =====
    
    // Both SmartHomeProtocolBase and IThreadProtocol override GetType();
    // under virtual inheritance neither dominates, so the concrete class has
    // to name the winner itself.
    SmartHomeProtocolType GetType() const override {
        return SmartHomeProtocolType::Thread;
    }

    bool Initialize() override;
    void Shutdown() override;
    
    // ===== HARDWARE =====
    
    bool IsHardwareAvailable() const override;
    
    std::string GetHardwareInfo() const override;
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
    std::vector<std::shared_ptr<ISmartHomeDevice>> GetDevices() const override;
    std::shared_ptr<ISmartHomeDevice> GetDevice(const std::string& deviceId) const override;
    std::vector<SmartHomeDeviceInfo> GetPairedDevices() override;
    bool PairDevice(const std::string& deviceId,
                    const std::map<std::string, std::string>& params) override;
    bool UnpairDevice(const std::string& deviceId) override;
    bool GetDeviceState(const std::string& deviceId,
                        std::map<std::string, std::string>& state) override;
    
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
    
    // ===== THREAD-SPECIFIC (IThreadProtocol) =====
    
    bool IsBorderRouter() const override;
    bool EnableBorderRouter(bool enable) override;
    
    uint64_t GetExtendedPanId() const override;
    uint16_t GetPanId() const override;
    uint16_t GetChannel() const override;
    
    std::vector<uint8_t> GetActiveDataset() const override;
    bool SetActiveDataset(const std::vector<uint8_t>& dataset) override;
    
    std::vector<uint8_t> GetPendingDataset() const override;
    bool SetPendingDataset(const std::vector<uint8_t>& dataset) override;
    
    bool StartCommissioner() override;
    void StopCommissioner() override;
    bool IsCommissionerActive() const override;
    
    bool AddJoiner(const std::string& eui64, const std::string& pskd, uint32_t timeout) override;
    bool RemoveJoiner(const std::string& eui64) override;
    
    std::string GetMeshLocalAddress() const override;
    std::vector<std::string> GetIPv6Addresses() const override;
    
    // ===== ADDITIONAL THREAD METHODS =====
    
    // Dataset management
    ThreadDataset GetDataset() const;
    bool SetDataset(const ThreadDataset& dataset);
    bool GenerateDataset(const std::string& networkName, uint16_t channel = 0);
    
    // Role and state
    ThreadDeviceRole GetDeviceRole() const;
    bool IsAttached() const;
    bool IsLeader() const;
    bool IsRouter() const;
    
    // Network state
    ThreadDiagnostics GetThreadDiagnostics() const;
    std::vector<ThreadRouter> GetRouterTable() const;
    std::vector<ThreadChild> GetChildTable() const;
    
    // Neighbor discovery
    std::vector<ThreadRouter> GetNeighborTable() const;
    bool PingDevice(const std::string& address, uint32_t timeoutMs = 2000);
    
    // Multicast
    bool SendMulticast(const std::string& address, const std::vector<uint8_t>& data);
    bool JoinMulticastGroup(const std::string& address);
    bool LeaveMulticastGroup(const std::string& address);
    
    // Service discovery
    bool RegisterService(const std::string& serviceName, uint16_t port,
                         const std::vector<std::pair<std::string, std::string>>& txtRecords);
    bool UnregisterService(const std::string& serviceName);
    std::vector<std::string> DiscoverServices(const std::string& serviceType);
    
    // External commissioning
    bool SetPSKc(const std::string& pskc);
    std::string GetPSKc() const;
    
private:
    // ===== INTERNAL METHODS =====
    
    bool InitializeOpenThread();
    void ShutdownOpenThread();
    
    void ProcessThread();
    void NetworkThread();
    
    void UpdateTables();
    void CheckJoinerTimeouts();
    
    void OnRoleChanged(ThreadDeviceRole newRole);
    void OnStateChanged(bool attached);
    void OnChildAdded(const ThreadChild& child);
    void OnChildRemoved(uint16_t rloc16);
    void OnJoinerEvent(const std::string& eui64, bool joined);
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    // OpenThread callbacks land here. The parameters are untyped because this
    // header does not include OpenThread's; the definitions cast them back.
    void HandleStateChange(uint32_t flags);
    void HandleDiscoveryResult(const void* scanResult);
    void HandleCommissionerState(int state);
    void HandleJoinerEvent(int event, const void* joinerInfo, const void* joinerId);
#endif
    
    SmartHomeDeviceInfo ChildToDeviceInfo(const ThreadChild& child) const;
    std::string Eui64ToString(uint64_t eui64) const;
    uint64_t StringToEui64(const std::string& str) const;
    uint64_t GetSelfExtAddress() const;
    std::string GetMeshLocalPrefix() const;
    std::string RoleToString(ThreadDeviceRole role) const;
    
    // ===== OPENTHREAD WRAPPERS =====
    
    // These would wrap the actual OpenThread API calls
    // Placeholder for openthread integration
    class OpenThreadInstance;
    std::unique_ptr<OpenThreadInstance> openThread;   // not "otInstance": that name is the C typedef
    
    // ===== STATE =====
    
    ThreadDataset activeDataset;
    ThreadDataset pendingDataset;
    ThreadDeviceRole currentRole = ThreadDeviceRole::Disabled;
    std::atomic<bool> borderRouterEnabled{false};
    std::atomic<bool> commissionerActive{false};
    
    // Routers and children
    std::vector<ThreadRouter> routerTable;
    std::vector<ThreadChild> childTable;
    mutable std::mutex tableMutex;
    
    // Joiner management
    struct JoinerEntry {
        std::string Eui64;
        std::string PSKd;
        uint32_t Timeout;
        uint64_t AddedAt;
    };
    std::vector<JoinerEntry> pendingJoiners;
    mutable std::mutex joinerMutex;
    
    // Multicast groups
    std::vector<std::string> multicastGroups;
    
    // Services
    struct ServiceEntry {
        std::string ServiceName;
        uint16_t Port;
        std::vector<std::pair<std::string, std::string>> TxtRecords;
    };
    std::map<std::string, ServiceEntry> registeredServices;
    
    // Groups (for application layer)
    std::map<uint16_t, std::string> groups;
    std::map<uint16_t, std::vector<std::string>> groupMembers;
    
    // Thread processing
    std::thread processThread;
    std::thread networkThread;
    std::atomic<bool> running{false};
    
    // Selected adapter
    std::string selectedAdapter;
    std::string storagePath;
};

/**
 * @brief Factory function for Thread protocol
 */
std::shared_ptr<ISmartHomeProtocol> CreateThreadProtocol();

} // namespace SmartHome
} // namespace UltraCanvas
