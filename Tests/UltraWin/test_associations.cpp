// Tests/UltraWin/test_associations.cpp
// The program->environment association store and its place in RunApp's
// selection order (explicit option > owning prefix > association >
// Default). Uses the stub wine; the recorded $WINEPREFIX proves which
// environment a launch used.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraWinInternal.h"

#include <filesystem>
#include <fstream>

#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace ultrawin_internal;

namespace {

std::string ScratchRoot() {
    static std::string root = [] {
        std::string r = fs::temp_directory_path() /
                        ("ultrawin-assoc-" + std::to_string(getpid()));
        fs::create_directories(r);
        return r;
    }();
    return root;
}

std::string WriteFile(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path) << content;
    return path;
}

std::string StubWine() {
    std::string path = ScratchRoot() + "/wine";
    std::ofstream(path) << "#!/bin/sh\n"
                           "case \"$1\" in\n"
                           "  --version) echo 'wine-11.0 (stub)'; exit 0;;\n"
                           "  wineboot) : > \"$WINEPREFIX/system.reg\"; exit 0;;\n"
                           "esac\n"
                           "echo \"$@\" > \"$WINEPREFIX/last-cmd.txt\"\n"
                           "exit 0\n";
    chmod(path.c_str(), 0755);
    return path;
}

UltraWinConfig StubConfig() {
    UltraWinConfig cfg;
    cfg.environmentsRoot = ScratchRoot() + "/environments";
    cfg.winePath = StubWine();
    cfg.environmentCreateTimeoutSeconds = 30;
    return cfg;
}

// Launches and returns the environment whose prefix recorded the command.
std::string LaunchAndFindPrefix(const std::string& path,
                                const std::string& explicitEnv = {}) {
    UltraWinRunOptions opt;
    opt.environment = explicitEnv;
    UltraWinHandle h = UltraWinInvalidHandle;
    if (!UltraWin_RunApp(path, opt, &h)) return "<run failed>";
    int code = -1;
    if (!UltraWin_WaitApp(h, 10000, &code)) return "<wait failed>";
    UltraWin_ReleaseApp(h);
    for (const auto& env : UltraWin_ListEnvironments()) {
        if (fs::exists(env.prefixPath + "/last-cmd.txt")) {
            fs::remove(env.prefixPath + "/last-cmd.txt");
            return env.name;
        }
    }
    return "<no prefix recorded>";
}

}  // namespace

TEST(association_store_round_trip) {
    REQUIRE(UltraWin_Initialize(StubConfig()));
    REQUIRE_EQ(UltraWin_GetAssociation("/apps/tool.exe"), std::string(""));
    REQUIRE(UltraWin_SetAssociation("/apps/tool.exe", "MyApp"));
    REQUIRE_EQ(UltraWin_GetAssociation("/apps/tool.exe"),
               std::string("MyApp"));

    // Invalid inputs are rejected, valid overwrite works.
    REQUIRE_EQ(UltraWin_SetAssociation("relative.exe", "X").code,
               UltraWinResultCode::InvalidArgument);
    REQUIRE_EQ(UltraWin_SetAssociation("/apps/tool.exe", "../esc").code,
               UltraWinResultCode::InvalidArgument);
    REQUIRE(UltraWin_SetAssociation("/apps/tool.exe", "Other"));
    REQUIRE_EQ(UltraWin_GetAssociation("/apps/tool.exe"),
               std::string("Other"));

    REQUIRE(UltraWin_RemoveAssociation("/apps/tool.exe"));
    REQUIRE_EQ(UltraWin_GetAssociation("/apps/tool.exe"), std::string(""));
    REQUIRE_EQ(UltraWin_RemoveAssociation("/apps/tool.exe").code,
               UltraWinResultCode::InvalidArgument);
    UltraWin_Shutdown();
}

TEST(suggestion_prefers_own_then_sibling_then_folder) {
    REQUIRE(UltraWin_Initialize(StubConfig()));
    // No associations: the parent folder names the suggestion.
    REQUIRE_EQ(UltraWin_SuggestEnvironment("/opt/Big App/main.exe"),
               std::string("Big_App"));
    // A sibling's association wins over the folder name.
    REQUIRE(UltraWin_SetAssociation("/opt/Big App/main.exe", "BigApp"));
    REQUIRE_EQ(UltraWin_SuggestEnvironment("/opt/Big App/helper.exe"),
               std::string("BigApp"));
    // The program's own association wins over everything.
    REQUIRE(UltraWin_SetAssociation("/opt/Big App/helper.exe", "Elsewhere"));
    REQUIRE_EQ(UltraWin_SuggestEnvironment("/opt/Big App/helper.exe"),
               std::string("Elsewhere"));
    UltraWin_RemoveAssociation("/opt/Big App/main.exe");
    UltraWin_RemoveAssociation("/opt/Big App/helper.exe");
    UltraWin_Shutdown();
}

TEST(association_goes_stale_when_the_file_is_replaced) {
    // The Downloads case: setup.exe associated, deleted, and a DIFFERENT
    // installer downloaded to the same path — the stored choice must not
    // silently apply to the new program.
    REQUIRE(UltraWin_Initialize(StubConfig()));
    std::string dl =
        WriteFile(ScratchRoot() + "/downloads/setup.exe", "INSTALLER ONE");
    REQUIRE(UltraWin_SetAssociation(dl, "AppOne"));
    REQUIRE_EQ(UltraWin_GetAssociation(dl), std::string("AppOne"));

    // Different size.
    WriteFile(dl, "A COMPLETELY DIFFERENT INSTALLER PAYLOAD");
    REQUIRE_EQ(UltraWin_GetAssociation(dl), std::string(""));

    // Same size, different bytes — the content hash still catches it.
    REQUIRE(UltraWin_SetAssociation(dl, "AppTwo"));
    REQUIRE_EQ(UltraWin_GetAssociation(dl), std::string("AppTwo"));
    WriteFile(dl, "B COMPLETELY DIFFERENT INSTALLER PAYLOAD");
    REQUIRE_EQ(UltraWin_GetAssociation(dl), std::string(""));

    // Re-choosing after the change works normally.
    REQUIRE(UltraWin_SetAssociation(dl, "AppThree"));
    REQUIRE_EQ(UltraWin_GetAssociation(dl), std::string("AppThree"));
    UltraWin_RemoveAssociation(dl);
    UltraWin_Shutdown();
}

TEST(run_selection_order_with_associations) {
    REQUIRE(UltraWin_Initialize(StubConfig()));
    std::string exe = WriteFile(ScratchRoot() + "/portable/app.exe", "MZ");

    // No association: Default.
    REQUIRE_EQ(LaunchAndFindPrefix(exe), std::string("Default"));

    // Association beats Default.
    REQUIRE(UltraWin_SetAssociation(exe, "Chosen"));
    REQUIRE_EQ(LaunchAndFindPrefix(exe), std::string("Chosen"));

    // Explicit option beats the association.
    REQUIRE_EQ(LaunchAndFindPrefix(exe, "Forced"), std::string("Forced"));

    // Owning prefix beats the association: a program INSIDE an
    // environment runs there even when an association points elsewhere.
    std::string inner = WriteFile(
        ultrawin_internal::PrefixPath("Forced") + "/drive_c/inner.exe",
        "MZ");
    REQUIRE(UltraWin_SetAssociation(inner, "Chosen"));
    REQUIRE_EQ(LaunchAndFindPrefix(inner), std::string("Forced"));

    UltraWin_RemoveAssociation(exe);
    UltraWin_RemoveAssociation(inner);
    UltraWin_Shutdown();
}
