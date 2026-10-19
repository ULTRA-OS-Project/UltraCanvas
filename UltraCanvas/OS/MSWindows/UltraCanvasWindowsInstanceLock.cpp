// OS/MSWindows/UltraCanvasWindowsInstanceLock.cpp
// Windows backend for UltraCanvasInstanceLock: LockFileEx on the first byte
// of the lock file.
//
// The file is opened sharing read, write and delete, so a virus scanner or a
// backup program that opens it is never mistaken for a second copy; only the
// byte-range lock decides. Windows releases it when the handle closes, which
// the system does at process exit however the process ends.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraCanvasInstanceLock.h"
#include "UltraCanvasPathUtf8.h"

#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace UltraCanvas {

    InstanceLockResult NativeAcquireInstanceLock(const std::string& path, intptr_t& handle,
                                                 std::string& error) {
        HANDLE file = ::CreateFileW(PathFromUtf8(path).c_str(), GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            error = "cannot open " + path + " (error " + std::to_string(::GetLastError()) + ")";
            return InstanceLockResult::Failed;
        }
        OVERLAPPED at{};
        if (!::LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &at)) {
            const DWORD code = ::GetLastError();
            ::CloseHandle(file);
            if (code == ERROR_LOCK_VIOLATION || code == ERROR_IO_PENDING) return InstanceLockResult::HeldElsewhere;
            error = "cannot lock " + path + " (error " + std::to_string(code) + ")";
            return InstanceLockResult::Failed;
        }
        handle = reinterpret_cast<intptr_t>(file);
        return InstanceLockResult::Acquired;
    }

    void NativeReleaseInstanceLock(intptr_t handle) {
        HANDLE file = reinterpret_cast<HANDLE>(handle);
        OVERLAPPED at{};
        ::UnlockFileEx(file, 0, 1, 0, &at);
        ::CloseHandle(file);
    }

} // namespace UltraCanvas
