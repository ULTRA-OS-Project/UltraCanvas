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

## 1. Run a diagnostic launcher

Two ship in the Windows package. Which one you want depends on the symptom:

| Symptom | Use |
|---|---|
| The app exits immediately, or you just want the exit code | `uc-diagnose.bat` |
| **No window ever appears**, or you don't know whether it died or is stuck | `uc-diagnose.ps1` |

`scripts/uc-diagnose.bat` runs the executable **attached** so the exit code
survives, enables the framework log, prints the Windows build number, and
checks the executable for a Mark of the Web:

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

`scripts/uc-diagnose.ps1` answers the question the batch file cannot: **is the
process dead, or alive and stuck?** Those two look identical from the outside —
no window, nothing on screen — and have nothing in common as bugs.

```powershell
.\uc-diagnose.ps1
.\uc-diagnose.ps1 .\bin\Ladybird.exe https://example.com
```

It launches the executable, watches for up to 20 seconds, and reports one of
three verdicts: the process **exited** (with the decoded exit code), a **window
appeared** (it works), or the process is **alive with no window** — plus the
child processes it spawned, any Windows event-log entry naming it, and the
framework log. It leaves the process running so you can inspect it. Windows
PowerShell 5.1 and PowerShell 7 both work, and it changes nothing on the
machine.

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

One trap in that PowerShell form: **PowerShell does not wait for a
GUI-subsystem process.** It returns to the prompt immediately, and
`$LASTEXITCODE` is left over from whatever ran before — it is not the app's
exit code. (`cmd` and batch files do wait, which is why `uc-diagnose.bat` can
read `%ERRORLEVEL%`.) To get the real code from PowerShell, wait explicitly:

```powershell
$p = Start-Process .\bin\App.exe -Wait -PassThru
'0x{0:X8}' -f $p.ExitCode
```

Note that `Start-Process -PassThru` **without** `-Wait` returns an object whose
`ExitCode` may read 0 even for a process that exited non-zero — enough to
report a crash as a clean exit. `uc-diagnose.ps1` therefore starts the process
through `[System.Diagnostics.Process]::Start`, which keeps the OS handle open
and reports the true code.

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

**A log file that exists but is empty.** Worth separating from the case above:
creating the file *succeeded*, so the process ran far enough to open one. That
rules out a loader failure, a missing DLL and a policy block — all of which
kill the process before it can touch the filesystem. Either the binary predates
0.3.80 and does not honour `ULTRACANVAS_DEBUG_LOG` (the file is then the
application's own log, opened and never flushed), or the process stopped
between opening it and the first flushed line. Go to the next section.

## ILLEGAL_INSTRUCTION: built for a CPU this machine is not

```
Ladybird crashed: exception 0xC000001D (ILLEGAL_INSTRUCTION (binary uses CPU
instructions this machine lacks)) at 0x00007FF8599C3793 in
C:\...\Ladybird\bin\lagom-gfx.dll. Windows 11 (10.0 build 26200)
This CPU: Intel(R) Core(TM) i5-8250U CPU @ 1.60GHz [SSE4.2 AVX AVX2]
Bytes at the fault: 62 F1 7C 48 28 C1 ...
The binary was built for a CPU this one is not. Rebuild it without
-march=native (or /arch:AVX*) so it targets a baseline this machine has.
```

Nothing is wrong with the code at that address. The compiler emitted an
instruction the machine will not execute, and the loader was happy to map it —
the fault only happens when execution reaches it. Which is why this crash tends
to land in a *graphics* or *image* library: those are where the vector code is.

The reporter prints three things that settle it between them:

- **the module** — which library was compiled too aggressively;
- **this CPU** — the brand string and which instruction sets the machine
  actually offers, read through `IsProcessorFeaturePresent` so it reports what
  the OS *permits*, not what the silicon has;
- **the bytes at the fault** — enough to identify the instruction. A `62`
  prefix is EVEX, i.e. AVX-512; `C5` or `C4` is VEX, i.e. AVX/AVX2.

Two causes, and the CPU line tells you which:

1. **The build machine had a newer CPU than the target.** `-march=native` bakes
   in whatever the *builder's* processor supported. Build with an explicit
   baseline instead — `-march=x86-64-v2` (SSE4.2, POPCNT) is a safe floor for
   anything that runs Windows 10 or 11; `-march=x86-64-v3` requires AVX2 and
   excludes a lot of still-current laptops and most virtual machines. Never ship
   `-march=native` binaries.
2. **The binary is x64 running under emulation on an ARM64 machine.** The
   reporter appends `EMULATED: x64 image on a ARM64 machine` when it detects
   this (via `IsWow64Process2`). Windows on ARM emulates a *subset* of x86:
   AVX-512 is not available at all, and AVX/AVX2 only on recent builds. The fix
   is to ship a native ARM64 build; lowering the baseline only helps if the
   emulator covers what is left.

**This is the failure most likely to be misread as an operating-system
problem.** "Works on Windows 10, crashes on Windows 11" is what you observe when
the two machines also differ in CPU — which they usually do, because the
Windows 11 machine is a different box. The OS is a coincidence; the instruction
set is the cause. The banner's `cpu` line records this on every start, so
comparing two machines is a diff of two log files rather than a guess.

## Alive but no window

If `uc-diagnose.ps1` reports the process is still running after 20 seconds with
no window, nothing crashed. There is no exit code to read and there will be no
event-log entry — their absence is evidence, not a dead end. The process is
blocked, or spinning, before it gets as far as creating a window.

The CPU time the script prints splits the two cases:

- **Near zero and not growing** — blocked on a wait that never completes.
- **Climbing steadily** — spinning in the event loop.

For an application that merely *uses* UltraCanvas, suspect its own startup
work. For an application that **embeds** UltraCanvas and drives it from its own
event loop — a browser, an editor host, anything with helper processes — the
usual cause is the event loop not servicing something the application is
waiting on.

The specific case to check first is file-descriptor watches.
`UltraCanvasApplicationBase::AddFdWatch()` lets a host register sockets (IPC to
a content or helper process, most often) so the toolkit's own wait services
them, instead of the host needing a second event loop. A platform backend that
does not fold those descriptors into its native wait leaves every registered
callback unfired: the host sits waiting for an IPC reply that the loop is never
going to deliver, and no window is ever created. From the outside that is
indistinguishable from a hang.

Both backends now do this — Linux through the `select()` in
`CollectAndProcessNativeEvents()`, Windows through
`UltraCanvasWindowsApplication::PollAndServiceFdWatches()`, which polls the
registered Winsock sockets around the `MsgWaitForMultipleObjectsEx()` wait and
bounds that wait so level-triggered readiness is picked up promptly. **The
Windows side arrived in 0.3.81; before that the Windows loop ignored fd-watches
entirely while Linux honoured them.** A host built against an older Windows
UltraCanvas will hang exactly as described here, and the same host will run on
Linux, which makes the bug look like a platform difference in the host rather
than a missing piece of the backend.

So: if a host application hangs before its first window on Windows, check what
it is built against before looking anywhere else.

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
   usually also different hardware. Exception `0xC000001D`
   (`ILLEGAL_INSTRUCTION`) means the binary uses instructions this CPU does not
   have — see [ILLEGAL_INSTRUCTION](#illegal_instruction-built-for-a-cpu-this-machine-is-not)
   above, which is the most common real cause of a "works on 10, not on 11"
   report and has nothing to do with the OS version.
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
