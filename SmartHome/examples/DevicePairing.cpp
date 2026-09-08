// DevicePairing.cpp
// UltraCanvas SmartHome - Thread Device Pairing Example
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework

/**
 * This example demonstrates how to:
 * 1. Initialize the SmartHome API
 * 2. Form a Thread network
 * 3. Enable commissioning (pairing) mode
 * 4. Add joiners and handle pairing events
 * 5. Control paired devices
 */

#include "UltraCanvasSmartHome.h"
#include "ThreadProtocol.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>

using namespace UltraCanvas::SmartHome;

// Global flag for main loop
static bool g_running = true;

void PrintUsage() {
    std::cout << "\nCommands:\n"
              << "  form <name>    - Form new Thread network\n"
              << "  pair           - Start pairing mode (120s)\n"
              << "  add <eui64> <pskd> - Add specific joiner\n"
              << "  addany <pskd>  - Allow any joiner with PSKd\n"
              << "  devices        - List paired devices\n"
              << "  topology       - Show network topology\n"
              << "  diag           - Show diagnostics\n"
              << "  quit           - Exit\n\n";
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

void PrintDevices() {
    auto devices = SMARTHOME_API.GetDevices();
    
    std::cout << "\nPaired Devices (" << devices.size() << "):\n";
    std::cout << "----------------------------------------\n";
    
    for (const auto& device : devices) {
        std::cout << "  " << device.Name << "\n"
                  << "    ID:       " << device.DeviceId << "\n"
                  << "    Category: " << DeviceCategoryToString(device.Category) << "\n"
                  << "    Protocol: " << ProtocolTypeToString(device.Protocol) << "\n"
                  << "    State:    " << (device.State == SmartHomeDeviceState::Online 
                                          ? "Online" : "Offline") << "\n";
    }
    std::cout << std::endl;
}

void PrintTopology(ThreadProtocol* thread) {
    if (!thread) {
        std::cout << "Thread protocol not available\n";
        return;
    }
    
    auto topology = thread->GetTopology();
    
    std::cout << "\nThread Network Topology:\n";
    std::cout << "----------------------------------------\n";
    
    for (const auto& node : topology.Nodes) {
        std::cout << "  [" << node.NodeType << "] " << node.NodeId;
        if (node.IsRouter) std::cout << " (Router)";
        if (node.IsOnline) std::cout << " - Online";
        else std::cout << " - Offline";
        if (!node.ParentId.empty()) std::cout << " -> " << node.ParentId;
        std::cout << "\n";
    }
    std::cout << std::endl;
}

void PrintDiagnostics(ThreadProtocol* thread) {
    if (!thread) {
        std::cout << "Thread protocol not available\n";
        return;
    }
    
    auto diag = thread->GetThreadDiagnostics();
    auto dataset = thread->GetDataset();
    
    std::cout << "\nThread Network Diagnostics:\n";
    std::cout << "----------------------------------------\n";
    std::cout << "  Network Name:  " << dataset.NetworkName << "\n";
    std::cout << "  PAN ID:        0x" << std::hex << dataset.PanId << std::dec << "\n";
    std::cout << "  Channel:       " << dataset.Channel << "\n";
    std::cout << "  Role:          " << (thread->IsLeader() ? "Leader" : 
                                         thread->IsRouter() ? "Router" : "Child") << "\n";
    std::cout << "  Partition ID:  " << diag.PartitionId << "\n";
    std::cout << "  Router Count:  " << diag.RouterCount << "\n";
    std::cout << "  Child Count:   " << diag.ChildCount << "\n";
    std::cout << "  Mesh Local:    " << thread->GetMeshLocalAddress() << "\n";
    std::cout << "  Border Router: " << (thread->IsBorderRouter() ? "Yes" : "No") << "\n";
    std::cout << "  Commissioner:  " << (thread->IsCommissionerActive() ? "Active" : "Inactive") << "\n";
    
    auto addresses = thread->GetIPv6Addresses();
    std::cout << "  IPv6 Addresses:\n";
    for (const auto& addr : addresses) {
        std::cout << "    " << addr << "\n";
    }
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "======================================\n";
    std::cout << "UltraCanvas SmartHome - Thread Pairing\n";
    std::cout << "======================================\n\n";
    
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
    
    // Create and register Thread protocol
    auto threadProtocol = std::dynamic_pointer_cast<ThreadProtocol>(CreateThreadProtocol());
    
    // Check available adapters
    auto adapters = threadProtocol->GetAvailableAdapters();
    std::cout << "Available Thread adapters:\n";
    for (const auto& adapter : adapters) {
        std::cout << "  " << adapter << "\n";
    }
    
    // Select adapter (first real one if available)
    if (!adapters.empty()) {
        for (const auto& adapter : adapters) {
            if (adapter.find("spinel+hdlc+forkpty") == std::string::npos) {
                threadProtocol->SelectAdapter(adapter);
                std::cout << "Selected adapter: " << adapter << "\n";
                break;
            }
        }
    }
    
    // Initialize Thread protocol
    std::cout << "\nInitializing Thread protocol...\n";
    if (!threadProtocol->Initialize()) {
        std::cerr << "Failed to initialize Thread protocol\n";
        std::cerr << "(This may be normal if no Thread hardware is present)\n";
    }
    
    // Get SmartHomeManager instance and register protocol
    // In real code, this would be done through SmartHomeManager
    // SMARTHOME_API.RegisterProtocol(SmartHomeProtocolType::Thread, threadProtocol);
    
    PrintUsage();
    
    // Main command loop
    std::string line;
    while (g_running) {
        std::cout << "> ";
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
            std::string name;
            iss >> name;
            if (name.empty()) {
                name = "UltraCanvas-Thread";
            }
            std::cout << "Forming Thread network: " << name << "\n";
            if (threadProtocol->FormNetwork(name)) {
                std::cout << "Network formation started. Waiting for leader election...\n";
                
                // Wait a bit for network to form
                std::this_thread::sleep_for(std::chrono::seconds(3));
                
                if (threadProtocol->IsAttached()) {
                    std::cout << "Network formed successfully!\n";
                    PrintDiagnostics(threadProtocol.get());
                } else {
                    std::cout << "Network still forming...\n";
                }
            } else {
                std::cerr << "Failed to form network\n";
            }
        }
        else if (cmd == "pair") {
            if (!threadProtocol->IsAttached()) {
                std::cout << "Not attached to network. Form a network first.\n";
                continue;
            }
            
            std::cout << "Starting pairing mode (120 seconds)...\n";
            if (threadProtocol->StartPairing(120)) {
                std::cout << "Pairing mode active. Devices can now join.\n";
                std::cout << "Use 'add <eui64> <pskd>' to authorize specific devices.\n";
                std::cout << "Use 'addany <pskd>' to allow any device with the PSKd.\n";
            } else {
                std::cerr << "Failed to start pairing mode\n";
            }
        }
        else if (cmd == "add") {
            std::string eui64, pskd;
            iss >> eui64 >> pskd;
            if (eui64.empty() || pskd.empty()) {
                std::cout << "Usage: add <eui64> <pskd>\n";
                std::cout << "Example: add 18b430000000001a ULTRACANVAS\n";
                continue;
            }
            
            if (threadProtocol->AddJoiner(eui64, pskd, 120)) {
                std::cout << "Joiner added: " << eui64 << "\n";
            } else {
                std::cerr << "Failed to add joiner\n";
            }
        }
        else if (cmd == "addany") {
            std::string pskd;
            iss >> pskd;
            if (pskd.empty()) {
                pskd = "ULTRACANVAS";  // Default PSKd
            }
            
            if (threadProtocol->AddJoiner("*", pskd, 120)) {
                std::cout << "Any joiner with PSKd '" << pskd << "' can now join\n";
            } else {
                std::cerr << "Failed to add wildcard joiner\n";
            }
        }
        else if (cmd == "devices") {
            PrintDevices();
        }
        else if (cmd == "topology") {
            PrintTopology(threadProtocol.get());
        }
        else if (cmd == "diag") {
            PrintDiagnostics(threadProtocol.get());
        }
        else if (cmd == "stop") {
            threadProtocol->StopPairing();
            std::cout << "Pairing mode stopped\n";
        }
        else if (cmd == "leave") {
            threadProtocol->LeaveNetwork();
            std::cout << "Left network\n";
        }
        else if (cmd == "br") {
            bool enable;
            std::string arg;
            iss >> arg;
            enable = (arg == "on" || arg == "enable" || arg == "1");
            
            if (threadProtocol->EnableBorderRouter(enable)) {
                std::cout << "Border Router " << (enable ? "enabled" : "disabled") << "\n";
            } else {
                std::cerr << "Failed to configure Border Router\n";
            }
        }
        else if (!cmd.empty()) {
            std::cout << "Unknown command: " << cmd << "\n";
            std::cout << "Type 'help' for available commands.\n";
        }
    }
    
    // Cleanup
    std::cout << "\nShutting down...\n";
    threadProtocol->Shutdown();
    SMARTHOME_API.Shutdown();
    
    std::cout << "Goodbye!\n";
    return 0;
}
