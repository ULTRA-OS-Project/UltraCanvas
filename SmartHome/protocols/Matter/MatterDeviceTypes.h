// MatterDeviceTypes.h
// What a Matter device is, from the device types its endpoints declare.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework
//
// A Matter node says what it is in the Descriptor cluster of each endpoint:
// a DeviceTypeList of ids from the Matter Device Library. The backend reads
// those lists once a node is commissioned (MatterProtocol.cpp) and this file
// turns them into the module's SmartHomeDeviceCategory.
//
// Nothing here needs connectedhomeip, so it is tested on every build
// (tests/MatterDeviceTypeTest.cpp). The ids are the ones in the SDK's copy
// of the specification, data_model/<version>/device_types/*.xml.
#pragma once

#include "UltraCanvasSmartHome.h"
#include <cstdint>
#include <vector>

namespace UltraCanvas {
namespace SmartHome {

namespace MatterDeviceTypes {
    // Utility types: they describe the node, not what it does, and never
    // decide its category.
    constexpr uint32_t RootNode = 0x0016;
    constexpr uint32_t PowerSource = 0x0011;
    constexpr uint32_t OtaRequestor = 0x0012;
    constexpr uint32_t BridgedNode = 0x0013;
    constexpr uint32_t ElectricalSensor = 0x0510;
    constexpr uint32_t DeviceEnergyManagement = 0x050D;

    constexpr uint32_t Aggregator = 0x000E;        // a bridge

    constexpr uint32_t OnOffLight = 0x0100;
    constexpr uint32_t DimmableLight = 0x0101;
    constexpr uint32_t ColorTemperatureLight = 0x010C;
    constexpr uint32_t ExtendedColorLight = 0x010D;
    constexpr uint32_t OnOffPlug = 0x010A;
    constexpr uint32_t DimmablePlug = 0x010B;
    constexpr uint32_t MountedOnOffControl = 0x010F;
    constexpr uint32_t MountedDimmableLoadControl = 0x0110;
    constexpr uint32_t OnOffSwitch = 0x0103;
    constexpr uint32_t DimmerSwitch = 0x0104;
    constexpr uint32_t ColorDimmerSwitch = 0x0105;
    constexpr uint32_t GenericSwitch = 0x000F;
    constexpr uint32_t Thermostat = 0x0301;
    constexpr uint32_t DoorLock = 0x000A;
    constexpr uint32_t WindowCovering = 0x0202;
    constexpr uint32_t Fan = 0x002B;
    constexpr uint32_t AirPurifier = 0x002D;
    constexpr uint32_t Speaker = 0x0022;

    constexpr uint32_t TemperatureSensor = 0x0302;
    constexpr uint32_t HumiditySensor = 0x0307;
    constexpr uint32_t OccupancySensor = 0x0107;
    constexpr uint32_t ContactSensor = 0x0015;
    constexpr uint32_t LightSensor = 0x0106;
    constexpr uint32_t PressureSensor = 0x0305;
    constexpr uint32_t FlowSensor = 0x0306;
    constexpr uint32_t SmokeCoAlarm = 0x0076;
    constexpr uint32_t AirQualitySensor = 0x002C;
    constexpr uint32_t WaterLeakDetector = 0x0043;
    constexpr uint32_t WaterFreezeDetector = 0x0041;
    constexpr uint32_t RainSensor = 0x0044;

    constexpr uint32_t Camera = 0x0142;
    constexpr uint32_t VideoDoorbell = 0x0143;
    constexpr uint32_t FloodlightCamera = 0x0144;
    constexpr uint32_t SnapshotCamera = 0x0145;

    // Appliances (Matter 1.2 and 1.3).
    constexpr uint32_t Refrigerator = 0x0070;
    constexpr uint32_t TemperatureControlledCabinet = 0x0071;
    constexpr uint32_t RoomAirConditioner = 0x0072;
    constexpr uint32_t LaundryWasher = 0x0073;
    constexpr uint32_t RoboticVacuumCleaner = 0x0074;
    constexpr uint32_t Dishwasher = 0x0075;
    constexpr uint32_t CookSurface = 0x0077;
    constexpr uint32_t Cooktop = 0x0078;
    constexpr uint32_t MicrowaveOven = 0x0079;
    constexpr uint32_t ExtractorHood = 0x007A;
    constexpr uint32_t Oven = 0x007B;
    constexpr uint32_t LaundryDryer = 0x007C;
}

// The category one device type names, or Unknown for a utility type and a
// type this module has no category for (a heat pump, an EVSE).
//
// A room air conditioner is an Appliance, not a Thermostat, although it
// carries a Thermostat cluster: the Thermostat controls set the heating
// setpoint, which is the wrong one for a machine that cools.
inline SmartHomeDeviceCategory MatterDeviceTypeCategory(uint32_t deviceType) {
    using namespace MatterDeviceTypes;
    switch (deviceType) {
        case Aggregator:
            return SmartHomeDeviceCategory::Gateway;
        case OnOffLight: case DimmableLight: case ColorTemperatureLight: case ExtendedColorLight:
            return SmartHomeDeviceCategory::Light;
        case OnOffPlug: case DimmablePlug:
            return SmartHomeDeviceCategory::Plug;
        case OnOffSwitch: case DimmerSwitch: case ColorDimmerSwitch: case GenericSwitch:
        case MountedOnOffControl: case MountedDimmableLoadControl:
            return SmartHomeDeviceCategory::Switch;
        case Thermostat:
            return SmartHomeDeviceCategory::Thermostat;
        case DoorLock:
            return SmartHomeDeviceCategory::Lock;
        case WindowCovering:
            return SmartHomeDeviceCategory::Blind;
        case Fan: case AirPurifier:
            return SmartHomeDeviceCategory::Fan;
        case Speaker:
            return SmartHomeDeviceCategory::Speaker;
        case TemperatureSensor: case HumiditySensor: case OccupancySensor: case ContactSensor:
        case LightSensor: case PressureSensor: case FlowSensor: case SmokeCoAlarm:
        case AirQualitySensor: case WaterLeakDetector: case WaterFreezeDetector: case RainSensor:
            return SmartHomeDeviceCategory::Sensor;
        case Camera: case VideoDoorbell: case FloodlightCamera: case SnapshotCamera:
            return SmartHomeDeviceCategory::Camera;
        case Refrigerator: case TemperatureControlledCabinet: case RoomAirConditioner:
        case LaundryWasher: case RoboticVacuumCleaner: case Dishwasher: case CookSurface:
        case Cooktop: case MicrowaveOven: case ExtractorHood: case Oven: case LaundryDryer:
            return SmartHomeDeviceCategory::Appliance;
        default:
            return SmartHomeDeviceCategory::Unknown;
    }
}

// The category of a whole node from the device types of all its endpoints,
// in endpoint order. A node is often several things at once - a fridge is a
// Refrigerator with two Temperature Controlled Cabinets, a light may carry an
// occupancy sensor, a bridge is an Aggregator in front of the devices it
// bridges - so the type that says most about what a person controls wins:
// a bridge first, then an appliance, then the other actuators, a sensor
// last. Between two of the same rank the earlier endpoint wins.
inline SmartHomeDeviceCategory MatterCategoryForDeviceTypes(const std::vector<uint32_t>& deviceTypes) {
    static constexpr SmartHomeDeviceCategory kRank[] = {
        SmartHomeDeviceCategory::Gateway,   SmartHomeDeviceCategory::Appliance,
        SmartHomeDeviceCategory::Lock,      SmartHomeDeviceCategory::Thermostat,
        SmartHomeDeviceCategory::Blind,     SmartHomeDeviceCategory::Camera,
        SmartHomeDeviceCategory::Light,     SmartHomeDeviceCategory::Fan,
        SmartHomeDeviceCategory::Plug,      SmartHomeDeviceCategory::Switch,
        SmartHomeDeviceCategory::Speaker,   SmartHomeDeviceCategory::Sensor,
    };
    constexpr size_t kUnranked = sizeof(kRank) / sizeof(kRank[0]);
    size_t best = kUnranked;
    for (uint32_t type : deviceTypes) {
        const SmartHomeDeviceCategory category = MatterDeviceTypeCategory(type);
        for (size_t rank = 0; rank < best; ++rank) {
            if (kRank[rank] == category) { best = rank; break; }
        }
    }
    return best == kUnranked ? SmartHomeDeviceCategory::Unknown : kRank[best];
}

} // namespace SmartHome
} // namespace UltraCanvas
