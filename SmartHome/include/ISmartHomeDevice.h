// driver/smarthome/devices/ISmartHomeDevice.h
// Smart Home Device Interface - Abstract Base for All Smart Home Devices
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasSmartHome.h"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <any>

namespace UltraCanvas {
namespace SmartHome {

// ===== DEVICE CAPABILITY FLAGS =====

enum class DeviceCapability : uint32_t {
    None            = 0,
    OnOff           = 1 << 0,
    Brightness      = 1 << 1,
    ColorTemp       = 1 << 2,
    ColorRGB        = 1 << 3,
    Temperature     = 1 << 4,
    Humidity        = 1 << 5,
    Motion          = 1 << 6,
    Contact         = 1 << 7,
    Lock            = 1 << 8,
    Battery         = 1 << 9,
    Power           = 1 << 10,
    Energy          = 1 << 11,
    Position        = 1 << 12,
    Tilt            = 1 << 13,
    Speed           = 1 << 14,
    Mode            = 1 << 15,
    Volume          = 1 << 16,
    Mute            = 1 << 17,
    PlayPause       = 1 << 18,
    OTA             = 1 << 19
};

inline DeviceCapability operator|(DeviceCapability a, DeviceCapability b) {
    return static_cast<DeviceCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline DeviceCapability operator&(DeviceCapability a, DeviceCapability b) {
    return static_cast<DeviceCapability>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool HasCapability(DeviceCapability caps, DeviceCapability check) {
    return (static_cast<uint32_t>(caps) & static_cast<uint32_t>(check)) != 0;
}

// ===== DEVICE ATTRIBUTE STRUCTURE =====

struct DeviceAttribute {
    std::string Name;
    std::string Type;       // "bool", "int", "float", "string", "enum"
    std::any Value;
    std::any MinValue;
    std::any MaxValue;
    std::vector<std::string> EnumValues;
    bool Readable = true;
    bool Writable = false;
    std::string Unit;
};

// ===== DEVICE ENDPOINT =====

struct DeviceEndpoint {
    int EndpointId = 0;
    std::string Name;
    SmartHomeDeviceCategory Category = SmartHomeDeviceCategory::Unknown;
    DeviceCapability Capabilities = DeviceCapability::None;
    std::map<std::string, DeviceAttribute> Attributes;
};

// ===== SMART HOME DEVICE INTERFACE =====

class ISmartHomeDevice {
public:
    virtual ~ISmartHomeDevice() = default;
    
    // ===== IDENTIFICATION =====
    virtual std::string GetDeviceId() const = 0;
    virtual std::string GetName() const = 0;
    virtual void SetName(const std::string& name) = 0;
    virtual SmartHomeDeviceCategory GetCategory() const = 0;
    virtual SmartHomeProtocolType GetProtocol() const = 0;
    
    // ===== DEVICE INFO =====
    virtual std::string GetManufacturer() const = 0;
    virtual std::string GetModel() const = 0;
    virtual std::string GetFirmwareVersion() const = 0;
    virtual std::string GetSerialNumber() const = 0;
    virtual SmartHomeDeviceInfo GetDeviceInfo() const = 0;
    
    // ===== STATE =====
    virtual SmartHomeDeviceState GetState() const = 0;
    virtual bool IsReachable() const = 0;
    virtual uint64_t GetLastSeen() const = 0;
    virtual void UpdateLastSeen() = 0;
    
    // ===== CAPABILITIES =====
    virtual DeviceCapability GetCapabilities() const = 0;
    virtual bool HasCapability(DeviceCapability capability) const = 0;
    virtual std::vector<std::string> GetSupportedCommands() const = 0;
    
    // ===== ENDPOINTS (for multi-endpoint devices) =====
    virtual int GetEndpointCount() const { return 1; }
    virtual std::vector<DeviceEndpoint> GetEndpoints() const = 0;
    virtual DeviceEndpoint GetEndpoint(int endpointId) const = 0;
    
    // ===== ATTRIBUTES =====
    virtual std::map<std::string, DeviceAttribute> GetAttributes() const = 0;
    virtual DeviceAttribute GetAttribute(const std::string& name) const = 0;
    virtual bool SetAttribute(const std::string& name, const std::any& value) = 0;
    virtual bool HasAttribute(const std::string& name) const = 0;
    
    // ===== COMMANDS =====
    virtual bool ExecuteCommand(const std::string& command, 
                                const std::map<std::string, std::string>& params = {}) = 0;
    virtual bool SupportsCommand(const std::string& command) const = 0;
    
    // ===== POLLING =====
    virtual bool Poll() = 0;  // Refresh device state
    virtual int GetPollInterval() const = 0;  // Recommended poll interval in seconds
    
    // ===== OTA UPDATES =====
    virtual bool SupportsOTA() const { return false; }
    virtual std::string GetAvailableFirmware() const { return ""; }
    virtual bool StartFirmwareUpdate() { return false; }
    virtual int GetUpdateProgress() const { return -1; }
    
    // ===== BINDING (for Zigbee/Thread/Matter) =====
    virtual bool SupportsBind() const { return false; }
    virtual bool Bind(const std::string& targetDeviceId) { return false; }
    virtual bool Unbind(const std::string& targetDeviceId) { return false; }
    virtual std::vector<std::string> GetBoundDevices() const { return {}; }
    
    // ===== GROUPS =====
    virtual bool SupportsGroups() const { return false; }
    virtual bool JoinGroup(const std::string& groupId) { return false; }
    virtual bool LeaveGroup(const std::string& groupId) { return false; }
    virtual std::vector<std::string> GetGroups() const { return {}; }
    
    // ===== SCENES =====
    virtual bool SupportsScenes() const { return false; }
    virtual bool StoreScene(const std::string& sceneId) { return false; }
    virtual bool RecallScene(const std::string& sceneId) { return false; }
    virtual bool DeleteScene(const std::string& sceneId) { return false; }
    
    // ===== SECURITY =====
    virtual SmartHomeSecurityLevel GetSecurityLevel() const = 0;
    virtual bool IsSecureConnection() const { return GetSecurityLevel() >= SmartHomeSecurityLevel::Encrypted; }
    
    // ===== BATTERY =====
    virtual bool IsBatteryPowered() const { return false; }
    virtual int GetBatteryLevel() const { return -1; }  // 0-100, -1 if not battery powered
    virtual bool IsLowBattery() const { return false; }
    
    // ===== RAW ACCESS =====
    virtual bool SendRawData(const std::vector<uint8_t>& data) { return false; }
    virtual std::vector<uint8_t> ReceiveRawData() { return {}; }
};

// ===== SPECIFIC DEVICE INTERFACES =====

class ISmartLight : public ISmartHomeDevice {
public:
    virtual SmartHomeDeviceCategory GetCategory() const override { return SmartHomeDeviceCategory::Light; }
    
    // Light-specific operations
    virtual bool TurnOn() = 0;
    virtual bool TurnOff() = 0;
    virtual bool Toggle() = 0;
    virtual bool IsOn() const = 0;
    
    virtual bool SetBrightness(uint8_t level) = 0;
    virtual uint8_t GetBrightness() const = 0;
    
    virtual bool SetColorTemperature(uint16_t kelvin) = 0;
    virtual uint16_t GetColorTemperature() const = 0;
    
    virtual bool SetColorRGB(uint8_t r, uint8_t g, uint8_t b) = 0;
    virtual void GetColorRGB(uint8_t& r, uint8_t& g, uint8_t& b) const = 0;
    
    virtual bool SetColorHSV(uint16_t hue, uint8_t saturation, uint8_t value) = 0;
    virtual void GetColorHSV(uint16_t& hue, uint8_t& saturation, uint8_t& value) const = 0;
    
    virtual SmartHomeLightState GetLightState() const = 0;
    virtual bool SetLightState(const SmartHomeLightState& state) = 0;
    
    virtual bool SupportsColorTemperature() const = 0;
    virtual bool SupportsColor() const = 0;
    virtual bool SupportsDimming() const = 0;
    
    virtual uint16_t GetMinColorTemp() const { return 2700; }
    virtual uint16_t GetMaxColorTemp() const { return 6500; }
};

class ISmartThermostat : public ISmartHomeDevice {
public:
    virtual SmartHomeDeviceCategory GetCategory() const override { return SmartHomeDeviceCategory::Thermostat; }
    
    virtual float GetCurrentTemperature() const = 0;
    virtual float GetTargetTemperature() const = 0;
    virtual bool SetTargetTemperature(float temperature) = 0;
    
    virtual float GetHumidity() const = 0;
    
    virtual std::string GetMode() const = 0;  // "heat", "cool", "auto", "off"
    virtual bool SetMode(const std::string& mode) = 0;
    virtual std::vector<std::string> GetSupportedModes() const = 0;
    
    virtual std::string GetFanMode() const = 0;
    virtual bool SetFanMode(const std::string& mode) = 0;
    
    virtual bool IsRunning() const = 0;
    
    virtual SmartHomeThermostatState GetThermostatState() const = 0;
    
    virtual float GetMinTemperature() const { return 10.0f; }
    virtual float GetMaxTemperature() const { return 35.0f; }
    virtual float GetTemperatureStep() const { return 0.5f; }
};

class ISmartLock : public ISmartHomeDevice {
public:
    virtual SmartHomeDeviceCategory GetCategory() const override { return SmartHomeDeviceCategory::Lock; }
    
    virtual bool Lock() = 0;
    virtual bool Unlock() = 0;
    virtual bool IsLocked() const = 0;
    virtual bool IsJammed() const = 0;
    
    virtual SmartHomeLockState GetLockState() const = 0;
    
    virtual std::string GetLastUser() const = 0;
    virtual uint64_t GetLastActivity() const = 0;
    
    virtual bool SupportsAutoLock() const { return false; }
    virtual bool SetAutoLock(bool enable, int delaySeconds = 30) { return false; }
};

class ISmartSensor : public ISmartHomeDevice {
public:
    virtual SmartHomeDeviceCategory GetCategory() const override { return SmartHomeDeviceCategory::Sensor; }
    
    virtual SmartHomeSensorType GetSensorType() const = 0;
    virtual SmartHomeSensorReading GetReading() const = 0;
    
    virtual float GetValue() const = 0;
    virtual std::string GetUnit() const = 0;
    virtual bool IsTriggered() const = 0;  // For binary sensors
    
    virtual float GetMinValue() const { return 0.0f; }
    virtual float GetMaxValue() const { return 100.0f; }
};

class ISmartSwitch : public ISmartHomeDevice {
public:
    virtual SmartHomeDeviceCategory GetCategory() const override { return SmartHomeDeviceCategory::Switch; }
    
    virtual bool TurnOn() = 0;
    virtual bool TurnOff() = 0;
    virtual bool Toggle() = 0;
    virtual bool IsOn() const = 0;
    
    // Power monitoring (if supported)
    virtual bool SupportsPowerMonitoring() const { return false; }
    virtual float GetPower() const { return 0.0f; }  // Watts
    virtual float GetVoltage() const { return 0.0f; }
    virtual float GetCurrent() const { return 0.0f; }
    virtual float GetEnergyUsage() const { return 0.0f; }  // kWh
};

class ISmartBlind : public ISmartHomeDevice {
public:
    virtual SmartHomeDeviceCategory GetCategory() const override { return SmartHomeDeviceCategory::Blind; }
    
    virtual bool Open() = 0;
    virtual bool Close() = 0;
    virtual bool Stop() = 0;
    
    virtual bool SetPosition(uint8_t position) = 0;  // 0-100
    virtual uint8_t GetPosition() const = 0;
    
    virtual bool SupportsTilt() const { return false; }
    virtual bool SetTilt(uint8_t angle) { return false; }  // 0-100
    virtual uint8_t GetTilt() const { return 0; }
    
    virtual bool IsMoving() const = 0;
};

// ===== FACTORY FUNCTION =====

std::shared_ptr<ISmartHomeDevice> CreateSmartHomeDevice(
    SmartHomeDeviceCategory category,
    const std::string& deviceId,
    SmartHomeProtocolType protocol);

} // namespace SmartHome
} // namespace UltraCanvas
