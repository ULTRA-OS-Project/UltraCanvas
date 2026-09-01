// core/UltraCanvasFolderWatcher.cpp
// Platform-independent half of the folder watcher: lifetime, the "which
// folder" bookkeeping, and the fallback for platforms with no native backend.
// Every operating system call lives in OS/<Platform>/, so nothing here needs
// to know how a given system reports directory changes.
// Version: 1.0.0
// Last Modified: 2026-08-25
// Author: UltraCanvas Framework

#include "UltraCanvasFolderWatcher.h"

namespace UltraCanvas {

#ifndef ULTRACANVAS_HAS_NATIVE_FOLDER_WATCH
    // No backend on this platform: every Watch() fails and the caller polls.
    // The platforms that do have one define the symbol in their own file and
    // CMake sets ULTRACANVAS_HAS_NATIVE_FOLDER_WATCH there.
    std::unique_ptr<IFolderWatchBackend> CreateNativeFolderWatchBackend() {
        return nullptr;
    }
#endif

    UltraCanvasFolderWatcher::UltraCanvasFolderWatcher() = default;

    UltraCanvasFolderWatcher::~UltraCanvasFolderWatcher() {
        Stop();
    }

    bool UltraCanvasFolderWatcher::NativeBackendAvailable() {
#ifdef ULTRACANVAS_HAS_NATIVE_FOLDER_WATCH
        return true;
#else
        return false;
#endif
    }

    bool UltraCanvasFolderWatcher::Watch(const std::string& path,
                                         ChangedCallback onChanged,
                                         FailedCallback onFailed) {
        Stop();
        if (path.empty() || !onChanged) return false;

        // One backend instance per watched folder: starting fresh is simpler
        // than asking every platform to support re-targeting, and switching
        // folders is a user navigation, not a hot path.
        backend = CreateNativeFolderWatchBackend();
        if (!backend) return false;

        if (!backend->Start(path, std::move(onChanged), std::move(onFailed))) {
            backend.reset();
            return false;
        }
        watchedPath = path;
        watching = true;
        return true;
    }

    void UltraCanvasFolderWatcher::Stop() {
        if (backend) {
            backend->Stop();   // joins its thread: no callback survives this
            backend.reset();
        }
        watchedPath.clear();
        watching = false;
    }

} // namespace UltraCanvas
