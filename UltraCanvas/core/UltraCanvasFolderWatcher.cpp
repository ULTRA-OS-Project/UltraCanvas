// core/UltraCanvasFolderWatcher.cpp
// Platform-independent half of the folder watcher: lifetime, the "which
// folder" bookkeeping, and the fallback for platforms with no native backend.
// Every operating system call lives in OS/<Platform>/, so nothing here needs
// to know how a given system reports directory changes.
// Version: 1.1.0 - the process's watchers are registered, so an
//                  UltraCanvasFolderWatchHold can keep them off a folder
//                  about to be deleted, renamed or moved
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework

#include "UltraCanvasFolderWatcher.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8

#include <cwctype>
#include <filesystem>
#include <set>
#include <vector>

namespace fs = std::filesystem;

namespace UltraCanvas {

    namespace {
        // Every watcher alive in the process, for ReleaseWatchesUnder. Never
        // destroyed: a watcher that outlives static destruction must still
        // find it to sign out.
        std::mutex& RegistryMutex() {
            static std::mutex* m = new std::mutex();
            return *m;
        }
        std::set<UltraCanvasFolderWatcher*>& Registry() {
            static auto* r = new std::set<UltraCanvasFolderWatcher*>();
            return *r;
        }
        // The folders held off by live UltraCanvasFolderWatchHolds, once per
        // hold (two holds on one folder list it twice). Under RegistryMutex.
        std::vector<std::string>& HeldFolders() {
            static auto* h = new std::vector<std::string>();
            return *h;
        }

        // A path's names, without the empty element a trailing separator
        // leaves; "." and ".." already resolved.
        std::vector<fs::path::string_type> Components(const std::string& utf8) {
            std::vector<fs::path::string_type> out;
            for (const fs::path& part : PathFromUtf8(utf8).lexically_normal()) {
                if (!part.empty()) out.push_back(part.native());
            }
            return out;
        }

        bool SameName(const fs::path::string_type& a, const fs::path::string_type& b) {
#if defined(_WIN32) || defined(_WIN64)
            // Windows names differ in case and in nothing else ("c:\Users"
            // from one API, "C:\Users" from another).
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                if (std::towlower(static_cast<wint_t>(a[i])) !=
                    std::towlower(static_cast<wint_t>(b[i])))
                    return false;
            }
            return true;
#else
            return a == b;
#endif
        }

        bool IsSameOrBelow(const std::string& path, const std::string& ancestor) {
            const auto p = Components(path);
            const auto a = Components(ancestor);
            if (a.empty() || p.size() < a.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                if (!SameName(p[i], a[i])) return false;
            }
            return true;
        }

        // Under RegistryMutex.
        bool IsHeldOff(const std::string& path) {
            for (const std::string& folder : HeldFolders()) {
                if (IsSameOrBelow(path, folder)) return true;
            }
            return false;
        }
    }

#ifndef ULTRACANVAS_HAS_NATIVE_FOLDER_WATCH
    // No backend on this platform: every Watch() fails and the caller polls.
    // The platforms that do have one define the symbol in their own file and
    // CMake sets ULTRACANVAS_HAS_NATIVE_FOLDER_WATCH there.
    std::unique_ptr<IFolderWatchBackend> CreateNativeFolderWatchBackend() {
        return nullptr;
    }
#endif

    UltraCanvasFolderWatcher::UltraCanvasFolderWatcher() {
        std::lock_guard<std::mutex> lk(RegistryMutex());
        Registry().insert(this);
    }

    UltraCanvasFolderWatcher::~UltraCanvasFolderWatcher() {
        // Out of the registry first, so no release reaches a watcher on its
        // way out; then stopped, which joins the backend.
        {
            std::lock_guard<std::mutex> lk(RegistryMutex());
            Registry().erase(this);
        }
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
        // The registry first, then this watcher - the order a hold takes
        // them in - so a hold taken now either sees this watch or keeps it
        // from starting.
        std::lock_guard<std::mutex> registryLock(RegistryMutex());
        std::lock_guard<std::mutex> lk(mutex);
        StopLocked();
        if (path.empty() || !onChanged) return false;
        if (IsHeldOff(path)) return false;   // about to be deleted or moved

        // One backend instance per watched folder: starting fresh is simpler
        // than asking every platform to support re-targeting, and switching
        // folders is a user navigation, not a hot path.
        backend = CreateNativeFolderWatchBackend();
        if (!backend) return false;

        // The failure report, once per watch, whoever notices first: the
        // backend's thread when the watch dies, or a release.
        FailedCallback once;
        if (onFailed) {
            auto fired = std::make_shared<std::atomic<bool>>(false);
            once = [fired, report = std::move(onFailed)]() {
                if (!fired->exchange(true)) report();
            };
        }
        if (!backend->Start(path, std::move(onChanged), once)) {
            backend.reset();
            return false;
        }
        watchedPath = path;
        failedCallback = std::move(once);
        watching.store(true);
        return true;
    }

    void UltraCanvasFolderWatcher::Stop() {
        std::lock_guard<std::mutex> lk(mutex);
        StopLocked();
    }

    void UltraCanvasFolderWatcher::StopLocked() {
        if (backend) {
            backend->Stop();   // joins its thread: no callback survives this
            backend.reset();
        }
        watchedPath.clear();
        failedCallback = nullptr;
        watching.store(false);
    }

    void UltraCanvasFolderWatcher::ReleaseLocked() {
        // As if the watch had died: the handle goes (Stop() joins the
        // backend's thread), the path stays for the owner to poll, and the
        // owner hears it through its failure report - under this watcher's
        // lock, so a Stop() racing the release returns only once the report
        // is out, as the contract says.
        if (!backend) return;
        backend->Stop();
        backend.reset();
        watching.store(false);
        if (failedCallback) failedCallback();
        failedCallback = nullptr;
    }

    // ===== HOLDS =====
    UltraCanvasFolderWatchHold::UltraCanvasFolderWatchHold(std::vector<std::string> held)
            : folders(std::move(held)) {
        std::lock_guard<std::mutex> registryLock(RegistryMutex());
        for (const std::string& folder : folders) {
            if (!folder.empty()) HeldFolders().push_back(folder);
        }
        for (UltraCanvasFolderWatcher* watcher : Registry()) {
            std::lock_guard<std::mutex> lk(watcher->mutex);
            if (!watcher->backend) continue;
            for (const std::string& folder : folders) {
                if (folder.empty() || !IsSameOrBelow(watcher->watchedPath, folder)) continue;
                watcher->ReleaseLocked();
                ++released;
                break;
            }
        }
    }

    UltraCanvasFolderWatchHold::~UltraCanvasFolderWatchHold() {
        std::lock_guard<std::mutex> registryLock(RegistryMutex());
        std::vector<std::string>& held = HeldFolders();
        for (const std::string& folder : folders) {
            if (folder.empty()) continue;
            // This hold's own entry: one of possibly several equal ones.
            for (auto it = held.begin(); it != held.end(); ++it) {
                if (*it == folder) { held.erase(it); break; }
            }
        }
    }

} // namespace UltraCanvas
