// Apps/SmartHomeCenter/ui/SmartHomeCenterCli.h
// SmartHome Center's command line, apart from the window: start the home,
// and the actions that work on it with no display - list it, set a device,
// run a scene, show a device's history and the event log.
//
// Each action returns its text and an exit code instead of printing, so
// main() prints and SmartHomeCenterCliTest checks the very same output.
// The words are the model's (SmartHomeCenterModel.h): --list says what the
// window's tiles say. CSV and the history's numbers are written with a dot
// whatever the locale, since they are read by programs.
//
// Phase 1 runs the module in-process: these act on the home this process
// starts. While the window holds a radio, a second process cannot open it;
// the hub service (proposal §4.2, phase 3) lifts that.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>

namespace SmartHomeCenter {

// Starts the module and its radios; true when no radio could start and the
// home is the simulated one (or `forceDemo` asked for it).
bool StartHome(bool forceDemo);

// Opens the home's store - `path`, or the per-user one when empty - and
// seeds the demo home when it is simulated. False (with `error` filled)
// when the store did not open; the home still works without it.
bool OpenHome(bool simulated, const std::string& path, std::string* error);

struct CliResult {
    int exitCode = 0;     // 0 done; 1 bad input or not found; 2 the device refused or cannot do it
    std::string out;      // for stdout
    std::string err;      // for stderr
};

// --list [--csv]: room by room, as the window shows it; CSV with a header
// row (room,name,type,state,value,battery,power_w,online,id).
CliResult ListHome(bool csv);

// --set <device> <attribute>=<value>: the device by name (not minding case)
// or id. Attributes: on (true/false, on/off, 1/0), brightness (0-100),
// colorTemp (kelvin), target (°C), mode (heat, cool, auto, off), locked
// (true/false), position (0-100). Waits until the command has been sent.
CliResult SetAttribute(const std::string& device, const std::string& assignment);

// --scene <name|id>: runs a scene, by id or name (not minding case).
CliResult RunScene(const std::string& scene);

// --history <device> [--since 24h] [--csv]: the recorded readings, oldest
// first; CSV columns time,value,unit,triggered (time in ISO 8601, local).
CliResult ShowHistory(const std::string& device, int64_t sinceSeconds, bool csv);

// --log [--since 1h]: the event log, oldest first.
CliResult ShowLog(int64_t sinceSeconds);

// "90s", "15m", "24h", "7d", or a plain number of seconds.
bool ParseDuration(const std::string& text, int64_t& seconds);

// The id of a device by name (not minding case) or id; "" with `error`
// filled when there is none, or more than one with that name.
std::string FindDevice(const std::string& nameOrId, std::string* error);

} // namespace SmartHomeCenter
