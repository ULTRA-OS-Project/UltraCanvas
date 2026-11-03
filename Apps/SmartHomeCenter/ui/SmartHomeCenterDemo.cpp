// Apps/SmartHomeCenter/ui/SmartHomeCenterDemo.cpp
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SmartHomeCenterDemo.h"

#include "UltraCanvasSmartHome.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace UltraCanvas::SmartHome;

namespace SmartHomeCenter {

const char* const kSceneMorning = "center.morning";
const char* const kSceneEvening = "center.evening";
const char* const kSceneAway = "center.away";
const char* const kSceneNight = "center.night";

namespace {

void SeedScenes(SmartHomeAPI& api, const std::vector<SmartHomeDeviceInfo>& devices) {
    // Once: a home that has scenes keeps the ones the person left it with.
    if (!api.GetScenes().empty()) return;
    auto idOf = [&](const char* name) {
        for (const auto& d : devices) if (d.Name == name) return d.DeviceId;
        return std::string();
    };
    auto cmd = [&](const char* name, const char* command, std::map<std::string, std::string> params = {}) {
        return SmartHomeCommand{idOf(name), command, std::move(params)};
    };
    const std::vector<SmartHomeScene> scenes = {
        {kSceneMorning, "Good morning", "sunrise", {
            cmd("Patio blinds", "setPosition", {{"position", "100"}}),
            cmd("Bedroom blinds", "setPosition", {{"position", "100"}}),
            cmd("Kitchen spots", "turnOn"), cmd("Kettle", "turnOn"),
            cmd("Living room heating", "setTargetTemp", {{"temperature", "21"}}),
            cmd("Office heating", "setTargetTemp", {{"temperature", "21"}})}, false},
        {kSceneEvening, "Evening", "sunset", {
            cmd("Ceiling light", "setLight", {{"on", "true"}, {"brightness", "120"}, {"colorTemp", "2600"}}),
            cmd("Floor lamp", "setLight", {{"on", "true"}, {"brightness", "150"}, {"colorTemp", "2300"}}),
            cmd("Patio blinds", "setPosition", {{"position", "0"}}),
            cmd("Path lights", "turnOn"), cmd("Desk lamp", "turnOff"), cmd("Workstation", "turnOff")}, false},
        {kSceneAway, "Away", "away", {
            cmd("Ceiling light", "turnOff"), cmd("Floor lamp", "turnOff"), cmd("Kitchen spots", "turnOff"),
            cmd("Desk lamp", "turnOff"), cmd("Bedside lamp", "turnOff"), cmd("Mirror light", "turnOff"),
            cmd("Hall light", "turnOff"), cmd("TV", "turnOff"), cmd("Kettle", "turnOff"), cmd("Towel rail", "turnOff"),
            cmd("Front door", "lock"), cmd("Garden gate", "lock")}, false},
        {kSceneNight, "Good night", "moon", {
            cmd("Ceiling light", "turnOff"), cmd("Floor lamp", "turnOff"), cmd("Kitchen spots", "turnOff"),
            cmd("TV", "turnOff"), cmd("Path lights", "turnOff"), cmd("Bedroom blinds", "setPosition", {{"position", "0"}}),
            cmd("Front door", "lock"), cmd("Garden gate", "lock"),
            cmd("Bedroom heating", "setTargetTemp", {{"temperature", "18"}})}, false},
    };
    for (const SmartHomeScene& scene : scenes) api.CreateScene(scene);
}

void SeedAutomations(SmartHomeAPI& api, const std::vector<SmartHomeDeviceInfo>& devices) {
    // What a home would set up first: they run in the module's automation
    // engine and show in Activity when they do. Created once; after that the
    // store has them, with whatever the person changed.
    if (!api.GetAutomations().empty()) return;
    auto idOf = [&](const char* name) {
        for (const auto& d : devices) if (d.Name == name) return d.DeviceId;
        return std::string();
    };
    const std::string leak = idOf("Under-sink leak sensor");
    const std::string smoke = idOf("Kitchen smoke alarm");
    const std::string motion = idOf("Hallway motion");
    const std::string patio = idOf("Patio door");
    const std::string hall = idOf("Hall light");
    const std::string kettle = idOf("Kettle");
    const std::string rail = idOf("Towel rail");
    auto device = [](const std::string& id, const char* attribute, const char* value, int forMinutes = 0) {
        std::string config = std::string(R"({"type":"device","deviceId":")") + id + R"(","attribute":")" +
                             attribute + R"(","value":")" + value + "\"";
        if (forMinutes > 0) config += ",\"forMinutes\":" + std::to_string(forMinutes);
        return config + "}";
    };
    auto notify = [](const std::string& message) { return SmartHomeCommand{"", "notify", {{"message", message}}}; };

    api.CreateAutomation({"center.leak", "Water leak", true, "device", device(leak, "triggered", "true"),
                          {notify("Water under the kitchen sink - the kettle and towel rail are switched off"),
                           {kettle, "turnOff", {}}, {rail, "turnOff", {}}}, {}});
    api.CreateAutomation({"center.smoke", "Smoke alarm", true, "device", device(smoke, "triggered", "true"),
                          {notify("Smoke in the kitchen"), {hall, "turnOn", {}}}, {}});
    api.CreateAutomation({"center.hall", "Hall light on motion", true, "device", device(motion, "triggered", "true"),
                          {{hall, "turnOn", {}}, {"", "delay", {{"seconds", "120"}}}, {hall, "turnOff", {}}}, {}});
    api.CreateAutomation({"center.patio", "Patio door left open", true, "device", device(patio, "open", "true", 10),
                          {notify("The patio door has been open for 10 minutes")}, {}});
    api.CreateAutomation({"center.sunset", "Evening at sunset", true, "sunset", R"({"type":"sunset","offsetMinutes":-15})",
                          {{"", "activateScene", {{"sceneId", kSceneEvening}}}}, {}});
}

} // namespace

void SeedDemoHome() {
    auto& api = SmartHomeAPI::Instance();
    // Sunset needs a place, before the automation that waits for it: the
    // demo home stands in Berlin.
    double latitude = 0, longitude = 0;
    if (!api.GetHomeLocation(latitude, longitude)) api.SetHomeLocation(52.52, 13.405);
    const auto devices = api.GetDevices();
    SeedScenes(api, devices);
    SeedAutomations(api, devices);
}

} // namespace SmartHomeCenter
