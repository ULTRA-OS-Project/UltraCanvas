// include/UltraCanvasUtils.h
// Utils
// Version: 1.1.0
// Last Modified: 2026-07-21
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasTextUtils.h"   // Trim/Split/ToLowerCase..., Base64 / Base32
#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <memory>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {
    extern const char* versionString;
    // ToLowerCase, StartsWith, Trim, Split, L/R/TrimWhitespace and the Base64 /
    // Base32 codecs are declared in UltraCanvasTextUtils.h (included above):
    // platform-free text helpers compiled into the UltraCanvasTextUtils
    // library, so headless modules can link them without this file's glue.
    Color ParseColor(const std::string& colorStr);
    std::string GetFileExtension(const std::string& filePath);
    std::string LoadFile(const std::string& filePath);
    std::string FormatFileSize(size_t bytes);

    std::string GetExecutableDir();
    std::string NormalizePath(const std::string& in);

    // Is the file/folder hidden by the conventions of the platform it lives
    // on? A leading dot hides on every platform; Windows additionally hides
    // entries carrying the HIDDEN file attribute (the NTUSER.DAT hives and
    // the "Anwendungsdaten"-style compatibility junctions of a profile
    // folder), macOS entries carrying the UF_HIDDEN flag (~/Library).
    // Costs one file-attribute lookup on Windows/macOS when the name alone
    // does not already decide it; `path` must be the full path of the entry.
    bool IsHiddenFileSystemEntry(const std::filesystem::path& path);

    // The user's well-known folders, resolved through the platform:
    // SHGetKnownFolderPath on Windows (follows folder redirection, e.g. a
    // Documents folder moved into OneDrive), the fixed home subfolders on
    // macOS, the xdg-user-dirs configuration on Linux (localized names,
    // entries pointing at $HOME itself are disabled per the spec). Only
    // folders that exist are returned, in the canonical Desktop, Documents,
    // Downloads, Music, Pictures, Videos, Public, Templates order; paths are
    // encoded like std::filesystem::path::string() on the platform.
    enum class UserFolderKind {
        Desktop, Documents, Downloads, Music, Pictures, Videos, Public, Templates
    };
    struct UserFolderInfo {
        UserFolderKind kind;
        std::string path;    // absolute path of an existing directory
        std::string label;   // display name (the on-disk folder name)
    };
    std::vector<UserFolderInfo> GetWellKnownUserFolders();

    // Its cloud-storage counterpart - the OneDrive / Google Drive / Dropbox /
    // iCloud folders present on this machine - is GetCloudStorageFolders() in
    // UltraCanvasCloudStorage.h. It lives in its own header because reading
    // the Dropbox configuration needs UltraCanvasJSON, which this bottom-of-
    // the-stack header deliberately does not drag in.

    // UltraCanvas strings are UTF-8 everywhere. On Windows the narrow CRT /
    // ANSI Win32 APIs interpret narrow strings in the legacy system code page,
    // so characters outside it (Thai, CJK, ...) get mangled to '?'. These
    // helpers convert a UTF-8 string to a std::filesystem::path via UTF-16 so
    // file opens and directory walks work for any file name; on other
    // platforms they pass through unchanged.
    std::filesystem::path PathFromUtf8(const std::string& utf8);
    std::string PathToUtf8(const std::filesystem::path& p);
#if defined(_WIN32) || defined(_WIN64)
    std::wstring Utf8ToWide(const std::string& utf8);
    std::string WideToUtf8(const std::wstring& wide);
#endif

    void OpenURL(const std::string& url);

    // Starts argv[0] with the given arguments, fully detached from the
    // calling process: closing this application never takes the launched one
    // down, and no zombie is left behind (POSIX: double fork + setsid;
    // Windows: CreateProcess into a new detached process group).
    // workingDirectory may be empty (the child inherits the current one).
    // Returns false with a user-presentable outError when nothing could be
    // started; a child that starts but fails to exec reports success — the
    // detachment makes the exec result unobservable.
    bool LaunchDetachedProcess(const std::vector<std::string>& argv,
                               const std::string& workingDirectory,
                               std::string& outError);

    template <typename Func, typename... Args>
    void measureExecutionTime(const std::string& logPrefix, Func&& func, Args&&... args) {
        auto start = std::chrono::high_resolution_clock::now();

        // Execute the provided function
        std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);

        auto end = std::chrono::high_resolution_clock::now();

        // Return duration in microseconds
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        debugOutput << logPrefix << " Execution time: " << duration << " us\n";
    }


// Cache entry MUST have payload shared pointer, lastAccess and GetEntrySize method, like below
//    struct UCPixmapCairoCacheEntry {
//        std::shared_ptr<UCPixmapCairo> payload;
//        std::chrono::steady_clock::time_point lastAccess;
//        size_t GetEntrySize() {
//            return payload->GetWidth() * payload->GetHeight() * 4 + sizeof(UCPixmapCairoCacheEntry);
//        }
//    };
//
// GetEntrySize() is asked ONCE, when the entry is stored, and the answer is
// what the cache holds against the budget and gives back when the entry goes.
// It is never asked again, because for several payloads it does not stay the
// same: an image's size counts a lazily decoded animation, an SVG document's
// counts pages it rasterizes on demand. Re-asking at eviction time returned
// MORE than was ever added, and `currentCacheSize` is unsigned - so the total
// wrapped to an enormous number, every later insert found itself over budget,
// and the loop below emptied the whole cache to make room for one entry. The
// cache then held one item for the rest of the session: every image was
// decoded again on every use, which is what "the thumbnails stopped showing"
// looked like from the outside. An entry whose payload grows is therefore
// under-counted rather than over-counted, which costs some memory and keeps
// the cache working; a caller that needs the new size re-adds the entry, and
// the overwrite path below accounts for that correctly.

    template <class ET, class CACHEENTRY> class UCCache {
    private:
        // The entry as the caller defined it, plus the size it was charged
        // for. Keeping the two together is what makes add and remove exact
        // inverses of each other.
        struct Slot {
            CACHEENTRY entry;
            size_t bytes = 0;
        };

        std::unordered_map<std::string, Slot> cache;
        std::mutex cacheMutex;
        size_t maxCacheSize = 50 * 1024 * 1024;
        size_t currentCacheSize = 0;

        // Give back exactly what `it` was charged. std::min because a counter
        // that cannot go below zero is worth more than one that is arithmetically
        // pure: an underflow here is not a small error, it is a cache that
        // believes it is permanently full.
        void ReleaseSlot(typename std::unordered_map<std::string, Slot>::iterator it) {
            currentCacheSize -= std::min(currentCacheSize, it->second.bytes);
        }

        void RemoveOldestCacheEntry() {
            // Find oldest entry (no lock needed, called from locked context)
            auto oldest = cache.begin();
            for (auto it = cache.begin(); it != cache.end(); ++it) {
                if (it->second.entry.lastAccess < oldest->second.entry.lastAccess) {
                    oldest = it;
                }
            }

            if (oldest != cache.end()) {
                ReleaseSlot(oldest);
                cache.erase(oldest);
            }
        }
    public:
        UCCache(size_t maxCSize) : maxCacheSize(maxCSize) {}

        void AddToCache(const std::string& key, std::shared_ptr<ET> p) {
            if (!p) return;

            std::lock_guard<std::mutex> lock(cacheMutex);

            Slot slot;
            slot.entry.lastAccess = std::chrono::steady_clock::now();
            slot.entry.payload = p;
            slot.bytes = slot.entry.GetEntrySize();

            const size_t dataSize = slot.bytes;

            // Replacing an entry returns the old one's bytes first: the key
            // holds one payload, not two. Two threads that miss on the same
            // key and both decode it - four thumbnail workers on one picture -
            // arrive here one after the other, and without this the second
            // would charge for a payload the first one's is replacing.
            auto existing = cache.find(key);
            if (existing != cache.end()) {
                ReleaseSlot(existing);
                cache.erase(existing);
            }

            // Check if we need to make room
            while (currentCacheSize + dataSize > maxCacheSize && !cache.empty()) {
                RemoveOldestCacheEntry();
            }

            cache[key] = std::move(slot);
            currentCacheSize += dataSize;
        }

        std::shared_ptr<ET> GetFromCache(const std::string& key) {
            std::lock_guard<std::mutex> lock(cacheMutex);

            auto it = cache.find(key);
            if (it != cache.end()) {
                it->second.entry.lastAccess = std::chrono::steady_clock::now();
                return it->second.entry.payload;
            }

            return nullptr;
        }

        void ClearCache() {
            std::lock_guard<std::mutex> lock(cacheMutex);
            cache.clear();
            currentCacheSize = 0;
        }

        // Drop a single entry by exact key. Returns true if one was removed.
        bool RemoveFromCache(const std::string& key) {
            std::lock_guard<std::mutex> lock(cacheMutex);
            auto it = cache.find(key);
            if (it == cache.end()) return false;
            ReleaseSlot(it);
            cache.erase(it);
            return true;
        }

        // Drop every entry whose key begins with `prefix`, returning how many
        // were removed. Used to evict all derived entries of one source at once
        // (e.g. every cached pixmap size/scale of a single image path).
        size_t RemoveFromCacheByPrefix(const std::string& prefix) {
            std::lock_guard<std::mutex> lock(cacheMutex);
            size_t removed = 0;
            for (auto it = cache.begin(); it != cache.end();) {
                if (it->first.compare(0, prefix.size(), prefix) == 0) {
                    ReleaseSlot(it);
                    it = cache.erase(it);
                    ++removed;
                } else {
                    ++it;
                }
            }
            return removed;
        }

        void SetMaxCacheSize(size_t size) { maxCacheSize = size; }

        // What the cache believes it is holding, and how many entries that is.
        // Public so a test can assert the two stay in step with what was put
        // in - the drift these two numbers used to develop was invisible from
        // the outside until the cache had emptied itself.
        size_t GetCurrentCacheSize() {
            std::lock_guard<std::mutex> lock(cacheMutex);
            return currentCacheSize;
        }
        size_t GetEntryCount() {
            std::lock_guard<std::mutex> lock(cacheMutex);
            return cache.size();
        }
    };

}