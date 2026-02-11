// include/UltraCanvasUtils.h
// Utils
// Version: 1.0.0
// Last Modified: 2025-09-14
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUtils.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <mutex>

namespace UltraCanvas {
    extern const char* versionString;
    std::string ToLowerCase(const std::string &str);
    bool StartsWith(const std::string& str, const std::string& prefix);
    std::string Trim(const std::string& str);
    std::vector<std::string> Split(const std::string& str, char delimiter);
    Color ParseColor(const std::string& colorStr);
    std::string GetFileExtension(const std::string& filePath);
    std::string LoadFile(const std::string& filePath);
    std::string FormatFileSize(size_t bytes);

    std::vector<uint8_t> Base64Decode(const std::string& input);

    inline std::string LTrimWhitespace(std::string s) {
        std::string result = s;
        result.erase(result.begin(), std::find_if(s.begin(), result.end(), [](unsigned char ch) {
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

        std::cout << logPrefix << " Execution time: " << duration << " us\n";
    }


    template <class ET> class UCCache {
    private:
        struct UCCacheEntry {
            std::shared_ptr<ET> payload = nullptr;
            std::chrono::steady_clock::time_point lastAccess;
        };

        std::unordered_map<std::string, UCCacheEntry> cache;
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
                currentCacheSize -= oldest->second.payload->GetDataSize();
                cache.erase(oldest);
            }
        }
    public:
        UCCache(size_t maxCSize) : maxCacheSize(maxCSize) {}

        void AddToCache(const std::string& key, std::shared_ptr<ET> p) {
            if (!p) return;

            std::lock_guard<std::mutex> lock(cacheMutex);
            size_t dataSize = p->GetDataSize();

            // Check if we need to make room
            while (currentCacheSize + dataSize > maxCacheSize && !cache.empty()) {
                RemoveOldestCacheEntry();
            }

            UCCacheEntry entry;
            entry.lastAccess = std::chrono::steady_clock::now();
            entry.payload = p;
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

        void SetMaxCacheSize(size_t size) { maxCacheSize = size; }
    };

}