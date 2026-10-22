// Apps/SmartHomeCenter/ui/SmartHomeCenterCli.cpp
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SmartHomeCenterCli.h"
#include "SmartHomeCenterDemo.h"
#include "SmartHomeCenterModel.h"
#include "SmartHomeCenterPaths.h"

#include "UltraCanvasSmartHome.h"
#include "UltraCanvasTextUtils.h"   // TryParseFloat / FormatFloatClassic

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <functional>
#include <vector>

using namespace UltraCanvas::SmartHome;

namespace SmartHomeCenter {

namespace {

constexpr int kCommandWaitMs = 5000;

std::string Lower(std::string text) {
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

bool HasTag(const SmartHomeDeviceInfo& info, const char* tag) {
    return std::find(info.Capabilities.begin(), info.Capabilities.end(), tag) != info.Capabilities.end();
}

bool ParseBool(const std::string& text, bool& value) {
    const std::string t = Lower(text);
    if (t == "true" || t == "on" || t == "1" || t == "yes") { value = true; return true; }
    if (t == "false" || t == "off" || t == "0" || t == "no") { value = false; return true; }
    return false;
}

// A field of a CSV row: quoted when it holds a comma, a quote or a line break.
std::string Csv(const std::string& field) {
    if (field.find_first_of(",\"\n\r") == std::string::npos) return field;
    std::string quoted = "\"";
    for (char c : field) {
        if (c == '"') quoted += '"';
        quoted += c;
    }
    return quoted + "\"";
}

// Local time; digits are the same in every locale.
std::string LocalTime(int64_t seconds, const char* format) {
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char text[40];
    std::strftime(text, sizeof text, format, &local);
    return text;
}

CliResult Fail(int code, const std::string& message) {
    CliResult r;
    r.exitCode = code;
    r.err = message + "\n";
    return r;
}

std::string Lines(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) text += line + "\n";
    return text;
}

} // namespace

// ===== STARTING =====

bool StartHome(bool forceDemo) {
    auto& api = SmartHomeAPI::Instance();
    api.Initialize();
    bool anyRadio = false;
    if (!forceDemo) {
        for (SmartHomeProtocolType type : {SmartHomeProtocolType::Matter, SmartHomeProtocolType::Thread,
                                           SmartHomeProtocolType::Zigbee, SmartHomeProtocolType::ZWave,
                                           SmartHomeProtocolType::KNX}) {
            // Only backends this build carries have a factory; EnableProtocol
            // refuses the rest, and a carried one with no hardware fails too.
            if (api.EnableProtocol(type)) anyRadio = true;
        }
    }
    if (anyRadio) return false;
    api.EnableProtocol(SmartHomeProtocolType::Simulated);
    return true;
}

bool OpenHome(bool simulated, const std::string& path, std::string* error) {
    auto& api = SmartHomeAPI::Instance();
    const std::string storePath = path.empty() ? DefaultStorePath(simulated) : path;
    bool opened = !storePath.empty() && api.OpenStore(storePath);
    if (!opened && error) *error = storePath.empty() ? "no home directory to keep the store in" : api.GetStoreError();
    if (simulated) SeedDemoHome();
    return opened;
}

std::string FindDevice(const std::string& nameOrId, std::string* error) {
    const auto devices = SmartHomeAPI::Instance().GetDevices();
    for (const auto& d : devices)
        if (d.DeviceId == nameOrId) return d.DeviceId;
    std::vector<const SmartHomeDeviceInfo*> named;
    const std::string key = Lower(nameOrId);
    for (const auto& d : devices)
        if (Lower(d.Name) == key) named.push_back(&d);
    if (named.size() == 1) return named.front()->DeviceId;
    if (error) {
        if (named.empty()) {
            *error = "No device is called \"" + nameOrId + "\" (--list shows them all)";
        } else {
            *error = std::to_string(named.size()) + " devices are called \"" + nameOrId + "\"; give one of their ids:";
            for (const auto* d : named) *error += " " + d->DeviceId + " (" + (d->Room.empty() ? kNoRoom : d->Room) + ")";
        }
    }
    return "";
}

bool ParseDuration(const std::string& text, int64_t& seconds) {
    if (text.empty()) return false;
    std::string digits = text;
    int64_t unit = 1;
    switch (Lower(text).back()) {
        case 's': unit = 1; digits.pop_back(); break;
        case 'm': unit = 60; digits.pop_back(); break;
        case 'h': unit = 3600; digits.pop_back(); break;
        case 'd': unit = 86400; digits.pop_back(); break;
        default: break;
    }
    if (digits.empty() || digits.size() > 9) return false;
    int64_t n = 0;
    for (char c : digits) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + (c - '0');
    }
    seconds = n * unit;
    return true;
}

// ===== LIST =====

CliResult ListHome(bool csv) {
    const std::vector<DeviceView> devices = DescribeHome();
    CliResult r;
    if (!csv) {
        r.out = FormatHomeText(devices);
        return r;
    }
    r.out = "room,name,type,state,value,battery,power_w,online,id\n";
    for (const std::string& room : RoomsOf(devices)) {
        for (const DeviceView& d : devices) {
            const std::string deviceRoom = d.info.Room.empty() ? kNoRoom : d.info.Room;
            if (deviceRoom != room) continue;
            r.out += Csv(d.info.Room) + "," + Csv(d.info.Name) + "," + Csv(d.typeName) + "," + Csv(d.state) + "," +
                     Csv(d.value) + "," + (d.battery >= 0 ? std::to_string(d.battery) : std::string()) + "," +
                     (d.metered ? UltraCanvas::FormatFloatClassic(std::round(d.power * 10.0f) / 10.0, 12) : std::string()) +
                     "," + (d.online ? "true" : "false") + "," + Csv(d.info.DeviceId) + "\n";
        }
    }
    return r;
}

// ===== SET =====

CliResult SetAttribute(const std::string& device, const std::string& assignment) {
    const size_t eq = assignment.find('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 >= assignment.size())
        return Fail(1, "--set needs <attribute>=<value>, such as on=true or brightness=40");
    const std::string attribute = Lower(assignment.substr(0, eq));
    const std::string value = assignment.substr(eq + 1);

    std::string error;
    const std::string id = FindDevice(device, &error);
    if (id.empty()) return Fail(1, error);
    auto& api = SmartHomeAPI::Instance();
    const DeviceView before = DescribeDevice(api.GetDevice(id));
    const std::string& name = before.info.Name;
    if (!before.online) return Fail(2, name + " is offline");

    auto refuse = [&](const std::string& why) { return Fail(2, name + " (" + before.typeName + ") " + why); };
    auto badValue = [&](const std::string& wanted) {
        return Fail(1, "\"" + value + "\" is not a value for " + attribute + " (" + wanted + ")");
    };
    double number = 0;
    const bool isNumber = UltraCanvas::TryParseFloat(value, number) && std::isfinite(number);
    bool flag = false;

    // What to check afterwards, for the attributes that take effect at once.
    std::function<bool()> took;

    if (attribute == "on") {
        if (!ParseBool(value, flag)) return badValue("true or false");
        if (before.kind == DeviceKind::Light) {
            api.SendCommand({id, flag ? "turnOn" : "turnOff", {}});
            took = [&api, id, flag] { return api.GetLightState(id).On == flag; };
        } else if (before.kind == DeviceKind::Switch) {
            api.SetSwitchState(id, flag);
            took = [&api, id, flag] { return api.GetSwitchState(id) == flag; };
        } else {
            return refuse("cannot be switched on or off");
        }
    } else if (attribute == "brightness") {
        if (before.kind != DeviceKind::Light || !HasTag(before.info, "dimmer")) return refuse("cannot be dimmed");
        if (!isNumber || number < 0 || number > 100) return badValue("0 to 100");
        const int level = static_cast<int>(std::lround(number * 255.0 / 100.0));
        if (level == 0) {
            api.SendCommand({id, "turnOff", {}});
            took = [&api, id] { return !api.GetLightState(id).On; };
        } else {
            api.SendCommand({id, "setBrightness", {{"level", std::to_string(level)}}});
            took = [&api, id, level] { return api.GetLightState(id).Brightness == level; };
        }
    } else if (attribute == "colortemp") {
        if (before.kind != DeviceKind::Light || !HasTag(before.info, "colourTemperature"))
            return refuse("has no colour temperature");
        if (!isNumber || number < 1000 || number > 10000) return badValue("a colour temperature in kelvin, such as 2700");
        const int kelvin = static_cast<int>(std::lround(number));
        api.SendCommand({id, "setColorTemp", {{"kelvin", std::to_string(kelvin)}}});
        // The light clamps to its own range; that it moved is enough.
        const int was = api.GetLightState(id).ColorTemp;
        took = [&api, id, was, kelvin] { const int now = api.GetLightState(id).ColorTemp; return now != was || now == kelvin; };
    } else if (attribute == "target") {
        if (before.kind != DeviceKind::Thermostat) return refuse("has no target temperature");
        if (!isNumber || number < 4 || number > 35) return badValue("4 to 35 °C");
        const float target = static_cast<float>(number);
        api.SetThermostatTarget(id, target);
        took = [&api, id, target] { return std::fabs(api.GetThermostatState(id).TargetTemperature - target) < 0.05f; };
    } else if (attribute == "mode") {
        if (before.kind != DeviceKind::Thermostat) return refuse("has no mode");
        const std::string mode = Lower(value);
        if (mode != "heat" && mode != "cool" && mode != "auto" && mode != "off") return badValue("heat, cool, auto or off");
        api.SetThermostatMode(id, mode);
        took = [&api, id, mode] { return api.GetThermostatState(id).Mode == mode; };
    } else if (attribute == "locked") {
        if (before.kind != DeviceKind::Lock) return refuse("is not a lock");
        if (!ParseBool(value, flag)) return badValue("true or false");
        api.SetLockState(id, flag);
        took = [&api, id, flag] { return api.GetLockState(id).Locked == flag; };
    } else if (attribute == "position") {
        if (before.kind != DeviceKind::Blind) return refuse("is not a blind");
        if (!isNumber || number < 0 || number > 100) return badValue("0 (closed) to 100 (open)");
        api.SetBlindPosition(id, static_cast<uint8_t>(std::lround(number)));
        // A blind takes its time to get there; that it was sent is the answer.
    } else {
        return Fail(1, "Unknown attribute \"" + attribute +
                       "\": use on, brightness, colorTemp, target, mode, locked or position");
    }

    if (!api.WaitForCommands(kCommandWaitMs)) return Fail(2, "The command to " + name + " was not sent in time");
    if (took && !took()) return Fail(2, name + " did not take " + attribute + "=" + value);
    const DeviceView after = DescribeDevice(api.GetDevice(id));
    CliResult r;
    r.out = name + ": " + (after.value.empty() ? after.state : after.value + "  " + after.state);
    if (attribute == "position") r.out += " (moving to " + FormatPercent(static_cast<int>(std::lround(number))) + ")";
    r.out += "\n";
    return r;
}

// ===== SCENE =====

CliResult RunScene(const std::string& scene) {
    auto& api = SmartHomeAPI::Instance();
    const auto scenes = api.GetScenes();
    const SmartHomeScene* found = nullptr;
    for (const auto& s : scenes)
        if (s.SceneId == scene) found = &s;
    if (!found) {
        for (const auto& s : scenes)
            if (Lower(s.Name) == Lower(scene)) found = &s;
    }
    if (!found) {
        std::string names;
        for (const auto& s : scenes) names += (names.empty() ? "" : ", ") + s.Name;
        return Fail(1, "No scene is called \"" + scene + "\"" + (names.empty() ? std::string(" (the home has none)")
                                                                              : " - the home has: " + names));
    }
    const std::string name = found->Name;
    const size_t actions = found->Actions.size();
    if (!api.ActivateScene(found->SceneId)) return Fail(2, "\"" + name + "\" could not be run");
    if (!api.WaitForCommands(kCommandWaitMs)) return Fail(2, "\"" + name + "\" was not sent in time");
    CliResult r;
    r.out = "Ran \"" + name + "\" (" + std::to_string(actions) + (actions == 1 ? " action)\n" : " actions)\n");
    return r;
}

// ===== HISTORY AND LOG =====

CliResult ShowHistory(const std::string& device, int64_t sinceSeconds, bool csv) {
    std::string error;
    const std::string id = FindDevice(device, &error);
    if (id.empty()) return Fail(1, error);
    auto& api = SmartHomeAPI::Instance();
    if (!api.IsStoreOpen()) return Fail(2, "No history: the home's store is not open (" + api.GetStoreError() + ")");
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    const uint64_t startMs = static_cast<uint64_t>(std::max<int64_t>(0, now - sinceSeconds)) * 1000;
    const auto readings = api.GetSensorHistory(id, startMs, UINT64_MAX);
    const std::string name = api.GetDevice(id).Name;
    CliResult r;
    if (csv) {
        r.out = "time,value,unit,triggered\n";
        for (const auto& reading : readings) {
            r.out += LocalTime(static_cast<int64_t>(reading.Timestamp / 1000), "%Y-%m-%dT%H:%M:%S") + "," +
                     UltraCanvas::FormatFloatClassic(reading.Value, 12) + "," + Csv(reading.Unit) + "," +
                     (reading.Triggered ? "true" : "false") + "\n";
        }
        return r;
    }
    std::vector<std::string> lines;
    lines.push_back(name + ": " + std::to_string(readings.size()) +
                    (readings.size() == 1 ? " reading" : " readings"));
    for (const auto& reading : readings) {
        char value[48];
        std::snprintf(value, sizeof value, "%.1f %s", reading.Value, reading.Unit.c_str());   // locale-ok: shown to the person
        lines.push_back("  " + LocalTime(static_cast<int64_t>(reading.Timestamp / 1000), "%Y-%m-%d %H:%M:%S") + "  " +
                        value + (reading.Triggered ? "  triggered" : ""));
    }
    r.out = Lines(lines);
    return r;
}

CliResult ShowLog(int64_t sinceSeconds) {
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    std::vector<SmartHomeEvent> events = SmartHomeAPI::Instance().GetEventLog(now - sinceSeconds, 1000);
    std::reverse(events.begin(), events.end());   // oldest first, as a log reads
    CliResult r;
    for (const auto& e : events) {
        std::string kind = "[" + e.Kind + "]";
        if (kind.size() < 13) kind.append(13 - kind.size(), ' ');
        r.out += LocalTime(e.Time, "%Y-%m-%d %H:%M:%S") + "  " + kind + e.Text + "\n";
    }
    if (events.empty()) r.out = "Nothing in the log since " + LocalTime(now - sinceSeconds, "%Y-%m-%d %H:%M") + "\n";
    return r;
}

} // namespace SmartHomeCenter
