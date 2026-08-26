// include/UltraWin/UltraWinVm.h
// Stage 2 VM tier, part 1: the lifecycle of UltraWin's single shared
// Windows guest — a QEMU/KVM virtual machine controlled over its QMP
// socket. The guest runs headless; the desktop is never shown — Windows
// application windows reach ULTRA OS through FreeRDP RemoteApp (next part
// of Stage 2). Provisioning prepares the machine (disk image, unattended
// answer file, machine manifest); installing Windows requires user-supplied
// install media and a Windows license.
// Version: 0.1.0 (Stage 2a)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraWinCore.h"

enum class UltraWinVmState {
    NotProvisioned,  // no machine manifest under the VM directory
    Stopped,         // provisioned, no QEMU process
    Running,         // QEMU up, vCPUs executing
    Suspended,       // QEMU up, vCPUs paused (QMP "stop")
    Unknown          // QEMU process up but QMP unreachable
};

// Options for UltraWin_VmProvision. Every size has a working default.
struct UltraWinVmOptions {
    // User-supplied Windows installation ISO (Pro/Enterprise — the RemoteApp
    // host role needs it). Optional at provision time: without it the
    // machine is prepared and the ISO can be added later by re-provisioning.
    std::string windowsIsoPath;

    // virtio-win guest drivers ISO (disk/net drivers during setup). Optional.
    std::string driversIsoPath;

    int diskSizeGB = 64;   // qcow2 grows on demand — this is the cap
    int memoryMB = 4096;
    int cpus = 4;
};

struct UltraWinVmInfo {
    UltraWinVmState state = UltraWinVmState::NotProvisioned;
    std::string vmDirectory;   // machine home (manifest, disk, sockets)
    std::string diskPath;      // qcow2 system disk
    int64_t qemuPid = 0;       // 0 when not running
    bool kvm = false;          // started with KVM acceleration
    int rdpHostPort = 0;       // host loopback port forwarded to guest 3389
    bool homeShared = false;   // this run exports home over virtiofs
    // Windows setup completed: flipped automatically the first time the
    // guest's RDP port answers while the machine runs; later boots then
    // skip the install media. (Manifest-backed.)
    bool windowsInstalled = false;
};

// Prepare the machine under UltraWinConfig::vmDirectory: create the qcow2
// disk (via qemu-img), write the unattended-install answer file
// (autounattend.xml: RDP + RemoteApp allow-list enabled, TPM/RAM setup
// checks bypassed) and the machine manifest. Fails with VmUnavailable when
// no QEMU is found, EnvironmentExists-like VmAlreadyProvisioned when a
// manifest already exists (re-provision with different ISOs is allowed —
// only sizes are fixed after creation).
UltraWinResult UltraWin_VmProvision(const UltraWinVmOptions& options = {});

// Boot the guest headless (KVM when /dev/kvm is usable, TCG otherwise).
// While install media is configured and Windows setup has not completed,
// the machine boots from the ISO with the answer file attached. Returns
// once QMP is reachable; the guest OS keeps booting in the background.
UltraWinResult UltraWin_VmStart();

// Graceful stop: ACPI powerdown via QMP, bounded wait, then hard quit.
// VmKill quits immediately (virtual power cord).
UltraWinResult UltraWin_VmStop();
UltraWinResult UltraWin_VmKill();

// Pause / resume the vCPUs (QMP stop/cont) — the cheap way to keep the
// guest resident but idle between application launches.
UltraWinResult UltraWin_VmSuspend();
UltraWinResult UltraWin_VmResume();

UltraWinVmState UltraWin_VmGetState();
UltraWinResult UltraWin_VmGetInfo(UltraWinVmInfo* out);
