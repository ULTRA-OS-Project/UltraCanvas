// include/UltraCanvasArchiveRun.h
// "Extract and run": starting a program that is inside an archive.
//
// A file inside a .zip (or any archive VirtualFS opens) has no path the
// operating system can execute - it is a virtual path, and the kernel only
// runs files. So the program is unpacked first, together with the rest of
// its archive (a program rarely lives alone: its DLLs, data and
// configuration sit beside it), into a folder of its own; it runs from
// there with that folder as its working directory; and the folder is
// deleted once the program - and whatever it started - has ended.
//
// This header holds the parts of that which are not UI:
//
//   - which archive entries count as programs (IsRunnableArchiveEntry),
//   - a launch whose end can be waited for (LaunchWatchedProgram), which a
//     detached launch (LaunchDetachedProcess) deliberately cannot tell,
//   - the run folders: made one per run under a root, each with a marker
//     naming the processes that use it, removed only when nothing holds a
//     file in it, and swept up on a later start when a run's owner went
//     away without removing its folder (SweepArchiveRunFolders),
//   - carrying Windows' "downloaded from the internet" mark from the archive
//     to what was unpacked from it (CopyDownloadMarking), so SmartScreen
//     still looks at a program that arrived in a downloaded .zip.
//
// The file display (UltraCanvasFilerWidget::ExtractAndRunEntry) is the
// caller; the unpacking itself goes through VirtualFS.
//
// Backends: Windows (a job object tracks the program and every process it
// starts; OS/MSWindows/UltraCanvasWindowsArchiveRun.cpp) and POSIX (the
// program leads a process group of its own; in the core file, which serves
// Linux, the BSDs and macOS alike). WebAssembly cannot start programs, and
// says so.
// Version: 1.0.0
// Last Modified: 2026-09-28
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace UltraCanvas {

    // ===== WHICH ARCHIVE ENTRIES ARE PROGRAMS =====
    // Which kind of system the entry would run on. A parameter rather than a
    // compile-time switch so that both rules can be tested on either.
    enum class ArchiveRunHost {
        Windows,   // by extension: .exe .com .bat .cmd .msi
        Posix      // by the execute permission the archive recorded, or .AppImage
    };

    // The host this build runs on.
    ArchiveRunHost CurrentArchiveRunHost();

    // Whether an archive entry is a program `host` can run once unpacked.
    // `extension` is lowercase, without the dot. `executableBit` is whether
    // the archive recorded an execute permission for the entry (tar and
    // Unix-made zips do; Windows-made zips never do, so on Windows the
    // extension alone decides).
    bool IsRunnableArchiveEntry(const std::string& extension, bool executableBit,
                                ArchiveRunHost host);
    inline bool IsRunnableArchiveEntry(const std::string& extension,
                                       bool executableBit) {
        return IsRunnableArchiveEntry(extension, executableBit,
                                      CurrentArchiveRunHost());
    }

    // ===== A PROGRAM WHOSE END CAN BE WAITED FOR =====
    class WatchedProcess {
    public:
        virtual ~WatchedProcess() = default;
        // True while the program runs, or anything it started that stayed
        // with it: on Windows every process of its job (an installer that
        // unpacks a second stage and hands over to it counts as running
        // until the second stage ends), on POSIX every process of its
        // process group. A program that deliberately leaves - a daemon that
        // starts a session of its own, a child that breaks away from the job,
        // an installer relaunched elevated through UAC - is not seen; the
        // run folder's removal then waits on the files it holds instead (see
        // RemoveArchiveRunFolder).
        virtual bool IsRunning() = 0;
        // The started program's process id.
        virtual uint64_t GetProcessId() const = 0;
    };

    // Starts the program at `programPath` (a real, local file) with
    // `workingDirectory` as its current folder, and returns a handle that
    // can tell when it has ended. Not detached from this process the way
    // LaunchDetachedProcess is, but closing this application does not end
    // the program either.
    //
    // Windows: an .exe or .com runs directly (in a console of its own, should
    // it be a console program), a .bat or .cmd through cmd.exe, an .msi
    // through msiexec. A program whose manifest asks for administrator
    // rights goes through the shell, which shows the UAC prompt.
    // POSIX: the file is executed as it is - a binary, or a script through
    // its #! line.
    //
    // Returns null with a message fit for the user in `outError` when
    // nothing could be started.
    std::unique_ptr<WatchedProcess> LaunchWatchedProgram(
            const std::string& programPath,
            const std::string& workingDirectory,
            std::string& outError);

    // Whether a process with this id exists right now (including one this
    // user may not look at, such as an elevated one).
    bool IsProcessAlive(uint64_t processId);

    // The id of this process.
    uint64_t CurrentProcessId();

    // ===== THE DOWNLOAD MARK =====
    // Windows: when `sourceFile` carries a Zone.Identifier (the mark a
    // browser puts on a download), writes the same mark onto every file
    // under `folder`, which is what Explorer does when it unpacks a zip.
    // Returns how many files were marked. Elsewhere, and for a file that
    // carries no mark, returns 0 and does nothing.
    int CopyDownloadMarking(const std::string& sourceFile, const std::string& folder);

    // ===== RUN FOLDERS =====
    // Where run folders go unless the caller picks a root of its own:
    // "UltraCanvas-Run" in the system's temporary folder.
    std::string DefaultArchiveRunRoot();

    // Makes a new, empty run folder under `root` (creating `root` when
    // needed) and its marker next to it, "<folder>.run", which names this
    // process. Fills `outFolder` and returns true, or returns false with a
    // message in `outError`.
    bool CreateArchiveRunFolder(const std::string& root, std::string& outFolder,
                                std::string& outError);

    // Adds a process to a run folder's marker: while any process the marker
    // names is alive, a sweep leaves the folder alone.
    void RecordArchiveRunProcess(const std::string& folder, uint64_t processId);

    // Removes a run folder and its marker, but only if nothing holds a file
    // in it. The folder is renamed first, and on Windows a folder cannot be
    // renamed while any file under it is open or running - so a program that
    // escaped the watch (an installer's elevated second stage) keeps its
    // files until it lets go of them. Returns true once the folder is gone;
    // false means "not yet", and the caller asks again later.
    bool RemoveArchiveRunFolder(const std::string& folder);

    // Removes, under `root`, every run folder whose marker names no living
    // process - left behind by an application that ended (or crashed) while
    // its program still ran - and whatever an interrupted removal left.
    // Folders still held are skipped as RemoveArchiveRunFolder skips them.
    // Returns how many were removed. Safe to call from any thread.
    int SweepArchiveRunFolders(const std::string& root);

} // namespace UltraCanvas
