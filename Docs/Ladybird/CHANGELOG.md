#### 2026-08-28 *0.1.0*
- **First tracked version of the Ladybird port.** Ladybird-driven work has been
  landing in this repository for a while with nowhere to record it — the most
  recent example changed two framework files to fix a Ladybird compile and
  carried no changelog entry at all. This file is that place. The version above
  is the port's own and starts here; it does not track the framework's.
- **Windows event loop services the port's IPC.** `AddFdWatch()` was honoured on
  Linux and ignored on Windows, so the browser process waited forever for a
  WebContent reply the loop was never going to deliver: no crash, no log, no
  window. `UltraCanvasWindowsApplication::PollAndServiceFdWatches()` now polls
  the registered Winsock sockets around the `MsgWaitForMultipleObjectsEx()` wait
  and bounds that wait, so level-triggered readiness is picked up promptly.
- **Windows header floors no longer fight the host's.** UltraCanvas pinned
  `NTDDI_VERSION` to a literal, which the Windows SDK rejects when the host
  build already asks for a higher `_WIN32_WINNT` — Ladybird asks for `0x0A00`.
  `NTDDI_VERSION` is now derived from `_WIN32_WINNT` the way the SDK derives it
  by default, keeping the Win8/Vista floors intact under MinGW-w64, MSVC and
  clang-cl alike.
- **A failed start now says why.** The port's Windows launcher runs a
  GUI-subsystem binary through `start`, so a failure to come up produced no
  window, no console output and no exit code. The framework's startup banner,
  crash reporter and runtime `ULTRACANVAS_DEBUG_LOG` make that visible, and
  `uc-diagnose.ps1` distinguishes a process that died from one that is alive and
  stuck. See `Docs/UltraCanvas/UltraCanvasWindowsDiagnostics.md`.
- **Known packaging trap, not a framework bug:** a Windows build whose Lagom
  libraries were compiled with `-march=native` faults with `0xC000001D`
  (`ILLEGAL_INSTRUCTION`) on any machine older than the build host — reported in
  the field as `lagom-gfx.dll` crashing on Windows 11 while the same package ran
  on Windows 10, which was a CPU difference and not an OS one. Build the port
  against an explicit baseline (`-march=x86-64-v2` is a safe floor for Windows
  10 and 11); ship a native ARM64 build rather than relying on x64 emulation,
  which offers no AVX-512 at all.

---

### What belongs in this file

The **Ladybird port** — its builds, packaging, launchers, and the browser-side
integration — versioned independently of the framework, the same way UltraTexter
and UltraCleaner are. The port's own sources live outside this repository; this
changelog is the record of its releases and of the integration surface it
depends on.

A change to **UltraCanvas itself** belongs in
[`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) even when
Ladybird is what motivated it — that is the house rule in
[`AGENTS.md`](../../AGENTS.md), and it keeps one framework change from being
described in two places with two version numbers. Cross-reference it from an
entry here when a release of the port depends on it, as the entries above do.

The first line of this file is the single source of truth for the port's
version: `cmake/UltraCanvasVersion.cmake` parses it into `LADYBIRD_VERSION`
(plus `_DOT4` / `_COMMA4` for Windows resources). To release, add an entry at
the top in the form `#### YYYY-MM-DD *x.y.z*` — that is the whole bump. Do not
write the number anywhere else.
