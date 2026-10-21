// core/SmartHomeAutomationEngine.h
// Decides when an automation runs and runs it. The forms it reads - the
// trigger JSON, the condition strings, the special actions - are documented
// on SmartHomeAutomation in UltraCanvasSmartHome.h; they are what
// SmartHomeAutomationEditor writes.
//
// The engine owns no devices and no threads. The manager calls Evaluate()
// about once a second, and at once when a device changes, with the
// automations and the time; everything the engine needs from the home - an
// attribute's value, sending a command, the log - goes through the Host
// hooks, so tests drive it with a clock of their own.
//
// Rules it keeps:
// - A device trigger fires when its attribute comes to match, once per
//   match; the state a device is in when the engine first sees it is not an
//   event. With forMinutes, only once it has matched that long.
// - A time or sun trigger fires once a day, in the 90 seconds after its
//   moment: a home that starts at noon does not run its 07:00 automation.
// - More than three runs in ten seconds switches the automation off (it is
//   triggering itself), through Host::disable, with a log line saying why.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasSmartHome.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace SmartHome {

class SmartHomeAutomationEngine {
public:
    struct Host {
        // An attribute's current value as text, or nullopt when the device
        // or the attribute does not exist.
        std::function<std::optional<std::string>(const std::string& deviceId,
                                                 const std::string& attribute)> readAttribute;
        std::function<void(const SmartHomeCommand& command)> sendCommand;
        std::function<void(const std::string& sceneId)> activateScene;
        std::function<void(const std::string& automationId, const std::string& message)> notify;
        std::function<void(const std::string& automationId)> disable;
        std::function<void(const std::string& automationId)> ran;
        std::function<void(const SmartHomeEvent& event)> log;
        std::function<bool(double& latitude, double& longitude)> location;
    };

    explicit SmartHomeAutomationEngine(Host host);

    // Fires what is due at `now` (seconds since the epoch), and the delayed
    // actions whose time has come.
    void Evaluate(const std::vector<SmartHomeAutomation>& automations, int64_t now);

    // Runs one automation's actions now; `reason` goes in the log line.
    // False when the loop guard stopped it.
    bool Run(const SmartHomeAutomation& automation, int64_t now, const std::string& reason);

    // Forgets what the engine remembered about an automation (an edited or
    // deleted one starts afresh).
    void Forget(const std::string& automationId);

    // Whether every condition holds at `now`; `why` gets the first that
    // does not.
    bool ConditionsHold(const SmartHomeAutomation& automation, int64_t now, std::string* why = nullptr);

    // ===== HELPERS (public for tests) =====
    // "07:30" -> 450.
    static bool ParseClock(const std::string& text, int& minutesOfDay);
    // Numbers compare as numbers, true/on/yes and false/off/no as booleans,
    // anything else as text without case (only = and != then).
    static bool Compare(const std::string& actual, const std::string& op, const std::string& expected);
    // Sunrise and sunset on the local day containing `now`, by the NOAA
    // approximation (a minute or two of error), with the standard -0.833
    // degree horizon. False in a polar day or night.
    static bool ComputeSunTimes(double latitude, double longitude, int64_t now,
                                int64_t& sunrise, int64_t& sunset);

private:
    struct Trigger {
        std::string type;          // device, time, sunrise, sunset, manual, location
        std::string deviceId, attribute, op = "=", value;
        int forMinutes = 0;
        int minuteOfDay = -1;      // time
        int offsetMinutes = 0;     // sunrise / sunset
        std::vector<int> days;     // 0 = Sunday; empty = every day
        bool valid = false;
    };
    struct State {
        std::string configSeen;    // the TriggerConfig the state belongs to
        Trigger trigger;
        bool primed = false;       // has seen the device once
        bool matched = false;
        int64_t matchedSince = 0;
        bool firedThisMatch = false;
        int lastFiredDay = -1;     // local yyyymmdd of the last time/sun run
        std::deque<int64_t> recentRuns;
        bool reportedProblem = false;
    };
    struct Delayed {
        int64_t due;
        std::string automationId;
        SmartHomeCommand command;
    };

    Trigger ParseTrigger(const SmartHomeAutomation& automation) const;
    bool ConditionHolds(const std::string& condition, int64_t now, bool& understood) const;
    void Execute(const std::string& automationId, const std::vector<SmartHomeCommand>& actions,
                 size_t from, int64_t now);
    void ExecuteOne(const std::string& automationId, const SmartHomeCommand& action);
    void Log(const std::string& automationId, const std::string& text, int64_t now,
             const std::string& kind = "automation");

    Host host;
    std::recursive_mutex mutex;          // Run may be called from inside Evaluate
    std::map<std::string, State> states;
    std::vector<Delayed> delayed;
};

} // namespace SmartHome
} // namespace UltraCanvas
