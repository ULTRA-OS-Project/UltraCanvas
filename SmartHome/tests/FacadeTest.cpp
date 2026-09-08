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
#include <cassert>
#include <cstdio>

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
    std::vector<SmartHomeDeviceInfo> GetPairedDevices() override { return {}; }
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
    if (!SMARTHOME_API.RegisterProtocol(SmartHomeProtocolType::Zigbee,
                                        std::make_shared<FakeProtocol>())) {
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
