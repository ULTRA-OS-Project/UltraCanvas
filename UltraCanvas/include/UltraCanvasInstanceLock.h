// include/UltraCanvasInstanceLock.h
// One running copy of an application per user: the lock a program takes at
// start so a second start can tell that the first is still there.
//
//   InstanceLock lock;
//   if (lock.Acquire(dataDir + "/instance.lock") == InstanceLockResult::HeldElsewhere) {
//       // another copy runs: hand it the request and quit
//   }
//
// The lock is an operating-system lock on a file, not the file's existence:
// the system drops it when the process ends, however it ends, so a crash
// never leaves a stale lock behind that would stop the next start. The file
// itself stays and is reused.
//
// Backends: POSIX flock() (Linux, the BSDs, macOS) and Windows LockFileEx.
// Android and WebAssembly have none: Acquire() answers Unsupported there, and
// InstanceLockAvailable() is false, so the caller runs as if no lock existed.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <string>

namespace UltraCanvas {

    enum class InstanceLockResult {
        Acquired,       // this process holds the lock now
        HeldElsewhere,  // another process holds it: another copy is running
        Failed,         // the lock file could not be opened (see the error)
        Unsupported     // no backend on this platform
    };

    // ===== BACKEND INTERFACE (implemented per platform under OS/<Platform>/) =====
    // Not part of the public surface: callers use InstanceLock. On Acquired
    // `handle` is the open file (a descriptor, or a HANDLE on Windows) that
    // NativeReleaseInstanceLock closes.
    InstanceLockResult NativeAcquireInstanceLock(const std::string& path, intptr_t& handle,
                                                 std::string& error);
    void NativeReleaseInstanceLock(intptr_t handle);

    class InstanceLock {
    public:
        InstanceLock() = default;
        ~InstanceLock() { Release(); }
        InstanceLock(const InstanceLock&) = delete;
        InstanceLock& operator=(const InstanceLock&) = delete;

        // Tries, without waiting, to take the lock on the file at `path`
        // (UTF-8), creating the file and its folder when they are missing.
        // `error` receives the reason on Failed. Calling it again while held
        // answers Acquired.
        InstanceLockResult Acquire(const std::string& path, std::string* error = nullptr);
        // Lets go; the next start can take it. The destructor does the same.
        void Release();
        bool IsHeld() const { return handle_ != -1; }

    private:
        intptr_t handle_ = -1;
    };

    // Whether this build can lock at all. False means Acquire() always
    // answers Unsupported.
    bool InstanceLockAvailable();

} // namespace UltraCanvas
