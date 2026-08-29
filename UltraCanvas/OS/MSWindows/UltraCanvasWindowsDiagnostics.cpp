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
        char    gCrashCpuSummary[256]       = "";
        char    gCrashMarchAdvice[64]       = "-march=x86-64-v2";
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

        // ===== CPU / EMULATION =====
        // An ILLEGAL_INSTRUCTION fault says the binary used an instruction this
        // machine will not execute. Naming the exception is only half an answer;
        // the other half is what the machine actually offers, which is what the
        // helpers below capture. Note the deliberate use of
        // IsProcessorFeaturePresent rather than raw CPUID feature bits: it
        // reports what the OS *permits*, so an x64 process running under
        // emulation on an ARM64 machine is described by what the emulator
        // supports, not by the silicon underneath.

// Older MinGW-w64 winnt.h predates these; the values are fixed by the ABI.
#ifndef PF_SSE4_2_INSTRUCTIONS_AVAILABLE
#define PF_SSE4_2_INSTRUCTIONS_AVAILABLE 38
#endif
#ifndef PF_AVX_INSTRUCTIONS_AVAILABLE
#define PF_AVX_INSTRUCTIONS_AVAILABLE 39
#endif
#ifndef PF_AVX2_INSTRUCTIONS_AVAILABLE
#define PF_AVX2_INSTRUCTIONS_AVAILABLE 40
#endif
#ifndef PF_AVX512F_INSTRUCTIONS_AVAILABLE
#define PF_AVX512F_INSTRUCTIONS_AVAILABLE 41
#endif

        // The brand string comes from CPUID, which exists only on x86 - and the
        // guard has to be on the *architecture*, not on the compiler. Guarding on
        // the compiler is what broke the ARM64 build: MSYS2's CLANGARM64 toolchain
        // defines __clang__, so a "GCC or MSVC" split sent an ARM64 target down
        // the MSVC-intrinsic path and asked for __cpuidex on a CPU that has no
        // CPUID at all.
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    #define ULTRACANVAS_HAS_CPUID 1
#endif

#if defined(ULTRACANVAS_HAS_CPUID)
    #if defined(__GNUC__) || defined(__clang__)
        // GCC's and clang's own header. Preferred over hand-written asm because
        // it gets the 32-bit-PIC EBX save/restore right, which the naive "=b"
        // output constraint does not.
        #include <cpuid.h>
    #elif defined(_MSC_VER)
        #include <intrin.h>
    #endif

        bool CpuIdRaw(unsigned int leaf, unsigned int subleaf, unsigned int out[4]) {
    #if defined(__GNUC__) || defined(__clang__)
            return __get_cpuid_count(leaf, subleaf, &out[0], &out[1], &out[2], &out[3]) != 0;
    #elif defined(_MSC_VER)
            __cpuidex(reinterpret_cast<int*>(out), static_cast<int>(leaf),
                      static_cast<int>(subleaf));
            return true;
    #else
            (void)leaf; (void)subleaf; (void)out;
            return false;
    #endif
        }
#endif // ULTRACANVAS_HAS_CPUID

        // The 48-byte brand string from CPUID leaves 0x80000002..4, if present.
        std::string CpuBrand() {
#if !defined(ULTRACANVAS_HAS_CPUID)
    #if defined(_M_ARM64) || defined(__aarch64__)
            return "ARM64";
    #elif defined(_M_ARM) || defined(__arm__)
            return "ARM";
    #else
            return "unknown (no CPUID on this architecture)";
    #endif
#else
            unsigned int regs[4] = {};
            if (!CpuIdRaw(0x80000000u, 0, regs) || regs[0] < 0x80000004u) return "unknown";

            char brand[49] = {};
            for (unsigned int i = 0; i < 3; ++i) {
                if (!CpuIdRaw(0x80000002u + i, 0, regs)) return "unknown";
                std::memcpy(brand + i * 16, regs, 16);
            }
            brand[48] = '\0';
            std::string out(brand);
            // The brand string is space-padded at both ends.
            while (!out.empty() && out.front() == ' ') out.erase(out.begin());
            while (!out.empty() && out.back()  == ' ') out.pop_back();
            return out.empty() ? "unknown" : out;
#endif
        }

        // "x64 process emulated on ARM64" and friends. An x64 build running on an
        // ARM64 machine is the single most likely reason for an unsupported
        // instruction: the emulator implements a subset, so a binary tuned for
        // the build machine's CPU faults even though both are "x64".
        std::string EmulationDescription() {
            using IsWow64Process2Func = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
            HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
            if (!kernel32) return "";
            auto isWow64Process2 = reinterpret_cast<IsWow64Process2Func>(
                reinterpret_cast<void (*)()>(GetProcAddress(kernel32, "IsWow64Process2")));
            if (!isWow64Process2) return "";  // pre-1511; no emulation to report

            USHORT processMachine = 0, nativeMachine = 0;
            if (!isWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine)) return "";

            const char* native = nullptr;
            switch (nativeMachine) {
                case IMAGE_FILE_MACHINE_ARM64: native = "ARM64"; break;
                case IMAGE_FILE_MACHINE_AMD64: native = "x64";   break;
                case IMAGE_FILE_MACHINE_I386:  native = "x86";   break;
                default: return "";
            }
            // IMAGE_FILE_MACHINE_UNKNOWN for processMachine means "not running
            // under WOW64", i.e. the image matches the native machine.
            if (processMachine == IMAGE_FILE_MACHINE_UNKNOWN) return "";

            const char* image = (processMachine == IMAGE_FILE_MACHINE_AMD64) ? "x64"
                              : (processMachine == IMAGE_FILE_MACHINE_I386)  ? "x86"
                                                                             : "?";
            return std::string(", EMULATED: ") + image + " image on a " + native + " machine";
        }

        // Feature set read from CPUID rather than IsProcessorFeaturePresent.
        // The Win32 call knows only a handful of PF_* constants, and the
        // extensions that actually break a -march=native build are mostly not
        // among them: a field report had an AVX2-capable Ryzen 5 5500U fault on
        // VGF2P8AFFINEQB, a GFNI instruction, while the old summary read
        // "[SSE4.2 AVX AVX2]" and could not name the one thing that was missing.
        // GFNI, VAES and VPCLMULQDQ are VEX-encoded and need no AVX-512, so a
        // CPU can have every feature this used to print and still refuse the
        // binary.
        struct CpuFeatures {
            bool sse3 = false, ssse3 = false, sse41 = false, sse42 = false;
            bool popcnt = false, cx16 = false, movbe = false, lahf = false, lzcnt = false;
            bool osxsave = false, avx = false, avx2 = false, fma = false, f16c = false;
            bool bmi1 = false, bmi2 = false;
            bool aes = false, pclmul = false, sha = false;
            bool gfni = false, vaes = false, vpclmulqdq = false;
            bool avx512f = false, avx512bw = false, avx512cd = false;
            bool avx512dq = false, avx512vl = false, avx512vnni = false;
        };

#if defined(ULTRACANVAS_HAS_CPUID)
        // Inside the guard: off x86 the whole body of DetectCpuFeatures() below
        // is compiled out, which would leave this defined and unused.
        bool Bit(unsigned int value, int index) {
            return (value & (1u << index)) != 0;
        }
#endif

        CpuFeatures DetectCpuFeatures() {
            CpuFeatures f;
#if defined(ULTRACANVAS_HAS_CPUID)
            unsigned int r[4] = {};
            if (!CpuIdRaw(0, 0, r)) return f;
            const unsigned int maxLeaf = r[0];

            if (maxLeaf >= 1 && CpuIdRaw(1, 0, r)) {
                const unsigned int ecx = r[2];
                f.sse3   = Bit(ecx, 0);   f.pclmul  = Bit(ecx, 1);
                f.ssse3  = Bit(ecx, 9);   f.fma     = Bit(ecx, 12);
                f.cx16   = Bit(ecx, 13);  f.sse41   = Bit(ecx, 19);
                f.sse42  = Bit(ecx, 20);  f.movbe   = Bit(ecx, 22);
                f.popcnt = Bit(ecx, 23);  f.aes     = Bit(ecx, 25);
                f.osxsave= Bit(ecx, 27);  f.avx     = Bit(ecx, 28);
                f.f16c   = Bit(ecx, 29);
            }
            if (maxLeaf >= 7 && CpuIdRaw(7, 0, r)) {
                const unsigned int ebx = r[1];
                const unsigned int ecx = r[2];
                f.bmi1     = Bit(ebx, 3);   f.avx2       = Bit(ebx, 5);
                f.bmi2     = Bit(ebx, 8);   f.avx512f    = Bit(ebx, 16);
                f.avx512dq = Bit(ebx, 17);  f.avx512cd   = Bit(ebx, 28);
                f.sha      = Bit(ebx, 29);  f.avx512bw   = Bit(ebx, 30);
                f.avx512vl = Bit(ebx, 31);
                f.gfni     = Bit(ecx, 8);   f.vaes       = Bit(ecx, 9);
                f.vpclmulqdq = Bit(ecx, 10); f.avx512vnni = Bit(ecx, 11);
            }
            if (CpuIdRaw(0x80000000u, 0, r) && r[0] >= 0x80000001u &&
                CpuIdRaw(0x80000001u, 0, r)) {
                f.lahf  = Bit(r[2], 0);
                f.lzcnt = Bit(r[2], 5);
            }
#endif
            return f;
        }

        // Highest psABI microarchitecture level the CPU satisfies. This is the
        // actionable number: it is exactly what -march=x86-64-v<N> means, so a
        // build that targets it is guaranteed to run here. Note GFNI, VAES,
        // VPCLMULQDQ and SHA belong to *no* level - they are opt-in features
        // that -march=native picks up from the build machine and no
        // -march=x86-64-vN will ever emit.
        int X86_64Level(const CpuFeatures& f) {
            const bool v2 = f.cx16 && f.lahf && f.popcnt && f.sse3 && f.ssse3 &&
                            f.sse41 && f.sse42;
            const bool v3 = v2 && f.avx && f.avx2 && f.bmi1 && f.bmi2 && f.f16c &&
                            f.fma && f.lzcnt && f.movbe && f.osxsave;
            const bool v4 = v3 && f.avx512f && f.avx512bw && f.avx512cd &&
                            f.avx512dq && f.avx512vl;
            if (v4) return 4;
            if (v3) return 3;
            if (v2) return 2;
            return 1;
        }

        std::string CpuSummary() {
            const CpuFeatures f = DetectCpuFeatures();

            std::string out = CpuBrand();
#if defined(ULTRACANVAS_HAS_CPUID)
            out += " (x86-64-v" + std::to_string(X86_64Level(f)) + ")";
#endif
            out += " [";
            struct Named { bool present; const char* name; };
            const Named named[] = {
                { f.sse42,      "SSE4.2"     },
                { f.avx,        "AVX"        },
                { f.avx2,       "AVX2"       },
                { f.fma,        "FMA"        },
                { f.bmi2,       "BMI2"       },
                { f.aes,        "AES"        },
                { f.sha,        "SHA"        },
                // The ones that bite. Absent here and present on the build
                // machine is the whole bug.
                { f.gfni,       "GFNI"       },
                { f.vaes,       "VAES"       },
                { f.vpclmulqdq, "VPCLMULQDQ" },
                { f.avx512f,    "AVX512F"    },
                { f.avx512bw,   "AVX512BW"   },
                { f.avx512vl,   "AVX512VL"   },
                { f.avx512vnni, "AVX512VNNI" },
            };
            bool first = true;
            for (const Named& entry : named) {
                if (!entry.present) continue;
                if (!first) out += " ";
                out += entry.name;
                first = false;
            }
            if (first) out += "baseline";
            out += "]";
            out += EmulationDescription();
            return out;
        }

        // Hex dump of the bytes the CPU refused, so the instruction can be
        // identified. VirtualQuery first: the address faulted on decode, so it is
        // normally readable, but a crash handler must not take that on trust.
        void FormatBytesAt(const void* address, char* out, size_t capacity) {
            if (capacity) out[0] = '\0';
            if (!address || capacity < 8) return;

            MEMORY_BASIC_INFORMATION info = {};
            if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info)) return;
            if (info.State != MEM_COMMIT) return;
            const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                   PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                   PAGE_EXECUTE_WRITECOPY;
            if (!(info.Protect & readable)) return;
            if (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return;

            // Stay inside the queried region so a 16-byte read cannot run off it.
            const auto  base      = static_cast<const unsigned char*>(address);
            const auto  regionEnd = static_cast<const unsigned char*>(info.BaseAddress) +
                                    info.RegionSize;
            const size_t available = static_cast<size_t>(regionEnd - base);
            const size_t count     = available < 16 ? available : 16;

            size_t used = 0;
            for (size_t i = 0; i < count && used + 4 < capacity; ++i) {
                const int written = std::snprintf(out + used, capacity - used, "%02X ", base[i]);
                if (written <= 0) break;
                used += static_cast<size_t>(written);
            }
            if (used && used <= capacity) out[used - 1] = '\0';
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

            // An instruction fault is the one case where naming the exception is
            // not enough to act on, so it gets the bytes the CPU refused and what
            // this machine actually supports. Every other case — a stack overflow
            // above all, which runs this filter on the stack that just ran out —
            // keeps to the small buffer and the short message.
            char detail[384] = "";
            if (code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_PRIV_INSTRUCTION) {
                char bytes[64] = "";
                FormatBytesAt(address, bytes, sizeof(bytes));
                std::snprintf(detail, sizeof(detail),
                              "\nThis CPU: %s\nBytes at the fault: %s\nThe binary was built for a "
                              "CPU this one is not. Rebuild it with %s instead of -march=native "
                              "(or /arch:AVX*). Note the feature list above is what this machine "
                              "has: an instruction can be missing from it even when AVX2 is "
                              "present -- GFNI, VAES and VPCLMULQDQ belong to no -march level and "
                              "are picked up only from the build machine.",
                              gCrashCpuSummary, bytes[0] ? bytes : "<unreadable>",
                              gCrashMarchAdvice);
            }

            char message[512];
            std::snprintf(message, sizeof(message),
                          "%s crashed: exception 0x%08lX (%s) at 0x%016llX in %s. %s",
                          gCrashAppName, static_cast<unsigned long>(code),
                          ExceptionCodeName(code),
                          static_cast<unsigned long long>(
                              reinterpret_cast<std::uintptr_t>(address)),
                          moduleName, gCrashOsVersion);

            if (detail[0]) {
                CrashLogLine(message);
                CrashLogLine(detail);
                if (gCrashDialogAllowed) {
                    char full[1024];
                    std::snprintf(full, sizeof(full), "%s%s", message, detail);
                    MessageBoxA(nullptr, full, gCrashAppName,
                                MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
                }
                return EXCEPTION_CONTINUE_SEARCH;
            }

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
        debugOutput << "UltraCanvas: cpu          = " << CpuSummary() << std::endl;
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
        // Captured now, while the process is healthy: CPUID and the feature
        // queries are not things to be doing inside the filter.
        CopyToFixedBuffer(gCrashCpuSummary, sizeof(gCrashCpuSummary), CpuSummary());
        // Name the level this machine actually satisfies, so the advice is a flag
        // the builder can paste rather than a generic "lower the baseline".
        CopyToFixedBuffer(gCrashMarchAdvice, sizeof(gCrashMarchAdvice),
                          "-march=x86-64-v" +
                              std::to_string(X86_64Level(DetectCpuFeatures())));
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
