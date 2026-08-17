// Tests/UltraWin/test_lifecycle.cpp
// Module lifecycle, capability probing, environment and app-launch error
// paths. Everything here runs WITHOUT Wine — tests that would need a real
// wine binary skip themselves. The environments root is redirected to a
// scratch directory so no user state is touched.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraWinInternal.h"

#include <cstdio>
#include <filesystem>

#include <unistd.h>

namespace fs = std::filesystem;

namespace {

// Fresh scratch root per test run; cleaned up by the OS (under /tmp) if a
// test aborts early.
std::string ScratchRoot() {
    static std::string root = [] {
        std::string r = fs::temp_directory_path() /
                        ("ultrawin-tests-" + std::to_string(getpid()));
        return r;
    }();
    fs::create_directories(root);
    return root;
}

UltraWinConfig ScratchConfig() {
    UltraWinConfig cfg;
    cfg.environmentsRoot = ScratchRoot();
    return cfg;
}

}  // namespace

TEST(not_initialized_errors) {
    UltraWin_Shutdown();
    CHECK(!UltraWin_IsInitialized());
    auto r = UltraWin_CreateEnvironment("X");
    REQUIRE_EQ(r.code, UltraWinResultCode::NotInitialized);
    UltraWinHandle h = 0;
    r = UltraWin_RunApp("/bin/true", {}, &h);
    REQUIRE_EQ(r.code, UltraWinResultCode::NotInitialized);
    REQUIRE_EQ(h, UltraWinInvalidHandle);
}

TEST(initialize_and_config) {
    auto cfg = ScratchConfig();
    cfg.homeDriveLetter = 'u';  // canonicalised on the way in
    REQUIRE(UltraWin_Initialize(cfg));
    CHECK(UltraWin_IsInitialized());
    REQUIRE_EQ(UltraWin_GetConfig().homeDriveLetter, 'U');
    REQUIRE_EQ(UltraWin_GetConfig().environmentsRoot, ScratchRoot());

    UltraWinConfig bad;
    bad.homeDriveLetter = '5';
    auto r = UltraWin_Initialize(bad);
    REQUIRE_EQ(r.code, UltraWinResultCode::InvalidArgument);

    CHECK(!UltraWin_GetVersion().empty());
    UltraWin_Shutdown();
    CHECK(!UltraWin_IsInitialized());
}

TEST(capabilities_probe_is_safe) {
    // Callable initialized or not; must never throw and must always know
    // the host architecture. Wine fields depend on the machine — only
    // consistency is checked.
    auto caps = UltraWin_GetCapabilities();
    CHECK(!caps.hostArchitecture.empty());
    REQUIRE_EQ(caps.wineTierAvailable, caps.wineAvailable);
    CHECK(!caps.vmTierAvailable);  // Stage 1
    if (caps.wineAvailable) CHECK(!caps.winePath.empty());
}

TEST(environment_argument_validation) {
    REQUIRE(UltraWin_Initialize(ScratchConfig()));
    auto r = UltraWin_CreateEnvironment("../escape");
    REQUIRE_EQ(r.code, UltraWinResultCode::InvalidArgument);
    r = UltraWin_DeleteEnvironment("nope");
    REQUIRE_EQ(r.code, UltraWinResultCode::EnvironmentNotFound);
    CHECK(!UltraWin_EnvironmentExists("nope"));
    CHECK(UltraWin_ListEnvironments().empty());
    CHECK(UltraWin_ListMappings("nope").empty());

    r = UltraWin_MapFolder("nope", '5', "/tmp");
    REQUIRE_EQ(r.code, UltraWinResultCode::InvalidArgument);
    r = UltraWin_MapFolder("nope", 'D', "relative");
    REQUIRE_EQ(r.code, UltraWinResultCode::InvalidArgument);
    r = UltraWin_MapFolder("nope", 'C', "/tmp");
    REQUIRE_EQ(r.code, UltraWinResultCode::InvalidArgument);  // reserved
    r = UltraWin_MapFolder("nope", 'U', "/tmp");
    REQUIRE_EQ(r.code, UltraWinResultCode::InvalidArgument);  // home letter
    r = UltraWin_MapFolder("nope", 'D', "/tmp");
    REQUIRE_EQ(r.code, UltraWinResultCode::EnvironmentNotFound);
    UltraWin_Shutdown();
}

TEST(run_app_argument_validation) {
    REQUIRE(UltraWin_Initialize(ScratchConfig()));
    UltraWinHandle h = 0;

    auto r = UltraWin_RunApp("/bin/true", {}, nullptr);
    REQUIRE_EQ(r.code, UltraWinResultCode::InvalidArgument);

    UltraWinRunOptions vm;
    vm.forceTier = UltraWinTier::Vm;
    r = UltraWin_RunApp("/bin/true", vm, &h);
    REQUIRE_EQ(r.code, UltraWinResultCode::NotSupported);

    r = UltraWin_RunApp("relative.exe", {}, &h);
    REQUIRE_EQ(r.code, UltraWinResultCode::InvalidArgument);

    r = UltraWin_RunApp("/definitely/not/there.exe", {}, &h);
    REQUIRE_EQ(r.code, UltraWinResultCode::FileNotFound);

    // Handle-based calls on a made-up handle.
    REQUIRE_EQ(UltraWin_CloseApp(12345).code,
               UltraWinResultCode::InvalidHandle);
    REQUIRE_EQ(UltraWin_GetAppState(12345), UltraWinAppState::Failed);
    UltraWinAppInfo info;
    REQUIRE_EQ(UltraWin_GetAppInfo(12345, &info).code,
               UltraWinResultCode::InvalidHandle);
    CHECK(UltraWin_ListApps().empty());
    UltraWin_Shutdown();
}

TEST(create_environment_with_real_wine) {
    REQUIRE(UltraWin_Initialize(ScratchConfig()));
    if (ultrawin_internal::FindWineBinary().empty())
        SKIP("no wine binary on this host");

    auto r = UltraWin_CreateEnvironment("WineSmoke");
    REQUIRE(r);
    CHECK(UltraWin_EnvironmentExists("WineSmoke"));
    auto envs = UltraWin_ListEnvironments();
    REQUIRE_EQ(envs.size(), static_cast<size_t>(1));
    REQUIRE_EQ(envs[0].name, std::string("WineSmoke"));
    CHECK(envs[0].initialized);

    // The unified home drive exists, the root drive does not (default
    // config), and a second create collides.
    fs::path dos = fs::path(envs[0].prefixPath) / "dosdevices";
    CHECK(fs::is_symlink(dos / "u:"));
    CHECK(!fs::exists(dos / "z:"));
    REQUIRE_EQ(UltraWin_CreateEnvironment("WineSmoke").code,
               UltraWinResultCode::EnvironmentExists);

    // Mapping round-trip against the real prefix.
    REQUIRE(UltraWin_MapFolder("WineSmoke", 'D', ScratchRoot()));
    auto maps = UltraWin_ListMappings("WineSmoke");
    REQUIRE_EQ(maps.size(), static_cast<size_t>(1));
    CHECK(fs::is_symlink(dos / "d:"));
    REQUIRE(UltraWin_UnmapFolder("WineSmoke", 'D'));
    CHECK(!fs::exists(dos / "d:"));

    REQUIRE(UltraWin_DeleteEnvironment("WineSmoke"));
    CHECK(!UltraWin_EnvironmentExists("WineSmoke"));
    UltraWin_Shutdown();
}
