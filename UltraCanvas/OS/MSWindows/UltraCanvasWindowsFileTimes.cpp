// OS/MSWindows/UltraCanvasWindowsFileTimes.cpp
// UltraCanvasFileTimes on Windows: creation, last-access and last-write times
// read with GetFileTime and written with SetFileTime (a date left at 0 is
// passed as NULL, which leaves it as it is). CopyFile keeps only the
// last-write time; Explorer shows a copy as created when it was copied unless
// this is done. BACKUP_SEMANTICS opens a folder as well as a file; every
// sharing flag is ours, so neither handle is what another program trips over.
// Version: 1.1.0 - NativeGetFileTimes / NativeSetFileTimes
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasFileTimes.h"
#include "UltraCanvasPathUtf8.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace UltraCanvas {

    namespace {
        // FILETIME counts 100 ns ticks from 1601-01-01 UTC.
        constexpr int64_t kTicksTo1970 = 116444736000000000LL;

        HANDLE OpenForTimes(const std::wstring& path, DWORD access) {
            return CreateFileW(path.c_str(), access,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        }
        int64_t ToUnixNanos(const FILETIME& ft) {
            const int64_t ticks = (static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
            if (ticks == 0) return 0;
            return (ticks - kTicksTo1970) * 100;
        }
        FILETIME FromUnixNanos(int64_t nanos) {
            const int64_t ticks = nanos / 100 + kTicksTo1970;
            FILETIME ft{};
            ft.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFF);
            ft.dwHighDateTime = static_cast<DWORD>(static_cast<uint64_t>(ticks) >> 32);
            return ft;
        }
    } // namespace

    bool NativeGetFileTimes(const std::string& pathUtf8, FileTimes& out) {
        HANDLE h = OpenForTimes(PathFromUtf8(pathUtf8).native(), FILE_READ_ATTRIBUTES);
        if (h == INVALID_HANDLE_VALUE) return false;
        FILETIME created{}, accessed{}, written{};
        const BOOL read = GetFileTime(h, &created, &accessed, &written);
        CloseHandle(h);
        if (!read) return false;
        out.created = ToUnixNanos(created);
        out.accessed = ToUnixNanos(accessed);
        out.modified = ToUnixNanos(written);
        return true;
    }

    bool NativeSetFileTimes(const std::string& pathUtf8, const FileTimes& times) {
        HANDLE h = OpenForTimes(PathFromUtf8(pathUtf8).native(), FILE_WRITE_ATTRIBUTES);
        if (h == INVALID_HANDLE_VALUE) return false;
        const FILETIME created = FromUnixNanos(times.created);
        const FILETIME accessed = FromUnixNanos(times.accessed);
        const FILETIME written = FromUnixNanos(times.modified);
        const BOOL set = SetFileTime(h, times.created ? &created : nullptr,
                                     times.accessed ? &accessed : nullptr,
                                     times.modified ? &written : nullptr);
        CloseHandle(h);
        return set != FALSE;
    }

} // namespace UltraCanvas
