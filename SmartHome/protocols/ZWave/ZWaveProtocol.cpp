// ZWaveProtocol.cpp
// Z-Wave Protocol Implementation for UltraCanvas SmartHome
// Version: 1.0.0
// Last Modified: 2025-12-09
// Author: UltraCanvas Framework

#include "ZWaveProtocol.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>

// OpenZWave headers
#ifdef ULTRACANVAS_WITH_ZWAVE
#include <openzwave/Manager.h>
#include <openzwave/Notification.h>
#include <openzwave/Options.h>
#include <openzwave/Driver.h>
#include <openzwave/Node.h>
#include <openzwave/Group.h>
#include <openzwave/Scene.h>
#include <openzwave/ValueStore.h>
#include <openzwave/value_classes/Value.h>
#include <openzwave/value_classes/ValueBool.h>
#include <openzwave/value_classes/ValueByte.h>
#include <openzwave/value_classes/ValueDecimal.h>
#include <openzwave/value_classes/ValueInt.h>
#include <openzwave/value_classes/ValueList.h>
#include <openzwave/value_classes/ValueShort.h>
#include <openzwave/value_classes/ValueString.h>
#include <openzwave/value_classes/ValueButton.h>
#include <openzwave/value_classes/ValueRaw.h>
#endif

namespace UltraCanvas {
namespace SmartHome {

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

ZWaveProtocol::ZWaveProtocol() {
    // Default paths - can be overridden before Initialize()
#ifdef __linux__
    controllerPath = "/dev/ttyUSB0";
    configPath = "/etc/openzwave";
    userPath = "/var/lib/openzwave";
#elif defined(__APPLE__)
    controllerPath = "/dev/cu.usbserial";
    configPath = "/usr/local/etc/openzwave";
    userPath = "/var/lib/openzwave";
#elif defined(_WIN32)
    controllerPath = "\\\\.\\COM3";
    configPath = "C:\\openzwave\\config";
    userPath = "C:\\openzwave\\user";
#endif
}

ZWaveProtocol::~ZWaveProtocol() {
    Shutdown();
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool ZWaveProtocol::Initialize() {
    if (initialized) {
        return true;
    }
    
#ifdef ULTRACANVAS_WITH_ZWAVE
    try {
        // Initialize OpenZWave Options
        OpenZWave::Options::Create(configPath, userPath, "");
        
        // Configure options
        OpenZWave::Options::Get()->AddOptionInt("SaveLogLevel", OpenZWave::LogLevel_Detail);
        OpenZWave::Options::Get()->AddOptionInt("QueueLogLevel", OpenZWave::LogLevel_Debug);
        OpenZWave::Options::Get()->AddOptionInt("DumpTriggerLevel", OpenZWave::LogLevel_Error);
        OpenZWave::Options::Get()->AddOptionBool("ConsoleOutput", false);
        OpenZWave::Options::Get()->AddOptionBool("Logging", true);
        OpenZWave::Options::Get()->AddOptionBool("SaveConfiguration", true);
        OpenZWave::Options::Get()->AddOptionInt("PollInterval", 500);
        OpenZWave::Options::Get()->AddOptionBool("IntervalBetweenPolls", true);
        OpenZWave::Options::Get()->AddOptionBool("ValidateValueChanges", true);
        OpenZWave::Options::Get()->AddOptionBool("Associate", true);
        
        // Security settings
        OpenZWave::Options::Get()->AddOptionBool("EnableSIS", true);
        OpenZWave::Options::Get()->AddOptionString("NetworkKey", 
            "0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10", false);
        
        OpenZWave::Options::Get()->Lock();
        
        // Create Manager
        OpenZWave::Manager::Create();
        
        // Add notification watcher
        OpenZWave::Manager::Get()->AddWatcher(OnNotification, this);
        
        // Add driver for controller
        if (!OpenZWave::Manager::Get()->AddDriver(controllerPath)) {
            std::cerr << "[ZWave] Failed to add driver for " << controllerPath << std::endl;
            return false;
        }
        
        // Start notification processing thread
        stopProcessing = false;
        processingThread = std::thread(&ZWaveProtocol::ProcessNotificationQueue, this);
        
        initialized = true;
        std::cout << "[ZWave] Initialized with controller: " << controllerPath << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[ZWave] Initialization error: " << e.what() << std::endl;
        return false;
    }
#else
    // Stub mode
    std::cout << "[ZWave] Running in stub mode (OpenZWave not available)" << std::endl;
    initialized = true;
    homeId = 0x12345678;
    controllerNodeId = 1;
    networkReady = true;
    return true;
#endif
}

void ZWaveProtocol::Shutdown() {
    if (!initialized) {
        return;
    }
    
#ifdef ULTRACANVAS_WITH_ZWAVE
    // Stop notification processing
    stopProcessing = true;
    if (processingThread.joinable()) {
        processingThread.join();
    }
    
    // Remove driver
    if (homeId != 0) {
        OpenZWave::Manager::Get()->RemoveDriver(controllerPath);
    }
    
    // Remove watcher
    OpenZWave::Manager::Get()->RemoveWatcher(OnNotification, this);
    
    // Destroy Manager
    OpenZWave::Manager::Destroy();
    
    // Destroy Options
    OpenZWave::Options::Destroy();
#endif
    
    std::lock_guard<std::mutex> lock(nodesMutex);
    nodes.clear();
    homeId = 0;
    controllerNodeId = 0;
    initialized = false;
    networkReady = false;
    
    std::cout << "[ZWave] Shutdown complete" << std::endl;
}

std::string ZWaveProtocol::GetVersion() const {
#ifdef ULTRACANVAS_WITH_ZWAVE
    return OpenZWave::Manager::Get()->getVersionAsString();
#else
    return "1.0.0-stub";
#endif
}

// ============================================================================
// DISCOVERY
// ============================================================================

bool ZWaveProtocol::StartDiscovery(int timeoutSeconds) {
    if (!initialized) {
        return false;
    }
    
    discovering = true;
    
    {
        std::lock_guard<std::mutex> lock(discoveredMutex);
        discoveredDevices.clear();
    }
    
    // Start inclusion mode for discovery
    StartInclusion(ZWaveInclusionMode::NetworkWide, true);
    
    // Schedule stop after timeout
    std::thread([this, timeoutSeconds]() {
        std::this_thread::sleep_for(std::chrono::seconds(timeoutSeconds));
        if (discovering) {
            StopDiscovery();
        }
    }).detach();
    
    return true;
}

void ZWaveProtocol::StopDiscovery() {
    if (!discovering) {
        return;
    }
    
    StopInclusion();
    discovering = false;
}

std::vector<SmartHomeDeviceInfo> ZWaveProtocol::GetDiscoveredDevices() const {
    std::lock_guard<std::mutex> lock(discoveredMutex);
    return discoveredDevices;
}

// ============================================================================
// PAIRING
// ============================================================================

bool ZWaveProtocol::PairDevice(const std::string& deviceId,
                               const std::map<std::string, std::string>& params) {
    if (!initialized) {
        return false;
    }
    
    // For Z-Wave, pairing happens through inclusion mode
    // The deviceId here might be a temporary ID from discovery
    
    bool secure = true;
    ZWaveInclusionMode mode = ZWaveInclusionMode::Normal;
    
    if (params.count("secure")) {
        secure = (params.at("secure") == "true" || params.at("secure") == "1");
    }
    if (params.count("mode")) {
        if (params.at("mode") == "nwi") {
            mode = ZWaveInclusionMode::NetworkWide;
        } else if (params.at("mode") == "lowpower") {
            mode = ZWaveInclusionMode::LowPower;
        }
    }
    
    return StartInclusion(mode, secure);
}

bool ZWaveProtocol::UnpairDevice(const std::string& deviceId) {
    if (!initialized) {
        return false;
    }
    
    uint8_t nodeId = ParseNodeId(deviceId);
    if (nodeId == 0) {
        // Start exclusion mode if no specific device
        return StartExclusion();
    }
    
    // Try to remove specific node if it's failed
    if (IsNodeFailed(nodeId)) {
        return RemoveFailedNode(nodeId);
    }
    
    // Otherwise start exclusion and wait for user action
    return StartExclusion();
}

// ============================================================================
// COMMANDS
// ============================================================================

bool ZWaveProtocol::SendCommand(const std::string& deviceId, const std::string& command,
                                const std::map<std::string, std::string>& params) {
    if (!initialized) {
        return false;
    }
    
    uint8_t nodeId = ParseNodeId(deviceId);
    if (nodeId == 0) {
        return false;
    }
    
    // Parse instance from params
    uint8_t instance = 1;
    if (params.count("instance")) {
        instance = static_cast<uint8_t>(std::stoi(params.at("instance")));
    }
    
    // Handle common commands
    if (command == "on" || command == "turnOn") {
        return SwitchOn(nodeId, instance);
    }
    else if (command == "off" || command == "turnOff") {
        return SwitchOff(nodeId, instance);
    }
    else if (command == "toggle") {
        // Get current state and toggle
        const ZWaveValue* value = GetValue(nodeId, ZWaveCommandClass::SwitchBinary, instance, 0);
        if (value) {
            return value->BoolValue ? SwitchOff(nodeId, instance) : SwitchOn(nodeId, instance);
        }
        return false;
    }
    else if (command == "setLevel" || command == "setBrightness") {
        uint8_t level = 99;
        if (params.count("level")) {
            level = static_cast<uint8_t>(std::stoi(params.at("level")));
        } else if (params.count("brightness")) {
            level = static_cast<uint8_t>(std::stoi(params.at("brightness")));
        }
        return SetLevel(nodeId, level, instance);
    }
    else if (command == "setColor") {
        uint8_t r = 255, g = 255, b = 255, w = 0;
        if (params.count("red")) r = static_cast<uint8_t>(std::stoi(params.at("red")));
        if (params.count("green")) g = static_cast<uint8_t>(std::stoi(params.at("green")));
        if (params.count("blue")) b = static_cast<uint8_t>(std::stoi(params.at("blue")));
        if (params.count("white")) w = static_cast<uint8_t>(std::stoi(params.at("white")));
        return SetColor(nodeId, r, g, b, w);
    }
    else if (command == "lock") {
        return LockDoor(nodeId);
    }
    else if (command == "unlock") {
        return UnlockDoor(nodeId);
    }
    else if (command == "setThermostatMode") {
        if (params.count("mode")) {
            return SetThermostatMode(nodeId, params.at("mode"));
        }
        return false;
    }
    else if (command == "setTargetTemp" || command == "setSetpoint") {
        float temp = 21.0f;
        uint8_t setpointType = 1;
        if (params.count("temperature")) {
            temp = std::stof(params.at("temperature"));
        }
        if (params.count("type")) {
            setpointType = static_cast<uint8_t>(std::stoi(params.at("type")));
        }
        return SetThermostatSetpoint(nodeId, temp, setpointType);
    }
    else if (command == "setFanMode") {
        if (params.count("mode")) {
            return SetThermostatFanMode(nodeId, params.at("mode"));
        }
        return false;
    }
    else if (command == "refresh") {
        return RefreshNodeInfo(nodeId);
    }
    else if (command == "heal") {
        return HealNode(nodeId, true);
    }
    else if (command == "setConfig") {
        if (params.count("param") && params.count("value")) {
            uint8_t paramId = static_cast<uint8_t>(std::stoi(params.at("param")));
            int32_t value = std::stoi(params.at("value"));
            uint8_t size = 4;
            if (params.count("size")) {
                size = static_cast<uint8_t>(std::stoi(params.at("size")));
            }
            return SetConfigParam(nodeId, paramId, value, size);
        }
        return false;
    }
    else if (command == "wakeup") {
        // Send No Operation to wake device
#ifdef ULTRACANVAS_WITH_ZWAVE
        OpenZWave::Manager::Get()->TestNetworkNode(homeId, nodeId, 1);
#endif
        return true;
    }
    
    std::cerr << "[ZWave] Unknown command: " << command << std::endl;
    return false;
}

bool ZWaveProtocol::GetDeviceState(const std::string& deviceId,
                                   std::map<std::string, std::string>& state) {
    if (!initialized) {
        return false;
    }
    
    uint8_t nodeId = ParseNodeId(deviceId);
    if (nodeId == 0) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(nodesMutex);
    auto it = nodes.find(nodeId);
    if (it == nodes.end()) {
        return false;
    }
    
    const ZWaveNode& node = it->second;
    
    // Basic state
    state["nodeId"] = std::to_string(nodeId);
    state["online"] = node.IsReady && !node.IsFailed ? "true" : "false";
    state["failed"] = node.IsFailed ? "true" : "false";
    state["awake"] = node.IsAwake ? "true" : "false";
    state["listening"] = node.IsListening ? "true" : "false";
    
    // Get values from command classes
    for (const auto& [valueId, value] : node.Values) {
        std::string key;
        
        switch (value.CommandClass) {
            case ZWaveCommandClass::SwitchBinary:
                if (value.Index == 0) {
                    state["on"] = value.BoolValue ? "true" : "false";
                }
                break;
                
            case ZWaveCommandClass::SwitchMultilevel:
                if (value.Index == 0) {
                    state["level"] = std::to_string(value.ByteValue);
                    state["brightness"] = std::to_string(value.ByteValue);
                }
                break;
                
            case ZWaveCommandClass::SwitchColor:
                // Color values have different indices for R, G, B, W
                if (value.Label == "Red") state["red"] = std::to_string(value.ByteValue);
                else if (value.Label == "Green") state["green"] = std::to_string(value.ByteValue);
                else if (value.Label == "Blue") state["blue"] = std::to_string(value.ByteValue);
                else if (value.Label == "White") state["white"] = std::to_string(value.ByteValue);
                break;
                
            case ZWaveCommandClass::DoorLock:
                if (value.Index == 0) {
                    state["locked"] = value.BoolValue ? "true" : "false";
                }
                break;
                
            case ZWaveCommandClass::ThermostatMode:
                if (value.Type == ZWaveValueType::List) {
                    if (!value.ListItems.empty() && value.ListSelection >= 0 &&
                        value.ListSelection < static_cast<int>(value.ListItems.size())) {
                        state["thermostatMode"] = value.ListItems[value.ListSelection];
                    }
                }
                break;
                
            case ZWaveCommandClass::ThermostatSetpoint:
                state["setpoint_" + std::to_string(value.Index)] = 
                    std::to_string(value.DecimalValue);
                if (value.Index == 1) {
                    state["targetTemp"] = std::to_string(value.DecimalValue);
                }
                break;
                
            case ZWaveCommandClass::ThermostatOperatingState:
                if (value.Type == ZWaveValueType::List) {
                    if (!value.ListItems.empty() && value.ListSelection >= 0) {
                        state["operatingState"] = value.ListItems[value.ListSelection];
                    }
                }
                break;
                
            case ZWaveCommandClass::SensorMultilevel:
                // Use label as key
                state["sensor_" + value.Label] = std::to_string(value.DecimalValue);
                if (value.Label == "Temperature") {
                    state["currentTemp"] = std::to_string(value.DecimalValue);
                } else if (value.Label == "Relative Humidity") {
                    state["humidity"] = std::to_string(static_cast<int>(value.DecimalValue));
                } else if (value.Label == "Luminance") {
                    state["luminance"] = std::to_string(value.DecimalValue);
                } else if (value.Label == "Power") {
                    state["power"] = std::to_string(value.DecimalValue);
                }
                break;
                
            case ZWaveCommandClass::SensorBinary:
                state["sensor_" + value.Label] = value.BoolValue ? "detected" : "clear";
                if (value.Label == "Motion") {
                    state["motion"] = value.BoolValue ? "detected" : "clear";
                } else if (value.Label == "Door/Window") {
                    state["contact"] = value.BoolValue ? "open" : "closed";
                }
                break;
                
            case ZWaveCommandClass::Battery:
                if (value.Index == 0) {
                    state["battery"] = std::to_string(value.ByteValue);
                }
                break;
                
            case ZWaveCommandClass::Meter:
                state["meter_" + value.Label] = std::to_string(value.DecimalValue);
                if (value.Label == "Energy") {
                    state["energy"] = std::to_string(value.DecimalValue);
                } else if (value.Label == "Power") {
                    state["power"] = std::to_string(value.DecimalValue);
                }
                break;
                
            case ZWaveCommandClass::Alarm:
                state["alarm_" + std::to_string(value.Index)] = 
                    value.BoolValue ? "active" : "clear";
                break;
                
            default:
                break;
        }
    }
    
    return true;
}

std::vector<SmartHomeDeviceInfo> ZWaveProtocol::GetPairedDevices() {
    std::vector<SmartHomeDeviceInfo> devices;
    
    std::lock_guard<std::mutex> lock(nodesMutex);
    for (const auto& [nodeId, node] : nodes) {
        // Skip controller node
        if (nodeId == controllerNodeId) {
            continue;
        }
        
        devices.push_back(ConvertToDeviceInfo(node));
    }
    
    return devices;
}

// ============================================================================
// CONTROLLER MANAGEMENT
// ============================================================================

void ZWaveProtocol::SetControllerPath(const std::string& path) {
    if (!initialized) {
        controllerPath = path;
    }
}

void ZWaveProtocol::SetConfigPath(const std::string& path) {
    if (!initialized) {
        configPath = path;
    }
}

void ZWaveProtocol::SetUserPath(const std::string& path) {
    if (!initialized) {
        userPath = path;
    }
}

bool ZWaveProtocol::IsPrimaryController() const {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    return OpenZWave::Manager::Get()->IsPrimaryController(homeId);
#else
    return true;
#endif
}

bool ZWaveProtocol::IsStaticUpdateController() const {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    return OpenZWave::Manager::Get()->IsStaticUpdateController(homeId);
#else
    return true;
#endif
}

bool ZWaveProtocol::SoftResetController() {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    OpenZWave::Manager::Get()->SoftReset(homeId);
    return true;
#else
    return true;
#endif
}

bool ZWaveProtocol::HardResetController() {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    OpenZWave::Manager::Get()->ResetController(homeId);
    
    // Clear local state
    std::lock_guard<std::mutex> lock(nodesMutex);
    nodes.clear();
    
    return true;
#else
    return true;
#endif
}

// ============================================================================
// NODE MANAGEMENT
// ============================================================================

bool ZWaveProtocol::StartInclusion(ZWaveInclusionMode mode, bool secure) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    
    bool doSecurity = secure;
    
    // OpenZWave uses AddNode for inclusion
    return OpenZWave::Manager::Get()->AddNode(homeId, doSecurity);
#else
    // Stub: simulate finding a device after 2 seconds
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        if (discovering || controllerState == ZWaveControllerState::InProgress) {
            // Create a simulated node
            ZWaveNode node;
            node.HomeId = homeId;
            node.NodeId = 2;
            node.Name = "Simulated Switch";
            node.ManufacturerName = "UltraCanvas";
            node.ProductName = "Virtual Switch";
            node.IsReady = true;
            node.IsListening = true;
            node.CommandClasses.insert(ZWaveCommandClass::SwitchBinary);
            
            {
                std::lock_guard<std::mutex> lock(nodesMutex);
                nodes[node.NodeId] = node;
            }
            
            NotifyNodeAdded(node);
            
            if (onInclusionResult) {
                onInclusionResult(true, node.NodeId, "Device added successfully");
            }
        }
    }).detach();
    
    controllerState = ZWaveControllerState::InProgress;
    return true;
#endif
}

bool ZWaveProtocol::StopInclusion() {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    return OpenZWave::Manager::Get()->CancelControllerCommand(homeId);
#else
    controllerState = ZWaveControllerState::Normal;
    return true;
#endif
}

bool ZWaveProtocol::StartExclusion() {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    return OpenZWave::Manager::Get()->RemoveNode(homeId);
#else
    controllerState = ZWaveControllerState::InProgress;
    return true;
#endif
}

bool ZWaveProtocol::StopExclusion() {
    return StopInclusion();  // Same cancel command
}

bool ZWaveProtocol::IsNodeFailed(uint8_t nodeId) const {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    return OpenZWave::Manager::Get()->IsNodeFailed(homeId, nodeId);
#else
    std::lock_guard<std::mutex> lock(nodesMutex);
    auto it = nodes.find(nodeId);
    return it != nodes.end() && it->second.IsFailed;
#endif
}

bool ZWaveProtocol::RemoveFailedNode(uint8_t nodeId) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    return OpenZWave::Manager::Get()->RemoveFailedNode(homeId, nodeId);
#else
    std::lock_guard<std::mutex> lock(nodesMutex);
    nodes.erase(nodeId);
    return true;
#endif
}

bool ZWaveProtocol::ReplaceFailedNode(uint8_t nodeId) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    return OpenZWave::Manager::Get()->ReplaceFailedNode(homeId, nodeId);
#else
    return true;
#endif
}

bool ZWaveProtocol::RequestNodeNeighborUpdate(uint8_t nodeId) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    return OpenZWave::Manager::Get()->RequestNodeNeighborUpdate(homeId, nodeId);
#else
    return true;
#endif
}

bool ZWaveProtocol::RefreshNodeInfo(uint8_t nodeId) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    return OpenZWave::Manager::Get()->RefreshNodeInfo(homeId, nodeId);
#else
    return true;
#endif
}

bool ZWaveProtocol::RequestAllConfigParams(uint8_t nodeId) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    return OpenZWave::Manager::Get()->RequestAllConfigParams(homeId, nodeId);
#else
    return true;
#endif
}

const ZWaveNode* ZWaveProtocol::GetNode(uint8_t nodeId) const {
    std::lock_guard<std::mutex> lock(nodesMutex);
    auto it = nodes.find(nodeId);
    if (it != nodes.end()) {
        return &it->second;
    }
    return nullptr;
}

std::map<uint8_t, ZWaveNode> ZWaveProtocol::GetAllNodes() const {
    std::lock_guard<std::mutex> lock(nodesMutex);
    return nodes;
}

bool ZWaveProtocol::SetNodeName(uint8_t nodeId, const std::string& name) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    OpenZWave::Manager::Get()->SetNodeName(homeId, nodeId, name);
#endif
    
    std::lock_guard<std::mutex> lock(nodesMutex);
    auto it = nodes.find(nodeId);
    if (it != nodes.end()) {
        it->second.Name = name;
        return true;
    }
    return false;
}

bool ZWaveProtocol::SetNodeLocation(uint8_t nodeId, const std::string& location) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    OpenZWave::Manager::Get()->SetNodeLocation(homeId, nodeId, location);
#endif
    
    std::lock_guard<std::mutex> lock(nodesMutex);
    auto it = nodes.find(nodeId);
    if (it != nodes.end()) {
        it->second.Location = location;
        return true;
    }
    return false;
}

// ============================================================================
// VALUE OPERATIONS
// ============================================================================

const ZWaveValue* ZWaveProtocol::GetValue(uint8_t nodeId, ZWaveCommandClass commandClass,
                                          uint8_t instance, uint16_t index) const {
    std::lock_guard<std::mutex> lock(nodesMutex);
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) {
        return nullptr;
    }
    
    for (const auto& [valueId, value] : nodeIt->second.Values) {
        if (value.CommandClass == commandClass &&
            value.Instance == instance &&
            value.Index == index) {
            return &value;
        }
    }
    
    return nullptr;
}

bool ZWaveProtocol::SetValueBool(uint8_t nodeId, ZWaveCommandClass commandClass,
                                 uint8_t instance, uint16_t index, bool value) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    OpenZWave::ValueID valueId;
    if (!FindValueId(nodeId, commandClass, instance, index, valueId)) {
        return false;
    }
    return OpenZWave::Manager::Get()->SetValue(valueId, value);
#else
    std::lock_guard<std::mutex> lock(nodesMutex);
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) return false;
    
    for (auto& [vid, val] : nodeIt->second.Values) {
        if (val.CommandClass == commandClass && val.Instance == instance && val.Index == index) {
            val.BoolValue = value;
            NotifyValueChanged(val);
            return true;
        }
    }
    return false;
#endif
}

bool ZWaveProtocol::SetValueByte(uint8_t nodeId, ZWaveCommandClass commandClass,
                                 uint8_t instance, uint16_t index, uint8_t value) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    OpenZWave::ValueID valueId;
    if (!FindValueId(nodeId, commandClass, instance, index, valueId)) {
        return false;
    }
    return OpenZWave::Manager::Get()->SetValue(valueId, value);
#else
    std::lock_guard<std::mutex> lock(nodesMutex);
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) return false;
    
    for (auto& [vid, val] : nodeIt->second.Values) {
        if (val.CommandClass == commandClass && val.Instance == instance && val.Index == index) {
            val.ByteValue = value;
            NotifyValueChanged(val);
            return true;
        }
    }
    return false;
#endif
}

bool ZWaveProtocol::SetValueInt(uint8_t nodeId, ZWaveCommandClass commandClass,
                                uint8_t instance, uint16_t index, int32_t value) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    OpenZWave::ValueID valueId;
    if (!FindValueId(nodeId, commandClass, instance, index, valueId)) {
        return false;
    }
    return OpenZWave::Manager::Get()->SetValue(valueId, value);
#else
    return false;
#endif
}

bool ZWaveProtocol::SetValueDecimal(uint8_t nodeId, ZWaveCommandClass commandClass,
                                    uint8_t instance, uint16_t index, float value) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    OpenZWave::ValueID valueId;
    if (!FindValueId(nodeId, commandClass, instance, index, valueId)) {
        return false;
    }
    return OpenZWave::Manager::Get()->SetValue(valueId, value);
#else
    std::lock_guard<std::mutex> lock(nodesMutex);
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) return false;
    
    for (auto& [vid, val] : nodeIt->second.Values) {
        if (val.CommandClass == commandClass && val.Instance == instance && val.Index == index) {
            val.DecimalValue = value;
            NotifyValueChanged(val);
            return true;
        }
    }
    return false;
#endif
}

bool ZWaveProtocol::SetValueList(uint8_t nodeId, ZWaveCommandClass commandClass,
                                 uint8_t instance, uint16_t index, const std::string& selection) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    OpenZWave::ValueID valueId;
    if (!FindValueId(nodeId, commandClass, instance, index, valueId)) {
        return false;
    }
    return OpenZWave::Manager::Get()->SetValueListSelection(valueId, selection);
#else
    return false;
#endif
}

bool ZWaveProtocol::PressButton(uint8_t nodeId, ZWaveCommandClass commandClass,
                                uint8_t instance, uint16_t index) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    OpenZWave::ValueID valueId;
    if (!FindValueId(nodeId, commandClass, instance, index, valueId)) {
        return false;
    }
    return OpenZWave::Manager::Get()->PressButton(valueId);
#else
    return true;
#endif
}

bool ZWaveProtocol::ReleaseButton(uint8_t nodeId, ZWaveCommandClass commandClass,
                                  uint8_t instance, uint16_t index) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    OpenZWave::ValueID valueId;
    if (!FindValueId(nodeId, commandClass, instance, index, valueId)) {
        return false;
    }
    return OpenZWave::Manager::Get()->ReleaseButton(valueId);
#else
    return true;
#endif
}

bool ZWaveProtocol::RefreshValue(uint8_t nodeId, ZWaveCommandClass commandClass,
                                 uint8_t instance, uint16_t index) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    OpenZWave::ValueID valueId;
    if (!FindValueId(nodeId, commandClass, instance, index, valueId)) {
        return false;
    }
    return OpenZWave::Manager::Get()->RefreshValue(valueId);
#else
    return true;
#endif
}

// ============================================================================
// DEVICE COMMANDS
// ============================================================================

bool ZWaveProtocol::SwitchOn(uint8_t nodeId, uint8_t instance) {
    return SetValueBool(nodeId, ZWaveCommandClass::SwitchBinary, instance, 0, true);
}

bool ZWaveProtocol::SwitchOff(uint8_t nodeId, uint8_t instance) {
    return SetValueBool(nodeId, ZWaveCommandClass::SwitchBinary, instance, 0, false);
}

bool ZWaveProtocol::SetLevel(uint8_t nodeId, uint8_t level, uint8_t instance) {
    // Level 0-99, with 255 meaning "restore last value"
    if (level > 99 && level != 255) {
        level = 99;
    }
    return SetValueByte(nodeId, ZWaveCommandClass::SwitchMultilevel, instance, 0, level);
}

bool ZWaveProtocol::SetThermostatSetpoint(uint8_t nodeId, float temperature, uint8_t setpointType) {
    return SetValueDecimal(nodeId, ZWaveCommandClass::ThermostatSetpoint, 1, setpointType, temperature);
}

bool ZWaveProtocol::SetThermostatMode(uint8_t nodeId, const std::string& mode) {
    return SetValueList(nodeId, ZWaveCommandClass::ThermostatMode, 1, 0, mode);
}

bool ZWaveProtocol::SetThermostatFanMode(uint8_t nodeId, const std::string& mode) {
    return SetValueList(nodeId, ZWaveCommandClass::ThermostatFanMode, 1, 0, mode);
}

bool ZWaveProtocol::LockDoor(uint8_t nodeId) {
    return SetValueBool(nodeId, ZWaveCommandClass::DoorLock, 1, 0, true);
}

bool ZWaveProtocol::UnlockDoor(uint8_t nodeId) {
    return SetValueBool(nodeId, ZWaveCommandClass::DoorLock, 1, 0, false);
}

bool ZWaveProtocol::SetColor(uint8_t nodeId, uint8_t red, uint8_t green,
                             uint8_t blue, uint8_t white) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    
    // Color values are typically at indices 2(Red), 3(Green), 4(Blue), 0(Warm White), 1(Cold White)
    // The exact indices depend on the device
    bool success = true;
    
    // Try to set each color component
    // Note: Actual implementation depends on device's value structure
    std::lock_guard<std::mutex> lock(nodesMutex);
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) return false;
    
    for (const auto& [valueId, value] : nodeIt->second.Values) {
        if (value.CommandClass == ZWaveCommandClass::SwitchColor) {
            OpenZWave::ValueID ozValueId;
            if (FindValueId(nodeId, ZWaveCommandClass::SwitchColor, value.Instance, value.Index, ozValueId)) {
                if (value.Label == "Red") {
                    success &= OpenZWave::Manager::Get()->SetValue(ozValueId, red);
                } else if (value.Label == "Green") {
                    success &= OpenZWave::Manager::Get()->SetValue(ozValueId, green);
                } else if (value.Label == "Blue") {
                    success &= OpenZWave::Manager::Get()->SetValue(ozValueId, blue);
                } else if (value.Label == "Warm White" || value.Label == "White") {
                    success &= OpenZWave::Manager::Get()->SetValue(ozValueId, white);
                }
            }
        }
    }
    
    return success;
#else
    return true;
#endif
}

// ============================================================================
// ASSOCIATION GROUPS
// ============================================================================

std::vector<ZWaveAssociationGroup> ZWaveProtocol::GetAssociationGroups(uint8_t nodeId) const {
    std::vector<ZWaveAssociationGroup> groups;
    
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return groups;
    
    uint8_t numGroups = OpenZWave::Manager::Get()->GetNumGroups(homeId, nodeId);
    
    for (uint8_t i = 1; i <= numGroups; i++) {
        ZWaveAssociationGroup group;
        group.GroupIndex = i;
        group.Label = OpenZWave::Manager::Get()->GetGroupLabel(homeId, nodeId, i);
        group.MaxAssociations = OpenZWave::Manager::Get()->GetMaxAssociations(homeId, nodeId, i);
        
        // Get current members
        uint8_t* associations;
        uint32_t numAssociations;
        OpenZWave::Manager::Get()->GetAssociations(homeId, nodeId, i, &associations);
        // Note: OpenZWave API returns count differently - check actual implementation
        
        groups.push_back(group);
    }
#endif
    
    return groups;
}

bool ZWaveProtocol::AddAssociation(uint8_t nodeId, uint8_t groupIndex, uint8_t targetNodeId) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    OpenZWave::Manager::Get()->AddAssociation(homeId, nodeId, groupIndex, targetNodeId);
    return true;
#else
    return true;
#endif
}

bool ZWaveProtocol::RemoveAssociation(uint8_t nodeId, uint8_t groupIndex, uint8_t targetNodeId) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    OpenZWave::Manager::Get()->RemoveAssociation(homeId, nodeId, groupIndex, targetNodeId);
    return true;
#else
    return true;
#endif
}

// ============================================================================
// CONFIGURATION
// ============================================================================

bool ZWaveProtocol::SetConfigParam(uint8_t nodeId, uint8_t paramId, int32_t value, uint8_t size) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    return OpenZWave::Manager::Get()->SetConfigParam(homeId, nodeId, paramId, value, size);
#else
    return true;
#endif
}

bool ZWaveProtocol::RequestConfigParam(uint8_t nodeId, uint8_t paramId) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    OpenZWave::Manager::Get()->RequestConfigParam(homeId, nodeId, paramId);
    return true;
#else
    return true;
#endif
}

// ============================================================================
// NETWORK OPERATIONS
// ============================================================================

bool ZWaveProtocol::HealNetwork(bool doReturnRoutes) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    OpenZWave::Manager::Get()->HealNetwork(homeId, doReturnRoutes);
    return true;
#else
    return true;
#endif
}

bool ZWaveProtocol::HealNode(uint8_t nodeId, bool doReturnRoutes) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    OpenZWave::Manager::Get()->HealNetworkNode(homeId, nodeId, doReturnRoutes);
    return true;
#else
    return true;
#endif
}

ZWaveNetworkStats ZWaveProtocol::GetNetworkStats() const {
    ZWaveNetworkStats stats;
    stats.HomeId = homeId;
    stats.ControllerNodeId = controllerNodeId;
    
    std::lock_guard<std::mutex> lock(nodesMutex);
    stats.TotalNodes = static_cast<int>(nodes.size());
    
    for (const auto& [nodeId, node] : nodes) {
        if (node.IsFailed) {
            stats.FailedNodes++;
        } else if (!node.IsListening && !node.IsAwake) {
            stats.SleepingNodes++;
        } else {
            stats.ActiveNodes++;
        }
    }
    
#ifdef ULTRACANVAS_WITH_ZWAVE
    // Get driver statistics if available
    // OpenZWave::Manager::Get()->GetDriverStatistics(homeId, ...);
#endif
    
    return stats;
}

bool ZWaveProtocol::TestNetwork(uint8_t nodeId, uint8_t count) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    if (!initialized || homeId == 0) return false;
    
    if (nodeId == 0) {
        // Test all nodes
        OpenZWave::Manager::Get()->TestNetwork(homeId, count);
    } else {
        OpenZWave::Manager::Get()->TestNetworkNode(homeId, nodeId, count);
    }
    return true;
#else
    return true;
#endif
}

// ============================================================================
// SCENES
// ============================================================================

uint8_t ZWaveProtocol::CreateScene(const std::string& label) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    uint8_t sceneId = OpenZWave::Manager::Get()->CreateScene();
    if (sceneId > 0) {
        OpenZWave::Manager::Get()->SetSceneLabel(sceneId, label);
    }
    return sceneId;
#else
    static uint8_t nextSceneId = 1;
    return nextSceneId++;
#endif
}

bool ZWaveProtocol::DeleteScene(uint8_t sceneId) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    return OpenZWave::Manager::Get()->RemoveScene(sceneId);
#else
    return true;
#endif
}

bool ZWaveProtocol::ActivateScene(uint8_t sceneId) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    return OpenZWave::Manager::Get()->ActivateScene(sceneId);
#else
    return true;
#endif
}

std::vector<ZWaveScene> ZWaveProtocol::GetScenes() const {
    std::vector<ZWaveScene> scenes;
    
#ifdef ULTRACANVAS_WITH_ZWAVE
    uint8_t numScenes = OpenZWave::Manager::Get()->GetNumScenes();
    uint8_t* sceneIds = new uint8_t[numScenes];
    OpenZWave::Manager::Get()->GetAllScenes(&sceneIds);
    
    for (uint8_t i = 0; i < numScenes; i++) {
        ZWaveScene scene;
        scene.SceneId = sceneIds[i];
        scene.Label = OpenZWave::Manager::Get()->GetSceneLabel(sceneIds[i]);
        scenes.push_back(scene);
    }
    
    delete[] sceneIds;
#endif
    
    return scenes;
}

bool ZWaveProtocol::AddValueToScene(uint8_t sceneId, const ZWaveValue& value) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    OpenZWave::ValueID valueId;
    if (!FindValueId(value.NodeId, value.CommandClass, value.Instance, value.Index, valueId)) {
        return false;
    }
    
    switch (value.Type) {
        case ZWaveValueType::Bool:
            return OpenZWave::Manager::Get()->AddSceneValueBool(sceneId, valueId, value.BoolValue);
        case ZWaveValueType::Byte:
            return OpenZWave::Manager::Get()->AddSceneValueByte(sceneId, valueId, value.ByteValue);
        case ZWaveValueType::Int:
            return OpenZWave::Manager::Get()->AddSceneValueInt(sceneId, valueId, value.IntValue);
        case ZWaveValueType::Decimal:
            return OpenZWave::Manager::Get()->AddSceneValueFloat(sceneId, valueId, value.DecimalValue);
        default:
            return false;
    }
#else
    return true;
#endif
}

bool ZWaveProtocol::RemoveValueFromScene(uint8_t sceneId, uint64_t valueId) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    // Would need to reconstruct ValueID from our stored ID
    // This is a simplified implementation
    return false;
#else
    return true;
#endif
}

// ============================================================================
// POLLING
// ============================================================================

bool ZWaveProtocol::EnablePoll(uint8_t nodeId, ZWaveCommandClass commandClass,
                               uint8_t instance, uint16_t index, uint8_t intensity) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    OpenZWave::ValueID valueId;
    if (!FindValueId(nodeId, commandClass, instance, index, valueId)) {
        return false;
    }
    return OpenZWave::Manager::Get()->EnablePoll(valueId, intensity);
#else
    return true;
#endif
}

bool ZWaveProtocol::DisablePoll(uint8_t nodeId, ZWaveCommandClass commandClass,
                                uint8_t instance, uint16_t index) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    OpenZWave::ValueID valueId;
    if (!FindValueId(nodeId, commandClass, instance, index, valueId)) {
        return false;
    }
    return OpenZWave::Manager::Get()->DisablePoll(valueId);
#else
    return true;
#endif
}

void ZWaveProtocol::SetPollInterval(int milliseconds) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    OpenZWave::Manager::Get()->SetPollInterval(milliseconds, true);
#endif
}

int ZWaveProtocol::GetPollInterval() const {
#ifdef ULTRACANVAS_WITH_ZWAVE
    return OpenZWave::Manager::Get()->GetPollInterval();
#else
    return 500;
#endif
}

// ============================================================================
// WAKE-UP
// ============================================================================

bool ZWaveProtocol::SetWakeUpInterval(uint8_t nodeId, uint32_t seconds) {
    // Wake-up interval is typically at index 0 of WakeUp command class
#ifdef ULTRACANVAS_WITH_ZWAVE
    OpenZWave::ValueID valueId;
    if (!FindValueId(nodeId, ZWaveCommandClass::WakeUp, 1, 0, valueId)) {
        return false;
    }
    return OpenZWave::Manager::Get()->SetValue(valueId, static_cast<int32_t>(seconds));
#else
    return true;
#endif
}

uint32_t ZWaveProtocol::GetWakeUpInterval(uint8_t nodeId) const {
    const ZWaveValue* value = GetValue(nodeId, ZWaveCommandClass::WakeUp, 1, 0);
    if (value) {
        return static_cast<uint32_t>(value->IntValue);
    }
    return 0;
}

// ============================================================================
// OPENZWAVE NOTIFICATION HANDLER
// ============================================================================

#ifdef ULTRACANVAS_WITH_ZWAVE

void ZWaveProtocol::OnNotification(const OpenZWave::Notification* notification, void* context) {
    ZWaveProtocol* protocol = static_cast<ZWaveProtocol*>(context);
    if (protocol) {
        protocol->ProcessNotification(notification);
    }
}

void ZWaveProtocol::ProcessNotification(const OpenZWave::Notification* notification) {
    switch (notification->GetType()) {
        case OpenZWave::Notification::Type_ValueAdded:
            HandleValueAdded(notification);
            break;
            
        case OpenZWave::Notification::Type_ValueRemoved:
            HandleValueRemoved(notification);
            break;
            
        case OpenZWave::Notification::Type_ValueChanged:
        case OpenZWave::Notification::Type_ValueRefreshed:
            HandleValueChanged(notification);
            break;
            
        case OpenZWave::Notification::Type_NodeAdded:
            HandleNodeAdded(notification);
            break;
            
        case OpenZWave::Notification::Type_NodeRemoved:
            HandleNodeRemoved(notification);
            break;
            
        case OpenZWave::Notification::Type_NodeEvent:
            HandleNodeEvent(notification);
            break;
            
        case OpenZWave::Notification::Type_DriverReady:
            HandleDriverReady(notification);
            break;
            
        case OpenZWave::Notification::Type_DriverFailed:
            HandleDriverFailed(notification);
            break;
            
        case OpenZWave::Notification::Type_AllNodesQueried:
        case OpenZWave::Notification::Type_AllNodesQueriedSomeDead:
        case OpenZWave::Notification::Type_AwakeNodesQueried:
            HandleAllNodesQueried(notification);
            break;
            
        case OpenZWave::Notification::Type_ControllerCommand:
            HandleControllerCommand(notification);
            break;
            
        case OpenZWave::Notification::Type_NodeNaming:
        case OpenZWave::Notification::Type_NodeProtocolInfo:
        case OpenZWave::Notification::Type_EssentialNodeQueriesComplete:
        case OpenZWave::Notification::Type_NodeQueriesComplete:
            // Update node info
            {
                uint8_t nodeId = notification->GetNodeId();
                std::lock_guard<std::mutex> lock(nodesMutex);
                auto it = nodes.find(nodeId);
                if (it != nodes.end()) {
                    UpdateNodeInfo(it->second);
                    
                    if (notification->GetType() == OpenZWave::Notification::Type_NodeQueriesComplete) {
                        it->second.IsReady = true;
                        if (onNodeReady) {
                            onNodeReady(it->second);
                        }
                    }
                }
            }
            break;
            
        default:
            break;
    }
    
    // Notify generic callback
    if (onNotification) {
        ZWaveNotificationType type;
        switch (notification->GetType()) {
            case OpenZWave::Notification::Type_ValueAdded: type = ZWaveNotificationType::ValueAdded; break;
            case OpenZWave::Notification::Type_ValueRemoved: type = ZWaveNotificationType::ValueRemoved; break;
            case OpenZWave::Notification::Type_ValueChanged: type = ZWaveNotificationType::ValueChanged; break;
            case OpenZWave::Notification::Type_NodeAdded: type = ZWaveNotificationType::NodeAdded; break;
            case OpenZWave::Notification::Type_NodeRemoved: type = ZWaveNotificationType::NodeRemoved; break;
            default: type = ZWaveNotificationType::Notification; break;
        }
        
        const ZWaveNode* node = GetNode(notification->GetNodeId());
        onNotification(type, node, nullptr);
    }
}

void ZWaveProtocol::HandleValueAdded(const OpenZWave::Notification* notification) {
    uint8_t nodeId = notification->GetNodeId();
    OpenZWave::ValueID valueId = notification->GetValueID();
    
    ZWaveValue value = ConvertValue(valueId);
    
    std::lock_guard<std::mutex> lock(nodesMutex);
    auto it = nodes.find(nodeId);
    if (it != nodes.end()) {
        it->second.Values[value.GetId()] = value;
        it->second.CommandClasses.insert(value.CommandClass);
    }
}

void ZWaveProtocol::HandleValueRemoved(const OpenZWave::Notification* notification) {
    uint8_t nodeId = notification->GetNodeId();
    OpenZWave::ValueID valueId = notification->GetValueID();
    
    ZWaveValue value = ConvertValue(valueId);
    
    std::lock_guard<std::mutex> lock(nodesMutex);
    auto it = nodes.find(nodeId);
    if (it != nodes.end()) {
        it->second.Values.erase(value.GetId());
    }
}

void ZWaveProtocol::HandleValueChanged(const OpenZWave::Notification* notification) {
    uint8_t nodeId = notification->GetNodeId();
    OpenZWave::ValueID valueId = notification->GetValueID();
    
    ZWaveValue value = ConvertValue(valueId);
    
    {
        std::lock_guard<std::mutex> lock(nodesMutex);
        auto it = nodes.find(nodeId);
        if (it != nodes.end()) {
            it->second.Values[value.GetId()] = value;
            it->second.LastSeen = std::chrono::system_clock::now();
        }
    }
    
    NotifyValueChanged(value);
}

void ZWaveProtocol::HandleNodeAdded(const OpenZWave::Notification* notification) {
    uint8_t nodeId = notification->GetNodeId();
    
    ZWaveNode node;
    node.HomeId = homeId;
    node.NodeId = nodeId;
    
    UpdateNodeInfo(node);
    
    {
        std::lock_guard<std::mutex> lock(nodesMutex);
        nodes[nodeId] = node;
    }
    
    NotifyNodeAdded(node);
    
    // Add to discovered devices
    {
        std::lock_guard<std::mutex> lock(discoveredMutex);
        discoveredDevices.push_back(ConvertToDeviceInfo(node));
    }
}

void ZWaveProtocol::HandleNodeRemoved(const OpenZWave::Notification* notification) {
    uint8_t nodeId = notification->GetNodeId();
    
    ZWaveNode removedNode;
    
    {
        std::lock_guard<std::mutex> lock(nodesMutex);
        auto it = nodes.find(nodeId);
        if (it != nodes.end()) {
            removedNode = it->second;
            nodes.erase(it);
        }
    }
    
    NotifyNodeRemoved(removedNode);
}

void ZWaveProtocol::HandleNodeEvent(const OpenZWave::Notification* notification) {
    uint8_t nodeId = notification->GetNodeId();
    uint8_t event = notification->GetEvent();
    
    // Node events are typically from Basic command class
    // Event value indicates the state change
    
    std::lock_guard<std::mutex> lock(nodesMutex);
    auto it = nodes.find(nodeId);
    if (it != nodes.end()) {
        it->second.LastSeen = std::chrono::system_clock::now();
        
        // For sleeping devices, this indicates wake-up
        if (!it->second.IsListening) {
            it->second.IsAwake = true;
            it->second.LastWakeUp = std::chrono::system_clock::now();
        }
    }
}

void ZWaveProtocol::HandleDriverReady(const OpenZWave::Notification* notification) {
    homeId = notification->GetHomeId();
    controllerNodeId = notification->GetNodeId();
    
    std::cout << "[ZWave] Driver ready - HomeId: 0x" << std::hex << homeId 
              << " Controller: " << std::dec << (int)controllerNodeId << std::endl;
    
    // Create controller node entry
    ZWaveNode controllerNode;
    controllerNode.HomeId = homeId;
    controllerNode.NodeId = controllerNodeId;
    controllerNode.NodeType = ZWaveNodeType::Controller;
    controllerNode.IsListening = true;
    controllerNode.IsReady = true;
    
    UpdateNodeInfo(controllerNode);
    
    {
        std::lock_guard<std::mutex> lock(nodesMutex);
        nodes[controllerNodeId] = controllerNode;
    }
    
    if (onControllerState) {
        onControllerState(ZWaveControllerState::Normal, "Driver ready");
    }
}

void ZWaveProtocol::HandleDriverFailed(const OpenZWave::Notification* notification) {
    std::cerr << "[ZWave] Driver failed!" << std::endl;
    
    if (onControllerState) {
        onControllerState(ZWaveControllerState::Failed, "Driver failed to initialize");
    }
}

void ZWaveProtocol::HandleAllNodesQueried(const OpenZWave::Notification* notification) {
    networkReady = true;
    
    std::cout << "[ZWave] All nodes queried - network ready" << std::endl;
    
    // Mark all nodes as ready
    {
        std::lock_guard<std::mutex> lock(nodesMutex);
        for (auto& [nodeId, node] : nodes) {
            if (!node.IsFailed) {
                node.IsReady = true;
            }
        }
    }
    
    if (onControllerState) {
        onControllerState(ZWaveControllerState::Completed, "Network ready");
    }
}

void ZWaveProtocol::HandleControllerCommand(const OpenZWave::Notification* notification) {
    auto state = notification->GetEvent();
    
    ZWaveControllerState newState;
    std::string message;
    
    switch (state) {
        case OpenZWave::Driver::ControllerState_Normal:
            newState = ZWaveControllerState::Normal;
            message = "Normal operation";
            break;
        case OpenZWave::Driver::ControllerState_Starting:
            newState = ZWaveControllerState::Starting;
            message = "Starting command";
            break;
        case OpenZWave::Driver::ControllerState_Cancel:
            newState = ZWaveControllerState::Cancel;
            message = "Command cancelled";
            break;
        case OpenZWave::Driver::ControllerState_Error:
            newState = ZWaveControllerState::Error;
            message = "Command error";
            break;
        case OpenZWave::Driver::ControllerState_Waiting:
            newState = ZWaveControllerState::Waiting;
            message = "Waiting for user action";
            break;
        case OpenZWave::Driver::ControllerState_Sleeping:
            newState = ZWaveControllerState::Sleeping;
            message = "Controller sleeping";
            break;
        case OpenZWave::Driver::ControllerState_InProgress:
            newState = ZWaveControllerState::InProgress;
            message = "Command in progress";
            break;
        case OpenZWave::Driver::ControllerState_Completed:
            newState = ZWaveControllerState::Completed;
            message = "Command completed";
            break;
        case OpenZWave::Driver::ControllerState_Failed:
            newState = ZWaveControllerState::Failed;
            message = "Command failed";
            break;
        case OpenZWave::Driver::ControllerState_NodeOK:
            newState = ZWaveControllerState::NodeOK;
            message = "Node OK";
            break;
        case OpenZWave::Driver::ControllerState_NodeFailed:
            newState = ZWaveControllerState::NodeFailed;
            message = "Node failed";
            break;
        default:
            newState = ZWaveControllerState::Normal;
            message = "Unknown state";
            break;
    }
    
    controllerState = newState;
    
    if (onControllerState) {
        onControllerState(newState, message);
    }
    
    // Handle inclusion result
    if (discovering || controllerState == ZWaveControllerState::InProgress) {
        if (newState == ZWaveControllerState::Completed) {
            // Node was added - get the node ID from notification
            uint8_t nodeId = notification->GetNodeId();
            if (onInclusionResult) {
                onInclusionResult(true, nodeId, "Device included successfully");
            }
        } else if (newState == ZWaveControllerState::Failed) {
            if (onInclusionResult) {
                onInclusionResult(false, 0, "Inclusion failed");
            }
        }
    }
}

ZWaveValue ZWaveProtocol::ConvertValue(const OpenZWave::ValueID& valueId) const {
    ZWaveValue value;
    
    value.HomeId = valueId.GetHomeId();
    value.NodeId = valueId.GetNodeId();
    value.CommandClass = static_cast<ZWaveCommandClass>(valueId.GetCommandClassId());
    value.Instance = valueId.GetInstance();
    value.Index = valueId.GetIndex();
    
    // Genre
    switch (valueId.GetGenre()) {
        case OpenZWave::ValueID::ValueGenre_Basic: value.Genre = ZWaveValueGenre::Basic; break;
        case OpenZWave::ValueID::ValueGenre_User: value.Genre = ZWaveValueGenre::User; break;
        case OpenZWave::ValueID::ValueGenre_Config: value.Genre = ZWaveValueGenre::Config; break;
        case OpenZWave::ValueID::ValueGenre_System: value.Genre = ZWaveValueGenre::System; break;
        default: value.Genre = ZWaveValueGenre::User; break;
    }
    
    // Type and value
    auto* mgr = OpenZWave::Manager::Get();
    
    value.Label = mgr->GetValueLabel(valueId);
    value.Units = mgr->GetValueUnits(valueId);
    value.Help = mgr->GetValueHelp(valueId);
    value.ReadOnly = mgr->IsValueReadOnly(valueId);
    value.WriteOnly = mgr->IsValueWriteOnly(valueId);
    value.IsSet = mgr->IsValueSet(valueId);
    value.IsPolled = mgr->IsValuePolled(valueId);
    
    switch (valueId.GetType()) {
        case OpenZWave::ValueID::ValueType_Bool:
            value.Type = ZWaveValueType::Bool;
            mgr->GetValueAsBool(valueId, &value.BoolValue);
            break;
            
        case OpenZWave::ValueID::ValueType_Byte:
            value.Type = ZWaveValueType::Byte;
            mgr->GetValueAsByte(valueId, &value.ByteValue);
            break;
            
        case OpenZWave::ValueID::ValueType_Decimal:
            value.Type = ZWaveValueType::Decimal;
            {
                std::string decStr;
                mgr->GetValueAsString(valueId, &decStr);
                value.DecimalValue = std::stof(decStr);
            }
            break;
            
        case OpenZWave::ValueID::ValueType_Int:
            value.Type = ZWaveValueType::Int;
            mgr->GetValueAsInt(valueId, &value.IntValue);
            break;
            
        case OpenZWave::ValueID::ValueType_Short:
            value.Type = ZWaveValueType::Short;
            mgr->GetValueAsShort(valueId, &value.ShortValue);
            break;
            
        case OpenZWave::ValueID::ValueType_String:
            value.Type = ZWaveValueType::String;
            mgr->GetValueAsString(valueId, &value.StringValue);
            break;
            
        case OpenZWave::ValueID::ValueType_Button:
            value.Type = ZWaveValueType::Button;
            break;
            
        case OpenZWave::ValueID::ValueType_List:
            value.Type = ZWaveValueType::List;
            {
                std::vector<std::string> items;
                mgr->GetValueListItems(valueId, &items);
                value.ListItems = items;
                
                std::string selected;
                mgr->GetValueListSelection(valueId, &selected);
                for (size_t i = 0; i < items.size(); i++) {
                    if (items[i] == selected) {
                        value.ListSelection = static_cast<int>(i);
                        break;
                    }
                }
            }
            break;
            
        case OpenZWave::ValueID::ValueType_Raw:
            value.Type = ZWaveValueType::Raw;
            {
                uint8_t* raw;
                uint8_t length;
                mgr->GetValueAsRaw(valueId, &raw, &length);
                value.RawValue.assign(raw, raw + length);
                delete[] raw;
            }
            break;
            
        default:
            value.Type = ZWaveValueType::String;
            break;
    }
    
    return value;
}

bool ZWaveProtocol::FindValueId(uint8_t nodeId, ZWaveCommandClass cc, uint8_t instance,
                                uint16_t index, OpenZWave::ValueID& outValueId) const {
    // This requires iterating through all values for the node
    // OpenZWave doesn't provide direct lookup by these parameters
    
    std::lock_guard<std::mutex> lock(nodesMutex);
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) {
        return false;
    }
    
    for (const auto& [valueId, value] : nodeIt->second.Values) {
        if (value.CommandClass == cc && value.Instance == instance && value.Index == index) {
            // Reconstruct OpenZWave::ValueID
            // This is a simplified approach - actual implementation may vary
            outValueId = OpenZWave::ValueID(
                homeId, nodeId, 
                static_cast<OpenZWave::ValueID::ValueGenre>(value.Genre),
                static_cast<uint8_t>(cc), instance, index,
                static_cast<OpenZWave::ValueID::ValueType>(value.Type));
            return true;
        }
    }
    
    return false;
}

#endif // ULTRACANVAS_WITH_ZWAVE

// ============================================================================
// HELPER METHODS
// ============================================================================

void ZWaveProtocol::UpdateNodeInfo(ZWaveNode& node) {
#ifdef ULTRACANVAS_WITH_ZWAVE
    auto* mgr = OpenZWave::Manager::Get();
    
    node.Name = mgr->GetNodeName(homeId, node.NodeId);
    node.Location = mgr->GetNodeLocation(homeId, node.NodeId);
    node.ManufacturerName = mgr->GetNodeManufacturerName(homeId, node.NodeId);
    node.ProductName = mgr->GetNodeProductName(homeId, node.NodeId);
    node.ProductType = mgr->GetNodeProductType(homeId, node.NodeId);
    node.ProductId = mgr->GetNodeProductId(homeId, node.NodeId);
    
    node.ManufacturerId = static_cast<uint16_t>(std::stoi(
        mgr->GetNodeManufacturerId(homeId, node.NodeId), nullptr, 16));
    node.ProductTypeId = static_cast<uint16_t>(std::stoi(
        node.ProductType, nullptr, 16));
    node.ProductIdNum = static_cast<uint16_t>(std::stoi(
        node.ProductId, nullptr, 16));
    
    node.BasicType = mgr->GetNodeBasic(homeId, node.NodeId);
    node.GenericType = mgr->GetNodeGeneric(homeId, node.NodeId);
    node.SpecificType = mgr->GetNodeSpecific(homeId, node.NodeId);
    
    node.IsListening = mgr->IsNodeListeningDevice(homeId, node.NodeId);
    node.IsFrequentListening = mgr->IsNodeFrequentListeningDevice(homeId, node.NodeId);
    node.IsBeaming = mgr->IsNodeBeamingDevice(homeId, node.NodeId);
    node.IsRouting = mgr->IsNodeRoutingDevice(homeId, node.NodeId);
    node.IsSecurityDevice = mgr->IsNodeSecurityDevice(homeId, node.NodeId);
    node.IsZWavePlus = mgr->IsNodeZWavePlus(homeId, node.NodeId);
    
    node.IsAwake = mgr->IsNodeAwake(homeId, node.NodeId);
    node.IsFailed = mgr->IsNodeFailed(homeId, node.NodeId);
    
    // Get neighbors
    uint8_t* neighbors;
    uint32_t numNeighbors = mgr->GetNodeNeighbors(homeId, node.NodeId, &neighbors);
    node.Neighbors.assign(neighbors, neighbors + numNeighbors);
    delete[] neighbors;
#endif
}

SmartHomeDeviceInfo ZWaveProtocol::ConvertToDeviceInfo(const ZWaveNode& node) const {
    SmartHomeDeviceInfo info;
    
    info.DeviceId = MakeDeviceId(node.NodeId);
    info.Name = node.Name.empty() ? node.ProductName : node.Name;
    if (info.Name.empty()) {
        info.Name = "Z-Wave Node " + std::to_string(node.NodeId);
    }
    
    info.Manufacturer = node.ManufacturerName;
    info.Model = node.ProductName;
    info.Protocol = SmartHomeProtocolType::ZWave;
    info.Category = DetermineCategory(node);
    
    info.State = node.IsFailed ? SmartHomeDeviceState::Error :
                 (node.IsReady ? SmartHomeDeviceState::Online : SmartHomeDeviceState::Offline);
    
    // Capabilities
    for (auto cc : node.CommandClasses) {
        switch (cc) {
            case ZWaveCommandClass::SwitchBinary:
                info.Capabilities.push_back("switch");
                info.Capabilities.push_back("onOff");
                break;
            case ZWaveCommandClass::SwitchMultilevel:
                info.Capabilities.push_back("dimmer");
                info.Capabilities.push_back("brightness");
                break;
            case ZWaveCommandClass::SwitchColor:
                info.Capabilities.push_back("color");
                break;
            case ZWaveCommandClass::ThermostatMode:
            case ZWaveCommandClass::ThermostatSetpoint:
                info.Capabilities.push_back("thermostat");
                break;
            case ZWaveCommandClass::DoorLock:
                info.Capabilities.push_back("lock");
                break;
            case ZWaveCommandClass::SensorBinary:
                info.Capabilities.push_back("binarySensor");
                break;
            case ZWaveCommandClass::SensorMultilevel:
                info.Capabilities.push_back("sensor");
                break;
            case ZWaveCommandClass::Meter:
                info.Capabilities.push_back("meter");
                info.Capabilities.push_back("energy");
                break;
            case ZWaveCommandClass::Battery:
                info.Capabilities.push_back("battery");
                break;
            default:
                break;
        }
    }
    
    return info;
}

SmartHomeDeviceCategory ZWaveProtocol::DetermineCategory(const ZWaveNode& node) const {
    // Determine category based on command classes and generic type
    
    // Check for specific command classes
    if (node.CommandClasses.count(ZWaveCommandClass::DoorLock)) {
        return SmartHomeDeviceCategory::Lock;
    }
    if (node.CommandClasses.count(ZWaveCommandClass::ThermostatMode) ||
        node.CommandClasses.count(ZWaveCommandClass::ThermostatSetpoint)) {
        return SmartHomeDeviceCategory::Thermostat;
    }
    if (node.CommandClasses.count(ZWaveCommandClass::SwitchColor)) {
        return SmartHomeDeviceCategory::Light;
    }
    if (node.CommandClasses.count(ZWaveCommandClass::SwitchMultilevel)) {
        // Could be light or blind
        if (node.GenericType == 0x11) {  // GENERIC_TYPE_SWITCH_MULTILEVEL
            if (node.SpecificType == 0x06 || node.SpecificType == 0x07) {
                return SmartHomeDeviceCategory::Blind;
            }
        }
        return SmartHomeDeviceCategory::Light;
    }
    if (node.CommandClasses.count(ZWaveCommandClass::SwitchBinary)) {
        // Could be switch, plug, or light
        if (node.GenericType == 0x10) {  // GENERIC_TYPE_SWITCH_BINARY
            if (node.SpecificType == 0x01) {
                return SmartHomeDeviceCategory::Plug;
            }
        }
        return SmartHomeDeviceCategory::Switch;
    }
    if (node.CommandClasses.count(ZWaveCommandClass::SensorBinary) ||
        node.CommandClasses.count(ZWaveCommandClass::SensorMultilevel) ||
        node.CommandClasses.count(ZWaveCommandClass::Alarm)) {
        return SmartHomeDeviceCategory::Sensor;
    }
    
    return SmartHomeDeviceCategory::Unknown;
}

std::string ZWaveProtocol::MakeDeviceId(uint8_t nodeId) const {
    std::stringstream ss;
    ss << "zwave-" << std::hex << homeId << "-" << std::dec << (int)nodeId;
    return ss.str();
}

uint8_t ZWaveProtocol::ParseNodeId(const std::string& deviceId) const {
    // Format: zwave-<homeId>-<nodeId>
    size_t lastDash = deviceId.rfind('-');
    if (lastDash != std::string::npos) {
        try {
            return static_cast<uint8_t>(std::stoi(deviceId.substr(lastDash + 1)));
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

void ZWaveProtocol::NotifyNodeAdded(const ZWaveNode& node) {
    if (onNodeAdded) {
        onNodeAdded(node);
    }
    
    // Also notify SmartHome callbacks
    SmartHomeDeviceInfo info = ConvertToDeviceInfo(node);
    if (deviceDiscoveredCallback) {
        deviceDiscoveredCallback(info);
    }
}

void ZWaveProtocol::NotifyNodeRemoved(const ZWaveNode& node) {
    if (onNodeRemoved) {
        onNodeRemoved(node);
    }
}

void ZWaveProtocol::NotifyValueChanged(const ZWaveValue& value) {
    if (onValueChanged) {
        onValueChanged(value);
    }
    
    // Convert to SmartHome state change
    std::map<std::string, std::string> state;
    
    switch (value.CommandClass) {
        case ZWaveCommandClass::SwitchBinary:
            state["on"] = value.BoolValue ? "true" : "false";
            break;
        case ZWaveCommandClass::SwitchMultilevel:
            state["level"] = std::to_string(value.ByteValue);
            break;
        case ZWaveCommandClass::Battery:
            state["battery"] = std::to_string(value.ByteValue);
            break;
        default:
            return;  // Don't notify for unhandled types
    }
    
    if (deviceStateChangedCallback && !state.empty()) {
        deviceStateChangedCallback(MakeDeviceId(value.NodeId), state);
    }
}

void ZWaveProtocol::ProcessNotificationQueue() {
    while (!stopProcessing) {
        std::function<void()> task;
        
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (!notificationQueue.empty()) {
                task = notificationQueue.front();
                notificationQueue.pop();
            }
        }
        
        if (task) {
            task();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

std::shared_ptr<ISmartHomeProtocol> CreateZWaveProtocol() {
    return std::make_shared<ZWaveProtocol>();
}

} // namespace SmartHome
} // namespace UltraCanvas
