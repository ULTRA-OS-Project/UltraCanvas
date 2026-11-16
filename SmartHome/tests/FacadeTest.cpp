// SmartHome/tests/FacadeTest.cpp
// Proves the public facade is sufficient on its own: an application that
// includes only UltraCanvasSmartHome.h can start the module, supply a protocol
// backend and enable it, without ever reaching into core/.
//
// That was impossible before: SmartHomeAPI had no way to register a backend,
// which is why the call is commented out in examples/DevicePairing.cpp with
// the note "In real code, this would be done through SmartHomeManager".
//
// Build (no protocol backends needed — the stub below stands in for one):
//   g++ -std=gnu++20 -I SmartHome/include -I SmartHome/core \
//       SmartHome/tests/FacadeTest.cpp SmartHome/core/*.cpp -lpthread
//
// Author: UltraCanvas Framework
#include "UltraCanvasSmartHome.h"
#include "ISmartHomeProtocol.h"
#include "SmartHomeProtocolBase.h"
#include "UltraCanvasDebug.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace UltraCanvas::SmartHome;

// A stand-in backend: no hardware, just enough to be registered and enabled.
class FakeProtocol : public SmartHomeProtocolBase {
public:
    FakeProtocol() : SmartHomeProtocolBase(SmartHomeProtocolType::Zigbee, "Fake") {}
    bool Initialize() override { SetState(ProtocolState::Ready); return true; }
    void Shutdown() override {}
    std::vector<SmartHomeDeviceCategory> GetSupportedDeviceCategories() const override {
        return {SmartHomeDeviceCategory::Light};
    }
    bool IsHardwareAvailable() const override { return true; }
    std::string GetHardwareInfo() const override { return "fake"; }
    std::vector<std::string> GetAvailableAdapters() const override { return {"fake0"}; }
    bool SelectAdapter(const std::string&) override { return true; }
    bool FormNetwork(const std::string&) override { return true; }
    bool JoinNetwork(const std::string&) override { return true; }
    bool LeaveNetwork() override { return true; }
    NetworkTopology GetTopology() const override { return {}; }
    bool StartDiscovery(int) override { return true; }
    void StopDiscovery() override {}
    bool StartPairing(int) override { return true; }
    void StopPairing() override {}
    std::vector<std::shared_ptr<ISmartHomeDevice>> GetDevices() const override { return {}; }
    std::shared_ptr<ISmartHomeDevice> GetDevice(const std::string&) const override { return nullptr; }
    bool RemoveDevice(const std::string&) override { return true; }
    bool InterviewDevice(const std::string&) override { return true; }
    std::vector<SmartHomeDeviceInfo> GetPairedDevices() override {
        std::lock_guard<std::mutex> lock(deviceMutex);
        std::vector<SmartHomeDeviceInfo> devices;
        for (const auto& [id, info] : pairedDevices) devices.push_back(info);
        return devices;
    }
    // What a backend does when a device joins and when what it knows of one
    // changes - with no device object behind it, as Matter and Zigbee keep.
    void Join(const SmartHomeDeviceInfo& info) { AddPairedDevice(info); }
    void Update(const SmartHomeDeviceInfo& info) { UpdatePairedDevice(info); }
    void Fail(int code, const std::string& message) { ReportError(code, message); }
    void Say(int level, const std::string& message) { Log(level, message); }
    bool PairDevice(const std::string&, const std::map<std::string,std::string>&) override { return true; }
    bool UnpairDevice(const std::string&) override { return true; }
    bool GetDeviceState(const std::string&, std::map<std::string,std::string>&) override { return true; }
    bool SendCommand(const std::string&, const std::string&,
                     const std::map<std::string,std::string>&) override { return true; }
    bool LoadConfig(const std::string&) override { return true; }
    bool SaveConfig(const std::string&) override { return true; }
    SmartHomeSecurityLevel GetSecurityLevel() const override {
        return SmartHomeSecurityLevel::Encrypted;
    }
};

int main() {
    // 1. Start the module through the facade only.
    if (!SMARTHOME_API.Initialize()) { std::puts("FAIL: Initialize"); return 1; }
    std::puts("ok  Initialize()");

    // 2. No backend compiled in, so Zigbee starts unbacked.
    if (SMARTHOME_API.HasProtocolBackend(SmartHomeProtocolType::Zigbee)) {
        std::puts("FAIL: expected no Zigbee backend"); return 1;
    }
    std::puts("ok  HasProtocolBackend(Zigbee) == false");

    // 3. Enabling an unbacked protocol must fail rather than pretend.
    if (SMARTHOME_API.EnableProtocol(SmartHomeProtocolType::Zigbee)) {
        std::puts("FAIL: enabled a protocol with no backend"); return 1;
    }
    std::puts("ok  EnableProtocol without a backend fails");

    // 4. Supply our own backend through the public API. This is the call that
    //    was impossible before, and commented out in DevicePairing.cpp.
    auto fake = std::make_shared<FakeProtocol>();
    if (!SMARTHOME_API.RegisterProtocol(SmartHomeProtocolType::Zigbee, fake)) {
        std::puts("FAIL: RegisterProtocol"); return 1;
    }
    std::puts("ok  RegisterProtocol()");

    if (!SMARTHOME_API.HasProtocolBackend(SmartHomeProtocolType::Zigbee)) {
        std::puts("FAIL: backend not visible"); return 1;
    }
    std::puts("ok  HasProtocolBackend(Zigbee) == true");

    // 5. And now it enables.
    if (!SMARTHOME_API.EnableProtocol(SmartHomeProtocolType::Zigbee)) {
        std::puts("FAIL: EnableProtocol"); return 1;
    }
    std::puts("ok  EnableProtocol()");

    // 5b. A backend with no device objects reports a change: the registry
    //     must take the backend's new info, not keep what it had at the join
    //     (a Matter device that answers after a restart, say).
    {
        SmartHomeDeviceInfo lamp;
        lamp.DeviceId = "fake_lamp";
        lamp.Name = "Fake lamp";
        lamp.Protocol = SmartHomeProtocolType::Zigbee;
        lamp.Category = SmartHomeDeviceCategory::Light;
        lamp.State = SmartHomeDeviceState::Offline;
        lamp.Reachable = false;
        fake->Join(lamp);
        if (SMARTHOME_API.GetDevice("fake_lamp").DeviceId != "fake_lamp" ||
            SMARTHOME_API.GetDevice("fake_lamp").Reachable) {
            std::puts("FAIL: the joined device is not registered as it joined"); return 1;
        }
        lamp.State = SmartHomeDeviceState::Online;
        lamp.Reachable = true;
        fake->Update(lamp);
        const SmartHomeDeviceInfo now = SMARTHOME_API.GetDevice("fake_lamp");
        if (!now.Reachable || now.State != SmartHomeDeviceState::Online) {
            std::puts("FAIL: an update from a backend without device objects was not read back"); return 1;
        }
        std::puts("ok  a backend without device objects updates the registry");
    }

    // 5c. What a backend logs reaches the framework's diagnostic stream:
    //     errors always, the rest up to the protocol's level (default info).
    {
        const std::filesystem::path log =
            std::filesystem::temp_directory_path() / "uc-smarthome-facade-test.log";
        std::filesystem::remove(log);
        UltraCanvas::SetDebugOutputFile(UltraCanvas::PathToUtf8(log));
        fake->Fail(-7, "the stick was unplugged");
        fake->Say(1, "a device stopped answering");
        fake->Say(3, "a debug detail");   // above the default level
        UltraCanvas::SetDebugOutputFile("");
        UltraCanvas::SetDebugOutputEnabled(false);
        std::ifstream in(log);
        std::stringstream text;
        text << in.rdbuf();
        const std::string written = text.str();
        if (written.find("SmartHome[Fake] error: the stick was unplugged (code -7)") == std::string::npos ||
            written.find("SmartHome[Fake] warning: a device stopped answering") == std::string::npos) {
            std::printf("FAIL: the backend's log did not reach debugOutput:\n%s", written.c_str()); return 1;
        }
        if (written.find("a debug detail") != std::string::npos) {
            std::puts("FAIL: a debug line was written above the protocol's log level"); return 1;
        }
        std::filesystem::remove(log);
        std::puts("ok  a backend's errors and warnings reach debugOutput");
    }

    // 6. The factory path: enable a type with no backend, via a factory.
    RegisterProtocolFactory(SmartHomeProtocolType::Thread,
                            []{ return std::make_shared<FakeProtocol>(); });
    if (!SMARTHOME_API.EnableProtocol(SmartHomeProtocolType::Thread)) {
        std::puts("FAIL: factory path"); return 1;
    }
    std::puts("ok  EnableProtocol() built a backend from its factory");

    SMARTHOME_API.Shutdown();
    std::puts("ok  Shutdown()");
    std::puts("\nPASS - the facade is sufficient on its own");
    return 0;
}
