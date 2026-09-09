// Apps/UltraWinSetup/main.cpp
// ultrawin-setup — command-line driver for the UltraWin VM tier. This is
// the tool for the Stage 2c validation run on real hardware: it probes
// the host, provisions the machine from user-supplied install media,
// boots it, reports progress, and launches programs into the guest. All
// output is plain text so a failing run can be pasted into an issue
// together with the machine directory's ultrawin-*.log files.
// Console tool: no UI elements are involved.
// Version: 0.1.0 (Stage 2c)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWin/UltraWin.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

const char* StateName(UltraWinVmState s) {
    switch (s) {
        case UltraWinVmState::NotProvisioned: return "not provisioned";
        case UltraWinVmState::Stopped: return "stopped";
        case UltraWinVmState::Running: return "running";
        case UltraWinVmState::Suspended: return "suspended";
        case UltraWinVmState::Unknown: return "unknown (QMP unreachable)";
    }
    return "?";
}

int Fail(const UltraWinResult& r) {
    std::fprintf(stderr, "error: %s (code %d)\n", r.message.c_str(),
                 static_cast<int>(r.code));
    return 1;
}

void PrintStatus() {
    auto caps = UltraWin_GetCapabilities();
    std::printf("host capabilities\n");
    std::printf("  wine:        %s %s\n", caps.wineAvailable ? "yes" : "no",
                caps.wineVersion.c_str());
    std::printf("  winetricks:  %s\n",
                caps.winetricksAvailable ? "yes" : "no");
    std::printf("  qemu:        %s %s\n", caps.qemuAvailable ? "yes" : "no",
                caps.qemuPath.c_str());
    std::printf("  kvm:         %s\n", caps.kvmAvailable ? "yes" : "no");
    std::printf("  virtiofsd:   %s\n",
                caps.virtiofsdAvailable ? "yes" : "no");
    std::printf("  remoteapp:   %s (FreeRDP %s)\n",
                caps.remoteAppSupported ? "yes" : "no",
                caps.remoteAppSupported ? "linked" : "not built in");
    std::printf("  vm tier:     %s\n", caps.vmTierAvailable ? "yes" : "no");

    UltraWinVmInfo info;
    if (UltraWin_VmGetInfo(&info)) {
        std::printf("machine\n");
        std::printf("  state:       %s\n", StateName(info.state));
        std::printf("  directory:   %s\n", info.vmDirectory.c_str());
        std::printf("  installed:   %s\n",
                    info.windowsInstalled ? "yes" : "no (boots install media)");
        if (info.qemuPid > 0) {
            std::printf("  qemu pid:    %lld (%s)\n",
                        static_cast<long long>(info.qemuPid),
                        info.kvm ? "KVM" : "TCG");
            std::printf("  rdp port:    127.0.0.1:%d\n", info.rdpHostPort);
            std::printf("  home share:  %s\n",
                        info.homeShared ? "virtiofs (tag ultrawin_home)"
                                        : "off");
        }
    }
}

int Usage() {
    std::printf(
        "ultrawin-setup — drive the UltraWin VM tier (Stage 2c "
        "validation)\n\n"
        "  ultrawin-setup status\n"
        "  ultrawin-setup provision --windows-iso <path> [--drivers-iso "
        "<path>]\n"
        "                 [--disk-gb N] [--memory-mb N] [--cpus N]\n"
        "  ultrawin-setup start | stop | kill | suspend | resume\n"
        "  ultrawin-setup watch          (poll state until Windows is "
        "installed)\n"
        "  ultrawin-setup run <program>  (VM tier: guest path, ||alias, or "
        "host path under $HOME)\n"
        "  ultrawin-setup run-wine <path.exe|.msi> [args...]   (Wine "
        "tier)\n"
        "  ultrawin-setup component <environment> <verb>       (e.g. "
        "vcrun2019, corefonts)\n"
        "  ultrawin-setup programs       (installed Start-Menu programs, "
        "all environments)\n\n"
        "Install media: a Windows 10/11 Pro ISO (your license) and the\n"
        "virtio-win drivers ISO "
        "(fedorapeople.org/groups/virt/virtio-win/).\n"
        "Logs land in the machine directory (ultrawin-qemu.log, "
        "ultrawin-virtiofsd.log).\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return Usage();
    const std::string cmd = argv[1];
    auto init = UltraWin_Initialize();
    if (!init) return Fail(init);

    if (cmd == "status") {
        PrintStatus();
        return 0;
    }
    if (cmd == "provision") {
        UltraWinVmOptions opt;
        for (int i = 2; i + 1 < argc; i += 2) {
            const std::string key = argv[i], value = argv[i + 1];
            if (key == "--windows-iso") opt.windowsIsoPath = value;
            else if (key == "--drivers-iso") opt.driversIsoPath = value;
            else if (key == "--disk-gb") opt.diskSizeGB = std::atoi(value.c_str());
            else if (key == "--memory-mb") opt.memoryMB = std::atoi(value.c_str());
            else if (key == "--cpus") opt.cpus = std::atoi(value.c_str());
            else return Usage();
        }
        auto r = UltraWin_VmProvision(opt);
        if (!r) return Fail(r);
        std::printf("provisioned.\n");
        PrintStatus();
        return 0;
    }
    if (cmd == "start" || cmd == "stop" || cmd == "kill" ||
        cmd == "suspend" || cmd == "resume") {
        UltraWinResult r =
            cmd == "start"     ? UltraWin_VmStart()
            : cmd == "stop"    ? UltraWin_VmStop()
            : cmd == "kill"    ? UltraWin_VmKill()
            : cmd == "suspend" ? UltraWin_VmSuspend()
                               : UltraWin_VmResume();
        if (!r) return Fail(r);
        PrintStatus();
        return 0;
    }
    if (cmd == "watch") {
        // Poll until the RDP probe marks Windows installed (the state
        // query flips the manifest) — the signal the unattended install
        // finished. Ctrl-C to abort.
        for (;;) {
            UltraWinVmInfo info;
            auto r = UltraWin_VmGetInfo(&info);
            if (!r) return Fail(r);
            std::printf("state=%s installed=%s\n", StateName(info.state),
                        info.windowsInstalled ? "yes" : "no");
            std::fflush(stdout);
            if (info.windowsInstalled) {
                std::printf("Windows is up. RemoteApp launches are ready "
                            "(ultrawin-setup run \"||notepad\").\n");
                return 0;
            }
            if (info.state == UltraWinVmState::Stopped) {
                std::printf("machine stopped — check %s/ultrawin-qemu.log\n",
                            info.vmDirectory.c_str());
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    }
    if ((cmd == "run" || cmd == "run-wine") && argc >= 3) {
        UltraWinRunOptions opt;
        opt.forceTier =
            cmd == "run" ? UltraWinTier::Vm : UltraWinTier::Wine;
        for (int i = 3; i < argc; ++i) opt.arguments.push_back(argv[i]);
        UltraWinHandle h = UltraWinInvalidHandle;
        auto r = UltraWin_RunApp(argv[2], opt, &h);
        if (!r) return Fail(r);
        std::printf("launched (handle %llu) — waiting for the program to "
                    "end...\n",
                    static_cast<unsigned long long>(h));
        int code = -1;
        UltraWin_WaitApp(h, 0, &code);
        std::printf("ended (exit code %d).\n", code);
        return 0;
    }
    if (cmd == "component" && argc == 4) {
        std::printf("installing '%s' into environment '%s' — downloads "
                    "may take a while...\n",
                    argv[3], argv[2]);
        std::fflush(stdout);
        auto r = UltraWin_InstallComponent(argv[2], argv[3]);
        if (!r) return Fail(r);
        std::printf("installed. components now in '%s':\n", argv[2]);
        for (const auto& c : UltraWin_ListComponents(argv[2]))
            std::printf("  %s\n", c.c_str());
        return 0;
    }
    if (cmd == "programs") {
        auto environments = UltraWin_ListEnvironments();
        for (const auto& env : environments) {
            for (const auto& p : UltraWin_ListPrograms(env.name)) {
                std::printf("%-16s %-28s %s\n", env.name.c_str(),
                            (p.category.empty() ? p.name
                                                : p.category + "/" + p.name)
                                .c_str(),
                            p.shortcutPath.c_str());
            }
        }
        if (environments.empty()) std::printf("no environments yet.\n");
        return 0;
    }
    return Usage();
}
