// OS/MSWindows/UltraCanvasWindowsDiagnostics.h
// Startup and crash diagnostics for Windows builds.
// Version: 1.0.0
// Author: UltraCanvas Framework
//
// Windows apps are linked with WIN32_EXECUTABLE (the GUI subsystem), which
// gives the process no console: `std::cerr` has nowhere to go, an unhandled
// exception kills the process without a word, and a failed initialisation used
// to return false and exit with no window and no message. On the developer's
// own machine that is invisible; on a user's machine it is the whole bug report
// ("it just doesn't start").
//
// The helpers here make that failure describable:
//
//   AttachParentConsole()          reconnects stdio to the console the process
//                                  was launched from, so running the EXE from
//                                  cmd/PowerShell shows its output.
//   LogWindowsStartupBanner()      records Windows build, architecture, paths
//                                  and locale — the first thing a "works on 10,
//                                  not on 11" report needs.
//   InstallWindowsCrashReporter()  turns a silent crash into a logged
//                                  exception code, address and faulting module,
//                                  plus a message box.
//   ReportWindowsStartupFailure()  reports a fatal init failure to the log and
//                                  to the user instead of exiting quietly.
//
// Every one of them is diagnostic only: nothing here changes what the app does
// when it starts successfully. Message boxes are suppressed by setting
// ULTRACANVAS_NO_ERROR_DIALOG=1, which is what a helper/child process
// (Ladybird's WebContent, a test runner, a CI job) should do so a failure logs
// and exits instead of blocking on a dialog nobody will click.
#pragma once

#ifndef ULTRACANVAS_WINDOWS_DIAGNOSTICS_H
#define ULTRACANVAS_WINDOWS_DIAGNOSTICS_H

#include <string>

namespace UltraCanvas {

    // Reconnects stdout/stderr/stdin to the console of the launching process,
    // when there is one and they are not already connected (a launcher that
    // redirects them to a file keeps its redirection). Returns true if the
    // process now has a console. A GUI process started from Explorer, or
    // detached with `start`, has no parent console and this returns false --
    // which is exactly why the log file matters more than the console.
    bool AttachParentConsole();

    // Windows version as reported by RtlGetVersion, e.g. "Windows 11 (10.0
    // build 22631)". GetVersionEx is deliberately not used: it reports 6.2 for
    // an application without a compatibility manifest, which would make every
    // Windows 10 and 11 machine look identical in a bug report.
    std::string GetWindowsVersionString();

    // Writes one block of environment facts to debugOutput: app name and
    // version-relevant paths, the Windows version above, process architecture,
    // whether the process is elevated, and the code page. Called once during
    // initialisation; harmless (and free) when the debug sink is off.
    void LogWindowsStartupBanner(const std::string& appName);

    // Installs an unhandled-exception filter. On a crash it appends one line to
    // the ULTRACANVAS_DEBUG_LOG file naming the exception code, the faulting
    // address and the module that address belongs to, then shows a message box
    // with the same text. The log write goes straight to the file with the Win32
    // API -- no allocation, no C++ stream, no lock -- because the process is
    // already in an undefined state and the normal sink may be mid-write.
    void InstallWindowsCrashReporter(const std::string& appName);

    // Logs `stage` + `detail` as a fatal startup failure and shows it in a
    // message box. Call before returning false from an initialisation step, so
    // the reason reaches the user rather than only the (possibly disabled) log.
    void ReportWindowsStartupFailure(const std::string& stage, const std::string& detail);

    // Reports a C++ exception that escaped an event handler on the UI thread
    // -- thrown somewhere below the window procedure, which runs inside a
    // callback the kernel dispatched. On x64 the unwinder cannot cross that
    // boundary, so without a catch at the window procedure the process dies
    // with STATUS_BAD_FUNCTION_TABLE (0xC00000FF) or the bare GCC throw code
    // (0x20474343) in the crash reporter, and the error text is lost. `where`
    // names the event, `what` is the exception text. Logged every time; shown
    // in a message box once per process, because the handler that threw is
    // usually hit again by the very next event of the same kind.
    void ReportWindowsEventException(const std::string& where, const std::string& what);

    // Formats a Win32 error code as "<code> (<FormatMessage text>)".
    std::string DescribeWin32Error(unsigned long error);

} // namespace UltraCanvas

#endif // ULTRACANVAS_WINDOWS_DIAGNOSTICS_H
