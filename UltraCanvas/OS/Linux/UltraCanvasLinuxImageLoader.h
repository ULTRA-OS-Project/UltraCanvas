// OS/Linux/UltraCanvasLinuxImageLoader.h
// Linux image loading utilities for PNG and JPG support with caching
// Version: 1.0.0
// Last Modified: 2025-01-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <cairo/cairo.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>

namespace UltraCanvas {

// ===== IMAGE CACHE ENTRY =====
struct CachedImage {
    cairo_surface_t* surface;
    int width;
    int height;
    std::chrono::steady_clock::time_point lastAccessed;
    size_t memorySize;
    
    CachedImage() : surface(nullptr), width(0), height(0), memorySize(0) {}
    
    ~CachedImage() {
        if (surface) {
            cairo_surface_destroy(surface);
        }
    }
};

// ===== IMAGE LOADING RESULT =====
struct ImageLoadResult {
    bool success;
    std::string errorMessage;
    cairo_surface_t* surface;
    int width;
    int height;
    
    ImageLoadResult() : success(false), surface(nullptr), width(0), height(0) {}
};

// ===== LINUX IMAGE LOADER CLASS =====
class LinuxImageLoader {
private:
    static std::unordered_map<std::string, std::shared_ptr<CachedImage>> imageCache;
    static std::mutex cacheMutex;
    static size_t maxCacheSize;
    static size_t currentCacheSize;
    
    // Internal loading functions
    static cairo_surface_t* LoadPngImage(const std::string& filePath, int& width, int& height);
    static cairo_surface_t* LoadJpegImage(const std::string& filePath, int& width, int& height);
    static cairo_surface_t* LoadPngFromMemory(const uint8_t* data, size_t size, int& width, int& height);
    static cairo_surface_t* LoadJpegFromMemory(const uint8_t* data, size_t size, int& width, int& height);
    
    // Cache management
    static void CleanupOldCacheEntries();
    static void AddToCache(const std::string& key, cairo_surface_t* surface, int width, int height);
    static std::shared_ptr<CachedImage> GetFromCache(const std::string& key);
    
    // Format detection
    static bool IsPngFile(const std::string& filePath);
    static bool IsJpegFile(const std::string& filePath);
    static bool IsPngData(const uint8_t* data, size_t size);
    static bool IsJpegData(const uint8_t* data, size_t size);

public:
    // ===== PUBLIC INTERFACE =====
    
    // Load image from file path
    static ImageLoadResult LoadImage(const std::string& imagePath);
    
    // Load image from memory
    static ImageLoadResult LoadImageFromMemory(const uint8_t* data, size_t size);
    
    // Cache management
    static void SetMaxCacheSize(size_t maxSize);
    static void ClearCache();
    static size_t GetCacheSize();
    static size_t GetCacheMemoryUsage();
    
    // Utility functions
    static std::vector<std::string> GetSupportedFormats();
    static bool IsFormatSupported(const std::string& extension);
    
    // Enable/disable caching
    static void EnableCaching(bool enable);
    static bool IsCachingEnabled();
    
private:
    static bool cachingEnabled;
};

// ===== INLINE HELPER FUNCTIONS =====
inline std::string GetFileExtension(const std::string& filePath) {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) return "";
    
    std::string ext = filePath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

inline std::string GenerateCacheKey(const std::string& filePath) {
    return filePath;
}

inline std::string GenerateCacheKey(const uint8_t* data, size_t size) {
    // Simple hash for memory-based images
    std::hash<std::string> hasher;
    std::string dataStr(reinterpret_cast<const char*>(data), std::min(size, size_t(1024)));
    return "memory_" + std::to_string(hasher(dataStr)) + "_" + std::to_string(size);
}

} // namespace UltraCanvas