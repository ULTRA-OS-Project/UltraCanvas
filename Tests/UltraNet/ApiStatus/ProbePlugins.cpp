// Tests/UltraNet/ApiStatus/ProbePlugins.cpp
// Probes for UltraNet/UltraNetPlugins.h — the protocol-plug-in registry.
//
// In-process registration is checked with a throwaway plug-in defined here.
// DSO loading is checked against the plug-in directory this build produced
// (path injected by CMake as ULTRANET_PROBE_PLUGIN_DIR_DEFINE); when no
// plug-ins were built, that entry reports IMPLEMENTED rather than WORKING.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ApiStatus.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace ultranet_apistatus;

namespace {

constexpr const char* kArea = "Plugins";

// A minimal in-process plug-in. Records whether Shutdown() was called so the
// unregister probe can prove the registry drives the plug-in lifecycle.
class ProbePlugin : public IUltraNetPlugin {
public:
    explicit ProbePlugin(std::string name, std::vector<std::string> schemes)
        : name_(std::move(name)), schemes_(std::move(schemes)) {}

    std::string GetName() const override { return name_; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override { return schemes_; }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        initialized = true;
        return UltraNetResult::Ok();
    }
    void Shutdown() override { shutDown = true; }

    bool initialized = false;
    bool shutDown = false;

private:
    std::string              name_;
    std::vector<std::string> schemes_;
};

bool Contains(const std::vector<std::string>& v, const std::string& needle) {
    return std::find(v.begin(), v.end(), needle) != v.end();
}

std::size_t PluginCount() { return UltraNet_GetAllPlugins().size(); }

// Directory this build dropped its plug-in DSOs into, if any.
std::string BuiltPluginDir() {
    const std::string override = EnvOverride("ULTRANET_PROBE_PLUGIN_DIR");
    if (!override.empty()) return override;
#ifdef ULTRANET_PROBE_PLUGIN_DIR_DEFINE
    return ULTRANET_PROBE_PLUGIN_DIR_DEFINE;
#else
    return {};
#endif
}

bool DirectoryHasPlugins(const std::string& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    auto it = std::filesystem::directory_iterator(dir, ec);
    if (ec) return false;
    for (const auto& entry : it) {
        const std::string ext = entry.path().extension().string();
        if (ext == ".so" || ext == ".dylib" || ext == ".dll") return true;
    }
    return false;
}

} // namespace

ULTRANET_PROBE(kArea, UltraNet_RegisterPlugin) {
    const std::size_t before = PluginCount();

    auto plugin = std::make_shared<ProbePlugin>(
        "ApiStatusProbePlugin", std::vector<std::string>{"probe", "PROBE-ALT"});
    UltraNet_RegisterPlugin(plugin);

    const bool countGrew = PluginCount() == before + 1;
    const bool byScheme  = UltraNet_GetPlugin("probe") == plugin;
    // Scheme lookup is case-folded on registration.
    const bool byAltScheme = UltraNet_GetPlugin("probe-alt") == plugin;
    const bool inSchemes = Contains(UltraNet_GetSupportedSchemes(), "probe");

    // A null plug-in and one with an empty name must be ignored, not crash.
    UltraNet_RegisterPlugin(nullptr);
    UltraNet_RegisterPlugin(std::make_shared<ProbePlugin>(
        "", std::vector<std::string>{"nameless"}));
    const bool namelessRejected = UltraNet_GetPlugin("nameless") == nullptr;

    UltraNet_UnregisterPlugin("ApiStatusProbePlugin");

    PROBE_EXPECT(countGrew);
    PROBE_EXPECT(byScheme);
    PROBE_EXPECT_MSG(byAltScheme, "scheme names are not case-folded on registration");
    PROBE_EXPECT(inSchemes);
    PROBE_EXPECT(namelessRejected);
    return Working("plug-in indexed by name and by every scheme it claims "
                   "(case-folded); null and unnamed plug-ins ignored");
}

ULTRANET_PROBE(kArea, UltraNet_UnregisterPlugin) {
    auto plugin = std::make_shared<ProbePlugin>(
        "ApiStatusProbePlugin", std::vector<std::string>{"probe"});
    UltraNet_RegisterPlugin(plugin);
    PROBE_EXPECT(UltraNet_GetPlugin("probe") == plugin);

    const std::size_t before = PluginCount();
    UltraNet_UnregisterPlugin("ApiStatusProbePlugin");

    PROBE_EXPECT(UltraNet_GetPlugin("probe") == nullptr);
    PROBE_EXPECT(PluginCount() == before - 1);
    PROBE_EXPECT(!Contains(UltraNet_GetSupportedSchemes(), "probe"));
    PROBE_EXPECT_MSG(plugin->shutDown,
                     "Shutdown() was not called on the unregistered plug-in");

    // Unregistering something that is not there must be a no-op.
    UltraNet_UnregisterPlugin("ApiStatusProbePlugin");
    UltraNet_UnregisterPlugin("never-registered");
    return Working("plug-in removed from both indexes, Shutdown() called, and "
                   "repeat/unknown removals are safe no-ops");
}

ULTRANET_PROBE(kArea, UltraNet_GetPlugin) {
    PROBE_EXPECT(UltraNet_GetPlugin("no-such-scheme-anywhere") == nullptr);
    PROBE_EXPECT(UltraNet_GetPlugin("") == nullptr);

    auto plugin = std::make_shared<ProbePlugin>(
        "ApiStatusProbePlugin", std::vector<std::string>{"probe"});
    UltraNet_RegisterPlugin(plugin);

    const bool exact = UltraNet_GetPlugin("probe") == plugin;
    const bool folded = UltraNet_GetPlugin("PROBE") == plugin;
    UltraNet_UnregisterPlugin("ApiStatusProbePlugin");

    PROBE_EXPECT(exact);
    PROBE_EXPECT_MSG(folded, "scheme lookup is not case-insensitive");
    return Working("case-insensitive scheme lookup; unknown and empty schemes "
                   "return nullptr");
}

ULTRANET_PROBE(kArea, UltraNet_GetAllPlugins) {
    const std::size_t before = PluginCount();

    auto a = std::make_shared<ProbePlugin>("ProbeA", std::vector<std::string>{"probe-a"});
    auto b = std::make_shared<ProbePlugin>("ProbeB", std::vector<std::string>{"probe-b"});
    UltraNet_RegisterPlugin(a);
    UltraNet_RegisterPlugin(b);

    const std::vector<std::shared_ptr<IUltraNetPlugin>> all = UltraNet_GetAllPlugins();
    const bool grewByTwo = all.size() == before + 2;
    const bool hasA = std::find(all.begin(), all.end(),
                                std::static_pointer_cast<IUltraNetPlugin>(a)) != all.end();
    const bool hasB = std::find(all.begin(), all.end(),
                                std::static_pointer_cast<IUltraNetPlugin>(b)) != all.end();

    UltraNet_UnregisterPlugin("ProbeA");
    UltraNet_UnregisterPlugin("ProbeB");

    PROBE_EXPECT(grewByTwo);
    PROBE_EXPECT(hasA && hasB);
    PROBE_EXPECT(PluginCount() == before);
    return Working("returns one entry per registered plug-in and tracks "
                   "registration and removal");
}

ULTRANET_PROBE(kArea, UltraNet_RefreshPlugins) {
    const std::string saved = UltraNet_GetPluginDirectory();

    // An empty / missing directory must be a quiet no-op.
    const std::size_t baseline = PluginCount();
    UltraNet_SetPluginDirectory("/nonexistent/ultranet/plugins");
    UltraNet_RefreshPlugins();
    const bool quietOnMissing = PluginCount() == baseline;

    const std::string dir = BuiltPluginDir();
    if (!DirectoryHasPlugins(dir)) {
        UltraNet_SetPluginDirectory(saved);
        PROBE_EXPECT(quietOnMissing);
        return Implemented("a missing plug-in directory is handled quietly, but "
                           "this build produced no plug-in DSOs to load" +
                           (dir.empty() ? std::string()
                                        : " (looked in " + dir + ")") +
                           " — set ULTRANET_PROBE_PLUGIN_DIR to a directory "
                           "with built plug-ins to verify DSO loading");
    }

    UltraNet_SetPluginDirectory(dir);
    UltraNet_RefreshPlugins();
    const std::size_t afterFirst = PluginCount();

    // Idempotence: a second refresh must not double-register anything.
    UltraNet_RefreshPlugins();
    const std::size_t afterSecond = PluginCount();

    const std::vector<std::string> schemes = UltraNet_GetSupportedSchemes();
    UltraNet_SetPluginDirectory(saved);

    PROBE_EXPECT(quietOnMissing);
    PROBE_EXPECT_MSG(afterFirst > baseline,
                     "no plug-ins were registered from " + dir);
    PROBE_EXPECT_MSG(afterSecond == afterFirst,
                     "a second refresh changed the plug-in count from " +
                     std::to_string(afterFirst) + " to " +
                     std::to_string(afterSecond));
    return Working("loaded " + std::to_string(afterFirst - baseline) +
                   " plug-in DSOs from " + dir +
                   "; repeat refresh is idempotent; missing directory is a no-op");
}

ULTRANET_PROBE(kArea, UltraNet_GetPluginDirectory) {
    const std::string saved = UltraNet_GetPluginDirectory();
    PROBE_EXPECT_MSG(!saved.empty(), "the default plug-in directory is empty");

    UltraNet_SetPluginDirectory("/tmp/ultranet-probe-plugins");
    const std::string readBack = UltraNet_GetPluginDirectory();
    UltraNet_SetPluginDirectory(saved);

    PROBE_EXPECT(readBack == "/tmp/ultranet-probe-plugins");
    PROBE_EXPECT(UltraNet_GetPluginDirectory() == saved);
    return Working("reports the current directory (default \"" + saved +
                   "\") and reflects writes immediately");
}

ULTRANET_PROBE(kArea, UltraNet_SetPluginDirectory) {
    const std::string saved = UltraNet_GetPluginDirectory();

    UltraNet_SetPluginDirectory("relative/path");
    PROBE_EXPECT(UltraNet_GetPluginDirectory() == "relative/path");

    UltraNet_SetPluginDirectory("");
    PROBE_EXPECT(UltraNet_GetPluginDirectory().empty());
    UltraNet_RefreshPlugins();   // empty directory must not scan anything

    UltraNet_SetPluginDirectory(saved);
    PROBE_EXPECT(UltraNet_GetPluginDirectory() == saved);
    return Working("stores relative, absolute and empty paths; refreshing with "
                   "an empty directory is a safe no-op");
}

ULTRANET_PROBE(kArea, UltraNet_GetSupportedSchemes) {
    const std::vector<std::string> core = UltraNet_GetSupportedSchemes();
    for (const char* scheme : {"http", "https", "ws", "wss", "ftp", "ftps",
                               "sftp", "tcp", "udp", "tls", "dns"}) {
        PROBE_EXPECT_MSG(Contains(core, scheme),
                         std::string("core scheme \"") + scheme + "\" is missing");
    }
    // Sorted and de-duplicated, per the documented contract.
    PROBE_EXPECT(std::is_sorted(core.begin(), core.end()));
    PROBE_EXPECT(std::adjacent_find(core.begin(), core.end()) == core.end());

    auto plugin = std::make_shared<ProbePlugin>(
        "ApiStatusProbePlugin", std::vector<std::string>{"probe"});
    UltraNet_RegisterPlugin(plugin);
    const std::vector<std::string> withPlugin = UltraNet_GetSupportedSchemes();
    UltraNet_UnregisterPlugin("ApiStatusProbePlugin");

    PROBE_EXPECT(Contains(withPlugin, "probe"));
    PROBE_EXPECT(withPlugin.size() == core.size() + 1);
    return Working("every core scheme present, output sorted and de-duplicated, "
                   "and plug-in schemes merged in");
}
