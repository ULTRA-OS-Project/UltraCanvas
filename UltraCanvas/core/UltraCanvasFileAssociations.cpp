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
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <sys/wait.h>
#include <unistd.h>
#endif

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

// ===== DIRECT EXECUTION =====
// POSIX-only: on Windows ShellExecute's "open" verb already runs
// executables through OpenWithDefaultApplication, and WebAssembly cannot
// run anything, so both report NotExecutable and the launcher refuses.
#if defined(_WIN32) || defined(__EMSCRIPTEN__)

ExecutableKind ClassifyExecutable(const std::string&) {
    return ExecutableKind::NotExecutable;
}

bool LaunchExecutable(const std::string& path, std::string& outError) {
    outError = "Running \"" + path + "\" directly is not supported here.";
    return false;
}

#else

ExecutableKind ClassifyExecutable(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec)
        return ExecutableKind::NotExecutable;
    if (::access(path.c_str(), X_OK) != 0)
        return ExecutableKind::NotExecutable;
    // The execute bit alone is not enough (FAT mounts set it on everything):
    // the content must actually look runnable.
    unsigned char head[4] = {};
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return ExecutableKind::NotExecutable;
    const size_t n = std::fread(head, 1, sizeof head, f);
    std::fclose(f);
    if (n >= 4 && head[0] == 0x7f && head[1] == 'E' && head[2] == 'L' &&
        head[3] == 'F')
        return ExecutableKind::Binary;          // ELF (AppImages included)
    if (n >= 4 && ((head[0] == 0xfe && head[1] == 0xed && head[2] == 0xfa) ||
                   (head[1] == 0xfa && head[2] == 0xed && head[3] == 0xfe) ||
                   (head[0] == 0xca && head[1] == 0xfe && head[2] == 0xba &&
                    head[3] == 0xbe)))
        return ExecutableKind::Binary;          // Mach-O / fat binary (macOS)
    if (n >= 2 && head[0] == '#' && head[1] == '!')
        return ExecutableKind::Script;
    return ExecutableKind::NotExecutable;
}

bool LaunchExecutable(const std::string& path, std::string& outError) {
    outError.clear();
    if (ClassifyExecutable(path) == ExecutableKind::NotExecutable) {
        outError = "\"" + path + "\" is not an executable.";
        return false;
    }
    // Double fork + setsid: the program is re-parented to init and survives
    // the filer closing, and no zombie is left behind.
    const pid_t first = ::fork();
    if (first < 0) {
        outError = "Could not start \"" + path + "\": fork failed.";
        return false;
    }
    if (first == 0) {
        ::setsid();
        const pid_t second = ::fork();
        if (second != 0) ::_exit(second < 0 ? 127 : 0);
        const std::string dir =
                std::filesystem::path(path).parent_path().string();
        if (!dir.empty() && ::chdir(dir.c_str()) != 0) { /* keep going */ }
        ::execl(path.c_str(), path.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);   // exec failed; the launcher cannot see this anymore
    }
    int status = 0;
    ::waitpid(first, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        outError = "Could not start \"" + path + "\".";
        return false;
    }
    return true;
}

#endif // direct execution

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

// ===== ON-DISK ICON CACHE RETENTION =====
// Shared by every backend that keeps extracted icons as files (Windows and
// macOS today); see UltraCanvasFileAssociationsBackend.h for why the policy
// lives here rather than in each of them. Pure std::filesystem, so it is
// compiled on every platform whether or not that platform's backend uses it.
namespace FileAssociationsBackend {

    namespace {
        namespace fs = std::filesystem;

        // A hit re-stamps the file, but only once the stamp has gone this
        // stale — a two-week window wants day resolution, not a disk write
        // every time a menu opens.
        constexpr auto kStampInterval = std::chrono::hours(24);

        // Compared as paths, not strings: fs::path::string_type is wide on
        // Windows and narrow elsewhere, and path comparison needs no encoding
        // conversion that could throw on an odd file name.
        bool IsIconCacheFile(const fs::path& path) {
            const fs::path ext = path.extension();
            return ext == ".png" || ext == ".tmp";
        }
    } // namespace

    void StampIconCacheFile(const std::string& path) {
        std::error_code ec;
        const fs::path file = PathFromUtf8(path);
        const auto stamp = fs::last_write_time(file, ec);
        if (ec) return;
        const auto now = fs::file_time_type::clock::now();
        if (now - stamp < kStampInterval) return;
        // A read-only cache directory makes this fail; that is harmless — the
        // file is simply swept earlier than it would otherwise have been.
        fs::last_write_time(file, now, ec);
    }

    void SweepIconCache(const std::string& directory) {
        std::error_code ec;
        fs::directory_iterator it(PathFromUtf8(directory), ec);
        if (ec) return;
        const auto now = fs::file_time_type::clock::now();
        // Collected first, deleted after: removing entries from a directory
        // while walking it is not something the iterator promises to survive.
        std::vector<fs::path> expired;
        for (const fs::directory_entry& entry : it) {
            if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
            if (!IsIconCacheFile(entry.path())) continue;
            const auto stamp = entry.last_write_time(ec);
            // Unreadable stamp: leave the file alone rather than guess at it.
            if (ec) { ec.clear(); continue; }
            // A stamp in the future — a clock that was set back — reads as
            // infinitely fresh. That is the safe way round, so let it be.
            if (now - stamp <= kIconCacheMaxAge) continue;
            expired.push_back(entry.path());
        }
        for (const fs::path& path : expired) {
            fs::remove(path, ec);
            ec.clear();   // in use by another process: it goes next time
        }
    }

} // namespace FileAssociationsBackend

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
