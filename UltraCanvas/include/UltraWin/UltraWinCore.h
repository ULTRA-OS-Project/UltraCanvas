// include/UltraWin/UltraWinCore.h
// Stage 1 (Wine tier) subset of the UltraWin design proposal: handles,
// result type, host capabilities, configuration, and the
// initialize/shutdown/version entrypoints. Full surface (VM tier,
// compatibility routing) defined in Docs/Research/UltraWinDesignProposal.md.
// UltraWin is Linux / ULTRA OS only.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Defensive: X11/Xlib.h defines `Success` and `None` as macros, which would
// turn our enum-class members into literal integers (same guard as
// UltraNetCore.h — UltraCanvas's Linux platform glue pulls X11 in
// transitively).
#ifdef Success
#undef Success
#endif
#ifdef None
#undef None
#endif
#ifdef Bool
#undef Bool
#endif
#ifdef Status
#undef Status
#endif

// ============================================================================
// Opaque handle. Zero is reserved for "invalid handle". Returned for running
// application instances (Stage 1) and, later, VM lifecycle operations.
// ============================================================================
using UltraWinHandle = uint64_t;
constexpr UltraWinHandle UltraWinInvalidHandle = 0;

// ============================================================================
// Result codes. Stage 1 produces the Wine-tier subset; VM-tier codes are
// reserved up front so the surface is stable for downstream callers.
// ============================================================================
enum class UltraWinResultCode {
    Success,
    NotInitialized,
    NotSupported,          // wrong platform, or tier not available on host
    InvalidArgument,
    InvalidHandle,
    WineNotFound,          // no usable wine binary on this host
    WinetricksNotFound,    // no usable winetricks script on this host
    EnvironmentNotFound,
    EnvironmentExists,
    EnvironmentCreateFailed,
    EnvironmentDeleteFailed,
    EnvironmentBusy,       // apps from this environment still running
    FileNotFound,          // .exe (or wine binary) path does not exist
    LaunchFailed,          // fork/exec failure
    ProcessError,          // app exited with failure before/at startup
    // Named DriveMappingFailed (not MappingFailed) on purpose: X11's X.h
    // defines MappingFailed as a macro, and UltraCanvas TUs often see both
    // headers.
    DriveMappingFailed,    // drive-letter symlink could not be created
    ComponentInstallFailed, // winetricks reported failure (or timed out)
    PermissionDenied,
    Timeout,
    IoError,
    VmNotProvisioned,      // VM operations before UltraWin_VmProvision
    VmUnavailable,         // no usable QEMU (or qemu-img) on this host
    VmAlreadyProvisioned,  // manifest exists and sizes cannot change
    VmAlreadyRunning,
    VmNotRunning,
    QmpError,              // the guest's QMP control socket misbehaved
    Unknown
};

// ============================================================================
// Structured result for every blocking operation. operator bool() lets
// callers write `if (UltraWin_RunApp(...)) { ... }` for the success path.
// ============================================================================
struct UltraWinResult {
    UltraWinResultCode code = UltraWinResultCode::Unknown;
    bool success = false;
    std::string message;

    operator bool() const { return success; }

    static UltraWinResult Ok() {
        UltraWinResult r;
        r.code = UltraWinResultCode::Success;
        r.success = true;
        return r;
    }

    static UltraWinResult Error(UltraWinResultCode c, const std::string& msg) {
        UltraWinResult r;
        r.code = c;
        r.success = false;
        r.message = msg;
        return r;
    }
};

// ============================================================================
// Execution tiers. Stage 1 implements Wine only; Vm is reserved so
// UltraWinRunOptions::forceTier keeps a stable meaning across stages.
// ============================================================================
enum class UltraWinTier {
    Auto,   // pick the best available tier for the application (default)
    Wine,   // API-translation tier (Wine / Hangover)
    Vm      // full-virtualisation tier — reserved, Stage 2
};

// ============================================================================
// Host capabilities, filled by UltraWin_GetCapabilities(). Detection is
// best-effort and read-only: a false value means "not usable from this
// process now", not necessarily "not installed".
// ============================================================================
struct UltraWinCapabilities {
    bool wineAvailable = false;      // usable wine binary found
    std::string winePath;            // absolute path of the binary
    std::string wineVersion;         // e.g. "wine-11.0" (verbatim --version)
    bool winetricksAvailable = false; // usable winetricks script found
    std::string winetricksPath;      // absolute path of the script
    bool ntsyncAvailable = false;    // /dev/ntsync present (Linux >= 6.14)
    bool kvmAvailable = false;       // /dev/kvm accessible (VM acceleration)
    bool qemuAvailable = false;      // qemu-system-x86_64 + qemu-img found
    std::string qemuPath;            // absolute path of the system emulator
    bool virtiofsdAvailable = false; // host->guest shared folders possible
    std::string hostArchitecture;    // uname machine, e.g. "x86_64", "aarch64"
    bool wineTierAvailable = false;  // == wineAvailable
    // The host can run the VM tier: QEMU present and KVM usable. Whether
    // the machine is provisioned/installed is UltraWin_VmGetInfo's business.
    bool vmTierAvailable = false;
    // This build carries the FreeRDP RemoteApp client (compile-time flag —
    // the library links libfreerdp when it is present at build time).
    bool remoteAppSupported = false;
};

// ============================================================================
// Module configuration, passed to UltraWin_Initialize(). Every field has a
// working default; an empty config is valid.
// ============================================================================
struct UltraWinConfig {
    // Absolute path to the wine binary. Empty = search PATH ("wine", then
    // "wine64"; on aarch64 hosts a Hangover wine build is what PATH finds).
    std::string winePath;

    // Root directory for environments (Wine prefixes). Empty = default:
    // $XDG_DATA_HOME/ultrawin/environments (~/.local/share/ultrawin/...).
    std::string environmentsRoot;

    // Drive letter under which the user's home directory is exposed in every
    // environment ('A'..'Z'; the design proposal's unified mapping). 0
    // disables the automatic home mapping.
    char homeDriveLetter = 'U';

    // Keep Wine's default Z: -> / mapping. Off by default: apps should see
    // the user's folders (U:), not the whole host filesystem.
    bool exposeRootDrive = false;

    // Disable Wine's bundled-download prompts for .NET (mono) and Gecko
    // (mshtml) during environment creation. Components become installable
    // explicitly via UltraWin_InstallComponent (later stage).
    bool suppressWinePrompts = true;

    // Seconds allowed for `wineboot --init` when creating an environment.
    int environmentCreateTimeoutSeconds = 180;

    // Absolute path to the winetricks script (component installer). Empty =
    // search PATH. Winetricks is optional exactly like Wine: probed at
    // runtime, spawned as a child process, never linked.
    std::string winetricksPath;

    // Seconds allowed for one UltraWin_InstallComponent call. Components
    // download from upstream mirrors; large ones (.NET) take a while.
    int componentInstallTimeoutSeconds = 1800;

    // ---- VM tier (Stage 2) ----

    // Absolute path of qemu-system-x86_64. Empty = search PATH.
    std::string qemuPath;

    // Machine home (manifest, qcow2 disk, control sockets). Empty = default:
    // $XDG_DATA_HOME/ultrawin/vm (sibling of the environments root).
    std::string vmDirectory;

    // Host loopback port forwarded to the guest's RDP port 3389 (used by
    // the RemoteApp integration). 0 = default 13389.
    int vmRdpHostPort = 13389;

    // Allow booting without KVM (TCG software emulation — an order of
    // magnitude slower; useful for tests and diagnostics only).
    bool vmAllowWithoutKvm = false;

    // Seconds allowed for QEMU to answer on its QMP socket after spawn.
    int vmStartTimeoutSeconds = 30;

    // Guest account the RemoteApp integration signs in with. Defaults
    // match the account UltraWin_VmProvision's answer file creates.
    std::string vmGuestUsername = "ultra";
    std::string vmGuestPassword = "ultra";
};

// ============================================================================
// Entry points
// ============================================================================

// Initialize the module. Safe to call more than once (later calls just
// replace the configuration). Does NOT require Wine to be present — only
// running applications does; callers can Initialize, inspect
// UltraWin_GetCapabilities(), and degrade gracefully.
UltraWinResult UltraWin_Initialize(const UltraWinConfig& config = {});

// Shut down the module. Running applications are left running (they are
// ordinary child processes of the host session); their handles become
// invalid.
void UltraWin_Shutdown();

bool UltraWin_IsInitialized();

UltraWinConfig UltraWin_GetConfig();
void UltraWin_SetConfig(const UltraWinConfig& config);

// Probe the host. Cheap enough to call at UI-build time; the wine version
// probe result is cached for the lifetime of the module.
UltraWinCapabilities UltraWin_GetCapabilities();

// Module version, "major.minor.patch".
std::string UltraWin_GetVersion();
