// Tests/UltraWin/test_supervision.cpp
// End-to-end launch/supervision coverage WITHOUT a real Wine install: the
// UltraWinConfig::winePath override points at a stub shell script that
// answers --version, fakes `wineboot --init`, and otherwise sleeps in
// place of a Windows app. This exercises environment creation, drive
// mapping, fork/exec, state tracking, Close/Kill/Wait/Release, and the
// EnvironmentBusy guard exactly as with the real binary.
// Version: 0.1.0
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
                        ("ultrawin-stub-" + std::to_string(getpid()));
        fs::create_directories(r);
        return r;
    }();
    return root;
}

// A stand-in wine: --version prints a banner, `wineboot --init` creates the
// prefix registry (what PrefixInitialized checks), "exit7.exe" exits with
// code 7, anything else sleeps until signalled.
std::string WriteStubWine() {
    std::string path = StubRoot() + "/wine";
    std::ofstream out(path);
    out << "#!/bin/sh\n"
           "case \"$1\" in\n"
           "  --version) echo 'wine-11.0 (UltraWin stub)'; exit 0;;\n"
           "  wineboot) : > \"$WINEPREFIX/system.reg\"; exit 0;;\n"
           "esac\n"
           "case \"$(basename \"$1\")\" in\n"
           "  exit7.exe) exit 7;;\n"
           "esac\n"
           "exec sleep 300\n";
    out.close();
    chmod(path.c_str(), 0755);
    return path;
}

std::string WriteDummyExe(const std::string& name) {
    std::string path = StubRoot() + "/" + name;
    std::ofstream(path) << "MZ";
    return path;
}

UltraWinConfig StubConfig() {
    UltraWinConfig cfg;
    cfg.environmentsRoot = StubRoot() + "/environments";
    cfg.winePath = WriteStubWine();
    cfg.environmentCreateTimeoutSeconds = 30;
    return cfg;
}

}  // namespace

TEST(stub_environment_create_and_mappings) {
    REQUIRE(UltraWin_Initialize(StubConfig()));
    REQUIRE(UltraWin_CreateEnvironment("Stub"));
    auto envs = UltraWin_ListEnvironments();
    REQUIRE_EQ(envs.size(), static_cast<size_t>(1));
    CHECK(envs[0].initialized);

    fs::path dos = fs::path(envs[0].prefixPath) / "dosdevices";
    CHECK(fs::is_symlink(dos / "u:"));   // unified home drive
    CHECK(!fs::exists(dos / "z:"));      // root drive suppressed by default
    UltraWin_Shutdown();
}

TEST(stub_app_exit_code_is_reported) {
    REQUIRE(UltraWin_Initialize(StubConfig()));
    UltraWinHandle h = 0;
    UltraWinRunOptions opt;
    opt.environment = "Stub";
    REQUIRE(UltraWin_RunApp(WriteDummyExe("exit7.exe"), opt, &h));
    REQUIRE(h != UltraWinInvalidHandle);

    int exitCode = -1;
    REQUIRE(UltraWin_WaitApp(h, 10000, &exitCode));
    REQUIRE_EQ(exitCode, 7);
    REQUIRE_EQ(UltraWin_GetAppState(h), UltraWinAppState::Exited);
    REQUIRE(UltraWin_ReleaseApp(h));
    UltraWin_Shutdown();
}

TEST(stub_app_close_terminates_and_busy_guard) {
    REQUIRE(UltraWin_Initialize(StubConfig()));
    UltraWinHandle h = 0;
    UltraWinRunOptions opt;
    opt.environment = "Stub";
    REQUIRE(UltraWin_RunApp(WriteDummyExe("longrun.exe"), opt, &h));
    REQUIRE_EQ(UltraWin_GetAppState(h), UltraWinAppState::Running);

    UltraWinAppInfo info;
    REQUIRE(UltraWin_GetAppInfo(h, &info));
    REQUIRE_EQ(info.environment, std::string("Stub"));
    REQUIRE_EQ(info.tier, UltraWinTier::Wine);
    CHECK(info.processId > 0);

    // Running apps keep their environment alive...
    REQUIRE_EQ(UltraWin_DeleteEnvironment("Stub").code,
               UltraWinResultCode::EnvironmentBusy);
    // ...and running apps cannot be released.
    REQUIRE_EQ(UltraWin_ReleaseApp(h).code,
               UltraWinResultCode::InvalidArgument);

    REQUIRE(UltraWin_CloseApp(h));
    int exitCode = -1;
    REQUIRE(UltraWin_WaitApp(h, 10000, &exitCode));
    REQUIRE_EQ(UltraWin_GetAppState(h), UltraWinAppState::Terminated);
    REQUIRE(UltraWin_ReleaseApp(h));

    REQUIRE(UltraWin_DeleteEnvironment("Stub"));
    UltraWin_Shutdown();
}
