// Tests/UltraNet/v1only/V1OnlyPlugin.cpp
// A plug-in DSO in the retired v1 shape: it exports only
// UltraNet_PluginRegister and registers itself through the host's
// UltraNet_RegisterPlugin symbol. The loader must refuse it - the test sees
// its "v1only" scheme never appear (test_plugins.cpp). POSIX only: v1 resolves
// the host symbol at load time, which is what Windows cannot do.
#include <UltraNet/UltraNetPlugins.h>

#include <memory>
#include <string>
#include <vector>

namespace {

class V1OnlyPlugin : public IUltraNetPlugin {
public:
    std::string GetName() const override { return "V1OnlyTestPlugin"; }
    std::string GetVersion() const override { return "0.0.1"; }
    std::vector<std::string> GetSupportedSchemes() const override { return {"v1only"}; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {}
};

} // namespace

extern "C" __attribute__((visibility("default"))) void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<V1OnlyPlugin>());
}
