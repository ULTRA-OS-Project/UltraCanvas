// include/UltraCanvasImageLoader.h
// Cross-platform image loader using PIMPL idiom
// Version: 2.0.0
// Last Modified: 2025-10-24
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVASIMAGELOADER_H
#define ULTRACANVASIMAGELOADER_H

#include "UltraCanvasImage.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>

namespace UltraCanvas {
// ===== IMAGE LOADER CLASS WITH PIMPL =====
    class UltraCanvasImageLoaderBase {

    struct CacheEntry {
        std::shared_ptr<UCImage> image = nullptr;
        size_t size = 0;
        std::chrono::steady_clock::time_point lastAccess;
    };
    protected:
        std::unordered_map<std::string, CacheEntry> imageCache;
        std::mutex cacheMutex;
        size_t maxCacheSize = 50 * 1024 * 1024;
        size_t currentCacheSize = 0;
        bool cachingEnabled = true;

    public:
        // Load image from file path
        std::shared_ptr<UCImage> GetFromFile(const std::string& filePath);

        virtual std::shared_ptr<UCImage> LoadFromFile(const std::string& filePath) = 0;

//        // Load image from memory buffer
//        std::shared_ptr<UCImage> LoadFromMemory(
//                const uint8_t* data,
//                size_t size,
//                UCImageFormat format = UCImageFormat::Auto);
//
//        // Load image from memory vector
//        static std::unique_ptr<UCImage> LoadFromMemory(
//                const std::vector<uint8_t>& data,
//                UCImageFormat format = UCImageFormat::Auto);
//
//        // ===== SAVING FUNCTIONS =====
//
//        // Save image to file
//        static bool SaveToFile(
//                const UCImage& image,
//                const std::string& filePath,
//                UCImageFormat format = UCImageFormat::Auto);
//
//        // Save image to memory buffer
//        static std::vector<uint8_t> SaveToMemory(
//                const UCImage& image,
//                UCImageFormat format);
//
//        // ===== UTILITY FUNCTIONS =====
//
//        // Get list of supported image formats for loading
//        static std::vector<std::string> GetSupportedLoadFormats();
//
//        // Get list of supported image formats for saving
//        static std::vector<std::string> GetSupportedSaveFormats();
//
//        // Check if a format is supported for loading
//        static bool IsLoadFormatSupported(const std::string& extension);
//
//        // Check if a format is supported for saving
//        static bool IsSaveFormatSupported(const std::string& extension);
//
//        // Detect format from file extension
//        static UCImageFormat DetectFormatFromPath(const std::string& filePath);
//
//        // Detect format from memory buffer (magic bytes)
//        static UCImageFormat DetectFormatFromMemory(const uint8_t* data, size_t size);

        // ===== CACHE MANAGEMENT =====
        std::shared_ptr<UCImage> GetFromCache(const std::string& key);
        void AddToCache(const std::string& key, std::shared_ptr<UCImage> image);
        // ===== CACHE MANAGEMENT =====
        void ClearCache();
        size_t GetCacheSize() const;
        void SetMaxCacheSize(size_t size) { maxCacheSize = size; }
        void EnableCaching(bool enable) { cachingEnabled = enable; }
        bool IsCachingEnabled() const { return cachingEnabled; };

//        // ===== BATCH OPERATIONS =====
//
//        // Load multiple images
//        static std::vector<std::unique_ptr<UCImage>> LoadMultiple(
//                const std::vector<std::string>& filePaths);
//
//        // Load images from directory
//        static std::vector<std::unique_ptr<UCImage>> LoadFromDirectory(
//                const std::string& directoryPath,
//                bool recursive = false);
//
//        // ===== ERROR HANDLING =====
//
//        // Get last error message
//        static std::string GetLastError();
//
//        // Clear last error
//        static void ClearLastError();
//
//        // ===== CONFIGURATION =====
//
//        // Set JPEG quality (0-100)
//        static void SetJpegQuality(int quality);
//
//        // Set PNG compression level (0-9)
//        static void SetPngCompressionLevel(int level);
//
//        // Enable/disable auto-rotation based on EXIF
//        static void EnableAutoRotation(bool enable);
    protected:
        void RemoveOldestCacheEntry();
    };

// ===== CONVENIENCE FUNCTIONS =====

// Quick load from file
//
//// Quick load from memory
//    inline std::unique_ptr<UCImage> LoadImageFromMemory(const uint8_t* data, size_t size) {
//        return UltraCanvasImageLoader::LoadFromMemory(data, size);
//    }
//
//// Quick save to file
//    inline bool SaveImage(const UCImage& image, const std::string& filePath) {
//        return UltraCanvasImageLoader::SaveToFile(image, filePath);
//    }
} // namespace UltraCanvas

#ifdef __linux__
#include "../OS/Linux/UltraCanvasLinuxImageLoader.h"
namespace UltraCanvas { using UltraCanvasImageLoader = UltraCanvas::UltraCanvasLinuxImageLoader; }
#elif defined(_WIN32) || defined(_WIN64)
#endif


inline std::shared_ptr<UltraCanvas::UCImage> GetImageFromFile(const std::string &filePath) {
    return UltraCanvas::UltraCanvasImageLoader::instance.GetFromFile(filePath);
}

inline std::shared_ptr<UltraCanvas::UCImage> LoadImageFromFile(const std::string &filePath) {
    return UltraCanvas::UltraCanvasImageLoader::instance.LoadFromFile(filePath);
}
#endif