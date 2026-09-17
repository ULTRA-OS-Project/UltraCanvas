// OS/MSWindows/UltraCanvasWindowsElevatedFileOperations.cpp
// Windows backend for UltraCanvasElevatedFileOperations: the platform where
// "retry as administrator" is a thing. A standard user's process cannot
// raise its own rights, so the retry starts a second copy of this executable
// with the shell's "runas" verb. That is exactly what Explorer's shield
// button does: Windows puts up its consent prompt (on the secure desktop,
// so nothing here can answer it), and on Yes the copy runs elevated, does
// the work and exits. Consent is asked for every retry; nothing is cached.
//
// The two processes talk through the command line (in: the paths) and a
// report file the caller creates in its temp folder and names on that
// command line (out: what could not be deleted, and why). The helper reads
// nothing else, so a tampered file cannot widen what the user consented to.
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework
#include "UltraCanvasElevatedFileOperations.h"

// TokenElevation needs the Vista-era declarations; the same guard as the
// file-associations backend, and NTDDI_VERSION kept consistent with it.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0600
#endif
#if !defined(NTDDI_VERSION) || (NTDDI_VERSION >> 16) < _WIN32_WINNT
#  undef NTDDI_VERSION
#  define NTDDI_VERSION (_WIN32_WINNT << 16)
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace UltraCanvas {
    namespace ElevatedFileOperations {

        namespace {

            // Local UTF-8 <-> UTF-16 so this backend links without
            // UltraCanvasUtils (the standalone test builds it on its own).
            std::wstring ToWide(const std::string& utf8) {
                if (utf8.empty()) return {};
                const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                                  static_cast<int>(utf8.size()),
                                                  nullptr, 0);
                if (n <= 0) return {};
                std::wstring out(static_cast<size_t>(n), L'\0');
                MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                    static_cast<int>(utf8.size()), out.data(), n);
                return out;
            }

            std::string ToUtf8(const std::wstring& wide) {
                if (wide.empty()) return {};
                const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                                  static_cast<int>(wide.size()),
                                                  nullptr, 0, nullptr, nullptr);
                if (n <= 0) return {};
                std::string out(static_cast<size_t>(n), '\0');
                WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                    static_cast<int>(wide.size()), out.data(), n,
                                    nullptr, nullptr);
                return out;
            }

            // The system's own sentence for a Win32 error, without the
            // trailing newline FormatMessage appends.
            std::string Win32ErrorText(DWORD error) {
                wchar_t* buffer = nullptr;
                const DWORD n = FormatMessageW(
                        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS,
                        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                        reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
                std::string text = n && buffer ? ToUtf8(std::wstring(buffer, n)) : "";
                if (buffer) LocalFree(buffer);
                while (!text.empty() && (text.back() == '\r' || text.back() == '\n' ||
                                         text.back() == ' ' || text.back() == '.'))
                    text.pop_back();
                if (text.empty()) text = "Windows error " + std::to_string(error);
                return text;
            }

            // Full path of this executable, long paths included.
            std::wstring OwnExecutablePath() {
                std::wstring path(MAX_PATH, L'\0');
                for (;;) {
                    const DWORD n = GetModuleFileNameW(nullptr, path.data(),
                                                       static_cast<DWORD>(path.size()));
                    if (n == 0) return {};
                    if (n < path.size()) { path.resize(n); return path; }
                    path.resize(path.size() * 2);
                }
            }

            // An empty file in the user's temp folder for the helper's report.
            // GetTempFileName creates it, which is what reserves the name.
            std::wstring CreateReportFile() {
                wchar_t dir[MAX_PATH + 1] = {};
                const DWORD n = GetTempPathW(MAX_PATH + 1, dir);
                if (n == 0 || n > MAX_PATH) return {};
                wchar_t name[MAX_PATH + 1] = {};
                if (GetTempFileNameW(dir, L"UCE", 0, name) == 0) return {};
                return name;
            }

            std::string ReadWholeFile(const std::wstring& path) {
                std::ifstream in(fs::path(path), std::ios::binary);
                if (!in) return {};
                return std::string(std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>());
            }

            void TruncateFile(const std::wstring& path) {
                std::ofstream out(fs::path(path), std::ios::binary | std::ios::trunc);
            }

            // The whole command line must stay under the 32767-character
            // limit CreateProcess imposes; the executable path is counted
            // separately, so this leaves room for a long one.
            constexpr size_t kMaxParametersLength = 30000;

        } // namespace

        bool NativeProcessIsElevated() {
            HANDLE token = nullptr;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
                return false;
            TOKEN_ELEVATION elevation = {};
            DWORD returned = 0;
            const bool ok = GetTokenInformation(token, TokenElevation, &elevation,
                                                sizeof(elevation), &returned) != FALSE;
            CloseHandle(token);
            return ok && elevation.TokenIsElevated != 0;
        }

        bool NativeIsPermissionFailure(const std::error_code& ec) {
            // libstdc++ and libc++ both report a failed DeleteFileW /
            // RemoveDirectoryW as the raw Win32 code in the system category;
            // the errc comparison covers a runtime that maps it to EACCES.
            if (ec.category() == std::system_category() &&
                ec.value() == static_cast<int>(ERROR_ACCESS_DENIED))
                return true;
            return ec == std::errc::permission_denied;
        }

        std::vector<std::string> NativeCommandLineArguments() {
            // Straight from the system: the narrow argv the C runtime builds
            // goes through the process code page, which mangles a path with
            // characters outside it on a Windows older than 1903 (where the
            // manifest's UTF-8 code page does not apply).
            int argc = 0;
            wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            if (!argv) return {};
            std::vector<std::string> out;
            out.reserve(static_cast<size_t>(argc));
            for (int i = 0; i < argc; ++i) out.push_back(ToUtf8(argv[i]));
            LocalFree(argv);
            return out;
        }

        bool NativeRunElevatedDelete(const std::vector<std::string>& paths,
                                     ElevatedDeleteResult& out) {
            out = ElevatedDeleteResult{};
            const std::wstring exe = OwnExecutablePath();
            if (exe.empty()) {
                out.outcome = ElevatedOutcome::Failed;
                out.error = "Could not find this application's executable.";
                return true;
            }
            const std::wstring report = CreateReportFile();
            if (report.empty()) {
                out.outcome = ElevatedOutcome::Failed;
                out.error = "Could not create a temporary file for the result.";
                return true;
            }

            // ShellExecuteEx may hand the verb to a shell extension, which
            // wants COM on the calling thread - and this is a worker thread
            // with none. Balanced below; an already initialised thread
            // (S_FALSE) is fine, a mode mismatch (RPC_E_CHANGED_MODE) means
            // somebody else set it up and it stays theirs.
            const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                        COINIT_DISABLE_OLE1DDE);
            const bool uninitCom = SUCCEEDED(com);

            const std::string prefix = std::string(kHelperDeleteFlag) + " " +
                                       QuoteCommandLineArgument(ToUtf8(report)) + " ";
            const auto runs = SplitIntoCommandLines(paths, prefix.size(),
                                                    kMaxParametersLength);
            out.outcome = ElevatedOutcome::Completed;
            for (const std::vector<std::string>& run : runs) {
                std::string parameters = prefix;
                for (size_t i = 0; i < run.size(); ++i) {
                    if (i) parameters += ' ';
                    parameters += QuoteCommandLineArgument(run[i]);
                }
                const std::wstring parametersW = ToWide(parameters);

                SHELLEXECUTEINFOW info = {};
                info.cbSize = sizeof(info);
                // NOCLOSEPROCESS: hand back the process so it can be waited
                // for. NO_UI: a failure to start comes back as an error code
                // for the caller's own dialog, not as a shell message box
                // (the consent prompt itself is not "UI" in this sense and
                // still shows). NOASYNC: the call is made from a worker
                // thread that has no message loop of its own.
                info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC |
                             SEE_MASK_FLAG_NO_UI;
                info.hwnd = GetActiveWindow();
                info.lpVerb = L"runas";
                info.lpFile = exe.c_str();
                info.lpParameters = parametersW.c_str();
                info.nShow = SW_HIDE;   // the helper opens no window anyway
                SetLastError(ERROR_SUCCESS);
                if (!ShellExecuteExW(&info)) {
                    const DWORD error = GetLastError();
                    if (error == ERROR_CANCELLED) {
                        out.outcome = ElevatedOutcome::Declined;
                    } else {
                        out.outcome = ElevatedOutcome::Failed;
                        out.error = Win32ErrorText(error);
                    }
                    break;
                }
                DWORD exitCode = static_cast<DWORD>(kHelperExitPartial);
                if (info.hProcess) {
                    WaitForSingleObject(info.hProcess, INFINITE);
                    if (!GetExitCodeProcess(info.hProcess, &exitCode))
                        exitCode = static_cast<DWORD>(kHelperExitPartial);
                    CloseHandle(info.hProcess);
                }

                std::vector<ElevatedFailure> failures =
                        ParseReport(ReadWholeFile(report));
                TruncateFile(report);   // the next run starts from a clean one
                if (failures.empty() && exitCode != static_cast<DWORD>(kHelperExitOk)) {
                    // The helper died before it could report (or refused
                    // its arguments): what is still there is what failed.
                    for (const std::string& path : run) {
                        std::error_code ec;
                        if (fs::exists(fs::path(ToWide(path)), ec))
                            failures.push_back({path,
                                    exitCode == static_cast<DWORD>(kHelperExitBadArguments)
                                            ? "The helper rejected its arguments"
                                            : "Still present after the attempt"});
                    }
                }
                out.failures.insert(out.failures.end(), failures.begin(), failures.end());
            }
            DeleteFileW(report.c_str());
            if (uninitCom) CoUninitialize();
            return true;
        }

    } // namespace ElevatedFileOperations
} // namespace UltraCanvas
