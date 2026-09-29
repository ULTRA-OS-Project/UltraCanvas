// OS/MSWindows/UltraCanvasWindowsArchiveRun.cpp
// Windows backend for UltraCanvasArchiveRun: the watched launch and the
// download mark. The run folders and the rest are in the core file.
//
// The launch puts the program into a job object before it runs a single
// instruction (it is created suspended), so the job holds it and everything
// it starts. That is what "has it ended" has to mean for a program run out of
// an archive: a setup.exe typically unpacks a second stage into %TEMP%,
// starts it and exits at once, and a folder deleted at that moment would be
// pulled from under the second stage. The job counts the second stage as
// still running.
//
// The job is never set to kill on close: UltraFiler quitting must not end
// the programs it started.
// Version: 1.0.0
// Last Modified: 2026-09-28
// Author: UltraCanvas Framework

#include "UltraCanvasArchiveRun.h"
#include "UltraCanvasPathUtf8.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace UltraCanvas {

    namespace {

        class WindowsWatchedProcess : public WatchedProcess {
        public:
            WindowsWatchedProcess(HANDLE process, HANDLE job, DWORD id)
                : process(process), job(job), id(id) {}

            ~WindowsWatchedProcess() override {
                // Closing the job handle leaves its processes running: the
                // job was not made to kill on close.
                if (job) CloseHandle(job);
                if (process) CloseHandle(process);
            }

            bool IsRunning() override {
                if (job) {
                    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info = {};
                    if (QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
                                                  &info, sizeof(info), nullptr))
                        return info.ActiveProcesses > 0;
                }
                return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
            }

            uint64_t GetProcessId() const override { return id; }

        private:
            HANDLE process;
            HANDLE job;
            DWORD id;
        };

        // The standard argv quoting rule (the one CommandLineToArgvW and the
        // C runtime undo): quotes around an argument with a blank or a quote
        // in it, embedded quotes and the backslashes before them escaped.
        std::wstring QuoteArgument(const std::wstring& arg) {
            if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos)
                return arg;
            std::wstring out = L"\"";
            size_t backslashes = 0;
            for (wchar_t c : arg) {
                if (c == L'\\') { ++backslashes; continue; }
                if (c == L'"') out.append(backslashes * 2 + 1, L'\\');
                else out.append(backslashes, L'\\');
                backslashes = 0;
                out += c;
            }
            out.append(backslashes * 2, L'\\');
            out += L'"';
            return out;
        }

        std::wstring SystemProgram(const wchar_t* fileName) {
            wchar_t dir[MAX_PATH] = {};
            const UINT n = GetSystemDirectoryW(dir, MAX_PATH);
            if (n == 0 || n >= MAX_PATH) return fileName;
            return std::wstring(dir) + L"\\" + fileName;
        }

        std::wstring CommandInterpreter() {
            wchar_t buf[MAX_PATH] = {};
            const DWORD n = GetEnvironmentVariableW(L"ComSpec", buf, MAX_PATH);
            if (n > 0 && n < MAX_PATH) return buf;
            return SystemProgram(L"cmd.exe");
        }

        std::string LowerExtension(const fs::path& p) {
            std::string ext = PathToUtf8(p.extension());
            if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return ext;
        }

        std::string SystemMessage(DWORD err) {
            wchar_t* text = nullptr;
            const DWORD n = FormatMessageW(
                    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr, err, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
            std::wstring w = (n && text) ? std::wstring(text, n) : std::wstring();
            if (text) LocalFree(text);
            while (!w.empty() && (w.back() == L'\r' || w.back() == L'\n' ||
                                  w.back() == L' ' || w.back() == L'.'))
                w.pop_back();
            if (w.empty()) return "error " + std::to_string(err);
            return PathToUtf8(fs::path(w));   // path-string-ok: built from a wide string
        }

        HANDLE MakeJob() {
            HANDLE job = CreateJobObjectW(nullptr, nullptr);
            if (!job) return nullptr;
            // A program that starts a child outside its job on purpose
            // (CREATE_BREAKAWAY_FROM_JOB - some updaters and browsers do) would
            // fail to start it at all in a job that forbids it. Allowing it
            // costs only the watch on that one child.
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_BREAKAWAY_OK;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                    &limits, sizeof(limits));
            return job;
        }

    } // namespace

    std::unique_ptr<WatchedProcess> LaunchWatchedProgram(
            const std::string& programPath,
            const std::string& workingDirectory,
            std::string& outError) {
        outError.clear();
        if (programPath.empty()) {
            outError = "No program to start.";
            return nullptr;
        }
        const fs::path program = PathFromUtf8(programPath);
        const std::wstring programW = program.native();
        const std::string name = PathToUtf8(program.filename());
        const std::string ext = LowerExtension(program);

        // What actually starts, and with which command line. Only a program
        // file is started through CreateProcess itself; a batch file needs
        // its interpreter and an installer package needs msiexec.
        std::wstring application;
        std::wstring commandLine;
        if (ext == "exe" || ext == "com") {
            application = programW;
            commandLine = QuoteArgument(programW);
        } else if (ext == "bat" || ext == "cmd") {
            // /d: no AutoRun commands from the registry; /s /c "...": run the
            // quoted line as it is, whatever quotes it contains.
            application = CommandInterpreter();
            commandLine = L"cmd.exe /d /s /c \"" + QuoteArgument(programW) + L"\"";
        } else if (ext == "msi") {
            application = SystemProgram(L"msiexec.exe");
            commandLine = L"msiexec.exe /i " + QuoteArgument(programW);
        } else {
            outError = "\"" + name + "\" is not a program Windows can start.";
            return nullptr;
        }

        const std::wstring dirW = PathFromUtf8(workingDirectory).native();
        std::vector<wchar_t> mutableCmd(commandLine.begin(), commandLine.end());
        mutableCmd.push_back(L'\0');

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        // Suspended, so it is in the job before it can start anything; a
        // console of its own, as Explorer gives a console program. (Not a
        // new process group: that would switch Ctrl+C off in its console.)
        const BOOL started = CreateProcessW(
                application.c_str(), mutableCmd.data(), nullptr, nullptr, FALSE,
                CREATE_SUSPENDED | CREATE_NEW_CONSOLE,
                nullptr, dirW.empty() ? nullptr : dirW.c_str(), &si, &pi);
        if (started) {
            HANDLE job = MakeJob();
            if (job && !AssignProcessToJobObject(job, pi.hProcess)) {
                // Windows 7 cannot nest jobs, and this application may be in
                // one already: watch the program alone then.
                CloseHandle(job);
                job = nullptr;
            }
            ResumeThread(pi.hThread);
            CloseHandle(pi.hThread);
            return std::make_unique<WindowsWatchedProcess>(pi.hProcess, job,
                                                           pi.dwProcessId);
        }

        const DWORD err = GetLastError();
        if (err != ERROR_ELEVATION_REQUIRED) {
            outError = "Could not start \"" + name + "\": " + SystemMessage(err);
            return nullptr;
        }
        // Its manifest asks for administrator rights, which CreateProcess
        // cannot give. The shell can: it shows the UAC prompt. The elevated
        // process cannot join this unelevated process's job, so it is watched
        // alone - an elevated installer that hands over to a second stage is
        // then covered by RemoveArchiveRunFolder, which leaves the folder as
        // long as a file in it is open.
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        sei.lpVerb = L"open";
        sei.lpFile = programW.c_str();
        sei.lpDirectory = dirW.empty() ? nullptr : dirW.c_str();
        sei.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&sei)) {
            const DWORD shellErr = GetLastError();
            outError = shellErr == ERROR_CANCELLED
                    ? "\"" + name + "\" was not started: it needs administrator "
                      "rights, and they were not given."
                    : "Could not start \"" + name + "\": " + SystemMessage(shellErr);
            return nullptr;
        }
        if (!sei.hProcess) {
            outError = "\"" + name + "\" was handed to Windows, but cannot be watched.";
            return nullptr;
        }
        return std::make_unique<WindowsWatchedProcess>(
                sei.hProcess, nullptr, ::GetProcessId(sei.hProcess));
    }

    bool IsProcessAlive(uint64_t processId) {
        if (processId == 0 || processId > 0xFFFFFFFFull) return false;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               static_cast<DWORD>(processId));
        if (!h) {
            // Denied: it exists, it is just not ours to look at (an elevated
            // installer, another user's program).
            return GetLastError() == ERROR_ACCESS_DENIED;
        }
        DWORD code = 0;
        const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
        CloseHandle(h);
        return alive;
    }

    uint64_t CurrentProcessId() {
        return static_cast<uint64_t>(GetCurrentProcessId());
    }

    int CopyDownloadMarking(const std::string& sourceFile, const std::string& folder) {
        if (sourceFile.empty() || folder.empty()) return 0;
        // The mark is an alternate data stream: "<file>:Zone.Identifier", a
        // few lines of text naming the zone (3 = internet) and the address.
        const std::wstring stream =
                PathFromUtf8(sourceFile).native() + L":Zone.Identifier";
        HANDLE in = CreateFileW(stream.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
        if (in == INVALID_HANDLE_VALUE) return 0;   // not downloaded, or FAT
        std::vector<char> mark(64 * 1024);
        DWORD got = 0;
        const BOOL read = ReadFile(in, mark.data(), static_cast<DWORD>(mark.size()),
                                   &got, nullptr);
        CloseHandle(in);
        if (!read || got == 0) return 0;
        mark.resize(got);

        int marked = 0;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(PathFromUtf8(folder), ec), end;
             !ec && it != end; it.increment(ec)) {
            std::error_code typeEc;
            if (!it->is_regular_file(typeEc)) continue;
            const std::wstring target = it->path().native() + L":Zone.Identifier";
            HANDLE out = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (out == INVALID_HANDLE_VALUE) continue;
            DWORD written = 0;
            if (WriteFile(out, mark.data(), static_cast<DWORD>(mark.size()),
                          &written, nullptr) && written == mark.size())
                ++marked;
            CloseHandle(out);
        }
        return marked;
    }

} // namespace UltraCanvas
