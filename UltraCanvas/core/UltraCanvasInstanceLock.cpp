// core/UltraCanvasInstanceLock.cpp
// Platform-independent half of the single-instance lock: the InstanceLock
// class and the fallback for platforms with no backend. Every operating
// system call lives in OS/<Platform>/.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraCanvasInstanceLock.h"
#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <system_error>

namespace UltraCanvas {

#ifndef ULTRACANVAS_HAS_NATIVE_INSTANCE_LOCK
    // No backend on this platform (Android, WebAssembly): there is one
    // process per application there anyway. The platforms that do have one
    // define these in their own file and CMake sets
    // ULTRACANVAS_HAS_NATIVE_INSTANCE_LOCK there.
    InstanceLockResult NativeAcquireInstanceLock(const std::string&, intptr_t&, std::string&) {
        return InstanceLockResult::Unsupported;
    }
    void NativeReleaseInstanceLock(intptr_t) {}
#endif

    bool InstanceLockAvailable() {
#ifdef ULTRACANVAS_HAS_NATIVE_INSTANCE_LOCK
        return true;
#else
        return false;
#endif
    }

    InstanceLockResult InstanceLock::Acquire(const std::string& path, std::string* error) {
        if (IsHeld()) return InstanceLockResult::Acquired;
        std::error_code ec;
        const std::filesystem::path parent = PathFromUtf8(path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
        std::string message;
        intptr_t handle = -1;
        const InstanceLockResult result = NativeAcquireInstanceLock(path, handle, message);
        if (result == InstanceLockResult::Acquired) handle_ = handle;
        if (error) *error = message;
        return result;
    }

    void InstanceLock::Release() {
        if (!IsHeld()) return;
        NativeReleaseInstanceLock(handle_);
        handle_ = -1;
    }

} // namespace UltraCanvas
