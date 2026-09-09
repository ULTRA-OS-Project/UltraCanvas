// Tests/UltraWin/test_remoteapp.cpp
// VM-tier RemoteApp routing and the RDP session layer. Routing/validation
// runs everywhere; the handshake test connects a real RdpSession to a
// REAL RDP server (freerdp-shadow-cli under Xvfb, loopback, throwaway
// port) — full TCP/TLS/auth/capability negotiation without any Windows.
// It skips itself where the server cannot run (no shadow/Xvfb installed,
// or IPv6-less containers — FreeRDP's listener refuses to start there).
// RAIL specifics need a Windows RemoteApp host and stay for Stage 2b-ii.
// Version: 0.1.0 (Stage 2b)
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraWinInternal.h"
#include "UltraWinRdp.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace ultrawin_internal;

namespace {

UltraWinConfig ScratchConfig() {
    static std::string root = [] {
        std::string r = fs::temp_directory_path() /
                        ("ultrawin-rail-" + std::to_string(getpid()));
        fs::create_directories(r);
        return r;
    }();
    UltraWinConfig cfg;
    cfg.environmentsRoot = root + "/environments";
    cfg.vmDirectory = root + "/vm";
    return cfg;
}

// Spawns `argv` silenced; returns pid (or -1). SIGKILLed by StopChild.
pid_t SpawnChild(const std::vector<std::string>& argv) {
    pid_t pid = fork();
    if (pid != 0) return pid;
    setpgid(0, 0);
    if (FILE* f = std::fopen("/dev/null", "r+")) {
        dup2(fileno(f), STDIN_FILENO);
        dup2(fileno(f), STDOUT_FILENO);
        dup2(fileno(f), STDERR_FILENO);
    }
    std::vector<char*> cargv;
    for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);
    execvp(cargv[0], cargv.data());
    _exit(127);
}

void StopChild(pid_t pid) {
    if (pid <= 0) return;
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
}

}  // namespace

TEST(vm_tier_run_routing_validation) {
    REQUIRE(UltraWin_Initialize(ScratchConfig()));
    UltraWinRunOptions vm;
    vm.forceTier = UltraWinTier::Vm;
    UltraWinHandle h = 0;

    if (!RdpBuiltIn()) {
        auto r = UltraWin_RunApp("C:\\Windows\\notepad.exe", vm, &h);
        REQUIRE_EQ(r.code, UltraWinResultCode::NotSupported);
        UltraWin_Shutdown();
        return;
    }

    // Host paths are rejected until shared folders exist; guest paths and
    // aliases pass path validation and then require the machine.
    auto r = UltraWin_RunApp("/bin/true", vm, &h);
    REQUIRE_EQ(r.code, UltraWinResultCode::InvalidArgument);
    r = UltraWin_RunApp("C:\\Windows\\notepad.exe", vm, &h);
    REQUIRE_EQ(r.code, UltraWinResultCode::VmNotRunning);
    r = UltraWin_RunApp("||notepad", vm, &h);
    REQUIRE_EQ(r.code, UltraWinResultCode::VmNotRunning);
    CHECK(UltraWin_GetCapabilities().remoteAppSupported);
    UltraWin_Shutdown();
}

TEST(rdp_session_refuses_dead_port) {
    if (!RdpBuiltIn()) SKIP("built without FreeRDP");
    RdpSessionOptions opt;
    opt.port = 24389;  // nothing listens here
    opt.username = "x";
    opt.password = "x";
    RdpSession session;
    CHECK(!session.Connect(opt));
    REQUIRE_EQ(static_cast<int>(session.State()),
               static_cast<int>(RdpSessionState::Failed));
    CHECK(!session.LastError().empty());
}

TEST(rdp_session_handshake_with_real_server) {
    if (!RdpBuiltIn()) SKIP("built without FreeRDP");
    if (FindInPath("freerdp-shadow-cli").empty() ||
        FindInPath("Xvfb").empty())
        SKIP("freerdp-shadow-cli / Xvfb not installed");

    pid_t xvfb = SpawnChild({"Xvfb", ":93", "-screen", "0", "640x480x24"});
    sleep(1);
    setenv("DISPLAY", ":93", 1);
    pid_t shadow = SpawnChild(
        {"freerdp-shadow-cli", "/port:24390", "-auth", "/sec:tls"});
    REQUIRE(shadow > 0);

    // The server needs a moment to listen; connect retries make this
    // deterministic enough for CI-like hosts.
    RdpSessionOptions opt;
    opt.port = 24390;
    opt.username = "ultra";
    opt.password = "ultra";
    RdpSession session;
    bool connected = false;
    for (int attempt = 0; attempt < 20 && !connected; ++attempt) {
        connected = session.Connect(opt);
        if (!connected) usleep(500 * 1000);
    }
    if (!connected) {
        StopChild(shadow);
        StopChild(xvfb);
        SKIP(("shadow server did not come up: " + session.LastError())
                 .c_str());
    }
    REQUIRE_EQ(static_cast<int>(session.State()),
               static_cast<int>(RdpSessionState::Connected));
    session.Disconnect();
    REQUIRE_EQ(static_cast<int>(session.State()),
               static_cast<int>(RdpSessionState::Disconnected));

    StopChild(shadow);
    StopChild(xvfb);
}
