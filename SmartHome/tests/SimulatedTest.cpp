// SmartHome/tests/SimulatedTest.cpp
// Drives the simulated home through the public facade, the way an
// application does, and checks that every kind of device answers: commands
// change state, state queries read it back, the clock moves the world on,
// sensors trigger, devices drop off, pairing adds one.
//
// The backend runs with realTime = false, so the test owns the clock
// (Advance) and nothing depends on how fast the machine is. Commands still
// go through the manager's worker thread, as in an application, so each one
// is awaited before its effect is checked.
//
// It runs under a comma-decimal locale when the machine has one: the
// thermostat's target travels as text, and must arrive as 23.5, not 23.
//
// Author: UltraCanvas Framework
#include "UltraCanvasSmartHome.h"
#include "SimulatedProtocol.h"

#include <atomic>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>

using namespace UltraCanvas::SmartHome;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    std::printf("%s  %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

// Commands are queued to the manager's worker thread; give each a moment.
bool WaitFor(const std::function<bool()>& predicate) {
    for (int i = 0; i < 300; ++i) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

std::string IdOf(const std::string& name) {
    for (const auto& device : SMARTHOME_API.GetDevices()) {
        if (device.Name == name) return device.DeviceId;
    }
    return "";
}

} // namespace

int main() {
    const bool commaLocale = std::setlocale(LC_ALL, "de_DE.UTF-8") != nullptr;
    std::printf("(locale: %s)\n", commaLocale ? "de_DE.UTF-8" : "C - no comma-decimal locale here");

    SimulatedHomeOptions options;
    options.realTime = false;
    options.randomEvents = false;
    options.pairingSeconds = 3.0;
    auto sim = std::make_shared<SimulatedProtocol>(options);

    // Updates arrive on whichever thread caused them. The callback reads the
    // API straight back, which is what an application does and what used to
    // deadlock when the manager dispatched with its device lock held.
    std::mutex seenMutex;
    std::set<std::string> updated;
    std::atomic<int> callbackReads{0};
    SMARTHOME_API.SetOnDeviceUpdate([&](const std::string& id) {
        if (!SMARTHOME_API.GetDevice(id).DeviceId.empty()) ++callbackReads;
        std::lock_guard<std::mutex> lock(seenMutex);
        updated.insert(id);
    });
    auto sawUpdate = [&](const std::string& id) {
        std::lock_guard<std::mutex> lock(seenMutex);
        return updated.count(id) > 0;
    };
    std::atomic<bool> paired{false};
    std::string pairedId;
    std::mutex pairedMutex;
    SMARTHOME_API.SetOnPairingComplete([&](bool success, const SmartHomeDeviceInfo& device) {
        std::lock_guard<std::mutex> lock(pairedMutex);
        pairedId = device.DeviceId;
        paired = success;
    });

    // ----- Start-up: the home is there as soon as the protocol is enabled.
    Check(SMARTHOME_API.Initialize(), "Initialize()");
    Check(SMARTHOME_API.RegisterProtocol(SmartHomeProtocolType::Simulated, sim), "RegisterProtocol(Simulated)");
    Check(SMARTHOME_API.EnableProtocol(SmartHomeProtocolType::Simulated), "EnableProtocol(Simulated)");
    const auto devices = SMARTHOME_API.GetDevices();
    Check(devices.size() == 30, "the demo home has 30 devices");
    Check(sim->GetRooms().size() == 8, "in 8 rooms");
    bool allPlaced = true, allSimulated = true, allOnline = true;
    std::set<SmartHomeDeviceCategory> categories;
    for (const auto& d : devices) {
        allPlaced = allPlaced && !d.Room.empty();
        allSimulated = allSimulated && d.Protocol == SmartHomeProtocolType::Simulated;
        allOnline = allOnline && d.State == SmartHomeDeviceState::Online && d.Reachable;
        categories.insert(d.Category);
    }
    Check(allPlaced, "every device has a room");
    Check(allSimulated, "every device reports the Simulated protocol, never a real one");
    Check(allOnline, "every device starts online");
    Check(categories.size() == 7, "seven categories: light, switch, plug, thermostat, lock, blind, sensor");
    Check(SMARTHOME_API.GetNetwork(SmartHomeProtocolType::Simulated).DeviceCount == 30,
          "the network reports the device count");

    // ----- Light
    const std::string spots = IdOf("Kitchen spots");
    Check(!spots.empty() && !SMARTHOME_API.GetLightState(spots).On, "Kitchen spots start off");
    SmartHomeLightState light;
    light.On = true;
    light.Brightness = 128;
    SMARTHOME_API.SetLightState(spots, light);
    Check(WaitFor([&] { auto s = SMARTHOME_API.GetLightState(spots); return s.On && s.Brightness == 128; }),
          "SetLightState turns them on at half brightness");
    Check(SMARTHOME_API.GetSwitchState(spots), "GetSwitchState answers for a light");
    Check(WaitFor([&] { return sawUpdate(spots); }), "the change was reported as a device update");
    Check(callbackReads > 0, "a callback could read the API back without deadlocking");

    // ----- Thermostat: the target arrives dot-decimal, the room then warms.
    const std::string office = IdOf("Office heating");
    SMARTHOME_API.SetThermostatTarget(office, 23.5f);
    Check(WaitFor([&] { return std::fabs(SMARTHOME_API.GetThermostatState(office).TargetTemperature - 23.5f) < 0.01f; }),
          "SetThermostatTarget(23.5) arrives as 23.5 in this locale");
    const float before = SMARTHOME_API.GetThermostatState(office).CurrentTemperature;
    sim->Advance(1.0);
    Check(SMARTHOME_API.GetThermostatState(office).Running, "the valve opens below the target");
    for (int i = 0; i < 120; ++i) sim->Advance(1.0);
    Check(SMARTHOME_API.GetThermostatState(office).CurrentTemperature > before + 0.5f,
          "two minutes of heating warm the room by more than half a degree");

    // ----- Blind: it travels, it does not jump.
    const std::string blinds = IdOf("Patio blinds");
    Check(SMARTHOME_API.GetBlindPosition(blinds) == 70, "Patio blinds start at 70 %");
    SMARTHOME_API.SetBlindPosition(blinds, 20);
    Check(WaitFor([&] {
              std::map<std::string, std::string> state;
              return sim->GetDeviceState(blinds, state) && state["target"] == "20";
          }),
          "the blind accepts 20 % as its target");
    sim->Advance(1.0);
    const int midway = SMARTHOME_API.GetBlindPosition(blinds);
    Check(midway < 70 && midway > 20, "after a second the blind is on its way");
    for (int i = 0; i < 10; ++i) sim->Advance(1.0);
    Check(SMARTHOME_API.GetBlindPosition(blinds) == 20, "and arrives at 20 %");

    // ----- Lock and battery
    const std::string door = IdOf("Front door");
    Check(SMARTHOME_API.GetLockState(door).Locked, "the front door starts locked");
    SMARTHOME_API.SetLockState(door, false);
    Check(WaitFor([&] { return !SMARTHOME_API.GetLockState(door).Locked; }), "SetLockState(false) unlocks it");
    Check(SMARTHOME_API.GetBatteryLevel(door) == 84, "its battery reads 84 %");
    Check(SMARTHOME_API.GetBatteryLevel(spots) == -1, "a mains device reports no battery");

    // ----- Plugs: power and energy
    const std::string kettle = IdOf("Kettle");
    Check(SMARTHOME_API.GetPowerUsage(kettle) == 0.0f, "the kettle draws nothing while off");
    const float kettleEnergy = SMARTHOME_API.GetEnergyUsage(kettle);
    SMARTHOME_API.SetSwitchState(kettle, true);
    Check(WaitFor([&] { return SMARTHOME_API.GetPowerUsage(kettle) > 2000.0f; }), "switched on, it draws 2.2 kW");
    for (int i = 0; i < 70; ++i) sim->Advance(1.0);
    Check(SMARTHOME_API.GetPowerUsage(kettle) < 50.0f, "after a minute it only keeps warm");
    Check(SMARTHOME_API.GetEnergyUsage(kettle) > kettleEnergy + 0.03f, "and its meter counted the heating");
    Check(SMARTHOME_API.GetPowerUsage(IdOf("Path lights")) == 0.0f, "an unmetered switch reports no power");

    // ----- Sensors: an event staged through the facade alone
    const std::string leak = IdOf("Under-sink leak sensor");
    Check(!SMARTHOME_API.GetSensorReading(leak).Triggered, "the leak sensor starts dry");
    SMARTHOME_API.SendCommand({leak, "simulateTrigger", {}});
    Check(WaitFor([&] { return SMARTHOME_API.GetSensorReading(leak).Triggered; }), "simulateTrigger raises a leak");
    for (int i = 0; i < 60; ++i) sim->Advance(1.0);
    Check(SMARTHOME_API.GetSensorReading(leak).Triggered, "a leak stays raised until cleared");
    const std::string motion = IdOf("Hallway motion");
    SMARTHOME_API.SendCommand({motion, "simulateTrigger", {}});
    Check(WaitFor([&] { return SMARTHOME_API.GetSensorReading(motion).Triggered; }), "motion detected");
    for (int i = 0; i < 25; ++i) sim->Advance(1.0);
    Check(!SMARTHOME_API.GetSensorReading(motion).Triggered, "motion clears itself after 20 s");
    const auto outdoor = SMARTHOME_API.GetSensorReading(IdOf("Outdoor temperature"));
    Check(outdoor.Type == SmartHomeSensorType::Temperature && outdoor.Unit == "°C" &&
          outdoor.Value > 5.0f && outdoor.Value < 14.0f, "the outdoor temperature reads about 9 °C");

    // ----- Offline: the registry follows, and commands fail.
    const std::string lamp = IdOf("Bedside lamp");
    SMARTHOME_API.SendCommand({lamp, "simulateOffline", {}});
    Check(WaitFor([&] { return SMARTHOME_API.GetDevice(lamp).State == SmartHomeDeviceState::Offline; }),
          "simulateOffline takes the lamp offline in the registry too");
    SMARTHOME_API.SetSwitchState(lamp, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Check(!SMARTHOME_API.GetLightState(lamp).On, "an offline lamp ignores commands");
    SMARTHOME_API.SendCommand({lamp, "simulateOnline", {}});
    Check(WaitFor([&] { return SMARTHOME_API.GetDevice(lamp).Reachable; }), "simulateOnline brings it back");

    // ----- Rename survives the next update from the device.
    SMARTHOME_API.RenameDevice(spots, "Worktop spots");
    SMARTHOME_API.SetSwitchState(spots, false);
    Check(WaitFor([&] { return !SMARTHOME_API.GetLightState(spots).On; }), "the spots go off again");
    Check(SMARTHOME_API.GetDevice(spots).Name == "Worktop spots", "the rename survives a refresh from the device");

    // ----- Scenes run through the same queue.
    SmartHomeScene scene;
    scene.SceneId = "away";
    scene.Name = "Away";
    scene.Actions = {{IdOf("Ceiling light"), "turnOff", {}}, {IdOf("Floor lamp"), "turnOff", {}},
                     {door, "lock", {}}};
    SMARTHOME_API.CreateScene(scene);
    SMARTHOME_API.ActivateScene("away");
    Check(WaitFor([&] {
              return !SMARTHOME_API.GetLightState(IdOf("Ceiling light")).On &&
                     !SMARTHOME_API.GetLightState(IdOf("Floor lamp")).On &&
                     SMARTHOME_API.GetLockState(door).Locked;
          }),
          "the Away scene turns the living room off and locks the door");

    // ----- Pairing finds a device after a few seconds.
    Check(SMARTHOME_API.StartPairing(SmartHomeProtocolType::Simulated, 60), "StartPairing(Simulated)");
    sim->Advance(1.0);
    Check(!paired && SMARTHOME_API.GetDevices().size() == 30, "nothing joins in the first second");
    for (int i = 0; i < 3; ++i) sim->Advance(1.0);
    Check(paired, "after three seconds pairing completes");
    std::string newId;
    {
        std::lock_guard<std::mutex> lock(pairedMutex);
        newId = pairedId;
    }
    Check(SMARTHOME_API.GetDevices().size() == 31, "the home now has 31 devices");
    const auto added = SMARTHOME_API.GetDevice(newId);
    Check(added.Name == "Colour bulb" && added.Room.empty(), "the new colour bulb waits for a room");
    Check(SMARTHOME_API.GetLightState(newId).On, "and is controllable straight away");

    // ----- Disabling the protocol takes its devices with it.
    Check(SMARTHOME_API.DisableProtocol(SmartHomeProtocolType::Simulated), "DisableProtocol(Simulated)");
    Check(SMARTHOME_API.GetDevices().empty(), "its devices are gone from the registry");

    SMARTHOME_API.SetOnDeviceUpdate(nullptr);
    SMARTHOME_API.SetOnPairingComplete(nullptr);
    SMARTHOME_API.Shutdown();

    if (failures) {
        std::printf("\nFAIL - %d check(s) failed\n", failures);
        return 1;
    }
    std::puts("\nPASS - the simulated home answers through the facade");
    return 0;
}
