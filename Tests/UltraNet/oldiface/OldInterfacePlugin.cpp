// Tests/UltraNet/oldiface/OldInterfacePlugin.cpp
// A plug-in DSO as built before plug-in interface versioning: it has the
// current entry point, UltraNet_PluginInit, but does not compile the host shim
// and so exports no UltraNet_PluginInterfaceVersion. Its vtables could lack
// methods the host calls, so the loader must refuse it without initialising
// it - the test sees its "oldiface" scheme never appear, and the library
// listed by UltraNet_GetRefusedPlugins (test_plugins.cpp).
#include <UltraNet/UltraNetPlugins.h>

#include <memory>
#include <string>
#include <vector>

namespace {

class OldInterfacePlugin : public IUltraNetPlugin {
public:
    std::string GetName() const override { return "OldInterfaceTestPlugin"; }
    std::string GetVersion() const override { return "0.0.1"; }
    std::vector<std::string> GetSupportedSchemes() const override { return {"oldiface"}; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {}
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (host && host->RegisterPlugin) host->RegisterPlugin(std::make_shared<OldInterfacePlugin>());
}
