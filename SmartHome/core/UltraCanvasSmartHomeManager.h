// core/UltraCanvasSmartHomeManager.h
// Smart Home Manager - Central Singleton for Device and Protocol Management
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasSmartHome.h"
#include "SmartHomeAutomationEngine.h"
#include "SmartHomeStore.h"
#include <deque>
#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <functional>
#include <optional>

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
    // Re-reads a device's info from its object and dispatches the update.
    void RefreshDeviceInfo(const std::string& deviceId);
    
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
    // A copy of the open store at `path` / open the store at `path`.
    bool SaveConfiguration(const std::string& path);
    bool LoadConfiguration(const std::string& path);

    // ===== THE STORE (SmartHomeAPI::OpenStore and friends) =====
    bool OpenStore(const std::string& path);
    void CloseStore();
    bool IsStoreOpen() const;
    std::string GetStoreError() const;
    bool ExportStore(const std::string& path) const;
    std::vector<SmartHomeSensorReading> GetSensorHistory(const std::string& deviceId,
                                                         uint64_t startMs, uint64_t endMs) const;

    // ===== THE HOME =====
    bool SetDeviceName(const std::string& deviceId, const std::string& name);
    bool SetDeviceRoom(const std::string& deviceId, const std::string& room);
    std::vector<SmartHomeRoom> GetRooms() const;
    SmartHomeRoom GetRoom(const std::string& roomId) const;
    bool CreateRoom(const SmartHomeRoom& room, std::string* createdId);
    bool UpdateRoom(const SmartHomeRoom& room);
    bool DeleteRoom(const std::string& roomId);
    bool AssignDeviceToRoom(const std::string& deviceId, const std::string& roomId);
    std::string GetDeviceRoom(const std::string& deviceId) const;
    void SetOnHomeChange(OnHomeChange callback);
    bool SetHomeLocation(double latitude, double longitude);
    bool GetHomeLocation(double& latitude, double& longitude) const;
    bool GetSunTimes(int64_t& sunrise, int64_t& sunset) const;
    // A device attribute as the automation engine reads it (see
    // SmartHomeAutomation in UltraCanvasSmartHome.h for the names).
    std::optional<std::string> ReadDeviceAttribute(const std::string& deviceId,
                                                   const std::string& attribute) const;

    // ===== AUTOMATION RUNS AND THE EVENT LOG =====
    bool RunAutomation(const std::string& automationId);
    void RecordEvent(const SmartHomeEvent& event);
    std::vector<SmartHomeEvent> GetEventLog(int64_t since, size_t limit) const;
    void SetOnAutomationNotify(OnAutomationNotify callback);
    void SetOnEvent(OnSmartHomeEvent callback);

    // The clock automations run by, in seconds since the epoch. Tests set
    // one of their own; nullptr goes back to the system clock.
    void SetTimeSource(std::function<int64_t()> source);
    int64_t Now() const;
    
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
    // Registers a device a backend reports, with the backend's device
    // object when it keeps one.
    void AdoptDevice(ISmartHomeProtocol* protocol, const SmartHomeDeviceInfo& info);
    // The person's name and room over the backend's, and the room resolved
    // to one of the home's (a room name seen for the first time becomes a
    // room). Called with no lock held.
    void ApplyOverrides(SmartHomeDeviceInfo& info);
    // Reads a device back from its backend, applies the overrides and
    // stores the result (after its room or a room changed).
    void ReapplyDevice(const std::string& deviceId);
    // Under homeMutex: the room with this name (not minding case), made and
    // saved when there is none; a new id unlike every other.
    std::string RoomForNameLocked(const std::string& name);
    std::string NewRoomIdLocked(const std::string& name) const;
    // Calls OnHomeChange when the rooms changed since the last call.
    void NotifyHomeChange();
    // Sensor history: what a device reads now, when it moved past its
    // deadband or 15 minutes have passed (store open only).
    void RecordReadings(const std::string& deviceId);
    void EnsureAutomationEngine();
    void WakeAutomations();
    
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
    std::atomic<SmartHomeProtocolType> pairingProtocol;  // read on backend threads
    int pairingTimeout;

    // The store, and what of it lives in memory
    std::unique_ptr<SmartHomeStore> store;
    std::map<std::string, SmartHomeDeviceOverride> overrides;   // under deviceMutex
    // The home's rooms. Lock order: deviceMutex, then homeMutex - never the
    // other way round.
    std::vector<SmartHomeRoom> rooms;                           // under homeMutex
    // Rooms the person made or changed while no store was open; the others
    // came from backend names and are made again from them when it opens.
    std::set<std::string> roomsChosen;                          // under homeMutex
    mutable std::mutex homeMutex;
    std::atomic<bool> homeChanged{false};
    mutable std::mutex locationMutex;
    bool hasLocation = false;
    double homeLatitude = 0.0, homeLongitude = 0.0;
    struct RecordedReading { float value = 0; bool triggered = false; uint64_t timeMs = 0; };
    std::map<std::string, RecordedReading> lastRecorded;        // under readingMutex
    std::mutex readingMutex;

    // Automations
    std::unique_ptr<SmartHomeAutomationEngine> engine;
    std::mutex automationWakeMutex;
    std::condition_variable automationWake;
    bool automationWakePending = false;                         // under automationWakeMutex
    mutable std::mutex clockMutex;
    std::function<int64_t()> timeSource;                        // under clockMutex

    // The event log in memory (the store keeps the rest)
    std::deque<SmartHomeEvent> recentEvents;                    // newest first
    mutable std::mutex eventMutex;
    mutable std::mutex callbackMutex;                           // the two below
    OnAutomationNotify onAutomationNotify;
    OnSmartHomeEvent onEvent;
    OnHomeChange onHomeChange;
};

} // namespace SmartHome
} // namespace UltraCanvas
