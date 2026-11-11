// protocols/Simulated/SimulatedProtocol.cpp
// The demo home: device models, the furnished sample home and the clock.
// See SimulatedProtocol.h for what it is for.
//
// Every device is an object of the matching ISmartHomeDevice interface
// (ISmartLight, ISmartThermostat, ...), which is what SmartHomeAPI's state
// queries read, plus the SimulatedDevice side the protocol drives: Advance()
// moves it on in time, Command() answers a command. Both report whether
// anything visible changed, and the protocol turns that into one device
// update - so a thermostat that warms by a hundredth of a degree does not
// notify, while one whose displayed tenth changes does.
//
// Locking: each device guards its own state with its own mutex and never
// calls out while holding it. The protocol's simMutex guards the device list
// and the clock; callbacks are fired with neither held, because the manager
// answers an update by reading the device straight back.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SimulatedProtocol.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <locale>
#include <sstream>

namespace UltraCanvas {
namespace SmartHome {

namespace {

using Params = std::map<std::string, std::string>;

constexpr double kClockStepSeconds = 0.5;

uint64_t NowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

// Dot-decimal, whatever LC_NUMERIC says: commands carry numbers as text
// written by SmartHomeAPI with a classic-locale stream. The engine cannot use
// UltraCanvas's TryParseFloat - it builds without the UI library - and the
// grammar needed is small: an optional sign, digits, an optional fraction.
bool ParseNumber(const std::string& text, double& out) {
    size_t i = 0;
    while (i < text.size() && text[i] == ' ') ++i;
    bool negative = false;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) negative = text[i++] == '-';
    double value = 0.0;
    bool digits = false;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        value = value * 10.0 + (text[i++] - '0');
        digits = true;
    }
    if (i < text.size() && text[i] == '.') {
        double scale = 0.1;
        for (++i; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
            value += (text[i] - '0') * scale;
            scale *= 0.1;
            digits = true;
        }
    }
    if (!digits) return false;
    out = negative ? -value : value;
    return true;
}

bool ParamNumber(const Params& params, const char* key, double& out) {
    auto it = params.find(key);
    return it != params.end() && ParseNumber(it->second, out);
}

std::string FormatNumber(double value, int decimals = 1) {
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text.setf(std::ios::fixed);
    text.precision(decimals);
    text << value;
    return text.str();
}

double Clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }

// The value a person sees, to the resolution they see it at: a change below
// this is not worth an update.
double Tenths(double v) { return std::round(v * 10.0) / 10.0; }

} // namespace

// ===== THE SIMULATION SIDE OF A DEVICE =====

class SimulatedDevice {
public:
    virtual ~SimulatedDevice() = default;

    // Moves the device on in time; true when anything visible changed.
    virtual bool Advance(double seconds, std::mt19937& random) = 0;
    // Answers a command; true when it was understood and carried out.
    virtual bool Command(const std::string& command, const Params& params) = 0;
    virtual void FillState(Params& state) const = 0;
    virtual std::vector<std::string> Commands() const = 0;
    virtual bool IsMotionSensor() const { return false; }

    std::string Id() const { std::lock_guard<std::mutex> l(m); return id; }
    SmartHomeDeviceInfo Info() const {
        std::lock_guard<std::mutex> l(m);
        SmartHomeDeviceInfo info;
        info.DeviceId = id;
        info.Name = name;
        info.Manufacturer = manufacturer;
        info.Model = model;
        info.FirmwareVersion = firmware;
        info.SerialNumber = serial;
        info.Category = category;
        info.Protocol = SmartHomeProtocolType::Simulated;
        info.State = reachable ? SmartHomeDeviceState::Online : SmartHomeDeviceState::Offline;
        info.Security = SmartHomeSecurityLevel::Encrypted;
        info.Reachable = reachable;
        info.LastSeen = lastSeen;
        info.Capabilities = tags;
        info.Room = room;
        return info;
    }

    // Runs a command, or one of the four that stand in for the outside world.
    bool Handle(const std::string& command, const Params& params) {
        if (command == "simulateOffline" || command == "simulateOnline") {
            std::lock_guard<std::mutex> l(m);
            const bool online = command == "simulateOnline";
            if (reachable == online) return false;
            reachable = online;
            if (online) lastSeen = NowMs();
            return true;
        }
        {
            // An unreachable device does not answer: the command fails, as
            // it would over a radio.
            std::lock_guard<std::mutex> l(m);
            if (!reachable) return false;
        }
        if (!Command(command, params)) return false;
        std::lock_guard<std::mutex> l(m);
        lastSeen = NowMs();
        return true;
    }

    bool IsReachableNow() const { std::lock_guard<std::mutex> l(m); return reachable; }

    // Identity, filled in once by the sample home before anyone else sees
    // the device; after that only `name`, `reachable`, `lastSeen` and the
    // battery change, under the lock.
    std::string id, name, room, manufacturer = "UltraHome Demo", model, firmware = "1.0.0", serial;
    SmartHomeDeviceCategory category = SmartHomeDeviceCategory::Unknown;
    DeviceCapability caps = DeviceCapability::NoCapabilities;
    std::vector<std::string> tags;
    int linkQuality = 200;

protected:
    mutable std::mutex m;
    bool reachable = true;
    uint64_t lastSeen = NowMs();
    double battery = -1.0;              // -1: mains powered
};

// Everything ISmartHomeDevice asks of every device, answered from the
// SimulatedDevice side; `Interface` is the device kind (ISmartLight, ...).
template <typename Interface>
class SimulatedDeviceBase : public Interface, public SimulatedDevice {
public:
    std::string GetDeviceId() const override { return Id(); }
    std::string GetName() const override { std::lock_guard<std::mutex> l(m); return name; }
    void SetName(const std::string& newName) override { std::lock_guard<std::mutex> l(m); name = newName; }
    SmartHomeDeviceCategory GetCategory() const override { return category; }
    SmartHomeProtocolType GetProtocol() const override { return SmartHomeProtocolType::Simulated; }
    std::string GetManufacturer() const override { return manufacturer; }
    std::string GetModel() const override { return model; }
    std::string GetFirmwareVersion() const override { return firmware; }
    std::string GetSerialNumber() const override { return serial; }
    SmartHomeDeviceInfo GetDeviceInfo() const override { return Info(); }
    SmartHomeDeviceState GetState() const override {
        return IsReachableNow() ? SmartHomeDeviceState::Online : SmartHomeDeviceState::Offline;
    }
    bool IsReachable() const override { return IsReachableNow(); }
    uint64_t GetLastSeen() const override { std::lock_guard<std::mutex> l(m); return lastSeen; }
    void UpdateLastSeen() override { std::lock_guard<std::mutex> l(m); lastSeen = NowMs(); }

    DeviceCapability GetCapabilities() const override { return caps; }
    bool HasCapability(DeviceCapability capability) const override {
        return ::UltraCanvas::SmartHome::HasCapability(caps, capability);
    }
    std::vector<std::string> GetSupportedCommands() const override {
        std::vector<std::string> all = Commands();
        for (const char* c : {"simulateOffline", "simulateOnline"}) all.push_back(c);
        return all;
    }
    std::vector<DeviceEndpoint> GetEndpoints() const override { return {GetEndpoint(1)}; }
    DeviceEndpoint GetEndpoint(int endpointId) const override {
        DeviceEndpoint endpoint;
        endpoint.EndpointId = endpointId;
        endpoint.Name = GetName();
        endpoint.Category = category;
        endpoint.Capabilities = caps;
        return endpoint;
    }

    // The attribute API is for protocol-level tools; the simulated home has
    // no attribute table behind its devices. Its state is the typed API of
    // each device kind, and GetDeviceState() as text.
    std::map<std::string, DeviceAttribute> GetAttributes() const override { return {}; }
    DeviceAttribute GetAttribute(const std::string&) const override { return {}; }
    bool SetAttribute(const std::string&, const std::any&) override { return false; }
    bool HasAttribute(const std::string&) const override { return false; }

    // Direct calls change the device but do not notify; SmartHomeAPI goes
    // through SimulatedProtocol::SendCommand, which does.
    bool ExecuteCommand(const std::string& command, const Params& params) override {
        return Handle(command, params);
    }
    bool SupportsCommand(const std::string& command) const override {
        const auto all = GetSupportedCommands();
        return std::find(all.begin(), all.end(), command) != all.end();
    }
    bool Poll() override { return IsReachableNow(); }
    int GetPollInterval() const override { return 0; }   // it reports, never needs polling
    SmartHomeSecurityLevel GetSecurityLevel() const override { return SmartHomeSecurityLevel::Encrypted; }

    bool IsBatteryPowered() const override { std::lock_guard<std::mutex> l(m); return battery >= 0; }
    int GetBatteryLevel() const override {
        std::lock_guard<std::mutex> l(m);
        return battery < 0 ? -1 : static_cast<int>(std::lround(battery));
    }
    bool IsLowBattery() const override {
        std::lock_guard<std::mutex> l(m);
        return battery >= 0 && battery < 15.0;
    }

    void SetBattery(double level) { std::lock_guard<std::mutex> l(m); battery = level; }

protected:
    // Batteries run down, slowly enough to notice over a long demo; true
    // when the whole percent shown changed.
    bool DrainBattery(double seconds, double percentPerHour) {
        if (battery < 0) return false;
        const long before = std::lround(battery);
        battery = std::max(1.0, battery - percentPerHour * seconds / 3600.0);
        return std::lround(battery) != before;
    }
};

// ===== LIGHT =====

class SimulatedLight : public SimulatedDeviceBase<ISmartLight> {
public:
    SimulatedLight(bool dimmable, bool tunable, bool colour)
        : dimming(dimmable), whiteTuning(tunable), colouring(colour) {
        category = SmartHomeDeviceCategory::Light;
        caps = DeviceCapability::OnOff;
        tags = {"switch"};
        if (dimming) { caps = caps | DeviceCapability::Brightness; tags.push_back("dimmer"); }
        if (whiteTuning) { caps = caps | DeviceCapability::ColorTemp; tags.push_back("colourTemperature"); }
        if (colouring) { caps = caps | DeviceCapability::ColorRGB; tags.push_back("colour"); }
    }

    void Preset(bool isOn, uint8_t level, uint16_t kelvin = 2700) {
        on = isOn; brightness = dimming ? level : 255; colourTemp = kelvin;
    }

    bool TurnOn() override { return Command("turnOn", {}); }
    bool TurnOff() override { return Command("turnOff", {}); }
    bool Toggle() override { return Command("toggle", {}); }
    bool IsOn() const override { std::lock_guard<std::mutex> l(m); return on; }
    bool SetBrightness(uint8_t level) override { return Command("setBrightness", {{"level", std::to_string(level)}}); }
    uint8_t GetBrightness() const override { std::lock_guard<std::mutex> l(m); return brightness; }
    bool SetColorTemperature(uint16_t kelvin) override {
        return Command("setColorTemp", {{"kelvin", std::to_string(kelvin)}});
    }
    uint16_t GetColorTemperature() const override { std::lock_guard<std::mutex> l(m); return colourTemp; }
    bool SetColorRGB(uint8_t r, uint8_t g, uint8_t b) override {
        return Command("setColor", {{"r", std::to_string(r)}, {"g", std::to_string(g)}, {"b", std::to_string(b)}});
    }
    void GetColorRGB(uint8_t& r, uint8_t& g, uint8_t& b) const override {
        std::lock_guard<std::mutex> l(m); r = red; g = green; b = blue;
    }
    bool SetColorHSV(uint16_t, uint8_t, uint8_t) override { return false; }
    void GetColorHSV(uint16_t& hue, uint8_t& saturation, uint8_t& value) const override {
        hue = 0; saturation = 0; value = GetBrightness();
    }
    SmartHomeLightState GetLightState() const override {
        std::lock_guard<std::mutex> l(m);
        SmartHomeLightState state;
        state.On = on;
        state.Brightness = brightness;
        state.ColorTemp = whiteTuning ? colourTemp : 0;
        state.Red = red; state.Green = green; state.Blue = blue;
        state.Capabilities = static_cast<uint32_t>(SmartHomeLightCapability::OnOff);
        if (dimming) state.Capabilities |= static_cast<uint32_t>(SmartHomeLightCapability::Brightness);
        if (whiteTuning) state.Capabilities |= static_cast<uint32_t>(SmartHomeLightCapability::ColorTemp);
        if (colouring) state.Capabilities |= static_cast<uint32_t>(SmartHomeLightCapability::ColorRGB);
        return state;
    }
    bool SetLightState(const SmartHomeLightState& state) override {
        return Command("setLight", {{"on", state.On ? "true" : "false"},
                                    {"brightness", std::to_string(state.Brightness)},
                                    {"colorTemp", std::to_string(state.ColorTemp)}});
    }
    bool SupportsColorTemperature() const override { return whiteTuning; }
    bool SupportsColor() const override { return colouring; }
    bool SupportsDimming() const override { return dimming; }

    bool Advance(double, std::mt19937&) override { return false; }

    bool Command(const std::string& command, const Params& params) override {
        std::lock_guard<std::mutex> l(m);
        double v = 0;
        if (command == "turnOn") { on = true; if (brightness == 0) brightness = 255; return true; }
        if (command == "turnOff") { on = false; return true; }
        if (command == "toggle") { on = !on; if (on && brightness == 0) brightness = 255; return true; }
        if (command == "setBrightness" && dimming && ParamNumber(params, "level", v)) {
            brightness = static_cast<uint8_t>(Clamp(v, 0, 255));
            on = brightness > 0;
            return true;
        }
        if (command == "setColorTemp" && whiteTuning && ParamNumber(params, "kelvin", v)) {
            colourTemp = static_cast<uint16_t>(Clamp(v, 2200, 6500));
            return true;
        }
        if (command == "setColor" && colouring) {
            double r = 0, g = 0, b = 0;
            if (!ParamNumber(params, "r", r) || !ParamNumber(params, "g", g) || !ParamNumber(params, "b", b)) return false;
            red = static_cast<uint8_t>(Clamp(r, 0, 255));
            green = static_cast<uint8_t>(Clamp(g, 0, 255));
            blue = static_cast<uint8_t>(Clamp(b, 0, 255));
            return true;
        }
        if (command == "setLight") {
            // SmartHomeAPI::SetLightState sends every field; a zero means
            // "not given" for the colour fields, and brightness 0 with
            // on=true means "on at the last level", as a wall switch does.
            auto onIt = params.find("on");
            if (onIt != params.end()) on = onIt->second == "true";
            if (dimming && ParamNumber(params, "brightness", v) && v > 0) brightness = static_cast<uint8_t>(Clamp(v, 1, 255));
            if (on && brightness == 0) brightness = 255;
            if (whiteTuning && ParamNumber(params, "colorTemp", v) && v > 0) colourTemp = static_cast<uint16_t>(Clamp(v, 2200, 6500));
            double r = 0, g = 0, b = 0;
            if (colouring && ParamNumber(params, "r", r) && ParamNumber(params, "g", g) &&
                ParamNumber(params, "b", b) && (r + g + b) > 0) {
                red = static_cast<uint8_t>(Clamp(r, 0, 255));
                green = static_cast<uint8_t>(Clamp(g, 0, 255));
                blue = static_cast<uint8_t>(Clamp(b, 0, 255));
            }
            return true;
        }
        return false;
    }

    void FillState(Params& state) const override {
        std::lock_guard<std::mutex> l(m);
        state["on"] = on ? "true" : "false";
        state["brightness"] = std::to_string(brightness);
        if (whiteTuning) state["colorTemp"] = std::to_string(colourTemp);
        if (colouring) state["color"] = std::to_string(red) + "," + std::to_string(green) + "," + std::to_string(blue);
    }
    std::vector<std::string> Commands() const override {
        std::vector<std::string> c = {"turnOn", "turnOff", "toggle", "setLight"};
        if (dimming) c.push_back("setBrightness");
        if (whiteTuning) c.push_back("setColorTemp");
        if (colouring) c.push_back("setColor");
        return c;
    }

private:
    const bool dimming, whiteTuning, colouring;
    bool on = false;
    uint8_t brightness = 255;
    uint16_t colourTemp = 2700;
    uint8_t red = 255, green = 180, blue = 100;
};

// ===== SWITCH / PLUG =====

// What is plugged in decides how a metered plug draws power.
enum class SimulatedLoad {
    Unmetered,           // an unmetered switch
    Steady,         // a constant draw with a little noise: a TV, a computer
    Fridge,         // always on; the compressor cycles between 95 W and 2 W
    Kettle          // heats at full power for a minute, then keeps warm
};

class SimulatedPlug : public SimulatedDeviceBase<ISmartSwitch> {
public:
    SimulatedPlug(SimulatedLoad kind, double ratedWatts) : load(kind), rated(ratedWatts) {
        category = kind == SimulatedLoad::Unmetered ? SmartHomeDeviceCategory::Switch : SmartHomeDeviceCategory::Plug;
        caps = DeviceCapability::OnOff;
        tags = {"switch"};
        if (kind != SimulatedLoad::Unmetered) {
            caps = caps | DeviceCapability::Power | DeviceCapability::Energy;
            tags.push_back("power");
            tags.push_back("energy");
        }
    }

    void Preset(bool isOn, double kWh = 0.0) { on = isOn; energy = kWh; if (on) watts = Draw(0); }

    bool TurnOn() override { return Command("turnOn", {}); }
    bool TurnOff() override { return Command("turnOff", {}); }
    bool Toggle() override { return Command("toggle", {}); }
    bool IsOn() const override { std::lock_guard<std::mutex> l(m); return on; }
    bool SupportsPowerMonitoring() const override { return load != SimulatedLoad::Unmetered; }
    float GetPower() const override { std::lock_guard<std::mutex> l(m); return static_cast<float>(watts); }
    float GetVoltage() const override { return SupportsPowerMonitoring() ? 230.0f : 0.0f; }
    float GetCurrent() const override { return GetPower() / 230.0f; }
    float GetEnergyUsage() const override { std::lock_guard<std::mutex> l(m); return static_cast<float>(energy); }

    bool Advance(double seconds, std::mt19937& random) override {
        std::lock_guard<std::mutex> l(m);
        if (load == SimulatedLoad::Unmetered) return false;
        energy += watts * seconds / 3600000.0;
        phase += seconds;
        sinceNoise += seconds;
        const double before = std::round(watts);
        if (!on) {
            watts = 0;
        } else if (sinceNoise >= 5.0 || load != SimulatedLoad::Steady) {
            sinceNoise = 0;
            std::uniform_real_distribution<double> noise(-0.03, 0.03);
            watts = Draw(phase) * (1.0 + (load == SimulatedLoad::Steady ? noise(random) : 0.0));
        }
        return std::round(watts) != before;
    }

    bool Command(const std::string& command, const Params&) override {
        std::lock_guard<std::mutex> l(m);
        bool wanted = on;
        if (command == "turnOn") wanted = true;
        else if (command == "turnOff") wanted = false;
        else if (command == "toggle") wanted = !on;
        else return false;
        if (wanted && !on) phase = 0;        // a kettle starts heating from cold
        on = wanted;
        watts = on ? Draw(phase) : 0.0;
        return true;
    }

    void FillState(Params& state) const override {
        std::lock_guard<std::mutex> l(m);
        state["on"] = on ? "true" : "false";
        if (load != SimulatedLoad::Unmetered) {
            state["power"] = FormatNumber(watts);
            state["energy"] = FormatNumber(energy, 3);
        }
    }
    std::vector<std::string> Commands() const override { return {"turnOn", "turnOff", "toggle"}; }

private:
    double Draw(double t) const {
        switch (load) {
            case SimulatedLoad::Steady: return rated;
            case SimulatedLoad::Fridge: return std::fmod(t, 900.0) < 360.0 ? rated : 2.0;
            case SimulatedLoad::Kettle: return t < 60.0 ? rated : 25.0;
            default: return 0.0;
        }
    }

    const SimulatedLoad load;
    const double rated;
    bool on = false;
    double watts = 0.0;
    double energy = 0.0;          // kWh
    double phase = 0.0;           // seconds into the appliance's own cycle
    double sinceNoise = 0.0;
};

// ===== THERMOSTAT =====

class SimulatedThermostat : public SimulatedDeviceBase<ISmartThermostat> {
public:
    SimulatedThermostat() {
        category = SmartHomeDeviceCategory::Thermostat;
        caps = DeviceCapability::Temperature | DeviceCapability::Humidity | DeviceCapability::Mode;
        tags = {"thermostat", "temperature", "humidity"};
    }

    void Preset(double current, double target, const std::string& startMode = "heat") {
        temperature = current; setpoint = target; mode = startMode;
    }

    float GetCurrentTemperature() const override { std::lock_guard<std::mutex> l(m); return static_cast<float>(Tenths(temperature)); }
    float GetTargetTemperature() const override { std::lock_guard<std::mutex> l(m); return static_cast<float>(setpoint); }
    bool SetTargetTemperature(float t) override { return Command("setTargetTemp", {{"temperature", FormatNumber(t)}}); }
    float GetHumidity() const override { std::lock_guard<std::mutex> l(m); return static_cast<float>(std::round(humidity)); }
    std::string GetMode() const override { std::lock_guard<std::mutex> l(m); return mode; }
    bool SetMode(const std::string& newMode) override { return Command("setMode", {{"mode", newMode}}); }
    std::vector<std::string> GetSupportedModes() const override { return {"heat", "cool", "auto", "off"}; }
    std::string GetFanMode() const override { std::lock_guard<std::mutex> l(m); return fanMode; }
    bool SetFanMode(const std::string& fan) override { return Command("setFanMode", {{"mode", fan}}); }
    bool IsRunning() const override { std::lock_guard<std::mutex> l(m); return running; }
    SmartHomeThermostatState GetThermostatState() const override {
        std::lock_guard<std::mutex> l(m);
        SmartHomeThermostatState state;
        state.CurrentTemperature = static_cast<float>(Tenths(temperature));
        state.TargetTemperature = static_cast<float>(setpoint);
        state.Humidity = static_cast<float>(std::round(humidity));
        state.Mode = mode;
        state.FanMode = fanMode;
        state.Running = running;
        return state;
    }

    // The room warms at about a degree every two minutes while heating and
    // cools towards the house's 17 °C when not: quick enough to watch in a
    // demo, slow enough to look like a room.
    bool Advance(double seconds, std::mt19937& random) override {
        std::lock_guard<std::mutex> l(m);
        const double shownBefore = Tenths(temperature);
        const bool runningBefore = running;
        const bool heat = mode == "heat" || (mode == "auto" && temperature < setpoint);
        const bool cool = mode == "cool" || (mode == "auto" && temperature > setpoint + 1.0);
        // Hysteresis of a fifth of a degree either side, as a real one has.
        if (heat) running = running ? temperature < setpoint + 0.2 : temperature < setpoint - 0.2;
        else if (cool) running = running ? temperature > setpoint - 0.2 : temperature > setpoint + 0.2;
        else running = false;

        if (running && heat) temperature += 0.008 * seconds;
        else if (running && cool) temperature -= 0.008 * seconds;
        else temperature += (17.0 - temperature) * 0.0008 * seconds;

        std::uniform_real_distribution<double> drift(-0.01, 0.01);
        humidity = Clamp(humidity + drift(random) * seconds, 35.0, 60.0);
        return Tenths(temperature) != shownBefore || running != runningBefore;
    }

    bool Command(const std::string& command, const Params& params) override {
        std::lock_guard<std::mutex> l(m);
        double v = 0;
        if (command == "setTargetTemp" && ParamNumber(params, "temperature", v)) {
            setpoint = std::round(Clamp(v, 10.0, 30.0) * 2.0) / 2.0;   // half-degree steps
            return true;
        }
        if (command == "setMode") {
            auto it = params.find("mode");
            if (it == params.end()) return false;
            const std::string& next = it->second;
            if (next != "heat" && next != "cool" && next != "auto" && next != "off") return false;
            mode = next;
            if (mode == "off") running = false;
            return true;
        }
        if (command == "setFanMode") {
            auto it = params.find("mode");
            if (it == params.end()) return false;
            fanMode = it->second;
            return true;
        }
        return false;
    }

    void FillState(Params& state) const override {
        std::lock_guard<std::mutex> l(m);
        state["temperature"] = FormatNumber(temperature);
        state["target"] = FormatNumber(setpoint);
        state["humidity"] = FormatNumber(humidity, 0);
        state["mode"] = mode;
        state["running"] = running ? "true" : "false";
    }
    std::vector<std::string> Commands() const override { return {"setTargetTemp", "setMode", "setFanMode"}; }

private:
    double temperature = 19.0;
    double setpoint = 21.0;
    double humidity = 45.0;
    std::string mode = "heat";
    std::string fanMode = "auto";
    bool running = false;
};

// ===== LOCK =====

class SimulatedLock : public SimulatedDeviceBase<ISmartLock> {
public:
    SimulatedLock() {
        category = SmartHomeDeviceCategory::Lock;
        caps = DeviceCapability::Lock | DeviceCapability::Battery;
        tags = {"lock", "battery"};
        battery = 84.0;
    }

    void Preset(bool isLocked, double level) { locked = isLocked; battery = level; }

    bool Lock() override { return Command("lock", {}); }
    bool Unlock() override { return Command("unlock", {}); }
    bool IsLocked() const override { std::lock_guard<std::mutex> l(m); return locked; }
    bool IsJammed() const override { return false; }
    SmartHomeLockState GetLockState() const override {
        std::lock_guard<std::mutex> l(m);
        SmartHomeLockState state;
        state.Locked = locked;
        state.Jammed = false;
        state.BatteryLevel = static_cast<uint8_t>(std::lround(battery));
        state.LastUser = lastUser;
        state.LastActivity = lastActivity;
        return state;
    }
    std::string GetLastUser() const override { std::lock_guard<std::mutex> l(m); return lastUser; }
    uint64_t GetLastActivity() const override { std::lock_guard<std::mutex> l(m); return lastActivity; }

    bool Advance(double seconds, std::mt19937&) override {
        std::lock_guard<std::mutex> l(m);
        return DrainBattery(seconds, 0.05);
    }

    bool Command(const std::string& command, const Params& params) override {
        std::lock_guard<std::mutex> l(m);
        if (command != "lock" && command != "unlock") return false;
        locked = command == "lock";
        auto user = params.find("user");
        lastUser = user != params.end() ? user->second : "SmartHome Center";
        lastActivity = NowMs();
        return true;
    }

    void FillState(Params& state) const override {
        std::lock_guard<std::mutex> l(m);
        state["locked"] = locked ? "true" : "false";
        state["battery"] = FormatNumber(battery, 0);
    }
    std::vector<std::string> Commands() const override { return {"lock", "unlock"}; }

private:
    bool locked = true;
    std::string lastUser;
    uint64_t lastActivity = 0;
};

// ===== BLIND =====

class SimulatedBlind : public SimulatedDeviceBase<ISmartBlind> {
public:
    SimulatedBlind() {
        category = SmartHomeDeviceCategory::Blind;
        caps = DeviceCapability::Position | DeviceCapability::Tilt;
        tags = {"position", "tilt"};
    }

    void Preset(double open) { position = target = open; }

    bool Open() override { return Command("open", {}); }
    bool Close() override { return Command("close", {}); }
    bool Stop() override { return Command("stop", {}); }
    bool SetPosition(uint8_t p) override { return Command("setPosition", {{"position", std::to_string(p)}}); }
    uint8_t GetPosition() const override { std::lock_guard<std::mutex> l(m); return static_cast<uint8_t>(std::lround(position)); }
    bool SupportsTilt() const override { return true; }
    bool SetTilt(uint8_t angle) override { return Command("setTilt", {{"tilt", std::to_string(angle)}}); }
    uint8_t GetTilt() const override { std::lock_guard<std::mutex> l(m); return tilt; }
    bool IsMoving() const override { std::lock_guard<std::mutex> l(m); return std::fabs(target - position) > 0.01; }

    // A motor that takes about eight seconds from closed to open.
    bool Advance(double seconds, std::mt19937&) override {
        std::lock_guard<std::mutex> l(m);
        if (std::fabs(target - position) < 0.01) return false;
        const long before = std::lround(position);
        const double step = 12.5 * seconds;
        position = target > position ? std::min(target, position + step) : std::max(target, position - step);
        return std::lround(position) != before || std::fabs(target - position) < 0.01;
    }

    bool Command(const std::string& command, const Params& params) override {
        std::lock_guard<std::mutex> l(m);
        double v = 0;
        if (command == "open") { target = 100; return true; }
        if (command == "close") { target = 0; return true; }
        if (command == "stop") { target = position; return true; }
        if (command == "setPosition" && ParamNumber(params, "position", v)) { target = Clamp(v, 0, 100); return true; }
        if (command == "setTilt" && ParamNumber(params, "tilt", v)) { tilt = static_cast<uint8_t>(Clamp(v, 0, 100)); return true; }
        return false;
    }

    void FillState(Params& state) const override {
        std::lock_guard<std::mutex> l(m);
        state["position"] = FormatNumber(position, 0);
        state["target"] = FormatNumber(target, 0);
        state["tilt"] = std::to_string(tilt);
        state["moving"] = std::fabs(target - position) > 0.01 ? "true" : "false";
    }
    std::vector<std::string> Commands() const override { return {"open", "close", "stop", "setPosition", "setTilt"}; }

private:
    double position = 100.0;      // 100 = fully open
    double target = 100.0;
    uint8_t tilt = 50;
};

// ===== SENSOR =====

class SimulatedSensor : public SimulatedDeviceBase<ISmartSensor> {
public:
    SimulatedSensor(SmartHomeSensorType kind, double startValue) : type(kind), value(startValue), centre(startValue) {
        category = SmartHomeDeviceCategory::Sensor;
        battery = 90.0;
        switch (type) {
            case SmartHomeSensorType::Temperature: caps = DeviceCapability::Temperature; tags = {"temperature"}; unit = "°C"; break;
            case SmartHomeSensorType::Humidity:    caps = DeviceCapability::Humidity; tags = {"humidity"}; unit = "%"; break;
            case SmartHomeSensorType::Motion:      caps = DeviceCapability::Motion; tags = {"binarySensor", "motion"}; break;
            case SmartHomeSensorType::Contact:     caps = DeviceCapability::Contact; tags = {"binarySensor", "contact"}; break;
            default:                               tags = {"binarySensor"}; break;
        }
        caps = caps | DeviceCapability::Battery;
        tags.push_back("battery");
    }

    bool IsBinary() const {
        return type != SmartHomeSensorType::Temperature && type != SmartHomeSensorType::Humidity &&
               type != SmartHomeSensorType::Light && type != SmartHomeSensorType::Pressure &&
               type != SmartHomeSensorType::AirQuality;
    }
    bool IsMotionSensor() const override { return type == SmartHomeSensorType::Motion; }

    SmartHomeSensorType GetSensorType() const override { return type; }
    SmartHomeSensorReading GetReading() const override {
        std::lock_guard<std::mutex> l(m);
        SmartHomeSensorReading reading;
        reading.Type = type;
        reading.Value = static_cast<float>(IsBinary() ? (triggered ? 1.0 : 0.0) : Tenths(value));
        reading.Unit = unit;
        reading.Timestamp = lastSeen;
        reading.Triggered = triggered;
        return reading;
    }
    float GetValue() const override { return GetReading().Value; }
    std::string GetUnit() const override { return unit; }
    bool IsTriggered() const override { std::lock_guard<std::mutex> l(m); return triggered; }

    bool Advance(double seconds, std::mt19937& random) override {
        std::lock_guard<std::mutex> l(m);
        bool changed = DrainBattery(seconds, 0.02);
        if (IsBinary()) {
            // Motion clears itself once nobody has moved for a while; the
            // rest stay triggered until cleared, as a leak or a smoke
            // alarm must.
            if (triggered && type == SmartHomeSensorType::Motion) {
                holdLeft -= seconds;
                if (holdLeft <= 0) { triggered = false; changed = true; }
            }
            return changed;
        }
        const double shownBefore = Tenths(value);
        std::uniform_real_distribution<double> drift(-0.004, 0.004);
        // A random walk that keeps returning towards where it started.
        value += drift(random) * seconds + (centre - value) * 0.002 * seconds;
        if (Tenths(value) != shownBefore) { lastSeen = NowMs(); changed = true; }
        return changed;
    }

    bool Command(const std::string& command, const Params&) override {
        std::lock_guard<std::mutex> l(m);
        if (!IsBinary()) return false;
        if (command == "simulateTrigger") { triggered = true; holdLeft = 20.0; return true; }
        if (command == "simulateClear") { triggered = false; return true; }
        return false;
    }

    void FillState(Params& state) const override {
        std::lock_guard<std::mutex> l(m);
        if (IsBinary()) state["triggered"] = triggered ? "true" : "false";
        else state["value"] = FormatNumber(value);
        state["unit"] = unit;
        state["battery"] = FormatNumber(battery, 0);
    }
    std::vector<std::string> Commands() const override {
        if (IsBinary()) return {"simulateTrigger", "simulateClear"};
        return {};
    }

private:
    const SmartHomeSensorType type;
    std::string unit;
    double value;
    const double centre;
    bool triggered = false;
    double holdLeft = 0.0;
};

// ===== THE PROTOCOL =====

SimulatedProtocol::SimulatedProtocol(SimulatedHomeOptions opts)
    : SmartHomeProtocolBase(SmartHomeProtocolType::Simulated, "Simulated")
    , options(opts)
    , random(opts.seed) {
    protocolVersion = "0.1.0";
    SetCapability(ProtocolCapability::Discovery);
    SetCapability(ProtocolCapability::Pairing);
    SetCapability(ProtocolCapability::LocalControl);
    SetCapability(ProtocolCapability::Encryption);
    if (options.sampleHome) FurnishSampleHome();
    nextMotionEvent = 20.0;
}

SimulatedProtocol::~SimulatedProtocol() {
    Shutdown();
}

bool SimulatedProtocol::Initialize() {
    if (IsInitialized() && GetState() != ProtocolState::Shutdown) return true;
    SetState(ProtocolState::Initializing);
    FormNetwork("Demo home");
    if (options.realTime) {
        std::lock_guard<std::mutex> lock(clockMutex);
        clockRunning = true;
        clock = std::thread(&SimulatedProtocol::ClockThread, this);
    }
    SetState(ProtocolState::Ready);
    return true;
}

void SimulatedProtocol::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(clockMutex);
        clockRunning = false;
    }
    clockWake.notify_all();
    if (clock.joinable()) clock.join();
    discovering = false;
    pairing = false;
    if (IsInitialized()) SetState(ProtocolState::Shutdown);
}

std::vector<SmartHomeDeviceCategory> SimulatedProtocol::GetSupportedDeviceCategories() const {
    return {SmartHomeDeviceCategory::Light, SmartHomeDeviceCategory::Switch, SmartHomeDeviceCategory::Plug,
            SmartHomeDeviceCategory::Thermostat, SmartHomeDeviceCategory::Lock, SmartHomeDeviceCategory::Blind,
            SmartHomeDeviceCategory::Sensor};
}

std::string SimulatedProtocol::GetHardwareInfo() const {
    return "Simulated demo home - no radio";
}

// ===== NETWORK =====

bool SimulatedProtocol::FormNetwork(const std::string& networkName) {
    SmartHomeNetworkInfo info;
    info.NetworkId = "sim-demo-home";
    info.NetworkName = networkName.empty() ? "Demo home" : networkName;
    info.Protocol = SmartHomeProtocolType::Simulated;
    info.Active = true;
    info.Channel = "-";
    {
        std::lock_guard<std::mutex> lock(simMutex);
        info.DeviceCount = static_cast<int>(devices.size());
    }
    {
        std::lock_guard<std::mutex> lock(networkMutex);
        networkInfo = info;
        hasNetwork = true;
    }
    OnProtocolNetworkForm callback;
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        callback = onNetworkForm;
    }
    if (callback) callback(info);
    return true;
}

bool SimulatedProtocol::JoinNetwork(const std::string&) { return FormNetwork(""); }

bool SimulatedProtocol::LeaveNetwork() {
    ClearNetwork();
    return true;
}

void SimulatedProtocol::UpdateNetworkInfo() {
    int count;
    {
        std::lock_guard<std::mutex> lock(simMutex);
        count = static_cast<int>(devices.size());
    }
    std::lock_guard<std::mutex> lock(networkMutex);
    networkInfo.DeviceCount = count;
}

NetworkTopology SimulatedProtocol::GetTopology() const {
    // A star around the hub, with mains-powered devices as routers: the
    // shape a small Zigbee or Thread home settles into.
    NetworkTopology topology;
    topology.NetworkId = "sim-demo-home";
    NetworkNode hub;
    hub.NodeId = "hub";
    hub.IsCoordinator = true;
    hub.LinkQuality = 255;
    std::lock_guard<std::mutex> lock(simMutex);
    for (const auto& device : devices) {
        NetworkNode node;
        node.NodeId = device->id;
        node.DeviceId = device->id;
        node.ParentId = "hub";
        node.Depth = 1;
        node.LinkQuality = device->linkQuality;
        node.IsRouter = device->category != SmartHomeDeviceCategory::Sensor &&
                        device->category != SmartHomeDeviceCategory::Lock;
        hub.Neighbors.push_back(node.NodeId);
        (node.IsRouter ? topology.RouterCount : topology.EndDeviceCount)++;
        topology.Nodes.push_back(node);
    }
    topology.Nodes.insert(topology.Nodes.begin(), hub);
    topology.MaxDepth = devices.empty() ? 0 : 1;
    return topology;
}

// ===== DISCOVERY / PAIRING =====

bool SimulatedProtocol::StartDiscovery(int durationSeconds) {
    std::vector<SmartHomeDeviceInfo> found;
    {
        std::lock_guard<std::mutex> lock(simMutex);
        discoveryUntil = simulatedTime + std::max(1, durationSeconds);
        discovering = true;
        if (discoverable.empty()) {
            for (int i = 0; i < 2; ++i) discoverable.push_back(MakeCatalogueDevice(catalogueNext++));
        }
        for (const auto& device : discoverable) found.push_back(device->Info());
    }
    for (const auto& info : found) NotifyDiscover(info);
    return true;
}

void SimulatedProtocol::StopDiscovery() {
    discovering = false;
}

bool SimulatedProtocol::StartPairing(int timeoutSeconds) {
    std::lock_guard<std::mutex> lock(simMutex);
    pairingUntil = simulatedTime + std::max(1, timeoutSeconds);
    pairingFindsAt = simulatedTime + options.pairingSeconds;
    pairing = true;
    return true;
}

void SimulatedProtocol::StopPairing() {
    pairing = false;
}

// ===== DEVICES =====

void SimulatedProtocol::AddDevice(const std::shared_ptr<SimulatedDevice>& device) {
    if (device->serial.empty()) device->serial = "SIM" + std::to_string(100000 + nextSerial);
    if (device->id.empty()) {
        std::ostringstream id;
        id << "sim-" << nextSerial;
        device->id = id.str();
    }
    ++nextSerial;
    if (!device->room.empty() && std::find(rooms.begin(), rooms.end(), device->room) == rooms.end()) {
        rooms.push_back(device->room);
    }
    devices.push_back(device);
}

std::shared_ptr<SimulatedDevice> SimulatedProtocol::FindDevice(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock(simMutex);
    for (const auto& device : devices) {
        if (device->id == deviceId) return device;
    }
    return nullptr;
}

std::vector<std::shared_ptr<ISmartHomeDevice>> SimulatedProtocol::GetDevices() const {
    std::vector<std::shared_ptr<ISmartHomeDevice>> result;
    std::lock_guard<std::mutex> lock(simMutex);
    for (const auto& device : devices) {
        // Every simulated device is also its ISmartHomeDevice kind; the
        // cross-cast finds that side of the same object.
        result.push_back(std::dynamic_pointer_cast<ISmartHomeDevice>(device));
    }
    return result;
}

std::shared_ptr<ISmartHomeDevice> SimulatedProtocol::GetDevice(const std::string& deviceId) const {
    return std::dynamic_pointer_cast<ISmartHomeDevice>(FindDevice(deviceId));
}

bool SimulatedProtocol::RemoveDevice(const std::string& deviceId) {
    {
        std::lock_guard<std::mutex> lock(simMutex);
        auto it = std::find_if(devices.begin(), devices.end(),
                               [&](const auto& d) { return d->id == deviceId; });
        if (it == devices.end()) return false;
        devices.erase(it);
    }
    UpdateNetworkInfo();
    NotifyLeave(deviceId);
    return true;
}

bool SimulatedProtocol::InterviewDevice(const std::string& deviceId) {
    return FindDevice(deviceId) != nullptr;
}

std::vector<SmartHomeDeviceInfo> SimulatedProtocol::GetPairedDevices() {
    std::vector<SmartHomeDeviceInfo> result;
    std::lock_guard<std::mutex> lock(simMutex);
    for (const auto& device : devices) result.push_back(device->Info());
    return result;
}

bool SimulatedProtocol::PairDevice(const std::string& deviceId, const Params& params) {
    SmartHomeDeviceInfo info;
    {
        std::lock_guard<std::mutex> lock(simMutex);
        auto it = std::find_if(discoverable.begin(), discoverable.end(),
                               [&](const auto& d) { return d->id == deviceId; });
        if (it == discoverable.end()) return false;
        std::shared_ptr<SimulatedDevice> device = *it;
        discoverable.erase(it);
        auto room = params.find("room");
        if (room != params.end()) device->room = room->second;
        auto name = params.find("name");
        if (name != params.end()) device->name = name->second;
        devices.push_back(device);
        if (!device->room.empty() && std::find(rooms.begin(), rooms.end(), device->room) == rooms.end()) {
            rooms.push_back(device->room);
        }
        info = device->Info();
    }
    UpdateNetworkInfo();
    NotifyJoin(info);
    return true;
}

bool SimulatedProtocol::UnpairDevice(const std::string& deviceId) {
    return RemoveDevice(deviceId);
}

bool SimulatedProtocol::GetDeviceState(const std::string& deviceId, Params& state) {
    auto device = FindDevice(deviceId);
    if (!device) return false;
    device->FillState(state);
    state["reachable"] = device->IsReachableNow() ? "true" : "false";
    return true;
}

bool SimulatedProtocol::SendCommand(const std::string& deviceId, const std::string& command,
                                    const Params& params) {
    auto device = FindDevice(deviceId);
    if (!device || !device->Handle(command, params)) return false;
    NotifyUpdate(deviceId);
    return true;
}

// ===== CALLBACKS =====

void SimulatedProtocol::SetOnStateChange(OnProtocolStateChange callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    onStateChange = std::move(callback);
}
void SimulatedProtocol::SetOnDeviceDiscover(OnProtocolDeviceDiscover callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    onDeviceDiscover = std::move(callback);
}
void SimulatedProtocol::SetOnDeviceJoin(OnProtocolDeviceJoin callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    onDeviceJoin = std::move(callback);
}
void SimulatedProtocol::SetOnDeviceLeave(OnProtocolDeviceLeave callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    onDeviceLeave = std::move(callback);
}
void SimulatedProtocol::SetOnDeviceUpdate(OnProtocolDeviceUpdate callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    onDeviceUpdate = std::move(callback);
}
void SimulatedProtocol::SetOnNetworkForm(OnProtocolNetworkForm callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    onNetworkForm = std::move(callback);
}
void SimulatedProtocol::SetOnError(OnProtocolError callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    onError = std::move(callback);
}

void SimulatedProtocol::NotifyUpdate(const std::string& deviceId) {
    OnProtocolDeviceUpdate callback;
    { std::lock_guard<std::mutex> lock(callbackMutex); callback = onDeviceUpdate; }
    if (callback) callback(deviceId);
}

void SimulatedProtocol::NotifyJoin(const SmartHomeDeviceInfo& info) {
    OnProtocolDeviceJoin callback;
    { std::lock_guard<std::mutex> lock(callbackMutex); callback = onDeviceJoin; }
    if (callback) callback(info);
}

void SimulatedProtocol::NotifyLeave(const std::string& deviceId) {
    OnProtocolDeviceLeave callback;
    { std::lock_guard<std::mutex> lock(callbackMutex); callback = onDeviceLeave; }
    if (callback) callback(deviceId);
}

void SimulatedProtocol::NotifyDiscover(const SmartHomeDeviceInfo& info) {
    {
        std::lock_guard<std::mutex> lock(deviceMutex);
        discoveredDevices.push_back(info);
    }
    OnProtocolDeviceDiscover callback;
    { std::lock_guard<std::mutex> lock(callbackMutex); callback = onDeviceDiscover; }
    if (callback) callback(info);
}

std::map<std::string, std::string> SimulatedProtocol::GetDiagnostics() const {
    auto diag = SmartHomeProtocolBase::GetDiagnostics();
    std::lock_guard<std::mutex> lock(simMutex);
    diag["deviceCount"] = std::to_string(devices.size());
    diag["simulatedSeconds"] = FormatNumber(simulatedTime, 0);
    diag["realTime"] = options.realTime ? "true" : "false";
    return diag;
}

// ===== SIMULATION =====

void SimulatedProtocol::Advance(double seconds) {
    if (seconds <= 0) return;
    std::vector<std::shared_ptr<SimulatedDevice>> snapshot;
    std::shared_ptr<SimulatedDevice> joined;
    std::string motionId;
    {
        std::lock_guard<std::mutex> lock(simMutex);
        simulatedTime += seconds;
        snapshot = devices;

        if (pairing && simulatedTime >= pairingFindsAt) {
            // One device per pairing window, as a real permit-join usually
            // finds: the one the person is holding.
            joined = MakeCatalogueDevice(catalogueNext++);
            AddDevice(joined);
            pairing = false;
        } else if (pairing && simulatedTime >= pairingUntil) {
            pairing = false;
        }
        if (discovering && simulatedTime >= discoveryUntil) discovering = false;

        if (options.randomEvents && simulatedTime >= nextMotionEvent) {
            std::vector<std::shared_ptr<SimulatedDevice>> motion;
            for (const auto& d : devices) if (d->IsMotionSensor()) motion.push_back(d);
            if (!motion.empty()) {
                std::uniform_int_distribution<size_t> pick(0, motion.size() - 1);
                motionId = motion[pick(random)]->id;
            }
            std::uniform_real_distribution<double> gap(30.0, 90.0);
            nextMotionEvent = simulatedTime + gap(random);
        }
    }

    std::vector<std::string> changed;
    for (const auto& device : snapshot) {
        bool moved;
        {
            // The generator is shared and not thread-safe; the devices'
            // own locks do not cover it.
            std::lock_guard<std::mutex> lock(simMutex);
            moved = device->Advance(seconds, random);
        }
        if (moved) changed.push_back(device->id);
    }
    if (!motionId.empty()) {
        if (auto device = FindDevice(motionId); device && device->Handle("simulateTrigger", {})) {
            changed.push_back(motionId);
        }
    }

    if (joined) {
        UpdateNetworkInfo();
        NotifyJoin(joined->Info());
    }
    for (const std::string& id : changed) NotifyUpdate(id);
}

double SimulatedProtocol::GetSimulatedTime() const {
    std::lock_guard<std::mutex> lock(simMutex);
    return simulatedTime;
}

std::vector<std::string> SimulatedProtocol::GetRooms() const {
    std::lock_guard<std::mutex> lock(simMutex);
    return rooms;
}

void SimulatedProtocol::ClockThread() {
    auto last = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(clockMutex);
    while (clockRunning) {
        clockWake.wait_for(lock, std::chrono::milliseconds(static_cast<int>(kClockStepSeconds * 1000)),
                           [this] { return !clockRunning; });
        if (!clockRunning) break;
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - last).count();
        last = now;
        lock.unlock();
        Advance(elapsed);
        lock.lock();
    }
}

// ===== THE DEMO HOME =====

namespace {

template <typename T, typename... Args>
std::shared_ptr<T> Make(const std::string& name, const std::string& room, const std::string& model,
                        Args&&... args) {
    auto device = std::make_shared<T>(std::forward<Args>(args)...);
    device->name = name;
    device->room = room;
    device->model = model;
    return device;
}

} // namespace

void SimulatedProtocol::FurnishSampleHome() {
    // Constructed before anyone can see the protocol, so no lock is needed;
    // taken anyway, so AddDevice's precondition holds the same everywhere.
    std::lock_guard<std::mutex> lock(simMutex);
    using S = SmartHomeSensorType;

    // ----- Living room
    auto ceiling = Make<SimulatedLight>("Ceiling light", "Living room", "SIM-L200 tunable white", true, true, false);
    ceiling->Preset(true, 180, 2900);
    AddDevice(ceiling);
    auto floorLamp = Make<SimulatedLight>("Floor lamp", "Living room", "SIM-L300 colour", true, true, true);
    floorLamp->Preset(true, 120, 2400);
    AddDevice(floorLamp);
    auto tv = Make<SimulatedPlug>("TV", "Living room", "SIM-P100 metered plug", SimulatedLoad::Steady, 88.0);
    tv->Preset(true, 41.6);
    AddDevice(tv);
    auto livingBlinds = Make<SimulatedBlind>("Patio blinds", "Living room", "SIM-B100 roller");
    livingBlinds->Preset(70);
    AddDevice(livingBlinds);
    auto livingHeat = Make<SimulatedThermostat>("Living room heating", "Living room", "SIM-T100 radiator valve");
    livingHeat->Preset(20.6, 21.5);
    AddDevice(livingHeat);
    AddDevice(Make<SimulatedSensor>("Living room motion", "Living room", "SIM-S300 motion", S::Motion, 0.0));
    AddDevice(Make<SimulatedSensor>("Patio door", "Living room", "SIM-S400 contact", S::Contact, 0.0));

    // ----- Kitchen
    auto spots = Make<SimulatedLight>("Kitchen spots", "Kitchen", "SIM-L100 dimmable", true, false, false);
    spots->Preset(false, 220);
    AddDevice(spots);
    auto kettle = Make<SimulatedPlug>("Kettle", "Kitchen", "SIM-P100 metered plug", SimulatedLoad::Kettle, 2200.0);
    kettle->Preset(false, 12.4);
    AddDevice(kettle);
    auto fridge = Make<SimulatedPlug>("Fridge", "Kitchen", "SIM-P100 metered plug", SimulatedLoad::Fridge, 95.0);
    fridge->Preset(true, 186.2);
    AddDevice(fridge);
    AddDevice(Make<SimulatedSensor>("Under-sink leak sensor", "Kitchen", "SIM-S500 water", S::Water, 0.0));
    AddDevice(Make<SimulatedSensor>("Kitchen smoke alarm", "Kitchen", "SIM-S600 smoke", S::Smoke, 0.0));

    // ----- Bedroom
    auto bedside = Make<SimulatedLight>("Bedside lamp", "Bedroom", "SIM-L200 tunable white", true, true, false);
    bedside->Preset(false, 90, 2200);
    AddDevice(bedside);
    auto bedBlinds = Make<SimulatedBlind>("Bedroom blinds", "Bedroom", "SIM-B100 roller");
    bedBlinds->Preset(100);
    AddDevice(bedBlinds);
    auto bedHeat = Make<SimulatedThermostat>("Bedroom heating", "Bedroom", "SIM-T100 radiator valve");
    bedHeat->Preset(18.4, 18.0);
    AddDevice(bedHeat);
    AddDevice(Make<SimulatedSensor>("Bedroom humidity", "Bedroom", "SIM-S200 humidity", S::Humidity, 51.0));

    // ----- Kids' room
    auto nightLight = Make<SimulatedLight>("Night light", "Kids' room", "SIM-L300 colour", true, false, true);
    nightLight->Preset(false, 40);
    AddDevice(nightLight);
    AddDevice(Make<SimulatedSensor>("Kids' room temperature", "Kids' room", "SIM-S100 temperature", S::Temperature, 20.3));

    // ----- Bathroom
    auto mirror = Make<SimulatedLight>("Mirror light", "Bathroom", "SIM-L100 dimmable", true, false, false);
    mirror->Preset(false, 255);
    AddDevice(mirror);
    auto towelRail = Make<SimulatedPlug>("Towel rail", "Bathroom", "SIM-P100 metered plug", SimulatedLoad::Steady, 300.0);
    towelRail->Preset(false, 22.9);
    AddDevice(towelRail);
    AddDevice(Make<SimulatedSensor>("Bathroom humidity", "Bathroom", "SIM-S200 humidity", S::Humidity, 58.0));

    // ----- Hallway
    auto frontDoor = Make<SimulatedLock>("Front door", "Hallway", "SIM-K100 lock");
    frontDoor->Preset(true, 84.0);
    AddDevice(frontDoor);
    auto hallLight = Make<SimulatedLight>("Hall light", "Hallway", "SIM-L050 on/off", false, false, false);
    hallLight->Preset(false, 255);
    AddDevice(hallLight);
    AddDevice(Make<SimulatedSensor>("Hallway motion", "Hallway", "SIM-S300 motion", S::Motion, 0.0));

    // ----- Office
    auto desk = Make<SimulatedLight>("Desk lamp", "Office", "SIM-L200 tunable white", true, true, false);
    desk->Preset(true, 230, 4800);
    AddDevice(desk);
    auto workstation = Make<SimulatedPlug>("Workstation", "Office", "SIM-P100 metered plug", SimulatedLoad::Steady, 142.0);
    workstation->Preset(true, 97.3);
    AddDevice(workstation);
    auto officeHeat = Make<SimulatedThermostat>("Office heating", "Office", "SIM-T100 radiator valve");
    officeHeat->Preset(19.2, 20.5);
    AddDevice(officeHeat);

    // ----- Garden
    auto pathLights = Make<SimulatedPlug>("Path lights", "Garden", "SIM-W100 relay", SimulatedLoad::Unmetered, 0.0);
    pathLights->Preset(false);
    AddDevice(pathLights);
    auto backGate = Make<SimulatedLock>("Garden gate", "Garden", "SIM-K100 lock");
    backGate->Preset(true, 23.0);
    AddDevice(backGate);
    AddDevice(Make<SimulatedSensor>("Outdoor temperature", "Garden", "SIM-S100 temperature", S::Temperature, 9.4));

    // Link quality falls off with distance from the hub in the hallway, so
    // the network view has something to show.
    for (const auto& device : devices) {
        if (device->room == "Garden") device->linkQuality = 92;
        else if (device->room == "Bedroom" || device->room == "Kids' room") device->linkQuality = 150;
        else device->linkQuality = 210;
    }
}

std::shared_ptr<SimulatedDevice> SimulatedProtocol::MakeCatalogueDevice(int index) {
    // What pairing finds next, in turn. Not in a room yet: placing it is the
    // last step of adding a device.
    std::shared_ptr<SimulatedDevice> device;
    switch (index % 4) {
        case 0: {
            auto bulb = Make<SimulatedLight>("Colour bulb", "", "SIM-L300 colour", true, true, true);
            bulb->Preset(true, 200, 2700);
            device = bulb;
            break;
        }
        case 1: {
            auto plug = Make<SimulatedPlug>("Smart plug", "", "SIM-P100 metered plug", SimulatedLoad::Steady, 6.0);
            plug->Preset(true);
            device = plug;
            break;
        }
        case 2: device = Make<SimulatedSensor>("Motion sensor", "", "SIM-S300 motion", SmartHomeSensorType::Motion, 0.0); break;
        default: device = Make<SimulatedSensor>("Door sensor", "", "SIM-S400 contact", SmartHomeSensorType::Contact, 0.0); break;
    }
    device->serial = "SIM" + std::to_string(100000 + nextSerial);
    device->id = "sim-" + std::to_string(nextSerial);
    ++nextSerial;
    return device;
}

std::shared_ptr<ISmartHomeProtocol> CreateSimulatedProtocol() {
    return std::make_shared<SimulatedProtocol>();
}

} // namespace SmartHome
} // namespace UltraCanvas
