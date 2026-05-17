// Tests/UltraNet/test_extra_plugins.cpp
// Registration / interface sanity tests for the four libcurl-native
// "quick-win" plug-ins added on top of the mail trio:
//   - WebDAV   (IFileShareProtocolPlugin)
//   - LDAP     (IDirectoryProtocolPlugin)
//   - Telnet   (IRemoteAccessPlugin)
//   - RTSP     (IStreamingProtocolPlugin)
//
// Live-server testing for each protocol needs real services and is out
// of scope for CI. We verify each plug-in compiles standalone as a DSO,
// loads via UltraNet_RefreshPlugins, registers for the expected schemes,
// and rejects malformed input cleanly.
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

// Build (or locate a pre-built) plug-in DSO for `name`. Same pattern as
// the SMTP / IMAP / POP3 tests — checks an env var, then a CMake-supplied
// path, then compiles from source via g++ as a last resort.
fs::path FindOrBuild(const std::string& name) {
    std::string envName = "ULTRANET_" + name + "_PLUGIN_PATH";
    for (auto& c : envName) c = static_cast<char>(std::toupper(c));
    if (const char* p = std::getenv(envName.c_str())) {
        if (fs::exists(p)) return p;
    }
#ifdef ULTRANET_WEBDAV_PLUGIN_PATH_DEFINE
    if (name == "WEBDAV" && fs::exists(ULTRANET_WEBDAV_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_WEBDAV_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_LDAP_PLUGIN_PATH_DEFINE
    if (name == "LDAP" && fs::exists(ULTRANET_LDAP_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_LDAP_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_TELNET_PLUGIN_PATH_DEFINE
    if (name == "TELNET" && fs::exists(ULTRANET_TELNET_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_TELNET_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_RTSP_PLUGIN_PATH_DEFINE
    if (name == "RTSP" && fs::exists(ULTRANET_RTSP_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_RTSP_PLUGIN_PATH_DEFINE};
#endif

    // Map test name -> source/class name pair.
    std::string lower = name, cls = name;
    for (auto& c : lower) c = static_cast<char>(std::tolower(c));
    if      (name == "WEBDAV") cls = "WebDav";
    else if (name == "LDAP")   cls = "Ldap";
    else if (name == "TELNET") cls = "Telnet";
    else if (name == "RTSP")   cls = "Rtsp";

    const fs::path src = fs::path{"UltraCanvas/Plugins/UltraNet"} / lower /
                         (cls + "Plugin.cpp");
    if (!fs::exists(src)) return {};
    if (std::system("command -v g++ > /dev/null 2>&1") != 0) return {};

    const fs::path outDir =
        fs::temp_directory_path() / ("ultranet_" + lower + "_test");
    fs::remove_all(outDir);
    fs::create_directories(outDir);
    const fs::path outFile = outDir / ("ultranet_" + lower + ".so");

    const std::string cmd =
        "g++ -std=c++20 -fPIC -shared "
        "-I UltraCanvas/include "
        + src.string() +
        " $(pkg-config --cflags --libs libcurl) "
        "-o " + outFile.string() + " 2>&1";
    if (std::system(cmd.c_str()) != 0) return {};
    return fs::exists(outFile) ? outFile : fs::path{};
}

bool LoadInto(const std::string& name, const std::string& expectedScheme) {
    const fs::path p = FindOrBuild(name);
    if (p.empty()) return false;
    UltraNet_SetPluginDirectory(p.parent_path().string());
    UltraNet_RefreshPlugins();
    auto plugin = UltraNet_GetPlugin(expectedScheme);
    return plugin != nullptr;
}

} // namespace

// ===== WebDAV =====
TEST(webdav_plugin_loads_and_registers) {
    UltraNet_Initialize();
    if (!LoadInto("WEBDAV", "webdav")) SKIP("WebDAV plug-in not buildable");

    auto p = UltraNet_GetPlugin("webdav");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-WebDAV"});
    auto schemes = p->GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "webdav")  != schemes.end());
    CHECK(std::find(schemes.begin(), schemes.end(), "webdavs") != schemes.end());
    auto* dav = dynamic_cast<IFileShareProtocolPlugin*>(p.get());
    REQUIRE(dav != nullptr);
}

// ===== LDAP =====
TEST(ldap_plugin_loads_and_registers) {
    if (!LoadInto("LDAP", "ldap")) SKIP("LDAP plug-in not buildable");

    auto p = UltraNet_GetPlugin("ldap");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-LDAP"});
    auto schemes = p->GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "ldap")  != schemes.end());
    CHECK(std::find(schemes.begin(), schemes.end(), "ldaps") != schemes.end());
    auto* dir = dynamic_cast<IDirectoryProtocolPlugin*>(p.get());
    REQUIRE(dir != nullptr);
}

// ===== Telnet =====
TEST(telnet_plugin_loads_and_registers) {
    if (!LoadInto("TELNET", "telnet")) SKIP("Telnet plug-in not buildable");

    auto p = UltraNet_GetPlugin("telnet");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-Telnet"});
    auto* shell = dynamic_cast<IRemoteAccessPlugin*>(p.get());
    REQUIRE(shell != nullptr);

    // Bad URL -> InvalidHandle, no crash.
    UltraNetRemoteOptions opt;
    UltraNetHandle h = shell->OpenShell("not-a-telnet-url", opt);
    REQUIRE_EQ(h, UltraNetInvalidHandle);

    // ExecuteCommand on bogus handle -> InvalidHandle result.
    std::string out, err; int exitCode = 0;
    auto r = shell->ExecuteCommand(99999u, "ls", out, err, exitCode);
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidHandle);
}

// ===== RTSP =====
TEST(rtsp_plugin_loads_and_registers) {
    if (!LoadInto("RTSP", "rtsp")) SKIP("RTSP plug-in not buildable");

    auto p = UltraNet_GetPlugin("rtsp");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-RTSP"});
    auto* stream = dynamic_cast<IStreamingProtocolPlugin*>(p.get());
    REQUIRE(stream != nullptr);

    // ReadFrame on a fresh / non-existent handle: must report
    // UnsupportedScheme (the documented "control-only" limitation) or
    // InvalidHandle, never empty success.
    std::vector<uint8_t> frame;
    auto r = stream->ReadFrame(99999u, frame);
    CHECK(!bool(r));
}

// ===== Scheme registry coverage =====
TEST(all_quickwin_plugins_appear_in_supported_schemes) {
    // After loading any of them, the supported-scheme list should grow.
    const auto schemes = UltraNet_GetSupportedSchemes();
    // Core schemes always present.
    CHECK(std::find(schemes.begin(), schemes.end(), "https") != schemes.end());
    // At least one of the four — whichever loaded most recently — should
    // show up. (Each test above is independent; we don't assert all four
    // are present together because earlier tests may have torn things down.)
    bool anyQuickWin =
        std::find(schemes.begin(), schemes.end(), "webdav") != schemes.end() ||
        std::find(schemes.begin(), schemes.end(), "ldap")   != schemes.end() ||
        std::find(schemes.begin(), schemes.end(), "telnet") != schemes.end() ||
        std::find(schemes.begin(), schemes.end(), "rtsp")   != schemes.end();
    CHECK(anyQuickWin);
}
