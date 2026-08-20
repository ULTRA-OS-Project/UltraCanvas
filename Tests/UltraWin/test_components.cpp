// Tests/UltraWin/test_components.cpp
// Component installation (winetricks wrapper) WITHOUT a real winetricks:
// the UltraWinConfig::winetricksPath override points at a stub script that
// records verbs into the prefix's winetricks.log the way the real one
// does, and fails on a designated verb. Uses the same stub wine as
// test_supervision.cpp for environment creation.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraWinInternal.h"

#include <filesystem>
#include <fstream>

#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

std::string StubRoot() {
    static std::string root = [] {
        std::string r = fs::temp_directory_path() /
                        ("ultrawin-comp-" + std::to_string(getpid()));
        fs::create_directories(r);
        return r;
    }();
    return root;
}

std::string WriteScript(const std::string& name, const std::string& body) {
    std::string path = StubRoot() + "/" + name;
    std::ofstream(path) << body;
    chmod(path.c_str(), 0755);
    return path;
}

// Same contract as the supervision stub: fake wineboot so environments can
// be created without a Wine install.
std::string StubWine() {
    return WriteScript("wine",
                       "#!/bin/sh\n"
                       "case \"$1\" in\n"
                       "  --version) echo 'wine-11.0 (UltraWin stub)'; exit 0;;\n"
                       "  wineboot) : > \"$WINEPREFIX/system.reg\"; exit 0;;\n"
                       "esac\n"
                       "exit 0\n");
}

// Records the verb like the real winetricks; "badverb" fails; also asserts
// the WINE variable reached it (written to a probe file for the test).
std::string StubWinetricks() {
    return WriteScript(
        "winetricks",
        "#!/bin/sh\n"
        "[ \"$1\" = \"--unattended\" ] || exit 64\n"
        "[ \"$2\" = \"badverb\" ] && exit 1\n"
        "echo \"$WINE\" > \"$WINEPREFIX/wine-used.txt\"\n"
        "echo \"$2\" >> \"$WINEPREFIX/winetricks.log\"\n"
        "exit 0\n");
}

UltraWinConfig StubConfig() {
    UltraWinConfig cfg;
    cfg.environmentsRoot = StubRoot() + "/environments";
    cfg.winePath = StubWine();
    cfg.winetricksPath = StubWinetricks();
    cfg.environmentCreateTimeoutSeconds = 30;
    cfg.componentInstallTimeoutSeconds = 30;
    return cfg;
}

}  // namespace

TEST(component_name_validation) {
    using namespace ultrawin_internal;
    CHECK(IsValidComponentName("vcrun2019"));
    CHECK(IsValidComponentName("dotnet48"));
    CHECK(IsValidComponentName("d3dcompiler_47"));
    CHECK(IsValidComponentName("ie8-kb2936068"));
    CHECK(!IsValidComponentName(""));
    CHECK(!IsValidComponentName("Corefonts"));   // upper case
    CHECK(!IsValidComponentName("-flag"));       // leading dash
    CHECK(!IsValidComponentName("a b"));
    CHECK(!IsValidComponentName("a/b"));
    CHECK(!IsValidComponentName(std::string(65, 'x')));
}

TEST(install_component_argument_validation) {
    REQUIRE(UltraWin_Initialize(StubConfig()));
    auto r = UltraWin_InstallComponent("../escape", "corefonts");
    REQUIRE_EQ(r.code, UltraWinResultCode::InvalidArgument);
    r = UltraWin_InstallComponent("Env", "--force");
    REQUIRE_EQ(r.code, UltraWinResultCode::InvalidArgument);
    CHECK(UltraWin_ListComponents("nope").empty());
    UltraWin_Shutdown();
}

TEST(install_component_missing_winetricks) {
    auto cfg = StubConfig();
    cfg.winetricksPath = StubRoot() + "/definitely-not-there";
    REQUIRE(UltraWin_Initialize(cfg));
    auto r = UltraWin_InstallComponent("Env", "corefonts");
    REQUIRE_EQ(r.code, UltraWinResultCode::WinetricksNotFound);
    UltraWin_Shutdown();
}

TEST(install_component_records_and_lists) {
    REQUIRE(UltraWin_Initialize(StubConfig()));
    // Capabilities see the stub script.
    auto caps = UltraWin_GetCapabilities();
    CHECK(caps.winetricksAvailable);

    // Auto-creates the environment, then installs two verbs.
    REQUIRE(UltraWin_InstallComponent("CompEnv", "corefonts"));
    REQUIRE(UltraWin_InstallComponent("CompEnv", "vcrun2019"));
    auto components = UltraWin_ListComponents("CompEnv");
    REQUIRE_EQ(components.size(), static_cast<size_t>(2));
    REQUIRE_EQ(components[0], std::string("corefonts"));
    REQUIRE_EQ(components[1], std::string("vcrun2019"));

    // The configured wine binary reached winetricks via $WINE.
    std::ifstream probe(ultrawin_internal::PrefixPath("CompEnv") +
                        "/wine-used.txt");
    std::string usedWine;
    std::getline(probe, usedWine);
    REQUIRE_EQ(usedWine, UltraWin_GetConfig().winePath);

    // A failing verb reports ComponentInstallFailed and records nothing new.
    auto r = UltraWin_InstallComponent("CompEnv", "badverb");
    REQUIRE_EQ(r.code, UltraWinResultCode::ComponentInstallFailed);
    REQUIRE_EQ(UltraWin_ListComponents("CompEnv").size(),
               static_cast<size_t>(2));

    REQUIRE(UltraWin_DeleteEnvironment("CompEnv"));
    UltraWin_Shutdown();
}
