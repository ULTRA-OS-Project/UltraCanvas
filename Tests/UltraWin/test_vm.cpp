// Tests/UltraWin/test_vm.cpp
// VM-tier backbone coverage. Argument validation and answer-file
// generation run everywhere; the lifecycle test boots a REAL QEMU (TCG,
// no KVM needed, empty disk — no Windows required) and drives it through
// QMP: start -> Running -> Suspend/Resume -> Kill. Skips when QEMU is not
// installed, exactly like the real-wine test.
// Version: 0.1.0 (Stage 2a)
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraWinInternal.h"

#include <filesystem>

#include <unistd.h>

namespace fs = std::filesystem;

namespace {

std::string ScratchRoot() {
    static std::string root = [] {
        std::string r = fs::temp_directory_path() /
                        ("ultrawin-vm-" + std::to_string(getpid()));
        fs::create_directories(r);
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
    std::string xml = GenerateAutounattendXml("ultra", "secret");
    CHECK(xml.find("<?xml") == 0);
    // The guest-side switches the RemoteApp integration depends on.
    CHECK(xml.find("fDenyTSConnections") != std::string::npos);
    CHECK(xml.find("TSAppAllowList") != std::string::npos);
    CHECK(xml.find("fDisabledAllowList") != std::string::npos);
    CHECK(xml.find("BypassTPMCheck") != std::string::npos);
    CHECK(xml.find("<Name>ultra</Name>") != std::string::npos);
    CHECK(xml.find("secret") != std::string::npos);
    CHECK(xml.find("</unattend>") != std::string::npos);
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
    CHECK(fs::exists(ScratchRoot() + "/vm/disk.qcow2"));
    CHECK(fs::exists(ScratchRoot() + "/vm/unattend/autounattend.xml"));

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
