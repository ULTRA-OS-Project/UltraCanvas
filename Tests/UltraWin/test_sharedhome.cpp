// Tests/UltraWin/test_sharedhome.cpp
// Shared-home coverage: host->guest path translation (pure) and, with a
// real QEMU + virtiofsd, a boot with the vhost-user-fs home share
// attached — QEMU validates the device configuration at startup, so a
// successful boot proves the wiring. Skips without QEMU/virtiofsd.
// Version: 0.1.0 (Stage 2b)
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraWinInternal.h"

#include <cstdlib>
#include <filesystem>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace ultrawin_internal;

namespace {

std::string ScratchRoot() {
    static std::string root = [] {
        std::string r = fs::temp_directory_path() /
                        ("ultrawin-share-" + std::to_string(getpid()));
        fs::create_directories(r);
        return r;
    }();
    return root;
}

UltraWinConfig VmConfig() {
    UltraWinConfig cfg;
    cfg.environmentsRoot = ScratchRoot() + "/environments";
    cfg.vmDirectory = ScratchRoot() + "/vm";
    cfg.vmAllowWithoutKvm = true;
    cfg.vmRdpHostPort = 23391;
    cfg.vmStartTimeoutSeconds = 60;
    return cfg;
}

}  // namespace

TEST(host_to_guest_path_translation) {
    REQUIRE_EQ(HostToGuestPath("/home/u/Docs/a.exe", "/home/u", 'U'),
               std::string("U:\\Docs\\a.exe"));
    REQUIRE_EQ(HostToGuestPath("/home/u", "/home/u", 'u'),
               std::string("U:\\"));
    REQUIRE_EQ(HostToGuestPath("/home/u/", "/home/u/", 'U'),
               std::string("U:\\"));
    // Prefix boundaries and invalid inputs.
    REQUIRE_EQ(HostToGuestPath("/home/ux/a.exe", "/home/u", 'U'),
               std::string(""));
    REQUIRE_EQ(HostToGuestPath("/etc/passwd", "/home/u", 'U'),
               std::string(""));
    REQUIRE_EQ(HostToGuestPath("relative", "/home/u", 'U'),
               std::string(""));
    REQUIRE_EQ(HostToGuestPath("/home/u/a", "", 'U'), std::string(""));
    REQUIRE_EQ(HostToGuestPath("/home/u/a", "/home/u", 0),
               std::string(""));
}

TEST(tcp_probe_semantics) {
    // Closed port: refused outright.
    CHECK(!ProbeTcpPort("127.0.0.1", 24397, 500));
    // A held-open, silent listener (RDP behaves this way) is "open".
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    REQUIRE(fd >= 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(24397);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0);
    REQUIRE(listen(fd, 1) == 0);
    CHECK(ProbeTcpPort("127.0.0.1", 24397, 500));
    close(fd);
}

TEST(vm_boot_with_home_share) {
    REQUIRE(UltraWin_Initialize(VmConfig()));
    if (FindQemuBinary().empty() || FindQemuImgBinary().empty())
        SKIP("no QEMU on this host");
    if (FindVirtiofsdBinary().empty())
        SKIP("no virtiofsd on this host");
    const char* home = std::getenv("HOME");
    if (!home || *home != '/') SKIP("no HOME in this environment");

    UltraWinVmOptions opt;
    opt.diskSizeGB = 24;
    opt.memoryMB = 1024;
    opt.cpus = 1;
    REQUIRE(UltraWin_VmProvision(opt));
    auto started = UltraWin_VmStart();
    REQUIRE(started);

    UltraWinVmInfo info;
    REQUIRE(UltraWin_VmGetInfo(&info));
    CHECK(info.homeShared);  // virtiofsd up and the device accepted
    REQUIRE_EQ(UltraWin_VmGetState(), UltraWinVmState::Running);

    REQUIRE(UltraWin_VmKill());
    REQUIRE(UltraWin_VmGetInfo(&info));
    CHECK(!info.homeShared);  // share daemon ends with the machine
    UltraWin_Shutdown();
}
