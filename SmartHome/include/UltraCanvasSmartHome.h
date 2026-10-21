// include/UltraCanvasSmartHome.h
// Smart Home Integration Module - Main Public API
// Version: 1.1.0 - the home's rooms (SmartHomeRoom, GetRooms and friends)
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>

namespace UltraCanvas {
namespace SmartHome {

// ===== FORWARD DECLARATIONS =====
class ISmartHomeDevice;
class ISmartHomeProtocol;
class SmartHomeManager;

// ===== ENUMERATIONS =====

enum class SmartHomeProtocolType {
    Matter,
    Thread,
    Zigbee,
    ZWave,
    KNX,
    WiFi,
    Bluetooth,
    // No radio: the demo home in protocols/Simulated, for development,
    // tests and a showcase without hardware. Never mixed with a real home.
    Simulated,
    Unknown
};

enum class SmartHomeDeviceCategory {
    Light,
    Switch,
    Thermostat,
    Lock,
    Sensor,
    Camera,
    Speaker,
    Plug,
    Blind,
    Fan,
    Appliance,
    Gateway,
    Unknown
};

enum class SmartHomeDeviceState {
    Online,
    Offline,
    Pairing,
    Updating,
    Error,
    Unknown
};

enum class SmartHomeSensorType {
    Temperature,
    Humidity,
    Motion,
    Contact,
    Smoke,
    CarbonMonoxide,
    Water,
    Light,
    Pressure,
    AirQuality,
    Occupancy,
    Vibration,
    Power,          // watts, from a metered plug (history only)
    Unknown
};

enum class SmartHomeLightCapability : uint32_t {
    OnOff       = 1 << 0,
    Brightness  = 1 << 1,
    ColorTemp   = 1 << 2,
    ColorRGB    = 1 << 3,
    ColorHSV    = 1 << 4,
    Effect      = 1 << 5
};

enum class SmartHomeSecurityLevel {
    None,
    Basic,
    Encrypted,
    Certified
};

// ===== DATA STRUCTURES =====

struct SmartHomeDeviceInfo {
    std::string DeviceId;
    std::string Name;
    std::string Manufacturer;
    std::string Model;
    std::string FirmwareVersion;
    std::string SerialNumber;
    SmartHomeDeviceCategory Category = SmartHomeDeviceCategory::Unknown;
    SmartHomeProtocolType Protocol = SmartHomeProtocolType::Unknown;
    SmartHomeDeviceState State = SmartHomeDeviceState::Unknown;
    SmartHomeSecurityLevel Security = SmartHomeSecurityLevel::None;
    bool Reachable = false;
    uint64_t LastSeen = 0;
    // What the device can actually do, as lower-case tags: "switch", "dimmer",
    // "colour", "thermostat", "lock", "binarySensor"… A backend fills these from
    // whatever its protocol advertises (Z-Wave command classes, Zigbee clusters),
    // so a caller can choose a control from what the device supports rather than
    // guessing from Category alone.
    std::vector<std::string> Capabilities;
    // The name of the device's room, empty when it is in none. A backend
    // fills it when it knows one (a KNX project names it, a Matter device
    // may carry a location label, the simulated home assigns one); the
    // registry answers with the room of the home model the device is in -
    // the person's choice over the backend's (SmartHomeAPI::GetRooms).
    std::string Room;
    // That room's SmartHomeRoom::RoomId; empty when in none. Filled by the
    // registry only.
    std::string RoomId;
};

// A room of the home. A room name a backend reports becomes a room the
// first time a device brings it; the person adds, renames and removes rooms
// and moves devices between them, and all of it is kept in the store.
struct SmartHomeRoom {
    std::string RoomId;         // stable; made from the name when empty at CreateRoom
    std::string Name;
    std::string Icon;           // a name the application maps to a picture; "" for its default
    std::string Floor;          // "Ground floor", "" when the home has one
    int SortOrder = 0;          // ascending; rooms that tie keep the order they came in
};

struct SmartHomeNetworkInfo {
    std::string NetworkId;
    std::string NetworkName;
    SmartHomeProtocolType Protocol = SmartHomeProtocolType::Unknown;
    int DeviceCount = 0;
    bool Active = false;
    std::string PanId;          // For Zigbee/Thread
    std::string Channel;
    std::string ExtendedPanId;  // For Thread
};

struct SmartHomeLightState {
    bool On = false;
    uint8_t Brightness = 0;     // 0-255
    uint16_t ColorTemp = 0;     // Kelvin (2700-6500)
    uint8_t Red = 0;
    uint8_t Green = 0;
    uint8_t Blue = 0;
    uint16_t Hue = 0;           // 0-360
    uint8_t Saturation = 0;     // 0-100
    uint32_t Capabilities = 0;  // SmartHomeLightCapability flags
};

struct SmartHomeThermostatState {
    float CurrentTemperature = 0.0f;
    float TargetTemperature = 0.0f;
    float Humidity = 0.0f;
    std::string Mode;           // "heat", "cool", "auto", "off"
    std::string FanMode;        // "auto", "on", "circulate"
    bool Running = false;
};

struct SmartHomeLockState {
    bool Locked = false;
    bool Jammed = false;
    uint8_t BatteryLevel = 0;   // 0-100
    std::string LastUser;
    uint64_t LastActivity = 0;
};

struct SmartHomeSensorReading {
    SmartHomeSensorType Type = SmartHomeSensorType::Unknown;
    float Value = 0.0f;
    std::string Unit;
    uint64_t Timestamp = 0;
    bool Triggered = false;     // For binary sensors
};

struct SmartHomeCommand {
    std::string DeviceId;
    std::string Command;
    std::map<std::string, std::string> Parameters;
};

struct SmartHomeScene {
    std::string SceneId;
    std::string Name;
    std::string Icon;
    std::vector<SmartHomeCommand> Actions;
    bool Active = false;
};

// An automation: when its trigger fires and every condition holds, its
// actions run, in order. The forms are the ones SmartHomeAutomationEditor
// writes, and the engine reads exactly these:
//
// TriggerConfig, JSON, by "type" (TriggerType repeats it):
//   {"type":"device","deviceId":"…","attribute":"triggered","value":"true"}
//       fires when the attribute comes to match - once per match, not every
//       second it stays matched. Optional "operator" (=, !=, <, >, <=, >=;
//       default =) and "forMinutes": fire only once it has matched that long
//       ("door open for 10 minutes").
//   {"type":"time","time":"07:30"}            local time; optional "days":[1,…,5]
//   {"type":"sunrise","offsetMinutes":-15}    and "sunset"; needs SetHomeLocation
//   {"type":"manual"}                         only RunAutomation runs it
// Attributes a device trigger or condition can name: on, brightness (percent),
//   colorTemp, power, energy, temperature, target, humidity, mode, running,
//   locked, jammed, position, tilt, moving, value, triggered (also motion,
//   open, contact, water, leak, smoke), battery, state, online.
//
// Conditions, one per string:
//   "time between 22:00 and 06:30"            may wrap past midnight
//   "on Mon,Tue,Wed,Thu,Fri"                  Sun Mon Tue Wed Thu Fri Sat
//   "<deviceId> <attribute> <op> <value>"     e.g. "sim-22 locked = true"
//   A condition the engine cannot read fails, so the automation does not run.
//
// Actions are commands, plus three that are not sent to a device:
//   {"", "notify", {{"message","Water under the sink"}}}  -> OnAutomationNotify
//   {"", "delay", {{"seconds","300"}}}         the following actions wait
//   {"", "activateScene", {{"sceneId","…"}}}
// An automation that runs more than three times in ten seconds (it keeps
// triggering itself) is switched off and the event log says why.
struct SmartHomeAutomation {
    std::string AutomationId;
    std::string Name;
    bool Enabled = true;
    std::string TriggerType;    // "time", "device", "sunrise", "sunset", "manual", "location"
    std::string TriggerConfig;  // JSON configuration, as above
    std::vector<SmartHomeCommand> Actions;
    std::vector<std::string> Conditions;
};

// One line of the home's event log: an automation ran, a scene was
// activated, a device joined, left, went offline or came back.
struct SmartHomeEvent {
    int64_t Time = 0;           // seconds since the epoch
    std::string Kind;           // "automation", "scene", "device", "error"
    std::string DeviceId;       // empty when it is about the home
    std::string SubjectId;      // the automation or scene, when there is one
    std::string Text;
};

// ===== CALLBACK TYPES =====

using OnDeviceDiscover = std::function<void(const SmartHomeDeviceInfo&)>;
using OnDeviceStateChange = std::function<void(const std::string& deviceId, SmartHomeDeviceState state)>;
using OnDeviceUpdate = std::function<void(const std::string& deviceId)>;
using OnNetworkChange = std::function<void(const SmartHomeNetworkInfo&)>;
using OnPairingProgress = std::function<void(int progress, const std::string& status)>;
using OnPairingComplete = std::function<void(bool success, const SmartHomeDeviceInfo& device)>;
using OnSensorRead = std::function<void(const std::string& deviceId, const SmartHomeSensorReading&)>;
using OnAutomationTrigger = std::function<void(const std::string& automationId)>;
using OnSmartHomeError = std::function<void(int code, const std::string& message)>;
using OnAutomationNotify = std::function<void(const std::string& automationId, const std::string& message)>;
using OnSmartHomeEvent = std::function<void(const SmartHomeEvent& event)>;
using OnHomeChange = std::function<void()>;   // the rooms changed

// ===== MAIN API CLASS =====

class SmartHomeAPI {
public:
    // Singleton access
    static SmartHomeAPI& Instance();
    
    // ===== INITIALIZATION =====
    bool Initialize();
    void Shutdown();
    bool IsInitialized() const;
    
    // ===== PROTOCOL BACKENDS =====
    // A protocol type is only usable once a backend is registered for it.
    // Initialize() registers every backend compiled into this build, so the
    // common case needs none of these; they are for supplying a backend of
    // your own, or replacing a built-in one. The backend is taken by
    // shared_ptr to an incomplete type on purpose: applications that only
    // drive devices never need to see ISmartHomeProtocol.
    bool RegisterProtocol(SmartHomeProtocolType protocol,
                          std::shared_ptr<ISmartHomeProtocol> backend);
    bool UnregisterProtocol(SmartHomeProtocolType protocol);
    bool HasProtocolBackend(SmartHomeProtocolType protocol) const;

    // ===== PROTOCOL MANAGEMENT =====
    bool EnableProtocol(SmartHomeProtocolType protocol);
    bool DisableProtocol(SmartHomeProtocolType protocol);
    bool IsProtocolEnabled(SmartHomeProtocolType protocol) const;
    bool IsProtocolAvailable(SmartHomeProtocolType protocol) const;
    std::vector<SmartHomeProtocolType> GetEnabledProtocols() const;
    std::vector<SmartHomeProtocolType> GetAvailableProtocols() const;
    
    // ===== DEVICE DISCOVERY =====
    bool StartDiscovery(SmartHomeProtocolType protocol = SmartHomeProtocolType::Unknown);
    void StopDiscovery();
    bool IsDiscovering() const;
    std::vector<SmartHomeDeviceInfo> GetDiscoveredDevices() const;
    
    // ===== DEVICE MANAGEMENT =====
    std::vector<SmartHomeDeviceInfo> GetDevices() const;
    std::vector<SmartHomeDeviceInfo> GetDevicesByCategory(SmartHomeDeviceCategory category) const;
    std::vector<SmartHomeDeviceInfo> GetDevicesByProtocol(SmartHomeProtocolType protocol) const;
    SmartHomeDeviceInfo GetDevice(const std::string& deviceId) const;
    bool RemoveDevice(const std::string& deviceId);
    bool RenameDevice(const std::string& deviceId, const std::string& newName);
    
    // ===== PAIRING =====
    bool StartPairing(SmartHomeProtocolType protocol, int timeoutSeconds = 60);
    void StopPairing();
    bool IsPairing() const;
    bool CommissionDevice(const std::string& setupCode);  // Matter QR/numeric code
    
    // ===== DEVICE CONTROL =====
    bool SendCommand(const SmartHomeCommand& command);
    bool SetLightState(const std::string& deviceId, const SmartHomeLightState& state);
    bool SetThermostatTarget(const std::string& deviceId, float temperature);
    bool SetThermostatMode(const std::string& deviceId, const std::string& mode);
    bool SetLockState(const std::string& deviceId, bool locked);
    bool SetSwitchState(const std::string& deviceId, bool on);
    bool SetBlindPosition(const std::string& deviceId, uint8_t position);  // 0-100
    
    // ===== STATE QUERIES =====
    SmartHomeLightState GetLightState(const std::string& deviceId) const;
    SmartHomeThermostatState GetThermostatState(const std::string& deviceId) const;
    SmartHomeLockState GetLockState(const std::string& deviceId) const;
    bool GetSwitchState(const std::string& deviceId) const;
    SmartHomeSensorReading GetSensorReading(const std::string& deviceId) const;
    uint8_t GetBlindPosition(const std::string& deviceId) const;   // 0 closed - 100 open
    int GetBatteryLevel(const std::string& deviceId) const;        // 0-100, -1 on mains
    float GetPowerUsage(const std::string& deviceId) const;        // watts now, 0 if unmetered
    float GetEnergyUsage(const std::string& deviceId) const;       // kWh since metering began
    // Recorded readings, oldest first; times in milliseconds since the
    // epoch, as SmartHomeSensorReading::Timestamp. Recorded while a store is
    // open, when a value moves past its deadband (a tenth of a degree, a
    // percent of humidity, a watt) and at least every 15 minutes; a
    // thermostat records its room temperature, a metered plug its power.
    std::vector<SmartHomeSensorReading> GetSensorHistory(const std::string& deviceId, 
                                                          uint64_t startTime, 
                                                          uint64_t endTime) const;
    
    // ===== SCENES =====
    bool CreateScene(const SmartHomeScene& scene);
    bool UpdateScene(const SmartHomeScene& scene);
    bool DeleteScene(const std::string& sceneId);
    bool ActivateScene(const std::string& sceneId);
    std::vector<SmartHomeScene> GetScenes() const;
    SmartHomeScene GetScene(const std::string& sceneId) const;
    
    // ===== AUTOMATIONS =====
    bool CreateAutomation(const SmartHomeAutomation& automation);
    bool UpdateAutomation(const SmartHomeAutomation& automation);
    bool DeleteAutomation(const std::string& automationId);
    bool EnableAutomation(const std::string& automationId, bool enable);
    std::vector<SmartHomeAutomation> GetAutomations() const;
    SmartHomeAutomation GetAutomation(const std::string& automationId) const;
    
    // Runs an automation's actions now, whatever its trigger and conditions.
    bool RunAutomation(const std::string& automationId);

    // ===== THE STORE (smarthome.db) =====
    // The home's own database: scenes, automations, the names and rooms the
    // person gave devices, the event log, sensor history and settings. Open
    // it once after Initialize(); what it holds replaces the scenes and
    // automations in memory, and every change afterwards is written through.
    // ":memory:" opens one that lasts until CloseStore (tests). Without a
    // store everything works but nothing survives a restart and
    // GetSensorHistory has nothing to answer with.
    bool OpenStore(const std::string& path);
    void CloseStore();
    bool IsStoreOpen() const;
    std::string GetStoreError() const;           // why the last store call failed
    // A consistent copy of the open store at `path` (a backup); the file
    // must not exist yet.
    bool ExportStore(const std::string& path) const;

    // ===== THE HOME =====
    // The rooms, by SortOrder. A room's devices are the ones whose
    // SmartHomeDeviceInfo::RoomId is its id.
    std::vector<SmartHomeRoom> GetRooms() const;
    SmartHomeRoom GetRoom(const std::string& roomId) const;   // RoomId empty when there is none
    // A new room; false when its name is empty or another room has it (not
    // minding case). `createdId`, when given, receives the id it got.
    bool CreateRoom(const SmartHomeRoom& room, std::string* createdId = nullptr);
    // Renames, re-icons or reorders a room; its devices follow a new name.
    bool UpdateRoom(const SmartHomeRoom& room);
    // Removes a room. Its devices are then in no room - the backend's room
    // for them no longer applies either - until they are put in another.
    bool DeleteRoom(const std::string& roomId);
    // Puts a device in a room; "" takes it out of every room.
    bool AssignDeviceToRoom(const std::string& deviceId, const std::string& roomId);
    std::string GetDeviceRoom(const std::string& deviceId) const;   // its RoomId
    // The same by name, for callers that have one: the room with that name,
    // made when there is none yet. Empty puts the device back where the
    // backend has it.
    bool SetDeviceRoom(const std::string& deviceId, const std::string& room);
    // Where the home is, for sunrise and sunset triggers (degrees, east and
    // north positive). Kept in the store.
    bool SetHomeLocation(double latitude, double longitude);
    bool GetHomeLocation(double& latitude, double& longitude) const;
    // Today's sunrise and sunset at the home's location, in seconds since
    // the epoch; false without a location, or in a polar day or night.
    bool GetSunTimes(int64_t& sunrise, int64_t& sunset) const;

    // ===== EVENT LOG =====
    // Newest first; `since` in seconds since the epoch. From the store when
    // one is open, else from the last thousand events in memory.
    std::vector<SmartHomeEvent> GetEventLog(int64_t since = 0, size_t limit = 200) const;

    // ===== NETWORK INFO =====
    std::vector<SmartHomeNetworkInfo> GetNetworks() const;
    SmartHomeNetworkInfo GetNetwork(SmartHomeProtocolType protocol) const;
    
    // ===== CALLBACKS =====
    void SetOnDeviceDiscover(OnDeviceDiscover callback);
    void SetOnDeviceStateChange(OnDeviceStateChange callback);
    void SetOnDeviceUpdate(OnDeviceUpdate callback);
    void SetOnNetworkChange(OnNetworkChange callback);
    void SetOnPairingProgress(OnPairingProgress callback);
    void SetOnPairingComplete(OnPairingComplete callback);
    void SetOnSensorRead(OnSensorRead callback);
    void SetOnAutomationTrigger(OnAutomationTrigger callback);
    void SetOnSmartHomeError(OnSmartHomeError callback);
    void SetOnAutomationNotify(OnAutomationNotify callback);
    void SetOnEvent(OnSmartHomeEvent callback);
    // The rooms changed: one added, renamed, reordered or removed. Called on
    // whichever thread made the change, with no lock held.
    void SetOnHomeChange(OnHomeChange callback);
    
private:
    SmartHomeAPI();
    ~SmartHomeAPI();
    SmartHomeAPI(const SmartHomeAPI&) = delete;
    SmartHomeAPI& operator=(const SmartHomeAPI&) = delete;
    
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// ===== PROTOCOL BACKEND REGISTRATION =====

// Registers a factory for a protocol backend. EnableProtocol() uses it to
// build a backend on demand for any type that has no backend registered yet.
using SmartHomeProtocolFactory =
    std::function<std::shared_ptr<ISmartHomeProtocol>()>;
void RegisterProtocolFactory(SmartHomeProtocolType type,
                             SmartHomeProtocolFactory factory);
void UnregisterProtocolFactory(SmartHomeProtocolType type);

// Registers a factory for every backend compiled into this build. Called by
// SmartHomeAPI::Initialize(); safe to call again. Returns how many were
// registered — zero is normal in a build with no protocol backends enabled.
int RegisterBuiltinProtocols();

// ===== UTILITY FUNCTIONS =====

std::string ProtocolTypeToString(SmartHomeProtocolType type);
SmartHomeProtocolType StringToProtocolType(const std::string& str);
std::string DeviceCategoryToString(SmartHomeDeviceCategory category);
SmartHomeDeviceCategory StringToDeviceCategory(const std::string& str);
std::string DeviceStateToString(SmartHomeDeviceState state);
std::string SensorTypeToString(SmartHomeSensorType type);

// ===== CONVENIENCE MACROS =====

#define SMARTHOME_API UltraCanvas::SmartHome::SmartHomeAPI::Instance()

} // namespace SmartHome
} // namespace UltraCanvas
