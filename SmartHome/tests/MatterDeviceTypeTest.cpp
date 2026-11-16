// SmartHome/tests/MatterDeviceTypeTest.cpp
// What a Matter node is, from the device types its endpoints declare
// (protocols/Matter/MatterDeviceTypes.h). Needs no connectedhomeip, so it runs
// on every build; the read that supplies the types is MatterProtocol's.
//
// The ids checked here are written from the Matter Device Library as the
// SDK carries it (data_model/1.6/device_types/*.xml), not copied from the
// header, so a mistyped constant fails rather than agreeing with itself.
//
// Author: UltraCanvas Framework
#include "MatterDeviceTypes.h"
#include <cstdio>
#include <vector>

using namespace UltraCanvas::SmartHome;

static int failures = 0;

static void Check(bool condition, const char* what) {
    if (condition) { std::printf("ok  %s\n", what); }
    else           { std::printf("FAIL %s\n", what); ++failures; }
}

static bool Is(const std::vector<uint32_t>& types, SmartHomeDeviceCategory expected) {
    return MatterCategoryForDeviceTypes(types) == expected;
}

int main() {
    using C = SmartHomeDeviceCategory;
    constexpr uint32_t kRootNode = 0x0016, kPowerSource = 0x0011, kBridgedNode = 0x0013;

    // ---- the appliances Matter 1.2 and 1.3 added ----
    Check(Is({kRootNode, 0x0074}, C::Appliance), "robotic vacuum cleaner (0x0074) is an appliance");
    Check(Is({kRootNode, 0x0073}, C::Appliance), "laundry washer (0x0073) is an appliance");
    Check(Is({kRootNode, 0x007C}, C::Appliance), "laundry dryer (0x007C) is an appliance");
    Check(Is({kRootNode, 0x0075}, C::Appliance), "dishwasher (0x0075) is an appliance");
    Check(Is({kRootNode, 0x0070, 0x0071, 0x0071}, C::Appliance),
          "refrigerator with two cabinets is an appliance");
    Check(Is({kRootNode, 0x007B}, C::Appliance), "oven (0x007B) is an appliance");
    Check(Is({kRootNode, 0x0079}, C::Appliance), "microwave oven (0x0079) is an appliance");
    Check(Is({kRootNode, 0x0072}, C::Appliance),
          "room air conditioner is an appliance, not a heating thermostat");

    // ---- the types the backend already knew ----
    Check(Is({kRootNode, 0x0100}, C::Light), "on/off light");
    Check(Is({kRootNode, 0x010D}, C::Light), "extended colour light");
    Check(Is({kRootNode, 0x010A}, C::Plug), "on/off plug-in unit");
    Check(Is({kRootNode, 0x000A}, C::Lock), "door lock");
    Check(Is({kRootNode, 0x0301}, C::Thermostat), "thermostat");
    Check(Is({kRootNode, 0x0202}, C::Blind), "window covering");
    Check(Is({kRootNode, 0x002B}, C::Fan), "fan");
    Check(Is({kRootNode, 0x0302}, C::Sensor), "temperature sensor");
    Check(Is({kRootNode, 0x0076}, C::Sensor), "smoke / CO alarm");
    Check(Is({kRootNode, 0x0022}, C::Speaker), "speaker");

    // ---- added alongside ----
    Check(Is({kRootNode, 0x002D}, C::Fan), "air purifier is a fan");
    Check(Is({kRootNode, 0x0043}, C::Sensor), "water leak detector");
    Check(Is({kRootNode, 0x002C}, C::Sensor), "air quality sensor");
    Check(Is({kRootNode, 0x0142}, C::Camera), "camera");
    Check(Is({kRootNode, 0x010F}, C::Switch), "mounted on/off control is a switch");

    // ---- nothing to go on is Unknown, never a guess ----
    Check(Is({}, C::Unknown), "no device types: unknown (the read failed)");
    Check(Is({kRootNode}, C::Unknown), "root node alone: unknown");
    Check(Is({kRootNode, kPowerSource}, C::Unknown), "utility types decide nothing");
    Check(Is({kRootNode, 0x0309}, C::Unknown), "a type with no category here (heat pump): unknown");

    // ---- a node that is several things ----
    Check(Is({kRootNode, 0x0107, 0x0100}, C::Light),
          "a light carrying an occupancy sensor on an earlier endpoint is a light");
    Check(Is({kRootNode, 0x0074, kPowerSource}, C::Appliance),
          "a vacuum with its battery's power source is an appliance");
    Check(Is({kRootNode, 0x000E, 0x0100, kBridgedNode, 0x010A, kBridgedNode}, C::Gateway),
          "a bridge (aggregator) in front of bridged devices is a gateway");
    Check(Is({kRootNode, 0x0104, 0x0101}, C::Light),
          "a dimmer switch next to the light it dims is a light");
    Check(Is({kRootNode, 0x0100, 0x0101}, C::Light), "two lights: a light");

    if (failures) { std::printf("%d check(s) FAILED\n", failures); return 1; }
    std::printf("PASS - Matter device types map to the module's categories\n");
    return 0;
}
