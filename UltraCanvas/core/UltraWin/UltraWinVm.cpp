// core/UltraWin/UltraWinVm.cpp
// Lifecycle of UltraWin's single shared Windows guest: provisioning (qcow2
// disk, unattended answer file, machine manifest), spawn of a headless
// QEMU (KVM when usable), and control over the machine's QMP socket.
// The guest desktop is never displayed; application windows arrive via
// FreeRDP RemoteApp (Stage 2b) through the forwarded RDP port.
// Version: 0.1.0 (Stage 2a)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWinInternal.h"
#include "UltraWinQmp.h"

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ultrawin_internal {

std::string FindInPath(const std::string& name) {
    const char* pathEnv = std::getenv("PATH");
    std::istringstream dirs(pathEnv ? pathEnv : "");
    std::string dir;
    while (std::getline(dirs, dir, ':')) {
        if (dir.empty()) continue;
        std::string full = dir + "/" + name;
        if (access(full.c_str(), X_OK) == 0) return full;
    }
    return {};
}

std::string FindQemuBinary() {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (!g_config.qemuPath.empty()) {
            return access(g_config.qemuPath.c_str(), X_OK) == 0
                       ? g_config.qemuPath
                       : std::string();
        }
    }
    return FindInPath("qemu-system-x86_64");
}

std::string FindQemuImgBinary() { return FindInPath("qemu-img"); }

std::string FindVirtiofsdBinary() {
    // Modern virtiofsd (Rust) is not on PATH on most distros.
    std::string found = FindInPath("virtiofsd");
    if (!found.empty()) return found;
    for (const char* candidate :
         {"/usr/libexec/virtiofsd", "/usr/lib/qemu/virtiofsd"}) {
        if (access(candidate, X_OK) == 0) return candidate;
    }
    return {};
}

std::string VmDirectory() {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (!g_config.vmDirectory.empty()) return g_config.vmDirectory;
    }
    // Sibling of ".../ultrawin/environments".
    std::string envRoot = EnvironmentsRoot();
    return fs::path(envRoot).parent_path() / "vm";
}

std::string GenerateAutounattendXml(const std::string& userName,
                                    const std::string& password) {
    // EXPERIMENTAL (Stage 2c validates against real media). Answers every
    // interactive stage: disk 0 is wiped into an EFI layout, Pro edition
    // installs to it, the hardware checks are bypassed, and first logon
    // enables the RDP host role and the RemoteApp allow-list — everything
    // the FreeRDP integration needs from the guest side.
    std::ostringstream x;
    x << R"(<?xml version="1.0" encoding="utf-8"?>
<unattend xmlns="urn:schemas-microsoft-com:unattend">
  <settings pass="windowsPE">
    <component name="Microsoft-Windows-Setup" processorArchitecture="amd64"
               publicKeyToken="31bf3856ad364e35" language="neutral"
               versionScope="nonSxS">
      <RunSynchronous>
        <RunSynchronousCommand wcm:action="add"
            xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
          <Order>1</Order>
          <Path>reg add HKLM\SYSTEM\Setup\LabConfig /v BypassTPMCheck /t REG_DWORD /d 1 /f</Path>
        </RunSynchronousCommand>
        <RunSynchronousCommand wcm:action="add"
            xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
          <Order>2</Order>
          <Path>reg add HKLM\SYSTEM\Setup\LabConfig /v BypassSecureBootCheck /t REG_DWORD /d 1 /f</Path>
        </RunSynchronousCommand>
        <RunSynchronousCommand wcm:action="add"
            xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
          <Order>3</Order>
          <Path>reg add HKLM\SYSTEM\Setup\LabConfig /v BypassRAMCheck /t REG_DWORD /d 1 /f</Path>
        </RunSynchronousCommand>
      </RunSynchronous>
      <DiskConfiguration>
        <Disk wcm:action="add"
            xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
          <DiskID>0</DiskID>
          <WillWipeDisk>true</WillWipeDisk>
          <CreatePartitions>
            <CreatePartition wcm:action="add"><Order>1</Order><Type>EFI</Type><Size>256</Size></CreatePartition>
            <CreatePartition wcm:action="add"><Order>2</Order><Type>MSR</Type><Size>128</Size></CreatePartition>
            <CreatePartition wcm:action="add"><Order>3</Order><Type>Primary</Type><Extend>true</Extend></CreatePartition>
          </CreatePartitions>
          <ModifyPartitions>
            <ModifyPartition wcm:action="add"><Order>1</Order><PartitionID>1</PartitionID><Format>FAT32</Format></ModifyPartition>
            <ModifyPartition wcm:action="add"><Order>2</Order><PartitionID>3</PartitionID><Format>NTFS</Format></ModifyPartition>
          </ModifyPartitions>
        </Disk>
      </DiskConfiguration>
      <ImageInstall>
        <OSImage>
          <InstallTo><DiskID>0</DiskID><PartitionID>3</PartitionID></InstallTo>
          <InstallToAvailablePartition>false</InstallToAvailablePartition>
        </OSImage>
      </ImageInstall>
      <UserData>
        <AcceptEula>true</AcceptEula>
        <ProductKey><WillShowUI>OnError</WillShowUI></ProductKey>
      </UserData>
    </component>
  </settings>
  <settings pass="specialize">
    <component name="Microsoft-Windows-TerminalServices-LocalSessionManager"
               processorArchitecture="amd64"
               publicKeyToken="31bf3856ad364e35" language="neutral"
               versionScope="nonSxS">
      <fDenyTSConnections>false</fDenyTSConnections>
    </component>
    <component name="Networking-MPSSVC-Svc" processorArchitecture="amd64"
               publicKeyToken="31bf3856ad364e35" language="neutral"
               versionScope="nonSxS">
      <FirewallGroups>
        <FirewallGroup wcm:action="add"
            xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
          <Key>RemoteDesktop</Key>
          <Active>true</Active>
          <Group>@FirewallAPI.dll,-28752</Group>
          <Profile>all</Profile>
        </FirewallGroup>
      </FirewallGroups>
    </component>
  </settings>
  <settings pass="oobeSystem">
    <component name="Microsoft-Windows-Shell-Setup" processorArchitecture="amd64"
               publicKeyToken="31bf3856ad364e35" language="neutral"
               versionScope="nonSxS">
      <OOBE>
        <HideEULAPage>true</HideEULAPage>
        <HideLocalAccountScreen>true</HideLocalAccountScreen>
        <HideOnlineAccountScreens>true</HideOnlineAccountScreens>
        <HideWirelessSetupInOOBE>true</HideWirelessSetupInOOBE>
        <ProtectYourPC>3</ProtectYourPC>
      </OOBE>
      <UserAccounts>
        <LocalAccounts>
          <LocalAccount wcm:action="add"
              xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
            <Name>)" << userName << R"(</Name>
            <Group>Administrators</Group>
            <Password><Value>)" << password << R"(</Value><PlainText>true</PlainText></Password>
          </LocalAccount>
        </LocalAccounts>
      </UserAccounts>
      <AutoLogon>
        <Enabled>true</Enabled>
        <LogonCount>1</LogonCount>
        <Username>)" << userName << R"(</Username>
        <Password><Value>)" << password << R"(</Value><PlainText>true</PlainText></Password>
      </AutoLogon>
      <FirstLogonCommands>
        <SynchronousCommand wcm:action="add"
            xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
          <Order>1</Order>
          <CommandLine>reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services" /v fAllowUnlistedRemotePrograms /t REG_DWORD /d 1 /f</CommandLine>
        </SynchronousCommand>
        <SynchronousCommand wcm:action="add"
            xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
          <Order>2</Order>
          <CommandLine>reg add "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Terminal Server\TSAppAllowList" /v fDisabledAllowList /t REG_DWORD /d 1 /f</CommandLine>
        </SynchronousCommand>
      </FirstLogonCommands>
    </component>
  </settings>
</unattend>
)";
    return x.str();
}

// ---------------------------------------------------------------------------
// Machine state (guarded by g_mutex)
// ---------------------------------------------------------------------------

namespace {

pid_t g_vmPid = 0;         // spawned QEMU, 0 when not tracked
bool g_vmKvm = false;

const char* kManifest = "ultrawin-vm.conf";

std::string QmpSocketPath() {
    return (fs::path(VmDirectory()) / "qmp.sock").string();
}

std::map<std::string, std::string> ReadManifest(const std::string& vmDir) {
    std::map<std::string, std::string> kv;
    std::ifstream in(fs::path(vmDir) / kManifest);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}

bool WriteManifest(const std::string& vmDir,
                   const std::map<std::string, std::string>& kv) {
    std::ofstream out(fs::path(vmDir) / kManifest, std::ios::trunc);
    if (!out) return false;
    out << "# UltraWin machine manifest — KEY=VALUE per line.\n";
    for (const auto& [k, v] : kv) out << k << '=' << v << '\n';
    return static_cast<bool>(out);
}

// Collects the tracked QEMU if it exited; returns true when a live spawned
// process is being tracked. Caller holds g_mutex.
bool VmProcessAliveLocked() {
    if (g_vmPid == 0) return false;
    int status = 0;
    pid_t r = waitpid(g_vmPid, &status, WNOHANG);
    if (r == 0) return true;
    g_vmPid = 0;  // exited (or unwaitable) — no longer tracked
    return false;
}

bool KvmUsable() { return access("/dev/kvm", R_OK | W_OK) == 0; }

}  // namespace
}  // namespace ultrawin_internal

// ===========================================================================
// Public entry points
// ===========================================================================

using namespace ultrawin_internal;

UltraWinResult UltraWin_VmProvision(const UltraWinVmOptions& options) {
    if (!UltraWin_IsInitialized())
        return UltraWinResult::Error(UltraWinResultCode::NotInitialized,
                                     "call UltraWin_Initialize first");
    if (options.diskSizeGB < 24 || options.memoryMB < 1024 ||
        options.cpus < 1)
        return UltraWinResult::Error(
            UltraWinResultCode::InvalidArgument,
            "sizes below Windows minimums (disk >= 24 GB, memory >= 1024 "
            "MB, cpus >= 1)");
    for (const std::string* iso :
         {&options.windowsIsoPath, &options.driversIsoPath}) {
        if (!iso->empty() &&
            ((*iso)[0] != '/' || !fs::is_regular_file(*iso)))
            return UltraWinResult::Error(UltraWinResultCode::FileNotFound,
                                         *iso + " is not an ISO file path");
    }
    std::string qemuImg = FindQemuImgBinary();
    if (FindQemuBinary().empty() || qemuImg.empty())
        return UltraWinResult::Error(
            UltraWinResultCode::VmUnavailable,
            "QEMU is not installed (qemu-system-x86_64 + qemu-img)");

    const std::string vmDir = VmDirectory();
    std::error_code ec;
    fs::create_directories(vmDir, ec);
    if (ec)
        return UltraWinResult::Error(UltraWinResultCode::IoError,
                                     "cannot create " + vmDir);
    const std::string disk = (fs::path(vmDir) / "disk.qcow2").string();

    auto manifest = ReadManifest(vmDir);
    const bool existed = !manifest.empty();
    if (existed && fs::exists(disk)) {
        // Re-provision only swaps the install media; the disk (and its
        // sizes) stay — deleting a Windows install must be explicit.
        manifest["windows_iso"] = options.windowsIsoPath;
        manifest["drivers_iso"] = options.driversIsoPath;
        if (!WriteManifest(vmDir, manifest))
            return UltraWinResult::Error(UltraWinResultCode::IoError,
                                         "cannot write machine manifest");
        return UltraWinResult::Ok();
    }

    // qemu-img create -f qcow2 disk.qcow2 <N>G — reusing the wine command
    // runner (it is just fork/exec/wait with output capture).
    const std::string createLog =
        (fs::path(vmDir) / "ultrawin-provision.log").string();
    int rc = RunWineCommand(
        qemuImg,
        {"create", "-f", "qcow2", disk,
         std::to_string(options.diskSizeGB) + "G"},
        vmDir, /*suppressPrompts=*/false, /*timeoutSeconds=*/120, {},
        createLog);
    if (rc != 0)
        return UltraWinResult::Error(
            UltraWinResultCode::VmUnavailable,
            "qemu-img could not create the disk (details: " + createLog +
                ")");

    // The answer file lives in its own subdirectory: QEMU attaches that
    // directory as a virtual FAT volume during installation, whose root is
    // where Windows Setup searches for autounattend.xml.
    fs::create_directories(fs::path(vmDir) / "unattend", ec);
    {
        std::ofstream out(fs::path(vmDir) / "unattend" /
                          "autounattend.xml");
        out << GenerateAutounattendXml("ultra", "ultra");
        if (!out)
            return UltraWinResult::Error(UltraWinResultCode::IoError,
                                         "cannot write autounattend.xml");
    }

    manifest["disk_gb"] = std::to_string(options.diskSizeGB);
    manifest["memory_mb"] = std::to_string(options.memoryMB);
    manifest["cpus"] = std::to_string(options.cpus);
    manifest["windows_iso"] = options.windowsIsoPath;
    manifest["drivers_iso"] = options.driversIsoPath;
    manifest["installed"] = "0";
    if (!WriteManifest(vmDir, manifest))
        return UltraWinResult::Error(UltraWinResultCode::IoError,
                                     "cannot write machine manifest");
    return UltraWinResult::Ok();
}

UltraWinResult UltraWin_VmStart() {
    if (!UltraWin_IsInitialized())
        return UltraWinResult::Error(UltraWinResultCode::NotInitialized,
                                     "call UltraWin_Initialize first");
    const std::string vmDir = VmDirectory();
    auto manifest = ReadManifest(vmDir);
    if (manifest.empty())
        return UltraWinResult::Error(UltraWinResultCode::VmNotProvisioned,
                                     "run UltraWin_VmProvision first");
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (VmProcessAliveLocked())
            return UltraWinResult::Error(UltraWinResultCode::VmAlreadyRunning,
                                         "the machine is already running");
    }
    std::string qemu = FindQemuBinary();
    if (qemu.empty())
        return UltraWinResult::Error(UltraWinResultCode::VmUnavailable,
                                     "QEMU is not installed");

    UltraWinConfig cfg = UltraWin_GetConfig();
    const bool kvm = KvmUsable();
    if (!kvm && !cfg.vmAllowWithoutKvm)
        return UltraWinResult::Error(
            UltraWinResultCode::VmUnavailable,
            "/dev/kvm is not usable (set vmAllowWithoutKvm to boot with "
            "slow software emulation)");

    const std::string disk = (fs::path(vmDir) / "disk.qcow2").string();
    const std::string qmpSock = QmpSocketPath();
    std::error_code ec;
    fs::remove(qmpSock, ec);  // stale socket from a previous run

    const int rdpPort =
        cfg.vmRdpHostPort > 0 ? cfg.vmRdpHostPort : 13389;
    std::vector<std::string> args = {
        "-name", "UltraWin",
        "-machine", "q35",
        "-accel", kvm ? "kvm" : "tcg",
        "-cpu", kvm ? "host" : "qemu64",
        "-smp", manifest.count("cpus") ? manifest["cpus"] : "4",
        "-m", manifest.count("memory_mb") ? manifest["memory_mb"] : "4096",
        "-drive", "file=" + disk + ",if=virtio,format=qcow2",
        "-netdev",
        "user,id=un0,hostfwd=tcp:127.0.0.1:" + std::to_string(rdpPort) +
            "-:3389",
        "-device", "virtio-net-pci,netdev=un0",
        "-qmp", "unix:" + qmpSock + ",server,nowait",
        "-display", "none",
    };
    // Until Windows is installed, boot from the install media with the
    // answer-file directory attached as a virtual FAT volume.
    if (manifest["installed"] != "1" && !manifest["windows_iso"].empty()) {
        args.insert(args.end(),
                    {"-drive",
                     "file=" + manifest["windows_iso"] +
                         ",media=cdrom,readonly=on"});
        if (!manifest["drivers_iso"].empty())
            args.insert(args.end(),
                        {"-drive",
                         "file=" + manifest["drivers_iso"] +
                             ",media=cdrom,readonly=on"});
        args.insert(
            args.end(),
            {"-drive",
             "file=fat:rw:" + (fs::path(vmDir) / "unattend").string() +
                 ",format=raw,if=none,id=unattend",
             "-device", "usb-storage,drive=unattend", "-usb", "-boot",
             "once=d"});
    }

    pid_t pid = fork();
    if (pid < 0)
        return UltraWinResult::Error(UltraWinResultCode::LaunchFailed,
                                     "fork failed");
    if (pid == 0) {
        setpgid(0, 0);
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) dup2(devnull, STDIN_FILENO);
        int log = open((fs::path(vmDir) / "ultrawin-qemu.log").c_str(),
                       O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log >= 0) {
            dup2(log, STDOUT_FILENO);
            dup2(log, STDERR_FILENO);
        }
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(qemu.c_str()));
        for (const auto& a : args)
            argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(qemu.c_str(), argv.data());
        _exit(127);
    }

    // The machine is up once QMP answers.
    QmpClient qmp;
    if (!qmp.Connect(qmpSock, cfg.vmStartTimeoutSeconds * 1000)) {
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
        int status = 0;
        waitpid(pid, &status, 0);
        return UltraWinResult::Error(
            UltraWinResultCode::QmpError,
            "QEMU did not answer on QMP: " + qmp.LastError() +
                " (details: " +
                (fs::path(vmDir) / "ultrawin-qemu.log").string() + ")");
    }

    std::lock_guard<std::mutex> lk(g_mutex);
    g_vmPid = pid;
    g_vmKvm = kvm;
    return UltraWinResult::Ok();
}

namespace {

// Shared implementation for the QMP one-liners (stop/cont/powerdown/quit).
UltraWinResult VmQmpCommand(const char* command) {
    if (!UltraWin_IsInitialized())
        return UltraWinResult::Error(UltraWinResultCode::NotInitialized,
                                     "call UltraWin_Initialize first");
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (!VmProcessAliveLocked())
            return UltraWinResult::Error(UltraWinResultCode::VmNotRunning,
                                         "the machine is not running");
    }
    QmpClient qmp;
    if (!qmp.Connect(QmpSocketPath(), 5000) || !qmp.Execute(command, 5000))
        return UltraWinResult::Error(UltraWinResultCode::QmpError,
                                     qmp.LastError());
    return UltraWinResult::Ok();
}

// Wait (bounded) for the tracked QEMU to exit; true when it did.
bool WaitVmExit(int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    for (;;) {
        {
            std::lock_guard<std::mutex> lk(g_mutex);
            if (!VmProcessAliveLocked()) return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

}  // namespace

UltraWinResult UltraWin_VmStop() {
    auto r = VmQmpCommand("system_powerdown");
    if (!r) return r;
    if (WaitVmExit(30000)) return UltraWinResult::Ok();
    // No guest OS (or one that ignores ACPI): pull the virtual plug.
    return UltraWin_VmKill();
}

UltraWinResult UltraWin_VmKill() {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (!VmProcessAliveLocked())
            return UltraWinResult::Error(UltraWinResultCode::VmNotRunning,
                                         "the machine is not running");
    }
    {
        QmpClient qmp;  // best-effort graceful quit first
        if (qmp.Connect(QmpSocketPath(), 2000)) qmp.Execute("quit", 2000);
    }
    if (WaitVmExit(5000)) return UltraWinResult::Ok();
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_vmPid != 0) {
        kill(-g_vmPid, SIGKILL);
        kill(g_vmPid, SIGKILL);
        int status = 0;
        waitpid(g_vmPid, &status, 0);
        g_vmPid = 0;
    }
    return UltraWinResult::Ok();
}

UltraWinResult UltraWin_VmSuspend() { return VmQmpCommand("stop"); }
UltraWinResult UltraWin_VmResume() { return VmQmpCommand("cont"); }

UltraWinVmState UltraWin_VmGetState() {
    if (!UltraWin_IsInitialized()) return UltraWinVmState::NotProvisioned;
    const std::string vmDir = VmDirectory();
    if (ReadManifest(vmDir).empty()) return UltraWinVmState::NotProvisioned;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (!VmProcessAliveLocked()) return UltraWinVmState::Stopped;
    }
    QmpClient qmp;
    if (!qmp.Connect(QmpSocketPath(), 3000))
        return UltraWinVmState::Unknown;
    std::string status = qmp.QueryStatus(3000);
    if (status == "paused" || status == "suspended")
        return UltraWinVmState::Suspended;
    if (status.empty()) return UltraWinVmState::Unknown;
    return UltraWinVmState::Running;
}

UltraWinResult UltraWin_VmGetInfo(UltraWinVmInfo* out) {
    if (!out)
        return UltraWinResult::Error(UltraWinResultCode::InvalidArgument,
                                     "out is required");
    *out = UltraWinVmInfo();
    if (!UltraWin_IsInitialized())
        return UltraWinResult::Error(UltraWinResultCode::NotInitialized,
                                     "call UltraWin_Initialize first");
    out->vmDirectory = VmDirectory();
    out->diskPath = (fs::path(out->vmDirectory) / "disk.qcow2").string();
    out->state = UltraWin_VmGetState();
    UltraWinConfig cfg = UltraWin_GetConfig();
    out->rdpHostPort = cfg.vmRdpHostPort > 0 ? cfg.vmRdpHostPort : 13389;
    std::lock_guard<std::mutex> lk(g_mutex);
    out->qemuPid = g_vmPid;
    out->kvm = g_vmKvm && g_vmPid != 0;
    return UltraWinResult::Ok();
}
