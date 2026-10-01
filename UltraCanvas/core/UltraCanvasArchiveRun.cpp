// core/UltraCanvasArchiveRun.cpp
// "Extract and run" - see UltraCanvasArchiveRun.h. The run folders and the
// rule for which entries are programs, for every platform; and the POSIX
// launch, which Linux, the BSDs and macOS share. Windows has its own launch
// and download mark in OS/MSWindows/UltraCanvasWindowsArchiveRun.cpp.
// Version: 1.0.0
// Last Modified: 2026-09-28
// Author: UltraCanvas Framework

#include "UltraCanvasArchiveRun.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace UltraCanvas {

    namespace {

        // The marker beside each run folder, and the name a folder takes
        // while it is being removed. A sweep that finds the second knows
        // nothing held that folder when it was renamed.
        constexpr const char* kMarkerSuffix = ".run";
        constexpr const char* kRemovingSuffix = ".remove";

        // A folder with no marker yet is being made by another process right
        // now (the folder comes first, the marker a moment later) - unless it
        // is older than this, when its maker died in between.
        constexpr auto kUnmarkedGrace = std::chrono::minutes(1);

        bool EndsWith(const std::string& s, const std::string& suffix) {
            return s.size() >= suffix.size() &&
                   s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        std::vector<uint64_t> ReadMarker(const fs::path& marker) {
            std::vector<uint64_t> ids;
            std::ifstream in(marker);
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty()) continue;
                uint64_t id = 0;
                bool digits = true;
                for (char c : line) {
                    if (c == '\r') break;
                    if (c < '0' || c > '9') { digits = false; break; }
                    id = id * 10 + static_cast<uint64_t>(c - '0');
                }
                if (digits && id != 0) ids.push_back(id);
            }
            return ids;
        }

        bool AppendToMarker(const fs::path& marker, uint64_t processId) {
            std::ofstream out(marker, std::ios::app);
            if (!out) return false;
            out << processId << '\n';
            return static_cast<bool>(out);
        }

        // Renames `folder` out of the way and deletes it. The rename is the
        // test: see RemoveArchiveRunFolder.
        bool RemoveHeldFolder(const fs::path& folder) {
            std::error_code ec;
            fs::path removing = UltraCanvas::PathFromUtf8(folder);
            removing += kRemovingSuffix;
            // A leftover of an interrupted removal by the same name is gone
            // for good already; clear it so the rename has somewhere to go.
            if (fs::exists(removing, ec)) fs::remove_all(removing, ec);
            ec.clear();
            fs::rename(UltraCanvas::PathFromUtf8(folder), removing, ec);
            if (ec) return false;   // something still holds a file in it
            fs::path marker = UltraCanvas::PathFromUtf8(folder);
            marker += kMarkerSuffix;
            fs::remove(marker, ec);
            ec.clear();
            fs::remove_all(removing, ec);
            // A file that resisted deletion after all is left to the next
            // sweep, which removes anything ending in ".remove".
            return true;
        }

    } // namespace

    // ===== WHICH ARCHIVE ENTRIES ARE PROGRAMS =====
    ArchiveRunHost CurrentArchiveRunHost() {
#if defined(_WIN32)
        return ArchiveRunHost::Windows;
#else
        return ArchiveRunHost::Posix;
#endif
    }

    bool IsRunnableArchiveEntry(const std::string& extension, bool executableBit,
                                ArchiveRunHost host) {
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (host == ArchiveRunHost::Windows) {
            // Windows runs by extension and nothing else; an execute bit a
            // Unix tool recorded means nothing here. .ps1 stays out on
            // purpose: a double-click on one opens it in an editor, not
            // PowerShell, and "run from an archive" should not be the one
            // place where that differs.
            return ext == "exe" || ext == "com" || ext == "bat" ||
                   ext == "cmd" || ext == "msi";
        }
        // POSIX runs what is marked executable. An AppImage downloaded inside
        // a zip usually lost its mark on the way (zip from a Windows machine
        // records none), and is a program by its name alone.
        return executableBit || ext == "appimage";
    }

    // ===== RUN FOLDERS =====
    std::string DefaultArchiveRunRoot() {
        std::error_code ec;
        fs::path temp = fs::temp_directory_path(ec);
        if (ec || temp.empty()) return std::string();
        return PathToUtf8(temp / "UltraCanvas-Run");
    }

    bool CreateArchiveRunFolder(const std::string& root, std::string& outFolder,
                                std::string& outError) {
        outFolder.clear();
        outError.clear();
        if (root.empty()) {
            outError = "There is no temporary folder to unpack the program into.";
            return false;
        }
        const fs::path rootPath = PathFromUtf8(root);
        std::error_code ec;
        fs::create_directories(rootPath, ec);
        if (!fs::is_directory(rootPath, ec)) {
            outError = "Could not create the folder " + root + ".";
            return false;
        }
        // "<process id>-<n>": short, because it becomes part of every path the
        // program sees, and unique across the processes sharing the root
        // because create_directory fails on a name already taken.
        const std::string prefix = std::to_string(CurrentProcessId()) + "-";
        for (int n = 1; n < 10000; ++n) {
            const fs::path folder = rootPath / PathFromUtf8(prefix + std::to_string(n));
            fs::path marker = UltraCanvas::PathFromUtf8(folder);
            marker += kMarkerSuffix;
            if (fs::exists(marker, ec)) continue;   // a folder mid-removal
            if (!fs::create_directory(UltraCanvas::PathFromUtf8(folder), ec) || ec) {
                if (!fs::exists(UltraCanvas::PathFromUtf8(folder))) {
                    outError = "Could not create a folder in " + root + ": " +
                               ec.message();
                    return false;
                }
                continue;
            }
            if (!AppendToMarker(marker, CurrentProcessId())) {
                fs::remove(UltraCanvas::PathFromUtf8(folder), ec);
                outError = "Could not write in " + root + ".";
                return false;
            }
            outFolder = PathToUtf8(folder);
            return true;
        }
        outError = "Too many programs are unpacked in " + root + " already.";
        return false;
    }

    void RecordArchiveRunProcess(const std::string& folder, uint64_t processId) {
        if (folder.empty() || processId == 0) return;
        fs::path marker = PathFromUtf8(folder);
        marker += kMarkerSuffix;
        AppendToMarker(marker, processId);
    }

    bool RemoveArchiveRunFolder(const std::string& folder) {
        if (folder.empty()) return true;
        const fs::path path = PathFromUtf8(folder);
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            fs::path marker = path;
            marker += kMarkerSuffix;
            fs::remove(marker, ec);
            return true;
        }
        return RemoveHeldFolder(path);
    }

    int SweepArchiveRunFolders(const std::string& root) {
        if (root.empty()) return 0;
        const fs::path rootPath = PathFromUtf8(root);
        std::error_code ec;
        if (!fs::is_directory(rootPath, ec)) return 0;

        std::vector<fs::path> folders;
        std::vector<fs::path> markers;
        std::vector<fs::path> removing;
        for (fs::directory_iterator it(rootPath, ec), end; !ec && it != end;
             it.increment(ec)) {
            const std::string name = PathToUtf8(it->path().filename());
            std::error_code typeEc;
            const bool isDir = it->is_directory(typeEc);
            if (isDir && EndsWith(name, kRemovingSuffix)) removing.push_back(it->path());
            else if (isDir) folders.push_back(it->path());
            else if (EndsWith(name, kMarkerSuffix)) markers.push_back(it->path());
        }

        int removed = 0;
        for (const fs::path& p : removing) {
            std::error_code rmEc;
            fs::remove_all(p, rmEc);
            if (!rmEc) ++removed;
        }
        for (const fs::path& folder : folders) {
            fs::path marker = UltraCanvas::PathFromUtf8(folder);
            marker += kMarkerSuffix;
            std::error_code mEc;
            if (fs::exists(marker, mEc)) {
                bool held = false;
                for (uint64_t id : ReadMarker(marker))
                    if (IsProcessAlive(id)) { held = true; break; }
                if (held) continue;
            } else {
                const auto written = fs::last_write_time(UltraCanvas::PathFromUtf8(folder), mEc);
                if (!mEc && fs::file_time_type::clock::now() - written < kUnmarkedGrace)
                    continue;
            }
            if (RemoveHeldFolder(folder)) ++removed;
        }
        // A marker whose folder is gone (removed by hand, say) goes too.
        for (const fs::path& marker : markers) {
            const std::string name = PathToUtf8(marker.filename());
            const fs::path folder = rootPath /
                    PathFromUtf8(name.substr(0, name.size() - std::strlen(kMarkerSuffix)));
            std::error_code mEc;
            if (!fs::exists(UltraCanvas::PathFromUtf8(folder), mEc)) fs::remove(marker, mEc);
        }
        return removed;
    }

#if !defined(_WIN32)
    // ===== POSIX: THE LAUNCH =====
    namespace {

        class PosixWatchedProcess : public WatchedProcess {
        public:
            explicit PosixWatchedProcess(pid_t pid) : pid(pid) {}

            ~PosixWatchedProcess() override {
                // The program is this process's child until it ends, and a
                // child nobody waits for stays a zombie. Hand the wait to a
                // thread of its own: it returns when the program does, or
                // goes down with this application.
                if (!leaderReaped) {
                    const pid_t child = pid;
                    std::thread([child]() {
                        int status = 0;
                        while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
                    }).detach();
                }
            }

            bool IsRunning() override {
                if (!leaderReaped) {
                    int status = 0;
                    const pid_t r = ::waitpid(pid, &status, WNOHANG);
                    if (r == 0) return true;
                    leaderReaped = true;   // ended (r == pid), or not ours to wait for
                }
                // The program led a process group of its own (setsid), and
                // what it started without a session of its own is still in
                // it: an existing group means someone is still at work.
                // EPERM answers for a member that changed user (sudo).
                if (::kill(-pid, 0) == 0) return true;
                return errno == EPERM;
            }

            uint64_t GetProcessId() const override {
                return static_cast<uint64_t>(pid);
            }

        private:
            pid_t pid;
            bool leaderReaped = false;
        };

        void SetCloseOnExec(int fd) {
            const int flags = ::fcntl(fd, F_GETFD);
            if (flags >= 0) ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
        }

    } // namespace

    std::unique_ptr<WatchedProcess> LaunchWatchedProgram(
            const std::string& programPath,
            const std::string& workingDirectory,
            std::string& outError) {
        outError.clear();
#if defined(__EMSCRIPTEN__)
        (void)programPath;
        (void)workingDirectory;
        outError = "Programs cannot be started from a web page.";
        return nullptr;
#else
        if (programPath.empty()) {
            outError = "No program to start.";
            return nullptr;
        }
        const std::string name = PathToUtf8(PathFromUtf8(programPath).filename());
        // The child reports a failed exec through this pipe; a successful
        // exec closes it (close-on-exec), which the parent reads as success.
        // A detached launch cannot tell the two apart, and "nothing happened"
        // is the worst answer to a double-click.
        int report[2] = {-1, -1};
        if (::pipe(report) != 0) {
            outError = "Could not start \"" + name + "\": " + std::strerror(errno);
            return nullptr;
        }
        SetCloseOnExec(report[0]);
        SetCloseOnExec(report[1]);

        const pid_t pid = ::fork();
        if (pid < 0) {
            const int err = errno;
            ::close(report[0]);
            ::close(report[1]);
            outError = "Could not start \"" + name + "\": " + std::strerror(err);
            return nullptr;
        }
        if (pid == 0) {
            ::close(report[0]);
            // A session (and so a process group) of its own: the program is
            // not ended by a signal to this application's group, and what it
            // starts is recognisable as its own (see IsRunning).
            ::setsid();
            if (!workingDirectory.empty() && ::chdir(workingDirectory.c_str()) != 0) {
                // Starting in the wrong folder still beats not starting.
            }
            // The parent may block SIGPIPE on some thread; the program should
            // start with the defaults.
            sigset_t none;
            sigemptyset(&none);
            ::sigprocmask(SIG_SETMASK, &none, nullptr);
            char* const argv[] = {const_cast<char*>(programPath.c_str()), nullptr};
            ::execv(programPath.c_str(), argv);
            const int err = errno;
            ssize_t written = ::write(report[1], &err, sizeof(err));
            (void)written;
            ::_exit(127);
        }
        ::close(report[1]);
        int childErr = 0;
        ssize_t got = 0;
        do {
            got = ::read(report[0], &childErr, sizeof(childErr));
        } while (got < 0 && errno == EINTR);
        ::close(report[0]);
        if (got == static_cast<ssize_t>(sizeof(childErr))) {
            int status = 0;
            ::waitpid(pid, &status, 0);
            outError = "Could not start \"" + name + "\": " + std::strerror(childErr);
            if (childErr == EACCES)
                outError += " (the folder it was unpacked to may be on a file "
                            "system that does not let programs run)";
            return nullptr;
        }
        return std::make_unique<PosixWatchedProcess>(pid);
#endif
    }

    bool IsProcessAlive(uint64_t processId) {
        if (processId == 0) return false;
        if (::kill(static_cast<pid_t>(processId), 0) == 0) return true;
        return errno == EPERM;
    }

    uint64_t CurrentProcessId() {
        return static_cast<uint64_t>(::getpid());
    }

    int CopyDownloadMarking(const std::string& /*sourceFile*/,
                            const std::string& /*folder*/) {
        // macOS's quarantine attribute is the counterpart; Gatekeeper looks at
        // it on a program's first start. Not carried over yet.
        return 0;
    }
#endif

} // namespace UltraCanvas
