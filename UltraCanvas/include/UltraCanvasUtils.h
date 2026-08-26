// include/UltraCanvasUtils.h
// Utils
// Version: 1.1.0
// Last Modified: 2026-07-21
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUtils.h"
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
#include "UltraCanvasDebug.h"

namespace UltraCanvas {
    extern const char* versionString;
    std::string ToLowerCase(const std::string &str);
    bool StartsWith(const std::string& str, const std::string& prefix);
    std::string Trim(const std::string& str, const std::string& strippedChars = " \t\r\n");
    std::vector<std::string> Split(const std::string& str, char delimiter);
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

    std::vector<uint8_t> Base64Decode(const std::string& input);
    std::string Base64Encode(const std::vector<uint8_t>& in, bool wrap = true);

    inline std::string LTrimWhitespace(std::string s) {
        std::string result = s;
        // NOTE: iterate `result` consistently. Mixing s.begin() with result.end()
        // walks off the end of a different allocation (heap overflow), since `s`
        // and `result` are distinct string objects.
        result.erase(result.begin(), std::find_if(result.begin(), result.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        return result;
    }

// Trim from the end (in place)
    inline std::string RTrimWhitespace(std::string s) {
        std::string result = s;
        result.erase(std::find_if(result.rbegin(), result.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), result.end());
        return result;
    }

    inline std::string TrimWhitespace(std::string s) {
        return LTrimWhitespace(RTrimWhitespace(s));
    }

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

    template <class ET, class CACHEENTRY> class UCCache {
    private:

        std::unordered_map<std::string, CACHEENTRY> cache;
        std::mutex cacheMutex;
        size_t maxCacheSize = 50 * 1024 * 1024;
        size_t currentCacheSize = 0;

        void RemoveOldestCacheEntry() {
            // Find oldest entry (no lock needed, called from locked context)
            auto oldest = cache.begin();
            for (auto it = cache.begin(); it != cache.end(); ++it) {
                if (it->second.lastAccess < oldest->second.lastAccess) {
                    oldest = it;
                }
            }

            if (oldest != cache.end()) {
                currentCacheSize -= oldest->second.GetEntrySize();
                cache.erase(oldest);
            }
        }
    public:
        UCCache(size_t maxCSize) : maxCacheSize(maxCSize) {}

        void AddToCache(const std::string& key, std::shared_ptr<ET> p) {
            if (!p) return;

            std::lock_guard<std::mutex> lock(cacheMutex);

            CACHEENTRY entry;
            entry.lastAccess = std::chrono::steady_clock::now();
            entry.payload = p;

            size_t dataSize = entry.GetEntrySize();

            // Check if we need to make room
            while (currentCacheSize + dataSize > maxCacheSize && !cache.empty()) {
                RemoveOldestCacheEntry();
            }

            cache[key] = std::move(entry);
            currentCacheSize += dataSize;
        }

        std::shared_ptr<ET> GetFromCache(const std::string& key) {
            std::lock_guard<std::mutex> lock(cacheMutex);

            auto it = cache.find(key);
            if (it != cache.end()) {
                it->second.lastAccess = std::chrono::steady_clock::now();
                return it->second.payload;
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
            currentCacheSize -= it->second.GetEntrySize();
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
                    currentCacheSize -= it->second.GetEntrySize();
                    it = cache.erase(it);
                    ++removed;
                } else {
                    ++it;
                }
            }
            return removed;
        }

        void SetMaxCacheSize(size_t size) { maxCacheSize = size; }
    };

}