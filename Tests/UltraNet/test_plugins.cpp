// Tests/UltraNet/test_plugins.cpp
// Plugin registry semantics: register/lookup-by-scheme/unregister,
// double-register idempotency, supported-schemes union with core.
#include "test_framework.h"

#include <UltraNet/UltraNetPlugins.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

class FakePlugin : public IUltraNetPlugin {
public:
    explicit FakePlugin(std::string name, std::vector<std::string> schemes)
        : name_(std::move(name)), schemes_(std::move(schemes)) {}
    std::string GetName() const override { return name_; }
    std::string GetVersion() const override { return "0.0.1"; }
    std::vector<std::string> GetSupportedSchemes() const override { return schemes_; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override { shutdownCalled_ = true; }
    bool shutdownCalled_ = false;
private:
    std::string name_;
    std::vector<std::string> schemes_;
};

bool Contains(const std::vector<std::string>& v, const std::string& needle) {
    return std::find(v.begin(), v.end(), needle) != v.end();
}

} // namespace

TEST(plugin_register_then_lookup_by_scheme) {
    auto p = std::make_shared<FakePlugin>("test-fake", std::vector<std::string>{"fake"});
    UltraNet_RegisterPlugin(p);
    auto found = UltraNet_GetPlugin("fake");
    REQUIRE(found != nullptr);
    REQUIRE_EQ(found->GetName(), std::string{"test-fake"});
    UltraNet_UnregisterPlugin("test-fake");
}

TEST(plugin_lookup_is_case_insensitive) {
    auto p = std::make_shared<FakePlugin>("test-fake-ci", std::vector<std::string>{"FAKE-ci"});
    UltraNet_RegisterPlugin(p);
    auto a = UltraNet_GetPlugin("fake-ci");
    auto b = UltraNet_GetPlugin("FAKE-CI");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    UltraNet_UnregisterPlugin("test-fake-ci");
}

TEST(plugin_unregister_drops_scheme_lookup) {
    auto p = std::make_shared<FakePlugin>("test-fake-u", std::vector<std::string>{"fake-u"});
    UltraNet_RegisterPlugin(p);
    UltraNet_UnregisterPlugin("test-fake-u");
    auto found = UltraNet_GetPlugin("fake-u");
    CHECK(found == nullptr);
}

TEST(plugin_unregister_invokes_shutdown) {
    auto p = std::make_shared<FakePlugin>("test-fake-s", std::vector<std::string>{"fake-s"});
    UltraNet_RegisterPlugin(p);
    UltraNet_UnregisterPlugin("test-fake-s");
    CHECK(p->shutdownCalled_);
}

TEST(plugin_supported_schemes_unions_core_and_plugin) {
    auto p = std::make_shared<FakePlugin>("test-fake-sc",
                                          std::vector<std::string>{"fake-sc"});
    UltraNet_RegisterPlugin(p);
    auto schemes = UltraNet_GetSupportedSchemes();
    CHECK(Contains(schemes, "https"));     // core
    CHECK(Contains(schemes, "ws"));        // core
    CHECK(Contains(schemes, "fake-sc"));   // plugin
    UltraNet_UnregisterPlugin("test-fake-sc");
}

TEST(plugin_double_register_replaces_first) {
    auto p1 = std::make_shared<FakePlugin>("test-fake-d", std::vector<std::string>{"fake-d"});
    auto p2 = std::make_shared<FakePlugin>("test-fake-d", std::vector<std::string>{"fake-d"});
    UltraNet_RegisterPlugin(p1);
    UltraNet_RegisterPlugin(p2);

    int count = 0;
    for (const auto& p : UltraNet_GetAllPlugins()) {
        if (p->GetName() == "test-fake-d") ++count;
    }
    REQUIRE_EQ(count, 1);
    UltraNet_UnregisterPlugin("test-fake-d");
}

TEST(plugin_refresh_with_empty_dir_is_noop) {
    UltraNet_SetPluginDirectory("/nonexistent-path-for-refresh-test");
    UltraNet_RefreshPlugins();
    // Should not crash; no plugin loaded from a missing directory.
    auto schemes = UltraNet_GetSupportedSchemes();
    CHECK(!schemes.empty());   // core schemes still present
}

TEST(plugin_directory_get_set_roundtrip) {
    const std::string p = "/tmp/some-plugin-dir";
    UltraNet_SetPluginDirectory(p);
    REQUIRE_EQ(UltraNet_GetPluginDirectory(), p);
}

TEST(plugin_host_vtable_abi_version_is_set) {
    // The host vtable handed to v2 plug-in entry points carries the ABI
    // version constant. If anything ever bumps the struct, the plug-in's
    // version check will refuse mismatched hosts.
    REQUIRE_EQ(ULTRANET_PLUGIN_HOST_ABI_VERSION, 1);

    UltraNetPluginHost vtable{};
    vtable.abiVersion    = ULTRANET_PLUGIN_HOST_ABI_VERSION;
    vtable.RegisterPlugin = &UltraNet_RegisterPlugin;
    REQUIRE(vtable.RegisterPlugin != nullptr);
    REQUIRE_EQ(vtable.abiVersion, 1);
}
