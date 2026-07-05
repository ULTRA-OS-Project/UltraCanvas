// Tests/UltraNet/test_smtp_plugin.cpp
// Verifies the SMTP plug-in DSO loads via UltraNet_RefreshPlugins() and
// registers itself for the smtp / smtps URL schemes.
//
// Source-of-truth for the plug-in binary:
//   1. $ULTRANET_SMTP_PLUGIN_PATH if set (CMake test runner provides this)
//   2. ULTRANET_SMTP_PLUGIN_PATH_DEFINE if compiled in (also CMake)
//   3. Build it on the fly via g++ from the source tree under
//      UltraCanvas/Plugins/UltraNet/smtp/SmtpPlugin.cpp
// Test SKIPs if none of those work.
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path FindOrBuildSmtpPlugin() {
    if (const char* env = std::getenv("ULTRANET_SMTP_PLUGIN_PATH")) {
        if (fs::exists(env)) return env;
    }
#ifdef ULTRANET_SMTP_PLUGIN_PATH_DEFINE
    if (fs::exists(ULTRANET_SMTP_PLUGIN_PATH_DEFINE)) {
        return fs::path{ULTRANET_SMTP_PLUGIN_PATH_DEFINE};
    }
#endif
    // On-the-fly build path (manual / sandbox runs).
    const fs::path src =
        "UltraCanvas/Plugins/UltraNet/smtp/SmtpPlugin.cpp";
    if (!fs::exists(src)) return {};

    if (std::system("command -v g++ > /dev/null 2>&1") != 0) return {};

    const fs::path outDir = fs::temp_directory_path() / "ultranet_smtp_test";
    fs::remove_all(outDir);
    fs::create_directories(outDir);
    const fs::path outFile = outDir / "ultranet_smtp.so";

    const std::string cmd =
        "g++ -std=c++20 -fPIC -shared "
        "-I UltraCanvas/include "
        + src.string() +
        " $(pkg-config --cflags --libs libcurl) "
        "-o " + outFile.string() + " 2>&1";
    if (std::system(cmd.c_str()) != 0) return {};
    if (!fs::exists(outFile)) return {};
    return outFile;
}

} // namespace

TEST(smtp_plugin_loads_via_refresh) {
    UltraNet_Initialize();

    const fs::path pluginPath = FindOrBuildSmtpPlugin();
    if (pluginPath.empty()) SKIP("SMTP plug-in not buildable in this env");

    // Use the plug-in's parent dir as the scan directory.
    UltraNet_SetPluginDirectory(pluginPath.parent_path().string());
    UltraNet_RefreshPlugins();

    auto plugin = UltraNet_GetPlugin("smtp");
    REQUIRE(plugin != nullptr);
    REQUIRE_EQ(plugin->GetName(), std::string{"UltraNet-SMTP"});

    auto schemes = plugin->GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "smtp")  != schemes.end());
    CHECK(std::find(schemes.begin(), schemes.end(), "smtps") != schemes.end());
}

TEST(smtp_plugin_refresh_is_idempotent) {
    const fs::path pluginPath = FindOrBuildSmtpPlugin();
    if (pluginPath.empty()) SKIP("SMTP plug-in not buildable in this env");
    UltraNet_SetPluginDirectory(pluginPath.parent_path().string());

    UltraNet_RefreshPlugins();
    const std::size_t before = UltraNet_GetAllPlugins().size();
    UltraNet_RefreshPlugins();
    UltraNet_RefreshPlugins();
    const std::size_t after = UltraNet_GetAllPlugins().size();
    REQUIRE_EQ(before, after);
}

TEST(smtp_plugin_via_supported_schemes_listing) {
    const fs::path pluginPath = FindOrBuildSmtpPlugin();
    if (pluginPath.empty()) SKIP("SMTP plug-in not buildable in this env");
    UltraNet_SetPluginDirectory(pluginPath.parent_path().string());
    UltraNet_RefreshPlugins();

    auto schemes = UltraNet_GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "smtp")  != schemes.end());
    CHECK(std::find(schemes.begin(), schemes.end(), "smtps") != schemes.end());
    // Core schemes still present:
    CHECK(std::find(schemes.begin(), schemes.end(), "https") != schemes.end());
}
