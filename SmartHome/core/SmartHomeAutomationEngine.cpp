// core/SmartHomeAutomationEngine.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SmartHomeAutomationEngine.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasTextUtils.h"   // TryParseFloat - dot-decimal, whatever the locale

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <locale>
#include <sstream>

namespace UltraCanvas {
namespace SmartHome {

namespace {

constexpr int64_t kFireWindowSeconds = 90;
constexpr int64_t kLoopWindowSeconds = 10;
constexpr size_t kLoopRuns = 3;   // a fourth run inside the window stops it

std::tm LocalTime(int64_t seconds) {
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    return local;
}

int DayKey(const std::tm& local) {
    return (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
}

// The epoch second of `minuteOfDay` on the local day of `now`. mktime
// works out the zone and daylight saving for that day.
int64_t AtMinuteOfDay(int64_t now, int minuteOfDay) {
    std::tm local = LocalTime(now);
    local.tm_hour = minuteOfDay / 60;
    local.tm_min = minuteOfDay % 60;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    return static_cast<int64_t>(std::mktime(&local));
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// true / false for the words people and widgets use, nothing otherwise.
std::optional<bool> AsBoolean(const std::string& text) {
    const std::string t = Lower(Trim(text));
    if (t == "true" || t == "on" || t == "yes" || t == "1" || t == "locked" || t == "open") return true;
    if (t == "false" || t == "off" || t == "no" || t == "0" || t == "unlocked" || t == "closed") return false;
    return std::nullopt;
}

int DayIndex(const std::string& name) {
    static const char* kDays[] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
    const std::string n = Lower(Trim(name)).substr(0, 3);
    for (int i = 0; i < 7; ++i) {
        if (n == kDays[i]) return i;
    }
    return -1;
}

std::string ClockText(int minuteOfDay) {
    char text[8];
    std::snprintf(text, sizeof text, "%02d:%02d", minuteOfDay / 60, minuteOfDay % 60);
    return text;
}

} // namespace

SmartHomeAutomationEngine::SmartHomeAutomationEngine(Host h) : host(std::move(h)) {}

// ===== HELPERS =====

bool SmartHomeAutomationEngine::ParseClock(const std::string& text, int& minutesOfDay) {
    const std::string t = Trim(text);
    const size_t colon = t.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= t.size()) return false;
    int hours = 0, minutes = 0;
    for (size_t i = 0; i < colon; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(t[i]))) return false;
        hours = hours * 10 + (t[i] - '0');
    }
    for (size_t i = colon + 1; i < t.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(t[i]))) return false;
        minutes = minutes * 10 + (t[i] - '0');
    }
    if (hours > 23 || minutes > 59 || t.size() - colon - 1 != 2) return false;
    minutesOfDay = hours * 60 + minutes;
    return true;
}

bool SmartHomeAutomationEngine::Compare(const std::string& actual, const std::string& op,
                                        const std::string& expected) {
    float a = 0, e = 0;
    if (TryParseFloat(Trim(actual), a) && TryParseFloat(Trim(expected), e)) {
        if (op == "=" || op == "==") return std::fabs(a - e) < 1e-6f;
        if (op == "!=") return std::fabs(a - e) >= 1e-6f;
        if (op == "<") return a < e;
        if (op == ">") return a > e;
        if (op == "<=") return a <= e;
        if (op == ">=") return a >= e;
        return false;
    }
    const auto ab = AsBoolean(actual), eb = AsBoolean(expected);
    const bool equal = (ab && eb) ? *ab == *eb : Lower(Trim(actual)) == Lower(Trim(expected));
    if (op == "=" || op == "==") return equal;
    if (op == "!=") return !equal;
    return false;
}

bool SmartHomeAutomationEngine::ComputeSunTimes(double latitude, double longitude, int64_t now,
                                                int64_t& sunrise, int64_t& sunset) {
    // The sunrise equation (NOAA's approximation), evaluated for the solar
    // days around `now`; the events kept are those on now's local day.
    constexpr double kPi = 3.14159265358979323846;
    const auto rad = [&](double d) { return d * kPi / 180.0; };
    const auto deg = [&](double r) { return r * 180.0 / kPi; };
    const int targetDay = DayKey(LocalTime(now));
    const double julianNow = static_cast<double>(now) / 86400.0 + 2440587.5;
    const double cycleNow = std::round(julianNow - 2451545.0 - 0.0009 + longitude / 360.0);

    bool haveRise = false, haveSet = false;
    for (int k = -1; k <= 1; ++k) {
        const double n = cycleNow + k;
        const double meanSolarNoon = n + 0.0009 - longitude / 360.0;
        const double m = std::fmod(357.5291 + 0.98560028 * meanSolarNoon, 360.0);
        const double c = 1.9148 * std::sin(rad(m)) + 0.02 * std::sin(rad(2 * m)) + 0.0003 * std::sin(rad(3 * m));
        const double lambda = std::fmod(m + c + 180.0 + 102.9372, 360.0);
        const double transit = 2451545.0 + meanSolarNoon + 0.0053 * std::sin(rad(m)) - 0.0069 * std::sin(rad(2 * lambda));
        const double sinDecl = std::sin(rad(lambda)) * std::sin(rad(23.4397));
        const double cosDecl = std::cos(std::asin(sinDecl));
        const double cosHour = (std::sin(rad(-0.833)) - std::sin(rad(latitude)) * sinDecl) /
                               (std::cos(rad(latitude)) * cosDecl);
        if (cosHour < -1.0 || cosHour > 1.0) continue;   // the sun neither rises nor sets
        const double hourAngle = deg(std::acos(cosHour));
        const auto toUnix = [](double julian) { return static_cast<int64_t>(std::llround((julian - 2440587.5) * 86400.0)); };
        const int64_t rise = toUnix(transit - hourAngle / 360.0);
        const int64_t set = toUnix(transit + hourAngle / 360.0);
        if (!haveRise && DayKey(LocalTime(rise)) == targetDay) { sunrise = rise; haveRise = true; }
        if (!haveSet && DayKey(LocalTime(set)) == targetDay) { sunset = set; haveSet = true; }
    }
    return haveRise && haveSet;
}

// ===== PARSING =====

SmartHomeAutomationEngine::Trigger SmartHomeAutomationEngine::ParseTrigger(const SmartHomeAutomation& a) const {
    Trigger t;
    JSONParseResult status;
    const JSONValue o = a.TriggerConfig.empty() ? JSONValue::MakeObject() : JSON::Parse(a.TriggerConfig, &status);
    if (!a.TriggerConfig.empty() && !status.success) return t;   // invalid

    t.type = Lower(o.Get("type").GetString(a.TriggerType));
    if (t.type.empty()) t.type = "manual";
    if (t.type == "device") {
        t.deviceId = o.Get("deviceId").GetString();
        t.attribute = o.Get("attribute").GetString();
        t.op = o.Get("operator").GetString("=");
        // The editor writes the value as text; a hand-written config may use
        // a number or a boolean.
        const JSONValue& v = o.Get("value");
        if (v.IsBoolean()) t.value = v.GetBoolean() ? "true" : "false";
        else if (v.IsNumber()) {
            std::ostringstream text;
            text.imbue(std::locale::classic());
            text << v.GetNumber();
            t.value = text.str();
        } else {
            t.value = v.GetString();
        }
        t.forMinutes = static_cast<int>(std::max<int64_t>(0, o.Get("forMinutes").GetInteger(0)));
        t.valid = !t.deviceId.empty() && !t.attribute.empty();
    } else if (t.type == "time") {
        t.valid = ParseClock(o.Get("time").GetString(), t.minuteOfDay);
    } else if (t.type == "sunrise" || t.type == "sunset") {
        t.offsetMinutes = static_cast<int>(o.Get("offsetMinutes").GetInteger(0));
        t.valid = true;
    } else {
        t.valid = t.type == "manual" || t.type == "location";
    }
    for (const auto& day : o.Get("days").GetElements()) {
        const int d = static_cast<int>(day.GetInteger(-1));
        if (d >= 0 && d <= 6) t.days.push_back(d);
    }
    return t;
}

bool SmartHomeAutomationEngine::ConditionHolds(const std::string& condition, int64_t now, bool& understood) const {
    understood = true;
    const std::string c = Trim(condition);
    const std::string lower = Lower(c);
    const std::tm local = LocalTime(now);

    if (lower.rfind("time between ", 0) == 0) {
        const std::string rest = c.substr(13);
        const size_t and_ = Lower(rest).find(" and ");
        int from = 0, to = 0;
        if (and_ == std::string::npos || !ParseClock(rest.substr(0, and_), from) ||
            !ParseClock(rest.substr(and_ + 5), to)) {
            understood = false;
            return false;
        }
        const int minute = local.tm_hour * 60 + local.tm_min;
        // 22:00 to 06:30 wraps past midnight.
        return from <= to ? (minute >= from && minute < to) : (minute >= from || minute < to);
    }
    if (lower.rfind("on ", 0) == 0) {
        std::stringstream days(c.substr(3));
        std::string day;
        bool any = false;
        while (std::getline(days, day, ',')) {
            const int d = DayIndex(day);
            if (d < 0) { understood = false; return false; }
            any = true;
            if (d == local.tm_wday) return true;
        }
        understood = any;
        return false;
    }

    // "<deviceId> <attribute> <op> <value...>"
    std::istringstream words(c);
    std::string deviceId, attribute, op;
    if (!(words >> deviceId >> attribute >> op)) { understood = false; return false; }
    std::string value;
    std::getline(words, value);
    value = Trim(value);
    if (op != "=" && op != "==" && op != "!=" && op != "<" && op != ">" && op != "<=" && op != ">=") {
        understood = false;
        return false;
    }
    if (!host.readAttribute) return false;
    const auto actual = host.readAttribute(deviceId, attribute);
    return actual && Compare(*actual, op, value);
}

bool SmartHomeAutomationEngine::ConditionsHold(const SmartHomeAutomation& a, int64_t now, std::string* why) {
    for (const std::string& condition : a.Conditions) {
        bool understood = true;
        if (!ConditionHolds(condition, now, understood)) {
            if (why) *why = understood ? "\"" + condition + "\" does not hold"
                                       : "\"" + condition + "\" is not a condition the engine can read";
            return false;
        }
    }
    return true;
}

// ===== EVALUATION =====

void SmartHomeAutomationEngine::Evaluate(const std::vector<SmartHomeAutomation>& automations, int64_t now) {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    // Delayed actions first: they were decided earlier.
    std::vector<Delayed> due;
    for (auto it = delayed.begin(); it != delayed.end();) {
        if (it->due <= now) { due.push_back(*it); it = delayed.erase(it); }
        else ++it;
    }
    for (const Delayed& d : due) ExecuteOne(d.automationId, d.command);

    const std::tm local = LocalTime(now);
    const int today = DayKey(local);
    for (const SmartHomeAutomation& a : automations) {
        if (!a.Enabled) continue;
        State& s = states[a.AutomationId];
        // An edited trigger starts afresh.
        const std::string seen = a.TriggerType + "\n" + a.TriggerConfig;
        if (s.configSeen != seen) {
            s = State();
            s.configSeen = seen;
            s.trigger = ParseTrigger(a);
        }
        if (!s.trigger.valid) {
            if (!s.reportedProblem) {
                s.reportedProblem = true;
                Log(a.AutomationId, "\"" + a.Name + "\" has a trigger the engine cannot read", now, "error");
            }
            continue;
        }
        const Trigger& t = s.trigger;
        if (!t.days.empty() && std::find(t.days.begin(), t.days.end(), local.tm_wday) == t.days.end()) continue;

        std::string reason;
        if (t.type == "device") {
            const auto actual = host.readAttribute ? host.readAttribute(t.deviceId, t.attribute) : std::nullopt;
            const bool matched = actual && Compare(*actual, t.op, t.value);
            if (!s.primed) {
                // The state the home is in when the engine starts is not an
                // event: a leak sensor already wet does not fire again.
                s.primed = true;
                s.matched = matched;
                s.matchedSince = now;
                s.firedThisMatch = matched;
                continue;
            }
            if (matched && !s.matched) { s.matchedSince = now; s.firedThisMatch = false; }
            s.matched = matched;
            if (!matched || s.firedThisMatch || now - s.matchedSince < t.forMinutes * 60) continue;
            s.firedThisMatch = true;
            reason = t.attribute + " " + t.op + " " + t.value;
            if (t.forMinutes > 0) reason += " for " + std::to_string(t.forMinutes) + " min";
        } else if (t.type == "time" || t.type == "sunrise" || t.type == "sunset") {
            if (s.lastFiredDay == today) continue;
            int64_t at = 0;
            if (t.type == "time") {
                at = AtMinuteOfDay(now, t.minuteOfDay);
                reason = "at " + ClockText(t.minuteOfDay);
            } else {
                double latitude = 0, longitude = 0;
                int64_t rise = 0, set = 0;
                if (!host.location || !host.location(latitude, longitude)) {
                    if (!s.reportedProblem) {
                        s.reportedProblem = true;
                        Log(a.AutomationId, "\"" + a.Name + "\" waits for the home's location (" + t.type + ")", now, "error");
                    }
                    continue;
                }
                if (!ComputeSunTimes(latitude, longitude, now, rise, set)) continue;
                at = (t.type == "sunrise" ? rise : set) + t.offsetMinutes * 60;
                reason = "at " + t.type;
                if (t.offsetMinutes != 0) {
                    reason += (t.offsetMinutes > 0 ? " + " : " - ") + std::to_string(std::abs(t.offsetMinutes)) + " min";
                }
            }
            if (now < at || now >= at + kFireWindowSeconds) continue;
            s.lastFiredDay = today;
        } else {
            continue;   // manual (RunAutomation) and location (not yet)
        }

        std::string why;
        if (!ConditionsHold(a, now, &why)) continue;
        Run(a, now, reason);
    }
}

bool SmartHomeAutomationEngine::Run(const SmartHomeAutomation& a, int64_t now, const std::string& reason) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    State& s = states[a.AutomationId];
    while (!s.recentRuns.empty() && now - s.recentRuns.front() >= kLoopWindowSeconds) s.recentRuns.pop_front();
    if (s.recentRuns.size() >= kLoopRuns) {
        // It keeps setting itself off. Stop it before it flaps the house.
        s.recentRuns.clear();
        delayed.erase(std::remove_if(delayed.begin(), delayed.end(),
                                     [&](const Delayed& d) { return d.automationId == a.AutomationId; }),
                      delayed.end());
        Log(a.AutomationId, "Switched \"" + a.Name + "\" off: it ran " + std::to_string(kLoopRuns + 1) +
                                " times in " + std::to_string(kLoopWindowSeconds) + " seconds, which looks like a loop",
            now, "error");
        if (host.disable) host.disable(a.AutomationId);
        return false;
    }
    s.recentRuns.push_back(now);
    Log(a.AutomationId, "Ran \"" + a.Name + "\"" + (reason.empty() ? "" : " (" + reason + ")"), now);
    if (host.ran) host.ran(a.AutomationId);
    Execute(a.AutomationId, a.Actions, 0, now);
    return true;
}

void SmartHomeAutomationEngine::Execute(const std::string& automationId, const std::vector<SmartHomeCommand>& actions,
                                        size_t from, int64_t now) {
    int64_t at = now;
    for (size_t i = from; i < actions.size(); ++i) {
        const SmartHomeCommand& action = actions[i];
        if (action.Command == "delay") {
            float seconds = 0;
            auto it = action.Parameters.find("seconds");
            if (it != action.Parameters.end() && TryParseFloat(it->second, seconds) && seconds > 0) {
                at += static_cast<int64_t>(std::llround(seconds));
            }
            continue;
        }
        if (at > now) delayed.push_back({at, automationId, action});
        else ExecuteOne(automationId, action);
    }
}

void SmartHomeAutomationEngine::ExecuteOne(const std::string& automationId, const SmartHomeCommand& action) {
    if (action.Command == "notify") {
        auto it = action.Parameters.find("message");
        const std::string message = it != action.Parameters.end() ? it->second : std::string();
        if (host.notify) host.notify(automationId, message);
        return;
    }
    if (action.Command == "activateScene") {
        auto it = action.Parameters.find("sceneId");
        if (it != action.Parameters.end() && host.activateScene) host.activateScene(it->second);
        return;
    }
    if (host.sendCommand) host.sendCommand(action);
}

void SmartHomeAutomationEngine::Forget(const std::string& automationId) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    states.erase(automationId);
    delayed.erase(std::remove_if(delayed.begin(), delayed.end(),
                                 [&](const Delayed& d) { return d.automationId == automationId; }),
                  delayed.end());
}

void SmartHomeAutomationEngine::Log(const std::string& automationId, const std::string& text, int64_t now,
                                    const std::string& kind) {
    if (!host.log) return;
    SmartHomeEvent event;
    event.Time = now;
    event.Kind = kind;
    event.SubjectId = automationId;
    event.Text = text;
    host.log(event);
}

} // namespace SmartHome
} // namespace UltraCanvas
