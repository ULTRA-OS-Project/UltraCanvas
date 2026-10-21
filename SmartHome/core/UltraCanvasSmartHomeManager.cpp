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
#include <cmath>
#include <locale>
#include <set>
#include <sstream>
#include "UltraCanvasTextUtils.h"   // TryParseFloat / FormatFloatClassic

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
    EnsureAutomationEngine();
    
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
    WakeAutomations();
    
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
        overrides.clear();
        std::lock_guard<std::mutex> home(homeMutex);
        rooms.clear();
        roomsChosen.clear();
        homeChanged = false;
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
    CloseStore();
    // The threads are gone; what the engine remembered goes with them.
    engine.reset();
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        recentEvents.clear();
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

    std::unique_lock<std::mutex> lock(protocolMutex);

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

    // The registry holds the backend, so the callbacks hold it raw: a
    // shared_ptr captured here would keep the backend alive from inside
    // itself.
    ISmartHomeProtocol* proto = it->second.Protocol.get();
    std::shared_ptr<ISmartHomeProtocol> backend = it->second.Protocol;

    // Set up protocol callbacks
    proto->SetOnDeviceDiscover([this](const SmartHomeDeviceInfo& device) {
        DispatchDeviceDiscover(device);
    });

    // RegisterDevice reports the join through DispatchDeviceDiscover itself.
    proto->SetOnDeviceJoin([this, proto, type](const SmartHomeDeviceInfo& device) {
        AdoptDevice(proto, device);
        RecordEvent({Now(), "device", device.DeviceId, "", device.Name + " joined the home"});
        if (pairing && pairingProtocol == type) {
            DispatchPairingProgress(100, "Added " + device.Name);
            DispatchPairingComplete(true, GetDeviceInfo(device.DeviceId));
        }
    });

    proto->SetOnDeviceLeave([this](const std::string& deviceId) {
        const std::string name = GetDeviceInfo(deviceId).Name;
        if (UnregisterDevice(deviceId)) RecordEvent({Now(), "device", deviceId, "", name + " left the home"});
    });

    // A backend reports that something about a device changed; the info the
    // registry holds (state, reachability, last seen) is re-read from the
    // device so GetDevices() does not go stale.
    proto->SetOnDeviceUpdate([this](const std::string& deviceId) {
        RefreshDeviceInfo(deviceId);
    });

    proto->SetOnError([this](int code, const std::string& message) {
        DispatchError(code, message);
    });

    lock.unlock();

    // A backend may come up with devices already paired - one that resumes
    // a saved network does, and so does the simulated home. Those never
    // join, so nothing would ever register them: adopt them now. The
    // registry lock is released first, since RegisterDevice dispatches.
    for (const SmartHomeDeviceInfo& info : backend->GetPairedDevices()) {
        AdoptDevice(proto, info);
    }

    return true;
}

bool SmartHomeManager::DisableProtocol(SmartHomeProtocolType type) {
    std::unique_lock<std::mutex> lock(protocolMutex);
    
    auto it = protocols.find(type);
    if (it == protocols.end()) {
        return false;
    }
    
    if (it->second.Protocol && it->second.Protocol->IsInitialized()) {
        it->second.Protocol->Shutdown();
    }
    
    it->second.Enabled = false;
    lock.unlock();

    // Its devices went with it: a device whose backend is shut down cannot
    // be controlled, and its object may no longer be safe to query.
    std::vector<std::string> gone;
    {
        std::lock_guard<std::mutex> deviceLock(deviceMutex);
        for (const auto& [id, reg] : devices) {
            if (reg.Protocol == type) gone.push_back(id);
        }
    }
    for (const std::string& id : gone) UnregisterDevice(id);
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
    {
        std::lock_guard<std::mutex> lock(deviceMutex);

        DeviceRegistration reg;
        reg.DeviceId = deviceId;
        reg.Device = device;
        reg.Info = info;
        reg.Protocol = protocol;

        devices[deviceId] = reg;
    }
    // Outside the lock: DispatchDeviceDiscover takes deviceMutex itself (it
    // used to be called with it held, which deadlocked the first device that
    // ever joined), and an application callback may call straight back into
    // the API.
    NotifyDeviceAdded(info);
    return true;
}

bool SmartHomeManager::UnregisterDevice(const std::string& deviceId) {
    {
        std::lock_guard<std::mutex> lock(deviceMutex);

        auto it = devices.find(deviceId);
        if (it == devices.end()) {
            return false;
        }

        devices.erase(it);
    }
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
    SmartHomeDeviceState oldState;
    {
        std::lock_guard<std::mutex> lock(deviceMutex);

        auto it = devices.find(deviceId);
        if (it == devices.end()) {
            return false;
        }

        oldState = it->second.Info.State;
        it->second.Info = info;
    }
    // Callbacks run with no lock held, so they may query the API.
    if (oldState != info.State) {
        DispatchDeviceStateChange(deviceId, info.State);
    }
    DispatchDeviceUpdate(deviceId);
    return true;
}

bool SmartHomeManager::UpdateDeviceState(const std::string& deviceId, SmartHomeDeviceState state) {
    SmartHomeDeviceState oldState;
    {
        std::lock_guard<std::mutex> lock(deviceMutex);

        auto it = devices.find(deviceId);
        if (it == devices.end()) {
            return false;
        }

        oldState = it->second.Info.State;
        it->second.Info.State = state;
    }
    if (oldState != state) {
        DispatchDeviceStateChange(deviceId, state);
    }
    return true;
}

void SmartHomeManager::RefreshDeviceInfo(const std::string& deviceId) {
    std::shared_ptr<ISmartHomeDevice> device = GetDevice(deviceId);
    if (!device) {
        // A backend that keeps no device objects has nothing fresher to say
        // than what it already reported; pass the update on as it stands.
        DispatchDeviceUpdate(deviceId);
        WakeAutomations();
        return;
    }
    SmartHomeDeviceInfo info = device->GetDeviceInfo();
    ApplyOverrides(info);
    const SmartHomeDeviceInfo before = GetDeviceInfo(deviceId);
    UpdateDeviceInfo(deviceId, info);
    NotifyHomeChange();
    if (!before.DeviceId.empty() && before.Reachable != info.Reachable) {
        RecordEvent({Now(), "device", deviceId, "", info.Name + (info.Reachable ? " is back online" : " stopped answering")});
    }
    RecordReadings(deviceId);
    // A device trigger answers at once, not at the next second.
    WakeAutomations();
}

void SmartHomeManager::ApplyOverrides(SmartHomeDeviceInfo& info) {
    std::lock_guard<std::mutex> lock(deviceMutex);
    std::string chosen;   // the person's room id, kNoRoomOverride, or "" for the backend's
    if (auto it = overrides.find(info.DeviceId); it != overrides.end()) {
        if (!it->second.Name.empty()) info.Name = it->second.Name;
        chosen = it->second.RoomId;
    }
    std::lock_guard<std::mutex> home(homeMutex);
    auto byId = [this](const std::string& id) -> const SmartHomeRoom* {
        for (const SmartHomeRoom& room : rooms)
            if (room.RoomId == id) return &room;
        return nullptr;
    };
    const SmartHomeRoom* room = nullptr;
    if (chosen == kNoRoomOverride) {
        room = nullptr;
    } else if (!chosen.empty() && (room = byId(chosen))) {
        // the person's room
    } else if (!info.RoomId.empty() && (room = byId(info.RoomId))) {
        // already resolved (a backend that keeps no device objects hands
        // back what the registry stored)
    } else if (!info.Room.empty()) {
        room = byId(RoomForNameLocked(info.Room));
    }
    info.RoomId = room ? room->RoomId : std::string();
    info.Room = room ? room->Name : std::string();
}

void SmartHomeManager::ReapplyDevice(const std::string& deviceId) {
    std::shared_ptr<ISmartHomeDevice> device = GetDevice(deviceId);
    SmartHomeDeviceInfo info = device ? device->GetDeviceInfo() : GetDeviceInfo(deviceId);
    if (info.DeviceId.empty()) return;
    // A fresh read carries the backend's room name only; drop what the
    // registry resolved before, so the override decides again.
    if (device) info.RoomId.clear();
    ApplyOverrides(info);
    UpdateDeviceInfo(deviceId, info);
}

namespace {

std::string LowerAscii(std::string text) {
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

} // namespace

std::string SmartHomeManager::NewRoomIdLocked(const std::string& name) const {
    // "Living room" -> "living-room"; bytes outside ASCII letters and
    // digits (accents, other scripts) become dashes, collapsed.
    std::string slug;
    for (char c : LowerAscii(name)) {
        const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (keep) slug += c;
        else if (!slug.empty() && slug.back() != '-') slug += '-';
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    if (slug.empty()) slug = "room";
    auto taken = [this](const std::string& id) {
        for (const SmartHomeRoom& room : rooms)
            if (room.RoomId == id) return true;
        return id == kNoRoomOverride;
    };
    std::string id = slug;
    for (int n = 2; taken(id); ++n) id = slug + "-" + std::to_string(n);
    return id;
}

std::string SmartHomeManager::RoomForNameLocked(const std::string& name) {
    const std::string key = LowerAscii(name);
    for (const SmartHomeRoom& room : rooms)
        if (LowerAscii(room.Name) == key) return room.RoomId;
    SmartHomeRoom room;
    room.RoomId = NewRoomIdLocked(name);
    room.Name = name;
    int last = 0;
    for (const SmartHomeRoom& r : rooms) last = std::max(last, r.SortOrder);
    room.SortOrder = last + 1;
    rooms.push_back(room);
    if (store) store->SaveRoom(room);
    homeChanged = true;
    return room.RoomId;
}

void SmartHomeManager::NotifyHomeChange() {
    if (!homeChanged.exchange(false)) return;
    Notify(&SmartHomeListener::HomeChange);
}

void SmartHomeManager::AdoptDevice(ISmartHomeProtocol* protocol, const SmartHomeDeviceInfo& info) {
    // The object behind the info, where the backend keeps one: it is what
    // the state queries read (GetLightState and the rest) and what a rename
    // writes to.
    std::shared_ptr<ISmartHomeDevice> device = protocol ? protocol->GetDevice(info.DeviceId) : nullptr;
    SmartHomeDeviceInfo current = device ? device->GetDeviceInfo() : info;
    ApplyOverrides(current);
    RegisterDevice(info.DeviceId, device, current, info.Protocol);
    NotifyHomeChange();
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
        ++commandsRunning;
        lock.unlock();
        
        bool success = ExecuteCommand(pending.Command);
        
        if (pending.Callback) {
            pending.Callback(success);
        }
        
        lock.lock();
        --commandsRunning;
    }
    commandsIdle.notify_all();
}

bool SmartHomeManager::WaitForCommands(int timeoutMs) {
    std::unique_lock<std::mutex> lock(commandMutex);
    return commandsIdle.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                 [this] { return commandQueue.empty() && commandsRunning == 0; });
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

// Every change is written through to the store when one is open; the
// in-memory maps stay the source the engine reads.

bool SmartHomeManager::AddScene(const SmartHomeScene& scene) {
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        scenes[scene.SceneId] = scene;
    }
    if (store) store->SaveScene(scene);
    return true;
}

bool SmartHomeManager::UpdateScene(const SmartHomeScene& scene) {
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        auto it = scenes.find(scene.SceneId);
        if (it == scenes.end()) {
            return false;
        }
        it->second = scene;
    }
    if (store) store->SaveScene(scene);
    return true;
}

bool SmartHomeManager::RemoveScene(const std::string& sceneId) {
    bool removed;
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        removed = scenes.erase(sceneId) > 0;
    }
    if (removed && store) store->DeleteScene(sceneId);
    return removed;
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
    RecordEvent({Now(), "scene", "", sceneId, "Activated \"" + scene.Name + "\""});
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
    {
        std::lock_guard<std::mutex> lock(automationMutex);
        automations[automation.AutomationId] = automation;
    }
    if (engine) engine->Forget(automation.AutomationId);
    if (store) store->SaveAutomation(automation);
    WakeAutomations();
    return true;
}

bool SmartHomeManager::UpdateAutomation(const SmartHomeAutomation& automation) {
    {
        std::lock_guard<std::mutex> lock(automationMutex);
        auto it = automations.find(automation.AutomationId);
        if (it == automations.end()) {
            return false;
        }
        it->second = automation;
    }
    if (engine) engine->Forget(automation.AutomationId);
    if (store) store->SaveAutomation(automation);
    WakeAutomations();
    return true;
}

bool SmartHomeManager::RemoveAutomation(const std::string& automationId) {
    bool removed;
    {
        std::lock_guard<std::mutex> lock(automationMutex);
        removed = automations.erase(automationId) > 0;
    }
    if (engine) engine->Forget(automationId);
    if (removed && store) store->DeleteAutomation(automationId);
    return removed;
}

bool SmartHomeManager::SetAutomationEnabled(const std::string& automationId, bool enabled) {
    SmartHomeAutomation changed;
    {
        std::lock_guard<std::mutex> lock(automationMutex);
        auto it = automations.find(automationId);
        if (it == automations.end()) {
            return false;
        }
        it->second.Enabled = enabled;
        changed = it->second;
    }
    // Switched back on, it starts afresh: what happened while it was off is
    // not a trigger.
    if (engine && enabled) engine->Forget(automationId);
    if (store) store->SaveAutomation(changed);
    return true;
}

bool SmartHomeManager::RunAutomation(const std::string& automationId) {
    SmartHomeAutomation automation = GetAutomation(automationId);
    if (automation.AutomationId.empty() || !engine) return false;
    return engine->Run(automation, Now(), "run by hand");
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
    if (engine) engine->Evaluate(activeAutomations, Now());
}

void SmartHomeManager::EnsureAutomationEngine() {
    if (engine) return;
    // Everything the engine needs from the home, through the manager.
    SmartHomeAutomationEngine::Host host;
    host.readAttribute = [this](const std::string& id, const std::string& attribute) {
        return ReadDeviceAttribute(id, attribute);
    };
    host.sendCommand = [this](const SmartHomeCommand& command) { QueueCommand(command); };
    host.activateScene = [this](const std::string& sceneId) { ActivateScene(sceneId); };
    host.notify = [this](const std::string& automationId, const std::string& message) {
        RecordEvent({Now(), "automation", "", automationId, "Notice: " + message});
        Notify(&SmartHomeListener::AutomationNotify, automationId, message);
    };
    host.disable = [this](const std::string& automationId) {
        SetAutomationEnabled(automationId, false);
        DispatchError(-10, "Automation " + automationId + " switched off: it was triggering itself");
    };
    host.ran = [this](const std::string& automationId) { DispatchAutomationTrigger(automationId); };
    host.log = [this](const SmartHomeEvent& event) { RecordEvent(event); };
    host.location = [this](double& latitude, double& longitude) { return GetHomeLocation(latitude, longitude); };
    engine = std::make_unique<SmartHomeAutomationEngine>(std::move(host));
}

void SmartHomeManager::WakeAutomations() {
    {
        std::lock_guard<std::mutex> lock(automationWakeMutex);
        automationWakePending = true;
    }
    automationWake.notify_one();
}

void SmartHomeManager::SetTimeSource(std::function<int64_t()> source) {
    std::lock_guard<std::mutex> lock(clockMutex);
    timeSource = std::move(source);
}

int64_t SmartHomeManager::Now() const {
    {
        std::lock_guard<std::mutex> lock(clockMutex);
        if (timeSource) return timeSource();
    }
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
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
    
    Notify(&SmartHomeListener::DeviceDiscover, info);
}

void SmartHomeManager::DispatchDeviceStateChange(const std::string& deviceId, SmartHomeDeviceState state) {
    Notify(&SmartHomeListener::DeviceStateChange, deviceId, state);
}

void SmartHomeManager::DispatchDeviceUpdate(const std::string& deviceId) {
    Notify(&SmartHomeListener::DeviceUpdate, deviceId);
}

void SmartHomeManager::DispatchNetworkChange(const SmartHomeNetworkInfo& info) {
    Notify(&SmartHomeListener::NetworkChange, info);
}

void SmartHomeManager::DispatchPairingProgress(int progress, const std::string& status) {
    Notify(&SmartHomeListener::PairingProgress, progress, status);
}

void SmartHomeManager::DispatchPairingComplete(bool success, const SmartHomeDeviceInfo& device) {
    pairing = false;
    Notify(&SmartHomeListener::PairingComplete, success, device);
}

void SmartHomeManager::DispatchSensorRead(const std::string& deviceId, const SmartHomeSensorReading& reading) {
    Notify(&SmartHomeListener::SensorRead, deviceId, reading);
}

void SmartHomeManager::DispatchAutomationTrigger(const std::string& automationId) {
    Notify(&SmartHomeListener::AutomationTrigger, automationId);
}

void SmartHomeManager::DispatchError(int code, const std::string& message) {
    Notify(&SmartHomeListener::Error, code, message);
}

// ===== LISTENERS =====

namespace {
// How deep this thread is inside notifications: RemoveListener from inside
// one must not wait for calls in flight - it may be one of them.
thread_local int tlsNotifyDepth = 0;
}

template <typename Fn, typename... Args>
void SmartHomeManager::Notify(Fn SmartHomeListener::* member, const Args&... args) {
    Fn slot;
    std::vector<std::shared_ptr<ListenerEntry>> snapshot;
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        slot = primary.*member;
        for (const auto& [id, entry] : listeners) {
            if (entry->fns.*member) snapshot.push_back(entry);
        }
    }
    struct Depth {
        Depth() { ++tlsNotifyDepth; }
        ~Depth() { --tlsNotifyDepth; }
    } depth;
    if (slot) slot(args...);
    for (const auto& entry : snapshot) {
        // Counted before the check: a RemoveListener that marks it after
        // this point waits for the call; one that marked it before is seen.
        struct Active {
            std::atomic<int>& n;
            explicit Active(std::atomic<int>& c) : n(c) { ++n; }
            ~Active() { --n; }
        } active(entry->active);
        if (entry->removed) continue;
        (entry->fns.*member)(args...);
    }
}

SmartHomeListenerId SmartHomeManager::AddListener(SmartHomeListener listener) {
    auto entry = std::make_shared<ListenerEntry>();
    entry->fns = std::move(listener);
    std::lock_guard<std::mutex> lock(callbackMutex);
    const SmartHomeListenerId id = nextListenerId++;
    listeners.emplace_back(id, std::move(entry));
    return id;
}

void SmartHomeManager::RemoveListener(SmartHomeListenerId id) {
    std::shared_ptr<ListenerEntry> entry;
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        auto it = std::find_if(listeners.begin(), listeners.end(),
                               [id](const auto& l) { return l.first == id; });
        if (it == listeners.end()) return;
        entry = it->second;
        listeners.erase(it);
    }
    entry->removed = true;
    if (tlsNotifyDepth > 0) return;   // inside a notification: later calls stop, this one may be ours
    while (entry->active > 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

// ===== PERSISTENCE =====

// The configuration is the store: saving is a copy of it, loading opens one.
bool SmartHomeManager::SaveConfiguration(const std::string& path) {
    return ExportStore(path);
}

bool SmartHomeManager::LoadConfiguration(const std::string& path) {
    return OpenStore(path);
}

// ===== THE STORE =====

bool SmartHomeManager::OpenStore(const std::string& path) {
    if (!store) store = std::make_unique<SmartHomeStore>();
    if (!store->Open(path)) {
        DispatchError(-20, store->LastError());
        return false;
    }

    // The store's scenes and automations join the ones in memory; where both
    // have one, the store's wins, and the ones only in memory are saved to it.
    std::vector<SmartHomeScene> memoryScenes = GetAllScenes();
    std::vector<SmartHomeAutomation> memoryAutomations = GetAllAutomations();
    std::set<std::string> storedScenes, storedAutomations;
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        for (SmartHomeScene& scene : store->LoadScenes()) {
            storedScenes.insert(scene.SceneId);
            scenes[scene.SceneId] = std::move(scene);
        }
    }
    {
        std::lock_guard<std::mutex> lock(automationMutex);
        for (SmartHomeAutomation& automation : store->LoadAutomations()) {
            storedAutomations.insert(automation.AutomationId);
            automations[automation.AutomationId] = std::move(automation);
        }
    }
    for (const auto& scene : memoryScenes)
        if (!storedScenes.count(scene.SceneId)) store->SaveScene(scene);
    for (const auto& automation : memoryAutomations)
        if (!storedAutomations.count(automation.AutomationId)) store->SaveAutomation(automation);
    if (engine) for (const auto& id : storedAutomations) engine->Forget(id);

    // The home's location.
    if (auto lat = store->GetSetting("location.latitude"), lon = store->GetSetting("location.longitude"); lat && lon) {
        double latitude = 0, longitude = 0;
        if (TryParseFloat(*lat, latitude) && TryParseFloat(*lon, longitude)) {
            std::lock_guard<std::mutex> lock(locationMutex);
            hasLocation = true;
            homeLatitude = latitude;
            homeLongitude = longitude;
        }
    }

    // The rooms: the store's, and the ones met before it opened (devices
    // brought them, or the person made them) that it does not have yet. A
    // room met keeps its id unless the store's rooms use it already; `moved`
    // says where the ids that had to change went.
    std::map<std::string, std::string> moved;
    std::set<std::string> pointedTo;   // rooms a choice made before the store opened names
    {
        std::lock_guard<std::mutex> lock(deviceMutex);
        for (const auto& [id, o] : overrides) pointedTo.insert(o.RoomId);
    }
    {
        std::lock_guard<std::mutex> lock(homeMutex);
        std::vector<SmartHomeRoom> met = std::move(rooms);
        rooms = store->LoadRooms();
        for (SmartHomeRoom& room : met) {
            // A room only a backend's name made comes back from that name
            // below, if a device still has it after the stored choices.
            if (!roomsChosen.count(room.RoomId) && !pointedTo.count(room.RoomId)) continue;
            const SmartHomeRoom* same = nullptr;
            bool idTaken = false;
            for (const SmartHomeRoom& r : rooms) {
                if (LowerAscii(r.Name) == LowerAscii(room.Name)) same = &r;
                idTaken = idTaken || r.RoomId == room.RoomId;
            }
            if (same) {
                if (same->RoomId != room.RoomId) moved[room.RoomId] = same->RoomId;
                continue;
            }
            if (idTaken) {
                const std::string id = NewRoomIdLocked(room.Name);
                moved[room.RoomId] = id;
                room.RoomId = id;
            }
            int last = 0;
            for (const SmartHomeRoom& r : rooms) last = std::max(last, r.SortOrder);
            room.SortOrder = last + 1;
            rooms.push_back(room);
            store->SaveRoom(room);
        }
        roomsChosen.clear();   // in the store now
        homeChanged = true;
    }

    // The names and rooms the person gave devices, over every device
    // already registered: the store's, and the ones given before it opened
    // that it does not have yet. A room kept by name (version 1 of the
    // store) becomes a room of the home here.
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(deviceMutex);
        std::map<std::string, SmartHomeDeviceOverride> given = std::move(overrides);
        overrides = store->LoadDeviceOverrides();
        for (auto& [id, o] : given) {
            if (overrides.count(id)) continue;
            if (auto m = moved.find(o.RoomId); m != moved.end()) o.RoomId = m->second;
            if (!o.Name.empty()) store->SetDeviceName(id, o.Name);
            if (!o.RoomId.empty()) store->SetDeviceRoomId(id, o.RoomId);
            overrides[id] = o;
        }
        for (auto& [id, o] : overrides) {
            if (o.Room.empty() || !o.RoomId.empty()) continue;
            {
                std::lock_guard<std::mutex> home(homeMutex);
                o.RoomId = RoomForNameLocked(o.Room);
            }
            o.Room.clear();
            store->SetDeviceRoomId(id, o.RoomId);
        }
        for (const auto& [id, reg] : devices) ids.push_back(id);
    }
    for (const std::string& id : ids) ReapplyDevice(id);
    NotifyHomeChange();
    WakeAutomations();
    return true;
}

void SmartHomeManager::CloseStore() {
    if (store) store->Close();
    std::lock_guard<std::mutex> lock(readingMutex);
    lastRecorded.clear();
}

bool SmartHomeManager::IsStoreOpen() const {
    return store && store->IsOpen();
}

std::string SmartHomeManager::GetStoreError() const {
    return store ? store->LastError() : std::string("no store has been opened");
}

bool SmartHomeManager::ExportStore(const std::string& path) const {
    return store && store->ExportTo(path);
}

std::vector<SmartHomeSensorReading> SmartHomeManager::GetSensorHistory(const std::string& deviceId,
                                                                       uint64_t startMs, uint64_t endMs) const {
    return store ? store->LoadReadings(deviceId, startMs, endMs) : std::vector<SmartHomeSensorReading>{};
}

// ===== THE HOME =====

bool SmartHomeManager::SetDeviceName(const std::string& deviceId, const std::string& name) {
    {
        std::lock_guard<std::mutex> lock(deviceMutex);
        if (!devices.count(deviceId)) return false;
        overrides[deviceId].Name = name;
    }
    if (store) store->SetDeviceName(deviceId, name);
    return true;
}

bool SmartHomeManager::SetDeviceRoom(const std::string& deviceId, const std::string& room) {
    if (room.empty()) {
        // Back to the backend's room.
        {
            std::lock_guard<std::mutex> lock(deviceMutex);
            if (!devices.count(deviceId)) return false;
            overrides[deviceId].Room.clear();
            overrides[deviceId].RoomId.clear();
        }
        if (store) store->SetDeviceRoomId(deviceId, "");
        std::shared_ptr<ISmartHomeDevice> device = GetDevice(deviceId);
        SmartHomeDeviceInfo info = device ? device->GetDeviceInfo() : GetDeviceInfo(deviceId);
        // Without a device object the backend's room is not known any more.
        if (!device) info.Room.clear();
        info.RoomId.clear();
        ApplyOverrides(info);
        UpdateDeviceInfo(deviceId, info);
        NotifyHomeChange();
        return true;
    }
    if (GetDeviceInfo(deviceId).DeviceId.empty()) return false;
    std::string roomId;
    {
        std::lock_guard<std::mutex> lock(homeMutex);
        roomId = RoomForNameLocked(room);
        if (!IsStoreOpen()) roomsChosen.insert(roomId);
    }
    const bool ok = AssignDeviceToRoom(deviceId, roomId);
    NotifyHomeChange();
    return ok;
}

std::vector<SmartHomeRoom> SmartHomeManager::GetRooms() const {
    std::vector<SmartHomeRoom> sorted;
    {
        std::lock_guard<std::mutex> lock(homeMutex);
        sorted = rooms;
    }
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const SmartHomeRoom& a, const SmartHomeRoom& b) { return a.SortOrder < b.SortOrder; });
    return sorted;
}

SmartHomeRoom SmartHomeManager::GetRoom(const std::string& roomId) const {
    std::lock_guard<std::mutex> lock(homeMutex);
    for (const SmartHomeRoom& room : rooms)
        if (room.RoomId == roomId) return room;
    return {};
}

bool SmartHomeManager::CreateRoom(const SmartHomeRoom& room, std::string* createdId) {
    if (room.Name.empty()) return false;
    {
        std::lock_guard<std::mutex> lock(homeMutex);
        const std::string key = LowerAscii(room.Name);
        for (const SmartHomeRoom& r : rooms) {
            if (LowerAscii(r.Name) == key) return false;
            if (!room.RoomId.empty() && r.RoomId == room.RoomId) return false;
        }
        SmartHomeRoom added = room;
        if (added.RoomId.empty() || added.RoomId == kNoRoomOverride) added.RoomId = NewRoomIdLocked(room.Name);
        if (added.SortOrder == 0) {
            int last = 0;
            for (const SmartHomeRoom& r : rooms) last = std::max(last, r.SortOrder);
            added.SortOrder = last + 1;
        }
        rooms.push_back(added);
        if (store) store->SaveRoom(added);
        if (!IsStoreOpen()) roomsChosen.insert(added.RoomId);
        if (createdId) *createdId = added.RoomId;
        homeChanged = true;
    }
    NotifyHomeChange();
    return true;
}

bool SmartHomeManager::UpdateRoom(const SmartHomeRoom& room) {
    if (room.Name.empty()) return false;
    bool renamed = false;
    {
        std::lock_guard<std::mutex> lock(homeMutex);
        SmartHomeRoom* target = nullptr;
        const std::string key = LowerAscii(room.Name);
        for (SmartHomeRoom& r : rooms) {
            if (r.RoomId == room.RoomId) target = &r;
            else if (LowerAscii(r.Name) == key) return false;
        }
        if (!target) return false;
        renamed = target->Name != room.Name;
        *target = room;
        if (store) store->SaveRoom(room);
        if (!IsStoreOpen()) roomsChosen.insert(room.RoomId);
        homeChanged = true;
    }
    // The devices in it show the new name. Their backends still say the old
    // one, so their place is kept by id from now on - otherwise the old
    // name would bring the old room back.
    std::vector<std::string> members;
    {
        std::lock_guard<std::mutex> lock(deviceMutex);
        for (const auto& [id, reg] : devices) {
            if (reg.Info.RoomId != room.RoomId) continue;
            members.push_back(id);
            if (renamed && overrides[id].RoomId.empty()) {
                overrides[id].RoomId = room.RoomId;
                if (store) store->SetDeviceRoomId(id, room.RoomId);
            }
        }
    }
    for (const std::string& id : members) ReapplyDevice(id);
    NotifyHomeChange();
    return true;
}

bool SmartHomeManager::DeleteRoom(const std::string& roomId) {
    // Its devices go to no room, and stay there: the backend's name for
    // the room must not make it again.
    std::vector<std::string> members;
    {
        std::lock_guard<std::mutex> lock(deviceMutex);
        {
            std::lock_guard<std::mutex> home(homeMutex);
            auto it = std::find_if(rooms.begin(), rooms.end(),
                                   [&](const SmartHomeRoom& r) { return r.RoomId == roomId; });
            if (it == rooms.end()) return false;
            rooms.erase(it);
            if (store) store->DeleteRoom(roomId);
            homeChanged = true;
        }
        for (const auto& [id, reg] : devices) {
            const bool inIt = reg.Info.RoomId == roomId || (overrides.count(id) && overrides[id].RoomId == roomId);
            if (!inIt) continue;
            members.push_back(id);
            overrides[id].RoomId = kNoRoomOverride;
            if (store) store->SetDeviceRoomId(id, kNoRoomOverride);
        }
    }
    for (const std::string& id : members) ReapplyDevice(id);
    NotifyHomeChange();
    return true;
}

bool SmartHomeManager::AssignDeviceToRoom(const std::string& deviceId, const std::string& roomId) {
    if (!roomId.empty() && GetRoom(roomId).RoomId.empty()) return false;
    const std::string chosen = roomId.empty() ? std::string(kNoRoomOverride) : roomId;
    {
        std::lock_guard<std::mutex> lock(deviceMutex);
        if (!devices.count(deviceId)) return false;
        overrides[deviceId].Room.clear();
        overrides[deviceId].RoomId = chosen;
    }
    if (store) store->SetDeviceRoomId(deviceId, chosen);
    ReapplyDevice(deviceId);
    NotifyHomeChange();
    return true;
}

std::string SmartHomeManager::GetDeviceRoom(const std::string& deviceId) const {
    return GetDeviceInfo(deviceId).RoomId;
}

void SmartHomeManager::SetOnHomeChange(OnHomeChange callback) {
    SetPrimary(&SmartHomeListener::HomeChange, std::move(callback));
}

bool SmartHomeManager::SetHomeLocation(double latitude, double longitude) {
    if (latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180) return false;
    {
        std::lock_guard<std::mutex> lock(locationMutex);
        hasLocation = true;
        homeLatitude = latitude;
        homeLongitude = longitude;
    }
    if (store) {
        store->SetSetting("location.latitude", FormatFloatClassic(latitude, 10));
        store->SetSetting("location.longitude", FormatFloatClassic(longitude, 10));
    }
    WakeAutomations();
    return true;
}

bool SmartHomeManager::GetHomeLocation(double& latitude, double& longitude) const {
    std::lock_guard<std::mutex> lock(locationMutex);
    if (!hasLocation) return false;
    latitude = homeLatitude;
    longitude = homeLongitude;
    return true;
}

bool SmartHomeManager::GetSunTimes(int64_t& sunrise, int64_t& sunset) const {
    double latitude = 0, longitude = 0;
    if (!GetHomeLocation(latitude, longitude)) return false;
    return SmartHomeAutomationEngine::ComputeSunTimes(latitude, longitude, Now(), sunrise, sunset);
}

namespace {
// Numbers as the automation engine compares them: dot-decimal, trimmed.
std::string Number(double value) {
    // Two decimals at most; FormatFloatClassic counts significant digits
    // (as %g does) and drops trailing zeros.
    return FormatFloatClassic(std::round(value * 100.0) / 100.0, 12);
}
std::string Bool(bool value) { return value ? "true" : "false"; }
} // namespace

std::optional<std::string> SmartHomeManager::ReadDeviceAttribute(const std::string& deviceId,
                                                                 const std::string& attribute) const {
    const SmartHomeDeviceInfo info = GetDeviceInfo(deviceId);
    if (info.DeviceId.empty()) return std::nullopt;
    const std::string a = attribute;
    if (a == "state") return DeviceStateToString(info.State);
    if (a == "online" || a == "reachable") return Bool(info.Reachable);

    std::shared_ptr<ISmartHomeDevice> device = GetDevice(deviceId);
    if (!device) return std::nullopt;
    if (a == "battery") {
        if (!device->IsBatteryPowered()) return std::nullopt;
        return std::to_string(device->GetBatteryLevel());
    }
    if (auto light = std::dynamic_pointer_cast<ISmartLight>(device)) {
        if (a == "on") return Bool(light->IsOn());
        if (a == "brightness") return std::to_string((light->GetBrightness() * 100 + 127) / 255);
        if (a == "colorTemp") return std::to_string(light->GetColorTemperature());
    }
    if (auto sw = std::dynamic_pointer_cast<ISmartSwitch>(device)) {
        if (a == "on") return Bool(sw->IsOn());
        if (a == "power" && sw->SupportsPowerMonitoring()) return Number(sw->GetPower());
        if (a == "energy" && sw->SupportsPowerMonitoring()) return Number(sw->GetEnergyUsage());
    }
    if (auto t = std::dynamic_pointer_cast<ISmartThermostat>(device)) {
        if (a == "temperature") return Number(t->GetCurrentTemperature());
        if (a == "target") return Number(t->GetTargetTemperature());
        if (a == "humidity") return Number(t->GetHumidity());
        if (a == "mode") return t->GetMode();
        if (a == "running") return Bool(t->IsRunning());
    }
    if (auto lock = std::dynamic_pointer_cast<ISmartLock>(device)) {
        if (a == "locked") return Bool(lock->IsLocked());
        if (a == "jammed") return Bool(lock->IsJammed());
    }
    if (auto blind = std::dynamic_pointer_cast<ISmartBlind>(device)) {
        if (a == "position") return std::to_string(blind->GetPosition());
        if (a == "tilt") return std::to_string(blind->GetTilt());
        if (a == "moving") return Bool(blind->IsMoving());
    }
    if (auto sensor = std::dynamic_pointer_cast<ISmartSensor>(device)) {
        const SmartHomeSensorReading r = sensor->GetReading();
        const bool binary = r.Type == SmartHomeSensorType::Motion || r.Type == SmartHomeSensorType::Contact ||
                            r.Type == SmartHomeSensorType::Water || r.Type == SmartHomeSensorType::Smoke ||
                            r.Type == SmartHomeSensorType::CarbonMonoxide || r.Type == SmartHomeSensorType::Occupancy ||
                            r.Type == SmartHomeSensorType::Vibration;
        if (a == "value") return Number(r.Value);
        if (a == "triggered" || a == "motion" || a == "open" || a == "contact" || a == "water" ||
            a == "leak" || a == "smoke") {
            return binary ? std::optional<std::string>(Bool(r.Triggered)) : std::nullopt;
        }
        if ((a == "temperature" && r.Type == SmartHomeSensorType::Temperature) ||
            (a == "humidity" && r.Type == SmartHomeSensorType::Humidity)) {
            return Number(r.Value);
        }
    }
    return std::nullopt;
}

void SmartHomeManager::RecordReadings(const std::string& deviceId) {
    if (!IsStoreOpen()) return;
    std::shared_ptr<ISmartHomeDevice> device = GetDevice(deviceId);
    if (!device) return;

    // What the device measures now, and how far it must move to be worth a
    // row: a tenth of a degree, a percent of humidity, a watt (or 2 % of a
    // big draw); any change of a sensor that triggers.
    SmartHomeSensorReading reading;
    float deadband = 0.0f;
    bool binary = false;
    if (auto sensor = std::dynamic_pointer_cast<ISmartSensor>(device)) {
        reading = sensor->GetReading();
        switch (reading.Type) {
            case SmartHomeSensorType::Temperature: deadband = 0.1f; break;
            case SmartHomeSensorType::Humidity: deadband = 1.0f; break;
            case SmartHomeSensorType::Light: case SmartHomeSensorType::Pressure:
            case SmartHomeSensorType::AirQuality: deadband = 1.0f; break;
            default: binary = true; break;
        }
    } else if (auto thermostat = std::dynamic_pointer_cast<ISmartThermostat>(device)) {
        reading.Type = SmartHomeSensorType::Temperature;
        reading.Value = thermostat->GetCurrentTemperature();
        reading.Unit = "°C";
        deadband = 0.1f;
    } else if (auto sw = std::dynamic_pointer_cast<ISmartSwitch>(device); sw && sw->SupportsPowerMonitoring()) {
        reading.Type = SmartHomeSensorType::Power;
        reading.Value = sw->GetPower();
        reading.Unit = "W";
        deadband = std::max(1.0f, reading.Value * 0.02f);
    } else {
        return;
    }
    reading.Timestamp = static_cast<uint64_t>(Now()) * 1000;

    {
        std::lock_guard<std::mutex> lock(readingMutex);
        auto it = lastRecorded.find(deviceId);
        if (it != lastRecorded.end()) {
            const RecordedReading& last = it->second;
            const bool moved = binary ? last.triggered != reading.Triggered
                                      : std::fabs(last.value - reading.Value) >= deadband - 1e-4f;
            const bool stale = reading.Timestamp >= last.timeMs + 15 * 60 * 1000;
            if (!moved && !stale) return;
        }
        lastRecorded[deviceId] = {reading.Value, reading.Triggered, reading.Timestamp};
    }
    store->AppendReading(deviceId, reading);
}

// ===== THE EVENT LOG =====

void SmartHomeManager::RecordEvent(const SmartHomeEvent& event) {
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        recentEvents.push_front(event);
        if (recentEvents.size() > 1000) recentEvents.pop_back();
    }
    if (store) store->AppendEvent(event);
    Notify(&SmartHomeListener::Event, event);
}

std::vector<SmartHomeEvent> SmartHomeManager::GetEventLog(int64_t since, size_t limit) const {
    if (IsStoreOpen()) return store->LoadEvents(since, limit);
    std::vector<SmartHomeEvent> events;
    std::lock_guard<std::mutex> lock(eventMutex);
    for (const SmartHomeEvent& e : recentEvents) {
        if (events.size() >= limit) break;
        if (e.Time >= since) events.push_back(e);
    }
    return events;
}

void SmartHomeManager::SetOnAutomationNotify(OnAutomationNotify callback) {
    SetPrimary(&SmartHomeListener::AutomationNotify, std::move(callback));
}

void SmartHomeManager::SetOnEvent(OnSmartHomeEvent callback) {
    SetPrimary(&SmartHomeListener::Event, std::move(callback));
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
    // Once a second for the clock, and at once when a device changes, so a
    // motion sensor turns the light on without waiting for the next tick.
    while (running) {
        EvaluateAutomations();
        std::unique_lock<std::mutex> lock(automationWakeMutex);
        automationWake.wait_for(lock, std::chrono::seconds(1), [this] { return automationWakePending || !running; });
        automationWakePending = false;
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
    // The person's name, kept in the store and laid over the backend's on
    // every refresh; the object too, where there is one.
    SmartHomeManager::Instance().SetDeviceName(deviceId, newName);
    if (auto device = SmartHomeManager::Instance().GetDevice(deviceId)) device->SetName(newName);
    auto info = SmartHomeManager::Instance().GetDeviceInfo(deviceId);
    info.Name = newName;
    return SmartHomeManager::Instance().UpdateDeviceInfo(deviceId, info);
}

bool SmartHomeAPI::StartPairing(SmartHomeProtocolType protocol, int timeoutSeconds) { return SmartHomeManager::Instance().StartPairing(protocol, timeoutSeconds); }
void SmartHomeAPI::StopPairing() { SmartHomeManager::Instance().StopPairing(); }
bool SmartHomeAPI::IsPairing() const { return SmartHomeManager::Instance().IsPairing(); }
bool SmartHomeAPI::CommissionDevice(const std::string& setupCode) { return SmartHomeManager::Instance().CommissionDevice(setupCode); }

bool SmartHomeAPI::SendCommand(const SmartHomeCommand& command) { return SmartHomeManager::Instance().QueueCommand(command); }
bool SmartHomeAPI::WaitForCommands(int timeoutMs) { return SmartHomeManager::Instance().WaitForCommands(timeoutMs); }

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
    // Dot-decimal whatever the locale: backends parse it with TryParseFloat,
    // and std::to_string would write "21,500000" under LC_NUMERIC=de_DE.
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << temperature;
    cmd.Parameters["temperature"] = text.str();
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

// State queries read the backend's device object. A backend that keeps none
// (or a device of another kind) answers with the empty state, as before.
namespace {
template <typename T>
std::shared_ptr<T> DeviceAs(const std::string& deviceId) {
    return std::dynamic_pointer_cast<T>(SmartHomeManager::Instance().GetDevice(deviceId));
}
} // namespace

SmartHomeLightState SmartHomeAPI::GetLightState(const std::string& deviceId) const {
    auto light = DeviceAs<ISmartLight>(deviceId);
    return light ? light->GetLightState() : SmartHomeLightState{};
}

SmartHomeThermostatState SmartHomeAPI::GetThermostatState(const std::string& deviceId) const {
    auto thermostat = DeviceAs<ISmartThermostat>(deviceId);
    return thermostat ? thermostat->GetThermostatState() : SmartHomeThermostatState{};
}

SmartHomeLockState SmartHomeAPI::GetLockState(const std::string& deviceId) const {
    auto lock = DeviceAs<ISmartLock>(deviceId);
    return lock ? lock->GetLockState() : SmartHomeLockState{};
}

bool SmartHomeAPI::GetSwitchState(const std::string& deviceId) const {
    if (auto sw = DeviceAs<ISmartSwitch>(deviceId)) return sw->IsOn();
    // A light answers "is it on" too, so a caller can treat both alike.
    if (auto light = DeviceAs<ISmartLight>(deviceId)) return light->IsOn();
    return false;
}

SmartHomeSensorReading SmartHomeAPI::GetSensorReading(const std::string& deviceId) const {
    auto sensor = DeviceAs<ISmartSensor>(deviceId);
    return sensor ? sensor->GetReading() : SmartHomeSensorReading{};
}

uint8_t SmartHomeAPI::GetBlindPosition(const std::string& deviceId) const {
    auto blind = DeviceAs<ISmartBlind>(deviceId);
    return blind ? blind->GetPosition() : 0;
}

int SmartHomeAPI::GetBatteryLevel(const std::string& deviceId) const {
    auto device = SmartHomeManager::Instance().GetDevice(deviceId);
    return device && device->IsBatteryPowered() ? device->GetBatteryLevel() : -1;
}

float SmartHomeAPI::GetPowerUsage(const std::string& deviceId) const {
    auto sw = DeviceAs<ISmartSwitch>(deviceId);
    return sw && sw->SupportsPowerMonitoring() ? sw->GetPower() : 0.0f;
}

float SmartHomeAPI::GetEnergyUsage(const std::string& deviceId) const {
    auto sw = DeviceAs<ISmartSwitch>(deviceId);
    return sw && sw->SupportsPowerMonitoring() ? sw->GetEnergyUsage() : 0.0f;
}

std::vector<SmartHomeSensorReading> SmartHomeAPI::GetSensorHistory(const std::string& deviceId, uint64_t startTime, uint64_t endTime) const {
    return SmartHomeManager::Instance().GetSensorHistory(deviceId, startTime, endTime);
}

// The store, the home and the event log
bool SmartHomeAPI::RunAutomation(const std::string& automationId) { return SmartHomeManager::Instance().RunAutomation(automationId); }
bool SmartHomeAPI::OpenStore(const std::string& path) { return SmartHomeManager::Instance().OpenStore(path); }
void SmartHomeAPI::CloseStore() { SmartHomeManager::Instance().CloseStore(); }
bool SmartHomeAPI::IsStoreOpen() const { return SmartHomeManager::Instance().IsStoreOpen(); }
std::string SmartHomeAPI::GetStoreError() const { return SmartHomeManager::Instance().GetStoreError(); }
bool SmartHomeAPI::ExportStore(const std::string& path) const { return SmartHomeManager::Instance().ExportStore(path); }
bool SmartHomeAPI::SetDeviceRoom(const std::string& deviceId, const std::string& room) { return SmartHomeManager::Instance().SetDeviceRoom(deviceId, room); }
std::vector<SmartHomeRoom> SmartHomeAPI::GetRooms() const { return SmartHomeManager::Instance().GetRooms(); }
SmartHomeRoom SmartHomeAPI::GetRoom(const std::string& roomId) const { return SmartHomeManager::Instance().GetRoom(roomId); }
bool SmartHomeAPI::CreateRoom(const SmartHomeRoom& room, std::string* createdId) { return SmartHomeManager::Instance().CreateRoom(room, createdId); }
bool SmartHomeAPI::UpdateRoom(const SmartHomeRoom& room) { return SmartHomeManager::Instance().UpdateRoom(room); }
bool SmartHomeAPI::DeleteRoom(const std::string& roomId) { return SmartHomeManager::Instance().DeleteRoom(roomId); }
bool SmartHomeAPI::AssignDeviceToRoom(const std::string& deviceId, const std::string& roomId) { return SmartHomeManager::Instance().AssignDeviceToRoom(deviceId, roomId); }
std::string SmartHomeAPI::GetDeviceRoom(const std::string& deviceId) const { return SmartHomeManager::Instance().GetDeviceRoom(deviceId); }
void SmartHomeAPI::SetOnHomeChange(OnHomeChange callback) { SmartHomeManager::Instance().SetOnHomeChange(std::move(callback)); }
bool SmartHomeAPI::SetHomeLocation(double latitude, double longitude) { return SmartHomeManager::Instance().SetHomeLocation(latitude, longitude); }
bool SmartHomeAPI::GetHomeLocation(double& latitude, double& longitude) const { return SmartHomeManager::Instance().GetHomeLocation(latitude, longitude); }
bool SmartHomeAPI::GetSunTimes(int64_t& sunrise, int64_t& sunset) const { return SmartHomeManager::Instance().GetSunTimes(sunrise, sunset); }
std::vector<SmartHomeEvent> SmartHomeAPI::GetEventLog(int64_t since, size_t limit) const { return SmartHomeManager::Instance().GetEventLog(since, limit); }
void SmartHomeAPI::SetOnAutomationNotify(OnAutomationNotify callback) { SmartHomeManager::Instance().SetOnAutomationNotify(std::move(callback)); }
void SmartHomeAPI::SetOnEvent(OnSmartHomeEvent callback) { SmartHomeManager::Instance().SetOnEvent(std::move(callback)); }

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
SmartHomeListenerId SmartHomeAPI::AddListener(SmartHomeListener listener) { return SmartHomeManager::Instance().AddListener(std::move(listener)); }
void SmartHomeAPI::RemoveListener(SmartHomeListenerId id) { SmartHomeManager::Instance().RemoveListener(id); }

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
        case SmartHomeProtocolType::Simulated: return "Simulated";
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
    if (str == "Simulated") return SmartHomeProtocolType::Simulated;
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
        case SmartHomeSensorType::Power: return "Power";
        default: return "Unknown";
    }
}

} // namespace SmartHome
} // namespace UltraCanvas
