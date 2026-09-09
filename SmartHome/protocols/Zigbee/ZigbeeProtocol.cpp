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
#include <cstdio>
#include <memory>
#include <atomic>
#include <optional>

// Conditional Zigbee stack includes
#ifdef ULTRACANVAS_WITH_EZSP
// The ASH framing and EZSP frame layers are implemented in this repository
// rather than taken from a vendor library: what was here before was
// #include "ezsp/ezsp.h" and "ezsp/ash-host.h", Silicon Labs' own host headers,
// against functions that were all "return false; // Not implemented". Those
// includes named a library this project does not ship and never called it.
#include "ezsp/AshTransport.h"
#include "ezsp/EzspFrame.h"
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

    // ZDO transactions in flight, keyed by their transaction sequence number.
    // Kept apart from the ZCL map above: both sequence spaces are eight bits
    // wide and run independently, so one map would let a ZCL read and a ZDO
    // query with the same number claim each other's answer.
    struct PendingZdo {
        uint16_t ResponseCluster;
        std::function<void(bool success, const std::vector<uint8_t>& zdoFrame)> Callback;
        uint64_t Timestamp;
    };
    std::map<uint8_t, PendingZdo> pendingZdo;
    uint8_t zdoSequence = 0;
    
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
    
    bool FormNetwork(uint16_t panId, uint8_t channel, uint64_t extendedPanId,
                     const std::array<uint8_t, 16>& networkKey) {
#ifdef ULTRACANVAS_WITH_EZSP
        return EZSP_FormNetwork(panId, channel, extendedPanId, networkKey);
#elif defined(ULTRACANVAS_WITH_ZSTACK)
        return ZStack_FormNetwork(panId, channel, extendedPanId, networkKey);
#else
        // Stub mode - simulate network formation
        (void)extendedPanId;
        (void)networkKey;
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
        // Nothing to pump: the ASH transport reads the port on a thread of
        // its own and delivers frames to OnEzspFrameReceived as they arrive.
    }
    
    void CheckTimeouts() {
        uint64_t now = GetTimestamp();

        // Collect the expired callbacks under the lock and run them outside
        // it: a callback may well send another request, which takes the lock.
        std::vector<std::function<void()>> expired;
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            for (auto it = pendingRequests.begin(); it != pendingRequests.end();) {
                if (now - it->second.Timestamp > kRequestTimeoutMs) {
                    if (it->second.Callback) {
                        expired.push_back([cb = it->second.Callback]() { cb(false, {}); });
                    }
                    it = pendingRequests.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto it = pendingZdo.begin(); it != pendingZdo.end();) {
                if (now - it->second.Timestamp > kRequestTimeoutMs) {
                    if (it->second.Callback) {
                        expired.push_back([cb = it->second.Callback]() { cb(false, {}); });
                    }
                    it = pendingZdo.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& fn : expired) fn();
    }

    static constexpr uint64_t kRequestTimeoutMs = 10000;
    
    uint64_t GetTimestamp() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    
    void Log(int level, const std::string& message) {
        if (protocol) {
            protocol->Log(level, "[stack] " + message);
        }
    }

#ifdef ULTRACANVAS_WITH_EZSP
    // ===== EZSP over ASH =====
    //
    // The transport does the framing, sequence numbers and retries; what is
    // left here is the EZSP conversation on top of it.

    Ash::Transport ashTransport;
    std::atomic<uint8_t> ezspVersion{0};    // negotiated with the NCP at start-up
    std::atomic<uint8_t> ezspSequence{0};

    uint8_t NextSequence() { return ezspSequence++; }

    // ----- command responses -----
    //
    // Every EZSP command is answered by a frame with the same sequence number
    // and frame id. Configuration and network formation need those answers,
    // so a caller can register for one and wait; the data path (sendUnicast
    // and friends) stays fire-and-forget and is judged by what the device
    // says back, not by the NCP's acceptance of the bytes.

    struct PendingCommand {
        uint16_t FrameId = 0;
        std::vector<uint8_t> Response;
        bool Done = false;
    };
    std::map<uint8_t, PendingCommand> pendingCommands;
    std::mutex commandMutex;
    std::condition_variable commandCv;

    static constexpr uint32_t kCommandTimeoutMs = 3000;
    static constexpr uint32_t kNetworkTimeoutMs = 15000;

    // Must not be called from the receive thread: the answer arrives there.
    std::optional<std::vector<uint8_t>> SendEzspAndWait(uint16_t frameId,
                                                        const std::vector<uint8_t>& params,
                                                        uint32_t timeoutMs = kCommandTimeoutMs) {
        if (ezspVersion == 0) return std::nullopt;
        const uint8_t seq = NextSequence();
        {
            std::lock_guard<std::mutex> lock(commandMutex);
            PendingCommand pending;
            pending.FrameId = frameId;
            pendingCommands[seq] = pending;
        }
        if (!ashTransport.Send(Ezsp::EncodeCommand(ezspVersion, seq, frameId, params))) {
            std::lock_guard<std::mutex> lock(commandMutex);
            pendingCommands.erase(seq);
            return std::nullopt;
        }

        std::unique_lock<std::mutex> lock(commandMutex);
        commandCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
            auto it = pendingCommands.find(seq);
            return it == pendingCommands.end() || it->second.Done;
        });
        std::optional<std::vector<uint8_t>> result;
        auto it = pendingCommands.find(seq);
        if (it != pendingCommands.end()) {
            if (it->second.Done) result = std::move(it->second.Response);
            pendingCommands.erase(it);
        }
        return result;
    }

    // For the commands whose whole answer is a status byte. `success` is 0x00
    // for both EmberStatus and EzspStatus.
    bool SendEzspExpectingStatus(const char* what, uint16_t frameId,
                                 const std::vector<uint8_t>& params) {
        auto rsp = SendEzspAndWait(frameId, params);
        if (!rsp) {
            Log(0, std::string(what) + ": no response from the NCP");
            return false;
        }
        if (rsp->empty() || (*rsp)[0] != Ezsp::kEmberSuccess) {
            Log(0, std::string(what) + ": NCP returned status 0x" +
                   Hex8(rsp->empty() ? 0xFF : (*rsp)[0]));
            return false;
        }
        return true;
    }

    static std::string Hex8(uint8_t v) {
        char buf[4];
        std::snprintf(buf, sizeof buf, "%02X", v);
        return buf;
    }

    // ----- stack status -----
    //
    // The NCP reports the network coming up or going down through
    // stackStatusHandler, asynchronously. Callers that need to know wait on
    // the state rather than the event, so an answer that arrives before they
    // start waiting is not missed.

    std::mutex stackStatusMutex;
    std::condition_variable stackStatusCv;
    bool networkUp = false;
    int networkWaiters = 0;

    bool WaitForNetwork(bool up, uint32_t timeoutMs) {
        std::unique_lock<std::mutex> lock(stackStatusMutex);
        ++networkWaiters;
        const bool ok = stackStatusCv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                               [&] { return networkUp == up; });
        --networkWaiters;
        return ok;
    }

    void HandleStackStatus(const std::vector<uint8_t>& params) {
        if (params.empty()) return;
        const uint8_t status = params[0];
        Log(2, "stack status 0x" + Hex8(status));

        bool unattendedUp = false;
        {
            std::lock_guard<std::mutex> lock(stackStatusMutex);
            if (status == Ezsp::kEmberNetworkUp) {
                networkUp = true;
                unattendedUp = networkWaiters == 0;
            } else if (status == Ezsp::kEmberNetworkDown) {
                networkUp = false;
            }
            stackStatusCv.notify_all();
        }

        if (status == Ezsp::kEmberNetworkDown) {
            if (protocol) protocol->OnNetworkDown();
        } else if (status == Ezsp::kEmberNetworkUp && unattendedUp) {
            // Nobody asked for this (an NCP rejoining on its own, say). The
            // parameters have to be fetched, and that cannot happen on this
            // thread because the answer would arrive on it.
            std::thread([this] { ReportNetworkUp(); }).detach();
        }
    }

    // Reads back what the NCP is running and tells the protocol. Called by
    // whoever waited for NETWORK_UP, never from the receive thread.
    bool ReportNetworkUp() {
        auto rsp = SendEzspAndWait(Ezsp::kGetNetworkParams, {});
        auto np = rsp ? Ezsp::DecodeNetworkParametersResponse(*rsp) : std::nullopt;
        if (!np || np->Status != Ezsp::kEmberSuccess) {
            Log(1, "network is up but getNetworkParameters failed");
            return false;
        }
        if (auto eui = SendEzspAndWait(Ezsp::kGetEui64, {}); eui && eui->size() >= 8) {
            coordinatorIeee = Ezsp::ReadU64(*eui, 0);
        }
        if (auto nid = SendEzspAndWait(Ezsp::kGetNodeId, {}); nid && nid->size() >= 2) {
            coordinatorNwk = Ezsp::ReadU16(*nid, 0);
        }

        ZigbeeNetworkParams params;
        params.ExtendedPanId = np->Parameters.ExtendedPanId;
        params.PanId = np->Parameters.PanId;
        params.Channel = np->Parameters.RadioChannel;
        params.ChannelMask = np->Parameters.Channels;
        params.NetworkUpdateId = np->Parameters.NwkUpdateId;
        params.TrustCenterAddress = coordinatorNwk;
        params.SecurityLevel = 5;
        if (protocol) protocol->OnNetworkUp(params, coordinatorIeee);
        return true;
    }

    // ----- start-up -----

    bool InitializeEZSP() {
        Ash::TransportConfig config;
        config.SerialPort = serialPort.empty() ? std::string("/dev/ttyUSB0") : serialPort;

        ashTransport.SetOnEzspFrame([this](const std::vector<uint8_t>& raw) {
            OnEzspFrameReceived(raw);
        });
        ashTransport.SetOnLinkDown([this](const std::string& reason) {
            Log(0, "EZSP link down: " + reason);
        });

        if (!ashTransport.Open(config)) {
            Log(0, "ASH transport did not come up: " + ashTransport.LastError());
            return false;
        }

        // The version command is the one exchange whose encoding is fixed, and
        // it has to happen before anything else: its answer decides how every
        // later frame is framed.
        const std::vector<uint8_t> command =
            Ezsp::EncodeVersionCommand(NextSequence(), 8);
        if (!ashTransport.Send(command)) {
            Log(0, "the NCP did not answer the version command");
            ashTransport.Close();
            return false;
        }

        // OnEzspFrameReceived fills ezspVersion in when the answer arrives.
        for (int waited = 0; waited < 50 && ezspVersion == 0; ++waited) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (ezspVersion == 0) {
            Log(0, "no version response from the NCP");
            ashTransport.Close();
            return false;
        }
        Log(2, "EZSP v" + std::to_string(static_cast<int>(ezspVersion)) + " negotiated");

        if (!ConfigureNcp()) {
            ashTransport.Close();
            ezspVersion = 0;
            return false;
        }

        // networkInit brings up whatever network the NCP kept in its tokens
        // from last time. NOT_JOINED is the normal answer on a fresh dongle.
        auto rsp = SendEzspAndWait(Ezsp::kNetworkInit, Ezsp::EncodeNetworkInitParams(ezspVersion));
        if (!rsp || rsp->empty()) {
            Log(0, "networkInit: no response from the NCP");
            ashTransport.Close();
            ezspVersion = 0;
            return false;
        }
        const uint8_t status = (*rsp)[0];
        if (status == Ezsp::kEmberSuccess) {
            if (WaitForNetwork(true, kNetworkTimeoutMs)) {
                Log(2, "NCP resumed its saved network");
                ReportNetworkUp();
            } else {
                Log(1, "networkInit succeeded but the network never came up");
            }
        } else if (status == Ezsp::kEmberNotJoined) {
            Log(2, "NCP holds no network; FormNetwork() will create one");
        } else {
            Log(1, "networkInit returned status 0x" + Hex8(status));
        }
        return true;
    }

    // Everything that must be in place before networkInit: the stack
    // configuration, the host endpoint, and the trust-centre policies.
    bool ConfigureNcp() {
        struct { uint8_t id; uint16_t value; const char* name; } configs[] = {
            {Ezsp::kConfigStackProfile, 2, "stack profile"},
            {Ezsp::kConfigSecurityLevel, 5, "security level"},
            {Ezsp::kConfigApplicationZdoFlags,
             Ezsp::kZdoFlagsAppReceivesSupportedRequests | Ezsp::kZdoFlagsAppHandlesUnsupportedRequests,
             "application ZDO flags"},
            {Ezsp::kConfigTrustCenterAddressCacheSize, 2, "trust centre address cache size"},
        };
        for (const auto& c : configs) {
            auto rsp = SendEzspAndWait(Ezsp::kSetConfigValue,
                                       Ezsp::EncodeSetConfigValueParams(c.id, c.value));
            if (!rsp) {
                Log(0, std::string("setConfigurationValue(") + c.name + "): no response");
                return false;
            }
            if (rsp->empty() || (*rsp)[0] != 0x00) {
                // Some values cannot be changed on some firmware; that is a
                // warning, not a reason to give up on the NCP.
                Log(1, std::string("NCP refused ") + c.name + " (status 0x" +
                       Hex8(rsp->empty() ? 0xFF : (*rsp)[0]) + ")");
            }
        }

        // The host endpoint. Its clusters are what this coordinator speaks to
        // devices: it serves Basic and Identify, and is a client of the rest.
        const std::vector<uint16_t> inputClusters{0x0000, 0x0003};
        const std::vector<uint16_t> outputClusters{
            0x0000, 0x0003, 0x0004, 0x0005, 0x0006, 0x0008, 0x0300,
            0x0400, 0x0402, 0x0405, 0x0406, 0x0500, 0x0702};
        if (!SendEzspExpectingStatus("addEndpoint", Ezsp::kAddEndpoint,
                Ezsp::EncodeAddEndpointParams(kHostEndpoint, ZigbeeProfiles::HomeAutomation,
                                              0x0005 /* configuration tool */, 0,
                                              inputClusters, outputClusters))) {
            return false;
        }

        // Trust-centre policies: joins allowed, devices may ask for the
        // current link key, application keys are not handed out.
        const uint8_t joinDecision = ezspVersion >= 8
            ? static_cast<uint8_t>(Ezsp::kDecisionAllowJoins | Ezsp::kDecisionAllowUnsecuredRejoins)
            : Ezsp::kDecisionAllowJoinsLegacy;
        struct { uint8_t policy; uint8_t decision; const char* name; } policies[] = {
            {Ezsp::kPolicyTrustCenter, joinDecision, "trust centre policy"},
            {Ezsp::kPolicyTcKeyRequest, Ezsp::kDecisionAllowTcKeyRequestsSendCurrent, "TC key request policy"},
            {Ezsp::kPolicyAppKeyRequest, Ezsp::kDecisionDenyAppKeyRequests, "app key request policy"},
        };
        for (const auto& p : policies) {
            if (!SendEzspExpectingStatus(p.name, Ezsp::kSetPolicy,
                                         Ezsp::EncodeSetPolicyParams(p.policy, p.decision))) {
                return false;
            }
        }
        return true;
    }

    void OnEzspFrameReceived(const std::vector<uint8_t>& raw) {
        // Before the version is known, the only frame that can arrive is the
        // version response, and it is in the legacy format.
        if (ezspVersion == 0) {
            if (auto version = Ezsp::DecodeVersionResponse(raw)) {
                ezspVersion = version->ProtocolVersion;
            }
            return;
        }
        auto frame = Ezsp::DecodeFrame(ezspVersion, raw);
        if (!frame) return;

        if (frame->Overflow) {
            // The NCP ran out of buffers and dropped callbacks. Devices may have
            // reported things nobody will ever hear about, so say so rather than
            // letting the node table quietly drift out of date.
            Log(1, "NCP callback overflow — some device reports were lost");
        }

        // A command someone is waiting on? Callbacks carry their own frame
        // ids, so they cannot be mistaken for one even if the sequence
        // numbers happen to line up.
        if (frame->IsResponse) {
            std::lock_guard<std::mutex> lock(commandMutex);
            auto it = pendingCommands.find(frame->Sequence);
            if (it != pendingCommands.end() && it->second.FrameId == frame->Id &&
                !it->second.Done) {
                it->second.Response = frame->Parameters;
                it->second.Done = true;
                commandCv.notify_all();
                return;
            }
        }

        switch (frame->Id) {
            case Ezsp::kIncomingMessage:
                HandleIncomingMessage(frame->Parameters);
                break;
            case Ezsp::kStackStatus:
                HandleStackStatus(frame->Parameters);
                break;
            case Ezsp::kTrustCenterJoin:
                HandleTrustCenterJoin(frame->Parameters);
                break;
            case Ezsp::kMessageSent:
                break;   // delivery reports: the device's answer is what counts
            default:
                break;   // an unsolicited response nobody waits for any more
        }
    }

    void HandleTrustCenterJoin(const std::vector<uint8_t>& params) {
        auto join = Ezsp::DecodeTrustCenterJoin(params);
        if (!join || !protocol) return;
        if (join->Status == Ezsp::kDeviceUpdateLeft) {
            // The device is gone; forget it. No leave request goes back out
            // — this runs on the receive thread and could not wait for the
            // answer anyway.
            protocol->OnDeviceLeft(join->Eui64);
        }
        // Joins are learnt from the device's own Device_annce, which carries
        // the same addresses and arrives on every join and rejoin.
    }

    // ----- incoming messages -----
    //
    // Everything a device says arrives through incomingMessageHandler: ZDO
    // responses and announcements on profile 0x0000, ZCL on everything else.
    // The APS frame decides which, and then the first bytes of the payload
    // say which conversation it belongs to.

    void HandleIncomingMessage(const std::vector<uint8_t>& params) {
        auto msg = Ezsp::DecodeIncomingMessage(params);
        if (!msg) {
            Log(1, "incomingMessageHandler too short to unpack; dropped");
            return;
        }
        if (msg->Aps.ProfileId == Ezsp::Zdo::kProfile) {
            HandleZdoMessage(*msg);
        } else {
            HandleZclMessage(*msg);
        }
    }

    void HandleZdoMessage(const Ezsp::IncomingMessage& msg) {
        const uint16_t cluster = msg.Aps.ClusterId;

        if (cluster == Ezsp::Zdo::kDeviceAnnce) {
            // Broadcast by every device when it joins or rejoins. It is the
            // one ZDO frame nobody asked for, and the way the coordinator
            // learns a network address.
            if (auto annce = Ezsp::Zdo::DecodeDeviceAnnce(msg.Contents)) {
                if (protocol) protocol->OnDeviceAnnounce(annce->Ieee, annce->Nwk);
            }
            return;
        }

        // Every other ZDO frame of interest is a response, and the TSN in its
        // first byte names the request it answers.
        if (!(cluster & 0x8000) || msg.Contents.empty()) return;

        std::function<void(bool, const std::vector<uint8_t>&)> callback;
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            auto it = pendingZdo.find(msg.Contents[0]);
            if (it == pendingZdo.end() || it->second.ResponseCluster != cluster) {
                return;   // late, duplicate, or somebody else's transaction
            }
            callback = std::move(it->second.Callback);
            pendingZdo.erase(it);
        }
        if (callback) callback(true, msg.Contents);
    }

    void HandleZclMessage(const Ezsp::IncomingMessage& msg) {
        auto header = Ezsp::Zcl::DecodeHeader(msg.Contents);
        if (!header) return;

        const uint16_t cluster = msg.Aps.ClusterId;
        const uint8_t endpoint = msg.Aps.SourceEndpoint;
        const std::vector<uint8_t> payload(msg.Contents.begin() + header->PayloadOffset,
                                           msg.Contents.end());

        // Whoever this is, they were just heard from.
        const uint64_t ieee = protocol
            ? protocol->NoteHeardFrom(msg.Sender, msg.LastHopLqi, msg.LastHopRssi)
            : 0;

        // A reply to one of our own ZCL requests comes back with our TSN.
        if (!header->ClusterSpecific) {
            std::function<void(bool, const std::vector<uint8_t>&)> callback;
            {
                std::lock_guard<std::mutex> lock(requestMutex);
                auto it = pendingRequests.find(header->Tsn);
                if (it != pendingRequests.end() && it->second.ClusterId == cluster) {
                    callback = std::move(it->second.Callback);
                    pendingRequests.erase(it);
                }
            }
            if (callback) {
                callback(true, payload);
                return;
            }
        }

        if (ieee == 0) {
            // A device this coordinator has never seen announce itself. Its
            // report cannot be filed anywhere yet.
            Log(3, "ZCL message from unknown node 0x" + Hex16(msg.Sender));
            return;
        }

        if (!header->ClusterSpecific &&
            (header->CommandId == Ezsp::Zcl::kReportAttributes ||
             header->CommandId == Ezsp::Zcl::kReadAttributesResponse)) {
            // Unsolicited reports, and read responses nobody is waiting for,
            // are both attribute values and go the same way.
            const bool withStatus = header->CommandId == Ezsp::Zcl::kReadAttributesResponse;
            for (const auto& attr : Ezsp::Zcl::DecodeAttributes(
                     msg.Contents, header->PayloadOffset, withStatus)) {
                if (withStatus && attr.Status != 0) continue;
                ZigbeeAttributeValue value;
                value.ClusterId = cluster;
                value.AttributeId = attr.Id;
                value.DataType = attr.DataType;
                value.Value = attr.Value;
                value.Status = attr.Status;
                if (protocol) protocol->OnAttributeReport(ieee, endpoint, cluster, value);
            }
            return;
        }

        if (protocol) {
            protocol->OnZCLResponse(ieee, endpoint, cluster, header->CommandId, payload);
        }
    }

    static std::string Hex16(uint16_t v) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "%04X", v);
        return buf;
    }

    // ----- sending -----

    bool SendEzsp(uint16_t frameId, const std::vector<uint8_t>& params) {
        if (ezspVersion == 0) return false;
        return ashTransport.Send(
            Ezsp::EncodeCommand(ezspVersion, NextSequence(), frameId, params));
    }

    void ShutdownEZSP() {
        ashTransport.Close();
        ezspVersion = 0;
        // Anyone waiting on an answer gets "none" now rather than at their
        // timeout.
        {
            std::lock_guard<std::mutex> lock(commandMutex);
            pendingCommands.clear();
            commandCv.notify_all();
        }
        {
            std::lock_guard<std::mutex> lock(stackStatusMutex);
            networkUp = false;
            stackStatusCv.notify_all();
        }
    }

    // ----- network formation -----
    //
    // Security first, then the network parameters, then wait for the stack
    // to say NETWORK_UP and read back what it is actually running.
    bool EZSP_FormNetwork(uint16_t panId, uint8_t channel, uint64_t extendedPanId,
                          const std::array<uint8_t, 16>& networkKey) {
        Ezsp::InitialSecurityState security;
        security.Bitmask = Ezsp::kSecurityHavePreconfiguredKey |
                           Ezsp::kSecurityHaveNetworkKey |
                           Ezsp::kSecurityTrustCenterGlobalLinkKey |
                           Ezsp::kSecurityRequireEncryptedKey;
        security.PreconfiguredKey = Ezsp::kZigbeeAllianceKey;
        security.NetworkKey = networkKey;
        security.NetworkKeySequenceNumber = 0;
        security.TrustCenterEui64 = 0;
        if (!SendEzspExpectingStatus("setInitialSecurityState", Ezsp::kSetInitialSecurityState,
                                     Ezsp::EncodeInitialSecurityStateParams(security))) {
            return false;
        }

        Ezsp::NetworkParameters params;
        params.ExtendedPanId = extendedPanId;
        params.PanId = panId;
        params.RadioTxPower = 8;
        params.RadioChannel = channel;
        params.JoinMethod = 0;               // MAC association
        params.NwkManagerId = 0;
        params.NwkUpdateId = 0;
        params.Channels = 1u << channel;
        std::vector<uint8_t> encoded;
        Ezsp::AppendNetworkParameters(encoded, params);
        if (!SendEzspExpectingStatus("formNetwork", Ezsp::kFormNetwork, encoded)) {
            return false;
        }

        if (!WaitForNetwork(true, kNetworkTimeoutMs)) {
            Log(0, "formNetwork was accepted but the stack never reported NETWORK_UP");
            return false;
        }
        return ReportNetworkUp();
    }

    bool EZSP_PermitJoining(uint8_t duration) {
        if (duration > 0) {
            // Zigbee 3.0 devices join using the well-known key as a transient
            // link key. Firmware old enough to lack the command still admits
            // Home Automation devices through the preconfigured key, so a
            // refusal here is worth a note, not a failure.
            if (!SendEzspExpectingStatus("addTransientLinkKey", Ezsp::kAddTransientLinkKey,
                    Ezsp::EncodeAddTransientLinkKeyParams(0xFFFFFFFFFFFFFFFFULL,
                                                          Ezsp::kZigbeeAllianceKey))) {
                Log(1, "transient link key not installed; Zigbee 3.0 devices may not join");
            }
        }
        if (!SendEzspExpectingStatus("permitJoining", Ezsp::kPermitJoining, {duration})) {
            return false;
        }

        // Tell the routers too, or only the coordinator's own radio range
        // would be open. Broadcast, so no answer is expected.
        Ezsp::ApsFrame aps;
        aps.ProfileId = Ezsp::Zdo::kProfile;
        aps.ClusterId = Ezsp::Zdo::kMgmtPermitJoiningReq;
        aps.SourceEndpoint = Ezsp::Zdo::kEndpoint;
        aps.DestinationEndpoint = Ezsp::Zdo::kEndpoint;
        aps.Sequence = apsSequence++;
        const std::vector<uint8_t> req =
            Ezsp::Zdo::EncodeMgmtPermitJoiningReq(NextZdoSequence(), duration);
        if (!SendEzsp(Ezsp::kSendBroadcast,
                      Ezsp::EncodeSendBroadcastParams(Ezsp::kBroadcastRouters, aps,
                                                      /*radius*/ 0, aps.Sequence, req))) {
            Log(1, "Mgmt_Permit_Joining broadcast not sent; only direct joins are open");
        }
        return true;
    }

    bool EZSP_LeaveNetwork() {
        if (!SendEzspExpectingStatus("leaveNetwork", Ezsp::kLeaveNetwork, {})) {
            return false;
        }
        if (!WaitForNetwork(false, kNetworkTimeoutMs)) {
            Log(1, "leaveNetwork was accepted but the stack never reported NETWORK_DOWN");
        }
        return true;
    }

    // The endpoint this host speaks from. Endpoint 1, Home Automation profile,
    // is what every Zigbee coordinator exposes.
    static constexpr uint8_t kHostEndpoint = 1;

    // Unicast APS options: ask for an APS acknowledgement and let the stack
    // find a route if it has none.
    static constexpr uint16_t kUnicastOptions =
        Ezsp::kApsRetry | Ezsp::kApsEnableRouteDiscovery;

    // A ZCL frame to one device. The message tag is the APS sequence, which
    // is enough to tell messageSentHandler callbacks apart if they are ever
    // listened to.
    bool EZSP_SendUnicast(uint16_t nwk, uint8_t ep, uint16_t cluster,
                          const std::vector<uint8_t>& data) {
        Ezsp::ApsFrame aps;
        aps.ProfileId = ZigbeeProfiles::HomeAutomation;
        aps.ClusterId = cluster;
        aps.SourceEndpoint = kHostEndpoint;
        aps.DestinationEndpoint = ep;
        aps.Options = kUnicastOptions;
        aps.Sequence = apsSequence++;
        return SendEzsp(Ezsp::kSendUnicast,
                        Ezsp::EncodeSendUnicastParams(nwk, aps, aps.Sequence, data));
    }

    // A ZCL frame to a group. Groupcasts are not acknowledged; the radius
    // is the default non-member radius from the Zigbee spec.
    bool EZSP_SendMulticast(uint16_t group, uint8_t ep, uint16_t cluster,
                            const std::vector<uint8_t>& data) {
        Ezsp::ApsFrame aps;
        aps.ProfileId = ZigbeeProfiles::HomeAutomation;
        aps.ClusterId = cluster;
        aps.SourceEndpoint = kHostEndpoint;
        aps.DestinationEndpoint = ep;
        aps.GroupId = group;
        aps.Sequence = apsSequence++;
        return SendEzsp(Ezsp::kSendMulticast,
                        Ezsp::EncodeSendMulticastParams(aps, /*hops*/ 0,
                                                        /*nonMemberRadius*/ 7,
                                                        aps.Sequence, data));
    }

    // ----- ZDO -----
    //
    // ZDO requests are ordinary unicasts on profile 0, endpoint 0. The frame's
    // first byte is the transaction sequence number and the answer echoes it,
    // so a request registers itself under that number and the incoming-message
    // path looks it up. The callback always fires: with the response frame on
    // success, or with `false` from CheckTimeouts.

    uint8_t NextZdoSequence() { return zdoSequence++; }

    bool SendZdo(uint16_t nwk, uint16_t requestCluster, uint16_t responseCluster,
                 const std::vector<uint8_t>& zdoFrame,
                 std::function<void(bool, const std::vector<uint8_t>&)> callback) {
        if (zdoFrame.empty()) return false;
        const uint8_t tsn = zdoFrame[0];

        {
            std::lock_guard<std::mutex> lock(requestMutex);
            PendingZdo pending;
            pending.ResponseCluster = responseCluster;
            pending.Callback = std::move(callback);
            pending.Timestamp = GetTimestamp();
            pendingZdo[tsn] = std::move(pending);
        }

        Ezsp::ApsFrame aps;
        aps.ProfileId = Ezsp::Zdo::kProfile;
        aps.ClusterId = requestCluster;
        aps.SourceEndpoint = Ezsp::Zdo::kEndpoint;
        aps.DestinationEndpoint = Ezsp::Zdo::kEndpoint;
        aps.Options = kUnicastOptions;
        aps.Sequence = apsSequence++;

        if (!SendEzsp(Ezsp::kSendUnicast,
                      Ezsp::EncodeSendUnicastParams(nwk, aps, aps.Sequence, zdoFrame))) {
            std::lock_guard<std::mutex> lock(requestMutex);
            pendingZdo.erase(tsn);
            return false;
        }
        return true;
    }

    bool EZSP_ActiveEndpoints(uint16_t nwk,
                              std::function<void(bool, const std::vector<uint8_t>&)> cb) {
        const uint8_t tsn = NextZdoSequence();
        return SendZdo(nwk, Ezsp::Zdo::kActiveEpReq, Ezsp::Zdo::kActiveEpRsp,
                       Ezsp::Zdo::EncodeActiveEpReq(tsn, nwk),
                       [cb](bool ok, const std::vector<uint8_t>& frame) {
                           if (!cb) return;
                           auto rsp = ok ? Ezsp::Zdo::DecodeActiveEpRsp(frame) : std::nullopt;
                           if (!rsp || rsp->Status != Ezsp::Zdo::kStatusSuccess) {
                               cb(false, {});
                               return;
                           }
                           cb(true, rsp->Endpoints);
                       });
    }

    bool EZSP_SimpleDescriptor(uint16_t nwk, uint8_t endpoint,
                               std::function<void(bool, const ZigbeeEndpoint&)> cb) {
        const uint8_t tsn = NextZdoSequence();
        return SendZdo(nwk, Ezsp::Zdo::kSimpleDescReq, Ezsp::Zdo::kSimpleDescRsp,
                       Ezsp::Zdo::EncodeSimpleDescReq(tsn, nwk, endpoint),
                       [cb](bool ok, const std::vector<uint8_t>& frame) {
                           if (!cb) return;
                           auto rsp = ok ? Ezsp::Zdo::DecodeSimpleDescRsp(frame) : std::nullopt;
                           if (!rsp || rsp->Status != Ezsp::Zdo::kStatusSuccess) {
                               cb(false, ZigbeeEndpoint{});
                               return;
                           }
                           ZigbeeEndpoint ep;
                           ep.EndpointId = rsp->Endpoint;
                           ep.ProfileId = rsp->ProfileId;
                           ep.DeviceId = rsp->DeviceId;
                           ep.DeviceVersion = rsp->DeviceVersion;
                           for (uint16_t id : rsp->InputClusters) {
                               ZigbeeCluster c;
                               c.ClusterId = id;
                               c.IsServer = true;
                               ep.InputClusters.push_back(c);
                           }
                           for (uint16_t id : rsp->OutputClusters) {
                               ZigbeeCluster c;
                               c.ClusterId = id;
                               c.IsServer = false;
                               ep.OutputClusters.push_back(c);
                           }
                           cb(true, ep);
                       });
    }

    bool EZSP_NodeDescriptor(uint16_t nwk,
                             std::function<void(bool, ZigbeeDeviceType)> cb) {
        const uint8_t tsn = NextZdoSequence();
        return SendZdo(nwk, Ezsp::Zdo::kNodeDescReq, Ezsp::Zdo::kNodeDescRsp,
                       Ezsp::Zdo::EncodeNodeDescReq(tsn, nwk),
                       [cb](bool ok, const std::vector<uint8_t>& frame) {
                           if (!cb) return;
                           auto rsp = ok ? Ezsp::Zdo::DecodeNodeDescRsp(frame) : std::nullopt;
                           if (!rsp || rsp->Status != Ezsp::Zdo::kStatusSuccess) {
                               cb(false, ZigbeeDeviceType::Unknown);
                               return;
                           }
                           ZigbeeDeviceType type = ZigbeeDeviceType::Unknown;
                           switch (rsp->LogicalType) {
                               case 0: type = ZigbeeDeviceType::Coordinator; break;
                               case 1: type = ZigbeeDeviceType::Router; break;
                               case 2: type = ZigbeeDeviceType::EndDevice; break;
                               default: break;
                           }
                           cb(true, type);
                       });
    }

    bool EZSP_IeeeAddress(uint16_t nwk, std::function<void(bool, uint64_t)> cb) {
        const uint8_t tsn = NextZdoSequence();
        return SendZdo(nwk, Ezsp::Zdo::kIeeeAddrReq, Ezsp::Zdo::kIeeeAddrRsp,
                       Ezsp::Zdo::EncodeIeeeAddrReq(tsn, nwk),
                       [cb](bool ok, const std::vector<uint8_t>& frame) {
                           if (!cb) return;
                           auto rsp = ok ? Ezsp::Zdo::DecodeIeeeAddrRsp(frame) : std::nullopt;
                           if (!rsp || rsp->Status != Ezsp::Zdo::kStatusSuccess) {
                               cb(false, 0);
                               return;
                           }
                           cb(true, rsp->Ieee);
                       });
    }

    // Bind, unbind and leave are answered by a bare status, and their callers
    // expect a yes or no. So these block until the device answers or the
    // request times out; `true` means the device said so, not that the bytes
    // left the serial port. Not to be called from the receive thread.
    bool SendZdoAndWaitForStatus(uint16_t nwk, uint16_t requestCluster,
                                 uint16_t responseCluster,
                                 const std::vector<uint8_t>& zdoFrame) {
        struct Wait {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            bool ok = false;
        };
        auto wait = std::make_shared<Wait>();

        const bool sent = SendZdo(nwk, requestCluster, responseCluster, zdoFrame,
            [wait](bool ok, const std::vector<uint8_t>& frame) {
                auto rsp = ok ? Ezsp::Zdo::DecodeStatusRsp(frame) : std::nullopt;
                std::lock_guard<std::mutex> lock(wait->m);
                wait->ok = rsp && rsp->Status == Ezsp::Zdo::kStatusSuccess;
                wait->done = true;
                wait->cv.notify_all();
            });
        if (!sent) return false;

        std::unique_lock<std::mutex> lock(wait->m);
        wait->cv.wait_for(lock, std::chrono::milliseconds(kRequestTimeoutMs + 500),
                          [&] { return wait->done; });
        return wait->done && wait->ok;
    }

    // A binding lives in the source device's table, so the request goes to
    // the source. The IEEE address has to be turned into a network address
    // first, which the protocol keeps.
    bool EZSP_Bind(uint64_t srcIeee, uint8_t srcEp, uint16_t cluster,
                   uint64_t dstIeee, uint8_t dstEp) {
        uint16_t nwk;
        if (!protocol || !protocol->NwkForIeee(srcIeee, nwk)) return false;
        const uint8_t tsn = NextZdoSequence();
        return SendZdoAndWaitForStatus(nwk, Ezsp::Zdo::kBindReq, Ezsp::Zdo::kBindRsp,
            Ezsp::Zdo::EncodeBindReq(tsn, srcIeee, srcEp, cluster, dstIeee, dstEp));
    }

    bool EZSP_Unbind(uint64_t srcIeee, uint8_t srcEp, uint16_t cluster,
                     uint64_t dstIeee, uint8_t dstEp) {
        uint16_t nwk;
        if (!protocol || !protocol->NwkForIeee(srcIeee, nwk)) return false;
        const uint8_t tsn = NextZdoSequence();
        return SendZdoAndWaitForStatus(nwk, Ezsp::Zdo::kUnbindReq, Ezsp::Zdo::kUnbindRsp,
            Ezsp::Zdo::EncodeUnbindReq(tsn, srcIeee, srcEp, cluster, dstIeee, dstEp));
    }

    // Mgmt_Leave to the device itself, no rejoin, children left alone.
    bool EZSP_RemoveDevice(uint64_t ieee) {
        uint16_t nwk;
        if (!protocol || !protocol->NwkForIeee(ieee, nwk)) return false;
        const uint8_t tsn = NextZdoSequence();
        return SendZdoAndWaitForStatus(nwk, Ezsp::Zdo::kMgmtLeaveReq, Ezsp::Zdo::kMgmtLeaveRsp,
            Ezsp::Zdo::EncodeMgmtLeaveReq(tsn, ieee, /*rejoin*/ false,
                                          /*removeChildren*/ false));
    }
#endif

#ifdef ULTRACANVAS_WITH_ZSTACK
    bool InitializeZStack() {
        // Initialize Z-Stack ZNP protocol
        return false;  // Not implemented
    }
    
    void ShutdownZStack() {}
    
    bool ZStack_FormNetwork(uint16_t panId, uint8_t channel, uint64_t extendedPanId,
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
                            networkParams.ExtendedPanId, networkParams.NetworkKey)) {
        ReportError(-200, "Failed to form network");
        return false;
    }
    
    // A real stack has already reported the network up (with the parameters
    // it is actually running) through OnNetworkUp; the stub has not.
    networkName_ = networkName;
    if (!hasNetwork) {
        hasNetwork = true;
        SetNetworkInfo(MakeNetworkInfo());
    }
    
    Log(2, "Zigbee network formed: PAN " + HexString(networkParams.PanId, 4) +
        ", Channel " + std::to_string(networkParams.Channel));
    
    return true;
}

SmartHomeNetworkInfo ZigbeeProtocol::MakeNetworkInfo() const {
    SmartHomeNetworkInfo info = GetNetworkInfo();
    info.Protocol = SmartHomeProtocolType::Zigbee;
    info.NetworkId = HexString(networkParams.ExtendedPanId, 16);
    if (!networkName_.empty()) info.NetworkName = networkName_;
    info.PanId = HexString(networkParams.PanId, 4);
    info.Channel = std::to_string(networkParams.Channel);
    info.ExtendedPanId = HexString(networkParams.ExtendedPanId, 16);
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        info.DeviceCount = static_cast<int>(zigbeeNodes.size());
    }
    return info;
}

std::string ZigbeeProtocol::HexString(uint64_t value, int digits) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(digits) << value;
    return out.str();
}

void ZigbeeProtocol::OnNetworkUp(const ZigbeeNetworkParams& params, uint64_t coordinatorIeee) {
    // What the NCP is running wins over what was asked for; the network key
    // is the one thing it does not report, so that stays as set.
    networkParams.ExtendedPanId = params.ExtendedPanId;
    networkParams.PanId = params.PanId;
    networkParams.Channel = params.Channel;
    networkParams.ChannelMask = params.ChannelMask;
    networkParams.NetworkUpdateId = params.NetworkUpdateId;
    networkParams.TrustCenterAddress = params.TrustCenterAddress;
    networkParams.SecurityLevel = params.SecurityLevel;
    coordinatorIeee_ = coordinatorIeee;
    hasNetwork = true;
    SetNetworkInfo(MakeNetworkInfo());
    Log(2, "Zigbee network up: PAN " + HexString(params.PanId, 4) +
        ", channel " + std::to_string(params.Channel) +
        ", coordinator " + IeeeAddressToString(coordinatorIeee));
}

void ZigbeeProtocol::OnNetworkDown() {
    Log(2, "Zigbee network down");
    hasNetwork = false;
    permitJoinActive = false;
    pairing = false;
    ClearNetwork();
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
    
    std::lock_guard<std::mutex> lock(nodeMutex);
    
    // Add coordinator
    NetworkNode coordNode;
    coordNode.NodeId = "coordinator";
    coordNode.IsCoordinator = true;
    coordNode.IsRouter = true;
    coordNode.Depth = 0;
    topology.Nodes.push_back(coordNode);
    
    // Add all nodes
    for (const auto& [deviceId, node] : zigbeeNodes) {
        NetworkNode netNode;
        netNode.NodeId = deviceId;
        netNode.IsRouter = (node.Type == ZigbeeDeviceType::Router);
        netNode.IsRouter = (node.Type == ZigbeeDeviceType::Router);
        netNode.Depth = 1;
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
    info.Active = (duration > 0);
    SetNetworkInfo(info);
    
    // Start timeout countdown
    if (duration > 0) {
        std::thread([this, duration]() {
            std::this_thread::sleep_for(std::chrono::seconds(duration));
            permitJoinActive = false;
            pairing = false;
            
            SmartHomeNetworkInfo info = GetNetworkInfo();
            info.Active = false;
            SetNetworkInfo(info);
        }).detach();
    }
    
    return true;
}

// ===== DEVICE OPERATIONS =====

bool ZigbeeProtocol::RemoveDevice(const std::string& deviceId) {
    uint64_t ieeeAddress;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it == zigbeeNodes.end()) {
            return false;
        }
        ieeeAddress = it->second.IeeeAddress;
    }

    // Ask the device to leave. This waits for its answer and needs the node
    // table to find its address, so it runs with the lock released. Whether
    // or not the device confirms, it is forgotten here: a device that never
    // answered is gone from this side's point of view either way.
    if (!stack->RemoveNode(ieeeAddress)) {
        Log(1, "Device " + deviceId + " did not confirm the leave request");
    }

    ForgetDevice(deviceId);
    return true;
}

bool ZigbeeProtocol::InterviewDevice(const std::string& deviceId) {
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        if (zigbeeNodes.find(deviceId) == zigbeeNodes.end()) {
            return false;
        }
    }
    
    Log(2, "Interviewing device: " + deviceId);
    InterviewNode(deviceId);
    
    return true;
}

// The interview asks the device about itself one ZDO query at a time. Each
// answer arrives on the stack's receive thread, so every step hands the
// receive thread a device id rather than a reference into the node table,
// finds the node again under the lock, and waits for the step to finish
// before starting the next. A node that is removed mid-interview is simply
// not found and the step becomes a no-op.

namespace {
struct InterviewStep {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;

    void Finish() {
        std::lock_guard<std::mutex> lock(m);
        done = true;
        cv.notify_all();
    }
    // The stack guarantees every callback fires, by response or by timeout,
    // so the ceiling here is only a guard against a stack that is shut down
    // underneath the interview.
    void Wait() {
        std::unique_lock<std::mutex> lock(m);
        cv.wait_for(lock, std::chrono::seconds(12), [&] { return done; });
    }
};
}  // namespace

void ZigbeeProtocol::InterviewNode(const std::string& deviceId) {
    uint16_t nwkAddress;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it == zigbeeNodes.end()) return;
        nwkAddress = it->second.NwkAddress;
    }
    
    // Step 1: Node descriptor — what kind of device this is
    {
        auto step = std::make_shared<InterviewStep>();
        stack->RequestNodeDescriptor(nwkAddress,
            [this, deviceId, step](bool success, ZigbeeDeviceType type) {
                if (success) {
                    std::lock_guard<std::mutex> lock(nodeMutex);
                    auto it = zigbeeNodes.find(deviceId);
                    if (it != zigbeeNodes.end()) it->second.Type = type;
                    Log(3, "Node type: " + std::to_string(static_cast<int>(type)));
                }
                step->Finish();
            });
        step->Wait();
    }
    
    // Step 2: Active endpoints
    DiscoverEndpoints(deviceId);
    
    // Step 3: Simple descriptor for each endpoint
    std::vector<uint8_t> endpoints;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it == zigbeeNodes.end()) return;
        for (const auto& ep : it->second.Endpoints) endpoints.push_back(ep.EndpointId);
    }
    for (uint8_t ep : endpoints) {
        DiscoverClusters(deviceId, ep);
    }
    
    // Step 4: Basic cluster attributes, read from the first endpoint
    if (!endpoints.empty()) {
        const uint8_t ep = endpoints.front();
        std::string manufacturerName, modelIdentifier;
        int powerSource = -1;
        
        ZigbeeAttributeValue mfgName;
        if (ReadAttribute(deviceId, ep, ZigbeeClusters::Basic, 
                          BasicAttributes::ManufacturerName, mfgName)) {
            manufacturerName = ZclStringToStd(mfgName);
        }
        
        ZigbeeAttributeValue modelId;
        if (ReadAttribute(deviceId, ep, ZigbeeClusters::Basic,
                          BasicAttributes::ModelIdentifier, modelId)) {
            modelIdentifier = ZclStringToStd(modelId);
        }
        
        ZigbeeAttributeValue powerSrc;
        if (ReadAttribute(deviceId, ep, ZigbeeClusters::Basic,
                          BasicAttributes::PowerSource, powerSrc)) {
            if (!powerSrc.Value.empty()) {
                powerSource = powerSrc.Value[0];
            }
        }
        
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it == zigbeeNodes.end()) return;
        if (!manufacturerName.empty()) it->second.ManufacturerName = manufacturerName;
        if (!modelIdentifier.empty()) it->second.ModelIdentifier = modelIdentifier;
        if (powerSource >= 0) it->second.PowerSource = static_cast<uint8_t>(powerSource);
    }
    
    ZigbeeNode snapshot;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it == zigbeeNodes.end()) return;
        it->second.IsOnline = true;
        it->second.LastSeen = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        snapshot = it->second;
    }
    
    // Update paired device info
    SmartHomeDeviceInfo info = NodeToDeviceInfo(snapshot);
    UpdatePairedDevice(info);
    
    Log(2, "Interview complete: " + snapshot.ManufacturerName + " " + snapshot.ModelIdentifier);
}

// ZCL character strings carry a length byte before the text; a device that
// answers with an octet string (0x41) or a long string (0x43/0x44) is treated
// the same way, because the Basic cluster's names are all human-readable.
std::string ZigbeeProtocol::ZclStringToStd(const ZigbeeAttributeValue& value) {
    const auto& v = value.Value;
    if (v.empty()) return {};
    size_t lengthBytes = 1;
    if (value.DataType == 0x43 || value.DataType == 0x44) lengthBytes = 2;
    if (v.size() < lengthBytes) return {};
    size_t len = v[0];
    if (lengthBytes == 2) len |= static_cast<size_t>(v[1]) << 8;
    if (len == 0xFF || len == 0xFFFF) return {};       // "invalid" string marker
    len = std::min(len, v.size() - lengthBytes);
    return std::string(v.begin() + lengthBytes, v.begin() + lengthBytes + len);
}

void ZigbeeProtocol::DiscoverEndpoints(const std::string& deviceId) {
    uint16_t nwkAddress;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it == zigbeeNodes.end()) return;
        nwkAddress = it->second.NwkAddress;
    }
    
    auto step = std::make_shared<InterviewStep>();
    stack->RequestActiveEndpoints(nwkAddress,
        [this, deviceId, step](bool success, const std::vector<uint8_t>& endpoints) {
            if (success) {
                std::lock_guard<std::mutex> lock(nodeMutex);
                auto it = zigbeeNodes.find(deviceId);
                if (it != zigbeeNodes.end()) {
                    it->second.Endpoints.clear();
                    for (uint8_t ep : endpoints) {
                        ZigbeeEndpoint endpoint;
                        endpoint.EndpointId = ep;
                        it->second.Endpoints.push_back(endpoint);
                    }
                }
                Log(3, "Found " + std::to_string(endpoints.size()) + " endpoints");
            }
            step->Finish();
        });
    step->Wait();
}

void ZigbeeProtocol::DiscoverClusters(const std::string& deviceId, uint8_t endpointId) {
    uint16_t nwkAddress;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it == zigbeeNodes.end()) return;
        nwkAddress = it->second.NwkAddress;
    }
    
    auto step = std::make_shared<InterviewStep>();
    stack->RequestSimpleDescriptor(nwkAddress, endpointId,
        [this, deviceId, endpointId, step](bool success, const ZigbeeEndpoint& descriptor) {
            if (success) {
                std::lock_guard<std::mutex> lock(nodeMutex);
                auto it = zigbeeNodes.find(deviceId);
                if (it != zigbeeNodes.end()) {
                    for (auto& ep : it->second.Endpoints) {
                        if (ep.EndpointId == endpointId) {
                            ep = descriptor;
                            break;
                        }
                    }
                }
                Log(3, "Endpoint " + std::to_string(endpointId) + 
                    ": " + std::to_string(descriptor.InputClusters.size()) + " clusters");
            }
            step->Finish();
        });
    step->Wait();
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
    
    // Send Add Group command (SendZCLCommand checks the device exists)
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

SmartHomeSecurityLevel ZigbeeProtocol::GetSecurityLevel() const {
    return SmartHomeSecurityLevel::Encrypted;  // Zigbee uses AES-128-CCM
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
    
    // The answer arrives on the stack's receive thread, or as a timeout from
    // its housekeeping, and either way exactly once. The state is shared so a
    // late answer after this call has given up has somewhere harmless to go.
    struct Reply {
        std::mutex m;
        std::condition_variable cv;
        bool done = false;
        bool success = false;
        ZigbeeAttributeValue value;
    };
    auto reply = std::make_shared<Reply>();
    
    const bool sent = stack->SendZCLFrame(nwkAddress, endpoint, clusterId,
                        ZCL::FrameTypeGlobal, ZCL::CmdReadAttributes, payload,
                        [reply, clusterId](bool ok, const std::vector<uint8_t>& data) {
                            std::lock_guard<std::mutex> lock(reply->m);
                            // Read Attributes Response record: id, status,
                            // then type and value only when status is success.
                            if (ok && data.size() >= 3) {
                                reply->value.ClusterId = clusterId;
                                reply->value.AttributeId = (data[1] << 8) | data[0];
                                reply->value.Status = data[2];
                                if (reply->value.Status == 0 && data.size() > 4) {
                                    reply->value.DataType = data[3];
                                    reply->value.Value.assign(data.begin() + 4, data.end());
                                }
                                reply->success = reply->value.Status == 0;
                            }
                            reply->done = true;
                            reply->cv.notify_all();
                        });
    if (!sent) return false;
    
    std::unique_lock<std::mutex> lock(reply->m);
    reply->cv.wait_for(lock, std::chrono::seconds(5), [&] { return reply->done; });
    if (reply->done && reply->success) {
        outValue = reply->value;
        return true;
    }
    return false;
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
        // It has already gone; there is nobody to send a leave request to,
        // and this may be running on the stack's receive thread, which
        // could not wait for the answer anyway.
        ForgetDevice(deviceId);
    }
}

void ZigbeeProtocol::ForgetDevice(const std::string& deviceId) {
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it != zigbeeNodes.end()) {
            ieeeToDeviceId.erase(it->second.IeeeAddress);
            nwkToDeviceId.erase(it->second.NwkAddress);
            zigbeeNodes.erase(it);
        }
    }
    RemovePairedDevice(deviceId);
    Log(2, "Removed Zigbee device: " + deviceId);
}

void ZigbeeProtocol::OnDeviceAnnounce(uint64_t ieeeAddress, uint16_t nwkAddress) {
    Log(3, "Device announce: " + IeeeAddressToString(ieeeAddress));
    
    // A known device announcing is back online, possibly at a new network
    // address. An unknown one has just joined: Device_annce is the first
    // thing a device broadcasts after joining, and how the coordinator hears
    // of it.
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = ieeeToDeviceId.find(ieeeAddress);
        if (it != ieeeToDeviceId.end()) {
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
            return;
        }
    }
    OnDeviceJoined(ieeeAddress, nwkAddress);
}

uint64_t ZigbeeProtocol::NoteHeardFrom(uint16_t nwkAddress, uint8_t lqi, int8_t rssi) {
    std::lock_guard<std::mutex> lock(nodeMutex);
    auto idIt = nwkToDeviceId.find(nwkAddress);
    if (idIt == nwkToDeviceId.end()) return 0;
    auto it = zigbeeNodes.find(idIt->second);
    if (it == zigbeeNodes.end()) return 0;
    
    ZigbeeNode& node = it->second;
    node.Lqi = lqi;
    node.Rssi = rssi;
    node.IsOnline = true;
    node.LastSeen = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    return node.IeeeAddress;
}

bool ZigbeeProtocol::NwkForIeee(uint64_t ieeeAddress, uint16_t& nwkAddress) const {
    std::lock_guard<std::mutex> lock(nodeMutex);
    auto idIt = ieeeToDeviceId.find(ieeeAddress);
    if (idIt == ieeeToDeviceId.end()) return false;
    auto it = zigbeeNodes.find(idIt->second);
    if (it == zigbeeNodes.end()) return false;
    nwkAddress = it->second.NwkAddress;
    return true;
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

bool ZigbeeProtocol::GetDeviceState(const std::string& deviceId,
                                    std::map<std::string, std::string>& state) {
    ZigbeeNode node;
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = zigbeeNodes.find(deviceId);
        if (it == zigbeeNodes.end()) return false;
        node = it->second;
    }
    state["online"] = node.IsOnline ? "true" : "false";
    state["nwk"] = std::to_string(node.NwkAddress);
    state["ieee"] = IeeeAddressToString(node.IeeeAddress);
    state["lqi"] = std::to_string(static_cast<int>(node.Lqi));
    state["rssi"] = std::to_string(static_cast<int>(node.Rssi));
    state["endpoints"] = std::to_string(node.Endpoints.size());
    return true;
}

bool ZigbeeProtocol::SetChannel(int newChannel) {
    // 802.15.4 channels 11-26 in the 2.4 GHz band; anything else is not a
    // Zigbee channel and would be silently ignored by the NCP.
    if (newChannel < 11 || newChannel > 26) return false;
    // Changing channel means moving the whole network, which the coordinator
    // announces to its children; without a formed network there is nothing to
    // move.
    if (!HasNetwork()) return false;
    return ChangeChannel(static_cast<uint8_t>(newChannel));
}

std::string ZigbeeProtocol::GetHardwareInfo() const {
    std::string info = "Zigbee NCP on " +
        (selectedAdapter.empty() ? std::string("(no adapter selected)") : selectedAdapter);
    if (HasNetwork()) {
        char buf[64];
        std::snprintf(buf, sizeof buf, ", PAN 0x%04X channel %u",
                      static_cast<unsigned>(GetPanId()),
                      static_cast<unsigned>(GetChannel()));
        info += buf;
    }
    return info;
}

std::vector<SmartHomeDeviceInfo> ZigbeeProtocol::GetPairedDevices() {
    std::vector<SmartHomeDeviceInfo> devices;
    std::lock_guard<std::mutex> lock(nodeMutex);
    devices.reserve(zigbeeNodes.size());
    for (const auto& [id, node] : zigbeeNodes) {
        devices.push_back(NodeToDeviceInfo(node));
    }
    return devices;
}

bool ZigbeeProtocol::PairDevice(const std::string&,
                                const std::map<std::string, std::string>& params) {
    // Zigbee devices are not paired individually: the coordinator opens the
    // network and whatever is in pairing mode joins. Anything else would be
    // reporting a per-device handshake that does not exist.
    int duration = 60;
    if (auto it = params.find("duration"); it != params.end()) {
        duration = std::atoi(it->second.c_str());
    }
    return PermitJoin(duration);
}

bool ZigbeeProtocol::UnpairDevice(const std::string& deviceId) {
    return RemoveDevice(deviceId);
}

std::vector<std::shared_ptr<ISmartHomeDevice>> ZigbeeProtocol::GetDevices() const {
    return {};
}

std::shared_ptr<ISmartHomeDevice> ZigbeeProtocol::GetDevice(const std::string&) const {
    return nullptr;
}

std::shared_ptr<ISmartHomeProtocol> CreateZigbeeProtocol() {
    return std::make_shared<ZigbeeProtocol>();
}

} // namespace SmartHome
} // namespace UltraCanvas
