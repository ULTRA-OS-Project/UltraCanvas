// Apps/SmartHomeCenter/tests/SmartHomeCenterCliTest.cpp
// SmartHome Center's command line on the demo home, with no display: the
// functions main() calls for --list, --set, --scene, --history and --log
// (ui/SmartHomeCenterCli.h), and what they leave the home in.
//
//  1. --list prints the home room by room; --csv gives one row per device
//     under a header.
//  2. --set switches, dims, sets a thermostat, unlocks a lock and moves a
//     blind, and the device is in that state when it returns; it refuses an
//     attribute the device does not have (exit 2), a value out of range and
//     a device that does not exist (exit 1), and asks for an id when two
//     devices share a name.
//  3. --scene runs a demo scene by name or id; an unknown one lists the
//     scenes there are.
//  4. --log has the scene's activation; --history the readings of a room
//     that warmed up.
//  5. Durations: 90s, 15m, 24h, 7d, a plain number.
//
// The demo home runs on the test's clock (realTime off), so nothing moves
// unless the test advances it.
// Author: UltraCanvas Framework
#include "SmartHomeCenterCli.h"

#include "SimulatedProtocol.h"
#include "UltraCanvasSmartHome.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace UltraCanvas::SmartHome;
using namespace SmartHomeCenter;

namespace {

int failures = 0;

void Check(bool condition, const std::string& what) {
    std::printf("%s  %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition) ++failures;
}

bool Has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

std::string Id(const std::string& name) {
    return FindDevice(name, nullptr);
}

size_t Lines(const std::string& text) {
    size_t n = 0;
    for (char c : text) n += c == '\n';
    return n;
}

} // namespace

int main() {
    SimulatedHomeOptions options;
    options.realTime = false;
    options.randomEvents = false;
    auto sim = std::make_shared<SimulatedProtocol>(options);
    auto& api = SmartHomeAPI::Instance();
    api.Initialize();
    api.RegisterProtocol(SmartHomeProtocolType::Simulated, sim);
    api.EnableProtocol(SmartHomeProtocolType::Simulated);
    std::string storeError;
    const bool store = OpenHome(true, ":memory:", &storeError);
    if (!store) std::printf("      (no store: %s)\n", storeError.c_str());

    std::puts("-- list");
    const CliResult list = ListHome(false);
    Check(list.exitCode == 0 && Has(list.out, "\nLiving room\n") && Has(list.out, "Kitchen spots"),
          "--list prints the home room by room");
    const CliResult csv = ListHome(true);
    Check(csv.out.rfind("room,name,type,state,value,battery,power_w,online,id\n", 0) == 0,
          "--list --csv starts with its header");
    Check(Lines(csv.out) == api.GetDevices().size() + 1, "and has a row per device");
    Check(Has(csv.out, "Kitchen,Kitchen spots,"), "a row reads room,name,...");

    std::puts("-- set");
    const std::string spots = Id("kitchen spots");
    Check(!spots.empty(), "a device is found by name, not minding case");
    CliResult r = SetAttribute("Kitchen spots", "on=true");
    Check(r.exitCode == 0 && api.GetLightState(spots).On && r.out.rfind("Kitchen spots: On", 0) == 0,
          "on=true switches a light on, and says so");
    r = SetAttribute("Kitchen spots", "brightness=40");
    Check(r.exitCode == 0 && api.GetLightState(spots).Brightness == 102, "brightness=40 dims it to 40 %");
    r = SetAttribute("Kettle", "on=1");
    Check(r.exitCode == 0 && api.GetSwitchState(Id("Kettle")), "on=1 switches a plug");
    const std::string heating = Id("Living room heating");
    r = SetAttribute("Living room heating", "target=22.5");
    Check(r.exitCode == 0 && std::fabs(api.GetThermostatState(heating).TargetTemperature - 22.5f) < 0.01f,
          "target=22.5 sets a thermostat (a dot, whatever the locale)");
    r = SetAttribute("Living room heating", "mode=off");
    Check(r.exitCode == 0 && api.GetThermostatState(heating).Mode == "off", "mode=off");
    r = SetAttribute("Front door", "locked=false");
    Check(r.exitCode == 0 && !api.GetLockState(Id("Front door")).Locked, "locked=false unlocks");
    r = SetAttribute("Patio blinds", "position=40");
    Check(r.exitCode == 0 && Has(r.out, "moving to 40 %"), "position=40 sends a blind on its way");
    for (int i = 0; i < 30; ++i) sim->Advance(1.0);
    Check(api.GetBlindPosition(Id("Patio blinds")) == 40, "and it gets there");

    r = SetAttribute("Kitchen spots", "target=21");
    Check(r.exitCode == 2 && Has(r.err, "has no target temperature"), "an attribute the device lacks: exit 2");
    r = SetAttribute("Kitchen spots", "brightness=150");
    Check(r.exitCode == 1 && Has(r.err, "0 to 100"), "a value out of range: exit 1");
    r = SetAttribute("Kitchen spots", "colour=red");
    Check(r.exitCode == 1 && Has(r.err, "Unknown attribute"), "an unknown attribute: exit 1");
    r = SetAttribute("Kitchen spots", "brightness");
    Check(r.exitCode == 1, "no =value: exit 1");
    r = SetAttribute("Garage door", "on=true");
    Check(r.exitCode == 1 && Has(r.err, "--list"), "a device that does not exist: exit 1, pointing at --list");
    api.RenameDevice(Id("Kettle"), "Desk lamp");
    std::string error;
    Check(FindDevice("desk lamp", &error).empty() && Has(error, "2 devices"), "two devices of one name: asks for an id");

    std::puts("-- scene");
    r = RunScene("good night");
    Check(r.exitCode == 0 && Has(r.out, "Ran \"Good night\""), "--scene runs a demo scene by name");
    Check(api.GetLockState(Id("Front door")).Locked && !api.GetLightState(spots).On,
          "and the home is in it: the door locked, the kitchen dark");
    r = RunScene("center.away");
    Check(r.exitCode == 0 && Has(r.out, "\"Away\""), "and by id");
    r = RunScene("Party");
    Check(r.exitCode == 1 && Has(r.err, "Good morning"), "an unknown scene: exit 1, listing the scenes");

    std::puts("-- log and history");
    const CliResult log = ShowLog(3600);
    Check(log.exitCode == 0 && Has(log.out, "Activated \"Good night\""), "--log has the scene's activation");
    if (store) {
        api.SetThermostatMode(heating, "heat");
        api.SetThermostatTarget(heating, 26.0f);
        api.WaitForCommands();
        for (int i = 0; i < 600; ++i) sim->Advance(1.0);
        const CliResult history = ShowHistory("Living room heating", 3600, false);
        Check(history.exitCode == 0 && Lines(history.out) >= 3, "--history lists a warming room's readings");
        const CliResult historyCsv = ShowHistory("Living room heating", 3600, true);
        Check(historyCsv.out.rfind("time,value,unit,triggered\n", 0) == 0 && Lines(historyCsv.out) >= 3,
              "--history --csv: a header, then a row per reading");
    } else {
        Check(ShowHistory("Living room heating", 3600, false).exitCode == 2, "without a store --history says so");
    }

    std::puts("-- durations");
    int64_t s = 0;
    Check(ParseDuration("90s", s) && s == 90, "90s");
    Check(ParseDuration("15m", s) && s == 900, "15m");
    Check(ParseDuration("24h", s) && s == 86400, "24h");
    Check(ParseDuration("7d", s) && s == 604800, "7d");
    Check(ParseDuration("45", s) && s == 45, "a plain number is seconds");
    Check(!ParseDuration("h", s) && !ParseDuration("1.5h", s) && !ParseDuration("", s), "nonsense is refused");

    api.Shutdown();
    std::printf("%s - SmartHome Center's command line\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
