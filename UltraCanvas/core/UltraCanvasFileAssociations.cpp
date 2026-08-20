// core/UltraCanvasFileAssociations.cpp
// Platform-independent half of the "Open with" service: the public API from
// UltraCanvasFileAssociations.h, a lazily spawned background worker, and the
// per-extension candidate cache. The platform backends (declared in
// UltraCanvasFileAssociationsBackend.h) do the actual association-database
// parsing and process launching; this file keeps them off the UI thread.
// Threading model: every backend call — from the worker and from cold-path
// synchronous lookups alike — runs under one backend mutex, so the backends
// stay lock-free. The cache has its own mutex and is safe from any thread.
// Version: 1.0.0
// Last Modified: 2026-08-16
// Author: UltraCanvas Framework

#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasFileAssociationsBackend.h"
#include "UltraCanvasUtils.h"   // ToLowerCase

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace UltraCanvas {
namespace FileAssociations {

namespace {

    // Cache key of one file: its lowercase extension, or — for extension-less
    // files like "Makefile", whose type comes from a literal-name glob — the
    // whole lowercase file name prefixed so the two namespaces cannot collide.
    std::string CacheKeyForFileName(const std::string& fileName) {
        const size_t slash = fileName.find_last_of("/\\");
        const std::string name = slash == std::string::npos
                                 ? fileName : fileName.substr(slash + 1);
        const size_t dot = name.find_last_of('.');
        if (dot != std::string::npos && dot + 1 < name.size() && dot > 0)
            return ToLowerCase(name.substr(dot + 1));
        return ":name:" + ToLowerCase(name);
    }

    class Service {
    public:
        static Service& Instance() {
            static Service instance;
            return instance;
        }

        std::vector<FileAssociationApp> Lookup(const std::string& path) {
            const std::string key = CacheKeyForFileName(path);
            {
                std::lock_guard<std::mutex> lk(cacheMutex);
                auto it = cache.find(key);
                if (it != cache.end()) return it->second;
            }
            // Cold miss: resolve synchronously — bounded by this one file
            // type, never by the whole prewarm.
            std::vector<FileAssociationApp> apps;
            {
                std::lock_guard<std::mutex> lk(backendMutex);
                if (FileAssociationsBackend::RefreshGlobalIndex())
                    DropCache();
                apps = FileAssociationsBackend::ResolveFile(path);
            }
            std::lock_guard<std::mutex> lk(cacheMutex);
            cache[key] = apps;
            return apps;
        }

        void QueueGlobalRefresh() {
            std::lock_guard<std::mutex> lk(queueMutex);
            globalRefreshQueued = true;
            EnsureWorkerLocked();
            queueSignal.notify_one();
        }

        void QueueExtensions(const std::vector<std::string>& extensions) {
            std::vector<std::string> fresh;
            {
                std::lock_guard<std::mutex> lk(cacheMutex);
                for (const std::string& e : extensions) {
                    if (e.empty()) continue;
                    const std::string key = ToLowerCase(e);
                    if (!cache.count(key)) fresh.push_back(key);
                }
            }
            if (fresh.empty()) return;
            std::lock_guard<std::mutex> lk(queueMutex);
            for (const std::string& key : fresh) {
                if (queuedKeys.insert(key).second)
                    extensionQueue.push_back(key);   // FIFO within one folder
            }
            EnsureWorkerLocked();
            queueSignal.notify_one();
        }

        std::mutex backendMutex;   // serializes ALL backend calls

        void DropCache() {
            std::lock_guard<std::mutex> lk(cacheMutex);
            cache.clear();
        }

    private:
        Service() = default;

        ~Service() {
            {
                std::lock_guard<std::mutex> lk(queueMutex);
                stopping = true;
                queueSignal.notify_one();
            }
            if (worker.joinable()) worker.join();
        }

        void EnsureWorkerLocked() {   // caller holds queueMutex
            if (worker.joinable() || stopping) return;
            worker = std::thread([this]() { WorkerLoop(); });
        }

        void WorkerLoop() {
            for (;;) {
                bool doGlobal = false;
                std::string key;
                {
                    std::unique_lock<std::mutex> lk(queueMutex);
                    queueSignal.wait(lk, [this]() {
                        return stopping || globalRefreshQueued ||
                               !extensionQueue.empty();
                    });
                    if (stopping) return;
                    if (globalRefreshQueued) {
                        globalRefreshQueued = false;
                        doGlobal = true;
                    } else {
                        key = extensionQueue.front();
                        extensionQueue.pop_front();
                        queuedKeys.erase(key);
                    }
                }
                if (doGlobal) {
                    std::lock_guard<std::mutex> lk(backendMutex);
                    if (FileAssociationsBackend::RefreshGlobalIndex())
                        DropCache();
                    continue;
                }
                {
                    std::lock_guard<std::mutex> lk(cacheMutex);
                    if (cache.count(key)) continue;   // resolved meanwhile
                }
                std::vector<FileAssociationApp> apps;
                {
                    std::lock_guard<std::mutex> lk(backendMutex);
                    if (FileAssociationsBackend::RefreshGlobalIndex())
                        DropCache();
                    // A representative name is enough — resolution only looks
                    // at the extension (":name:" keys never reach the queue).
                    apps = FileAssociationsBackend::ResolveFile("file." + key);
                }
                std::lock_guard<std::mutex> lk(cacheMutex);
                cache[key] = std::move(apps);
            }
        }

        std::mutex cacheMutex;
        std::unordered_map<std::string, std::vector<FileAssociationApp>> cache;

        std::mutex queueMutex;
        std::condition_variable queueSignal;
        std::deque<std::string> extensionQueue;
        std::unordered_set<std::string> queuedKeys;
        bool globalRefreshQueued = false;
        bool stopping = false;
        std::thread worker;
    };

} // namespace

std::vector<FileAssociationApp> GetApplicationsForFiles(
        const std::vector<std::string>& paths) {
    if (paths.empty()) return {};
    Service& service = Service::Instance();

    // The first file sets the order (and the default flag); every further
    // distinct file type intersects the list down to the applications that
    // can open the whole selection.
    std::vector<FileAssociationApp> result = service.Lookup(paths[0]);
    std::unordered_set<std::string> seenKeys{CacheKeyForFileName(paths[0])};
    for (size_t i = 1; i < paths.size() && !result.empty(); ++i) {
        if (!seenKeys.insert(CacheKeyForFileName(paths[i])).second) continue;
        std::unordered_set<std::string> ids;
        for (const FileAssociationApp& a : service.Lookup(paths[i]))
            ids.insert(a.id);
        result.erase(std::remove_if(result.begin(), result.end(),
                                    [&ids](const FileAssociationApp& a) {
                                        return !ids.count(a.id);
                                    }),
                     result.end());
    }
    return result;
}

bool OpenWithDefaultApplication(const std::vector<std::string>& paths,
                                std::string& outError) {
    outError.clear();
    if (paths.empty()) return false;
    Service& service = Service::Instance();
    std::lock_guard<std::mutex> lk(service.backendMutex);
    return FileAssociationsBackend::LaunchDefault(paths, outError);
}

bool OpenWithApplication(const FileAssociationApp& app,
                         const std::vector<std::string>& paths,
                         std::string& outError) {
    outError.clear();
    if (paths.empty()) return false;
    Service& service = Service::Instance();
    std::lock_guard<std::mutex> lk(service.backendMutex);
    return FileAssociationsBackend::LaunchWith(app, paths, outError);
}

bool OpenWithApplicationPath(const std::string& applicationPath,
                             const std::vector<std::string>& paths,
                             std::string& outError) {
    outError.clear();
    if (paths.empty() || applicationPath.empty()) return false;
    Service& service = Service::Instance();
    std::lock_guard<std::mutex> lk(service.backendMutex);
    return FileAssociationsBackend::LaunchWithPath(applicationPath, paths,
                                                   outError);
}

ApplicationFilter GetApplicationFilter() {
    return FileAssociationsBackend::GetApplicationFilter();
}

std::string GetApplicationsDirectory() {
    return FileAssociationsBackend::GetApplicationsDirectory();
}

void PrewarmAsync() {
    Service::Instance().QueueGlobalRefresh();
}

void PrewarmExtensionsAsync(const std::vector<std::string>& extensions) {
    Service::Instance().QueueExtensions(extensions);
}

} // namespace FileAssociations

// ===== BACKEND FOR PLATFORMS WITHOUT ONE =====
// WebAssembly has no application registry to enumerate or launch into; give
// it inline no-op backends so the core compiles without a platform file.
#if defined(__EMSCRIPTEN__)
namespace FileAssociationsBackend {
    bool RefreshGlobalIndex() { return false; }
    std::vector<FileAssociationApp> ResolveFile(const std::string&) { return {}; }
    bool LaunchDefault(const std::vector<std::string>&, std::string& outError) {
        outError = "Opening files with applications is not available here.";
        return false;
    }
    bool LaunchWith(const FileAssociationApp&, const std::vector<std::string>&,
                    std::string& outError) {
        outError = "Opening files with applications is not available here.";
        return false;
    }
    bool LaunchWithPath(const std::string&, const std::vector<std::string>&,
                        std::string& outError) {
        outError = "Opening files with applications is not available here.";
        return false;
    }
    FileAssociations::ApplicationFilter GetApplicationFilter() {
        return {"Applications", {"*"}};
    }
    std::string GetApplicationsDirectory() { return {}; }
} // namespace FileAssociationsBackend
#endif

} // namespace UltraCanvas
