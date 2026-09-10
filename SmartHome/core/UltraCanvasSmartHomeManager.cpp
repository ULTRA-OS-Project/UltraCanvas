// core/UltraCanvasSmartHomeManager.cpp
// Smart Home Manager - Central Singleton Implementation
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHomeManager.h"
#include "ISmartHomeProtocol.h"
#include "ISmartHomeDevice.h"
#include <chrono>
#include <algorithm>

namespace UltraCanvas {
namespace SmartHome {

// ===== SINGLETON INSTANCE =====

SmartHomeManager& SmartHomeManager::Instance() {
    static SmartHomeManager instance;
    return instance;
}

SmartHomeManager::SmartHomeManager() 
    : pairingProtocol(SmartHomeProtocolType::Unknown)
    , pairingTimeout(60) {
}

SmartHomeManager::~SmartHomeManager() {
    Shutdown();
}

// ===== LIFECYCLE =====

bool SmartHomeManager::Initialize() {
    if (initialized) {
        return true;
    }
    
    running = true;
    
    // Start worker thread for command processing
    workerThread = std::thread(&SmartHomeManager::WorkerThread, this);
    
    // Start automation evaluation thread
    automationThread = std::thread(&SmartHomeManager::AutomationThread, this);
    
    initialized = true;
    return true;
}

void SmartHomeManager::Shutdown() {
    if (!initialized) {
        return;
    }
    
    running = false;
    discovering = false;
    pairing = false;
    
    // Signal command queue to wake up
    commandCondition.notify_all();
    
    // Wait for threads to finish
    if (workerThread.joinable()) {
        workerThread.join();
    }
    if (discoveryThread.joinable()) {
        discoveryThread.join();
    }
    if (automationThread.joinable()) {
        automationThread.join();
    }
    
    // Shutdown all protocols
    {
        std::lock_guard<std::mutex> lock(protocolMutex);
        for (auto& [type, reg] : protocols) {
            if (reg.Protocol && reg.Protocol->IsInitialized()) {
                reg.Protocol->Shutdown();
            }
        }
        protocols.clear();
    }
    
    // Clear devices
    {
        std::lock_guard<std::mutex> lock(deviceMutex);
        devices.clear();
        discoveredDevices.clear();
    }
    
    // Clear scenes and automations
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        scenes.clear();
    }
    {
        std::lock_guard<std::mutex> lock(automationMutex);
        automations.clear();
    }
    
    initialized = false;
}

// ===== PROTOCOL REGISTRATION =====

bool SmartHomeManager::RegisterProtocol(SmartHomeProtocolType type, 
                                         std::shared_ptr<ISmartHomeProtocol> protocol) {
    if (!protocol) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(protocolMutex);
    
    ProtocolRegistration reg;
    reg.Type = type;
    reg.Protocol = protocol;
    reg.Enabled = false;
    reg.Available = protocol->IsHardwareAvailable();
    
    protocols[type] = reg;
    return true;
}

bool SmartHomeManager::UnregisterProtocol(SmartHomeProtocolType type) {
    std::lock_guard<std::mutex> lock(protocolMutex);
    
    auto it = protocols.find(type);
    if (it == protocols.end()) {
        return false;
    }
    
    if (it->second.Protocol && it->second.Protocol->IsInitialized()) {
        it->second.Protocol->Shutdown();
    }
    
    protocols.erase(it);
    return true;
}

std::shared_ptr<ISmartHomeProtocol> SmartHomeManager::GetProtocol(SmartHomeProtocolType type) const {
    std::lock_guard<std::mutex> lock(protocolMutex);
    
    auto it = protocols.find(type);
    if (it != protocols.end()) {
        return it->second.Protocol;
    }
    return nullptr;
}

bool SmartHomeManager::EnableProtocol(SmartHomeProtocolType type) {
    // No backend for this type yet? Build one from its registered factory.
    // Both calls take protocolMutex themselves, so this has to happen before
    // we lock: the mutex is not recursive.
    if (!GetProtocol(type)) {
        if (auto backend = CreateSmartHomeProtocol(type)) {
            RegisterProtocol(type, backend);
        }
    }

    std::lock_guard<std::mutex> lock(protocolMutex);

    auto it = protocols.find(type);
    if (it == protocols.end()) {
        DispatchError(-3, "No backend registered for protocol: " +
                              ProtocolTypeToString(type));
        return false;
    }
    
    if (!it->second.Available) {
        DispatchError(-1, "Protocol hardware not available: " + ProtocolTypeToString(type));
        return false;
    }
    
    if (!it->second.Protocol->IsInitialized()) {
        if (!it->second.Protocol->Initialize()) {
            DispatchError(-2, "Failed to initialize protocol: " + ProtocolTypeToString(type));
            return false;
        }
    }
    
    it->second.Enabled = true;
    
    // Set up protocol callbacks
    it->second.Protocol->SetOnDeviceDiscover([this](const SmartHomeDeviceInfo& device) {
        DispatchDeviceDiscover(device);
    });
    
    it->second.Protocol->SetOnDeviceJoin([this](const SmartHomeDeviceInfo& device) {
        RegisterDevice(device.DeviceId, nullptr, device, device.Protocol);
        DispatchDeviceDiscover(device);
    });
    
    it->second.Protocol->SetOnDeviceLeave([this](const std::string& deviceId) {
        UnregisterDevice(deviceId);
    });
    
    it->second.Protocol->SetOnDeviceUpdate([this](const std::string& deviceId) {
        DispatchDeviceUpdate(deviceId);
    });
    
    it->second.Protocol->SetOnError([this](int code, const std::string& message) {
        DispatchError(code, message);
    });
    
    return true;
}

bool SmartHomeManager::DisableProtocol(SmartHomeProtocolType type) {
    std::lock_guard<std::mutex> lock(protocolMutex);
    
    auto it = protocols.find(type);
    if (it == protocols.end()) {
        return false;
    }
    
    if (it->second.Protocol && it->second.Protocol->IsInitialized()) {
        it->second.Protocol->Shutdown();
    }
    
    it->second.Enabled = false;
    return true;
}

bool SmartHomeManager::IsProtocolEnabled(SmartHomeProtocolType type) const {
    std::lock_guard<std::mutex> lock(protocolMutex);
    
    auto it = protocols.find(type);
    if (it != protocols.end()) {
        return it->second.Enabled;
    }
    return false;
}

bool SmartHomeManager::IsProtocolAvailable(SmartHomeProtocolType type) const {
    std::lock_guard<std::mutex> lock(protocolMutex);
    
    auto it = protocols.find(type);
    if (it != protocols.end()) {
        return it->second.Available;
    }
    return false;
}

std::vector<SmartHomeProtocolType> SmartHomeManager::GetEnabledProtocols() const {
    std::vector<SmartHomeProtocolType> result;
    std::lock_guard<std::mutex> lock(protocolMutex);
    
    for (const auto& [type, reg] : protocols) {
        if (reg.Enabled) {
            result.push_back(type);
        }
    }
    return result;
}

std::vector<SmartHomeProtocolType> SmartHomeManager::GetAvailableProtocols() const {
    std::vector<SmartHomeProtocolType> result;
    std::lock_guard<std::mutex> lock(protocolMutex);
    
    for (const auto& [type, reg] : protocols) {
        if (reg.Available) {
            result.push_back(type);
        }
    }
    return result;
}

// ===== DEVICE REGISTRATION =====

bool SmartHomeManager::RegisterDevice(const std::string& deviceId, 
                                       std::shared_ptr<ISmartHomeDevice> device,
                                       const SmartHomeDeviceInfo& info, 
                                       SmartHomeProtocolType protocol) {
    std::lock_guard<std::mutex> lock(deviceMutex);
    
    DeviceRegistration reg;
    reg.DeviceId = deviceId;
    reg.Device = device;
    reg.Info = info;
    reg.Protocol = protocol;
    
    devices[deviceId] = reg;
    
    NotifyDeviceAdded(info);
    return true;
}

bool SmartHomeManager::UnregisterDevice(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(deviceMutex);
    
    auto it = devices.find(deviceId);
    if (it == devices.end()) {
        return false;
    }
    
    devices.erase(it);
    NotifyDeviceRemoved(deviceId);
    return true;
}

std::shared_ptr<ISmartHomeDevice> SmartHomeManager::GetDevice(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock(deviceMutex);
    
    auto it = devices.find(deviceId);
    if (it != devices.end()) {
        return it->second.Device;
    }
    return nullptr;
}

SmartHomeDeviceInfo SmartHomeManager::GetDeviceInfo(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock(deviceMutex);
    
    auto it = devices.find(deviceId);
    if (it != devices.end()) {
        return it->second.Info;
    }
    return SmartHomeDeviceInfo{};
}

std::vector<SmartHomeDeviceInfo> SmartHomeManager::GetAllDevices() const {
    std::vector<SmartHomeDeviceInfo> result;
    std::lock_guard<std::mutex> lock(deviceMutex);
    
    for (const auto& [id, reg] : devices) {
        result.push_back(reg.Info);
    }
    return result;
}

std::vector<SmartHomeDeviceInfo> SmartHomeManager::GetDevicesByCategory(SmartHomeDeviceCategory category) const {
    std::vector<SmartHomeDeviceInfo> result;
    std::lock_guard<std::mutex> lock(deviceMutex);
    
    for (const auto& [id, reg] : devices) {
        if (reg.Info.Category == category) {
            result.push_back(reg.Info);
        }
    }
    return result;
}

std::vector<SmartHomeDeviceInfo> SmartHomeManager::GetDevicesByProtocol(SmartHomeProtocolType protocol) const {
    std::vector<SmartHomeDeviceInfo> result;
    std::lock_guard<std::mutex> lock(deviceMutex);
    
    for (const auto& [id, reg] : devices) {
        if (reg.Protocol == protocol) {
            result.push_back(reg.Info);
        }
    }
    return result;
}

bool SmartHomeManager::UpdateDeviceInfo(const std::string& deviceId, const SmartHomeDeviceInfo& info) {
    std::lock_guard<std::mutex> lock(deviceMutex);
    
    auto it = devices.find(deviceId);
    if (it == devices.end()) {
        return false;
    }
    
    it->second.Info = info;
    DispatchDeviceUpdate(deviceId);
    return true;
}

bool SmartHomeManager::UpdateDeviceState(const std::string& deviceId, SmartHomeDeviceState state) {
    std::lock_guard<std::mutex> lock(deviceMutex);
    
    auto it = devices.find(deviceId);
    if (it == devices.end()) {
        return false;
    }
    
    SmartHomeDeviceState oldState = it->second.Info.State;
    it->second.Info.State = state;
    
    if (oldState != state) {
        DispatchDeviceStateChange(deviceId, state);
    }
    return true;
}

// ===== DISCOVERY =====

bool SmartHomeManager::StartDiscovery(SmartHomeProtocolType protocol) {
    if (discovering) {
        return false;
    }
    
    discovering = true;
    ClearDiscoveredDevices();
    
    // Start discovery on specified protocol or all enabled protocols
    std::lock_guard<std::mutex> lock(protocolMutex);
    
    for (auto& [type, reg] : protocols) {
        if (reg.Enabled && (protocol == SmartHomeProtocolType::Unknown || protocol == type)) {
            reg.Protocol->StartDiscovery(60);
        }
    }
    
    return true;
}

void SmartHomeManager::StopDiscovery() {
    discovering = false;
    
    std::lock_guard<std::mutex> lock(protocolMutex);
    
    for (auto& [type, reg] : protocols) {
        if (reg.Enabled && reg.Protocol->IsDiscovering()) {
            reg.Protocol->StopDiscovery();
        }
    }
}

std::vector<SmartHomeDeviceInfo> SmartHomeManager::GetDiscoveredDevices() const {
    std::lock_guard<std::mutex> lock(deviceMutex);
    return discoveredDevices;
}

void SmartHomeManager::ClearDiscoveredDevices() {
    std::lock_guard<std::mutex> lock(deviceMutex);
    discoveredDevices.clear();
}

// ===== PAIRING =====

bool SmartHomeManager::StartPairing(SmartHomeProtocolType protocol, int timeoutSeconds) {
    if (pairing) {
        return false;
    }
    
    auto proto = GetProtocol(protocol);
    if (!proto || !IsProtocolEnabled(protocol)) {
        DispatchError(-3, "Protocol not available for pairing");
        return false;
    }
    
    pairingProtocol = protocol;
    pairingTimeout = timeoutSeconds;
    pairing = true;
    
    DispatchPairingProgress(0, "Starting pairing mode...");
    
    return proto->StartPairing(timeoutSeconds);
}

void SmartHomeManager::StopPairing() {
    if (!pairing) {
        return;
    }
    
    auto proto = GetProtocol(pairingProtocol);
    if (proto) {
        proto->StopPairing();
    }
    
    pairing = false;
    pairingProtocol = SmartHomeProtocolType::Unknown;
}

bool SmartHomeManager::CommissionDevice(const std::string& setupCode) {
    if (!pairing || pairingProtocol != SmartHomeProtocolType::Matter) {
        return false;
    }
    
    auto proto = GetProtocol(SmartHomeProtocolType::Matter);
    if (!proto) {
        return false;
    }
    
    auto matterProto = std::dynamic_pointer_cast<IMatterProtocol>(proto);
    if (!matterProto) {
        return false;
    }
    
    return matterProto->CommissionWithCode(setupCode);
}

// ===== COMMAND PROCESSING =====

bool SmartHomeManager::QueueCommand(const SmartHomeCommand& command, 
                                     std::function<void(bool success)> callback) {
    std::lock_guard<std::mutex> lock(commandMutex);
    
    PendingCommand pending;
    pending.Command = command;
    pending.Callback = callback;
    pending.Timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    commandQueue.push(pending);
    commandCondition.notify_one();
    
    return true;
}

void SmartHomeManager::ProcessCommandQueue() {
    std::unique_lock<std::mutex> lock(commandMutex);
    
    while (!commandQueue.empty()) {
        PendingCommand pending = commandQueue.front();
        commandQueue.pop();
        lock.unlock();
        
        bool success = ExecuteCommand(pending.Command);
        
        if (pending.Callback) {
            pending.Callback(success);
        }
        
        lock.lock();
    }
}

int SmartHomeManager::GetPendingCommandCount() const {
    std::lock_guard<std::mutex> lock(commandMutex);
    return static_cast<int>(commandQueue.size());
}

bool SmartHomeManager::ExecuteCommand(const SmartHomeCommand& command) {
    // Find device
    SmartHomeProtocolType protocol;
    {
        std::lock_guard<std::mutex> lock(deviceMutex);
        auto it = devices.find(command.DeviceId);
        if (it == devices.end()) {
            return false;
        }
        protocol = it->second.Protocol;
    }
    
    // Get protocol and send command
    auto proto = GetProtocol(protocol);
    if (!proto) {
        return false;
    }
    
    return proto->SendCommand(command.DeviceId, command.Command, command.Parameters);
}

// ===== SCENE MANAGEMENT =====

bool SmartHomeManager::AddScene(const SmartHomeScene& scene) {
    std::lock_guard<std::mutex> lock(sceneMutex);
    scenes[scene.SceneId] = scene;
    return true;
}

bool SmartHomeManager::UpdateScene(const SmartHomeScene& scene) {
    std::lock_guard<std::mutex> lock(sceneMutex);
    auto it = scenes.find(scene.SceneId);
    if (it == scenes.end()) {
        return false;
    }
    it->second = scene;
    return true;
}

bool SmartHomeManager::RemoveScene(const std::string& sceneId) {
    std::lock_guard<std::mutex> lock(sceneMutex);
    return scenes.erase(sceneId) > 0;
}

bool SmartHomeManager::ActivateScene(const std::string& sceneId) {
    SmartHomeScene scene;
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        auto it = scenes.find(sceneId);
        if (it == scenes.end()) {
            return false;
        }
        scene = it->second;
    }
    
    // Execute all actions in the scene
    for (const auto& action : scene.Actions) {
        QueueCommand(action);
    }
    
    return true;
}

SmartHomeScene SmartHomeManager::GetScene(const std::string& sceneId) const {
    std::lock_guard<std::mutex> lock(sceneMutex);
    auto it = scenes.find(sceneId);
    if (it != scenes.end()) {
        return it->second;
    }
    return SmartHomeScene{};
}

std::vector<SmartHomeScene> SmartHomeManager::GetAllScenes() const {
    std::vector<SmartHomeScene> result;
    std::lock_guard<std::mutex> lock(sceneMutex);
    for (const auto& [id, scene] : scenes) {
        result.push_back(scene);
    }
    return result;
}

// ===== AUTOMATION MANAGEMENT =====

bool SmartHomeManager::AddAutomation(const SmartHomeAutomation& automation) {
    std::lock_guard<std::mutex> lock(automationMutex);
    automations[automation.AutomationId] = automation;
    return true;
}

bool SmartHomeManager::UpdateAutomation(const SmartHomeAutomation& automation) {
    std::lock_guard<std::mutex> lock(automationMutex);
    auto it = automations.find(automation.AutomationId);
    if (it == automations.end()) {
        return false;
    }
    it->second = automation;
    return true;
}

bool SmartHomeManager::RemoveAutomation(const std::string& automationId) {
    std::lock_guard<std::mutex> lock(automationMutex);
    return automations.erase(automationId) > 0;
}

bool SmartHomeManager::SetAutomationEnabled(const std::string& automationId, bool enabled) {
    std::lock_guard<std::mutex> lock(automationMutex);
    auto it = automations.find(automationId);
    if (it == automations.end()) {
        return false;
    }
    it->second.Enabled = enabled;
    return true;
}

SmartHomeAutomation SmartHomeManager::GetAutomation(const std::string& automationId) const {
    std::lock_guard<std::mutex> lock(automationMutex);
    auto it = automations.find(automationId);
    if (it != automations.end()) {
        return it->second;
    }
    return SmartHomeAutomation{};
}

std::vector<SmartHomeAutomation> SmartHomeManager::GetAllAutomations() const {
    std::vector<SmartHomeAutomation> result;
    std::lock_guard<std::mutex> lock(automationMutex);
    for (const auto& [id, auto_] : automations) {
        result.push_back(auto_);
    }
    return result;
}

void SmartHomeManager::EvaluateAutomations() {
    std::vector<SmartHomeAutomation> activeAutomations;
    {
        std::lock_guard<std::mutex> lock(automationMutex);
        for (const auto& [id, auto_] : automations) {
            if (auto_.Enabled) {
                activeAutomations.push_back(auto_);
            }
        }
    }
    
    // TODO: Implement automation trigger evaluation
    // This would check time-based triggers, device state triggers, etc.
}

// ===== NETWORK INFO =====

std::vector<SmartHomeNetworkInfo> SmartHomeManager::GetNetworks() const {
    std::vector<SmartHomeNetworkInfo> result;
    std::lock_guard<std::mutex> lock(protocolMutex);
    
    for (const auto& [type, reg] : protocols) {
        if (reg.Enabled && reg.Protocol->HasNetwork()) {
            result.push_back(reg.Protocol->GetNetworkInfo());
        }
    }
    return result;
}

SmartHomeNetworkInfo SmartHomeManager::GetNetwork(SmartHomeProtocolType protocol) const {
    auto proto = GetProtocol(protocol);
    if (proto && proto->HasNetwork()) {
        return proto->GetNetworkInfo();
    }
    return SmartHomeNetworkInfo{};
}

// ===== EVENT DISPATCHING =====

void SmartHomeManager::DispatchDeviceDiscover(const SmartHomeDeviceInfo& info) {
    {
        std::lock_guard<std::mutex> lock(deviceMutex);
        discoveredDevices.push_back(info);
    }
    
    if (onDeviceDiscover) {
        onDeviceDiscover(info);
    }
}

void SmartHomeManager::DispatchDeviceStateChange(const std::string& deviceId, SmartHomeDeviceState state) {
    if (onDeviceStateChange) {
        onDeviceStateChange(deviceId, state);
    }
}

void SmartHomeManager::DispatchDeviceUpdate(const std::string& deviceId) {
    if (onDeviceUpdate) {
        onDeviceUpdate(deviceId);
    }
}

void SmartHomeManager::DispatchNetworkChange(const SmartHomeNetworkInfo& info) {
    if (onNetworkChange) {
        onNetworkChange(info);
    }
}

void SmartHomeManager::DispatchPairingProgress(int progress, const std::string& status) {
    if (onPairingProgress) {
        onPairingProgress(progress, status);
    }
}

void SmartHomeManager::DispatchPairingComplete(bool success, const SmartHomeDeviceInfo& device) {
    pairing = false;
    
    if (onPairingComplete) {
        onPairingComplete(success, device);
    }
}

void SmartHomeManager::DispatchSensorRead(const std::string& deviceId, const SmartHomeSensorReading& reading) {
    if (onSensorRead) {
        onSensorRead(deviceId, reading);
    }
}

void SmartHomeManager::DispatchAutomationTrigger(const std::string& automationId) {
    if (onAutomationTrigger) {
        onAutomationTrigger(automationId);
    }
}

void SmartHomeManager::DispatchError(int code, const std::string& message) {
    if (onSmartHomeError) {
        onSmartHomeError(code, message);
    }
}

// ===== PERSISTENCE =====

bool SmartHomeManager::SaveConfiguration(const std::string& path) {
    // TODO: Implement JSON serialization of devices, scenes, automations
    return false;
}

bool SmartHomeManager::LoadConfiguration(const std::string& path) {
    // TODO: Implement JSON deserialization
    return false;
}

// ===== STATISTICS =====

int SmartHomeManager::GetTotalDeviceCount() const {
    std::lock_guard<std::mutex> lock(deviceMutex);
    return static_cast<int>(devices.size());
}

int SmartHomeManager::GetOnlineDeviceCount() const {
    std::lock_guard<std::mutex> lock(deviceMutex);
    int count = 0;
    for (const auto& [id, reg] : devices) {
        if (reg.Info.State == SmartHomeDeviceState::Online) {
            count++;
        }
    }
    return count;
}

int SmartHomeManager::GetOfflineDeviceCount() const {
    std::lock_guard<std::mutex> lock(deviceMutex);
    int count = 0;
    for (const auto& [id, reg] : devices) {
        if (reg.Info.State == SmartHomeDeviceState::Offline) {
            count++;
        }
    }
    return count;
}

// ===== WORKER THREADS =====

void SmartHomeManager::WorkerThread() {
    while (running) {
        std::unique_lock<std::mutex> lock(commandMutex);
        
        commandCondition.wait_for(lock, std::chrono::milliseconds(100), [this] {
            return !commandQueue.empty() || !running;
        });
        
        if (!running) {
            break;
        }
        
        lock.unlock();
        ProcessCommandQueue();
    }
}

void SmartHomeManager::DiscoveryThread() {
    while (running) {
        if (discovering) {
            // Check discovery status and update discovered devices
            std::lock_guard<std::mutex> lock(protocolMutex);
            
            bool anyDiscovering = false;
            for (const auto& [type, reg] : protocols) {
                if (reg.Enabled && reg.Protocol->IsDiscovering()) {
                    anyDiscovering = true;
                    
                    auto discovered = reg.Protocol->GetDiscoveredDevices();
                    // Merge with our discovered list (handled via callbacks)
                }
            }
            
            if (!anyDiscovering) {
                discovering = false;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void SmartHomeManager::AutomationThread() {
    while (running) {
        EvaluateAutomations();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ===== INTERNAL HELPERS =====

void SmartHomeManager::NotifyDeviceAdded(const SmartHomeDeviceInfo& info) {
    DispatchDeviceDiscover(info);
}

void SmartHomeManager::NotifyDeviceRemoved(const std::string& deviceId) {
    DispatchDeviceStateChange(deviceId, SmartHomeDeviceState::Offline);
}

// ===== SMART HOME API IMPLEMENTATION =====

class SmartHomeAPI::Impl {
public:
    // Just forwards to SmartHomeManager
};

SmartHomeAPI& SmartHomeAPI::Instance() {
    static SmartHomeAPI instance;
    return instance;
}

SmartHomeAPI::SmartHomeAPI() : pImpl(std::make_unique<Impl>()) {}
SmartHomeAPI::~SmartHomeAPI() = default;

bool SmartHomeAPI::Initialize() {
    // Make every backend compiled into this build available to
    // EnableProtocol() before the manager starts its threads.
    RegisterBuiltinProtocols();
    return SmartHomeManager::Instance().Initialize();
}

bool SmartHomeAPI::RegisterProtocol(SmartHomeProtocolType protocol,
                                    std::shared_ptr<ISmartHomeProtocol> backend) {
    return SmartHomeManager::Instance().RegisterProtocol(protocol, std::move(backend));
}

bool SmartHomeAPI::UnregisterProtocol(SmartHomeProtocolType protocol) {
    return SmartHomeManager::Instance().UnregisterProtocol(protocol);
}

bool SmartHomeAPI::HasProtocolBackend(SmartHomeProtocolType protocol) const {
    return SmartHomeManager::Instance().GetProtocol(protocol) != nullptr;
}
void SmartHomeAPI::Shutdown() { SmartHomeManager::Instance().Shutdown(); }
bool SmartHomeAPI::IsInitialized() const { return SmartHomeManager::Instance().IsInitialized(); }

bool SmartHomeAPI::EnableProtocol(SmartHomeProtocolType protocol) { return SmartHomeManager::Instance().EnableProtocol(protocol); }
bool SmartHomeAPI::DisableProtocol(SmartHomeProtocolType protocol) { return SmartHomeManager::Instance().DisableProtocol(protocol); }
bool SmartHomeAPI::IsProtocolEnabled(SmartHomeProtocolType protocol) const { return SmartHomeManager::Instance().IsProtocolEnabled(protocol); }
bool SmartHomeAPI::IsProtocolAvailable(SmartHomeProtocolType protocol) const { return SmartHomeManager::Instance().IsProtocolAvailable(protocol); }
std::vector<SmartHomeProtocolType> SmartHomeAPI::GetEnabledProtocols() const { return SmartHomeManager::Instance().GetEnabledProtocols(); }
std::vector<SmartHomeProtocolType> SmartHomeAPI::GetAvailableProtocols() const { return SmartHomeManager::Instance().GetAvailableProtocols(); }

bool SmartHomeAPI::StartDiscovery(SmartHomeProtocolType protocol) { return SmartHomeManager::Instance().StartDiscovery(protocol); }
void SmartHomeAPI::StopDiscovery() { SmartHomeManager::Instance().StopDiscovery(); }
bool SmartHomeAPI::IsDiscovering() const { return SmartHomeManager::Instance().IsDiscovering(); }
std::vector<SmartHomeDeviceInfo> SmartHomeAPI::GetDiscoveredDevices() const { return SmartHomeManager::Instance().GetDiscoveredDevices(); }

std::vector<SmartHomeDeviceInfo> SmartHomeAPI::GetDevices() const { return SmartHomeManager::Instance().GetAllDevices(); }
std::vector<SmartHomeDeviceInfo> SmartHomeAPI::GetDevicesByCategory(SmartHomeDeviceCategory category) const { return SmartHomeManager::Instance().GetDevicesByCategory(category); }
std::vector<SmartHomeDeviceInfo> SmartHomeAPI::GetDevicesByProtocol(SmartHomeProtocolType protocol) const { return SmartHomeManager::Instance().GetDevicesByProtocol(protocol); }
SmartHomeDeviceInfo SmartHomeAPI::GetDevice(const std::string& deviceId) const { return SmartHomeManager::Instance().GetDeviceInfo(deviceId); }
bool SmartHomeAPI::RemoveDevice(const std::string& deviceId) { return SmartHomeManager::Instance().UnregisterDevice(deviceId); }

bool SmartHomeAPI::RenameDevice(const std::string& deviceId, const std::string& newName) {
    auto info = SmartHomeManager::Instance().GetDeviceInfo(deviceId);
    info.Name = newName;
    return SmartHomeManager::Instance().UpdateDeviceInfo(deviceId, info);
}

bool SmartHomeAPI::StartPairing(SmartHomeProtocolType protocol, int timeoutSeconds) { return SmartHomeManager::Instance().StartPairing(protocol, timeoutSeconds); }
void SmartHomeAPI::StopPairing() { SmartHomeManager::Instance().StopPairing(); }
bool SmartHomeAPI::IsPairing() const { return SmartHomeManager::Instance().IsPairing(); }
bool SmartHomeAPI::CommissionDevice(const std::string& setupCode) { return SmartHomeManager::Instance().CommissionDevice(setupCode); }

bool SmartHomeAPI::SendCommand(const SmartHomeCommand& command) { return SmartHomeManager::Instance().QueueCommand(command); }

bool SmartHomeAPI::SetLightState(const std::string& deviceId, const SmartHomeLightState& state) {
    SmartHomeCommand cmd;
    cmd.DeviceId = deviceId;
    cmd.Command = "setLight";
    cmd.Parameters["on"] = state.On ? "true" : "false";
    cmd.Parameters["brightness"] = std::to_string(state.Brightness);
    cmd.Parameters["colorTemp"] = std::to_string(state.ColorTemp);
    cmd.Parameters["r"] = std::to_string(state.Red);
    cmd.Parameters["g"] = std::to_string(state.Green);
    cmd.Parameters["b"] = std::to_string(state.Blue);
    return SendCommand(cmd);
}

bool SmartHomeAPI::SetThermostatTarget(const std::string& deviceId, float temperature) {
    SmartHomeCommand cmd;
    cmd.DeviceId = deviceId;
    cmd.Command = "setTargetTemp";
    cmd.Parameters["temperature"] = std::to_string(temperature);
    return SendCommand(cmd);
}

bool SmartHomeAPI::SetThermostatMode(const std::string& deviceId, const std::string& mode) {
    SmartHomeCommand cmd;
    cmd.DeviceId = deviceId;
    cmd.Command = "setMode";
    cmd.Parameters["mode"] = mode;
    return SendCommand(cmd);
}

bool SmartHomeAPI::SetLockState(const std::string& deviceId, bool locked) {
    SmartHomeCommand cmd;
    cmd.DeviceId = deviceId;
    cmd.Command = locked ? "lock" : "unlock";
    return SendCommand(cmd);
}

bool SmartHomeAPI::SetSwitchState(const std::string& deviceId, bool on) {
    SmartHomeCommand cmd;
    cmd.DeviceId = deviceId;
    cmd.Command = on ? "turnOn" : "turnOff";
    return SendCommand(cmd);
}

bool SmartHomeAPI::SetBlindPosition(const std::string& deviceId, uint8_t position) {
    SmartHomeCommand cmd;
    cmd.DeviceId = deviceId;
    cmd.Command = "setPosition";
    cmd.Parameters["position"] = std::to_string(position);
    return SendCommand(cmd);
}

// State queries - would need device-specific implementation
SmartHomeLightState SmartHomeAPI::GetLightState(const std::string& deviceId) const { return SmartHomeLightState{}; }
SmartHomeThermostatState SmartHomeAPI::GetThermostatState(const std::string& deviceId) const { return SmartHomeThermostatState{}; }
SmartHomeLockState SmartHomeAPI::GetLockState(const std::string& deviceId) const { return SmartHomeLockState{}; }
bool SmartHomeAPI::GetSwitchState(const std::string& deviceId) const { return false; }
SmartHomeSensorReading SmartHomeAPI::GetSensorReading(const std::string& deviceId) const { return SmartHomeSensorReading{}; }
std::vector<SmartHomeSensorReading> SmartHomeAPI::GetSensorHistory(const std::string& deviceId, uint64_t startTime, uint64_t endTime) const { return {}; }

// Scenes
bool SmartHomeAPI::CreateScene(const SmartHomeScene& scene) { return SmartHomeManager::Instance().AddScene(scene); }
bool SmartHomeAPI::UpdateScene(const SmartHomeScene& scene) { return SmartHomeManager::Instance().UpdateScene(scene); }
bool SmartHomeAPI::DeleteScene(const std::string& sceneId) { return SmartHomeManager::Instance().RemoveScene(sceneId); }
bool SmartHomeAPI::ActivateScene(const std::string& sceneId) { return SmartHomeManager::Instance().ActivateScene(sceneId); }
std::vector<SmartHomeScene> SmartHomeAPI::GetScenes() const { return SmartHomeManager::Instance().GetAllScenes(); }
SmartHomeScene SmartHomeAPI::GetScene(const std::string& sceneId) const { return SmartHomeManager::Instance().GetScene(sceneId); }

// Automations
bool SmartHomeAPI::CreateAutomation(const SmartHomeAutomation& automation) { return SmartHomeManager::Instance().AddAutomation(automation); }
bool SmartHomeAPI::UpdateAutomation(const SmartHomeAutomation& automation) { return SmartHomeManager::Instance().UpdateAutomation(automation); }
bool SmartHomeAPI::DeleteAutomation(const std::string& automationId) { return SmartHomeManager::Instance().RemoveAutomation(automationId); }
bool SmartHomeAPI::EnableAutomation(const std::string& automationId, bool enable) { return SmartHomeManager::Instance().SetAutomationEnabled(automationId, enable); }
std::vector<SmartHomeAutomation> SmartHomeAPI::GetAutomations() const { return SmartHomeManager::Instance().GetAllAutomations(); }
SmartHomeAutomation SmartHomeAPI::GetAutomation(const std::string& automationId) const { return SmartHomeManager::Instance().GetAutomation(automationId); }

// Network
std::vector<SmartHomeNetworkInfo> SmartHomeAPI::GetNetworks() const { return SmartHomeManager::Instance().GetNetworks(); }
SmartHomeNetworkInfo SmartHomeAPI::GetNetwork(SmartHomeProtocolType protocol) const { return SmartHomeManager::Instance().GetNetwork(protocol); }

// Callbacks
void SmartHomeAPI::SetOnDeviceDiscover(OnDeviceDiscover callback) { SmartHomeManager::Instance().SetOnDeviceDiscover(callback); }
void SmartHomeAPI::SetOnDeviceStateChange(OnDeviceStateChange callback) { SmartHomeManager::Instance().SetOnDeviceStateChange(callback); }
void SmartHomeAPI::SetOnDeviceUpdate(OnDeviceUpdate callback) { SmartHomeManager::Instance().SetOnDeviceUpdate(callback); }
void SmartHomeAPI::SetOnNetworkChange(OnNetworkChange callback) { SmartHomeManager::Instance().SetOnNetworkChange(callback); }
void SmartHomeAPI::SetOnPairingProgress(OnPairingProgress callback) { SmartHomeManager::Instance().SetOnPairingProgress(callback); }
void SmartHomeAPI::SetOnPairingComplete(OnPairingComplete callback) { SmartHomeManager::Instance().SetOnPairingComplete(callback); }
void SmartHomeAPI::SetOnSensorRead(OnSensorRead callback) { SmartHomeManager::Instance().SetOnSensorRead(callback); }
void SmartHomeAPI::SetOnAutomationTrigger(OnAutomationTrigger callback) { SmartHomeManager::Instance().SetOnAutomationTrigger(callback); }
void SmartHomeAPI::SetOnSmartHomeError(OnSmartHomeError callback) { SmartHomeManager::Instance().SetOnSmartHomeError(callback); }

// ===== UTILITY FUNCTIONS =====

std::string ProtocolTypeToString(SmartHomeProtocolType type) {
    switch (type) {
        case SmartHomeProtocolType::Matter: return "Matter";
        case SmartHomeProtocolType::Thread: return "Thread";
        case SmartHomeProtocolType::Zigbee: return "Zigbee";
        case SmartHomeProtocolType::ZWave: return "Z-Wave";
        case SmartHomeProtocolType::KNX: return "KNX";
        case SmartHomeProtocolType::WiFi: return "WiFi";
        case SmartHomeProtocolType::Bluetooth: return "Bluetooth";
        default: return "Unknown";
    }
}

SmartHomeProtocolType StringToProtocolType(const std::string& str) {
    if (str == "Matter") return SmartHomeProtocolType::Matter;
    if (str == "Thread") return SmartHomeProtocolType::Thread;
    if (str == "Zigbee") return SmartHomeProtocolType::Zigbee;
    if (str == "Z-Wave") return SmartHomeProtocolType::ZWave;
    if (str == "KNX") return SmartHomeProtocolType::KNX;
    if (str == "WiFi") return SmartHomeProtocolType::WiFi;
    if (str == "Bluetooth") return SmartHomeProtocolType::Bluetooth;
    return SmartHomeProtocolType::Unknown;
}

std::string DeviceCategoryToString(SmartHomeDeviceCategory category) {
    switch (category) {
        case SmartHomeDeviceCategory::Light: return "Light";
        case SmartHomeDeviceCategory::Switch: return "Switch";
        case SmartHomeDeviceCategory::Thermostat: return "Thermostat";
        case SmartHomeDeviceCategory::Lock: return "Lock";
        case SmartHomeDeviceCategory::Sensor: return "Sensor";
        case SmartHomeDeviceCategory::Camera: return "Camera";
        case SmartHomeDeviceCategory::Speaker: return "Speaker";
        case SmartHomeDeviceCategory::Plug: return "Plug";
        case SmartHomeDeviceCategory::Blind: return "Blind";
        case SmartHomeDeviceCategory::Fan: return "Fan";
        case SmartHomeDeviceCategory::Appliance: return "Appliance";
        case SmartHomeDeviceCategory::Gateway: return "Gateway";
        default: return "Unknown";
    }
}

SmartHomeDeviceCategory StringToDeviceCategory(const std::string& str) {
    if (str == "Light") return SmartHomeDeviceCategory::Light;
    if (str == "Switch") return SmartHomeDeviceCategory::Switch;
    if (str == "Thermostat") return SmartHomeDeviceCategory::Thermostat;
    if (str == "Lock") return SmartHomeDeviceCategory::Lock;
    if (str == "Sensor") return SmartHomeDeviceCategory::Sensor;
    if (str == "Camera") return SmartHomeDeviceCategory::Camera;
    if (str == "Speaker") return SmartHomeDeviceCategory::Speaker;
    if (str == "Plug") return SmartHomeDeviceCategory::Plug;
    if (str == "Blind") return SmartHomeDeviceCategory::Blind;
    if (str == "Fan") return SmartHomeDeviceCategory::Fan;
    if (str == "Appliance") return SmartHomeDeviceCategory::Appliance;
    if (str == "Gateway") return SmartHomeDeviceCategory::Gateway;
    return SmartHomeDeviceCategory::Unknown;
}

std::string DeviceStateToString(SmartHomeDeviceState state) {
    switch (state) {
        case SmartHomeDeviceState::Online: return "Online";
        case SmartHomeDeviceState::Offline: return "Offline";
        case SmartHomeDeviceState::Pairing: return "Pairing";
        case SmartHomeDeviceState::Updating: return "Updating";
        case SmartHomeDeviceState::Error: return "Error";
        default: return "Unknown";
    }
}

std::string SensorTypeToString(SmartHomeSensorType type) {
    switch (type) {
        case SmartHomeSensorType::Temperature: return "Temperature";
        case SmartHomeSensorType::Humidity: return "Humidity";
        case SmartHomeSensorType::Motion: return "Motion";
        case SmartHomeSensorType::Contact: return "Contact";
        case SmartHomeSensorType::Smoke: return "Smoke";
        case SmartHomeSensorType::CarbonMonoxide: return "CarbonMonoxide";
        case SmartHomeSensorType::Water: return "Water";
        case SmartHomeSensorType::Light: return "Light";
        case SmartHomeSensorType::Pressure: return "Pressure";
        case SmartHomeSensorType::AirQuality: return "AirQuality";
        case SmartHomeSensorType::Occupancy: return "Occupancy";
        case SmartHomeSensorType::Vibration: return "Vibration";
        default: return "Unknown";
    }
}

} // namespace SmartHome
} // namespace UltraCanvas
