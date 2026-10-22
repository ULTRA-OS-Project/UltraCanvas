// ui/UltraCanvasSmartHomeIcons.cpp
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHomeIcons.h"
#include "UltraCanvasConfig.h"

#include <algorithm>
#include <cctype>

namespace UltraCanvas {
namespace SmartHome {

namespace {

std::string Lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool Has(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

// What is plugged in, from the name a person gave the plug.
std::string FromName(const std::string& name) {
    const std::string n = Lowered(name);
    if (Has(n, "tv") || Has(n, "television")) return "tv";
    if (Has(n, "fridge") || Has(n, "freezer")) return "fridge";
    if (Has(n, "kettle") || Has(n, "coffee")) return "kettle";
    if (Has(n, "workstation") || Has(n, "computer") || Has(n, "desktop")) return "computer";
    if (Has(n, "towel") || Has(n, "heater") || Has(n, "radiator")) return "heater";
    if (Has(n, "fan")) return "fan";
    if (Has(n, "speaker")) return "speaker";
    if (Has(n, "washer") || Has(n, "washing") || Has(n, "dryer") || Has(n, "dishwasher")) return "appliance";
    if (Has(n, "light") || Has(n, "lamp")) return "light";
    return "";
}

} // namespace

std::string SmartHomeIconsDir() {
    return GetResourcesDir() + "media/icons/smarthome/";
}

std::string SmartHomeDeviceIconName(const SmartHomeDeviceInfo& device, SmartHomeSensorType sensor) {
    switch (device.Category) {
        case SmartHomeDeviceCategory::Light:      return "light";
        case SmartHomeDeviceCategory::Thermostat: return "thermostat";
        case SmartHomeDeviceCategory::Lock:
            return Has(Lowered(device.Name), "gate") ? "gate" : "lock";
        case SmartHomeDeviceCategory::Blind:      return "blind";
        case SmartHomeDeviceCategory::Camera:     return "camera";
        case SmartHomeDeviceCategory::Speaker:    return "speaker";
        case SmartHomeDeviceCategory::Fan:        return "fan";
        case SmartHomeDeviceCategory::Gateway:    return "gateway";
        case SmartHomeDeviceCategory::Appliance: {
            const std::string named = FromName(device.Name);
            return named.empty() ? "appliance" : named;
        }
        case SmartHomeDeviceCategory::Plug:
        case SmartHomeDeviceCategory::Switch: {
            const std::string named = FromName(device.Name);
            if (!named.empty()) return named;
            return device.Category == SmartHomeDeviceCategory::Plug ? "plug" : "switch";
        }
        case SmartHomeDeviceCategory::Sensor:
            switch (sensor) {
                case SmartHomeSensorType::Temperature:    return "temperature";
                case SmartHomeSensorType::Humidity:       return "humidity";
                case SmartHomeSensorType::Motion:
                case SmartHomeSensorType::Occupancy:      return "motion";
                case SmartHomeSensorType::Contact:        return "door";
                case SmartHomeSensorType::Smoke:          return "smoke";
                case SmartHomeSensorType::CarbonMonoxide: return "co";
                case SmartHomeSensorType::Water:          return "water";
                case SmartHomeSensorType::Light:          return "lightlevel";
                case SmartHomeSensorType::Pressure:       return "pressure";
                case SmartHomeSensorType::AirQuality:     return "air";
                case SmartHomeSensorType::Vibration:      return "vibration";
                case SmartHomeSensorType::Power:          return "energy";
                default:                                  return "sensor";
            }
        default:
            return "devices";
    }
}

std::string SmartHomeDeviceIconPath(const SmartHomeDeviceInfo& device) {
    SmartHomeSensorType sensor = SmartHomeSensorType::Unknown;
    if (device.Category == SmartHomeDeviceCategory::Sensor && !device.DeviceId.empty()) {
        sensor = SMARTHOME_API.GetSensorReading(device.DeviceId).Type;
    }
    return SmartHomeIconsDir() + SmartHomeDeviceIconName(device, sensor) + ".svg";
}

Color SmartHomeCategoryColor(SmartHomeDeviceCategory category) {
    switch (category) {
        case SmartHomeDeviceCategory::Light:      return Color(0xF5, 0xA6, 0x23);   // amber
        case SmartHomeDeviceCategory::Thermostat:
        case SmartHomeDeviceCategory::Fan:        return Color(0xF4, 0x6B, 0x45);   // climate
        case SmartHomeDeviceCategory::Lock:
        case SmartHomeDeviceCategory::Camera:     return Color(0x2E, 0x9E, 0x6B);   // security
        case SmartHomeDeviceCategory::Sensor:     return Color(0x3B, 0x82, 0xF6);   // sensing
        case SmartHomeDeviceCategory::Blind:      return Color(0x8D, 0x6E, 0x63);   // shade
        case SmartHomeDeviceCategory::Speaker:    return Color(0x8B, 0x5C, 0xF6);
        case SmartHomeDeviceCategory::Plug:
        case SmartHomeDeviceCategory::Switch:
        case SmartHomeDeviceCategory::Appliance:  return Color(0x4F, 0x6D, 0x8A);   // power
        case SmartHomeDeviceCategory::Gateway:    return Color(0x26, 0x32, 0x38);
        default:                                  return Color(0x78, 0x90, 0x9C);
    }
}

} // namespace SmartHome
} // namespace UltraCanvas
