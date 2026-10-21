// SmartHome/tests/AutomationTest.cpp
// The store (smarthome.db) and the automation engine, through the facade,
// on the simulated home and a clock the test sets.
//
// Store: scenes, automations, a device's name and room and the home's
// location survive closing the module and opening the store again; the
// event log and sensor history are recorded; ExportStore writes a copy.
// Engine: a device trigger fires once per match and not for the state the
// home was in; forMinutes waits; a time trigger fires once in its minute;
// conditions gate it; a delay holds the following actions; an automation
// that keeps triggering itself is switched off; sunrise is where it should
// be.
//
// The automation thread keeps running, as in an application; the test's
// clock is the one it reads, so the time-based checks are deterministic.
//
// Author: UltraCanvas Framework
#include "UltraCanvasSmartHome.h"
#include "SimulatedProtocol.h"
#include "UltraCanvasSmartHomeManager.h"
#include "SmartHomeAutomationEngine.h"
#include "UltraCanvasPathUtf8.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <algorithm>
#include <optional>
#include <vector>
#include <unistd.h>

using namespace UltraCanvas::SmartHome;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    std::printf("%s  %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

bool WaitFor(const std::function<bool()>& predicate) {
    for (int i = 0; i < 300; ++i) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

void Settle() {
    // Let queued commands and the automation thread catch up.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
}

std::string IdOf(const std::string& name) {
    for (const auto& device : SMARTHOME_API.GetDevices()) {
        if (device.Name == name) return device.DeviceId;
    }
    return "";
}

// "HH:MM" of an epoch second, in local time, as a trigger names it.
std::string ClockOf(int64_t seconds) {
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm local{};
    localtime_r(&t, &local);
    char text[8];
    std::snprintf(text, sizeof text, "%02d:%02d", local.tm_hour, local.tm_min);
    return text;
}

std::atomic<int64_t> fakeNow{0};

std::shared_ptr<SimulatedProtocol> StartHome() {
    SimulatedHomeOptions options;
    options.realTime = false;
    options.randomEvents = false;
    auto sim = std::make_shared<SimulatedProtocol>(options);
    SMARTHOME_API.Initialize();
    SmartHomeManager::Instance().SetTimeSource([] { return fakeNow.load(); });
    SMARTHOME_API.RegisterProtocol(SmartHomeProtocolType::Simulated, sim);
    SMARTHOME_API.EnableProtocol(SmartHomeProtocolType::Simulated);
    return sim;
}

SmartHomeAutomation Automation(const std::string& id, const std::string& config,
                               std::vector<SmartHomeCommand> actions, std::vector<std::string> conditions = {}) {
    SmartHomeAutomation a;
    a.AutomationId = id;
    a.Name = id;
    a.Enabled = true;
    a.TriggerConfig = config;
    a.Actions = std::move(actions);
    a.Conditions = std::move(conditions);
    return a;
}

bool HasEvent(const std::string& fragment) {
    for (const auto& e : SMARTHOME_API.GetEventLog(0, 500)) {
        if (e.Text.find(fragment) != std::string::npos) return true;
    }
    return false;
}

} // namespace

int main() {
    // A fixed afternoon, so nothing depends on when the test runs.
    const int64_t start = 1782043200;   // 2026-06-21 12:00:00 UTC
    fakeNow = start;
    const fs::path dir = fs::temp_directory_path() / ("uc_smarthome_store_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string dbPath = UltraCanvas::PathToUtf8(dir / "smarthome.db");

    // ===== THE STORE =====
    // Today's date here: the store prunes history older than 90 days by the
    // real clock when it opens.
    std::puts("-- store");
    fakeNow = static_cast<int64_t>(std::time(nullptr));
    auto sim = StartHome();
#ifdef ULTRACANVAS_SMARTHOME_HAS_STORE
    Check(SMARTHOME_API.OpenStore(dbPath), "OpenStore(smarthome.db)");
    Check(SMARTHOME_API.IsStoreOpen(), "IsStoreOpen()");

    const std::string spots = IdOf("Kitchen spots");
    const std::string thermostat = IdOf("Office heating");
    SMARTHOME_API.RenameDevice(spots, "Worktop spots");
    SMARTHOME_API.SetDeviceRoom(spots, "Pantry");
    Check(SMARTHOME_API.GetDevice(spots).Room == "Pantry", "SetDeviceRoom shows at once");
    SMARTHOME_API.SetHomeLocation(52.52, 13.405);
    SMARTHOME_API.CreateScene({"movie", "Movie night", "film", {{IdOf("Ceiling light"), "turnOff", {}}}, false});
    SMARTHOME_API.CreateAutomation(Automation("kept", R"({"type":"time","time":"23:15"})",
                                              {{IdOf("Hall light"), "turnOff", {}}}, {"on Mon,Tue"}));
    SMARTHOME_API.ActivateScene("movie");
    Settle();

    // Sensor history: the thermostat warms the office over a few minutes.
    for (int i = 0; i < 300; ++i) {
        fakeNow += 1;
        sim->Advance(1.0);
    }
    const auto history = SMARTHOME_API.GetSensorHistory(thermostat, 0, UINT64_MAX);
    Check(history.size() >= 3, "a warming room records several temperature readings");
    Check(!history.empty() && history.front().Type == SmartHomeSensorType::Temperature &&
          history.back().Value > history.front().Value,
          "oldest first, and rising");

    const std::string copy = UltraCanvas::PathToUtf8(dir / "copy.db");
    Check(SMARTHOME_API.ExportStore(copy) && fs::exists(UltraCanvas::PathFromUtf8(copy)), "ExportStore writes a copy");

    // Close everything; a new start opens the same file.
    SMARTHOME_API.Shutdown();
    Check(SMARTHOME_API.GetScenes().empty(), "after Shutdown nothing is in memory");
    sim = StartHome();
    Check(SMARTHOME_API.OpenStore(dbPath), "the store opens again");
    Check(SMARTHOME_API.GetScene("movie").Name == "Movie night" &&
          SMARTHOME_API.GetScene("movie").Actions.size() == 1, "the scene came back with its action");
    const auto kept = SMARTHOME_API.GetAutomation("kept");
    Check(kept.TriggerConfig.find("23:15") != std::string::npos && kept.Conditions.size() == 1 &&
          kept.Conditions[0] == "on Mon,Tue" && kept.Actions.size() == 1, "the automation came back whole");
    const auto renamed = SMARTHOME_API.GetDevice(spots);
    Check(renamed.Name == "Worktop spots" && renamed.Room == "Pantry",
          "the device's name and room came back, over the backend's");
    double lat = 0, lon = 0;
    Check(SMARTHOME_API.GetHomeLocation(lat, lon) && std::fabs(lat - 52.52) < 1e-6 && std::fabs(lon - 13.405) < 1e-6,
          "the home's location came back");
    Check(HasEvent("Activated \"Movie night\""), "the event log kept the scene's activation");
    const size_t reopenedHistory = SMARTHOME_API.GetSensorHistory(thermostat, 0, UINT64_MAX).size();
    if (reopenedHistory != history.size())
        std::printf("      (history: %zu before, %zu after)\n", history.size(), reopenedHistory);
    Check(reopenedHistory == history.size(), "and the history its readings");

    SMARTHOME_API.SetDeviceRoom(spots, "");
    Check(SMARTHOME_API.GetDevice(spots).Room == "Kitchen", "an empty room gives the backend's back");
    SMARTHOME_API.DeleteAutomation("kept");
    SMARTHOME_API.DeleteScene("movie");
#else
    Check(!SMARTHOME_API.OpenStore(dbPath) && !SMARTHOME_API.GetStoreError().empty(),
          "built without UltraDatabase: OpenStore refuses and says why");
#endif

    // ===== THE ENGINE =====
    std::puts("-- automations");
    fakeNow = start;
    auto& manager = SmartHomeManager::Instance();
    std::mutex notesMutex;
    std::vector<std::string> notes;
    std::atomic<int> runs{0};
    SMARTHOME_API.SetOnAutomationNotify([&](const std::string&, const std::string& message) {
        std::lock_guard<std::mutex> lock(notesMutex);
        notes.push_back(message);
    });
    SMARTHOME_API.SetOnAutomationTrigger([&](const std::string&) { ++runs; });
    auto noted = [&](const std::string& message) {
        std::lock_guard<std::mutex> lock(notesMutex);
        for (const auto& n : notes) if (n == message) return true;
        return false;
    };

    // A device trigger, once per match.
    const std::string leak = IdOf("Under-sink leak sensor");
    const std::string kettle = IdOf("Kettle");
    SMARTHOME_API.SetSwitchState(kettle, true);
    WaitFor([&] { return SMARTHOME_API.GetSwitchState(kettle); });
    SMARTHOME_API.CreateAutomation(Automation("leak",
        R"({"type":"device","deviceId":")" + leak + R"(","attribute":"triggered","value":"true"})",
        {{"", "notify", {{"message", "Water under the sink"}}}, {kettle, "turnOff", {}}}));
    manager.EvaluateAutomations();
    Settle();
    Check(!noted("Water under the sink"), "a dry sensor does not fire");
    SMARTHOME_API.SendCommand({leak, "simulateTrigger", {}});
    Check(WaitFor([&] { return noted("Water under the sink"); }), "the leak fires: notify");
    Check(WaitFor([&] { return !SMARTHOME_API.GetSwitchState(kettle); }), "and the kettle is switched off");
    const int afterLeak = runs;
    for (int i = 0; i < 3; ++i) { fakeNow += 5; manager.EvaluateAutomations(); }
    Settle();
    Check(runs == afterLeak, "a leak that stays does not fire again");
    SMARTHOME_API.SendCommand({leak, "simulateClear", {}});
    WaitFor([&] { return !SMARTHOME_API.GetSensorReading(leak).Triggered; });
    manager.EvaluateAutomations();
    SMARTHOME_API.SendCommand({leak, "simulateTrigger", {}});
    Check(WaitFor([&] { return runs == afterLeak + 1; }), "cleared and wet again: it fires again");
    Check(HasEvent("Ran \"leak\" (triggered = true)") && HasEvent("Notice: Water under the sink"),
          "the event log says what ran and why");

    // The state the home is in is not an event.
    SMARTHOME_API.CreateAutomation(Automation("fridge", R"({"type":"device","deviceId":")" + IdOf("Fridge") +
                                                            R"(","attribute":"on","value":"on"})",
                                              {{"", "notify", {{"message", "fridge"}}}}));
    for (int i = 0; i < 3; ++i) { fakeNow += 1; manager.EvaluateAutomations(); }
    Settle();
    Check(!noted("fridge"), "a fridge that is already on does not fire");

    // forMinutes.
    const std::string door = IdOf("Patio door");
    SMARTHOME_API.CreateAutomation(Automation("door",
        R"({"type":"device","deviceId":")" + door + R"(","attribute":"open","value":"true","forMinutes":10})",
        {{"", "notify", {{"message", "Patio door still open"}}}}));
    manager.EvaluateAutomations();
    SMARTHOME_API.SendCommand({door, "simulateTrigger", {}});
    WaitFor([&] { return SMARTHOME_API.GetSensorReading(door).Triggered; });
    manager.EvaluateAutomations();
    fakeNow += 5 * 60;
    manager.EvaluateAutomations();
    Settle();
    Check(!noted("Patio door still open"), "open for five minutes: not yet");
    fakeNow += 6 * 60;
    manager.EvaluateAutomations();
    Check(WaitFor([&] { return noted("Patio door still open"); }), "open for eleven: it fires");

    // A time trigger, its condition, and a delay.
    const int64_t at = (fakeNow.load() / 60 + 2) * 60;   // the start of a minute two minutes on
    const std::string hall = IdOf("Hall light");
    SMARTHOME_API.CreateAutomation(Automation("timed", R"({"type":"time","time":")" + ClockOf(at) + R"("})",
        {{hall, "turnOn", {}}, {"", "delay", {{"seconds", "60"}}}, {hall, "turnOff", {}}}));
    SMARTHOME_API.CreateAutomation(Automation("gated", R"({"type":"time","time":")" + ClockOf(at) + R"("})",
        {{"", "notify", {{"message", "gated ran"}}}},
        {"time between " + ClockOf(at + 3600) + " and " + ClockOf(at + 7200)}));
    SMARTHOME_API.CreateAutomation(Automation("unlocked", R"({"type":"time","time":")" + ClockOf(at) + R"("})",
        {{"", "notify", {{"message", "front door is locked"}}}},
        {IdOf("Front door") + " locked = true"}));
    manager.EvaluateAutomations();
    Settle();
    Check(!SMARTHOME_API.GetSwitchState(hall), "before its minute, nothing");
    fakeNow = at + 10;
    manager.EvaluateAutomations();
    Check(WaitFor([&] { return SMARTHOME_API.GetSwitchState(hall); }),
          "in its minute the time trigger turns the hall light on");
    Check(WaitFor([&] { return noted("front door is locked"); }), "a device condition that holds lets it run");
    Check(!noted("gated ran"), "a time-range condition that does not hold stops it");
    fakeNow = at + 40;
    manager.EvaluateAutomations();
    Settle();
    Check(SMARTHOME_API.GetSwitchState(hall), "the delay holds the next action back");
    fakeNow = at + 75;
    manager.EvaluateAutomations();
    Check(WaitFor([&] { return !SMARTHOME_API.GetSwitchState(hall); }), "and after 60 s it runs");
    const int timedRuns = runs;
    fakeNow = at + 80;
    manager.EvaluateAutomations();
    Settle();
    Check(runs == timedRuns, "a time trigger runs once in its minute");

    // A loop: one automation turns a light off when it is on, the other on
    // when it is off. Tested on the engine itself, with no thread to race:
    // a command lands after the evaluation that sent it, as a real one
    // does (queued, then reported back), and the next evaluation sees it.
    {
        bool lightOn = true;
        std::optional<bool> pending;
        std::vector<std::string> disabled;
        std::vector<SmartHomeEvent> log;
        SmartHomeAutomationEngine::Host host;
        host.readAttribute = [&](const std::string&, const std::string&) {
            return std::optional<std::string>(lightOn ? "true" : "false");
        };
        host.sendCommand = [&](const SmartHomeCommand& c) { pending = c.Command == "turnOn"; };
        host.disable = [&](const std::string& id) { disabled.push_back(id); };
        host.log = [&](const SmartHomeEvent& e) { log.push_back(e); };
        SmartHomeAutomationEngine engine(host);
        std::vector<SmartHomeAutomation> pair = {
            Automation("loop-off", R"({"type":"device","deviceId":"lamp","attribute":"on","value":"true"})",
                       {{"lamp", "turnOff", {}}}),
            Automation("loop-on", R"({"type":"device","deviceId":"lamp","attribute":"on","value":"false"})",
                       {{"lamp", "turnOn", {}}})};
        engine.Evaluate(pair, start);       // primes: the light is on, which is not news
        lightOn = false;                    // someone switches it off; the loop begins
        for (int i = 0; i < 12 && disabled.empty(); ++i) {
            engine.Evaluate(pair, start + 1);
            if (pending) { lightOn = *pending; pending.reset(); }
            for (auto& a : pair) {
                if (std::find(disabled.begin(), disabled.end(), a.AutomationId) != disabled.end()) a.Enabled = false;
            }
        }
        Check(!disabled.empty(), "an automation that keeps triggering itself is switched off");
        bool explained = false;
        for (const auto& e : log) explained = explained || e.Text.find("which looks like a loop") != std::string::npos;
        Check(explained, "and the log says why");
    }

    // By hand, and an unreadable trigger.
    SMARTHOME_API.CreateAutomation(Automation("manual", R"({"type":"manual"})", {{"", "notify", {{"message", "by hand"}}}}));
    Check(SMARTHOME_API.RunAutomation("manual") && WaitFor([&] { return noted("by hand"); }), "RunAutomation runs it now");
    SMARTHOME_API.CreateAutomation(Automation("broken", R"({"type":"time","time":"25:99"})", {}));
    manager.EvaluateAutomations();
    Check(HasEvent("\"broken\" has a trigger the engine cannot read"), "an unreadable trigger is logged, not run");

    // ===== THE SUN =====
    std::puts("-- sun");
    int64_t rise = 0, set = 0;
    Check(SmartHomeAutomationEngine::ComputeSunTimes(52.52, 13.405, start, rise, set),
          "Berlin, 21 June 2026: the sun rises and sets");
    Check(std::llabs(rise - 1782009780) < 6 * 60, "sunrise about 02:43 UTC (04:43 CEST)");
    Check(std::llabs(set - 1782070380) < 6 * 60, "sunset about 19:33 UTC (21:33 CEST)");
    Check(!SmartHomeAutomationEngine::ComputeSunTimes(78.22, 15.65, start, rise, set),
          "Svalbard in June: no sunset, so no times");
    SMARTHOME_API.SetHomeLocation(52.52, 13.405);
    int64_t apiRise = 0, apiSet = 0;
    fakeNow = start;
    SMARTHOME_API.GetSunTimes(apiRise, apiSet);
    SMARTHOME_API.CreateAutomation(Automation("dusk", R"({"type":"sunset","offsetMinutes":-30})",
                                              {{"", "notify", {{"message", "dusk"}}}}));
    fakeNow = apiSet - 30 * 60 - 20;
    manager.EvaluateAutomations();
    Settle();
    Check(!noted("dusk"), "before sunset - 30 min, nothing");
    fakeNow = apiSet - 30 * 60 + 5;
    manager.EvaluateAutomations();
    Check(WaitFor([&] { return noted("dusk"); }), "half an hour before sunset it runs");

    // ===== HELPERS =====
    int minutes = 0;
    Check(SmartHomeAutomationEngine::ParseClock("07:30", minutes) && minutes == 450 &&
          !SmartHomeAutomationEngine::ParseClock("7:3", minutes) && !SmartHomeAutomationEngine::ParseClock("24:00", minutes),
          "clock times parse as HH:MM only");
    Check(SmartHomeAutomationEngine::Compare("21.5", ">", "21") && SmartHomeAutomationEngine::Compare("on", "=", "true") &&
          SmartHomeAutomationEngine::Compare("Heat", "=", "heat") && !SmartHomeAutomationEngine::Compare("21.5", "<", "21"),
          "numbers, booleans and words compare as they should");

    SMARTHOME_API.SetOnAutomationNotify(nullptr);
    SMARTHOME_API.SetOnAutomationTrigger(nullptr);
    SMARTHOME_API.Shutdown();
    manager.SetTimeSource(nullptr);
    fs::remove_all(dir);

    if (failures) {
        std::printf("\nFAIL - %d check(s) failed\n", failures);
        return 1;
    }
    std::puts("\nPASS - the store keeps the home, and automations run when they should");
    return 0;
}
