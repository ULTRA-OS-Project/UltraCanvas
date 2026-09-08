// SmartHomeProtocolBase.h
// Base class for Smart Home protocol implementations
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework

#pragma once

#include "ISmartHomeProtocol.h"
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>

namespace UltraCanvas {
namespace SmartHome {

/**
 * @brief Base implementation class for smart home protocols
 * 
 * Provides common functionality shared across all protocol implementations:
 * - State management
 * - Callback handling
 * - Device registry
 * - Thread-safe operations
 */
class SmartHomeProtocolBase : public ISmartHomeProtocol {
public:
    SmartHomeProtocolBase(SmartHomeProtocolType type, const std::string& name)
        : protocolType(type)
        , protocolName(name)
        , state(ProtocolState::Uninitialized)
        , discovering(false)
        , pairing(false) {}
    
    virtual ~SmartHomeProtocolBase() {
        if (IsInitialized()) {
            Shutdown();
        }
    }
    
    // ===== IDENTIFICATION =====
    
    SmartHomeProtocolType GetType() const override { return protocolType; }
    std::string GetName() const override { return protocolName; }
    std::string GetVersion() const override { return protocolVersion; }
    
    // ===== STATE =====
    
    ProtocolState GetState() const override { return state; }
    
    bool IsInitialized() const override { 
        return state != ProtocolState::Uninitialized; 
    }
    
    bool IsDiscovering() const override { return discovering; }
    bool IsPairing() const override { return pairing; }
    
    // ===== CAPABILITIES =====
    
    ProtocolCapability GetCapabilities() const override {
        return static_cast<ProtocolCapability>(capabilities);
    }
    
    bool HasCapability(ProtocolCapability cap) const override {
        return (capabilities & static_cast<uint32_t>(cap)) != 0;
    }
    
    // ===== NETWORK =====
    
    bool HasNetwork() const override { return hasNetwork; }
    
    SmartHomeNetworkInfo GetNetworkInfo() const override {
        std::lock_guard<std::mutex> lock(networkMutex);
        return networkInfo;
    }
    
    // ===== DEVICE MANAGEMENT =====
    
    std::vector<SmartHomeDeviceInfo> GetDiscoveredDevices() const override {
        std::lock_guard<std::mutex> lock(deviceMutex);
        return discoveredDevices;
    }
    
    std::vector<std::string> GetDeviceIds() const {
        std::lock_guard<std::mutex> lock(deviceMutex);
        std::vector<std::string> ids;
        for (const auto& [id, info] : pairedDevices) {
            ids.push_back(id);
        }
        return ids;
    }
    
    SmartHomeDeviceInfo GetDeviceInfo(const std::string& deviceId) const {
        std::lock_guard<std::mutex> lock(deviceMutex);
        auto it = pairedDevices.find(deviceId);
        if (it != pairedDevices.end()) {
            return it->second;
        }
        return SmartHomeDeviceInfo{};
    }
    
    bool HasDevice(const std::string& deviceId) const {
        std::lock_guard<std::mutex> lock(deviceMutex);
        return pairedDevices.find(deviceId) != pairedDevices.end();
    }
    
    // ===== CALLBACKS =====
    
    void SetOnStateChange(OnProtocolStateChange callback) override {
        onStateChange = callback;
    }
    
    void SetOnDeviceDiscover(OnProtocolDeviceDiscover callback) override {
        onDeviceDiscover = callback;
    }
    
    void SetOnDeviceJoin(OnProtocolDeviceJoin callback) override {
        onDeviceJoin = callback;
    }
    
    void SetOnDeviceLeave(OnProtocolDeviceLeave callback) override {
        onDeviceLeave = callback;
    }
    
    void SetOnDeviceUpdate(OnProtocolDeviceUpdate callback) override {
        onDeviceUpdate = callback;
    }
    
    void SetOnNetworkForm(OnProtocolNetworkForm callback) override {
        onNetworkForm = callback;
    }
    
    void SetOnError(OnProtocolError callback) override {
        onError = callback;
    }
    
    // ===== DIAGNOSTICS =====
    
    std::map<std::string, std::string> GetDiagnostics() const override {
        std::map<std::string, std::string> diag;
        diag["protocol"] = protocolName;
        diag["version"] = protocolVersion;
        diag["state"] = StateToString(state);
        diag["discovering"] = discovering ? "true" : "false";
        diag["pairing"] = pairing ? "true" : "false";
        diag["deviceCount"] = std::to_string(pairedDevices.size());
        diag["hasNetwork"] = hasNetwork ? "true" : "false";
        return diag;
    }
    
    void SetLogLevel(int level) override {
        logLevel = level;
    }

protected:
    // ===== STATE MANAGEMENT =====
    
    void SetState(ProtocolState newState) {
        ProtocolState oldState = state;
        state = newState;
        
        if (onStateChange && oldState != newState) {
            onStateChange(newState);
        }
    }
    
    void SetCapability(ProtocolCapability cap, bool enabled = true) {
        if (enabled) {
            capabilities |= static_cast<uint32_t>(cap);
        } else {
            capabilities &= ~static_cast<uint32_t>(cap);
        }
    }
    
    // ===== DEVICE MANAGEMENT HELPERS =====
    
    void AddDiscoveredDevice(const SmartHomeDeviceInfo& device) {
        {
            std::lock_guard<std::mutex> lock(deviceMutex);
            
            // Check for duplicates
            for (const auto& d : discoveredDevices) {
                if (d.DeviceId == device.DeviceId) {
                    return;
                }
            }
            discoveredDevices.push_back(device);
        }
        
        if (onDeviceDiscover) {
            onDeviceDiscover(device);
        }
    }
    
    void ClearDiscoveredDevices() {
        std::lock_guard<std::mutex> lock(deviceMutex);
        discoveredDevices.clear();
    }
    
    void AddPairedDevice(const SmartHomeDeviceInfo& device) {
        {
            std::lock_guard<std::mutex> lock(deviceMutex);
            pairedDevices[device.DeviceId] = device;
        }
        
        if (onDeviceJoin) {
            onDeviceJoin(device);
        }
    }
    
    void RemovePairedDevice(const std::string& deviceId) {
        {
            std::lock_guard<std::mutex> lock(deviceMutex);
            pairedDevices.erase(deviceId);
        }
        
        if (onDeviceLeave) {
            onDeviceLeave(deviceId);
        }
    }
    
    void UpdatePairedDevice(const SmartHomeDeviceInfo& device) {
        {
            std::lock_guard<std::mutex> lock(deviceMutex);
            auto it = pairedDevices.find(device.DeviceId);
            if (it != pairedDevices.end()) {
                it->second = device;
            }
        }
        
        if (onDeviceUpdate) {
            onDeviceUpdate(device.DeviceId);
        }
    }
    
    // ===== NETWORK HELPERS =====
    
    void SetNetworkInfo(const SmartHomeNetworkInfo& info) {
        {
            std::lock_guard<std::mutex> lock(networkMutex);
            networkInfo = info;
            hasNetwork = true;
        }
        
        if (onNetworkForm) {
            onNetworkForm(info);
        }
    }
    
    void ClearNetwork() {
        std::lock_guard<std::mutex> lock(networkMutex);
        networkInfo = SmartHomeNetworkInfo{};
        hasNetwork = false;
    }
    
    // ===== ERROR REPORTING =====
    
    void ReportError(int code, const std::string& message) {
        if (onError) {
            onError(code, message);
        }
        
        if (logLevel >= 1) {
            // Log error (integrate with UltraCanvas logging)
        }
    }
    
    void Log(int level, const std::string& message) {
        if (logLevel >= level) {
            // Log message (integrate with UltraCanvas logging)
        }
    }
    
    // ===== UTILITY =====
    
    static std::string StateToString(ProtocolState s) {
        switch (s) {
            case ProtocolState::Uninitialized: return "Uninitialized";
            case ProtocolState::Initializing: return "Initializing";
            case ProtocolState::Ready: return "Ready";
            case ProtocolState::Discovering: return "Discovering";
            case ProtocolState::Pairing: return "Pairing";
            case ProtocolState::Error: return "Error";
            default: return "Unknown";
        }
    }
    
    std::string GenerateDeviceId() const {
        // Generate unique device ID based on protocol and timestamp
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        return protocolName + "_" + std::to_string(ms);
    }

protected:
    // Protocol identity
    SmartHomeProtocolType protocolType;
    std::string protocolName;
    std::string protocolVersion = "1.0.0";
    
    // State
    std::atomic<ProtocolState> state;
    std::atomic<bool> discovering;
    std::atomic<bool> pairing;
    uint32_t capabilities = 0;
    int logLevel = 2;
    
    // Network
    SmartHomeNetworkInfo networkInfo;
    std::atomic<bool> hasNetwork{false};
    mutable std::mutex networkMutex;
    
    // Devices
    std::vector<SmartHomeDeviceInfo> discoveredDevices;
    std::map<std::string, SmartHomeDeviceInfo> pairedDevices;
    mutable std::mutex deviceMutex;
    
    // Callbacks
    OnProtocolStateChange onStateChange;
    OnProtocolDeviceDiscover onDeviceDiscover;
    OnProtocolDeviceJoin onDeviceJoin;
    OnProtocolDeviceLeave onDeviceLeave;
    OnProtocolDeviceUpdate onDeviceUpdate;
    OnProtocolNetworkForm onNetworkForm;
    OnProtocolError onError;
};

} // namespace SmartHome
} // namespace UltraCanvas
