// Tests/UltraWin/test_programs.cpp
// Launch routing (.msi via msiexec, .lnk via start /wait /unix) and
// Start-Menu program enumeration. The stub wine records its command line
// into the prefix so the routed argv can be asserted; the Start-Menu tree
// is planted directly into the prefix's drive_c. No real Wine needed.
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
                        ("ultrawin-prog-" + std::to_string(getpid()));
        fs::create_directories(r);
        return r;
    }();
    return root;
}

// Fakes wineboot and records every other invocation's arguments.
std::string StubWine() {
    std::string path = StubRoot() + "/wine";
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

std::string WriteFile(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path) << content;
    return path;
}

UltraWinConfig StubConfig() {
    UltraWinConfig cfg;
    cfg.environmentsRoot = StubRoot() + "/environments";
    cfg.winePath = StubWine();
    cfg.environmentCreateTimeoutSeconds = 30;
    return cfg;
}

// Runs `path` through UltraWin_RunApp in envName, waits for the stub to
// exit, and returns the command line the stub saw.
std::string LaunchAndCapture(const std::string& envName,
                             const std::string& path) {
    UltraWinRunOptions opt;
    opt.environment = envName;
    UltraWinHandle h = UltraWinInvalidHandle;
    if (!UltraWin_RunApp(path, opt, &h)) return "<run failed>";
    int code = -1;
    if (!UltraWin_WaitApp(h, 10000, &code) || code != 0)
        return "<app failed>";
    UltraWin_ReleaseApp(h);
    std::ifstream cmd(ultrawin_internal::PrefixPath(envName) +
                      "/last-cmd.txt");
    std::string line;
    std::getline(cmd, line);
    return line;
}

}  // namespace

TEST(run_routing_by_extension) {
    REQUIRE(UltraWin_Initialize(StubConfig()));
    std::string exe = WriteFile(StubRoot() + "/files/App.exe", "MZ");
    std::string msi = WriteFile(StubRoot() + "/files/Setup.MSI", "msi");
    std::string lnk = WriteFile(StubRoot() + "/files/App.lnk", "L");

    REQUIRE_EQ(LaunchAndCapture("Route", exe), exe);
    REQUIRE_EQ(LaunchAndCapture("Route", msi),
               "msiexec /i " + msi);  // extension matched case-insensitively
    REQUIRE_EQ(LaunchAndCapture("Route", lnk),
               "start /wait /unix " + lnk);
    UltraWin_Shutdown();
}

TEST(list_programs_from_start_menus) {
    REQUIRE(UltraWin_Initialize(StubConfig()));
    REQUIRE(UltraWin_CreateEnvironment("Menu"));
    fs::path driveC =
        fs::path(ultrawin_internal::PrefixPath("Menu")) / "drive_c";
    const std::string tail = "Microsoft/Windows/Start Menu/Programs";

    WriteFile((driveC / "ProgramData" / tail / "Zeta.lnk").string(), "L");
    WriteFile((driveC / "ProgramData" / tail / "Tools/Editor.lnk").string(),
              "L");
    WriteFile((driveC / "users/alice/AppData/Roaming" / tail /
               "Alpha.lnk").string(),
              "L");
    WriteFile((driveC / "ProgramData" / tail / "notes.txt").string(),
              "not a shortcut");

    auto programs = UltraWin_ListPrograms("Menu");
    REQUIRE_EQ(programs.size(), static_cast<size_t>(3));
    REQUIRE_EQ(programs[0].name, std::string("Alpha"));      // per-user menu
    REQUIRE_EQ(programs[1].name, std::string("Editor"));
    REQUIRE_EQ(programs[1].category, std::string("Tools"));  // subfolder
    REQUIRE_EQ(programs[2].name, std::string("Zeta"));
    REQUIRE_EQ(programs[2].category, std::string(""));
    REQUIRE_EQ(programs[2].environment, std::string("Menu"));
    CHECK(fs::exists(programs[0].shortcutPath));

    CHECK(UltraWin_ListPrograms("nope").empty());
    REQUIRE(UltraWin_DeleteEnvironment("Menu"));
    UltraWin_Shutdown();
}
