// OS/MSWindows/UltraCanvasWindowsFileTimes.cpp
// CopyFileTimes on Windows: creation, last-access and last-write times read
// with GetFileTime and written with SetFileTime. CopyFile keeps only the
// last-write time; Explorer shows a copy as created when it was copied unless
// this is done. BACKUP_SEMANTICS opens a folder as well as a file; every
// sharing flag is ours, so neither handle is what another program trips over.
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
        HANDLE OpenForTimes(const std::wstring& path, DWORD access) {
            return CreateFileW(path.c_str(), access,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        }
    } // namespace

    bool NativeCopyFileTimes(const std::string& fromUtf8, const std::string& toUtf8) {
        FILETIME created{}, accessed{}, written{};
        HANDLE source = OpenForTimes(PathFromUtf8(fromUtf8).native(), FILE_READ_ATTRIBUTES);
        if (source == INVALID_HANDLE_VALUE) return false;
        const BOOL read = GetFileTime(source, &created, &accessed, &written);
        CloseHandle(source);
        if (!read) return false;

        HANDLE target = OpenForTimes(PathFromUtf8(toUtf8).native(), FILE_WRITE_ATTRIBUTES);
        if (target == INVALID_HANDLE_VALUE) return false;
        const BOOL wroteTimes = SetFileTime(target, &created, &accessed, &written);
        CloseHandle(target);
        return wroteTimes != FALSE;
    }

} // namespace UltraCanvas
