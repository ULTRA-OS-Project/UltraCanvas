// core/UltraCanvasSmartHomeManager.h
// Smart Home Manager - Central Singleton for Device and Protocol Management
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasSmartHome.h"
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>

namespace UltraCanvas {
namespace SmartHome {

// ===== FORWARD DECLARATIONS =====
class ISmartHomeProtocol;
class ISmartHomeDevice;

// ===== INTERNAL STRUCTURES =====

struct ProtocolRegistration {
    SmartHomeProtocolType Type;
    std::shared_ptr<ISmartHomeProtocol> Protocol;
    bool Enabled = false;
    bool Available = false;
};

struct DeviceRegistration {
    std::string DeviceId;
    std::shared_ptr<ISmartHomeDevice> Device;
    SmartHomeDeviceInfo Info;
    SmartHomeProtocolType Protocol;
};

struct PendingCommand {
    SmartHomeCommand Command;
    std::function<void(bool success)> Callback;
    uint64_t Timestamp;
};

// ===== SMART HOME MANAGER =====

class SmartHomeManager {
public:
    // Singleton access
    static SmartHomeManager& Instance();
    
    // ===== LIFECYCLE =====
    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return initialized; }
    
    // ===== PROTOCOL REGISTRATION =====
    bool RegisterProtocol(SmartHomeProtocolType type, std::shared_ptr<ISmartHomeProtocol> protocol);
    bool UnregisterProtocol(SmartHomeProtocolType type);
    std::shared_ptr<ISmartHomeProtocol> GetProtocol(SmartHomeProtocolType type) const;
    bool EnableProtocol(SmartHomeProtocolType type);
    bool DisableProtocol(SmartHomeProtocolType type);
    bool IsProtocolEnabled(SmartHomeProtocolType type) const;
    bool IsProtocolAvailable(SmartHomeProtocolType type) const;
    std::vector<SmartHomeProtocolType> GetEnabledProtocols() const;
    std::vector<SmartHomeProtocolType> GetAvailableProtocols() const;
    
    // ===== DEVICE REGISTRATION =====
    bool RegisterDevice(const std::string& deviceId, std::shared_ptr<ISmartHomeDevice> device,
                       const SmartHomeDeviceInfo& info, SmartHomeProtocolType protocol);
    bool UnregisterDevice(const std::string& deviceId);
    std::shared_ptr<ISmartHomeDevice> GetDevice(const std::string& deviceId) const;
    SmartHomeDeviceInfo GetDeviceInfo(const std::string& deviceId) const;
    std::vector<SmartHomeDeviceInfo> GetAllDevices() const;
    std::vector<SmartHomeDeviceInfo> GetDevicesByCategory(SmartHomeDeviceCategory category) const;
    std::vector<SmartHomeDeviceInfo> GetDevicesByProtocol(SmartHomeProtocolType protocol) const;
    bool UpdateDeviceInfo(const std::string& deviceId, const SmartHomeDeviceInfo& info);
    bool UpdateDeviceState(const std::string& deviceId, SmartHomeDeviceState state);
    
    // ===== DISCOVERY =====
    bool StartDiscovery(SmartHomeProtocolType protocol = SmartHomeProtocolType::Unknown);
    void StopDiscovery();
    bool IsDiscovering() const { return discovering; }
    std::vector<SmartHomeDeviceInfo> GetDiscoveredDevices() const;
    void ClearDiscoveredDevices();
    
    // ===== PAIRING =====
    bool StartPairing(SmartHomeProtocolType protocol, int timeoutSeconds);
    void StopPairing();
    bool IsPairing() const { return pairing; }
    bool CommissionDevice(const std::string& setupCode);
    
    // ===== COMMAND PROCESSING =====
    bool QueueCommand(const SmartHomeCommand& command, 
                      std::function<void(bool success)> callback = nullptr);
    void ProcessCommandQueue();
    int GetPendingCommandCount() const;
    
    // ===== SCENE MANAGEMENT =====
    bool AddScene(const SmartHomeScene& scene);
    bool UpdateScene(const SmartHomeScene& scene);
    bool RemoveScene(const std::string& sceneId);
    bool ActivateScene(const std::string& sceneId);
    SmartHomeScene GetScene(const std::string& sceneId) const;
    std::vector<SmartHomeScene> GetAllScenes() const;
    
    // ===== AUTOMATION MANAGEMENT =====
    bool AddAutomation(const SmartHomeAutomation& automation);
    bool UpdateAutomation(const SmartHomeAutomation& automation);
    bool RemoveAutomation(const std::string& automationId);
    bool SetAutomationEnabled(const std::string& automationId, bool enabled);
    SmartHomeAutomation GetAutomation(const std::string& automationId) const;
    std::vector<SmartHomeAutomation> GetAllAutomations() const;
    void EvaluateAutomations();
    
    // ===== NETWORK INFO =====
    std::vector<SmartHomeNetworkInfo> GetNetworks() const;
    SmartHomeNetworkInfo GetNetwork(SmartHomeProtocolType protocol) const;
    
    // ===== EVENT CALLBACKS =====
    void SetOnDeviceDiscover(OnDeviceDiscover callback) { onDeviceDiscover = callback; }
    void SetOnDeviceStateChange(OnDeviceStateChange callback) { onDeviceStateChange = callback; }
    void SetOnDeviceUpdate(OnDeviceUpdate callback) { onDeviceUpdate = callback; }
    void SetOnNetworkChange(OnNetworkChange callback) { onNetworkChange = callback; }
    void SetOnPairingProgress(OnPairingProgress callback) { onPairingProgress = callback; }
    void SetOnPairingComplete(OnPairingComplete callback) { onPairingComplete = callback; }
    void SetOnSensorRead(OnSensorRead callback) { onSensorRead = callback; }
    void SetOnAutomationTrigger(OnAutomationTrigger callback) { onAutomationTrigger = callback; }
    void SetOnSmartHomeError(OnSmartHomeError callback) { onSmartHomeError = callback; }
    
    // ===== EVENT DISPATCHING (called by protocols/devices) =====
    void DispatchDeviceDiscover(const SmartHomeDeviceInfo& info);
    void DispatchDeviceStateChange(const std::string& deviceId, SmartHomeDeviceState state);
    void DispatchDeviceUpdate(const std::string& deviceId);
    void DispatchNetworkChange(const SmartHomeNetworkInfo& info);
    void DispatchPairingProgress(int progress, const std::string& status);
    void DispatchPairingComplete(bool success, const SmartHomeDeviceInfo& device);
    void DispatchSensorRead(const std::string& deviceId, const SmartHomeSensorReading& reading);
    void DispatchAutomationTrigger(const std::string& automationId);
    void DispatchError(int code, const std::string& message);
    
    // ===== PERSISTENCE =====
    bool SaveConfiguration(const std::string& path);
    bool LoadConfiguration(const std::string& path);
    
    // ===== STATISTICS =====
    int GetTotalDeviceCount() const;
    int GetOnlineDeviceCount() const;
    int GetOfflineDeviceCount() const;
    
private:
    SmartHomeManager();
    ~SmartHomeManager();
    SmartHomeManager(const SmartHomeManager&) = delete;
    SmartHomeManager& operator=(const SmartHomeManager&) = delete;
    
    // Worker thread
    void WorkerThread();
    void DiscoveryThread();
    void AutomationThread();
    
    // Internal helpers
    bool ExecuteCommand(const SmartHomeCommand& command);
    void NotifyDeviceAdded(const SmartHomeDeviceInfo& info);
    void NotifyDeviceRemoved(const std::string& deviceId);
    
    // State
    std::atomic<bool> initialized{false};
    std::atomic<bool> discovering{false};
    std::atomic<bool> pairing{false};
    std::atomic<bool> running{false};
    
    // Protocol registry
    std::map<SmartHomeProtocolType, ProtocolRegistration> protocols;
    mutable std::mutex protocolMutex;
    
    // Device registry
    std::map<std::string, DeviceRegistration> devices;
    std::vector<SmartHomeDeviceInfo> discoveredDevices;
    mutable std::mutex deviceMutex;
    
    // Command queue
    std::queue<PendingCommand> commandQueue;
    mutable std::mutex commandMutex;
    std::condition_variable commandCondition;
    
    // Scenes and automations
    std::map<std::string, SmartHomeScene> scenes;
    std::map<std::string, SmartHomeAutomation> automations;
    mutable std::mutex sceneMutex;
    mutable std::mutex automationMutex;
    
    // Worker threads
    std::thread workerThread;
    std::thread discoveryThread;
    std::thread automationThread;
    
    // Callbacks
    OnDeviceDiscover onDeviceDiscover;
    OnDeviceStateChange onDeviceStateChange;
    OnDeviceUpdate onDeviceUpdate;
    OnNetworkChange onNetworkChange;
    OnPairingProgress onPairingProgress;
    OnPairingComplete onPairingComplete;
    OnSensorRead onSensorRead;
    OnAutomationTrigger onAutomationTrigger;
    OnSmartHomeError onSmartHomeError;
    
    // Pairing state
    SmartHomeProtocolType pairingProtocol;
    int pairingTimeout;
};

} // namespace SmartHome
} // namespace UltraCanvas
