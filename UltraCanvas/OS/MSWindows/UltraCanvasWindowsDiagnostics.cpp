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
#include <dbghelp.h>   // the minidump types only; the function is resolved at run time

// Older MinGW-w64 winnt.h predates the ARM64 constant; the value is fixed.
#ifndef PROCESSOR_ARCHITECTURE_ARM64
#define PROCESSOR_ARCHITECTURE_ARM64 12
#endif

#include <atomic>
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

        // ===== THE CRASH DUMP =====
        // A minidump beside the message: the stack of every thread, the
        // module list and the memory the stacks point at, which is what a
        // debugger needs to say where the crash was and how it got there.
        // The exception code and the faulting module in the message box
        // name the symptom; the dump is the evidence. dbghelp's writer is
        // resolved while the process is healthy (LoadLibrary inside the
        // filter would take the loader lock, which the crashing thread may
        // hold), and the folder is created then too; only the file name,
        // which carries the time, is made in the filter - with wsprintf, no
        // heap. The path is kept in a fixed buffer like everything else here.
        using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                                  PMINIDUMP_EXCEPTION_INFORMATION,
                                                  PMINIDUMP_USER_STREAM_INFORMATION,
                                                  PMINIDUMP_CALLBACK_INFORMATION);
        MiniDumpWriteDumpFn gMiniDumpWriteDump = nullptr;
        wchar_t gCrashDumpDir[MAX_PATH * 2]   = L"";   // empty = no dump
        wchar_t gCrashDumpPath[MAX_PATH * 2]  = L"";   // the file, once written
        wchar_t gCrashDumpBaseName[64]        = L"UltraCanvas";

        // What goes into the dump. Not a full dump: that is the whole
        // address space, hundreds of megabytes nobody can attach to a bug
        // report. These keep it in the low tens of megabytes while giving a
        // debugger every stack, every module, the memory each stack refers
        // to and the modules' data segments (the globals).
        constexpr MINIDUMP_TYPE kCrashDumpType = static_cast<MINIDUMP_TYPE>(
                MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory |
                MiniDumpWithDataSegs | MiniDumpWithHandleData |
                MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);

        // Narrow on purpose: the value is an ASCII 0/1 switch, never a path
        // or a name, and this is read on the way to a crash report, where
        // nothing should allocate.
        bool EnvFlagSet(const char* name) {
            const char* value = std::getenv(name);   // path-string-ok: ASCII 0/1 flag, no allocation on the crash path
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
                case 0xE06D7363:                         return "unhandled C++ exception (MSVC)";
                // libgcc raises C++ exceptions through SEH under this code
                // ('GCC ' in ASCII); it reaches the filter only when nothing
                // on the throwing thread could catch it.
                case 0x20474343:                         return "unhandled C++ exception (GCC)";
                // Raised by ntdll while unwinding: a frame it cannot walk,
                // typically the kernel-callback boundary under a window
                // procedure, or a corrupted stack.
                case 0xC00000FF:                         return "BAD_FUNCTION_TABLE (unwind failed)";
                case 0xC0000028:                         return "BAD_STACK (unwind failed)";
                default:                                 return "unknown exception code";
            }
        }

        // The three codes an escaped C++ exception shows up as. The faulting
        // address of any of them is inside ntdll / KERNELBASE, never at the
        // throw, so the report has to say where to look instead.
        bool IsEscapedCppException(DWORD code) {
            return code == 0x20474343 || code == 0xE06D7363 || code == 0xC00000FF;
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

        // Writes the minidump for `info` into gCrashDumpDir and leaves its
        // path in gCrashDumpPath. False (and an empty path) when there is
        // no writer, no folder, or the write failed: the report then says
        // so instead of naming a file that is not there.
        bool WriteCrashDump(EXCEPTION_POINTERS* info) {
            gCrashDumpPath[0] = L'\0';
            if (!gMiniDumpWriteDump || !gCrashDumpDir[0]) return false;

            SYSTEMTIME now = {};
            GetLocalTime(&now);
            // wsprintfW: a kernel-side formatter that allocates nothing.
            const int n = wsprintfW(gCrashDumpPath, L"%s\\%s-%04u%02u%02u-%02u%02u%02u-%lu.dmp",
                                    gCrashDumpDir, gCrashDumpBaseName,
                                    static_cast<unsigned>(now.wYear),
                                    static_cast<unsigned>(now.wMonth),
                                    static_cast<unsigned>(now.wDay),
                                    static_cast<unsigned>(now.wHour),
                                    static_cast<unsigned>(now.wMinute),
                                    static_cast<unsigned>(now.wSecond),
                                    static_cast<unsigned long>(GetCurrentProcessId()));
            if (n <= 0) { gCrashDumpPath[0] = L'\0'; return false; }

            HANDLE file = CreateFileW(gCrashDumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) { gCrashDumpPath[0] = L'\0'; return false; }

            MINIDUMP_EXCEPTION_INFORMATION exception = {};
            exception.ThreadId = GetCurrentThreadId();
            exception.ExceptionPointers = info;
            exception.ClientPointers = FALSE;
            const BOOL ok = gMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                                               kCrashDumpType, info ? &exception : nullptr,
                                               nullptr, nullptr);
            CloseHandle(file);
            if (!ok) {
                DeleteFileW(gCrashDumpPath);   // a partial dump misleads
                gCrashDumpPath[0] = L'\0';
                return false;
            }
            return true;
        }

        // The dump folder: ULTRACANVAS_CRASH_DUMP_DIR when set, else
        // %LOCALAPPDATA%\UltraCanvas\CrashDumps - the per-user application
        // data folder, writable without rights and never inside the
        // installation (which may be read-only, or a download about to be
        // deleted). Created now, with its parent; an empty result means no
        // dump will be written and the report says where to look instead.
        void PrepareCrashDumpDir() {
            gCrashDumpDir[0] = L'\0';
            std::wstring dir;
            if (const char* custom = std::getenv("ULTRACANVAS_CRASH_DUMP_DIR")) {
                if (*custom) dir = Widen(custom);
            }
            if (dir.empty()) {
                wchar_t local[MAX_PATH * 2] = L"";
                const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH * 2);
                if (n == 0 || n >= MAX_PATH * 2) return;
                dir = std::wstring(local) + L"\\UltraCanvas";
                CreateDirectoryW(dir.c_str(), nullptr);
                dir += L"\\CrashDumps";
            }
            CreateDirectoryW(dir.c_str(), nullptr);
            const DWORD attrs = GetFileAttributesW(dir.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return;
            if (dir.size() + 1 >= MAX_PATH * 2) return;
            std::wmemcpy(gCrashDumpDir, dir.c_str(), dir.size() + 1);
        }

        // ===== NAMING THE CODE =====
        // "at 0x00007FFB1212C86D in libUltraCanvas.dll" cannot be traced to a
        // function: the DLL loads at a different address every start (ASLR), so
        // the number means nothing without the dump and the matching build in a
        // debugger. The report therefore names, for the fault and for every
        // caller on the stack, the module, the offset inside it - the same on
        // every start - and the function.
        //
        // The function comes from the module's export table. The framework DLL
        // is linked with WINDOWS_EXPORT_ALL_SYMBOLS, so every function with
        // external linkage is in it: the nearest export at or below an address
        // is the function the address is in - or, for code with internal
        // linkage (a lambda, a helper in an anonymous namespace), the exported
        // function compiled just before it, which is still the right file. No
        // dbghelp, no symbol file and no heap: only the module's own headers,
        // which stay mapped while the module is loaded.

        // Appends at most `length` characters of `text`; `out` stays terminated.
        size_t AppendText(char* out, size_t capacity, size_t used, const char* text, size_t length) {
            while (length-- && *text && used + 1 < capacity) out[used++] = *text++;
            out[used] = '\0';
            return used;
        }

        // "_ZN11UltraCanvas20UltraCanvasTextInput5PasteEv" ->
        // "UltraCanvas::UltraCanvasTextInput::Paste". The name only: the
        // parameters are dropped, and where the mangling goes beyond nested
        // names (template arguments, operators) the name ends in "...". Anything
        // that is not an Itanium name (a C function, an MSVC "?" name) is kept
        // as it is.
        void DemangledName(const char* mangled, char* out, size_t capacity) {
            if (!capacity) return;
            out[0] = '\0';
            const char* p = mangled;
            bool nested = false;
            if (std::strncmp(p, "_ZN", 3) == 0) {
                p += 3;
                nested = true;
                while (*p == 'r' || *p == 'V' || *p == 'K' || *p == 'R' || *p == 'O') ++p;
            } else if (std::strncmp(p, "_Z", 2) == 0 && p[2] >= '1' && p[2] <= '9') {
                p += 2;
            } else {
                AppendText(out, capacity, 0, mangled, std::strlen(mangled));
                return;
            }
            size_t used = 0;
            const char* last = nullptr;      // the enclosing class, for "Class::Class"
            size_t lastLength = 0;
            while (*p) {
                if (*p >= '0' && *p <= '9') {
                    size_t length = 0;
                    while (*p >= '0' && *p <= '9') length = length * 10 + static_cast<size_t>(*p++ - '0');
                    if (length == 0 || std::strlen(p) < length) break;
                    if (used) used = AppendText(out, capacity, used, "::", 2);
                    used = AppendText(out, capacity, used, p, length);
                    last = p;
                    lastLength = length;
                    p += length;
                    if (!nested) return;
                } else if (nested && *p == 'S' && p[1] == 't') {
                    used = AppendText(out, capacity, used, "std", 3);
                    p += 2;
                } else if (nested && last && (*p == 'C' || *p == 'D') && p[1] >= '0' && p[1] <= '5') {
                    used = AppendText(out, capacity, used, *p == 'D' ? "::~" : "::", *p == 'D' ? 3 : 2);
                    used = AppendText(out, capacity, used, last, lastLength);
                    p += 2;
                } else if (nested && *p == 'E') {
                    return;
                } else {
                    break;
                }
            }
            if (!used) {
                AppendText(out, capacity, 0, mangled, std::strlen(mangled));
                return;
            }
            AppendText(out, capacity, used, "...", 3);
        }

        // The exported name nearest at or below `rva` inside the code section
        // that holds `rva`, and that export's own RVA. Null when the module has
        // no export table or `rva` is not code.
        const char* NearestExport(const BYTE* base, DWORD rva, DWORD& exportRva) {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

            const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
            const IMAGE_SECTION_HEADER* code = nullptr;
            for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
                const DWORD size = section->Misc.VirtualSize ? section->Misc.VirtualSize
                                                             : section->SizeOfRawData;
                if (rva >= section->VirtualAddress && rva - section->VirtualAddress < size) {
                    if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) code = section;
                    break;
                }
            }
            if (!code) return nullptr;

            const IMAGE_DATA_DIRECTORY& directory =
                    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
            if (!directory.VirtualAddress || !directory.Size) return nullptr;
            const auto* exports =
                    reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + directory.VirtualAddress);
            const auto* functions = reinterpret_cast<const DWORD*>(base + exports->AddressOfFunctions);
            const auto* names     = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
            const auto* ordinals  = reinterpret_cast<const WORD*>(base + exports->AddressOfNameOrdinals);

            // Data exports (vtables, type info) and forwarders lie outside the
            // code section, so staying inside it keeps only functions.
            const char* best = nullptr;
            exportRva = 0;
            for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
                if (ordinals[i] >= exports->NumberOfFunctions) continue;
                const DWORD function = functions[ordinals[i]];
                if (function > rva || function < code->VirtualAddress) continue;
                if (!best || function > exportRva) {
                    best = reinterpret_cast<const char*>(base + names[i]);
                    exportRva = function;
                }
            }
            return best;
        }

        // "libUltraCanvas.dll+0x12C86D UltraCanvas::UltraCanvasTextInput::Paste+0x3D",
        // or the bare address when no module holds it. A return address is
        // looked up one byte back, inside the call it returns from: a call can
        // be the last instruction of its function, and the return address then
        // already belongs to the next one.
        void DescribeCodeAddress(DWORD64 address, bool isReturnAddress, char* out, size_t capacity) {
            const DWORD64 lookup = isReturnAddress && address ? address - 1 : address;
            HMODULE module = nullptr;
            if (!lookup ||
                !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    reinterpret_cast<LPCSTR>(lookup), &module) ||
                !module) {
                std::snprintf(out, capacity, "0x%016llX", static_cast<unsigned long long>(address));
                return;
            }
            char path[MAX_PATH] = "";
            GetModuleFileNameA(module, path, MAX_PATH);
            const char* file = path;
            for (const char* c = path; *c; ++c) {
                if (*c == '\\' || *c == '/') file = c + 1;
            }

            const auto* base = reinterpret_cast<const BYTE*>(module);
            const auto offset = static_cast<unsigned long>(address - reinterpret_cast<DWORD64>(base));
            DWORD exportRva = 0;
            const char* symbol =
                    NearestExport(base, static_cast<DWORD>(lookup - reinterpret_cast<DWORD64>(base)), exportRva);
            if (!symbol) {
                std::snprintf(out, capacity, "%s+0x%lX", file, offset);
                return;
            }
            char name[160];
            DemangledName(symbol, name, sizeof(name));
            std::snprintf(out, capacity, "%s+0x%lX %s+0x%lX", file, offset, name,
                          offset - static_cast<unsigned long>(exportRva));
        }

        // Room for the call stack's text: sixteen lines of module, offset and
        // function name.
        constexpr size_t kCrashCallStackText = 4096;

        // The faulting thread's return addresses, walked from the fault with the
        // unwind tables Windows' own exception dispatch uses. x64 only: that is
        // where the unwinder's interface is the same on every toolchain; an
        // ARM64 report names the faulting function and stops there.
#if defined(__x86_64__) || defined(_M_X64)
    #define ULTRACANVAS_CRASH_CALL_STACK 1
#endif

#if defined(ULTRACANVAS_CRASH_CALL_STACK)
        constexpr int kCrashStackFrames = 16;

        // Stops after `capacity` frames, at a frame with no unwind data past the
        // first, when the stack pointer stops moving up, or when it leaves this
        // thread's stack: a smashed stack ends the list instead of being read.
        int CaptureCallStack(const CONTEXT& fault, DWORD64* frames, int capacity) {
            static CONTEXT context;   // static: 1.2 KB of stack the filter may not have
            context = fault;
            const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
            const auto stackLow  = reinterpret_cast<DWORD64>(tib->StackLimit);
            const auto stackHigh = reinterpret_cast<DWORD64>(tib->StackBase);

            int count = 0;
            while (count < capacity && context.Rip) {
                frames[count++] = context.Rip;
                const DWORD64 sp = context.Rsp;
                if (sp < stackLow || sp + sizeof(DWORD64) > stackHigh) break;
                DWORD64 imageBase = 0;
                PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
                if (function) {
                    void* handlerData = nullptr;
                    DWORD64 establisherFrame = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context,
                                     &handlerData, &establisherFrame, nullptr);
                } else if (count == 1) {
                    // The faulting frame may be a leaf function, or a call that
                    // landed where no module is - through a freed object's
                    // vtable, say. Either way the return address is on top of
                    // the stack, and the caller it names is the one that matters.
                    context.Rip = *reinterpret_cast<const DWORD64*>(sp);
                    context.Rsp = sp + sizeof(DWORD64);
                } else {
                    break;
                }
                if (context.Rsp <= sp || context.Rsp > stackHigh) break;
            }
            return count;
        }

        // Set while the stack is walked. A walk over a stack corrupt enough to
        // make the unwinder fault raises a second exception, which brings the
        // process back into this filter; finding the flag set, it reports
        // without walking again instead of recursing.
        std::atomic<bool> gWalkingCallStack{false};
#endif

        LONG WINAPI UnhandledExceptionReporter(EXCEPTION_POINTERS* info) {
            const DWORD code = info && info->ExceptionRecord
                                   ? info->ExceptionRecord->ExceptionCode
                                   : 0;
            void* address = info && info->ExceptionRecord
                                ? info->ExceptionRecord->ExceptionAddress
                                : nullptr;

            // The dump first: it is the one thing that cannot be redone, and
            // the message box below blocks until somebody clicks it.
            const bool dumped = WriteCrashDump(info);
            // One line for the log, the same plus an instruction for the box.
            char dumpLine[MAX_PATH * 2 + 64] = "";
            if (dumped) {
                char dumpPath[MAX_PATH * 2] = "";
                WideCharToMultiByte(CP_ACP, 0, gCrashDumpPath, -1, dumpPath,
                                    sizeof(dumpPath), nullptr, nullptr);
                std::snprintf(dumpLine, sizeof(dumpLine), "Crash dump: %s", dumpPath);
            } else {
                std::snprintf(dumpLine, sizeof(dumpLine), "No crash dump could be written%s.",
                              gCrashDumpDir[0] ? "" : " (no dump folder)");
            }
            char dumpNote[sizeof(dumpLine) + 48] = "";
            std::snprintf(dumpNote, sizeof(dumpNote), "\n%s%s", dumpLine,
                          dumped ? "\nAttach it to the bug report." : "");

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
            if (IsEscapedCppException(code)) {
                std::snprintf(detail, sizeof(detail),
                              "\nA C++ exception was thrown where nothing could catch it: on a "
                              "background thread, or inside a window-procedure callback. The "
                              "address above is the unwinder, not the throw. Set "
                              "ULTRACANVAS_DEBUG_LOG to a file path and reproduce: the log names "
                              "the operation, the file and the error text.");
            } else if (code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_PRIV_INSTRUCTION) {
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

            // The offset into the module and the function the fault is in.
            char location[260] = "";
            if (module) {
                char described[256] = "";
                DescribeCodeAddress(reinterpret_cast<DWORD64>(address), false, described,
                                    sizeof(described));
                std::snprintf(location, sizeof(location), " (%s)", described);
            }

            char message[1024];
            std::snprintf(message, sizeof(message),
                          "%s crashed: exception 0x%08lX (%s) at 0x%016llX%s in %s. %s",
                          gCrashAppName, static_cast<unsigned long>(code),
                          ExceptionCodeName(code),
                          static_cast<unsigned long long>(
                              reinterpret_cast<std::uintptr_t>(address)),
                          location, moduleName, gCrashOsVersion);

            // How the thread got there, one caller per line. Not for a stack
            // overflow, whose filter runs on what is left of the exhausted
            // stack. Static buffers: the box text is a few kilobytes.
            static char callStack[kCrashCallStackText] = "";
            callStack[0] = '\0';
#if defined(ULTRACANVAS_CRASH_CALL_STACK)
            if (code != EXCEPTION_STACK_OVERFLOW && info && info->ContextRecord &&
                !gWalkingCallStack.exchange(true)) {
                DWORD64 frames[kCrashStackFrames] = {};
                const int count = CaptureCallStack(*info->ContextRecord, frames, kCrashStackFrames);
                gWalkingCallStack = false;
                size_t used = AppendText(callStack, sizeof(callStack), 0, "Call stack:", 11);
                for (int i = 0; i < count; ++i) {
                    char frame[320];
                    char line[340];
                    DescribeCodeAddress(frames[i], i > 0, frame, sizeof(frame));
                    std::snprintf(line, sizeof(line), "\n  #%d %s", i, frame);
                    used = AppendText(callStack, sizeof(callStack), used, line, sizeof(line));
                }
                if (count == 0) callStack[0] = '\0';
            }
#endif

            CrashLogLine(message);
            if (detail[0]) CrashLogLine(detail);
            if (callStack[0]) CrashLogLine(callStack);
            CrashLogLine(dumpLine);
            if (gCrashDialogAllowed) {
                static char full[sizeof(message) + sizeof(detail) + kCrashCallStackText +
                                 sizeof(dumpNote) + 8];
                std::snprintf(full, sizeof(full), "%s%s%s%s%s", message, detail,
                              callStack[0] ? "\n" : "", callStack, dumpNote);
                MessageBoxA(nullptr, full, gCrashAppName,
                            MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
            }
            if (detail[0]) return EXCEPTION_CONTINUE_SEARCH;

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

        // The dump writer and its folder, resolved while everything works.
        // dbghelp.dll ships with Windows; nothing links against it, so a
        // system without it (there is none) merely writes no dump. The
        // module stays loaded for the life of the process - a crash handler
        // cannot load anything.
        gMiniDumpWriteDump = nullptr;
        if (HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll")) {
            gMiniDumpWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(
                reinterpret_cast<void (*)()>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));
        }
        {
            // The app name as a file-name stem: letters, digits, '-' and '_'.
            size_t n = 0;
            for (const char* c = gCrashAppName; *c && n + 1 < 64; ++c) {
                const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                                (*c >= '0' && *c <= '9') || *c == '-' || *c == '_';
                if (ok) gCrashDumpBaseName[n++] = static_cast<wchar_t>(*c);
            }
            if (n == 0) { std::wmemcpy(gCrashDumpBaseName, L"UltraCanvas", 12); n = 11; }
            gCrashDumpBaseName[n] = L'\0';
        }
        if (!EnvFlagSet("ULTRACANVAS_NO_CRASH_DUMP")) PrepareCrashDumpDir();

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

    void ReportWindowsEventException(const std::string& where, const std::string& what) {
        debugOutput << "UltraCanvas: exception escaped the " << where
                    << " handler: " << what << std::endl;

        // Once per process: the handler that threw is normally hit again by
        // the next event of the same kind (every mouse move, every repaint),
        // and a dialog per event would be a storm the user cannot get out of.
        static std::atomic<bool> reported{false};
        if (reported.exchange(true)) return;
        if (DialogsSuppressed()) return;

        const std::string message =
                std::string(gCrashAppName) + ": an error occurred while handling " + where +
                ":\n\n" + what +
                "\n\nThe operation was abandoned. Further errors of this kind are only "
                "written to the log (set ULTRACANVAS_DEBUG_LOG to a file path).";
        MessageBoxA(nullptr, message.c_str(), gCrashAppName,
                    MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);
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
