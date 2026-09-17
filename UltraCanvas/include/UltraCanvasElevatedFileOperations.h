// include/UltraCanvasElevatedFileOperations.h
// "Retry as administrator" for a file operation the current user is not
// allowed to perform - what Explorer offers when a delete answers "You need
// permission to perform this action": Windows asks for consent (the UAC
// prompt), and the operation runs again with administrator rights.
//
//   // At the very top of main(), before any UI:
//   int helperExit = 0;
//   if (ElevatedFileOperations::RunHelperIfRequested(argc, argv, helperExit))
//       return helperExit;
//
//   // Later, when a delete fails with a permission error:
//   if (ElevatedFileOperations::IsAvailable() &&
//       ElevatedFileOperations::IsPermissionFailure(ec)) {
//       auto result = ElevatedFileOperations::DeleteElevated({path});
//   }
//
// How it works: there is no way to raise the rights of a running process, so
// the retry starts a second copy of THIS executable with the shell's "runas"
// verb - Windows shows its consent prompt and, if the user agrees, starts the
// copy elevated. That copy sees the helper flag on its command line, does the
// deletion, writes what it could not delete into a report file the caller
// named, and exits without ever creating a window. RunHelperIfRequested is
// the host's half of that contract: the application calls it first thing in
// main(), so a copy started as the helper never gets as far as its UI. A host
// that never calls it never offers the retry either - IsAvailable() stays
// false - because relaunching an executable that would open a second main
// window instead of deleting anything helps nobody.
//
// What it does NOT do: change the user's rights for anything else (the
// elevated process lives for the one operation and exits), remember the
// consent (every retry asks again, as Explorer does), or delete anything the
// user did not name (the helper takes absolute paths only, and only the
// ones on its command line - nothing is read from a file it did not write).
//
// Backends: Windows (ShellExecuteEx "runas" + the process token's elevation
// state). Everywhere else IsAvailable() is false and DeleteElevated answers
// Unavailable, so callers keep the plain "Try again / Skip" choice there.
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <system_error>
#include <vector>

namespace UltraCanvas {
    namespace ElevatedFileOperations {

        // ===== RESULT =====
        enum class ElevatedOutcome {
            Completed,    // the helper ran; see `failures` for what it could not do
            Declined,     // the user answered No to the consent prompt (or it timed out)
            Failed,       // the helper could not be started at all - `error` says why
            Unavailable   // no backend, helper not wired, or already elevated
        };

        struct ElevatedFailure {
            std::string path;     // as handed in
            std::string reason;   // the system's wording, e.g. "The process cannot access the file …"
        };

        struct ElevatedDeleteResult {
            ElevatedOutcome outcome = ElevatedOutcome::Unavailable;
            std::string error;                      // Failed: why nothing ran
            std::vector<ElevatedFailure> failures;  // Completed: entries still there
        };

        // ===== HOST SIDE =====
        // Call first in main(). True: this process was started as the elevated
        // helper - the work is done, `exitCode` is the process result, return
        // it. False: an ordinary start, carry on. Calling this at all is what
        // tells IsAvailable() that a relaunch of this executable will behave;
        // on the platforms without a backend it records that and returns false.
        bool RunHelperIfRequested(int argc, char** argv, int& exitCode);

        // Whether a retry with administrator rights can be offered right now:
        // a backend exists, the host wired RunHelperIfRequested, and this
        // process is not already elevated (when it is, the system has already
        // said no to an administrator, and asking again cannot change that).
        bool IsAvailable();

        // Whether this process already runs with administrator rights
        // (Windows: the token's elevation state). False elsewhere.
        bool ProcessIsElevated();

        // Whether `ec`, from a failed std::filesystem operation, is the
        // permission refusal a retry as administrator may resolve - as
        // opposed to a sharing violation ("in use by another program"),
        // which no amount of rights gets past.
        bool IsPermissionFailure(const std::error_code& ec);

        // Delete these entries (files, or folders with everything in them)
        // through the elevated helper. Absolute paths only. BLOCKS until the
        // consent prompt is answered and the helper has exited - run it off
        // the UI thread when the tree may be large. One consent prompt for
        // the whole list; a list too long for one command line asks once per
        // part. Read-only attributes inside are lifted the way the widget's
        // own delete lifts them, so a protected folder goes in one pass.
        ElevatedDeleteResult DeleteElevated(const std::vector<std::string>& paths);

        // ===== THE HELPER'S CONTRACT (exposed for the backend and the tests) =====
        // Command line of an elevated helper run:
        //   <exe> --uc-elevated-delete <report file> <absolute path>...
        // The helper deletes each path, writes one "<path>\t<reason>" line
        // per failure into the report file (UTF-8, LF), and exits 0 when
        // everything went, 1 when something is left, 2 when the arguments
        // made no sense.
        constexpr const char* kHelperDeleteFlag = "--uc-elevated-delete";
        constexpr int kHelperExitOk = 0;
        constexpr int kHelperExitPartial = 1;
        constexpr int kHelperExitBadArguments = 2;

        // The helper's work, platform-free: delete every path (absolute ones
        // only - a relative one is refused and reported), lifting read-only
        // bits first. Returns the exit code; `failures` names the rest.
        int RunHelperDelete(const std::vector<std::string>& paths,
                            std::vector<ElevatedFailure>& failures);

        // Report file encoding, both ways.
        std::string FormatReport(const std::vector<ElevatedFailure>& failures);
        std::vector<ElevatedFailure> ParseReport(const std::string& report);

        // One argument quoted for a Windows command line so CommandLineToArgvW
        // (and therefore the helper's argv) gets it back byte for byte:
        // quotes only when needed, backslashes before a quote doubled, the
        // quote itself escaped.
        std::string QuoteCommandLineArgument(const std::string& argument);

        // Split `paths` into runs whose quoted, space-joined length stays
        // under `maxLength` once `prefixLength` (the executable, the flag and
        // the report file, already quoted) is added. Every run holds at
        // least one path, however long, so nothing is silently dropped.
        std::vector<std::vector<std::string>> SplitIntoCommandLines(
                const std::vector<std::string>& paths,
                size_t prefixLength, size_t maxLength);

        // ===== BACKEND INTERFACE (implemented per platform under OS/<Platform>/) =====
        // Not part of the public surface. Windows implements them in
        // OS/MSWindows/UltraCanvasWindowsElevatedFileOperations.cpp; the core
        // file provides the no-backend fallbacks on every other platform.
        bool NativeProcessIsElevated();
        bool NativeIsPermissionFailure(const std::error_code& ec);
        // This process's own command line as UTF-8 arguments, from the
        // system rather than from a narrow argv the C runtime may have
        // mangled. Empty when the platform has nothing better than argv.
        std::vector<std::string> NativeCommandLineArguments();
        // Start the helper for these paths and wait for it. False = no
        // backend; the result is untouched then.
        bool NativeRunElevatedDelete(const std::vector<std::string>& paths,
                                     ElevatedDeleteResult& out);

    } // namespace ElevatedFileOperations
} // namespace UltraCanvas
