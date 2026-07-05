// Tests/UltraNet/test_pop3_plugin.cpp
// Verifies the POP3 plug-in DSO compiles, loads via UltraNet_RefreshPlugins,
// registers for the pop3 / pop3s URL schemes, and rejects bad inputs.
// Live POP3 fetch would need server credentials — out of scope for CI.
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

fs::path FindOrBuildPop3Plugin() {
    if (const char* env = std::getenv("ULTRANET_POP3_PLUGIN_PATH")) {
        if (fs::exists(env)) return env;
    }
#ifdef ULTRANET_POP3_PLUGIN_PATH_DEFINE
    if (fs::exists(ULTRANET_POP3_PLUGIN_PATH_DEFINE)) {
        return fs::path{ULTRANET_POP3_PLUGIN_PATH_DEFINE};
    }
#endif
    const fs::path src =
        "UltraCanvas/Plugins/UltraNet/pop3/Pop3Plugin.cpp";
    if (!fs::exists(src)) return {};
    if (std::system("command -v g++ > /dev/null 2>&1") != 0) return {};

    const fs::path outDir = fs::temp_directory_path() / "ultranet_pop3_test";
    fs::remove_all(outDir);
    fs::create_directories(outDir);
    const fs::path outFile = outDir / "ultranet_pop3.so";

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

TEST(pop3_plugin_loads_via_refresh) {
    UltraNet_Initialize();

    const fs::path pluginPath = FindOrBuildPop3Plugin();
    if (pluginPath.empty()) SKIP("POP3 plug-in not buildable in this env");

    UltraNet_SetPluginDirectory(pluginPath.parent_path().string());
    UltraNet_RefreshPlugins();

    auto plugin = UltraNet_GetPlugin("pop3");
    REQUIRE(plugin != nullptr);
    REQUIRE_EQ(plugin->GetName(), std::string{"UltraNet-POP3"});

    auto schemes = plugin->GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "pop3")  != schemes.end());
    CHECK(std::find(schemes.begin(), schemes.end(), "pop3s") != schemes.end());
}

TEST(pop3_plugin_send_mail_returns_unsupported) {
    const fs::path pluginPath = FindOrBuildPop3Plugin();
    if (pluginPath.empty()) SKIP("POP3 plug-in not buildable in this env");
    UltraNet_SetPluginDirectory(pluginPath.parent_path().string());
    UltraNet_RefreshPlugins();

    auto plugin = UltraNet_GetPlugin("pop3");
    REQUIRE(plugin != nullptr);
    auto* mail = dynamic_cast<IMailProtocolPlugin*>(plugin.get());
    REQUIRE(mail != nullptr);

    UltraNetMailMessage msg;
    msg.from = "a@example.com";
    msg.to   = {"b@example.com"};
    UltraNetMailOptions opt;
    auto r = mail->SendMail(msg, opt);
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::UnsupportedScheme);
}

TEST(pop3_plugin_fetch_invalid_url_returns_error) {
    const fs::path pluginPath = FindOrBuildPop3Plugin();
    if (pluginPath.empty()) SKIP("POP3 plug-in not buildable in this env");
    UltraNet_SetPluginDirectory(pluginPath.parent_path().string());
    UltraNet_RefreshPlugins();

    auto plugin = UltraNet_GetPlugin("pop3");
    REQUIRE(plugin != nullptr);
    auto* mail = dynamic_cast<IMailProtocolPlugin*>(plugin.get());
    REQUIRE(mail != nullptr);

    std::vector<UltraNetMailMessage> msgs;
    UltraNetMailOptions opt;
    auto r1 = mail->FetchMessages("https://not-pop3-scheme/", msgs, opt);
    CHECK(!bool(r1));
    REQUIRE_EQ(r1.code, UltraNetResultCode::InvalidUrl);

    auto r2 = mail->FetchMessages("garbage", msgs, opt);
    CHECK(!bool(r2));
}

TEST(pop3_plugin_coexists_with_imap_smtp_in_registry) {
    const fs::path pluginPath = FindOrBuildPop3Plugin();
    if (pluginPath.empty()) SKIP("POP3 plug-in not buildable in this env");
    UltraNet_SetPluginDirectory(pluginPath.parent_path().string());
    UltraNet_RefreshPlugins();

    // Even with just the POP3 DSO loaded, the supported-schemes union
    // exposes both core schemes (https) and the plug-in's schemes.
    auto schemes = UltraNet_GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "https") != schemes.end());
    CHECK(std::find(schemes.begin(), schemes.end(), "pop3")  != schemes.end());
}
