# Diagnosing a Windows Build That Will Not Start

What to do when a packaged UltraCanvas application — the demo, UltraFiler,
UltraViewer, Texter, or a host application such as the Ladybird port that
embeds the framework — starts on one Windows machine and does nothing at all
on another.

## Why the failure is silent

Windows applications built from this repository are linked as **GUI-subsystem**
executables (`set_target_properties(... WIN32_EXECUTABLE TRUE)` in
`CMakeLists.txt`). A GUI-subsystem process is not given a console, so:

- `std::cerr`, `printf` and the framework's `debugOutput` have nowhere to write;
- an unhandled exception kills the process without printing anything;
- the process exit code is discarded by the usual launcher.

Launchers make this worse by design. A launcher of the shape

```bat
@echo off
set "HERE=%~dp0"
start "" "%HERE%bin\App.exe" %*
```

uses `start`, which **detaches** the child: the shell prompt returns
immediately whether the application came up or died in its first millisecond,
and `ERRORLEVEL` never reflects what happened.

So this transcript contains no error at all:

```
PS C:\...\Ladybird> .\Ladybird.bat
PS C:\...\Ladybird>
```

The prompt returning at once is `start` doing its job. (A preceding
*"The command … was not found. However, it exists in the current location …
type `.\App.bat` instead"* is unrelated — that is PowerShell declining to run
a command from the current directory, and it is fixed by the `.\`, which the
transcript above already does.)

Everything below exists to turn that silence into a message.

## 1. Run the diagnostic launcher

`scripts/uc-diagnose.bat` ships in the Windows package. It runs the executable
**attached** so the exit code survives, enables the framework log, prints the
Windows build number, and checks the executable for a Mark of the Web:

```bat
uc-diagnose.bat
uc-diagnose.bat bin\Ladybird.exe https://example.com
```

With no argument it picks the single `.exe` beside it, or in `bin\`. It writes
`uc-diagnose.log` next to itself and prints it at the end, then decodes the
exit code:

```
===========================================================
Exit code: -1073741515
Meaning  : 0xC0000135 STATUS_DLL_NOT_FOUND - a required DLL is missing.
===========================================================
```

## 2. Turn on the log by hand

The launcher only sets one environment variable, and any shell can do the same:

```bat
set ULTRACANVAS_DEBUG_LOG=%TEMP%\ultracanvas.log
bin\App.exe
```

```powershell
$env:ULTRACANVAS_DEBUG_LOG = "$env:TEMP\ultracanvas.log"
.\bin\App.exe
```

| `ULTRACANVAS_DEBUG_LOG` | Effect |
|---|---|
| *unset* | Debug builds log to stderr; Release builds log nothing. |
| `0`, `off`, `no`, `none`, `false` | Off, in every build configuration. |
| `1`, `on`, `yes`, `true`, `stderr`, `-` | On, to stderr. |
| *any other value* | On, appending to that file. |

This works in **Release** builds. It used to not: `debugOutput` compiled to a
do-nothing stream unless `ULTRACANVAS_DEBUG` was defined, which meant the
packaged binaries users actually run were the only ones that could not be
diagnosed. The sink is now chosen at runtime
(`UltraCanvas/include/UltraCanvasDebug.h`), so a shipped binary can be asked
for a log without rebuilding it.

Two related variables:

| Variable | Effect |
|---|---|
| `ULTRACANVAS_NO_ERROR_DIALOG=1` | Suppresses the startup-failure and crash message boxes. Set it in helper processes (a browser's content process, a test runner, CI) so a failure logs and exits instead of blocking on a dialog nobody will click. |
| `FONTCONFIG_FILE` | Read, not written, by the framework. It is logged in the startup banner because a launcher pointing it at a file that does not exist is a common packaging mistake. |

A healthy start begins like this:

```
[10:14:02.113] UltraCanvas: ===== startup =====
[10:14:02.113] UltraCanvas: app          = Ladybird
[10:14:02.113] UltraCanvas: os           = Windows 11 (10.0 build 26100)
[10:14:02.113] UltraCanvas: architecture = 64-bit process on x64
[10:14:02.113] UltraCanvas: executable   = C:\...\Ladybird\bin\Ladybird.exe
[10:14:02.113] UltraCanvas: working dir  = C:\...\Ladybird
[10:14:02.114] UltraCanvas: Windows Application initialized successfully
```

The `os` line is what a "works on 10, not on 11" report needs, and it is not
guessable from `GetVersionEx`: an application without a compatibility manifest
is told it is running on 6.2, so the framework reads the true build number
through `RtlGetVersion`.

## 3. Read what came out

**A log with a `FATAL` line.** Initialisation failed and named the step. The
same text is shown in a message box, so the user sees it even without a
console.

```
[10:14:02.115] UltraCanvas: FATAL Could not register the window class
"UltraCanvas_Ladybird_Main": 1410 (Class already exists)
```

**A log ending in a crash line.** The unhandled-exception filter recorded the
exception code, the faulting address and — most usefully — the module that
address belongs to:

```
Ladybird crashed: exception 0xC0000005 (ACCESS_VIOLATION) at
0x00007FFB1C2E4A10 in C:\Windows\System32\ig9icd64.dll.
Windows 11 (10.0 build 26100)
```

A driver, a codec or a third-party DLL in that field means the framework was
the victim, not the cause.

**No log at all.** The process died before any UltraCanvas code ran, so the
problem is the loader, not the application. Go to the exit code and to
**Event Viewer → Windows Logs → Application**, which records an entry naming
the executable and the failing module.

## Windows 11 differences worth checking first

Nothing in `UltraCanvas/OS/MSWindows/` branches on the Windows version — the
DPI entry point is resolved dynamically and everything else is version-neutral
— so a Windows-11-only failure is almost always environmental. In rough order
of how often they bite:

1. **Smart App Control.** Windows 11 only, on by default on clean installs,
   and it blocks unsigned or unrecognised executables *without a dialog* —
   the exact reported symptom. Check Windows Security → App & browser control
   → Smart App Control; if it is On, switch it Off and try again. The
   long-term fix is Authenticode signing (see `SignUltraDemo.ps1`).
2. **Mark of the Web.** Files extracted from a downloaded ZIP carry a
   `Zone.Identifier` stream that Windows 11 enforces much harder than Windows
   10. Clear it for the whole folder:
   `Get-ChildItem -Recurse .\Ladybird | Unblock-File`.
3. **Endpoint protection.** Behaviour-based heuristics quarantine unsigned
   binaries that open sockets or load plug-in DLLs — the reason
   `package-win.sh` refuses to ship the test and diagnostic executables at all.
   Check the AV product's quarantine log.
4. **A different CPU, not a different Windows.** The Windows 11 machine is
   usually also newer or older hardware. Exit code `0xC000001D`
   (`ILLEGAL_INSTRUCTION`) means the binary uses instructions this CPU does not
   have; build without `-march=native`.
5. **A different GPU driver.** OpenGL surfaces go through WGL
   (`GLContextManagerWGL_MSWindows.cpp`), which needs a vendor driver for a
   3.x core context; Microsoft's fallback renderer only offers OpenGL 1.1.
   This is logged, not fatal, but it is worth ruling out on a VM or a machine
   running on the Microsoft Basic Display Adapter.

## Exit codes

`uc-diagnose.bat` decodes these; the table is here for the cases where you run
the executable yourself. `cmd` reports an NTSTATUS as a signed 32-bit integer,
which is why the decimal column looks the way it does.

| Hex | Decimal | Meaning |
|---|---|---|
| `0xC0000135` | -1073741515 | `STATUS_DLL_NOT_FOUND` — a required DLL is missing from the package. |
| `0xC0000139` | -1073741511 | `STATUS_ENTRYPOINT_NOT_FOUND` — a DLL was found but is the wrong version; usually an older copy earlier on `PATH`. |
| `0xC0000142` | -1073741502 | `STATUS_DLL_INIT_FAILED` — a DLL loaded but its initialiser failed. |
| `0xC000007B` | -1073741701 | `STATUS_INVALID_IMAGE_FORMAT` — 32/64-bit mismatch between the EXE and a DLL. |
| `0xC0000005` | -1073741819 | `ACCESS_VIOLATION` — crash; the log names the faulting module. |
| `0xC000001D` | -1073741795 | `ILLEGAL_INSTRUCTION` — binary built for a CPU this machine is not. |
| `0xC00000FD` | -1073741571 | `STATUS_STACK_OVERFLOW`. |
| `0xC0000409` | -1073740791 | `STATUS_STACK_BUFFER_OVERRUN` — a security check aborted the process. |
| `0xC0000022` | -1073741790 | `STATUS_ACCESS_DENIED` — something refused to let the process run. On Windows 11, suspect Smart App Control first. |

## Writing a launcher that does not hide failures

A launcher may legitimately want the detached, no-console behaviour — that is
what a desktop shortcut should do. It should not be the *only* way to start the
application. Ship both: the quiet launcher for normal use, and
`uc-diagnose.bat` beside it. If you write your own, the two rules are

- run the executable directly (`"%HERE%bin\App.exe" %*`) rather than through
  `start` whenever you want the exit code, and
- set `ULTRACANVAS_DEBUG_LOG` to a writable path so a failure leaves a trace
  even when the window never appears.

`FONTCONFIG_FILE` needs no launcher help. The framework calls
`SetupBundledFontconfig()` before anything touches fontconfig; it checks
whether the configured file actually exists and writes a working one into
`%LOCALAPPDATA%\UltraCanvas\fontconfig` when it does not — including when a
launcher points the variable at a path that was never packaged.

## API

Declared in `UltraCanvas/include/UltraCanvasDebug.h`:

```cpp
bool IsDebugOutputEnabled();                     // is anything reaching a sink?
void SetDebugOutputFile(const std::string& path); // redirect; "" returns to stderr
void SetDebugOutputEnabled(bool enabled);
```

Declared in `UltraCanvas/OS/MSWindows/UltraCanvasWindowsDiagnostics.h`
(Windows only; called for you by `UltraCanvasWindowsApplication::InitializeNative()`):

```cpp
bool        AttachParentConsole();
std::string GetWindowsVersionString();
void        LogWindowsStartupBanner(const std::string& appName);
void        InstallWindowsCrashReporter(const std::string& appName);
void        ReportWindowsStartupFailure(const std::string& stage, const std::string& detail);
std::string DescribeWin32Error(unsigned long error);
```

An application embedding the framework in its own `WinMain` gets all of this by
calling `UltraCanvasApplication::Initialize()` as usual; nothing extra is
required.
