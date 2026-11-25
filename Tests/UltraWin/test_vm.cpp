// Tests/UltraWin/test_vm.cpp
// VM-tier backbone coverage. Argument validation and answer-file
// generation run everywhere; the lifecycle test boots a REAL QEMU (TCG,
// no KVM needed, empty disk — no Windows required) and drives it through
// QMP: start -> Running -> Suspend/Resume -> Kill. Skips when QEMU is not
// installed, exactly like the real-wine test.
// Version: 0.1.1 - the QMP client's socket: nothing open after a failed Connect, one
//                  session after a second
// Version: 0.1.0 (Stage 2a)
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraWinInternal.h"
#include "UltraWinQmp.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <functional>
#include <thread>
#include <vector>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

namespace fs = std::filesystem;

namespace {

std::string ScratchRoot() {
    static std::string root = [] {
        std::string r = fs::temp_directory_path() /
                        ("ultrawin-vm-" + std::to_string(getpid()));
        fs::create_directories(UltraCanvas::PathFromUtf8(r));
        return r;
    }();
    return root;
}

UltraWinConfig VmConfig() {
    UltraWinConfig cfg;
    cfg.environmentsRoot = ScratchRoot() + "/environments";
    cfg.vmDirectory = ScratchRoot() + "/vm";
    cfg.vmAllowWithoutKvm = true;  // container/CI hosts have no /dev/kvm
    cfg.vmRdpHostPort = 23389;     // clear of any real RDP forward
    cfg.vmStartTimeoutSeconds = 60;
    return cfg;
}

}  // namespace

TEST(vm_argument_validation) {
    UltraWin_Shutdown();
    REQUIRE_EQ(UltraWin_VmProvision({}).code,
               UltraWinResultCode::NotInitialized);
    REQUIRE_EQ(UltraWin_VmGetState(), UltraWinVmState::NotProvisioned);

    REQUIRE(UltraWin_Initialize(VmConfig()));
    UltraWinVmOptions tiny;
    tiny.diskSizeGB = 1;
    REQUIRE_EQ(UltraWin_VmProvision(tiny).code,
               UltraWinResultCode::InvalidArgument);
    UltraWinVmOptions badIso;
    badIso.windowsIsoPath = "/definitely/not/there.iso";
    REQUIRE_EQ(UltraWin_VmProvision(badIso).code,
               UltraWinResultCode::FileNotFound);

    // Nothing provisioned: lifecycle calls answer accordingly.
    REQUIRE_EQ(UltraWin_VmStart().code,
               UltraWinResultCode::VmNotProvisioned);
    REQUIRE_EQ(UltraWin_VmStop().code, UltraWinResultCode::VmNotRunning);
    REQUIRE_EQ(UltraWin_VmSuspend().code, UltraWinResultCode::VmNotRunning);
    REQUIRE_EQ(UltraWin_VmGetState(), UltraWinVmState::NotProvisioned);
    UltraWin_Shutdown();
}

TEST(vm_autounattend_generation) {
    using namespace ultrawin_internal;
    std::string xml = GenerateAutounattendXml("ultra", "secret", 'u');
    CHECK(xml.find("<?xml") == 0);
    // The guest-side switches the RemoteApp integration depends on.
    CHECK(xml.find("fDenyTSConnections") != std::string::npos);
    CHECK(xml.find("TSAppAllowList") != std::string::npos);
    CHECK(xml.find("fDisabledAllowList") != std::string::npos);
    CHECK(xml.find("BypassTPMCheck") != std::string::npos);
    CHECK(xml.find("<Name>ultra</Name>") != std::string::npos);
    CHECK(xml.find("secret") != std::string::npos);
    CHECK(xml.find("</unattend>") != std::string::npos);
    // Stage 2c: virtio storage drivers reach WinPE (virtio system disk),
    // guest tools install, and the share mounts as the unified letter.
    CHECK(xml.find("PnpCustomizationsWinPE") != std::string::npos);
    CHECK(xml.find("DriverPaths") != std::string::npos);
    CHECK(xml.find("virtio-win-guest-tools.exe") != std::string::npos);
    CHECK(xml.find("VirtioFsSvc") != std::string::npos);
    CHECK(xml.find("\"U:\"") != std::string::npos);   // canonicalised 'u'
    CHECK(xml.find("@HOMEDRIVE@") == std::string::npos);
}

TEST(vm_provision_and_lifecycle_with_real_qemu) {
    REQUIRE(UltraWin_Initialize(VmConfig()));
    if (ultrawin_internal::FindQemuBinary().empty() ||
        ultrawin_internal::FindQemuImgBinary().empty())
        SKIP("no QEMU on this host");

    UltraWinVmOptions opt;
    opt.diskSizeGB = 24;   // sparse qcow2 — only megabytes on disk
    opt.memoryMB = 1024;
    opt.cpus = 1;
    REQUIRE(UltraWin_VmProvision(opt));
    REQUIRE_EQ(UltraWin_VmGetState(), UltraWinVmState::Stopped);
    CHECK(fs::exists(UltraCanvas::PathFromUtf8(ScratchRoot() + "/vm/disk.qcow2")));
    CHECK(fs::exists(UltraCanvas::PathFromUtf8(ScratchRoot() + "/vm/unattend/autounattend.xml")));

    // Re-provision with different sizes keeps the existing disk.
    UltraWinVmOptions again;
    again.diskSizeGB = 32;
    REQUIRE(UltraWin_VmProvision(again));

    // Boot (TCG; empty disk just loops in firmware — QMP still runs the
    // machine) and drive it through its states.
    auto started = UltraWin_VmStart();
    REQUIRE(started);
    REQUIRE_EQ(UltraWin_VmStart().code,
               UltraWinResultCode::VmAlreadyRunning);
    REQUIRE_EQ(UltraWin_VmGetState(), UltraWinVmState::Running);

    UltraWinVmInfo info;
    REQUIRE(UltraWin_VmGetInfo(&info));
    CHECK(info.qemuPid > 0);
    REQUIRE_EQ(info.rdpHostPort, 23389);

    REQUIRE(UltraWin_VmSuspend());
    REQUIRE_EQ(UltraWin_VmGetState(), UltraWinVmState::Suspended);
    REQUIRE(UltraWin_VmResume());
    REQUIRE_EQ(UltraWin_VmGetState(), UltraWinVmState::Running);

    REQUIRE(UltraWin_VmKill());
    REQUIRE_EQ(UltraWin_VmGetState(), UltraWinVmState::Stopped);
    UltraWin_Shutdown();
}

// The QMP client and its socket. Connect() left the socket of a connection
// that failed open until the destructor, and opened a new one over it when
// asked again - on the same object each try lost one. Now nothing stays open
// after a false, and a session the client had is closed before the next.
namespace {

int OpenDescriptors() {
    int count = 0;
    std::error_code ec;
    for (auto it = fs::directory_iterator("/proc/self/fd", ec);
         !ec && it != fs::directory_iterator(); it.increment(ec))
        ++count;
    return count;
}

// A stand-in for QEMU's control socket: greets, and answers every line with
// an empty return. Counts the sessions that the client ended.
struct FakeQmp {
    std::string path;
    int listener = -1;
    std::thread worker;
    std::atomic<int> accepted{0};
    std::atomic<int> closedByClient{0};
    std::atomic<bool> stop{false};

    bool Start(const std::string& socketPath) {
        path = socketPath;
        listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (listener < 0) return false;
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        unlink(path.c_str());
        if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
        if (listen(listener, 4) != 0) return false;
        worker = std::thread([this] { Serve(); });
        return true;
    }

    void Serve() {
        std::vector<int> sessions;
        while (!stop.load()) {
            std::vector<pollfd> fds;
            fds.push_back({listener, POLLIN, 0});
            for (int s : sessions) fds.push_back({s, POLLIN, 0});
            if (poll(fds.data(), fds.size(), 50) <= 0) continue;
            if (fds[0].revents & POLLIN) {
                const int session = accept(listener, nullptr, nullptr);
                if (session >= 0) {
                    const char greeting[] = "{\"QMP\":{\"version\":{}}}\n";
                    (void)!write(session, greeting, sizeof(greeting) - 1);
                    sessions.push_back(session);
                    ++accepted;
                }
            }
            for (size_t i = 1; i < fds.size(); ++i) {
                if (!fds[i].revents) continue;
                char buffer[512];
                const ssize_t n = read(fds[i].fd, buffer, sizeof(buffer));
                if (n <= 0) {
                    close(fds[i].fd);
                    sessions.erase(std::find(sessions.begin(), sessions.end(), fds[i].fd));
                    ++closedByClient;
                    continue;
                }
                const char answer[] = "{\"return\":{}}\n";
                (void)!write(fds[i].fd, answer, sizeof(answer) - 1);
            }
        }
        for (int s : sessions) close(s);
    }

    ~FakeQmp() {
        stop = true;
        if (worker.joinable()) worker.join();
        if (listener >= 0) close(listener);
        if (!path.empty()) unlink(path.c_str());
    }
};

bool WaitUntil(const std::function<bool()>& done) {
    for (int i = 0; i < 200 && !done(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return done();
}

}  // namespace

TEST(qmp_connect_leaves_nothing_open_when_it_fails) {
    const std::string nowhere = ScratchRoot() + "/no-such-qmp.sock";
    ultrawin_internal::QmpClient qmp;
    REQUIRE(!qmp.Connect(nowhere, 50));   // once, so that anything lazy is open
    const int before = OpenDescriptors();
    for (int i = 0; i < 5; ++i) REQUIRE(!qmp.Connect(nowhere, 50));
    REQUIRE_EQ(OpenDescriptors(), before);
    // Not connected: a command says so at once instead of waiting out its time.
    const auto asked = std::chrono::steady_clock::now();
    REQUIRE(!qmp.Execute("query-status", 5000));
    CHECK(std::chrono::steady_clock::now() - asked < std::chrono::seconds(1));
    CHECK(qmp.LastError().find("not connected") != std::string::npos);
}

TEST(qmp_connect_again_closes_the_session_before) {
    FakeQmp server;
    REQUIRE(server.Start(ScratchRoot() + "/fake-qmp.sock"));
    {
        ultrawin_internal::QmpClient qmp;
        REQUIRE(qmp.Connect(server.path, 3000));
        REQUIRE(qmp.Execute("query-status", 3000));
        const int connectedWith = OpenDescriptors();
        // The same client, connected again: the first session ends, one is open.
        REQUIRE(qmp.Connect(server.path, 3000));
        REQUIRE(WaitUntil([&] { return server.closedByClient.load() == 1; }));
        REQUIRE_EQ(server.accepted.load(), 2);
        REQUIRE_EQ(OpenDescriptors(), connectedWith);
        REQUIRE(qmp.Execute("query-status", 3000));

        qmp.Disconnect();
        REQUIRE(WaitUntil([&] { return server.closedByClient.load() == 2; }));
        REQUIRE(!qmp.Execute("query-status", 3000));
    }
}
