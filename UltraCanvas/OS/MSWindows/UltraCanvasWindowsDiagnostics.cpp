// OS/MSWindows/UltraCanvasWindowsDiagnostics.cpp
// Startup and crash diagnostics for Windows builds. See the header for why.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasWindowsDiagnostics.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Older MinGW-w64 winnt.h predates the ARM64 constant; the value is fixed.
#ifndef PROCESSOR_ARCHITECTURE_ARM64
#define PROCESSOR_ARCHITECTURE_ARM64 12
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <string>
#include <system_error>

#include "../../include/UltraCanvasDebug.h"

namespace UltraCanvas {

    namespace {

        // ===== CRASH-TIME STATE =====
        // Everything the unhandled-exception filter needs is captured here while
        // the process is still healthy. The filter itself must not allocate, take
        // a lock or touch a C++ stream: it runs after memory corruption, after a
        // stack overflow, and possibly while another thread holds the debug
        // sink's mutex. Fixed buffers and raw Win32 calls only.
        wchar_t gCrashLogPath[MAX_PATH * 2] = L"";
        char    gCrashAppName[128]          = "UltraCanvas";
        char    gCrashOsVersion[128]        = "";
        bool    gCrashDialogAllowed         = true;

        bool EnvFlagSet(const char* name) {
            const char* value = std::getenv(name);
            if (!value || !*value) return false;
            return !(value[0] == '0' && value[1] == '\0');
        }

        bool DialogsSuppressed() {
            return EnvFlagSet("ULTRACANVAS_NO_ERROR_DIALOG");
        }

        std::wstring Widen(const std::string& utf8) {
            if (utf8.empty()) return L"";
            const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
            if (size <= 0) return L"";
            std::wstring out(static_cast<size_t>(size - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), size);
            return out;
        }

        std::string Narrow(const std::wstring& utf16) {
            if (utf16.empty()) return "";
            const int size = WideCharToMultiByte(CP_UTF8, 0, utf16.c_str(), -1,
                                                 nullptr, 0, nullptr, nullptr);
            if (size <= 0) return "";
            std::string out(static_cast<size_t>(size - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, utf16.c_str(), -1, out.data(), size,
                                nullptr, nullptr);
            return out;
        }

        void ShowErrorDialog(const std::string& title, const std::string& text) {
            if (DialogsSuppressed()) return;
            MessageBoxW(nullptr, Widen(text).c_str(), Widen(title).c_str(),
                        MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
        }

        // Human-readable name for the common structured-exception codes. The
        // ones worth naming are those a user actually hits: a bad pointer, a
        // binary built for instructions this CPU does not have, a stack
        // overflow, a failed DLL initialiser.
        const char* ExceptionCodeName(DWORD code) {
            switch (code) {
                case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
                case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
                case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
                case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
                case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION "
                                                                "(binary uses CPU instructions this machine lacks)";
                case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
                case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
                case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
                case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
                case EXCEPTION_BREAKPOINT:               return "BREAKPOINT";
                case 0xC0000142:                         return "DLL_INIT_FAILED";
                case 0xC0000409:                         return "STACK_BUFFER_OVERRUN / __fastfail";
                case 0xE06D7363:                         return "unhandled C++ exception";
                default:                                 return "unknown exception code";
            }
        }

        // Appends one line to the crash log with no allocation and no locking.
        void CrashLogLine(const char* line) {
            if (!gCrashLogPath[0]) return;
            HANDLE file = CreateFileW(gCrashLogPath, FILE_APPEND_DATA,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return;
            SetFilePointer(file, 0, nullptr, FILE_END);
            DWORD written = 0;
            WriteFile(file, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
            WriteFile(file, "\r\n", 2, &written, nullptr);
            CloseHandle(file);
        }

        LONG WINAPI UnhandledExceptionReporter(EXCEPTION_POINTERS* info) {
            const DWORD code = info && info->ExceptionRecord
                                   ? info->ExceptionRecord->ExceptionCode
                                   : 0;
            void* address = info && info->ExceptionRecord
                                ? info->ExceptionRecord->ExceptionAddress
                                : nullptr;

            // Which module does the faulting address live in? For a packaged app
            // this is the single most useful fact in the report -- it separates
            // "our code" from "the GPU driver" from "a mismatched DLL".
            char moduleName[MAX_PATH] = "<unknown module>";
            HMODULE module = nullptr;
            if (address &&
                GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   static_cast<LPCSTR>(address), &module) &&
                module) {
                GetModuleFileNameA(module, moduleName, MAX_PATH);
            }

            // Deliberately small: a stack-overflow exception runs this filter
            // on the stack that just ran out.
            char message[512];
            std::snprintf(message, sizeof(message),
                          "%s crashed: exception 0x%08lX (%s) at 0x%016llX in %s. %s",
                          gCrashAppName, static_cast<unsigned long>(code),
                          ExceptionCodeName(code),
                          static_cast<unsigned long long>(
                              reinterpret_cast<std::uintptr_t>(address)),
                          moduleName, gCrashOsVersion);

            CrashLogLine(message);
            if (gCrashDialogAllowed) {
                MessageBoxA(nullptr, message, gCrashAppName,
                            MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
            }

            // Let the default handler run so a configured WER dump is still
            // produced; we only wanted the process to say something first.
            return EXCEPTION_CONTINUE_SEARCH;
        }

        void CopyToFixedBuffer(char* dest, size_t capacity, const std::string& source) {
            if (capacity == 0) return;
            const size_t count = source.size() < capacity - 1 ? source.size() : capacity - 1;
            std::memcpy(dest, source.data(), count);
            dest[count] = '\0';
        }

        std::string ProcessArchitecture() {
            SYSTEM_INFO info = {};
            GetNativeSystemInfo(&info);
            const char* machine = "unknown";
            switch (info.wProcessorArchitecture) {
                case PROCESSOR_ARCHITECTURE_AMD64: machine = "x64";   break;
                case PROCESSOR_ARCHITECTURE_ARM64: machine = "arm64"; break;
                case PROCESSOR_ARCHITECTURE_ARM:   machine = "arm";   break;
                case PROCESSOR_ARCHITECTURE_INTEL: machine = "x86";   break;
                default: break;
            }
            return std::string(sizeof(void*) == 8 ? "64-bit" : "32-bit") +
                   " process on " + machine;
        }

        bool ProcessIsElevated() {
            HANDLE token = nullptr;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
            TOKEN_ELEVATION elevation = {};
            DWORD size = sizeof(elevation);
            const bool ok = GetTokenInformation(token, TokenElevation, &elevation,
                                                sizeof(elevation), &size) != 0;
            CloseHandle(token);
            return ok && elevation.TokenIsElevated != 0;
        }

        std::string CurrentModulePath() {
            wchar_t path[MAX_PATH * 2] = L"";
            if (GetModuleFileNameW(nullptr, path, MAX_PATH * 2) == 0) return "<unknown>";
            return Narrow(path);
        }

        std::string CurrentDirectory() {
            wchar_t path[MAX_PATH * 2] = L"";
            if (GetCurrentDirectoryW(MAX_PATH * 2, path) == 0) return "<unknown>";
            return Narrow(path);
        }

    } // namespace

    std::string DescribeWin32Error(unsigned long error) {
        char* buffer = nullptr;
        const DWORD length = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, static_cast<DWORD>(error),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<char*>(&buffer), 0, nullptr);

        std::string text;
        if (length && buffer) {
            text.assign(buffer, length);
            while (!text.empty() && (text.back() == '\r' || text.back() == '\n' ||
                                     text.back() == ' ' || text.back() == '.')) {
                text.pop_back();
            }
        }
        if (buffer) LocalFree(buffer);

        std::string out = std::to_string(error);
        if (!text.empty()) out += " (" + text + ")";
        return out;
    }

    bool AttachParentConsole() {
        // Already connected -- a launcher redirecting stderr to a file, or a
        // console subsystem build. Leave the existing streams alone.
        if (GetStdHandle(STD_ERROR_HANDLE) != nullptr &&
            GetStdHandle(STD_ERROR_HANDLE) != INVALID_HANDLE_VALUE) {
            return true;
        }
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            return false;  // started from Explorer, or detached with `start`
        }

        (void)std::freopen("CONOUT$", "w", stdout);
        (void)std::freopen("CONOUT$", "w", stderr);
        (void)std::freopen("CONIN$",  "r", stdin);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        return true;
    }

    std::string GetWindowsVersionString() {
        // RtlGetVersion is the only API that reports the true build number to a
        // process without a compatibility manifest; GetVersionEx would answer
        // "6.2" on both Windows 10 and 11, which is worse than useless in a
        // report that exists to tell the two apart.
        using RtlGetVersionFunc = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        RTL_OSVERSIONINFOW version = {};
        version.dwOSVersionInfoSize = sizeof(version);

        if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
            // Via a generic function pointer: casting FARPROC straight to the
            // real signature is what -Wcast-function-type warns about.
            auto rtlGetVersion = reinterpret_cast<RtlGetVersionFunc>(
                reinterpret_cast<void (*)()>(GetProcAddress(ntdll, "RtlGetVersion")));
            if (rtlGetVersion && rtlGetVersion(&version) == 0) {
                // Windows 11 kept the major/minor of Windows 10; build 22000 is
                // the documented boundary between them.
                const char* name = "Windows";
                if (version.dwMajorVersion == 10) {
                    name = version.dwBuildNumber >= 22000 ? "Windows 11" : "Windows 10";
                }
                return std::string(name) + " (" +
                       std::to_string(version.dwMajorVersion) + "." +
                       std::to_string(version.dwMinorVersion) + " build " +
                       std::to_string(version.dwBuildNumber) + ")";
            }
        }
        return "Windows (version unavailable)";
    }

    void LogWindowsStartupBanner(const std::string& appName) {
        if (!IsDebugOutputEnabled()) return;

        debugOutput << "UltraCanvas: ===== startup =====" << std::endl;
        debugOutput << "UltraCanvas: app          = " << appName << std::endl;
        debugOutput << "UltraCanvas: os           = " << GetWindowsVersionString() << std::endl;
        debugOutput << "UltraCanvas: architecture = " << ProcessArchitecture() << std::endl;
        debugOutput << "UltraCanvas: executable   = " << CurrentModulePath() << std::endl;
        debugOutput << "UltraCanvas: working dir  = " << CurrentDirectory() << std::endl;
        debugOutput << "UltraCanvas: elevated     = " << (ProcessIsElevated() ? "yes" : "no") << std::endl;
        debugOutput << "UltraCanvas: ansi codepage= " << GetACP() << std::endl;
        // The effective value, not the launcher's: SetupBundledFontconfig() has
        // already run by this point and replaces a FONTCONFIG_FILE that names a
        // file which is not there. Logging whether the file exists is what makes
        // the line worth having -- "missing" here would mean that repair did not
        // happen, and text rendering is about to go wrong.
        if (const char* fontconfig = std::getenv("FONTCONFIG_FILE")) {
            std::error_code ec;
            const bool present = std::filesystem::exists(fontconfig, ec);
            debugOutput << "UltraCanvas: FONTCONFIG_FILE = " << fontconfig
                        << (present ? " (present)" : " (MISSING)") << std::endl;
        }
    }

    void InstallWindowsCrashReporter(const std::string& appName) {
        CopyToFixedBuffer(gCrashAppName, sizeof(gCrashAppName), appName);
        CopyToFixedBuffer(gCrashOsVersion, sizeof(gCrashOsVersion), GetWindowsVersionString());
        gCrashDialogAllowed = !DialogsSuppressed();

        // Resolve the log path here rather than asking the debug sink for it:
        // the crash path must not touch the sink's stream or its mutex. Only a
        // ULTRACANVAS_DEBUG_LOG that names a file gives the handler somewhere to
        // write; the keyword values ("1", "stderr", ...) mean a console sink,
        // which a crashing GUI process cannot use.
        gCrashLogPath[0] = L'\0';
        if (const char* setting = std::getenv("ULTRACANVAS_DEBUG_LOG")) {
            const std::string value(setting);
            const bool isKeyword = value == "0" || value == "1" || value == "-" ||
                                   value == "on" || value == "off" || value == "no" ||
                                   value == "yes" || value == "none" || value == "true" ||
                                   value == "false" || value == "stderr";
            if (!value.empty() && !isKeyword) {
                const std::wstring wide = Widen(value);
                if (wide.size() < MAX_PATH * 2) {
                    std::wmemcpy(gCrashLogPath, wide.c_str(), wide.size() + 1);
                }
            }
        }

        SetUnhandledExceptionFilter(UnhandledExceptionReporter);
    }

    void ReportWindowsStartupFailure(const std::string& stage, const std::string& detail) {
        const std::string message = stage + (detail.empty() ? "" : ": " + detail);
        debugOutput << "UltraCanvas: FATAL " << message << std::endl;
        ShowErrorDialog("UltraCanvas: startup failed",
                        message +
                            "\n\n" + GetWindowsVersionString() +
                            "\n" + CurrentModulePath() +
                            "\n\nSet ULTRACANVAS_DEBUG_LOG to a file path and start the "
                            "application again for a full log.");
    }

} // namespace UltraCanvas
