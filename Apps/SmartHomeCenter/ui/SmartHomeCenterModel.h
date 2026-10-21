// Apps/SmartHomeCenter/ui/SmartHomeCenterModel.h
// What the window shows about a device and about the home, worked out from
// SmartHomeAPI alone: no UI types, so --list and the window agree, and both
// read the same words for the same state.
//
// A DeviceView is one device as a person sees it: what kind of control it
// needs, its icon, one line of state ("On · 50 %", "21.4 °C · heating to
// 23.5 °C", "Locked", "Leak detected"), whether it is active (lit, running,
// open) or alerting (a leak, smoke, offline, a flat battery). `eventKey`
// changes only when something a person would call an event happens - on to
// off, locked to unlocked, a sensor going off - and not when a temperature
// drifts by a tenth, which is what the Activity page logs by.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasSmartHome.h"

#include <string>
#include <vector>

namespace SmartHomeCenter {

using UltraCanvas::SmartHome::SmartHomeDeviceInfo;

enum class DeviceKind {
    Light,
    Switch,          // relays and plugs
    Thermostat,
    Lock,
    Blind,
    ClimateSensor,   // a value: temperature, humidity
    BinarySensor,    // an event: motion, contact, water, smoke
    Other
};

struct DeviceView {
    SmartHomeDeviceInfo info;
    DeviceKind kind = DeviceKind::Other;
    std::string icon;            // file name in media/icons/smarthome/, no extension
    std::string typeName;        // "Light", "Smart plug", "Leak sensor", ...
    std::string state;           // one line, as shown on a tile
    std::string value;           // the big number of a climate tile or sensor ("21.4 °C"), or ""
    bool active = false;         // on, lit, heating, unlocked, open, detecting
    bool alert = false;          // needs attention now
    bool online = true;
    bool canToggle = false;      // a switch belongs on the tile
    bool on = false;             // the switch's position
    int battery = -1;            // percent, -1 on mains
    float power = 0.0f;          // watts, metered plugs only
    float energy = 0.0f;         // kWh
    bool metered = false;
    std::string eventKey;
};

DeviceView DescribeDevice(const SmartHomeDeviceInfo& info);

// The figures across the top of the Home page.
struct HomeSummary {
    int devices = 0;
    int online = 0;
    int lightsOn = 0;
    int lights = 0;
    int locks = 0;
    int unlocked = 0;
    std::string firstUnlocked;   // a name to show when one is open
    float insideTemperature = 0.0f;   // mean of the thermostats' rooms
    bool hasInsideTemperature = false;
    int heating = 0;             // thermostats calling for heat
    float power = 0.0f;          // metered draw now, watts
    int alerts = 0;
    std::string firstAlert;      // "Leak detected - Under-sink leak sensor"
};

HomeSummary SummarizeHome(const std::vector<DeviceView>& devices);

// Rooms in a stable order: the order devices name them in, rooms with no
// name last as "Not in a room".
std::vector<std::string> RoomsOf(const std::vector<DeviceView>& devices);
extern const char* const kNoRoom;

// Shown text, in the person's own locale (these are read, never parsed).
std::string FormatTemperature(float celsius);
std::string FormatPower(float watts);
std::string FormatEnergy(float kWh);
std::string FormatPercent(int percent);
std::string KindName(DeviceKind kind);

// Every device, described, sorted by room order then name.
std::vector<DeviceView> DescribeHome();

// The text of --list: one block per room, one line per device.
std::string FormatHomeText(const std::vector<DeviceView>& devices);

} // namespace SmartHomeCenter
