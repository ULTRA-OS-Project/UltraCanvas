// ZigbeeProtocol.cpp
// Zigbee Protocol Implementation
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework

#include "ZigbeeProtocol.h"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <random>
#include <cstring>
#include <queue>
#include <condition_variable>

// Conditional Zigbee stack includes
#ifdef ULTRACANVAS_WITH_EZSP
// Silicon Labs EZSP (Ember Serial Protocol)
#include "ezsp/ezsp.h"
#include "ezsp/ash-host.h"
#endif

#ifdef ULTRACANVAS_WITH_ZSTACK
// Texas Instruments Z-Stack
#include "znp/znp.h"
#endif

namespace UltraCanvas {
namespace SmartHome {

// ===== ZCL FRAME TYPES =====
namespace ZCL {
    // Frame control field
    constexpr uint8_t FrameTypeGlobal = 0x00;
    constexpr uint8_t FrameTypeClusterSpecific = 0x01;
    constexpr uint8_t DirectionClientToServer = 0x00;
    constexpr uint8_t DirectionServerToClient = 0x08;
    constexpr uint8_t DisableDefaultResponse = 0x10;
    
    // Global commands
    constexpr uint8_t CmdReadAttributes = 0x00;
    constexpr uint8_t CmdReadAttributesResponse = 0x01;
    constexpr uint8_t CmdWriteAttributes = 0x02;
    constexpr uint8_t CmdWriteAttributesResponse = 0x04;
    constexpr uint8_t CmdConfigureReporting = 0x06;
    constexpr uint8_t CmdConfigureReportingResponse = 0x07;
    constexpr uint8_t CmdReadReportingConfiguration = 0x08;
    constexpr uint8_t CmdReportAttributes = 0x0A;
    constexpr uint8_t CmdDefaultResponse = 0x0B;
    constexpr uint8_t CmdDiscoverAttributes = 0x0C;
    constexpr uint8_t CmdDiscoverAttributesResponse = 0x0D;
    
    // On/Off cluster commands
    constexpr uint8_t OnOffCmdOff = 0x00;
    constexpr uint8_t OnOffCmdOn = 0x01;
    constexpr uint8_t OnOffCmdToggle = 0x02;
    constexpr uint8_t OnOffCmdOffWithEffect = 0x40;
    constexpr uint8_t OnOffCmdOnWithRecallGlobalScene = 0x41;
    constexpr uint8_t OnOffCmdOnWithTimedOff = 0x42;
    
    // Level control cluster commands
    constexpr uint8_t LevelCmdMoveToLevel = 0x00;
    constexpr uint8_t LevelCmdMove = 0x01;
    constexpr uint8_t LevelCmdStep = 0x02;
    constexpr uint8_t LevelCmdStop = 0x03;
    constexpr uint8_t LevelCmdMoveToLevelWithOnOff = 0x04;
    
    // Color control cluster commands
    constexpr uint8_t ColorCmdMoveToHue = 0x00;
    constexpr uint8_t ColorCmdMoveHue = 0x01;
    constexpr uint8_t ColorCmdStepHue = 0x02;
    constexpr uint8_t ColorCmdMoveToSaturation = 0x03;
    constexpr uint8_t ColorCmdMoveToHueAndSaturation = 0x06;
    constexpr uint8_t ColorCmdMoveToColor = 0x07;
    constexpr uint8_t ColorCmdMoveToColorTemperature = 0x0A;
    constexpr uint8_t ColorCmdEnhancedMoveToHue = 0x40;
    constexpr uint8_t ColorCmdEnhancedMoveToHueAndSaturation = 0x43;
    constexpr uint8_t ColorCmdColorLoopSet = 0x44;
    
    // Groups cluster commands
    constexpr uint8_t GroupsCmdAddGroup = 0x00;
    constexpr uint8_t GroupsCmdViewGroup = 0x01;
    constexpr uint8_t GroupsCmdGetGroupMembership = 0x02;
    constexpr uint8_t GroupsCmdRemoveGroup = 0x03;
    constexpr uint8_t GroupsCmdRemoveAllGroups = 0x04;
    constexpr uint8_t GroupsCmdAddGroupIfIdentifying = 0x05;
    
    // Scenes cluster commands
    constexpr uint8_t ScenesCmdAddScene = 0x00;
    constexpr uint8_t ScenesCmdViewScene = 0x01;
    constexpr uint8_t ScenesCmdRemoveScene = 0x02;
    constexpr uint8_t ScenesCmdRemoveAllScenes = 0x03;
    constexpr uint8_t ScenesCmdStoreScene = 0x04;
    constexpr uint8_t ScenesCmdRecallScene = 0x05;
    constexpr uint8_t ScenesCmdGetSceneMembership = 0x06;
    
    // Door lock cluster commands
    constexpr uint8_t DoorLockCmdLock = 0x00;
    constexpr uint8_t DoorLockCmdUnlock = 0x01;
    constexpr uint8_t DoorLockCmdToggle = 0x02;
    
    // Window covering cluster commands
    constexpr uint8_t WindowCmdUpOpen = 0x00;
    constexpr uint8_t WindowCmdDownClose = 0x01;
    constexpr uint8_t WindowCmdStop = 0x02;
    constexpr uint8_t WindowCmdGoToLiftPercentage = 0x05;
    constexpr uint8_t WindowCmdGoToTiltPercentage = 0x08;
    
    // Data types
    constexpr uint8_t TypeNoData = 0x00;
    constexpr uint8_t TypeData8 = 0x08;
    constexpr uint8_t TypeData16 = 0x09;
    constexpr uint8_t TypeData24 = 0x0A;
    constexpr uint8_t TypeData32 = 0x0B;
    constexpr uint8_t TypeBoolean = 0x10;
    constexpr uint8_t TypeBitmap8 = 0x18;
    constexpr uint8_t TypeBitmap16 = 0x19;
    constexpr uint8_t TypeUint8 = 0x20;
    constexpr uint8_t TypeUint16 = 0x21;
    constexpr uint8_t TypeUint24 = 0x22;
    constexpr uint8_t TypeUint32 = 0x23;
    constexpr uint8_t TypeUint48 = 0x25;
    constexpr uint8_t TypeInt8 = 0x28;
    constexpr uint8_t TypeInt16 = 0x29;
    constexpr uint8_t TypeEnum8 = 0x30;
    constexpr uint8_t TypeEnum16 = 0x31;
    constexpr uint8_t TypeSingle = 0x39;
    constexpr uint8_t TypeOctetString = 0x41;
    constexpr uint8_t TypeCharString = 0x42;
    constexpr uint8_t TypeLongOctetString = 0x43;
    constexpr uint8_t TypeLongCharString = 0x44;
    constexpr uint8_t TypeArray = 0x48;
    constexpr uint8_t TypeStruct = 0x4C;
    constexpr uint8_t TypeIeeeAddress = 0xF0;
    constexpr uint8_t TypeSecurityKey = 0xF1;
}

// ===== BASIC CLUSTER ATTRIBUTES =====
namespace BasicAttributes {
    constexpr uint16_t ZCLVersion = 0x0000;
    constexpr uint16_t ApplicationVersion = 0x0001;
    constexpr uint16_t StackVersion = 0x0002;
    constexpr uint16_t HWVersion = 0x0003;
    constexpr uint16_t ManufacturerName = 0x0004;
    constexpr uint16_t ModelIdentifier = 0x0005;
    constexpr uint16_t DateCode = 0x0006;
    constexpr uint16_t PowerSource = 0x0007;
    constexpr uint16_t SWBuildId = 0x4000;
}

// ===== ZIGBEE STACK WRAPPER =====

class ZigbeeProtocol::ZigbeeStack {
public:
    ZigbeeProtocol* protocol = nullptr;
    
    // Serial port for USB dongle communication
    std::string serialPort;
    int serialFd = -1;
    int baudRate = 115200;
    
    // Sequence numbers
    uint8_t apsSequence = 0;
    uint8_t zclSequence = 0;
    
    // Pending requests
    struct PendingRequest {
        uint8_t Sequence;
        uint16_t ClusterId;
        std::function<void(bool success, const std::vector<uint8_t>& data)> Callback;
        uint64_t Timestamp;
    };
    std::map<uint8_t, PendingRequest> pendingRequests;
    std::mutex requestMutex;
    
    // Receive buffer
    std::vector<uint8_t> receiveBuffer;
    std::mutex bufferMutex;
    
    // Coordinator info
    uint64_t coordinatorIeee = 0;
    uint16_t coordinatorNwk = 0x0000;
    
    bool Initialize(const std::string& port) {
        serialPort = port;
        
#ifdef ULTRACANVAS_WITH_EZSP
        // Initialize EZSP over ASH
        if (!InitializeEZSP()) {
            return false;
        }
#elif defined(ULTRACANVAS_WITH_ZSTACK)
        // Initialize Z-Stack ZNP
        if (!InitializeZStack()) {
            return false;
        }
#else
        // Stub mode
        Log(2, "Zigbee stack initialized in stub mode");
#endif
        
        return true;
    }
    
    void Shutdown() {
#ifdef ULTRACANVAS_WITH_EZSP
        ShutdownEZSP();
#elif defined(ULTRACANVAS_WITH_ZSTACK)
        ShutdownZStack();
#endif
        
        if (serialFd >= 0) {
            close(serialFd);
            serialFd = -1;
        }
    }
    
    bool IsInitialized() const {
        return serialFd >= 0 || true;  // True in stub mode
    }
    
    void Process() {
        // Process incoming data from serial port
#if defined(ULTRACANVAS_WITH_EZSP) || defined(ULTRACANVAS_WITH_ZSTACK)
        ProcessSerial();
#endif
        
        // Check for timed out requests
        CheckTimeouts();
    }
    
    // ===== NETWORK OPERATIONS =====
    
    bool FormNetwork(uint16_t panId, uint8_t channel, const std::array<uint8_t, 16>& networkKey) {
#ifdef ULTRACANVAS_WITH_EZSP
        return EZSP_FormNetwork(panId, channel, networkKey);
#elif defined(ULTRACANVAS_WITH_ZSTACK)
        return ZStack_FormNetwork(panId, channel, networkKey);
#else
        // Stub mode - simulate network formation
        coordinatorNwk = 0x0000;
        coordinatorIeee = 0x00124B0001234567ULL;  // Simulated IEEE
        return true;
#endif
    }
    
    bool PermitJoining(uint8_t duration) {
#ifdef ULTRACANVAS_WITH_EZSP
        return EZSP_PermitJoining(duration);
#elif defined(ULTRACANVAS_WITH_ZSTACK)
        return ZStack_PermitJoining(duration);
#else
        return true;
#endif
    }
    
    bool LeaveNetwork() {
#ifdef ULTRACANVAS_WITH_EZSP
        return EZSP_LeaveNetwork();
#elif defined(ULTRACANVAS_WITH_ZSTACK)
        return ZStack_LeaveNetwork();
#else
        return true;
#endif
    }
    
    // ===== ZCL OPERATIONS =====
    
    bool SendZCLFrame(uint16_t nwkAddress, uint8_t endpoint, uint16_t clusterId,
                      uint8_t frameControl, uint8_t commandId,
                      const std::vector<uint8_t>& payload,
                      std::function<void(bool, const std::vector<uint8_t>&)> callback = nullptr) {
        
        // Build ZCL frame
        std::vector<uint8_t> zclFrame;
        zclFrame.push_back(frameControl);
        zclFrame.push_back(zclSequence++);
        zclFrame.push_back(commandId);
        zclFrame.insert(zclFrame.end(), payload.begin(), payload.end());
        
        // Track pending request
        if (callback) {
            std::lock_guard<std::mutex> lock(requestMutex);
            PendingRequest req;
            req.Sequence = zclFrame[1];
            req.ClusterId = clusterId;
            req.Callback = callback;
            req.Timestamp = GetTimestamp();
            pendingRequests[req.Sequence] = req;
        }
        
#ifdef ULTRACANVAS_WITH_EZSP
        return EZSP_SendUnicast(nwkAddress, endpoint, clusterId, zclFrame);
#elif defined(ULTRACANVAS_WITH_ZSTACK)
        return ZStack_SendAF(nwkAddress, endpoint, clusterId, zclFrame);
#else
        // Stub mode - simulate response
        if (callback) {
            std::thread([callback]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                callback(true, {});
            }).detach();
        }
        return true;
#endif
    }
    
    bool SendZCLGroupFrame(uint16_t groupId, uint8_t endpoint, uint16_t clusterId,
                           uint8_t frameControl, uint8_t commandId,
                           const std::vector<uint8_t>& payload) {
        
        std::vector<uint8_t> zclFrame;
        zclFrame.push_back(frameControl);
        zclFrame.push_back(zclSequence++);
        zclFrame.push_back(commandId);
        zclFrame.insert(zclFrame.end(), payload.begin(), payload.end());
        
#ifdef ULTRACANVAS_WITH_EZSP
        return EZSP_SendMulticast(groupId, endpoint, clusterId, zclFrame);
#elif defined(ULTRACANVAS_WITH_ZSTACK)
        return ZStack_SendAFGroup(groupId, endpoint, clusterId, zclFrame);
#else
        return true;
#endif
    }
    
    // ===== ZDO OPERATIONS =====
    
    bool RequestActiveEndpoints(uint16_t nwkAddress,
                                std::function<void(bool, const std::vector<uint8_t>&)> callback) {
#ifdef ULTRACANVAS_WITH_EZSP
        return EZSP_ActiveEndpoints(nwkAddress, callback);
#elif defined(ULTRACANVAS_WITH_ZSTACK)
        return ZStack_ActiveEndpoints(nwkAddress, callback);
#else
        // Stub - return endpoint 1
        if (callback) {
            std::thread([callback]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                callback(true, {1});  // Endpoint 1
            }).detach();
        }
        return true;
#endif
    }
    
    bool RequestSimpleDescriptor(uint16_t nwkAddress, uint8_t endpoint,
                                 std::function<void(bool, const ZigbeeEndpoint&)> callback) {
#ifdef ULTRACANVAS_WITH_EZSP
        return EZSP_SimpleDescriptor(nwkAddress, endpoint, callback);
#elif defined(ULTRACANVAS_WITH_ZSTACK)
        return ZStack_SimpleDescriptor(nwkAddress, endpoint, callback);
#else
        // Stub - return basic endpoint
        if (callback) {
            std::thread([callback, endpoint]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                ZigbeeEndpoint ep;
                ep.EndpointId = endpoint;
                ep.ProfileId = ZigbeeProfiles::HomeAutomation;
                ep.DeviceId = ZigbeeDeviceIds::OnOffLight;
                
                ZigbeeCluster onOff;
                onOff.ClusterId = ZigbeeClusters::OnOff;
                onOff.IsServer = true;
                ep.InputClusters.push_back(onOff);
                
                ZigbeeCluster basic;
                basic.ClusterId = ZigbeeClusters::Basic;
                basic.IsServer = true;
                ep.InputClusters.push_back(basic);
                
                callback(true, ep);
            }).detach();
        }
        return true;
#endif
    }
    
    bool RequestNodeDescriptor(uint16_t nwkAddress,
                               std::function<void(bool, ZigbeeDeviceType)> callback) {
#ifdef ULTRACANVAS_WITH_EZSP
        return EZSP_NodeDescriptor(nwkAddress, callback);
#elif defined(ULTRACANVAS_WITH_ZSTACK)
        return ZStack_NodeDescriptor(nwkAddress, callback);
#else
        if (callback) {
            std::thread([callback]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                callback(true, ZigbeeDeviceType::EndDevice);
            }).detach();
        }
        return true;
#endif
    }
    
    bool RequestIeeeAddress(uint16_t nwkAddress,
                            std::function<void(bool, uint64_t)> callback) {
#ifdef ULTRACANVAS_WITH_EZSP
        return EZSP_IeeeAddress(nwkAddress, callback);
#elif defined(ULTRACANVAS_WITH_ZSTACK)
        return ZStack_IeeeAddress(nwkAddress, callback);
#else
        if (callback) {
            std::thread([callback, nwkAddress]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                // Generate fake IEEE from nwk address
                callback(true, 0x00124B0000000000ULL | nwkAddress);
            }).detach();
        }
        return true;
#endif
    }
    
    bool BindRequest(uint64_t srcIeee, uint8_t srcEndpoint, uint16_t clusterId,
                     uint64_t dstIeee, uint8_t dstEndpoint) {
#ifdef ULTRACANVAS_WITH_EZSP
        return EZSP_Bind(srcIeee, srcEndpoint, clusterId, dstIeee, dstEndpoint);
#elif defined(ULTRACANVAS_WITH_ZSTACK)
        return ZStack_Bind(srcIeee, srcEndpoint, clusterId, dstIeee, dstEndpoint);
#else
        return true;
#endif
    }
    
    bool UnbindRequest(uint64_t srcIeee, uint8_t srcEndpoint, uint16_t clusterId,
                       uint64_t dstIeee, uint8_t dstEndpoint) {
#ifdef ULTRACANVAS_WITH_EZSP
        return EZSP_Unbind(srcIeee, srcEndpoint, clusterId, dstIeee, dstEndpoint);
#elif defined(ULTRACANVAS_WITH_ZSTACK)
        return ZStack_Unbind(srcIeee, srcEndpoint, clusterId, dstIeee, dstEndpoint);
#else
        return true;
#endif
    }
    
    bool RemoveNode(uint64_t ieeeAddress) {
#ifdef ULTRACANVAS_WITH_EZSP
        return EZSP_RemoveDevice(ieeeAddress);
#elif defined(ULTRACANVAS_WITH_ZSTACK)
        return ZStack_RemoveDevice(ieeeAddress);
#else
        return true;
#endif
    }

private:
    void ProcessSerial() {
        // Read from serial and process frames
        // Implementation depends on EZSP vs Z-Stack protocol
    }
    
    void CheckTimeouts() {
        uint64_t now = GetTimestamp();
        std::lock_guard<std::mutex> lock(requestMutex);
        
        for (auto it = pendingRequests.begin(); it != pendingRequests.end();) {
            if (now - it->second.Timestamp > 10000) {  // 10 second timeout
                if (it->second.Callback) {
                    it->second.Callback(false, {});
                }
                it = pendingRequests.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    uint64_t GetTimestamp() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    
    void Log(int level, const std::string& message) {
        if (protocol) {
            // Use protocol's logging
        }
    }

#ifdef ULTRACANVAS_WITH_EZSP
    bool InitializeEZSP() {
        // Initialize EZSP over ASH protocol
        // This would set up the serial connection and EZSP handshake
        return false;  // Not implemented
    }
    
    void ShutdownEZSP() {}
    
    bool EZSP_FormNetwork(uint16_t panId, uint8_t channel, 
                          const std::array<uint8_t, 16>& networkKey) { return false; }
    bool EZSP_PermitJoining(uint8_t duration) { return false; }
    bool EZSP_LeaveNetwork() { return false; }
    bool EZSP_SendUnicast(uint16_t nwk, uint8_t ep, uint16_t cluster, 
                          const std::vector<uint8_t>& data) { return false; }
    bool EZSP_SendMulticast(uint16_t group, uint8_t ep, uint16_t cluster,
                            const std::vector<uint8_t>& data) { return false; }
    bool EZSP_ActiveEndpoints(uint16_t nwk, 
                              std::function<void(bool, const std::vector<uint8_t>&)> cb) { return false; }
    bool EZSP_SimpleDescriptor(uint16_t nwk, uint8_t ep,
                               std::function<void(bool, const ZigbeeEndpoint&)> cb) { return false; }
    bool EZSP_NodeDescriptor(uint16_t nwk, 
                             std::function<void(bool, ZigbeeDeviceType)> cb) { return false; }
    bool EZSP_IeeeAddress(uint16_t nwk,
                          std::function<void(bool, uint64_t)> cb) { return false; }
    bool EZSP_Bind(uint64_t src, uint8_t srcEp, uint16_t cluster,
                   uint64_t dst, uint8_t dstEp) { return false; }
    bool EZSP_Unbind(uint64_t src, uint8_t srcEp, uint16_t cluster,
                     uint64_t dst, uint8_t dstEp) { return false; }
    bool EZSP_RemoveDevice(uint64_t ieee) { return false; }
#endif

#ifdef ULTRACANVAS_WITH_ZSTACK
    bool InitializeZStack() {
        // Initialize Z-Stack ZNP protocol
        return false;  // Not implemented
    }
    
    void ShutdownZStack() {}
    
    bool ZStack_FormNetwork(uint16_t panId, uint8_t channel,
                            const std::array<uint8_t, 16>& networkKey) { return false; }
    bool ZStack_PermitJoining(uint8_t duration) { return false; }
    bool ZStack_LeaveNetwork() { return false; }
    bool ZStack_SendAF(uint16_t nwk, uint8_t ep, uint16_t cluster,
                       const std::vector<uint8_t>& data) { return false; }
    bool ZStack_SendAFGroup(uint16_t group, uint8_t ep, uint16_t cluster,
                            const std::vector<uint8_t>& data) { return false; }
    bool ZStack_ActiveEndpoints(uint16_t nwk,
                                std::function<void(bool, const std::vector<uint8_t>&)> cb) { return false; }
    bool ZStack_SimpleDescriptor(uint16_t nwk, uint8_t ep,
                                 std::function<void(bool, const ZigbeeEndpoint&)> cb) { return false; }
    bool ZStack_NodeDescriptor(uint16_t nwk,
                               std::function<void(bool, ZigbeeDeviceType)> cb) { return false; }
    bool ZStack_IeeeAddress(uint16_t nwk,
                            std::function<void(bool, uint64_t)> cb) { return false; }
    bool ZStack_Bind(uint64_t src, uint8_t srcEp, uint16_t cluster,
                     uint64_t dst, uint8_t dstEp) { return false; }
    bool ZStack_Unbind(uint64_t src, uint8_t srcEp, uint16_t cluster,
                       uint64_t dst, uint8_t dstEp) { return false; }
    bool ZStack_RemoveDevice(uint64_t ieee) { return false; }
#endif
};

// ===== CONSTRUCTOR/DESTRUCTOR =====

ZigbeeProtocol::ZigbeeProtocol()
    : SmartHomeProtocolBase(SmartHomeProtocolType::Zigbee, "Zigbee")
    , stack(std::make_unique<ZigbeeStack>()) {
    
    protocolVersion = "3.0";  // Zigbee 3.0 specification
    
    // Set Zigbee capabilities
    SetCapability(ProtocolCapability::Discovery);
    SetCapability(ProtocolCapability::Pairing);
    SetCapability(ProtocolCapability::Mesh);
    SetCapability(ProtocolCapability::Binding);
    SetCapability(ProtocolCapability::Groups);
    SetCapability(ProtocolCapability::OTA);
    SetCapability(ProtocolCapability::Security);
    
    // Initialize network key
    networkParams.NetworkKey.fill(0);
}

ZigbeeProtocol::~ZigbeeProtocol() {
    if (IsInitialized()) {
        Shutdown();
    }
}

// ===== LIFECYCLE =====

bool ZigbeeProtocol::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    
    SetState(ProtocolState::Initializing);
    Log(2, "Initializing Zigbee protocol...");
    
    // Initialize Zigbee stack
    if (!InitializeStack()) {
        ReportError(-100, "Failed to initialize Zigbee stack");
        SetState(ProtocolState::Error);
        return false;
    }
    
    // Start processing threads
    running = true;
    processThread = std::thread(&ZigbeeProtocol::ProcessZigbee, this);
    networkThread = std::thread(&ZigbeeProtocol::NetworkThread, this);
    
    SetState(ProtocolState::Ready);
    Log(2, "Zigbee protocol initialized successfully");
    
    return true;
}

void ZigbeeProtocol::Shutdown() {
    if (!IsInitialized()) {
        return;
    }
    
    Log(2, "Shutting down Zigbee protocol...");
    
    // Stop threads
    running = false;
    permitJoinActive = false;
    
    if (processThread.joinable()) {
        processThread.join();
    }
    if (networkThread.joinable()) {
        networkThread.join();
    }
    
    // Clear state
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        zigbeeNodes.clear();
        ieeeToDeviceId.clear();
        nwkToDeviceId.clear();
    }
    
    groups.clear();
    groupMembers.clear();
    scenes.clear();
    deviceBindings.clear();
    greenPowerDevices.clear();
    
    // Shutdown stack
    ShutdownStack();
    
    SetState(ProtocolState::Uninitialized);
    Log(2, "Zigbee protocol shutdown complete");
}

bool ZigbeeProtocol::InitializeStack() {
    stack->protocol = this;
    
    std::string port = selectedAdapter.empty() ? "/dev/ttyUSB0" : selectedAdapter;
    
    if (!stack->Initialize(port)) {
        return false;
    }
    
    Log(3, "Zigbee stack initialized");
    return true;
}

void ZigbeeProtocol::ShutdownStack() {
    stack->Shutdown();
    Log(3, "Zigbee stack shutdown");
}

// ===== HARDWARE =====

bool ZigbeeProtocol::IsHardwareAvailable() const {
    auto adapters = GetAvailableAdapters();
    return !adapters.empty();
}

std::vector<std::string> ZigbeeProtocol::GetAvailableAdapters() const {
    std::vector<std::string> adapters;
    
#ifdef __linux__
    // Check for common Zigbee adapter paths
    const char* patterns[] = {
        "/dev/ttyUSB0", "/dev/ttyUSB1",
        "/dev/ttyACM0", "/dev/ttyACM1",
        "/dev/serial/by-id/*conbee*",
        "/dev/serial/by-id/*cc2531*",
        "/dev/serial/by-id/*cc2652*",
        "/dev/serial/by-id/*sonoff*"
    };
    
    for (const char* pattern : patterns) {
        if (strstr(pattern, "*") == nullptr) {
            FILE* f = fopen(pattern, "r");
            if (f) {
                fclose(f);
                adapters.push_back(pattern);
            }
        }
    }
#elif defined(__APPLE__)
    adapters.push_back("/dev/cu.usbserial-0001");
    adapters.push_back("/dev/cu.usbmodem0001");
#elif defined(_WIN32)
    // Check COM ports
    for (int i = 1; i <= 20; i++) {
        std::string port = "COM" + std::to_string(i);
        // Would check if port exists
    }
#endif
    
    // Simulation adapter for testing
    adapters.push_back("simulation://zigbee");
    
    return adapters;
}

bool ZigbeeProtocol::SelectAdapter(const std::string& adapterId) {
    if (IsInitialized()) {
        ReportError(-101, "Cannot change adapter while initialized");
        return false;
    }
    
    selectedAdapter = adapterId;
    Log(2, "Selected Zigbee adapter: " + adapterId);
    return true;
}

// ===== NETWORK =====

bool ZigbeeProtocol::FormNetwork(const std::string& networkName) {
    if (!IsInitialized()) {
        return false;
    }
    
    Log(2, "Forming Zigbee network...");
    
    // Generate network parameters if not set
    if (networkParams.PanId == 0) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint16_t> dist16;
        std::uniform_int_distribution<uint8_t> dist8;
        
        // Random PAN ID (not 0xFFFF)
        do {
            networkParams.PanId = dist16(gen);
        } while (networkParams.PanId == 0xFFFF);
        
        // Random Extended PAN ID
        networkParams.ExtendedPanId = ((uint64_t)dist16(gen) << 48) |
                                       ((uint64_t)dist16(gen) << 32) |
                                       ((uint64_t)dist16(gen) << 16) |
                                       dist16(gen);
        
        // Channel - prefer 15, 20, 25 to avoid WiFi
        const uint8_t goodChannels[] = {15, 20, 25, 11, 16, 21, 26};
        networkParams.Channel = goodChannels[dist8(gen) % 7];
        
        // Random network key
        for (int i = 0; i < 16; i++) {
            networkParams.NetworkKey[i] = dist8(gen);
        }
    }
    
    if (!stack->FormNetwork(networkParams.PanId, networkParams.Channel, 
                            networkParams.NetworkKey)) {
        ReportError(-200, "Failed to form network");
        return false;
    }
    
    hasNetwork = true;
    
    SmartHomeNetworkInfo info;
    info.Protocol = SmartHomeProtocolType::Zigbee;
    info.NetworkName = networkName;
    info.PanId = networkParams.PanId;
    info.Channel = networkParams.Channel;
    info.IsOpen = false;
    SetNetworkInfo(info);
    
    Log(2, "Zigbee network formed: PAN 0x" + 
        std::to_string(networkParams.PanId) + 
        ", Channel " + std::to_string(networkParams.Channel));
    
    return true;
}

bool ZigbeeProtocol::JoinNetwork(const std::string& networkId) {
    // As coordinator, we form networks, not join them
    // This would be used for router/end device mode
    Log(2, "JoinNetwork not supported in coordinator mode");
    return false;
}

bool ZigbeeProtocol::LeaveNetwork() {
    if (!IsInitialized()) {
        return false;
    }
    
    Log(2, "Leaving Zigbee network");
    
    if (!stack->LeaveNetwork()) {
        return false;
    }
    
    hasNetwork = false;
    ClearNetwork();
    
    return true;
}

NetworkTopology ZigbeeProtocol::GetTopology() const {
    NetworkTopology topology;
    topology.Protocol = SmartHomeProtocolType::Zigbee;
    
    std::lock_guard<std::mutex> lock(nodeMutex);
    
    // Add coordinator
    NetworkNode coordNode;
    coordNode.NodeId = "coordinator";
    coordNode.NodeType = "Coordinator";
    coordNode.IsRouter = true;
    coordNode.IsOnline = hasNetwork;
    topology.Nodes.push_back(coordNode);
    
    // Add all nodes
    for (const auto& [deviceId, node] : zigbeeNodes) {
        NetworkNode netNode;
        netNode.NodeId = deviceId;
        netNode.NodeType = (node.Type == ZigbeeDeviceType::Router) ? "Router" : "EndDevice";
        netNode.IsRouter = (node.Type == ZigbeeDeviceType::Router);
        netNode.IsOnline = node.IsOnline;
        netNode.LinkQuality = node.Lqi;
        netNode.ParentId = "coordinator";  // Simplified - would track actual parent
        topology.Nodes.push_back(netNode);
    }
    
    return topology;
}

// ===== DISCOVERY =====

bool ZigbeeProtocol::StartDiscovery(int timeoutSeconds) {
    if (!IsInitialized() || discovering) {
        return false;
    }
    
    Log(2, "Starting Zigbee device discovery...");
    discovering = true;
    ClearDiscoveredDevices();
    
    // Discovery in Zigbee happens via device announcements when permit join is active
    // We can also send IEEE address requests to scan for devices
    
    std::thread([this, timeoutSeconds]() {
        // Scan network addresses
        for (uint16_t nwkAddr = 0x0001; nwkAddr < 0xFFF8 && discovering; nwkAddr++) {
            // Send IEEE address request
            // In practice, this would be done more intelligently using neighbor tables
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(timeoutSeconds));
        discovering = false;
    }).detach();
    
    return true;
}

void ZigbeeProtocol::StopDiscovery() {
    if (!discovering) {
        return;
    }
    
    Log(2, "Stopping Zigbee discovery");
    discovering = false;
}

// ===== PAIRING =====

bool ZigbeeProtocol::StartPairing(int timeoutSeconds) {
    return PermitJoin(timeoutSeconds);
}

void ZigbeeProtocol::StopPairing() {
    PermitJoin(0);
}

bool ZigbeeProtocol::PermitJoin(int timeoutSeconds) {
    if (!IsInitialized() || !hasNetwork) {
        return false;
    }
    
    uint8_t duration = (timeoutSeconds > 254) ? 254 : static_cast<uint8_t>(timeoutSeconds);
    
    if (duration > 0) {
        Log(2, "Enabling permit join for " + std::to_string(duration) + " seconds");
        permitJoinActive = true;
        permitJoinTimeout = duration;
        pairing = true;
    } else {
        Log(2, "Disabling permit join");
        permitJoinActive = false;
        permitJoinTimeout = 0;
        pairing = false;
    }
    
    if (!stack->PermitJoining(duration)) {
        return false;
    }
    
    // Update network info
    SmartHomeNetworkInfo info = GetNetworkInfo();
    info.IsOpen = (duration > 0);
    SetNetworkInfo(info);
    
    // Start timeout countdown
    if (duration > 0) {
        std::thread([this, duration]() {
            std::this_thread::sleep_for(std::chrono::seconds(duration));
            permitJoinActive = false;
            pairing = false;
            
            SmartHomeNetworkInfo info = GetNetworkInfo();
            info.IsOpen = false;
            SetNetworkInfo(info);
        }).detach();
    }
    
    return true;
}

// ===== DEVICE OPERATIONS =====

bool ZigbeeProtocol::RemoveDevice(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(nodeMutex);
    
    auto it = zigbeeNodes.find(deviceId);
    if (it == zigbeeNodes.end()) {
        return false;
    }
    
    uint64_t ieeeAddress = it->second.IeeeAddress;
    uint16_t nwkAddress = it->second.NwkAddress;
    
    // Send leave request
    stack->RemoveNode(ieeeAddress);
    
    // Remove from tracking
    ieeeToDeviceId.erase(ieeeAddress);
    nwkToDeviceId.erase(nwkAddress);
    zigbeeNodes.erase(it);
    
    RemovePairedDevice(deviceId);
    
    Log(2, "Removed Zigbee device: " + deviceId);
    return true;
}

bool ZigbeeProtocol::InterviewDevice(const std::string& deviceId) {
    ZigbeeNode* node = nullptr;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it == zigbeeNodes.end()) {
            return false;
        }
        node = &it->second;
    }
    
    Log(2, "Interviewing device: " + deviceId);
    InterviewNode(*node);
    
    return true;
}

void ZigbeeProtocol::InterviewNode(ZigbeeNode& node) {
    // Step 1: Get node descriptor
    stack->RequestNodeDescriptor(node.NwkAddress, 
        [this, &node](bool success, ZigbeeDeviceType type) {
            if (success) {
                node.Type = type;
                Log(3, "Node type: " + std::to_string(static_cast<int>(type)));
            }
        });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Step 2: Get active endpoints
    DiscoverEndpoints(node);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Step 3: Get simple descriptor for each endpoint
    for (auto& endpoint : node.Endpoints) {
        DiscoverClusters(node, endpoint.EndpointId);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Step 4: Read basic cluster attributes
    if (!node.Endpoints.empty()) {
        uint8_t ep = node.Endpoints[0].EndpointId;
        
        // Read manufacturer name
        ZigbeeAttributeValue mfgName;
        if (ReadAttribute(node.DeviceId, ep, ZigbeeClusters::Basic, 
                          BasicAttributes::ManufacturerName, mfgName)) {
            node.ManufacturerName = std::string(mfgName.Value.begin(), mfgName.Value.end());
        }
        
        // Read model identifier
        ZigbeeAttributeValue modelId;
        if (ReadAttribute(node.DeviceId, ep, ZigbeeClusters::Basic,
                          BasicAttributes::ModelIdentifier, modelId)) {
            node.ModelIdentifier = std::string(modelId.Value.begin(), modelId.Value.end());
        }
        
        // Read power source
        ZigbeeAttributeValue powerSrc;
        if (ReadAttribute(node.DeviceId, ep, ZigbeeClusters::Basic,
                          BasicAttributes::PowerSource, powerSrc)) {
            if (!powerSrc.Value.empty()) {
                node.PowerSource = powerSrc.Value[0];
            }
        }
    }
    
    node.IsOnline = true;
    node.LastSeen = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    
    // Update paired device info
    SmartHomeDeviceInfo info = NodeToDeviceInfo(node);
    UpdatePairedDevice(info);
    
    Log(2, "Interview complete: " + node.ManufacturerName + " " + node.ModelIdentifier);
}

void ZigbeeProtocol::DiscoverEndpoints(ZigbeeNode& node) {
    stack->RequestActiveEndpoints(node.NwkAddress,
        [this, &node](bool success, const std::vector<uint8_t>& endpoints) {
            if (success) {
                node.Endpoints.clear();
                for (uint8_t ep : endpoints) {
                    ZigbeeEndpoint endpoint;
                    endpoint.EndpointId = ep;
                    node.Endpoints.push_back(endpoint);
                }
                Log(3, "Found " + std::to_string(endpoints.size()) + " endpoints");
            }
        });
}

void ZigbeeProtocol::DiscoverClusters(ZigbeeNode& node, uint8_t endpointId) {
    stack->RequestSimpleDescriptor(node.NwkAddress, endpointId,
        [this, &node, endpointId](bool success, const ZigbeeEndpoint& descriptor) {
            if (success) {
                for (auto& ep : node.Endpoints) {
                    if (ep.EndpointId == endpointId) {
                        ep = descriptor;
                        break;
                    }
                }
                Log(3, "Endpoint " + std::to_string(endpointId) + 
                    ": " + std::to_string(descriptor.InputClusters.size()) + " clusters");
            }
        });
}

// ===== COMMANDS =====

bool ZigbeeProtocol::SendCommand(const std::string& deviceId,
                                  const std::string& command,
                                  const std::map<std::string, std::string>& params) {
    ZigbeeNode node;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it == zigbeeNodes.end()) {
            ReportError(-500, "Device not found: " + deviceId);
            return false;
        }
        node = it->second;
    }
    
    // Default to endpoint 1
    uint8_t endpoint = 1;
    if (params.count("endpoint")) {
        endpoint = static_cast<uint8_t>(std::stoi(params.at("endpoint")));
    }
    
    // Parse command
    if (command == "on" || command == "turnOn") {
        return SendOnOff(deviceId, endpoint, ZCL::OnOffCmdOn);
    }
    else if (command == "off" || command == "turnOff") {
        return SendOnOff(deviceId, endpoint, ZCL::OnOffCmdOff);
    }
    else if (command == "toggle") {
        return SendOnOff(deviceId, endpoint, ZCL::OnOffCmdToggle);
    }
    else if (command == "setLevel" || command == "setBrightness") {
        uint8_t level = 254;
        uint16_t transitionTime = 10;  // 1 second
        
        if (params.count("level")) {
            level = static_cast<uint8_t>(std::stoi(params.at("level")));
        }
        if (params.count("brightness")) {
            // Convert 0-100 to 0-254
            int brightness = std::stoi(params.at("brightness"));
            level = static_cast<uint8_t>((brightness * 254) / 100);
        }
        if (params.count("transition")) {
            transitionTime = static_cast<uint16_t>(std::stoi(params.at("transition")));
        }
        
        return SendLevelControl(deviceId, endpoint, level, transitionTime);
    }
    else if (command == "setColor") {
        uint16_t hue = 0;
        uint8_t saturation = 254;
        uint16_t transitionTime = 10;
        
        if (params.count("hue")) {
            // Convert 0-360 to 0-254
            int h = std::stoi(params.at("hue"));
            hue = static_cast<uint16_t>((h * 254) / 360);
        }
        if (params.count("saturation")) {
            saturation = static_cast<uint8_t>(std::stoi(params.at("saturation")));
        }
        
        return SendColorControl(deviceId, endpoint, hue, saturation, transitionTime);
    }
    else if (command == "lock") {
        return SendZCLCommand(deviceId, endpoint, ZigbeeClusters::DoorLock,
                              ZCL::DoorLockCmdLock, {}, true);
    }
    else if (command == "unlock") {
        return SendZCLCommand(deviceId, endpoint, ZigbeeClusters::DoorLock,
                              ZCL::DoorLockCmdUnlock, {}, true);
    }
    else if (command == "open") {
        return SendZCLCommand(deviceId, endpoint, ZigbeeClusters::WindowCovering,
                              ZCL::WindowCmdUpOpen, {}, true);
    }
    else if (command == "close") {
        return SendZCLCommand(deviceId, endpoint, ZigbeeClusters::WindowCovering,
                              ZCL::WindowCmdDownClose, {}, true);
    }
    else if (command == "stop") {
        return SendZCLCommand(deviceId, endpoint, ZigbeeClusters::WindowCovering,
                              ZCL::WindowCmdStop, {}, true);
    }
    else if (command == "setPosition") {
        uint8_t position = 50;
        if (params.count("position")) {
            position = static_cast<uint8_t>(std::stoi(params.at("position")));
        }
        return SendZCLCommand(deviceId, endpoint, ZigbeeClusters::WindowCovering,
                              ZCL::WindowCmdGoToLiftPercentage, {position}, true);
    }
    
    Log(3, "Unknown command: " + command);
    return false;
}

bool ZigbeeProtocol::SendGroupCommand(uint16_t groupId,
                                       const std::string& command,
                                       const std::map<std::string, std::string>& params) {
    if (groups.find(groupId) == groups.end()) {
        ReportError(-501, "Group not found");
        return false;
    }
    
    uint8_t endpoint = 1;  // Default endpoint for groups
    
    if (command == "on" || command == "turnOn") {
        uint8_t frameControl = ZCL::FrameTypeClusterSpecific | ZCL::DisableDefaultResponse;
        return stack->SendZCLGroupFrame(groupId, endpoint, ZigbeeClusters::OnOff,
                                        frameControl, ZCL::OnOffCmdOn, {});
    }
    else if (command == "off" || command == "turnOff") {
        uint8_t frameControl = ZCL::FrameTypeClusterSpecific | ZCL::DisableDefaultResponse;
        return stack->SendZCLGroupFrame(groupId, endpoint, ZigbeeClusters::OnOff,
                                        frameControl, ZCL::OnOffCmdOff, {});
    }
    
    return false;
}

// ===== ZCL HELPERS =====

bool ZigbeeProtocol::SendOnOff(const std::string& deviceId, uint8_t endpoint, uint8_t command) {
    return SendZCLCommand(deviceId, endpoint, ZigbeeClusters::OnOff, command, {}, true);
}

bool ZigbeeProtocol::SendLevelControl(const std::string& deviceId, uint8_t endpoint,
                                       uint8_t level, uint16_t transitionTime) {
    std::vector<uint8_t> payload;
    payload.push_back(level);
    payload.push_back(transitionTime & 0xFF);
    payload.push_back((transitionTime >> 8) & 0xFF);
    
    return SendZCLCommand(deviceId, endpoint, ZigbeeClusters::LevelControl,
                          ZCL::LevelCmdMoveToLevelWithOnOff, payload, true);
}

bool ZigbeeProtocol::SendColorControl(const std::string& deviceId, uint8_t endpoint,
                                       uint16_t hue, uint8_t saturation, uint16_t transitionTime) {
    std::vector<uint8_t> payload;
    payload.push_back(hue & 0xFF);  // Enhanced hue low byte
    payload.push_back((hue >> 8) & 0xFF);  // Enhanced hue high byte
    payload.push_back(saturation);
    payload.push_back(transitionTime & 0xFF);
    payload.push_back((transitionTime >> 8) & 0xFF);
    
    return SendZCLCommand(deviceId, endpoint, ZigbeeClusters::ColorControl,
                          ZCL::ColorCmdEnhancedMoveToHueAndSaturation, payload, true);
}

bool ZigbeeProtocol::SendZCLCommand(const std::string& deviceId, uint8_t endpoint,
                                     uint16_t clusterId, uint8_t commandId,
                                     const std::vector<uint8_t>& payload,
                                     bool clusterSpecific) {
    uint16_t nwkAddress;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it == zigbeeNodes.end()) {
            return false;
        }
        nwkAddress = it->second.NwkAddress;
    }
    
    uint8_t frameControl = clusterSpecific ? ZCL::FrameTypeClusterSpecific : ZCL::FrameTypeGlobal;
    frameControl |= ZCL::DisableDefaultResponse;
    
    return stack->SendZCLFrame(nwkAddress, endpoint, clusterId, frameControl, commandId, payload);
}

// ===== GROUPS =====

bool ZigbeeProtocol::CreateGroup(uint16_t groupId, const std::string& name) {
    groups[groupId] = name;
    groupMembers[groupId] = {};
    return true;
}

bool ZigbeeProtocol::DeleteGroup(uint16_t groupId) {
    groups.erase(groupId);
    groupMembers.erase(groupId);
    return true;
}

bool ZigbeeProtocol::AddToGroup(const std::string& deviceId, uint16_t groupId) {
    if (groups.find(groupId) == groups.end()) {
        return false;
    }
    
    uint16_t nwkAddress;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it == zigbeeNodes.end()) {
            return false;
        }
        nwkAddress = it->second.NwkAddress;
    }
    
    // Send Add Group command
    std::vector<uint8_t> payload;
    payload.push_back(groupId & 0xFF);
    payload.push_back((groupId >> 8) & 0xFF);
    payload.push_back(0);  // Empty group name
    
    bool success = SendZCLCommand(deviceId, 1, ZigbeeClusters::Groups,
                                   ZCL::GroupsCmdAddGroup, payload, true);
    
    if (success) {
        auto& members = groupMembers[groupId];
        if (std::find(members.begin(), members.end(), deviceId) == members.end()) {
            members.push_back(deviceId);
        }
    }
    
    return success;
}

bool ZigbeeProtocol::RemoveFromGroup(const std::string& deviceId, uint16_t groupId) {
    auto it = groupMembers.find(groupId);
    if (it == groupMembers.end()) {
        return false;
    }
    
    // Send Remove Group command
    std::vector<uint8_t> payload;
    payload.push_back(groupId & 0xFF);
    payload.push_back((groupId >> 8) & 0xFF);
    
    bool success = SendZCLCommand(deviceId, 1, ZigbeeClusters::Groups,
                                   ZCL::GroupsCmdRemoveGroup, payload, true);
    
    if (success) {
        auto& members = it->second;
        members.erase(std::remove(members.begin(), members.end(), deviceId), members.end());
    }
    
    return success;
}

std::vector<uint16_t> ZigbeeProtocol::GetGroups() const {
    std::vector<uint16_t> result;
    for (const auto& [id, name] : groups) {
        result.push_back(id);
    }
    return result;
}

std::vector<std::string> ZigbeeProtocol::GetGroupMembers(uint16_t groupId) const {
    auto it = groupMembers.find(groupId);
    if (it != groupMembers.end()) {
        return it->second;
    }
    return {};
}

// ===== BINDING =====

bool ZigbeeProtocol::BindDevices(const std::string& sourceId, const std::string& targetId) {
    uint64_t srcIeee, dstIeee;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        
        auto srcIt = zigbeeNodes.find(sourceId);
        auto dstIt = zigbeeNodes.find(targetId);
        
        if (srcIt == zigbeeNodes.end() || dstIt == zigbeeNodes.end()) {
            return false;
        }
        
        srcIeee = srcIt->second.IeeeAddress;
        dstIeee = dstIt->second.IeeeAddress;
    }
    
    // Default binding on On/Off cluster
    return stack->BindRequest(srcIeee, 1, ZigbeeClusters::OnOff, dstIeee, 1);
}

bool ZigbeeProtocol::UnbindDevices(const std::string& sourceId, const std::string& targetId) {
    uint64_t srcIeee, dstIeee;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        
        auto srcIt = zigbeeNodes.find(sourceId);
        auto dstIt = zigbeeNodes.find(targetId);
        
        if (srcIt == zigbeeNodes.end() || dstIt == zigbeeNodes.end()) {
            return false;
        }
        
        srcIeee = srcIt->second.IeeeAddress;
        dstIeee = dstIt->second.IeeeAddress;
    }
    
    return stack->UnbindRequest(srcIeee, 1, ZigbeeClusters::OnOff, dstIeee, 1);
}

std::vector<std::string> ZigbeeProtocol::GetBindings(const std::string& deviceId) const {
    auto it = deviceBindings.find(deviceId);
    if (it != deviceBindings.end()) {
        std::vector<std::string> result;
        for (const auto& binding : it->second) {
            auto dstIt = ieeeToDeviceId.find(binding.DestAddress);
            if (dstIt != ieeeToDeviceId.end()) {
                result.push_back(dstIt->second);
            }
        }
        return result;
    }
    return {};
}

bool ZigbeeProtocol::CreateBinding(const ZigbeeBinding& binding) {
    return stack->BindRequest(binding.SourceAddress, binding.SourceEndpoint,
                              binding.ClusterId, binding.DestAddress, binding.DestEndpoint);
}

bool ZigbeeProtocol::RemoveBinding(const ZigbeeBinding& binding) {
    return stack->UnbindRequest(binding.SourceAddress, binding.SourceEndpoint,
                                binding.ClusterId, binding.DestAddress, binding.DestEndpoint);
}

std::vector<ZigbeeBinding> ZigbeeProtocol::GetBindingTable(const std::string& deviceId) const {
    auto it = deviceBindings.find(deviceId);
    if (it != deviceBindings.end()) {
        return it->second;
    }
    return {};
}

// ===== OTA =====

bool ZigbeeProtocol::StartOTAUpdate(const std::string& deviceId, const std::string& firmwarePath) {
    Log(2, "OTA update requested for " + deviceId);
    otaProgress[deviceId] = 0;
    
    // TODO: Implement OTA using ZigbeeClusters::OTAUpgrade
    // This involves:
    // 1. Parse OTA file header
    // 2. Send Image Notify to device
    // 3. Handle Query Next Image Request
    // 4. Send Image Block Response(s)
    // 5. Handle Upgrade End Request
    
    return false;  // Not implemented
}

int ZigbeeProtocol::GetOTAProgress(const std::string& deviceId) const {
    auto it = otaProgress.find(deviceId);
    if (it != otaProgress.end()) {
        return it->second;
    }
    return -1;
}

bool ZigbeeProtocol::CancelOTAUpdate(const std::string& deviceId) {
    otaProgress.erase(deviceId);
    return true;
}

// ===== SECURITY =====

int ZigbeeProtocol::GetSecurityLevel() const {
    return 5;  // Zigbee uses AES-128-CCM
}

bool ZigbeeProtocol::SetNetworkKey(const std::vector<uint8_t>& key) {
    if (key.size() != 16) {
        return false;
    }
    
    std::copy(key.begin(), key.end(), networkParams.NetworkKey.begin());
    return true;
}

// ===== CONFIGURATION =====

bool ZigbeeProtocol::LoadConfig(const std::string& path) {
    storagePath = path;
    // TODO: Load persistent Zigbee configuration
    return true;
}

bool ZigbeeProtocol::SaveConfig(const std::string& path) {
    // TODO: Save Zigbee configuration
    return true;
}

std::vector<SmartHomeDeviceCategory> ZigbeeProtocol::GetSupportedDeviceCategories() const {
    return {
        SmartHomeDeviceCategory::Light,
        SmartHomeDeviceCategory::Switch,
        SmartHomeDeviceCategory::Plug,
        SmartHomeDeviceCategory::Sensor,
        SmartHomeDeviceCategory::Lock,
        SmartHomeDeviceCategory::Thermostat,
        SmartHomeDeviceCategory::Blind,
        SmartHomeDeviceCategory::Fan
    };
}

// ===== ZIGBEE-SPECIFIC =====

uint16_t ZigbeeProtocol::GetPanId() const {
    return networkParams.PanId;
}

uint64_t ZigbeeProtocol::GetExtendedPanId() const {
    return networkParams.ExtendedPanId;
}

uint8_t ZigbeeProtocol::GetChannel() const {
    return networkParams.Channel;
}

bool ZigbeeProtocol::TouchLink(int timeoutSeconds) {
    Log(2, "Starting TouchLink commissioning...");
    
    // TouchLink (ZLL) commissioning
    // Requires sending Inter-PAN frames to scan for TouchLink targets
    
    // TODO: Implement TouchLink
    return false;
}

bool ZigbeeProtocol::FactoryReset(const std::string& deviceId) {
    Log(2, "Factory resetting device: " + deviceId);
    
    // Send ZCL Basic cluster reset command
    return SendZCLCommand(deviceId, 1, ZigbeeClusters::Basic, 0x00, {}, true);
}

bool ZigbeeProtocol::ReadAttribute(const std::string& deviceId, uint8_t endpoint,
                                    uint16_t clusterId, uint16_t attributeId,
                                    ZigbeeAttributeValue& outValue) {
    uint16_t nwkAddress;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it == zigbeeNodes.end()) {
            return false;
        }
        nwkAddress = it->second.NwkAddress;
    }
    
    std::vector<uint8_t> payload;
    payload.push_back(attributeId & 0xFF);
    payload.push_back((attributeId >> 8) & 0xFF);
    
    bool success = false;
    std::condition_variable cv;
    std::mutex cvMutex;
    
    stack->SendZCLFrame(nwkAddress, endpoint, clusterId,
                        ZCL::FrameTypeGlobal, ZCL::CmdReadAttributes, payload,
                        [&](bool ok, const std::vector<uint8_t>& data) {
                            if (ok && data.size() >= 4) {
                                outValue.ClusterId = clusterId;
                                outValue.AttributeId = (data[1] << 8) | data[0];
                                outValue.Status = data[2];
                                if (outValue.Status == 0 && data.size() > 4) {
                                    outValue.DataType = data[3];
                                    outValue.Value.assign(data.begin() + 4, data.end());
                                }
                                success = true;
                            }
                            std::lock_guard<std::mutex> lock(cvMutex);
                            cv.notify_one();
                        });
    
    // Wait for response (with timeout)
    std::unique_lock<std::mutex> lock(cvMutex);
    cv.wait_for(lock, std::chrono::seconds(5));
    
    return success;
}

bool ZigbeeProtocol::WriteAttribute(const std::string& deviceId, uint8_t endpoint,
                                     uint16_t clusterId, uint16_t attributeId,
                                     const std::vector<uint8_t>& value) {
    uint16_t nwkAddress;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it == zigbeeNodes.end()) {
            return false;
        }
        nwkAddress = it->second.NwkAddress;
    }
    
    std::vector<uint8_t> payload;
    payload.push_back(attributeId & 0xFF);
    payload.push_back((attributeId >> 8) & 0xFF);
    // Note: Would need to include data type
    payload.insert(payload.end(), value.begin(), value.end());
    
    return stack->SendZCLFrame(nwkAddress, endpoint, clusterId,
                               ZCL::FrameTypeGlobal, ZCL::CmdWriteAttributes, payload);
}

bool ZigbeeProtocol::ConfigureReporting(const std::string& deviceId, uint8_t endpoint,
                                         uint16_t clusterId, uint16_t attributeId,
                                         uint16_t minInterval, uint16_t maxInterval,
                                         uint16_t reportableChange) {
    uint16_t nwkAddress;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it == zigbeeNodes.end()) {
            return false;
        }
        nwkAddress = it->second.NwkAddress;
    }
    
    std::vector<uint8_t> payload;
    payload.push_back(0x00);  // Direction: configure reporting for attribute
    payload.push_back(attributeId & 0xFF);
    payload.push_back((attributeId >> 8) & 0xFF);
    payload.push_back(ZCL::TypeUint16);  // Data type (would be attribute-specific)
    payload.push_back(minInterval & 0xFF);
    payload.push_back((minInterval >> 8) & 0xFF);
    payload.push_back(maxInterval & 0xFF);
    payload.push_back((maxInterval >> 8) & 0xFF);
    payload.push_back(reportableChange & 0xFF);
    payload.push_back((reportableChange >> 8) & 0xFF);
    
    return stack->SendZCLFrame(nwkAddress, endpoint, clusterId,
                               ZCL::FrameTypeGlobal, ZCL::CmdConfigureReporting, payload);
}

// ===== NODE MANAGEMENT =====

ZigbeeNetworkParams ZigbeeProtocol::GetNetworkParams() const {
    return networkParams;
}

bool ZigbeeProtocol::SetNetworkParams(const ZigbeeNetworkParams& params) {
    if (hasNetwork) {
        return false;  // Can't change while network is active
    }
    networkParams = params;
    return true;
}

bool ZigbeeProtocol::ChangeChannel(uint8_t newChannel) {
    if (newChannel < 11 || newChannel > 26) {
        return false;
    }
    
    // TODO: Implement network manager channel change
    networkParams.Channel = newChannel;
    return true;
}

bool ZigbeeProtocol::UpdateNetworkKey(const std::array<uint8_t, 16>& newKey) {
    // TODO: Implement network key update broadcast
    networkParams.NetworkKey = newKey;
    return true;
}

ZigbeeNode ZigbeeProtocol::GetZigbeeNode(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock(nodeMutex);
    auto it = zigbeeNodes.find(deviceId);
    if (it != zigbeeNodes.end()) {
        return it->second;
    }
    return ZigbeeNode{};
}

std::vector<ZigbeeNode> ZigbeeProtocol::GetAllZigbeeNodes() const {
    std::vector<ZigbeeNode> result;
    std::lock_guard<std::mutex> lock(nodeMutex);
    for (const auto& [id, node] : zigbeeNodes) {
        result.push_back(node);
    }
    return result;
}

uint16_t ZigbeeProtocol::GetNodeNetworkAddress(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock(nodeMutex);
    auto it = zigbeeNodes.find(deviceId);
    if (it != zigbeeNodes.end()) {
        return it->second.NwkAddress;
    }
    return 0xFFFF;
}

uint64_t ZigbeeProtocol::GetNodeIeeeAddress(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock(nodeMutex);
    auto it = zigbeeNodes.find(deviceId);
    if (it != zigbeeNodes.end()) {
        return it->second.IeeeAddress;
    }
    return 0;
}

std::vector<ZigbeeEndpoint> ZigbeeProtocol::GetEndpoints(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock(nodeMutex);
    auto it = zigbeeNodes.find(deviceId);
    if (it != zigbeeNodes.end()) {
        return it->second.Endpoints;
    }
    return {};
}

std::vector<ZigbeeCluster> ZigbeeProtocol::GetClusters(const std::string& deviceId, 
                                                        uint8_t endpoint) const {
    std::lock_guard<std::mutex> lock(nodeMutex);
    auto it = zigbeeNodes.find(deviceId);
    if (it != zigbeeNodes.end()) {
        for (const auto& ep : it->second.Endpoints) {
            if (ep.EndpointId == endpoint) {
                return ep.InputClusters;
            }
        }
    }
    return {};
}

// ===== SCENES =====

bool ZigbeeProtocol::AddScene(uint16_t groupId, uint8_t sceneId, const std::string& name,
                               const std::vector<ZigbeeAttributeValue>& extensionFields) {
    uint32_t key = (static_cast<uint32_t>(groupId) << 8) | sceneId;
    
    SceneEntry entry;
    entry.GroupId = groupId;
    entry.SceneId = sceneId;
    entry.Name = name;
    entry.ExtensionFields = extensionFields;
    scenes[key] = entry;
    
    // TODO: Send Add Scene command to devices in group
    return true;
}

bool ZigbeeProtocol::RemoveScene(uint16_t groupId, uint8_t sceneId) {
    uint32_t key = (static_cast<uint32_t>(groupId) << 8) | sceneId;
    scenes.erase(key);
    
    // TODO: Send Remove Scene command
    return true;
}

bool ZigbeeProtocol::RecallScene(uint16_t groupId, uint8_t sceneId) {
    std::vector<uint8_t> payload;
    payload.push_back(groupId & 0xFF);
    payload.push_back((groupId >> 8) & 0xFF);
    payload.push_back(sceneId);
    
    uint8_t frameControl = ZCL::FrameTypeClusterSpecific | ZCL::DisableDefaultResponse;
    return stack->SendZCLGroupFrame(groupId, 1, ZigbeeClusters::Scenes,
                                    frameControl, ZCL::ScenesCmdRecallScene, payload);
}

bool ZigbeeProtocol::StoreScene(const std::string& deviceId, uint16_t groupId, uint8_t sceneId) {
    std::vector<uint8_t> payload;
    payload.push_back(groupId & 0xFF);
    payload.push_back((groupId >> 8) & 0xFF);
    payload.push_back(sceneId);
    
    return SendZCLCommand(deviceId, 1, ZigbeeClusters::Scenes,
                          ZCL::ScenesCmdStoreScene, payload, true);
}

// ===== COORDINATOR =====

bool ZigbeeProtocol::IsCoordinator() const {
    return true;  // This implementation is always coordinator
}

bool ZigbeeProtocol::SetInstallCode(const std::string& ieeeAddress, 
                                     const std::vector<uint8_t>& installCode) {
    // Install codes are used for secure joining
    // TODO: Implement install code handling
    return false;
}

// ===== GREEN POWER =====

bool ZigbeeProtocol::EnableGreenPowerProxy(bool enable) {
    greenPowerEnabled = enable;
    Log(2, enable ? "Green Power proxy enabled" : "Green Power proxy disabled");
    return true;
}

bool ZigbeeProtocol::AddGreenPowerDevice(uint32_t srcId, const std::vector<uint8_t>& key) {
    if (!greenPowerEnabled) {
        return false;
    }
    
    greenPowerDevices[srcId] = key;
    Log(2, "Added Green Power device: " + std::to_string(srcId));
    return true;
}

bool ZigbeeProtocol::RemoveGreenPowerDevice(uint32_t srcId) {
    greenPowerDevices.erase(srcId);
    return true;
}

// ===== DIAGNOSTICS =====

uint8_t ZigbeeProtocol::GetLqi(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock(nodeMutex);
    auto it = zigbeeNodes.find(deviceId);
    if (it != zigbeeNodes.end()) {
        return it->second.Lqi;
    }
    return 0;
}

int8_t ZigbeeProtocol::GetRssi(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock(nodeMutex);
    auto it = zigbeeNodes.find(deviceId);
    if (it != zigbeeNodes.end()) {
        return it->second.Rssi;
    }
    return -128;
}

std::vector<std::pair<std::string, std::string>> ZigbeeProtocol::GetNeighborTable() const {
    std::vector<std::pair<std::string, std::string>> result;
    // TODO: Read neighbor table from coordinator
    return result;
}

std::vector<std::pair<std::string, std::string>> ZigbeeProtocol::GetRoutingTable() const {
    std::vector<std::pair<std::string, std::string>> result;
    // TODO: Read routing table from coordinator
    return result;
}

// ===== EVENT HANDLERS =====

void ZigbeeProtocol::OnDeviceJoined(uint64_t ieeeAddress, uint16_t nwkAddress) {
    Log(2, "Device joined: " + IeeeAddressToString(ieeeAddress) + 
        " @ 0x" + std::to_string(nwkAddress));
    
    // Create new node entry
    ZigbeeNode node;
    node.IeeeAddress = ieeeAddress;
    node.NwkAddress = nwkAddress;
    node.DeviceId = "zigbee_" + IeeeAddressToString(ieeeAddress);
    node.IsOnline = true;
    node.LastSeen = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        zigbeeNodes[node.DeviceId] = node;
        ieeeToDeviceId[ieeeAddress] = node.DeviceId;
        nwkToDeviceId[nwkAddress] = node.DeviceId;
    }
    
    // Start interview
    std::thread([this, deviceId = node.DeviceId]() {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        InterviewDevice(deviceId);
    }).detach();
    
    // Add to paired devices
    SmartHomeDeviceInfo info = NodeToDeviceInfo(node);
    AddPairedDevice(info);
}

void ZigbeeProtocol::OnDeviceLeft(uint64_t ieeeAddress) {
    Log(2, "Device left: " + IeeeAddressToString(ieeeAddress));
    
    std::string deviceId;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = ieeeToDeviceId.find(ieeeAddress);
        if (it != ieeeToDeviceId.end()) {
            deviceId = it->second;
        }
    }
    
    if (!deviceId.empty()) {
        RemoveDevice(deviceId);
    }
}

void ZigbeeProtocol::OnDeviceAnnounce(uint64_t ieeeAddress, uint16_t nwkAddress) {
    Log(3, "Device announce: " + IeeeAddressToString(ieeeAddress));
    
    // Check if this is a new device or network address change
    std::lock_guard<std::mutex> lock(nodeMutex);
    auto it = ieeeToDeviceId.find(ieeeAddress);
    
    if (it != ieeeToDeviceId.end()) {
        // Existing device - may have changed network address
        auto& node = zigbeeNodes[it->second];
        if (node.NwkAddress != nwkAddress) {
            nwkToDeviceId.erase(node.NwkAddress);
            node.NwkAddress = nwkAddress;
            nwkToDeviceId[nwkAddress] = it->second;
        }
        node.IsOnline = true;
        node.LastSeen = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }
}

void ZigbeeProtocol::OnAttributeReport(uint64_t srcAddress, uint8_t endpoint,
                                        uint16_t clusterId, const ZigbeeAttributeValue& value) {
    std::string deviceId;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = ieeeToDeviceId.find(srcAddress);
        if (it != ieeeToDeviceId.end()) {
            deviceId = it->second;
        }
    }
    
    if (deviceId.empty()) {
        return;
    }
    
    // Process attribute report based on cluster
    // This would update device state and trigger callbacks
    Log(3, "Attribute report from " + deviceId + 
        ": cluster=" + std::to_string(clusterId) +
        ", attr=" + std::to_string(value.AttributeId));
}

void ZigbeeProtocol::OnZCLResponse(uint64_t srcAddress, uint8_t endpoint,
                                    uint16_t clusterId, uint8_t commandId,
                                    const std::vector<uint8_t>& payload) {
    // Handle ZCL responses
}

// ===== PROCESSING THREADS =====

void ZigbeeProtocol::ProcessZigbee() {
    Log(3, "Process thread started");
    
    while (running) {
        stack->Process();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    Log(3, "Process thread stopped");
}

void ZigbeeProtocol::NetworkThread() {
    Log(3, "Network thread started");
    
    while (running) {
        // Periodic tasks
        
        // Check device timeouts
        {
            std::lock_guard<std::mutex> lock(nodeMutex);
            uint32_t now = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            
            for (auto& [deviceId, node] : zigbeeNodes) {
                if (node.IsOnline && (now - node.LastSeen) > 3600) {
                    // Device not seen for 1 hour
                    node.IsOnline = false;
                    // Could trigger offline callback
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    
    Log(3, "Network thread stopped");
}

// ===== HELPERS =====

SmartHomeDeviceInfo ZigbeeProtocol::NodeToDeviceInfo(const ZigbeeNode& node) const {
    SmartHomeDeviceInfo info;
    info.DeviceId = node.DeviceId;
    info.Name = node.ModelIdentifier.empty() ? 
                ("Zigbee " + IeeeAddressToString(node.IeeeAddress).substr(0, 8)) :
                node.ModelIdentifier;
    info.Manufacturer = node.ManufacturerName;
    info.Model = node.ModelIdentifier;
    info.Protocol = SmartHomeProtocolType::Zigbee;
    info.Category = DetermineDeviceCategory(node);
    info.State = node.IsOnline ? SmartHomeDeviceState::Online : SmartHomeDeviceState::Offline;
    return info;
}

SmartHomeDeviceCategory ZigbeeProtocol::DetermineDeviceCategory(const ZigbeeNode& node) const {
    // Check device ID from simple descriptor
    for (const auto& ep : node.Endpoints) {
        switch (ep.DeviceId) {
            case ZigbeeDeviceIds::OnOffLight:
            case ZigbeeDeviceIds::DimmableLight:
            case ZigbeeDeviceIds::ColorDimmableLight:
                return SmartHomeDeviceCategory::Light;
                
            case ZigbeeDeviceIds::OnOffLightSwitch:
            case ZigbeeDeviceIds::DimmerSwitch:
            case ZigbeeDeviceIds::ColorDimmerSwitch:
                return SmartHomeDeviceCategory::Switch;
                
            case ZigbeeDeviceIds::SmartPlug:
            case ZigbeeDeviceIds::MainsPowerOutlet:
                return SmartHomeDeviceCategory::Plug;
                
            case ZigbeeDeviceIds::DoorLock:
                return SmartHomeDeviceCategory::Lock;
                
            case ZigbeeDeviceIds::Thermostat:
                return SmartHomeDeviceCategory::Thermostat;
                
            case ZigbeeDeviceIds::TemperatureSensor:
            case ZigbeeDeviceIds::OccupancySensor:
            case ZigbeeDeviceIds::LightSensor:
            case ZigbeeDeviceIds::IASZone:
                return SmartHomeDeviceCategory::Sensor;
                
            case ZigbeeDeviceIds::WindowCovering:
                return SmartHomeDeviceCategory::Blind;
        }
        
        // Check for specific clusters
        for (const auto& cluster : ep.InputClusters) {
            if (cluster.ClusterId == ZigbeeClusters::IASZone) {
                return SmartHomeDeviceCategory::Sensor;
            }
            if (cluster.ClusterId == ZigbeeClusters::Thermostat) {
                return SmartHomeDeviceCategory::Thermostat;
            }
            if (cluster.ClusterId == ZigbeeClusters::DoorLock) {
                return SmartHomeDeviceCategory::Lock;
            }
        }
    }
    
    return SmartHomeDeviceCategory::Unknown;
}

std::string ZigbeeProtocol::IeeeAddressToString(uint64_t addr) const {
    std::stringstream ss;
    for (int i = 7; i >= 0; i--) {
        ss << std::hex << std::setw(2) << std::setfill('0')
           << ((addr >> (i * 8)) & 0xFF);
        if (i > 0) ss << ":";
    }
    return ss.str();
}

uint64_t ZigbeeProtocol::StringToIeeeAddress(const std::string& str) const {
    uint64_t result = 0;
    std::string clean = str;
    
    clean.erase(std::remove(clean.begin(), clean.end(), ':'), clean.end());
    
    if (clean.length() == 16) {
        for (int i = 0; i < 16; i += 2) {
            result = (result << 8) | std::stoul(clean.substr(i, 2), nullptr, 16);
        }
    }
    
    return result;
}

// ===== FACTORY FUNCTION =====

std::shared_ptr<ISmartHomeProtocol> CreateZigbeeProtocol() {
    return std::make_shared<ZigbeeProtocol>();
}

} // namespace SmartHome
} // namespace UltraCanvas
