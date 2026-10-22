// Apps/SmartHomeCenter/ui/SmartHomeCenterModel.cpp
// Version: 0.2.0 - rooms in the home model's order
// Author: UltraCanvas Framework / ULTRA OS
#include "SmartHomeCenterModel.h"
// The module's icon choice, so a device looks the same here as in its widgets.
#include "UltraCanvasSmartHomeIcons.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

using namespace UltraCanvas::SmartHome;

namespace SmartHomeCenter {

const char* const kNoRoom = "Not in a room";

namespace {

bool HasTag(const SmartHomeDeviceInfo& info, const char* tag) {
    return std::find(info.Capabilities.begin(), info.Capabilities.end(), tag) != info.Capabilities.end();
}

// "sim-2" before "sim-10": ids carry numbers, and the order a backend
// numbered its devices in is the order its rooms were furnished in.
bool NaturalLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (std::isdigit(static_cast<unsigned char>(a[i])) && std::isdigit(static_cast<unsigned char>(b[j]))) {
            size_t ie = i, je = j;
            while (ie < a.size() && std::isdigit(static_cast<unsigned char>(a[ie]))) ++ie;
            while (je < b.size() && std::isdigit(static_cast<unsigned char>(b[je]))) ++je;
            const std::string na = a.substr(i, ie - i), nb = b.substr(j, je - j);
            if (na.size() != nb.size()) return na.size() < nb.size();
            if (na != nb) return na < nb;
            i = ie;
            j = je;
            continue;
        }
        if (a[i] != b[j]) return a[i] < b[j];
        ++i;
        ++j;
    }
    return a.size() - i < b.size() - j;
}

std::string SensorTypeName(SmartHomeSensorType type) {
    switch (type) {
        case SmartHomeSensorType::Temperature: return "Temperature sensor";
        case SmartHomeSensorType::Humidity: return "Humidity sensor";
        case SmartHomeSensorType::Motion: return "Motion sensor";
        case SmartHomeSensorType::Occupancy: return "Presence sensor";
        case SmartHomeSensorType::Contact: return "Door / window sensor";
        case SmartHomeSensorType::Water: return "Leak sensor";
        case SmartHomeSensorType::Smoke: return "Smoke alarm";
        case SmartHomeSensorType::CarbonMonoxide: return "CO alarm";
        default: return "Sensor";
    }
}

// What a binary sensor says, raised and at rest.
void BinaryWords(SmartHomeSensorType type, std::string& raised, std::string& rest) {
    switch (type) {
        case SmartHomeSensorType::Motion: raised = "Motion detected"; rest = "No motion"; break;
        case SmartHomeSensorType::Occupancy: raised = "Occupied"; rest = "Empty"; break;
        case SmartHomeSensorType::Contact: raised = "Open"; rest = "Closed"; break;
        case SmartHomeSensorType::Water: raised = "Leak detected"; rest = "Dry"; break;
        case SmartHomeSensorType::Smoke: raised = "Smoke detected"; rest = "Clear"; break;
        case SmartHomeSensorType::CarbonMonoxide: raised = "CO detected"; rest = "Clear"; break;
        default: raised = "Triggered"; rest = "Idle"; break;
    }
}

// A leak or smoke is an alarm; motion and an open door are just news.
bool IsAlarm(SmartHomeSensorType type) {
    return type == SmartHomeSensorType::Water || type == SmartHomeSensorType::Smoke ||
           type == SmartHomeSensorType::CarbonMonoxide;
}

std::string ModeWords(const std::string& mode) {
    if (mode == "heat") return "Heat";
    if (mode == "cool") return "Cool";
    if (mode == "auto") return "Auto";
    if (mode == "off") return "Off";
    return mode;
}

} // namespace

// ===== FORMATTING =====
// These are read by a person, so they follow the person's locale on
// purpose: a German desktop shows "21,4 °C".

std::string FormatTemperature(float celsius) {
    char text[32];
    std::snprintf(text, sizeof text, "%.1f °C", celsius);   // locale-ok: shown to the person
    return text;
}

std::string FormatPower(float watts) {
    char text[32];
    if (watts >= 1000.0f) std::snprintf(text, sizeof text, "%.2f kW", watts / 1000.0f);   // locale-ok: shown to the person
    else std::snprintf(text, sizeof text, "%.0f W", watts);                              // locale-ok: shown to the person
    return text;
}

std::string FormatEnergy(float kWh) {
    char text[32];
    std::snprintf(text, sizeof text, "%.1f kWh", kWh);   // locale-ok: shown to the person
    return text;
}

std::string FormatPercent(int percent) {
    return std::to_string(percent) + " %";
}

std::string KindName(DeviceKind kind) {
    switch (kind) {
        case DeviceKind::Light: return "Light";
        case DeviceKind::Switch: return "Switch";
        case DeviceKind::Thermostat: return "Thermostat";
        case DeviceKind::Lock: return "Lock";
        case DeviceKind::Blind: return "Blind";
        case DeviceKind::ClimateSensor: return "Climate sensor";
        case DeviceKind::BinarySensor: return "Sensor";
        default: return "Device";
    }
}

// ===== ONE DEVICE =====

DeviceView DescribeDevice(const SmartHomeDeviceInfo& info) {
    auto& api = SmartHomeAPI::Instance();
    DeviceView view;
    view.info = info;
    view.online = info.Reachable && info.State != SmartHomeDeviceState::Offline;
    view.battery = api.GetBatteryLevel(info.DeviceId);
    const std::string& id = info.DeviceId;
    SmartHomeSensorType sensorKind = SmartHomeSensorType::Unknown;

    switch (info.Category) {
        case SmartHomeDeviceCategory::Light: {
            view.kind = DeviceKind::Light;
            view.typeName = HasTag(info, "colour") ? "Colour light"
                          : HasTag(info, "colourTemperature") ? "Tunable white light"
                          : HasTag(info, "dimmer") ? "Dimmable light" : "Light";
            const SmartHomeLightState light = api.GetLightState(id);
            view.canToggle = true;
            view.on = light.On;
            view.active = light.On;
            if (!light.On) view.state = "Off";
            else if (HasTag(info, "dimmer")) view.state = "On · " + FormatPercent((light.Brightness * 100 + 127) / 255);
            else view.state = "On";
            view.eventKey = light.On ? "on" : "off";
            break;
        }
        case SmartHomeDeviceCategory::Switch:
        case SmartHomeDeviceCategory::Plug: {
            view.kind = DeviceKind::Switch;
            view.typeName = info.Category == SmartHomeDeviceCategory::Plug ? "Smart plug" : "Switch";
            view.canToggle = true;
            view.on = api.GetSwitchState(id);
            view.active = view.on;
            view.metered = HasTag(info, "power");
            view.power = api.GetPowerUsage(id);
            view.energy = api.GetEnergyUsage(id);
            view.state = view.on ? "On" : "Off";
            if (view.metered && view.on) view.state += " · " + FormatPower(view.power);
            view.eventKey = view.on ? "on" : "off";
            break;
        }
        case SmartHomeDeviceCategory::Thermostat: {
            view.kind = DeviceKind::Thermostat;
            view.typeName = "Thermostat";
            const SmartHomeThermostatState t = api.GetThermostatState(id);
            view.value = FormatTemperature(t.CurrentTemperature);
            view.active = t.Running;
            if (t.Mode == "off") view.state = "Off";
            else if (t.Running) view.state = (t.Mode == "cool" ? "Cooling to " : "Heating to ") + FormatTemperature(t.TargetTemperature);
            else view.state = ModeWords(t.Mode) + " · set to " + FormatTemperature(t.TargetTemperature);
            view.eventKey = t.Mode + (t.Running ? ":running" : ":idle");
            break;
        }
        case SmartHomeDeviceCategory::Lock: {
            view.kind = DeviceKind::Lock;
            const SmartHomeLockState lock = api.GetLockState(id);
            view.icon = lock.Locked ? "" : "unlock";   // an open lock shows open
            view.typeName = "Door lock";
            view.state = lock.Jammed ? "Jammed" : lock.Locked ? "Locked" : "Unlocked";
            view.active = !lock.Locked;
            view.alert = lock.Jammed;
            view.eventKey = view.state;
            break;
        }
        case SmartHomeDeviceCategory::Blind: {
            view.kind = DeviceKind::Blind;
            view.typeName = "Blind";
            const int position = api.GetBlindPosition(id);
            view.state = position >= 99 ? "Open" : position <= 1 ? "Closed" : "Partly open";
            view.value = FormatPercent(position);
            view.active = position > 1;
            view.eventKey = position >= 99 ? "open" : position <= 1 ? "closed" : "partly";
            break;
        }
        case SmartHomeDeviceCategory::Sensor: {
            const SmartHomeSensorReading reading = api.GetSensorReading(id);
            sensorKind = reading.Type;
            view.typeName = SensorTypeName(reading.Type);
            if (reading.Type == SmartHomeSensorType::Temperature || reading.Type == SmartHomeSensorType::Humidity) {
                view.kind = DeviceKind::ClimateSensor;
                view.value = reading.Type == SmartHomeSensorType::Temperature
                           ? FormatTemperature(reading.Value)
                           : FormatPercent(static_cast<int>(std::lround(reading.Value)));
                view.state = reading.Type == SmartHomeSensorType::Temperature ? "Temperature" : "Humidity";
                view.eventKey = "value";
            } else {
                view.kind = DeviceKind::BinarySensor;
                std::string raised, rest;
                BinaryWords(reading.Type, raised, rest);
                view.state = reading.Triggered ? raised : rest;
                view.active = reading.Triggered;
                view.alert = reading.Triggered && IsAlarm(reading.Type);
                view.eventKey = reading.Triggered ? "raised" : "rest";
            }
            break;
        }
        default:
            view.typeName = DeviceCategoryToString(info.Category);
            view.state = view.online ? "Online" : "Offline";
            view.eventKey = view.state;
            break;
    }

    // The module's icon (a plug called "Kettle" gets the kettle), except an
    // unlocked door lock, which shows open.
    const std::string moduleIcon = SmartHomeDeviceIconName(info, sensorKind);
    view.icon = view.icon == "unlock" && moduleIcon == "lock" ? "unlock" : moduleIcon;

    if (!view.online) {
        view.state = "Offline";
        view.active = false;
        view.alert = true;
        view.canToggle = false;
        view.eventKey = "offline";
    } else if (view.battery >= 0 && view.battery < 15 && !view.alert) {
        view.alert = true;
        view.state += " · battery low";
    }
    return view;
}

// ===== THE HOME =====

HomeSummary SummarizeHome(const std::vector<DeviceView>& devices) {
    HomeSummary summary;
    float temperatureSum = 0.0f;
    int temperatures = 0;
    for (const DeviceView& d : devices) {
        ++summary.devices;
        if (d.online) ++summary.online;
        if (d.kind == DeviceKind::Light) {
            ++summary.lights;
            if (d.on) ++summary.lightsOn;
        }
        if (d.kind == DeviceKind::Lock) {
            ++summary.locks;
            if (d.active) {
                if (summary.unlocked++ == 0) summary.firstUnlocked = d.info.Name;
            }
        }
        if (d.kind == DeviceKind::Thermostat && d.online) {
            const SmartHomeThermostatState t = SmartHomeAPI::Instance().GetThermostatState(d.info.DeviceId);
            temperatureSum += t.CurrentTemperature;
            ++temperatures;
            if (t.Running) ++summary.heating;
        }
        if (d.metered) summary.power += d.power;
        if (d.alert) {
            if (summary.alerts++ == 0) summary.firstAlert = d.state + " - " + d.info.Name;
        }
    }
    if (temperatures > 0) {
        summary.insideTemperature = temperatureSum / static_cast<float>(temperatures);
        summary.hasInsideTemperature = true;
    }
    return summary;
}

std::vector<std::string> RoomsOf(const std::vector<DeviceView>& devices) {
    // The home's own order (SmartHomeAPI::GetRooms), then any name a device
    // carries that the home does not know, then the devices in no room.
    std::vector<std::string> named;
    bool unplaced = false;
    for (const DeviceView& d : devices) {
        if (d.info.Room.empty()) unplaced = true;
        else if (std::find(named.begin(), named.end(), d.info.Room) == named.end()) named.push_back(d.info.Room);
    }
    std::vector<std::string> rooms;
    for (const SmartHomeRoom& room : SmartHomeAPI::Instance().GetRooms()) {
        if (std::find(named.begin(), named.end(), room.Name) != named.end()) rooms.push_back(room.Name);
    }
    for (const std::string& name : named) {
        if (std::find(rooms.begin(), rooms.end(), name) == rooms.end()) rooms.push_back(name);
    }
    if (unplaced) rooms.push_back(kNoRoom);
    return rooms;
}

std::vector<DeviceView> DescribeHome() {
    std::vector<SmartHomeDeviceInfo> infos = SmartHomeAPI::Instance().GetDevices();
    std::sort(infos.begin(), infos.end(), [](const SmartHomeDeviceInfo& a, const SmartHomeDeviceInfo& b) {
        return NaturalLess(a.DeviceId, b.DeviceId);
    });
    std::vector<DeviceView> views;
    views.reserve(infos.size());
    for (const auto& info : infos) views.push_back(DescribeDevice(info));
    return views;
}

std::string FormatHomeText(const std::vector<DeviceView>& devices) {
    std::string text;
    const HomeSummary summary = SummarizeHome(devices);
    text += std::to_string(summary.devices) + " devices, " + std::to_string(summary.online) + " online";
    if (summary.hasInsideTemperature) text += ", inside " + FormatTemperature(summary.insideTemperature);
    text += ", " + FormatPower(summary.power) + " metered\n";
    for (const std::string& room : RoomsOf(devices)) {
        text += "\n" + room + "\n";
        for (const DeviceView& d : devices) {
            const std::string deviceRoom = d.info.Room.empty() ? kNoRoom : d.info.Room;
            if (deviceRoom != room) continue;
            std::string line = "  " + d.info.Name;
            if (line.size() < 30) line.append(30 - line.size(), ' ');
            line += d.value.empty() ? d.state : d.value + "  " + d.state;
            if (d.alert) line += "  [!]";
            text += line + "\n";
        }
    }
    return text;
}

} // namespace SmartHomeCenter
