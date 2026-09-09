// ThreadProtocol.cpp
// Thread Protocol Implementation (OpenThread)
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework

#include "ThreadProtocol.h"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <random>
#include <cstring>

// OpenThread includes (conditional)
#ifdef ULTRACANVAS_WITH_OPENTHREAD
#include <openthread/instance.h>
#include <openthread/thread.h>
#include <openthread/dataset.h>
#include <openthread/commissioner.h>
#include <openthread/joiner.h>
#include <openthread/border_router.h>
#include <openthread/srp_client.h>
#include <openthread/dns_client.h>
#include <openthread/netdata.h>
#include <openthread/ip6.h>
#include <openthread/icmp6.h>
#include <openthread/link.h>
#include <openthread/tasklet.h>
#include <openthread/platform/radio.h>
#include <openthread/platform/misc.h>
#endif

namespace UltraCanvas {
namespace SmartHome {

// ===== OPENTHREAD INSTANCE WRAPPER =====

class ThreadProtocol::OpenThreadInstance {
public:
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    otInstance* instance = nullptr;
    
    // Callback storage
    ThreadProtocol* protocol = nullptr;
    
    bool Initialize(const std::string& radioUrl) {
        // Initialize platform
        otSysInit(0, nullptr);
        
        // Create instance
        instance = otInstanceInitSingle();
        if (!instance) {
            return false;
        }
        
        return true;
    }
    
    void Shutdown() {
        if (instance) {
            otInstanceFinalize(instance);
            instance = nullptr;
        }
        otSysDeinit();
    }
    
    void Process() {
        if (instance) {
            otTaskletsProcess(instance);
            otSysProcessDrivers(instance);
        }
    }
    
    bool IsInitialized() const {
        return instance != nullptr;
    }
#else
    // Stub implementation when OpenThread is not available
    bool Initialize(const std::string& radioUrl) {
        return true;
    }
    
    void Shutdown() {}
    void Process() {}
    bool IsInitialized() const { return true; }
    
    ThreadProtocol* protocol = nullptr;
#endif
};

// ===== CONSTRUCTOR/DESTRUCTOR =====

ThreadProtocol::ThreadProtocol()
    : SmartHomeProtocolBase(SmartHomeProtocolType::Thread, "Thread")
    , otInstance(std::make_unique<OpenThreadInstance>()) {
    
    protocolVersion = "1.3.0";  // Thread 1.3 specification
    
    // Set Thread capabilities
    SetCapability(ProtocolCapability::Discovery);
    SetCapability(ProtocolCapability::Pairing);
    SetCapability(ProtocolCapability::Mesh);
    SetCapability(ProtocolCapability::Groups);
    SetCapability(ProtocolCapability::Security);
    
    // Initialize network key to zeros
    activeDataset.NetworkKey.fill(0);
    activeDataset.MeshLocalPrefix.fill(0);
    activeDataset.PSKc.fill(0);
}

ThreadProtocol::~ThreadProtocol() {
    if (IsInitialized()) {
        Shutdown();
    }
}

// ===== LIFECYCLE =====

bool ThreadProtocol::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    
    SetState(ProtocolState::Initializing);
    Log(2, "Initializing Thread protocol...");
    
    // Initialize OpenThread
    if (!InitializeOpenThread()) {
        ReportError(-100, "Failed to initialize OpenThread");
        SetState(ProtocolState::Error);
        return false;
    }
    
    // Start processing threads
    running = true;
    processThread = std::thread(&ThreadProtocol::ProcessThread, this);
    networkThread = std::thread(&ThreadProtocol::NetworkThread, this);
    
    SetState(ProtocolState::Ready);
    Log(2, "Thread protocol initialized successfully");
    
    return true;
}

void ThreadProtocol::Shutdown() {
    if (!IsInitialized()) {
        return;
    }
    
    Log(2, "Shutting down Thread protocol...");
    
    // Stop threads
    running = false;
    discovering = false;
    pairing = false;
    commissionerActive = false;
    
    if (processThread.joinable()) {
        processThread.join();
    }
    if (networkThread.joinable()) {
        networkThread.join();
    }
    
    // Clear state
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        routerTable.clear();
        childTable.clear();
    }
    {
        std::lock_guard<std::mutex> lock(joinerMutex);
        pendingJoiners.clear();
    }
    
    multicastGroups.clear();
    registeredServices.clear();
    groups.clear();
    groupMembers.clear();
    
    // Shutdown OpenThread
    ShutdownOpenThread();
    
    currentRole = ThreadDeviceRole::Disabled;
    SetState(ProtocolState::Uninitialized);
    Log(2, "Thread protocol shutdown complete");
}

bool ThreadProtocol::InitializeOpenThread() {
    otInstance->protocol = this;
    
    std::string radioUrl = selectedAdapter.empty() ? "spinel+hdlc+uart:///dev/ttyACM0" : selectedAdapter;
    
    if (!otInstance->Initialize(radioUrl)) {
        return false;
    }
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        // Set state change callback
        otSetStateChangedCallback(otInstance->instance, 
            [](otChangedFlags flags, void* context) {
                auto* self = static_cast<ThreadProtocol*>(context);
                self->HandleStateChange(flags);
            }, this);
        
        // Set default dataset if not already set
        otOperationalDataset dataset;
        if (otDatasetGetActive(otInstance->instance, &dataset) != OT_ERROR_NONE) {
            // No active dataset, we'll need to form or join a network
            Log(3, "No active dataset found");
        }
    }
#endif
    
    Log(3, "OpenThread initialized");
    return true;
}

void ThreadProtocol::ShutdownOpenThread() {
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        // Disable Thread
        otThreadSetEnabled(otInstance->instance, false);
        
        // Stop commissioner if active
        if (commissionerActive) {
            otCommissionerStop(otInstance->instance);
        }
    }
#endif
    
    otInstance->Shutdown();
    Log(3, "OpenThread shutdown");
}

// ===== HARDWARE =====

bool ThreadProtocol::IsHardwareAvailable() const {
    // Check for Thread radio hardware
    auto adapters = GetAvailableAdapters();
    return !adapters.empty();
}

std::vector<std::string> ThreadProtocol::GetAvailableAdapters() const {
    std::vector<std::string> adapters;
    
    // Check common Thread adapter paths
    // Linux: /dev/ttyACM*, /dev/ttyUSB*
    // macOS: /dev/cu.usbmodem*
    
#ifdef __linux__
    // Check for common Thread radio dongles
    const char* patterns[] = {
        "/dev/ttyACM0", "/dev/ttyACM1",
        "/dev/ttyUSB0", "/dev/ttyUSB1",
        "/dev/serial/by-id/*thread*",
        "/dev/serial/by-id/*nrf52*",
        "/dev/serial/by-id/*efr32*"
    };
    
    for (const char* pattern : patterns) {
        // In real implementation, check if device exists
        // For now, add common defaults
        if (strstr(pattern, "*") == nullptr) {
            // Check if file exists
            FILE* f = fopen(pattern, "r");
            if (f) {
                fclose(f);
                adapters.push_back(pattern);
            }
        }
    }
#elif defined(__APPLE__)
    // macOS serial ports
    adapters.push_back("/dev/cu.usbmodem0001");
#endif
    
    // Always provide simulation option for testing
    adapters.push_back("spinel+hdlc+forkpty://ot-rcp?forkpty-arg=1");
    
    return adapters;
}

bool ThreadProtocol::SelectAdapter(const std::string& adapterId) {
    if (IsInitialized()) {
        ReportError(-101, "Cannot change adapter while initialized");
        return false;
    }
    
    selectedAdapter = adapterId;
    Log(2, "Selected adapter: " + adapterId);
    return true;
}

// ===== NETWORK =====

bool ThreadProtocol::FormNetwork(const std::string& networkName) {
    if (!IsInitialized()) {
        return false;
    }
    
    Log(2, "Forming Thread network: " + networkName);
    
    // Generate new dataset
    if (!GenerateDataset(networkName, 0)) {
        ReportError(-200, "Failed to generate dataset");
        return false;
    }
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        otOperationalDataset dataset;
        memset(&dataset, 0, sizeof(dataset));
        
        // Set network name
        strncpy(dataset.mNetworkName.m8, activeDataset.NetworkName.c_str(), 
                OT_NETWORK_NAME_MAX_SIZE);
        dataset.mComponents.mIsNetworkNamePresent = true;
        
        // Set PAN ID
        dataset.mPanId = activeDataset.PanId;
        dataset.mComponents.mIsPanIdPresent = true;
        
        // Set Extended PAN ID
        memcpy(dataset.mExtendedPanId.m8, &activeDataset.ExtendedPanId, 8);
        dataset.mComponents.mIsExtendedPanIdPresent = true;
        
        // Set Channel
        dataset.mChannel = activeDataset.Channel;
        dataset.mComponents.mIsChannelPresent = true;
        
        // Set Network Key
        memcpy(dataset.mNetworkKey.m8, activeDataset.NetworkKey.data(), 16);
        dataset.mComponents.mIsNetworkKeyPresent = true;
        
        // Set Mesh Local Prefix
        memcpy(dataset.mMeshLocalPrefix.m8, activeDataset.MeshLocalPrefix.data(), 8);
        dataset.mComponents.mIsMeshLocalPrefixPresent = true;
        
        // Set PSKc
        memcpy(dataset.mPskc.m8, activeDataset.PSKc.data(), 16);
        dataset.mComponents.mIsPskcPresent = true;
        
        // Set active timestamp
        dataset.mActiveTimestamp.mSeconds = activeDataset.ActiveTimestamp;
        dataset.mActiveTimestamp.mTicks = 0;
        dataset.mActiveTimestamp.mAuthoritative = true;
        dataset.mComponents.mIsActiveTimestampPresent = true;
        
        // Set security policy
        dataset.mSecurityPolicy.mRotationTime = 672;  // hours
        dataset.mSecurityPolicy.mFlags = OT_SECURITY_POLICY_OBTAIN_NETWORK_KEY |
                                         OT_SECURITY_POLICY_NATIVE_COMMISSIONING |
                                         OT_SECURITY_POLICY_ROUTERS |
                                         OT_SECURITY_POLICY_EXTERNAL_COMMISSIONER;
        dataset.mComponents.mIsSecurityPolicyPresent = true;
        
        // Apply dataset
        otError error = otDatasetSetActive(otInstance->instance, &dataset);
        if (error != OT_ERROR_NONE) {
            ReportError(-201, "Failed to set active dataset: " + std::to_string(error));
            return false;
        }
        
        // Enable interface
        error = otIp6SetEnabled(otInstance->instance, true);
        if (error != OT_ERROR_NONE) {
            ReportError(-202, "Failed to enable IPv6: " + std::to_string(error));
            return false;
        }
        
        // Start Thread
        error = otThreadSetEnabled(otInstance->instance, true);
        if (error != OT_ERROR_NONE) {
            ReportError(-203, "Failed to start Thread: " + std::to_string(error));
            return false;
        }
        
        Log(2, "Thread network forming...");
    }
#else
    // Stub mode - simulate network formation
    hasNetwork = true;
    currentRole = ThreadDeviceRole::Leader;
    
    SmartHomeNetworkInfo info;
    info.Protocol = SmartHomeProtocolType::Thread;
    info.NetworkName = activeDataset.NetworkName;
    info.PanId = activeDataset.PanId;
    info.Channel = activeDataset.Channel;
    info.IsOpen = false;
    SetNetworkInfo(info);
    
    OnRoleChanged(ThreadDeviceRole::Leader);
#endif
    
    return true;
}

bool ThreadProtocol::JoinNetwork(const std::string& networkId) {
    if (!IsInitialized()) {
        return false;
    }
    
    Log(2, "Joining Thread network: " + networkId);
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        // Enable interface first
        otError error = otIp6SetEnabled(otInstance->instance, true);
        if (error != OT_ERROR_NONE) {
            ReportError(-210, "Failed to enable IPv6");
            return false;
        }
        
        // Start Thread - will attempt to attach to existing network
        error = otThreadSetEnabled(otInstance->instance, true);
        if (error != OT_ERROR_NONE) {
            ReportError(-211, "Failed to start Thread");
            return false;
        }
    }
#else
    // Stub mode
    hasNetwork = true;
    currentRole = ThreadDeviceRole::Router;
    OnRoleChanged(ThreadDeviceRole::Router);
#endif
    
    return true;
}

bool ThreadProtocol::LeaveNetwork() {
    if (!IsInitialized()) {
        return false;
    }
    
    Log(2, "Leaving Thread network");
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        otThreadSetEnabled(otInstance->instance, false);
        otIp6SetEnabled(otInstance->instance, false);
    }
#endif
    
    hasNetwork = false;
    currentRole = ThreadDeviceRole::Detached;
    ClearNetwork();
    OnRoleChanged(ThreadDeviceRole::Detached);
    
    return true;
}

NetworkTopology ThreadProtocol::GetTopology() const {
    NetworkTopology topology;
    topology.Protocol = SmartHomeProtocolType::Thread;
    
    std::lock_guard<std::mutex> lock(tableMutex);
    
    // Add self as node
    NetworkNode selfNode;
    selfNode.NodeId = Eui64ToString(GetSelfExtAddress());
    selfNode.NodeType = RoleToString(currentRole);
    selfNode.IsRouter = (currentRole == ThreadDeviceRole::Router || 
                         currentRole == ThreadDeviceRole::Leader);
    selfNode.IsOnline = IsAttached();
    topology.Nodes.push_back(selfNode);
    
    // Add routers
    for (const auto& router : routerTable) {
        NetworkNode node;
        node.NodeId = Eui64ToString(router.ExtAddress);
        node.NodeType = router.IsLeader ? "Leader" : "Router";
        node.IsRouter = true;
        node.IsOnline = router.IsLinkEstablished;
        node.LinkQuality = router.LinkQualityIn;
        topology.Nodes.push_back(node);
    }
    
    // Add children
    for (const auto& child : childTable) {
        NetworkNode node;
        node.NodeId = child.DeviceId.empty() ? Eui64ToString(child.ExtAddress) : child.DeviceId;
        node.NodeType = child.IsFullThreadDevice ? "REED" : "SED";
        node.IsRouter = false;
        node.IsOnline = true;  // Children are tracked when they're alive
        node.ParentId = selfNode.NodeId;
        topology.Nodes.push_back(node);
    }
    
    return topology;
}

// ===== DISCOVERY =====

bool ThreadProtocol::StartDiscovery(int timeoutSeconds) {
    if (!IsInitialized() || discovering) {
        return false;
    }
    
    Log(2, "Starting Thread network discovery...");
    discovering = true;
    ClearDiscoveredDevices();
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        // Perform MLE discovery
        otError error = otThreadDiscover(otInstance->instance,
            0,  // scan all channels
            OT_PANID_BROADCAST,
            false,  // joiner
            false,  // enable EUI64 filtering
            [](otActiveScanResult* result, void* context) {
                auto* self = static_cast<ThreadProtocol*>(context);
                if (result) {
                    self->HandleDiscoveryResult(result);
                } else {
                    // Discovery complete
                    self->discovering = false;
                }
            }, this);
        
        if (error != OT_ERROR_NONE) {
            discovering = false;
            ReportError(-300, "Failed to start discovery");
            return false;
        }
    }
#else
    // Stub mode - simulate discovery
    std::thread([this, timeoutSeconds]() {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Simulate finding some devices
        SmartHomeDeviceInfo device1;
        device1.DeviceId = GenerateDeviceId();
        device1.Name = "Thread Sensor";
        device1.Protocol = SmartHomeProtocolType::Thread;
        device1.Category = SmartHomeDeviceCategory::Sensor;
        device1.State = SmartHomeDeviceState::Online;
        AddDiscoveredDevice(device1);
        
        std::this_thread::sleep_for(std::chrono::seconds(timeoutSeconds - 2));
        discovering = false;
    }).detach();
#endif
    
    return true;
}

void ThreadProtocol::StopDiscovery() {
    if (!discovering) {
        return;
    }
    
    Log(2, "Stopping Thread discovery");
    discovering = false;
    
    // OpenThread discovery will complete on its own
}

// ===== PAIRING (COMMISSIONING) =====

bool ThreadProtocol::StartPairing(int timeoutSeconds) {
    if (!IsInitialized() || pairing) {
        return false;
    }
    
    Log(2, "Starting Thread pairing mode (commissioner)...");
    
    if (!StartCommissioner()) {
        return false;
    }
    
    pairing = true;
    
    // Auto-stop after timeout
    std::thread([this, timeoutSeconds]() {
        std::this_thread::sleep_for(std::chrono::seconds(timeoutSeconds));
        if (pairing) {
            StopPairing();
        }
    }).detach();
    
    return true;
}

void ThreadProtocol::StopPairing() {
    if (!pairing) {
        return;
    }
    
    Log(2, "Stopping Thread pairing mode");
    StopCommissioner();
    pairing = false;
    
    // Clear pending joiners
    std::lock_guard<std::mutex> lock(joinerMutex);
    pendingJoiners.clear();
}

bool ThreadProtocol::PermitJoin(int timeoutSeconds) {
    return StartPairing(timeoutSeconds);
}

// ===== COMMISSIONER =====

bool ThreadProtocol::StartCommissioner() {
    if (commissionerActive) {
        return true;
    }
    
    Log(2, "Starting Thread Commissioner...");
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        // Set commissioner state callback
        otCommissionerSetStateCallback(otInstance->instance,
            [](otCommissionerState state, void* context) {
                auto* self = static_cast<ThreadProtocol*>(context);
                self->HandleCommissionerState(state);
            }, this);
        
        // Set joiner callback
        otCommissionerSetJoinerCallback(otInstance->instance,
            [](otCommissionerJoinerEvent event, const otJoinerInfo* joinerInfo,
               const otExtAddress* joinerId, void* context) {
                auto* self = static_cast<ThreadProtocol*>(context);
                self->HandleJoinerEvent(event, joinerInfo, joinerId);
            }, this);
        
        // Start commissioner
        otError error = otCommissionerStart(otInstance->instance, nullptr, nullptr, nullptr);
        if (error != OT_ERROR_NONE) {
            ReportError(-400, "Failed to start commissioner: " + std::to_string(error));
            return false;
        }
    }
#endif
    
    commissionerActive = true;
    return true;
}

void ThreadProtocol::StopCommissioner() {
    if (!commissionerActive) {
        return;
    }
    
    Log(2, "Stopping Thread Commissioner");
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        otCommissionerStop(otInstance->instance);
    }
#endif
    
    commissionerActive = false;
}

bool ThreadProtocol::IsCommissionerActive() const {
    return commissionerActive;
}

bool ThreadProtocol::AddJoiner(const std::string& eui64, const std::string& pskd, uint32_t timeout) {
    if (!commissionerActive) {
        if (!StartCommissioner()) {
            return false;
        }
    }
    
    Log(2, "Adding joiner: " + eui64 + " with timeout " + std::to_string(timeout) + "s");
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        otExtAddress extAddr;
        
        if (eui64 == "*") {
            // Allow any joiner
            memset(&extAddr, 0, sizeof(extAddr));
        } else {
            // Parse EUI64
            uint64_t addr = StringToEui64(eui64);
            memcpy(extAddr.m8, &addr, 8);
        }
        
        otError error = otCommissionerAddJoiner(otInstance->instance,
            eui64 == "*" ? nullptr : &extAddr,
            pskd.c_str(),
            timeout);
        
        if (error != OT_ERROR_NONE) {
            ReportError(-401, "Failed to add joiner");
            return false;
        }
    }
#endif
    
    // Track joiner
    std::lock_guard<std::mutex> lock(joinerMutex);
    JoinerEntry entry;
    entry.Eui64 = eui64;
    entry.PSKd = pskd;
    entry.Timeout = timeout;
    entry.AddedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    pendingJoiners.push_back(entry);
    
    return true;
}

bool ThreadProtocol::RemoveJoiner(const std::string& eui64) {
    Log(2, "Removing joiner: " + eui64);
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance && commissionerActive) {
        otExtAddress extAddr;
        
        if (eui64 != "*") {
            uint64_t addr = StringToEui64(eui64);
            memcpy(extAddr.m8, &addr, 8);
        }
        
        otError error = otCommissionerRemoveJoiner(otInstance->instance,
            eui64 == "*" ? nullptr : &extAddr);
        
        if (error != OT_ERROR_NONE) {
            return false;
        }
    }
#endif
    
    // Remove from tracking
    std::lock_guard<std::mutex> lock(joinerMutex);
    pendingJoiners.erase(
        std::remove_if(pendingJoiners.begin(), pendingJoiners.end(),
            [&eui64](const JoinerEntry& e) { return e.Eui64 == eui64; }),
        pendingJoiners.end());
    
    return true;
}

// ===== DEVICE OPERATIONS =====

bool ThreadProtocol::RemoveDevice(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(tableMutex);
    
    // Find and remove from child table
    auto it = std::find_if(childTable.begin(), childTable.end(),
        [&deviceId](const ThreadChild& c) { return c.DeviceId == deviceId; });
    
    if (it != childTable.end()) {
        uint16_t rloc16 = it->Rloc16;
        childTable.erase(it);
        OnChildRemoved(rloc16);
        RemovePairedDevice(deviceId);
        return true;
    }
    
    return false;
}

bool ThreadProtocol::InterviewDevice(const std::string& deviceId) {
    // Thread devices are automatically interviewed during joining
    // This can trigger a re-interview by sending diagnostic queries
    
    Log(2, "Interviewing Thread device: " + deviceId);
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    // Could send DIAG_GET.req for device diagnostics
#endif
    
    return true;
}

// ===== COMMANDS =====

bool ThreadProtocol::SendCommand(const std::string& deviceId,
                                  const std::string& command,
                                  const std::map<std::string, std::string>& params) {
    // Thread itself doesn't define application-layer commands
    // Commands are typically sent via CoAP or Matter over Thread
    
    Log(3, "SendCommand to " + deviceId + ": " + command);
    
    // Find device IPv6 address
    std::string ipv6Address;
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        for (const auto& child : childTable) {
            if (child.DeviceId == deviceId) {
                // Construct mesh-local address from RLOC16
                // Format: fdxx:xxxx:xxxx:xxxx:0000:00ff:fe00:RLOC16
                std::stringstream ss;
                ss << GetMeshLocalPrefix() << ":0:ff:fe00:" 
                   << std::hex << child.Rloc16;
                ipv6Address = ss.str();
                break;
            }
        }
    }
    
    if (ipv6Address.empty()) {
        ReportError(-500, "Device not found: " + deviceId);
        return false;
    }
    
    // Send CoAP command (would need CoAP implementation)
    // For now, this is a placeholder
    Log(3, "Would send CoAP command to " + ipv6Address);
    
    return true;
}

bool ThreadProtocol::SendGroupCommand(uint16_t groupId,
                                       const std::string& command,
                                       const std::map<std::string, std::string>& params) {
    if (groups.find(groupId) == groups.end()) {
        ReportError(-501, "Group not found");
        return false;
    }
    
    Log(3, "SendGroupCommand to group " + std::to_string(groupId) + ": " + command);
    
    // Thread groups use multicast addresses
    // ff03::1 is all Thread devices
    // Custom groups can use ff03::xxxx
    
    std::stringstream multicastAddr;
    multicastAddr << "ff03::" << std::hex << groupId;
    
    return SendMulticast(multicastAddr.str(), {});
}

// ===== GROUPS =====

bool ThreadProtocol::CreateGroup(uint16_t groupId, const std::string& name) {
    std::lock_guard<std::mutex> lock(tableMutex);
    groups[groupId] = name;
    groupMembers[groupId] = {};
    return true;
}

bool ThreadProtocol::DeleteGroup(uint16_t groupId) {
    std::lock_guard<std::mutex> lock(tableMutex);
    groups.erase(groupId);
    groupMembers.erase(groupId);
    return true;
}

bool ThreadProtocol::AddToGroup(const std::string& deviceId, uint16_t groupId) {
    std::lock_guard<std::mutex> lock(tableMutex);
    
    if (groups.find(groupId) == groups.end()) {
        return false;
    }
    
    auto& members = groupMembers[groupId];
    if (std::find(members.begin(), members.end(), deviceId) == members.end()) {
        members.push_back(deviceId);
    }
    
    return true;
}

bool ThreadProtocol::RemoveFromGroup(const std::string& deviceId, uint16_t groupId) {
    std::lock_guard<std::mutex> lock(tableMutex);
    
    auto it = groupMembers.find(groupId);
    if (it == groupMembers.end()) {
        return false;
    }
    
    auto& members = it->second;
    members.erase(std::remove(members.begin(), members.end(), deviceId), members.end());
    
    return true;
}

std::vector<uint16_t> ThreadProtocol::GetGroups() const {
    std::vector<uint16_t> result;
    std::lock_guard<std::mutex> lock(tableMutex);
    
    for (const auto& [id, name] : groups) {
        result.push_back(id);
    }
    return result;
}

std::vector<std::string> ThreadProtocol::GetGroupMembers(uint16_t groupId) const {
    std::lock_guard<std::mutex> lock(tableMutex);
    
    auto it = groupMembers.find(groupId);
    if (it != groupMembers.end()) {
        return it->second;
    }
    return {};
}

// ===== BINDING =====

bool ThreadProtocol::BindDevices(const std::string& sourceId, const std::string& targetId) {
    // Thread doesn't have native binding - this would be application layer
    Log(3, "Binding not supported at Thread layer");
    return false;
}

bool ThreadProtocol::UnbindDevices(const std::string& sourceId, const std::string& targetId) {
    return false;
}

std::vector<std::string> ThreadProtocol::GetBindings(const std::string& deviceId) const {
    return {};
}

// ===== OTA =====

bool ThreadProtocol::StartOTAUpdate(const std::string& deviceId, const std::string& firmwarePath) {
    // Thread OTA would use CoAP block transfer
    Log(2, "OTA update requested for " + deviceId);
    return false;  // Not implemented
}

int ThreadProtocol::GetOTAProgress(const std::string& deviceId) const {
    return -1;
}

bool ThreadProtocol::CancelOTAUpdate(const std::string& deviceId) {
    return false;
}

// ===== SECURITY =====

SmartHomeSecurityLevel ThreadProtocol::GetSecurityLevel() const {
    return SmartHomeSecurityLevel::Encrypted;  // Thread uses AES-128-CCM with replay protection
}

bool ThreadProtocol::SetNetworkKey(const std::vector<uint8_t>& key) {
    if (key.size() != 16) {
        return false;
    }
    
    std::copy(key.begin(), key.end(), activeDataset.NetworkKey.begin());
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance && hasNetwork) {
        otNetworkKey networkKey;
        memcpy(networkKey.m8, key.data(), 16);
        
        // This requires a pending dataset for live networks
        otOperationalDataset dataset;
        otDatasetGetActive(otInstance->instance, &dataset);
        memcpy(dataset.mNetworkKey.m8, key.data(), 16);
        dataset.mComponents.mIsNetworkKeyPresent = true;
        
        // Schedule update via pending dataset
        return otDatasetSetPending(otInstance->instance, &dataset) == OT_ERROR_NONE;
    }
#endif
    
    return true;
}

// ===== CONFIGURATION =====

bool ThreadProtocol::LoadConfig(const std::string& path) {
    storagePath = path;
    // TODO: Load persistent Thread configuration
    return true;
}

bool ThreadProtocol::SaveConfig(const std::string& path) {
    // TODO: Save Thread configuration
    return true;
}

std::vector<SmartHomeDeviceCategory> ThreadProtocol::GetSupportedDeviceCategories() const {
    // Thread is a transport layer - any device type can use it
    return {
        SmartHomeDeviceCategory::Light,
        SmartHomeDeviceCategory::Switch,
        SmartHomeDeviceCategory::Plug,
        SmartHomeDeviceCategory::Sensor,
        SmartHomeDeviceCategory::Lock,
        SmartHomeDeviceCategory::Thermostat,
        SmartHomeDeviceCategory::Blind
    };
}

// ===== THREAD-SPECIFIC =====

bool ThreadProtocol::IsBorderRouter() const {
    return borderRouterEnabled;
}

bool ThreadProtocol::EnableBorderRouter(bool enable) {
    Log(2, enable ? "Enabling Border Router" : "Disabling Border Router");
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        if (enable) {
            // Enable Border Router features
            // This requires proper Border Router initialization
            // which includes NAT64, DNS64, etc.
            SetCapability(ProtocolCapability::BorderRouter);
        } else {
            SetCapability(ProtocolCapability::BorderRouter, false);
        }
    }
#endif
    
    borderRouterEnabled = enable;
    return true;
}

uint64_t ThreadProtocol::GetExtendedPanId() const {
    return activeDataset.ExtendedPanId;
}

uint16_t ThreadProtocol::GetPanId() const {
    return activeDataset.PanId;
}

uint16_t ThreadProtocol::GetChannel() const {
    return activeDataset.Channel;
}

std::vector<uint8_t> ThreadProtocol::GetActiveDataset() const {
    std::vector<uint8_t> result;
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        otOperationalDatasetTlvs datasetTlvs;
        if (otDatasetGetActiveTlvs(otInstance->instance, &datasetTlvs) == OT_ERROR_NONE) {
            result.assign(datasetTlvs.mTlvs, datasetTlvs.mTlvs + datasetTlvs.mLength);
        }
    }
#endif
    
    return result;
}

bool ThreadProtocol::SetActiveDataset(const std::vector<uint8_t>& dataset) {
    if (dataset.empty() || dataset.size() > 254) {
        return false;
    }
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        otOperationalDatasetTlvs datasetTlvs;
        datasetTlvs.mLength = static_cast<uint8_t>(dataset.size());
        memcpy(datasetTlvs.mTlvs, dataset.data(), dataset.size());
        
        return otDatasetSetActiveTlvs(otInstance->instance, &datasetTlvs) == OT_ERROR_NONE;
    }
#endif
    
    return false;
}

std::vector<uint8_t> ThreadProtocol::GetPendingDataset() const {
    std::vector<uint8_t> result;
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        otOperationalDatasetTlvs datasetTlvs;
        if (otDatasetGetPendingTlvs(otInstance->instance, &datasetTlvs) == OT_ERROR_NONE) {
            result.assign(datasetTlvs.mTlvs, datasetTlvs.mTlvs + datasetTlvs.mLength);
        }
    }
#endif
    
    return result;
}

bool ThreadProtocol::SetPendingDataset(const std::vector<uint8_t>& dataset) {
    if (dataset.empty() || dataset.size() > 254) {
        return false;
    }
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        otOperationalDatasetTlvs datasetTlvs;
        datasetTlvs.mLength = static_cast<uint8_t>(dataset.size());
        memcpy(datasetTlvs.mTlvs, dataset.data(), dataset.size());
        
        return otDatasetSetPendingTlvs(otInstance->instance, &datasetTlvs) == OT_ERROR_NONE;
    }
#endif
    
    return false;
}

std::string ThreadProtocol::GetMeshLocalAddress() const {
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        const otIp6Address* addr = otThreadGetMeshLocalEid(otInstance->instance);
        if (addr) {
            char addrStr[OT_IP6_ADDRESS_STRING_SIZE];
            otIp6AddressToString(addr, addrStr, sizeof(addrStr));
            return std::string(addrStr);
        }
    }
#endif
    
    return "";
}

std::vector<std::string> ThreadProtocol::GetIPv6Addresses() const {
    std::vector<std::string> addresses;
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        const otNetifAddress* addr = otIp6GetUnicastAddresses(otInstance->instance);
        while (addr) {
            char addrStr[OT_IP6_ADDRESS_STRING_SIZE];
            otIp6AddressToString(&addr->mAddress, addrStr, sizeof(addrStr));
            addresses.push_back(std::string(addrStr));
            addr = addr->mNext;
        }
    }
#endif
    
    return addresses;
}

// ===== DATASET MANAGEMENT =====

ThreadDataset ThreadProtocol::GetDataset() const {
    return activeDataset;
}

bool ThreadProtocol::SetDataset(const ThreadDataset& dataset) {
    activeDataset = dataset;
    return true;
}

bool ThreadProtocol::GenerateDataset(const std::string& networkName, uint16_t channel) {
    // Random number generation
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist64;
    std::uniform_int_distribution<uint16_t> dist16;
    std::uniform_int_distribution<uint8_t> dist8;
    
    activeDataset.NetworkName = networkName.substr(0, 16);  // Max 16 chars
    
    // Generate random PAN ID (not 0xFFFF which is broadcast)
    do {
        activeDataset.PanId = dist16(gen);
    } while (activeDataset.PanId == 0xFFFF);
    
    // Generate random Extended PAN ID
    activeDataset.ExtendedPanId = dist64(gen);
    
    // Channel: use provided or pick random from good channels
    if (channel >= 11 && channel <= 26) {
        activeDataset.Channel = channel;
    } else {
        // Prefer channels 15, 20, 25 to avoid WiFi interference
        const uint16_t goodChannels[] = {15, 20, 25, 11, 16, 21, 26};
        activeDataset.Channel = goodChannels[dist8(gen) % 7];
    }
    
    // Generate random Network Key
    for (int i = 0; i < 16; i++) {
        activeDataset.NetworkKey[i] = dist8(gen);
    }
    
    // Generate Mesh Local Prefix (fd00::/8 ULA range)
    activeDataset.MeshLocalPrefix[0] = 0xfd;
    for (int i = 1; i < 8; i++) {
        activeDataset.MeshLocalPrefix[i] = dist8(gen);
    }
    
    // Generate PSKc from network name (simplified)
    // In practice, use pbkdf2 with network name and extended PAN ID
    for (int i = 0; i < 16; i++) {
        activeDataset.PSKc[i] = dist8(gen);
    }
    
    // Timestamp
    activeDataset.ActiveTimestamp = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    
    Log(2, "Generated dataset for network: " + networkName + 
        ", channel: " + std::to_string(activeDataset.Channel));
    
    return true;
}

// ===== ROLE AND STATE =====

ThreadDeviceRole ThreadProtocol::GetDeviceRole() const {
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        otDeviceRole role = otThreadGetDeviceRole(otInstance->instance);
        switch (role) {
            case OT_DEVICE_ROLE_DISABLED: return ThreadDeviceRole::Disabled;
            case OT_DEVICE_ROLE_DETACHED: return ThreadDeviceRole::Detached;
            case OT_DEVICE_ROLE_CHILD: return ThreadDeviceRole::Child;
            case OT_DEVICE_ROLE_ROUTER: return ThreadDeviceRole::Router;
            case OT_DEVICE_ROLE_LEADER: return ThreadDeviceRole::Leader;
        }
    }
#endif
    return currentRole;
}

bool ThreadProtocol::IsAttached() const {
    ThreadDeviceRole role = GetDeviceRole();
    return role == ThreadDeviceRole::Child ||
           role == ThreadDeviceRole::Router ||
           role == ThreadDeviceRole::Leader;
}

bool ThreadProtocol::IsLeader() const {
    return GetDeviceRole() == ThreadDeviceRole::Leader;
}

bool ThreadProtocol::IsRouter() const {
    ThreadDeviceRole role = GetDeviceRole();
    return role == ThreadDeviceRole::Router || role == ThreadDeviceRole::Leader;
}

// ===== NETWORK STATE =====

ThreadDiagnostics ThreadProtocol::GetThreadDiagnostics() const {
    ThreadDiagnostics diag;
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        diag.PartitionId = otThreadGetPartitionId(otInstance->instance);
        
        otLeaderData leaderData;
        if (otThreadGetLeaderData(otInstance->instance, &leaderData) == OT_ERROR_NONE) {
            diag.LeaderWeight = leaderData.mWeighting;
            diag.LeaderRouterId = leaderData.mLeaderRouterId;
            diag.LeaderRloc16 = leaderData.mLeaderRloc;
            diag.NetworkDataVersion = leaderData.mDataVersion;
            diag.StableDataVersion = leaderData.mStableDataVersion;
        }
    }
#endif
    
    std::lock_guard<std::mutex> lock(tableMutex);
    diag.RouterCount = static_cast<uint16_t>(routerTable.size());
    diag.ChildCount = static_cast<uint16_t>(childTable.size());
    
    return diag;
}

std::vector<ThreadRouter> ThreadProtocol::GetRouterTable() const {
    std::lock_guard<std::mutex> lock(tableMutex);
    return routerTable;
}

std::vector<ThreadChild> ThreadProtocol::GetChildTable() const {
    std::lock_guard<std::mutex> lock(tableMutex);
    return childTable;
}

std::vector<ThreadRouter> ThreadProtocol::GetNeighborTable() const {
    std::vector<ThreadRouter> neighbors;
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        otNeighborInfoIterator iterator = OT_NEIGHBOR_INFO_ITERATOR_INIT;
        otNeighborInfo info;
        
        while (otThreadGetNextNeighborInfo(otInstance->instance, &iterator, &info) == OT_ERROR_NONE) {
            ThreadRouter router;
            router.Rloc16 = info.mRloc16;
            router.ExtAddress = *reinterpret_cast<const uint64_t*>(info.mExtAddress.m8);
            router.LinkQualityIn = info.mLinkQualityIn;
            router.LinkQualityOut = info.mLinkQualityOut;
            router.Age = info.mAge;
            router.IsLinkEstablished = info.mRxOnWhenIdle;  // Approximate
            neighbors.push_back(router);
        }
    }
#endif
    
    return neighbors;
}

bool ThreadProtocol::PingDevice(const std::string& address, uint32_t timeoutMs) {
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        otIp6Address destAddr;
        if (otIp6AddressFromString(address.c_str(), &destAddr) != OT_ERROR_NONE) {
            return false;
        }
        
        // Send ICMPv6 echo request
        otError error = otIcmp6SendEchoRequest(otInstance->instance, 
            nullptr,  // Default message info
            &destAddr,
            nullptr, 0);  // No payload
        
        return error == OT_ERROR_NONE;
    }
#endif
    return false;
}

// ===== MULTICAST =====

bool ThreadProtocol::SendMulticast(const std::string& address, const std::vector<uint8_t>& data) {
    Log(3, "Sending multicast to " + address);
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        otIp6Address destAddr;
        if (otIp6AddressFromString(address.c_str(), &destAddr) != OT_ERROR_NONE) {
            return false;
        }
        
        // Would need UDP socket implementation
        // otUdpSend(...)
    }
#endif
    
    return true;
}

bool ThreadProtocol::JoinMulticastGroup(const std::string& address) {
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        otIp6Address groupAddr;
        if (otIp6AddressFromString(address.c_str(), &groupAddr) != OT_ERROR_NONE) {
            return false;
        }
        
        otError error = otIp6SubscribeMulticastAddress(otInstance->instance, &groupAddr);
        if (error == OT_ERROR_NONE) {
            multicastGroups.push_back(address);
            return true;
        }
    }
#else
    multicastGroups.push_back(address);
    return true;
#endif
    
    return false;
}

bool ThreadProtocol::LeaveMulticastGroup(const std::string& address) {
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        otIp6Address groupAddr;
        if (otIp6AddressFromString(address.c_str(), &groupAddr) != OT_ERROR_NONE) {
            return false;
        }
        
        otError error = otIp6UnsubscribeMulticastAddress(otInstance->instance, &groupAddr);
        if (error == OT_ERROR_NONE) {
            multicastGroups.erase(
                std::remove(multicastGroups.begin(), multicastGroups.end(), address),
                multicastGroups.end());
            return true;
        }
    }
#else
    multicastGroups.erase(
        std::remove(multicastGroups.begin(), multicastGroups.end(), address),
        multicastGroups.end());
    return true;
#endif
    
    return false;
}

// ===== SERVICE DISCOVERY =====

bool ThreadProtocol::RegisterService(const std::string& serviceName, uint16_t port,
                                      const std::vector<std::pair<std::string, std::string>>& txtRecords) {
    Log(2, "Registering service: " + serviceName + " on port " + std::to_string(port));
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        // Use SRP Client to register service
        otSrpClientService service;
        // Setup service structure...
        // otSrpClientAddService(otInstance->instance, &service);
    }
#endif
    
    // Track service
    ServiceEntry entry;
    entry.ServiceName = serviceName;
    entry.Port = port;
    entry.TxtRecords = txtRecords;
    registeredServices[serviceName] = entry;
    
    return true;
}

bool ThreadProtocol::UnregisterService(const std::string& serviceName) {
    auto it = registeredServices.find(serviceName);
    if (it == registeredServices.end()) {
        return false;
    }
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    // otSrpClientRemoveService(...)
#endif
    
    registeredServices.erase(it);
    return true;
}

std::vector<std::string> ThreadProtocol::DiscoverServices(const std::string& serviceType) {
    std::vector<std::string> services;
    
    Log(2, "Discovering services: " + serviceType);
    
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        // Use DNS-SD client to discover services
        // otDnsClientResolveService(...)
    }
#endif
    
    return services;
}

bool ThreadProtocol::SetPSKc(const std::string& pskc) {
    if (pskc.size() > 32) {
        return false;
    }
    
    // Store PSKc (simplified - should use proper key derivation)
    std::fill(activeDataset.PSKc.begin(), activeDataset.PSKc.end(), 0);
    std::copy(pskc.begin(), pskc.end(), activeDataset.PSKc.begin());
    
    return true;
}

std::string ThreadProtocol::GetPSKc() const {
    // Return hex-encoded PSKc
    std::stringstream ss;
    for (uint8_t byte : activeDataset.PSKc) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return ss.str();
}

// ===== PROCESSING THREADS =====

void ThreadProtocol::ProcessThread() {
    Log(3, "Process thread started");
    
    while (running) {
        // Process OpenThread tasklets
        otInstance->Process();
        
        // Small sleep to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    Log(3, "Process thread stopped");
}

void ThreadProtocol::NetworkThread() {
    Log(3, "Network thread started");
    
    while (running) {
        // Periodic network maintenance
        
        // Update router/child tables
        UpdateTables();
        
        // Check joiner timeouts
        CheckJoinerTimeouts();
        
        // Update network info
        if (IsAttached()) {
            SmartHomeNetworkInfo info;
            info.Protocol = SmartHomeProtocolType::Thread;
            info.NetworkName = activeDataset.NetworkName;
            info.PanId = activeDataset.PanId;
            info.Channel = activeDataset.Channel;
            info.IsOpen = pairing;
            
            if (!hasNetwork) {
                hasNetwork = true;
                SetNetworkInfo(info);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    
    Log(3, "Network thread stopped");
}

void ThreadProtocol::UpdateTables() {
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (!otInstance->instance) return;
    
    std::lock_guard<std::mutex> lock(tableMutex);
    
    // Update router table
    routerTable.clear();
    otRouterInfo routerInfo;
    for (uint8_t i = 0; i <= OT_NETWORK_MAX_ROUTER_ID; i++) {
        if (otThreadGetRouterInfo(otInstance->instance, i, &routerInfo) == OT_ERROR_NONE) {
            ThreadRouter router;
            router.Rloc16 = routerInfo.mRloc16;
            router.RouterId = routerInfo.mRouterId;
            router.ExtAddress = *reinterpret_cast<const uint64_t*>(routerInfo.mExtAddress.m8);
            router.IsLeader = (router.RouterId == 
                otThreadGetLeaderRouterId(otInstance->instance));
            router.LinkQualityIn = routerInfo.mLinkQualityIn;
            router.LinkQualityOut = routerInfo.mLinkQualityOut;
            router.Age = routerInfo.mAge;
            router.IsLinkEstablished = routerInfo.mLinkEstablished;
            routerTable.push_back(router);
        }
    }
    
    // Update child table
    std::vector<ThreadChild> oldChildTable = childTable;
    childTable.clear();
    
    otChildInfo childInfo;
    uint16_t maxChildren = otThreadGetMaxAllowedChildren(otInstance->instance);
    for (uint16_t i = 0; i < maxChildren; i++) {
        if (otThreadGetChildInfoByIndex(otInstance->instance, i, &childInfo) == OT_ERROR_NONE) {
            ThreadChild child;
            child.Rloc16 = childInfo.mRloc16;
            child.ExtAddress = *reinterpret_cast<const uint64_t*>(childInfo.mExtAddress.m8);
            child.IsRxOnWhenIdle = childInfo.mRxOnWhenIdle;
            child.IsFullThreadDevice = childInfo.mFullThreadDevice;
            child.IsFullNetworkData = childInfo.mFullNetworkData;
            child.Timeout = childInfo.mTimeout;
            child.Age = childInfo.mAge;
            
            // Try to find existing device ID
            for (const auto& old : oldChildTable) {
                if (old.ExtAddress == child.ExtAddress) {
                    child.DeviceId = old.DeviceId;
                    break;
                }
            }
            
            if (child.DeviceId.empty()) {
                child.DeviceId = "thread_" + Eui64ToString(child.ExtAddress);
            }
            
            childTable.push_back(child);
        }
    }
    
    // Detect new children
    for (const auto& child : childTable) {
        bool isNew = true;
        for (const auto& old : oldChildTable) {
            if (old.ExtAddress == child.ExtAddress) {
                isNew = false;
                break;
            }
        }
        if (isNew) {
            OnChildAdded(child);
        }
    }
    
    // Detect removed children
    for (const auto& old : oldChildTable) {
        bool stillPresent = false;
        for (const auto& child : childTable) {
            if (old.ExtAddress == child.ExtAddress) {
                stillPresent = true;
                break;
            }
        }
        if (!stillPresent) {
            OnChildRemoved(old.Rloc16);
        }
    }
#endif
}

void ThreadProtocol::CheckJoinerTimeouts() {
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    std::lock_guard<std::mutex> lock(joinerMutex);
    
    pendingJoiners.erase(
        std::remove_if(pendingJoiners.begin(), pendingJoiners.end(),
            [now](const JoinerEntry& e) {
                return (now - e.AddedAt) > e.Timeout;
            }),
        pendingJoiners.end());
}

// ===== EVENT HANDLERS =====

void ThreadProtocol::OnRoleChanged(ThreadDeviceRole newRole) {
    ThreadDeviceRole oldRole = currentRole;
    currentRole = newRole;
    
    Log(2, "Role changed: " + RoleToString(oldRole) + " -> " + RoleToString(newRole));
    
    // Update protocol state
    if (newRole == ThreadDeviceRole::Disabled || newRole == ThreadDeviceRole::Detached) {
        SetState(ProtocolState::Ready);
    } else {
        SetState(ProtocolState::Ready);  // Attached and operational
    }
}

void ThreadProtocol::OnStateChanged(bool attached) {
    Log(2, attached ? "Network attached" : "Network detached");
    
    if (attached) {
        hasNetwork = true;
    } else {
        hasNetwork = false;
    }
}

void ThreadProtocol::OnChildAdded(const ThreadChild& child) {
    Log(2, "Child joined: " + Eui64ToString(child.ExtAddress));
    
    // Create device info
    SmartHomeDeviceInfo info = ChildToDeviceInfo(child);
    AddPairedDevice(info);
}

void ThreadProtocol::OnChildRemoved(uint16_t rloc16) {
    Log(2, "Child left: RLOC16 " + std::to_string(rloc16));
    
    // Find and remove device
    std::lock_guard<std::mutex> lock(tableMutex);
    for (const auto& child : childTable) {
        if (child.Rloc16 == rloc16) {
            RemovePairedDevice(child.DeviceId);
            break;
        }
    }
}

void ThreadProtocol::OnJoinerEvent(const std::string& eui64, bool joined) {
    if (joined) {
        Log(2, "Joiner completed: " + eui64);
    } else {
        Log(2, "Joiner failed: " + eui64);
    }
}

#ifdef ULTRACANVAS_WITH_OPENTHREAD
void ThreadProtocol::HandleStateChange(otChangedFlags flags) {
    if (flags & OT_CHANGED_THREAD_ROLE) {
        OnRoleChanged(GetDeviceRole());
    }
    
    if (flags & OT_CHANGED_THREAD_PARTITION_ID) {
        Log(3, "Partition ID changed");
    }
    
    if (flags & OT_CHANGED_THREAD_CHILD_ADDED) {
        // Child added - will be picked up in UpdateTables()
    }
    
    if (flags & OT_CHANGED_THREAD_CHILD_REMOVED) {
        // Child removed - will be picked up in UpdateTables()
    }
    
    if (flags & OT_CHANGED_THREAD_NETDATA) {
        Log(3, "Network data changed");
    }
}

void ThreadProtocol::HandleDiscoveryResult(otActiveScanResult* result) {
    if (!result) return;
    
    SmartHomeDeviceInfo device;
    device.DeviceId = "thread_discovered_" + std::to_string(result->mPanId);
    device.Name = std::string(result->mNetworkName.m8);
    device.Protocol = SmartHomeProtocolType::Thread;
    device.Category = SmartHomeDeviceCategory::Gateway;
    device.State = SmartHomeDeviceState::Online;
    
    AddDiscoveredDevice(device);
}

void ThreadProtocol::HandleCommissionerState(otCommissionerState state) {
    switch (state) {
        case OT_COMMISSIONER_STATE_DISABLED:
            commissionerActive = false;
            Log(2, "Commissioner disabled");
            break;
        case OT_COMMISSIONER_STATE_PETITION:
            Log(2, "Commissioner petitioning");
            break;
        case OT_COMMISSIONER_STATE_ACTIVE:
            commissionerActive = true;
            Log(2, "Commissioner active");
            break;
    }
}

void ThreadProtocol::HandleJoinerEvent(otCommissionerJoinerEvent event,
                                        const otJoinerInfo* joinerInfo,
                                        const otExtAddress* joinerId) {
    std::string eui64 = joinerId ? Eui64ToString(*reinterpret_cast<const uint64_t*>(joinerId->m8)) : "*";
    
    switch (event) {
        case OT_COMMISSIONER_JOINER_START:
            Log(2, "Joiner started: " + eui64);
            break;
        case OT_COMMISSIONER_JOINER_CONNECTED:
            Log(2, "Joiner connected: " + eui64);
            break;
        case OT_COMMISSIONER_JOINER_FINALIZE:
            Log(2, "Joiner finalizing: " + eui64);
            break;
        case OT_COMMISSIONER_JOINER_END:
            OnJoinerEvent(eui64, true);
            break;
        case OT_COMMISSIONER_JOINER_REMOVED:
            OnJoinerEvent(eui64, false);
            break;
    }
}
#endif

// ===== HELPERS =====

SmartHomeDeviceInfo ThreadProtocol::ChildToDeviceInfo(const ThreadChild& child) const {
    SmartHomeDeviceInfo info;
    info.DeviceId = child.DeviceId;
    info.Name = "Thread Device " + Eui64ToString(child.ExtAddress).substr(0, 8);
    info.Protocol = SmartHomeProtocolType::Thread;
    info.Category = SmartHomeDeviceCategory::Sensor;  // Default, would be determined by interview
    info.State = SmartHomeDeviceState::Online;
    return info;
}

std::string ThreadProtocol::Eui64ToString(uint64_t eui64) const {
    std::stringstream ss;
    for (int i = 7; i >= 0; i--) {
        ss << std::hex << std::setw(2) << std::setfill('0') 
           << ((eui64 >> (i * 8)) & 0xFF);
        if (i > 0) ss << ":";
    }
    return ss.str();
}

uint64_t ThreadProtocol::StringToEui64(const std::string& str) const {
    uint64_t result = 0;
    std::string clean = str;
    
    // Remove colons
    clean.erase(std::remove(clean.begin(), clean.end(), ':'), clean.end());
    
    if (clean.length() == 16) {
        for (int i = 0; i < 16; i += 2) {
            result = (result << 8) | std::stoul(clean.substr(i, 2), nullptr, 16);
        }
    }
    
    return result;
}

uint64_t ThreadProtocol::GetSelfExtAddress() const {
#ifdef ULTRACANVAS_WITH_OPENTHREAD
    if (otInstance->instance) {
        const otExtAddress* addr = otLinkGetExtendedAddress(otInstance->instance);
        if (addr) {
            return *reinterpret_cast<const uint64_t*>(addr->m8);
        }
    }
#endif
    return 0;
}

std::string ThreadProtocol::GetMeshLocalPrefix() const {
    std::stringstream ss;
    ss << "fd";
    for (int i = 1; i < 8; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') 
           << static_cast<int>(activeDataset.MeshLocalPrefix[i]);
    }
    return ss.str();
}

std::string ThreadProtocol::RoleToString(ThreadDeviceRole role) const {
    switch (role) {
        case ThreadDeviceRole::Disabled: return "Disabled";
        case ThreadDeviceRole::Detached: return "Detached";
        case ThreadDeviceRole::Child: return "Child";
        case ThreadDeviceRole::Router: return "Router";
        case ThreadDeviceRole::Leader: return "Leader";
        default: return "Unknown";
    }
}

// ===== FACTORY FUNCTION =====

std::shared_ptr<ISmartHomeProtocol> CreateThreadProtocol() {
    return std::make_shared<ThreadProtocol>();
}

} // namespace SmartHome
} // namespace UltraCanvas
