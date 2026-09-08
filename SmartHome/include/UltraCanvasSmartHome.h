// include/UltraCanvasSmartHome.h
// Smart Home Integration Module - Main Public API
// Version: 1.0.0
// Last Modified: 2025-12-08
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

struct SmartHomeAutomation {
    std::string AutomationId;
    std::string Name;
    bool Enabled = true;
    std::string TriggerType;    // "time", "device", "location", "sunrise", "sunset"
    std::string TriggerConfig;  // JSON configuration
    std::vector<SmartHomeCommand> Actions;
    std::vector<std::string> Conditions;
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

// ===== MAIN API CLASS =====

class SmartHomeAPI {
public:
    // Singleton access
    static SmartHomeAPI& Instance();
    
    // ===== INITIALIZATION =====
    bool Initialize();
    void Shutdown();
    bool IsInitialized() const;
    
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
    
private:
    SmartHomeAPI();
    ~SmartHomeAPI();
    SmartHomeAPI(const SmartHomeAPI&) = delete;
    SmartHomeAPI& operator=(const SmartHomeAPI&) = delete;
    
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

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
