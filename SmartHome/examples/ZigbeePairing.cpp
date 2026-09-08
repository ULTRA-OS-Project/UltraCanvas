// ZigbeePairing.cpp
// UltraCanvas SmartHome - Zigbee Device Pairing Example
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework

/**
 * This example demonstrates how to:
 * 1. Initialize the SmartHome API with Zigbee protocol
 * 2. Form a Zigbee network
 * 3. Enable permit join for device pairing
 * 4. Control Zigbee devices (lights, switches, etc.)
 * 5. Manage groups and scenes
 */

#include "UltraCanvasSmartHome.h"
#include "ZigbeeProtocol.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>

using namespace UltraCanvas::SmartHome;

static bool g_running = true;

void PrintUsage() {
    std::cout << "\nZigbee Commands:\n"
              << "  form              - Form new Zigbee network\n"
              << "  permit [sec]      - Enable permit join (default 120s)\n"
              << "  stop              - Stop permit join\n"
              << "  devices           - List paired devices\n"
              << "  info <id>         - Show device info\n"
              << "  on <id>           - Turn device on\n"
              << "  off <id>          - Turn device off\n"
              << "  toggle <id>       - Toggle device\n"
              << "  level <id> <0-100> - Set brightness\n"
              << "  color <id> <h> <s> - Set color (hue 0-360, sat 0-100)\n"
              << "  group add <gid> <name> - Create group\n"
              << "  group join <id> <gid>  - Add device to group\n"
              << "  group on <gid>    - Turn group on\n"
              << "  group off <gid>   - Turn group off\n"
              << "  scene store <gid> <sid> <id> - Store scene\n"
              << "  scene recall <gid> <sid>     - Recall scene\n"
              << "  bind <src> <dst>  - Bind two devices\n"
              << "  remove <id>       - Remove device\n"
              << "  topology          - Show network topology\n"
              << "  quit              - Exit\n\n";
}

void OnDeviceDiscovered(const SmartHomeDeviceInfo& device) {
    std::cout << "\n[DISCOVERED] " << device.Name 
              << " (" << device.DeviceId << ")"
              << " - " << DeviceCategoryToString(device.Category)
              << std::endl;
}

void OnDeviceStateChange(const std::string& deviceId, SmartHomeDeviceState state) {
    std::cout << "\n[STATE] " << deviceId << " -> " 
              << (state == SmartHomeDeviceState::Online ? "Online" : "Offline")
              << std::endl;
}

void OnPairingProgress(int progress, const std::string& status) {
    std::cout << "\r[PAIRING] " << progress << "% - " << status << std::flush;
    if (progress == 100) std::cout << std::endl;
}

void OnError(int code, const std::string& message) {
    std::cerr << "\n[ERROR " << code << "] " << message << std::endl;
}

void PrintDevices(ZigbeeProtocol* zigbee) {
    auto nodes = zigbee->GetAllZigbeeNodes();
    
    std::cout << "\nZigbee Devices (" << nodes.size() << "):\n";
    std::cout << "================================================\n";
    
    for (const auto& node : nodes) {
        std::cout << "  " << node.DeviceId << "\n"
                  << "    Name:         " << node.ModelIdentifier << "\n"
                  << "    Manufacturer: " << node.ManufacturerName << "\n"
                  << "    IEEE:         " << std::hex << node.IeeeAddress << std::dec << "\n"
                  << "    Network Addr: 0x" << std::hex << node.NwkAddress << std::dec << "\n"
                  << "    Type:         " << (node.Type == ZigbeeDeviceType::Router ? "Router" : 
                                              node.Type == ZigbeeDeviceType::EndDevice ? "End Device" : "Unknown") << "\n"
                  << "    Status:       " << (node.IsOnline ? "Online" : "Offline") << "\n"
                  << "    LQI:          " << static_cast<int>(node.Lqi) << "\n"
                  << "    Endpoints:    " << node.Endpoints.size() << "\n";
        
        for (const auto& ep : node.Endpoints) {
            std::cout << "      EP " << static_cast<int>(ep.EndpointId) 
                      << ": Device 0x" << std::hex << ep.DeviceId << std::dec
                      << ", " << ep.InputClusters.size() << " clusters\n";
        }
        std::cout << "\n";
    }
}

void PrintDeviceInfo(ZigbeeProtocol* zigbee, const std::string& deviceId) {
    auto node = zigbee->GetZigbeeNode(deviceId);
    if (node.DeviceId.empty()) {
        std::cout << "Device not found: " << deviceId << "\n";
        return;
    }
    
    std::cout << "\nDevice: " << deviceId << "\n";
    std::cout << "----------------------------------------\n";
    std::cout << "  Model:          " << node.ModelIdentifier << "\n";
    std::cout << "  Manufacturer:   " << node.ManufacturerName << "\n";
    std::cout << "  IEEE Address:   " << std::hex << node.IeeeAddress << std::dec << "\n";
    std::cout << "  Network Addr:   0x" << std::hex << node.NwkAddress << std::dec << "\n";
    std::cout << "  Device Type:    " << (node.Type == ZigbeeDeviceType::Router ? "Router" : 
                                          node.Type == ZigbeeDeviceType::EndDevice ? "End Device" : "Unknown") << "\n";
    std::cout << "  Power Source:   " << (node.PowerSource == 1 ? "Mains" : 
                                          node.PowerSource == 3 ? "Battery" : "Unknown") << "\n";
    std::cout << "  Status:         " << (node.IsOnline ? "Online" : "Offline") << "\n";
    std::cout << "  Link Quality:   " << static_cast<int>(node.Lqi) << "/255\n";
    std::cout << "  RSSI:           " << static_cast<int>(node.Rssi) << " dBm\n";
    
    std::cout << "\nEndpoints:\n";
    for (const auto& ep : node.Endpoints) {
        std::cout << "  Endpoint " << static_cast<int>(ep.EndpointId) << ":\n";
        std::cout << "    Profile:  0x" << std::hex << ep.ProfileId << std::dec;
        if (ep.ProfileId == ZigbeeProfiles::HomeAutomation) std::cout << " (Home Automation)";
        else if (ep.ProfileId == ZigbeeProfiles::LightLink) std::cout << " (Light Link)";
        std::cout << "\n";
        std::cout << "    Device:   0x" << std::hex << ep.DeviceId << std::dec << "\n";
        
        std::cout << "    Input Clusters:\n";
        for (const auto& cluster : ep.InputClusters) {
            std::cout << "      0x" << std::hex << cluster.ClusterId << std::dec;
            // Add cluster names
            if (cluster.ClusterId == ZigbeeClusters::Basic) std::cout << " (Basic)";
            else if (cluster.ClusterId == ZigbeeClusters::OnOff) std::cout << " (On/Off)";
            else if (cluster.ClusterId == ZigbeeClusters::LevelControl) std::cout << " (Level Control)";
            else if (cluster.ClusterId == ZigbeeClusters::ColorControl) std::cout << " (Color Control)";
            else if (cluster.ClusterId == ZigbeeClusters::Groups) std::cout << " (Groups)";
            else if (cluster.ClusterId == ZigbeeClusters::Scenes) std::cout << " (Scenes)";
            else if (cluster.ClusterId == ZigbeeClusters::Thermostat) std::cout << " (Thermostat)";
            else if (cluster.ClusterId == ZigbeeClusters::DoorLock) std::cout << " (Door Lock)";
            else if (cluster.ClusterId == ZigbeeClusters::WindowCovering) std::cout << " (Window Covering)";
            else if (cluster.ClusterId == ZigbeeClusters::TemperatureMeasurement) std::cout << " (Temperature)";
            else if (cluster.ClusterId == ZigbeeClusters::RelativeHumidity) std::cout << " (Humidity)";
            else if (cluster.ClusterId == ZigbeeClusters::OccupancySensing) std::cout << " (Occupancy)";
            else if (cluster.ClusterId == ZigbeeClusters::IASZone) std::cout << " (IAS Zone)";
            else if (cluster.ClusterId == ZigbeeClusters::ElectricalMeasurement) std::cout << " (Electrical)";
            std::cout << "\n";
        }
    }
    std::cout << std::endl;
}

void PrintTopology(ZigbeeProtocol* zigbee) {
    auto topology = zigbee->GetTopology();
    
    std::cout << "\nZigbee Network Topology:\n";
    std::cout << "================================================\n";
    std::cout << "  PAN ID:      0x" << std::hex << zigbee->GetPanId() << std::dec << "\n";
    std::cout << "  Ext PAN ID:  0x" << std::hex << zigbee->GetExtendedPanId() << std::dec << "\n";
    std::cout << "  Channel:     " << static_cast<int>(zigbee->GetChannel()) << "\n\n";
    
    for (const auto& node : topology.Nodes) {
        std::cout << "  [" << node.NodeType << "] " << node.NodeId;
        if (node.IsRouter) std::cout << " (Router)";
        if (node.IsOnline) std::cout << " - Online";
        else std::cout << " - Offline";
        if (node.LinkQuality > 0) std::cout << " LQI:" << static_cast<int>(node.LinkQuality);
        std::cout << "\n";
    }
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "==========================================\n";
    std::cout << "UltraCanvas SmartHome - Zigbee Controller\n";
    std::cout << "==========================================\n\n";
    
    // Initialize SmartHome API
    std::cout << "Initializing SmartHome API...\n";
    if (!SMARTHOME_API.Initialize()) {
        std::cerr << "Failed to initialize SmartHome API\n";
        return 1;
    }
    
    // Set up callbacks
    SMARTHOME_API.SetOnDeviceDiscover(OnDeviceDiscovered);
    SMARTHOME_API.SetOnDeviceStateChange(OnDeviceStateChange);
    SMARTHOME_API.SetOnPairingProgress(OnPairingProgress);
    SMARTHOME_API.SetOnSmartHomeError(OnError);
    
    // Create Zigbee protocol
    auto zigbeeProtocol = std::dynamic_pointer_cast<ZigbeeProtocol>(CreateZigbeeProtocol());
    
    // Check available adapters
    auto adapters = zigbeeProtocol->GetAvailableAdapters();
    std::cout << "Available Zigbee adapters:\n";
    for (const auto& adapter : adapters) {
        std::cout << "  " << adapter << "\n";
    }
    
    // Select first real adapter if available
    for (const auto& adapter : adapters) {
        if (adapter.find("simulation") == std::string::npos) {
            zigbeeProtocol->SelectAdapter(adapter);
            std::cout << "Selected adapter: " << adapter << "\n";
            break;
        }
    }
    
    // Initialize Zigbee protocol
    std::cout << "\nInitializing Zigbee protocol...\n";
    if (!zigbeeProtocol->Initialize()) {
        std::cerr << "Failed to initialize Zigbee protocol\n";
        std::cerr << "(Running in stub mode - no hardware detected)\n";
    }
    
    PrintUsage();
    
    // Main command loop
    std::string line;
    while (g_running) {
        std::cout << "zigbee> ";
        if (!std::getline(std::cin, line)) {
            break;
        }
        
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        
        if (cmd == "quit" || cmd == "exit" || cmd == "q") {
            g_running = false;
        }
        else if (cmd == "help" || cmd == "?") {
            PrintUsage();
        }
        else if (cmd == "form") {
            std::cout << "Forming Zigbee network...\n";
            if (zigbeeProtocol->FormNetwork("UltraCanvas-Zigbee")) {
                std::cout << "Network formed successfully!\n";
                std::cout << "  PAN ID:  0x" << std::hex << zigbeeProtocol->GetPanId() << std::dec << "\n";
                std::cout << "  Channel: " << static_cast<int>(zigbeeProtocol->GetChannel()) << "\n";
            } else {
                std::cerr << "Failed to form network\n";
            }
        }
        else if (cmd == "permit") {
            int seconds = 120;
            iss >> seconds;
            std::cout << "Enabling permit join for " << seconds << " seconds...\n";
            if (zigbeeProtocol->PermitJoin(seconds)) {
                std::cout << "Permit join enabled. Put devices in pairing mode now.\n";
            } else {
                std::cerr << "Failed to enable permit join\n";
            }
        }
        else if (cmd == "stop") {
            zigbeeProtocol->PermitJoin(0);
            std::cout << "Permit join disabled\n";
        }
        else if (cmd == "devices") {
            PrintDevices(zigbeeProtocol.get());
        }
        else if (cmd == "info") {
            std::string deviceId;
            iss >> deviceId;
            if (deviceId.empty()) {
                std::cout << "Usage: info <device_id>\n";
            } else {
                PrintDeviceInfo(zigbeeProtocol.get(), deviceId);
            }
        }
        else if (cmd == "on") {
            std::string deviceId;
            iss >> deviceId;
            if (deviceId.empty()) {
                std::cout << "Usage: on <device_id>\n";
            } else {
                zigbeeProtocol->SendCommand(deviceId, "on", {});
                std::cout << "Sent ON command to " << deviceId << "\n";
            }
        }
        else if (cmd == "off") {
            std::string deviceId;
            iss >> deviceId;
            if (deviceId.empty()) {
                std::cout << "Usage: off <device_id>\n";
            } else {
                zigbeeProtocol->SendCommand(deviceId, "off", {});
                std::cout << "Sent OFF command to " << deviceId << "\n";
            }
        }
        else if (cmd == "toggle") {
            std::string deviceId;
            iss >> deviceId;
            if (deviceId.empty()) {
                std::cout << "Usage: toggle <device_id>\n";
            } else {
                zigbeeProtocol->SendCommand(deviceId, "toggle", {});
                std::cout << "Sent TOGGLE command to " << deviceId << "\n";
            }
        }
        else if (cmd == "level") {
            std::string deviceId;
            int level;
            iss >> deviceId >> level;
            if (deviceId.empty()) {
                std::cout << "Usage: level <device_id> <0-100>\n";
            } else {
                zigbeeProtocol->SendCommand(deviceId, "setLevel", {{"brightness", std::to_string(level)}});
                std::cout << "Set brightness to " << level << "% on " << deviceId << "\n";
            }
        }
        else if (cmd == "color") {
            std::string deviceId;
            int hue, sat;
            iss >> deviceId >> hue >> sat;
            if (deviceId.empty()) {
                std::cout << "Usage: color <device_id> <hue 0-360> <sat 0-100>\n";
            } else {
                zigbeeProtocol->SendCommand(deviceId, "setColor", {
                    {"hue", std::to_string(hue)},
                    {"saturation", std::to_string(sat)}
                });
                std::cout << "Set color H:" << hue << " S:" << sat << " on " << deviceId << "\n";
            }
        }
        else if (cmd == "group") {
            std::string subcmd;
            iss >> subcmd;
            
            if (subcmd == "add") {
                uint16_t groupId;
                std::string name;
                iss >> groupId >> name;
                if (zigbeeProtocol->CreateGroup(groupId, name)) {
                    std::cout << "Created group " << groupId << ": " << name << "\n";
                }
            }
            else if (subcmd == "join") {
                std::string deviceId;
                uint16_t groupId;
                iss >> deviceId >> groupId;
                if (zigbeeProtocol->AddToGroup(deviceId, groupId)) {
                    std::cout << "Added " << deviceId << " to group " << groupId << "\n";
                }
            }
            else if (subcmd == "on") {
                uint16_t groupId;
                iss >> groupId;
                zigbeeProtocol->SendGroupCommand(groupId, "on", {});
                std::cout << "Sent ON to group " << groupId << "\n";
            }
            else if (subcmd == "off") {
                uint16_t groupId;
                iss >> groupId;
                zigbeeProtocol->SendGroupCommand(groupId, "off", {});
                std::cout << "Sent OFF to group " << groupId << "\n";
            }
            else {
                std::cout << "Group commands: add, join, on, off\n";
            }
        }
        else if (cmd == "scene") {
            std::string subcmd;
            iss >> subcmd;
            
            if (subcmd == "store") {
                uint16_t groupId;
                uint8_t sceneId;
                std::string deviceId;
                iss >> groupId >> sceneId >> deviceId;
                if (zigbeeProtocol->StoreScene(deviceId, groupId, sceneId)) {
                    std::cout << "Stored scene " << static_cast<int>(sceneId) << " in group " << groupId << "\n";
                }
            }
            else if (subcmd == "recall") {
                uint16_t groupId;
                uint8_t sceneId;
                iss >> groupId >> sceneId;
                if (zigbeeProtocol->RecallScene(groupId, sceneId)) {
                    std::cout << "Recalled scene " << static_cast<int>(sceneId) << " in group " << groupId << "\n";
                }
            }
            else {
                std::cout << "Scene commands: store, recall\n";
            }
        }
        else if (cmd == "bind") {
            std::string srcId, dstId;
            iss >> srcId >> dstId;
            if (srcId.empty() || dstId.empty()) {
                std::cout << "Usage: bind <source_id> <destination_id>\n";
            } else {
                if (zigbeeProtocol->BindDevices(srcId, dstId)) {
                    std::cout << "Bound " << srcId << " -> " << dstId << "\n";
                }
            }
        }
        else if (cmd == "remove") {
            std::string deviceId;
            iss >> deviceId;
            if (deviceId.empty()) {
                std::cout << "Usage: remove <device_id>\n";
            } else {
                if (zigbeeProtocol->RemoveDevice(deviceId)) {
                    std::cout << "Removed device " << deviceId << "\n";
                }
            }
        }
        else if (cmd == "topology") {
            PrintTopology(zigbeeProtocol.get());
        }
        else if (cmd == "interview") {
            std::string deviceId;
            iss >> deviceId;
            if (deviceId.empty()) {
                std::cout << "Usage: interview <device_id>\n";
            } else {
                std::cout << "Interviewing " << deviceId << "...\n";
                zigbeeProtocol->InterviewDevice(deviceId);
            }
        }
        else if (cmd == "read") {
            std::string deviceId;
            int ep, cluster, attr;
            iss >> deviceId >> ep >> cluster >> attr;
            if (deviceId.empty()) {
                std::cout << "Usage: read <device_id> <endpoint> <cluster_id> <attr_id>\n";
            } else {
                ZigbeeAttributeValue value;
                if (zigbeeProtocol->ReadAttribute(deviceId, ep, cluster, attr, value)) {
                    std::cout << "Value: ";
                    for (uint8_t b : value.Value) {
                        std::cout << std::hex << static_cast<int>(b) << " ";
                    }
                    std::cout << std::dec << "\n";
                } else {
                    std::cout << "Failed to read attribute\n";
                }
            }
        }
        else if (!cmd.empty()) {
            std::cout << "Unknown command: " << cmd << "\n";
            std::cout << "Type 'help' for available commands.\n";
        }
    }
    
    // Cleanup
    std::cout << "\nShutting down...\n";
    zigbeeProtocol->Shutdown();
    SMARTHOME_API.Shutdown();
    
    std::cout << "Goodbye!\n";
    return 0;
}
