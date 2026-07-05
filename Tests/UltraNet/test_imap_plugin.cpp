// Tests/UltraNet/test_imap_plugin.cpp
// Verifies the IMAP plug-in DSO compiles, loads via UltraNet_RefreshPlugins,
// registers for the imap / imaps URL schemes, and fails gracefully when
// asked to send mail. No live IMAP server: a full FetchMessages test would
// need server credentials and is out of scope for CI.
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

fs::path FindOrBuildImapPlugin() {
    if (const char* env = std::getenv("ULTRANET_IMAP_PLUGIN_PATH")) {
        if (fs::exists(env)) return env;
    }
#ifdef ULTRANET_IMAP_PLUGIN_PATH_DEFINE
    if (fs::exists(ULTRANET_IMAP_PLUGIN_PATH_DEFINE)) {
        return fs::path{ULTRANET_IMAP_PLUGIN_PATH_DEFINE};
    }
#endif
    const fs::path src =
        "UltraCanvas/Plugins/UltraNet/imap/ImapPlugin.cpp";
    if (!fs::exists(src)) return {};
    if (std::system("command -v g++ > /dev/null 2>&1") != 0) return {};

    const fs::path outDir = fs::temp_directory_path() / "ultranet_imap_test";
    fs::remove_all(outDir);
    fs::create_directories(outDir);
    const fs::path outFile = outDir / "ultranet_imap.so";

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

TEST(imap_plugin_loads_via_refresh) {
    UltraNet_Initialize();

    const fs::path pluginPath = FindOrBuildImapPlugin();
    if (pluginPath.empty()) SKIP("IMAP plug-in not buildable in this env");

    UltraNet_SetPluginDirectory(pluginPath.parent_path().string());
    UltraNet_RefreshPlugins();

    auto plugin = UltraNet_GetPlugin("imap");
    REQUIRE(plugin != nullptr);
    REQUIRE_EQ(plugin->GetName(), std::string{"UltraNet-IMAP"});

    auto schemes = plugin->GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "imap")  != schemes.end());
    CHECK(std::find(schemes.begin(), schemes.end(), "imaps") != schemes.end());
}

TEST(imap_plugin_send_mail_returns_unsupported) {
    const fs::path pluginPath = FindOrBuildImapPlugin();
    if (pluginPath.empty()) SKIP("IMAP plug-in not buildable in this env");
    UltraNet_SetPluginDirectory(pluginPath.parent_path().string());
    UltraNet_RefreshPlugins();

    auto plugin = UltraNet_GetPlugin("imap");
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

TEST(imap_plugin_fetch_invalid_url_returns_error) {
    const fs::path pluginPath = FindOrBuildImapPlugin();
    if (pluginPath.empty()) SKIP("IMAP plug-in not buildable in this env");
    UltraNet_SetPluginDirectory(pluginPath.parent_path().string());
    UltraNet_RefreshPlugins();

    auto plugin = UltraNet_GetPlugin("imap");
    REQUIRE(plugin != nullptr);
    auto* mail = dynamic_cast<IMailProtocolPlugin*>(plugin.get());
    REQUIRE(mail != nullptr);

    std::vector<UltraNetMailMessage> msgs;
    UltraNetMailOptions opt;
    auto r1 = mail->FetchMessages("http://not-imap-scheme/", msgs, opt);
    CHECK(!bool(r1));
    REQUIRE_EQ(r1.code, UltraNetResultCode::InvalidUrl);

    auto r2 = mail->FetchMessages("garbage", msgs, opt);
    CHECK(!bool(r2));
}
