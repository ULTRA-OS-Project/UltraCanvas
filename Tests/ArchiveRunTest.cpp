// Tests/ArchiveRunTest.cpp
// Unit tests for UltraCanvasArchiveRun ("Extract and run"):
//
//   - which archive entries count as programs, by the Windows rule and by
//     the POSIX one, whichever host runs the test;
//   - run folders: one made per run, marked with this process, removed on
//     request, and swept up when every process its marker names is gone -
//     while a folder whose owner still lives is left alone;
//   - the watched launch: a program counts as running until it ends, and on
//     POSIX until what it started in the background has ended too; a file
//     that cannot be executed is reported rather than silently dropped.
//
// Framework-independent: builds from the module's own sources (core plus
// this platform's backend), with no display and no archive.
// Version: 1.0.0
// Last Modified: 2026-09-28
// Author: UltraCanvas Framework

#include "UltraCanvasArchiveRun.h"
#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;
using namespace UltraCanvas;

static int failures = 0;
static int checks = 0;

#define CHECK(cond, what) do { \
    ++checks; \
    if (cond) { \
        std::printf("  PASS  %s\n", what); \
    } else { \
        ++failures; \
        std::printf("  FAIL  %s (%s:%d)\n", what, __FILE__, __LINE__); \
    } \
} while (0)

// Polls until the program is no longer running, or `limit` passes.
static bool WaitForEnd(WatchedProcess& p, std::chrono::milliseconds limit) {
    const auto until = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < until) {
        if (!p.IsRunning()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return !p.IsRunning();
}

// A process id that is certainly not running: far above any pid_max, and
// on Windows a multiple of 4 above anything handed out in a test's lifetime.
static constexpr uint64_t kDeadProcessId = 4000000000ull;

static void WriteProgram(const fs::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
#if !defined(_WIN32)
    ::chmod(PathToUtf8(path).c_str(), 0755);
#endif
}

int main() {
    std::printf("── UltraCanvasArchiveRun ──\n");

    // ===== WHICH ENTRIES ARE PROGRAMS =====
    {
        const auto W = ArchiveRunHost::Windows;
        const auto P = ArchiveRunHost::Posix;
        CHECK(IsRunnableArchiveEntry("exe", false, W), "Windows: .exe runs");
        CHECK(IsRunnableArchiveEntry("EXE", false, W), "Windows: the extension's case does not matter");
        CHECK(IsRunnableArchiveEntry("com", false, W) && IsRunnableArchiveEntry("bat", false, W) &&
              IsRunnableArchiveEntry("cmd", false, W) && IsRunnableArchiveEntry("msi", false, W),
              "Windows: .com, .bat, .cmd and .msi run");
        CHECK(!IsRunnableArchiveEntry("txt", true, W),
              "Windows: an execute bit a Unix tool recorded does not make a program");
        CHECK(!IsRunnableArchiveEntry("ps1", false, W) && !IsRunnableArchiveEntry("dll", false, W),
              "Windows: .ps1 and .dll are not started");
        CHECK(IsRunnableArchiveEntry("", true, P), "POSIX: an executable entry without an extension runs");
        CHECK(IsRunnableArchiveEntry("sh", true, P), "POSIX: an executable script runs");
        CHECK(!IsRunnableArchiveEntry("sh", false, P), "POSIX: a script without the execute bit does not");
        CHECK(IsRunnableArchiveEntry("appimage", false, P),
              "POSIX: an AppImage runs even when the zip lost its execute bit");
        CHECK(!IsRunnableArchiveEntry("exe", false, P),
              "POSIX: a Windows .exe is not a native program");
    }

    const fs::path root = fs::temp_directory_path() / "uc-archiverun-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const std::string rootUtf8 = PathToUtf8(root);

    // ===== RUN FOLDERS =====
    std::string first, second, error;
    CHECK(CreateArchiveRunFolder(rootUtf8, first, error) && fs::is_directory(PathFromUtf8(first)),
          "a run folder is made, creating its root");
    CHECK(CreateArchiveRunFolder(rootUtf8, second, error) && second != first,
          "a second run gets a folder of its own");
    {
        std::ifstream marker(PathFromUtf8(first + ".run"));
        uint64_t id = 0;
        marker >> id;
        CHECK(id == CurrentProcessId(), "the marker names the process that made the folder");
    }
    RecordArchiveRunProcess(first, kDeadProcessId);
    {
        std::ifstream marker(PathFromUtf8(first + ".run"));
        uint64_t a = 0, b = 0;
        marker >> a >> b;
        CHECK(a == CurrentProcessId() && b == kDeadProcessId,
              "a started program is added to the marker");
    }
    CHECK(IsProcessAlive(CurrentProcessId()), "this process is alive");
    CHECK(!IsProcessAlive(kDeadProcessId), "a process id nobody has is not");

    std::ofstream(PathFromUtf8(first) / "data.txt") << "left by the program";
    CHECK(SweepArchiveRunFolders(rootUtf8) == 0 && fs::exists(PathFromUtf8(first)),
          "a sweep leaves folders whose owner is alive");
    CHECK(RemoveArchiveRunFolder(first) && !fs::exists(PathFromUtf8(first)) &&
          !fs::exists(PathFromUtf8(first + ".run")),
          "a folder nothing holds is removed with its marker");
    CHECK(RemoveArchiveRunFolder(first), "removing a folder already gone succeeds");

    // An owner that went away: the marker names only dead processes.
    {
        const fs::path orphan = root / "999999-1";
        fs::create_directories(orphan / "sub", ec);
        std::ofstream(orphan / "sub" / "x.dll") << "x";
        std::ofstream(root / "999999-1.run") << kDeadProcessId << "\n";
        const fs::path leftover = root / "999999-2.remove";
        fs::create_directories(leftover, ec);
        std::ofstream(root / "999999-3.run") << kDeadProcessId << "\n";   // folder gone
        const fs::path fresh = root / "999999-4";   // being made right now
        fs::create_directories(fresh, ec);

        const int removed = SweepArchiveRunFolders(rootUtf8);
        CHECK(!fs::exists(orphan) && !fs::exists(root / "999999-1.run"),
              "a sweep removes a folder whose owner is gone");
        CHECK(!fs::exists(leftover), "a sweep finishes an interrupted removal");
        CHECK(!fs::exists(root / "999999-3.run"), "a sweep drops a marker whose folder is gone");
        CHECK(fs::exists(fresh), "a sweep leaves a folder that has no marker yet");
        CHECK(fs::exists(PathFromUtf8(second)), "a sweep leaves this process's own folder");
        CHECK(removed == 2, "the sweep counts what it removed");
        fs::remove_all(fresh, ec);
    }

    // ===== THE WATCHED LAUNCH =====
    std::string runFolder;
    CHECK(CreateArchiveRunFolder(rootUtf8, runFolder, error), "a folder to run from");
    const fs::path runPath = PathFromUtf8(runFolder);
#if defined(_WIN32)
    const fs::path quick = runPath / "quick.cmd";
    WriteProgram(quick, "@echo off\r\nexit /b 0\r\n");
    const fs::path notProgram = runPath / "readme.txt";
    WriteProgram(notProgram, "not a program");
#else
    const fs::path quick = runPath / "quick.sh";
    WriteProgram(quick, "#!/bin/sh\nexit 0\n");
    const fs::path notProgram = runPath / "readme.txt";
    std::ofstream(notProgram) << "not a program";   // no execute bit
#endif
    {
        std::string launchError;
        auto p = LaunchWatchedProgram(PathToUtf8(quick), runFolder, launchError);
        CHECK(p != nullptr, "a program is started");
        if (p) {
            CHECK(p->GetProcessId() != 0, "and has a process id");
            CHECK(WaitForEnd(*p, std::chrono::seconds(20)), "it is seen to end");
        } else {
            std::printf("        (%s)\n", launchError.c_str());
        }
    }
    {
        std::string launchError;
        auto p = LaunchWatchedProgram(PathToUtf8(notProgram), runFolder, launchError);
        CHECK(p == nullptr && !launchError.empty(),
              "a file that cannot be executed is reported, not dropped");
    }
    {
        // The program leaves a child behind in the background and exits at
        // once - the installer that hands over to a second stage.
#if defined(_WIN32)
        const fs::path handover = runPath / "handover.cmd";
        WriteProgram(handover, "@echo off\r\n"
                               "start \"\" /b cmd /d /c \"ping -n 3 127.0.0.1 >nul\"\r\n"
                               "exit /b 0\r\n");
#else
        const fs::path handover = runPath / "handover.sh";
        WriteProgram(handover, "#!/bin/sh\nsleep 1 &\nexit 0\n");
#endif
        std::string launchError;
        auto p = LaunchWatchedProgram(PathToUtf8(handover), runFolder, launchError);
        CHECK(p != nullptr, "a program that hands over is started");
        if (p) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            CHECK(p->IsRunning(), "it counts as running while what it started runs");
            CHECK(WaitForEnd(*p, std::chrono::seconds(20)),
                  "and as ended once that has ended too");
        }
    }
#if !defined(_WIN32)
    {
        // The program's working directory is its folder.
        const fs::path where = runPath / "where.sh";
        WriteProgram(where, "#!/bin/sh\npwd > cwd.txt\n");
        std::string launchError;
        auto p = LaunchWatchedProgram(PathToUtf8(where), runFolder, launchError);
        if (p) WaitForEnd(*p, std::chrono::seconds(20));
        std::string cwd;
        std::getline(std::ifstream(runPath / "cwd.txt"), cwd);
        CHECK(fs::equivalent(PathFromUtf8(cwd), runPath, ec),
              "the program runs in the folder it was unpacked to");
    }
#endif
    CHECK(RemoveArchiveRunFolder(runFolder) && !fs::exists(runPath),
          "the run folder is removed once the program has ended");

    CHECK(CopyDownloadMarking(PathToUtf8(root / "no-such-archive.zip"), rootUtf8) == 0,
          "an archive without a download mark marks nothing");

    fs::remove_all(root, ec);
    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
