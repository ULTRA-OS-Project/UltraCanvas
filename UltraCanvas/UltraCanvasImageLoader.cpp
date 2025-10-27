// core/UltraCanvasImageLoader.cpp
// Cross-platform image loader implementation using PIMPL idiom
// Version: 2.0.0
// Last Modified: 2025-10-24
// Author: UltraCanvas Framework

#include "../include/UltraCanvasImageLoader.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <mutex>
#include <unordered_map>

namespace UltraCanvas {

// Forward declaration of platform-specific factory
    extern std::unique_ptr<UCImage::Impl> CreatePlatformImageImpl();

// ===== IMAGE LOADER IMPLEMENTATION CLASS (PIMPL) =====
    class UltraCanvasImageLoader::Impl {
    private:
        // Cache structure
        struct CacheEntry {
            std::unique_ptr<UCImage> image;
            size_t size;
            std::chrono::steady_clock::time_point lastAccess;
        };

        // Cache storage
        std::unordered_map<std::string, CacheEntry> imageCache;
        std::mutex cacheMutex;
        size_t maxCacheSize;
        size_t currentCacheSize;
        bool cachingEnabled;

        // Configuration
        int jpegQuality;
        int pngCompressionLevel;
        bool autoRotation;

        // Error handling
        std::string lastError;
        std::mutex errorMutex;

        // Initialization flag
        bool initialized;

    public:
        // Constructor
        Impl() : maxCacheSize(50 * 1024 * 1024), // 50MB default
                 currentCacheSize(0),
                 cachingEnabled(true),
                 jpegQuality(90),
                 pngCompressionLevel(6),
                 autoRotation(false),
                 initialized(false) {}

        // Destructor
        ~Impl() {
            ClearCache();
        }

        // ===== INITIALIZATION =====

        bool Initialize() {
            if (initialized) return true;

            // Platform-specific initialization would go here
            initialized = true;
            return true;
        }

        void Shutdown() {
            ClearCache();
            initialized = false;
        }

        bool IsInitialized() const {
            return initialized;
        }

        // ===== LOADING FUNCTIONS =====

        std::unique_ptr<UCImage> LoadFromFile(const std::string& filePath) {
            // Check cache first
            if (cachingEnabled) {
                auto cached = GetFromCache(filePath);
                if (cached) {
                    return cached;
                }
            }

            // Clear last error
            ClearLastError();

            // Platform-specific loading
            auto image = LoadFromFilePlatform(filePath);

            if (image && cachingEnabled) {
                AddToCache(filePath, image.get());
            }

            return image;
        }

        std::unique_ptr<UCImage> LoadFromMemory(
                const uint8_t* data,
                size_t size,
                UCImageFormat format) {

            if (!data || size == 0) {
                SetLastError("Invalid data: null pointer or zero size");
                return nullptr;
            }

            // Clear last error
            ClearLastError();

            // Auto-detect format if needed
            if (format == UCImageFormat::Auto) {
                format = DetectFormatFromMemory(data, size);
            }

            // Platform-specific loading
            return LoadFromMemoryPlatform(data, size, format);
        }

        // ===== PLATFORM-SPECIFIC LOADING (TO BE IMPLEMENTED) =====

        std::unique_ptr<UCImage> LoadFromFilePlatform(const std::string& filePath);
        std::unique_ptr<UCImage> LoadFromMemoryPlatform(
                const uint8_t* data, size_t size, UCImageFormat format);

        // ===== FORMAT DETECTION =====

        static UCImageFormat DetectFormatFromPath(const std::string& filePath) {
            size_t dotPos = filePath.find_last_of('.');
            if (dotPos == std::string::npos) {
                return UCImageFormat::Unknown;
            }

            std::string ext = filePath.substr(dotPos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == "png") return UCImageFormat::PNG;
            if (ext == "jpg" || ext == "jpeg") return UCImageFormat::JPEG;
            if (ext == "bmp") return UCImageFormat::BMP;
            if (ext == "gif") return UCImageFormat::GIF;
            if (ext == "tiff" || ext == "tif") return UCImageFormat::TIFF;
            if (ext == "webp") return UCImageFormat::WEBP;
            if (ext == "svg") return UCImageFormat::SVG;
            if (ext == "ico") return UCImageFormat::ICO;
            if (ext == "avif") return UCImageFormat::AVIF;

            return UCImageFormat::Unknown;
        }

        static UCImageFormat DetectFormatFromMemory(const uint8_t* data, size_t size) {
            if (!data || size < 8) {
                return UCImageFormat::Unknown;
            }

            // PNG signature
            if (size >= 8 && data[0] == 0x89 && data[1] == 0x50 &&
                data[2] == 0x4E && data[3] == 0x47) {
                return UCImageFormat::PNG;
            }

            // JPEG signature
            if (size >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
                return UCImageFormat::JPEG;
            }

            // BMP signature
            if (size >= 2 && data[0] == 0x42 && data[1] == 0x4D) {
                return UCImageFormat::BMP;
            }

            // GIF signature
            if (size >= 6 && data[0] == 0x47 && data[1] == 0x49 &&
                data[2] == 0x46 && data[3] == 0x38) {
                return UCImageFormat::GIF;
            }

            // WebP signature
            if (size >= 12 && data[0] == 0x52 && data[1] == 0x49 &&
                data[2] == 0x46 && data[3] == 0x46 &&
                data[8] == 0x57 && data[9] == 0x45 &&
                data[10] == 0x42 && data[11] == 0x50) {
                return UCImageFormat::WEBP;
            }

            return UCImageFormat::Unknown;
        }

        // ===== CACHE MANAGEMENT =====

        std::unique_ptr<UCImage> GetFromCache(const std::string& key) {
            std::lock_guard<std::mutex> lock(cacheMutex);

            auto it = imageCache.find(key);
            if (it != imageCache.end()) {
                it->second.lastAccess = std::chrono::steady_clock::now();
                // Clone the cached image
                return it->second.image->Clone();
            }

            return nullptr;
        }

        void AddToCache(const std::string& key, UCImage* image) {
            if (!image) return;

            std::lock_guard<std::mutex> lock(cacheMutex);

            size_t imageSize = image->GetDataSize();

            // Check if we need to make room
            while (currentCacheSize + imageSize > maxCacheSize && !imageCache.empty()) {
                RemoveOldestCacheEntry();
            }

            // Add to cache
            CacheEntry entry;
            entry.image = image->Clone();
            entry.size = imageSize;
            entry.lastAccess = std::chrono::steady_clock::now();

            imageCache[key] = std::move(entry);
            currentCacheSize += imageSize;
        }

        void RemoveOldestCacheEntry() {
            // Find oldest entry (no lock needed, called from locked context)
            auto oldest = imageCache.begin();
            for (auto it = imageCache.begin(); it != imageCache.end(); ++it) {
                if (it->second.lastAccess < oldest->second.lastAccess) {
                    oldest = it;
                }
            }

            if (oldest != imageCache.end()) {
                currentCacheSize -= oldest->second.size;
                imageCache.erase(oldest);
            }
        }

        void ClearCache() {
            std::lock_guard<std::mutex> lock(cacheMutex);
            imageCache.clear();
            currentCacheSize = 0;
        }

        size_t GetCacheSize() const {
            std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(cacheMutex));
            return currentCacheSize;
        }

        // ===== ERROR HANDLING =====

        void SetLastError(const std::string& error) {
            std::lock_guard<std::mutex> lock(errorMutex);
            lastError = error;
        }

        std::string GetLastError() const {
            std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(errorMutex));
            return lastError;
        }

        void ClearLastError() {
            std::lock_guard<std::mutex> lock(errorMutex);
            lastError.clear();
        }

        // ===== CONFIGURATION =====

        void SetMaxCacheSize(size_t size) { maxCacheSize = size; }
        void EnableCaching(bool enable) { cachingEnabled = enable; }
        bool IsCachingEnabled() const { return cachingEnabled; }
        void SetJpegQuality(int quality) { jpegQuality = std::clamp(quality, 0, 100); }
        void SetPngCompressionLevel(int level) { pngCompressionLevel = std::clamp(level, 0, 9); }
        void EnableAutoRotation(bool enable) { autoRotation = enable; }
    };

// ===== STATIC MEMBER INITIALIZATION =====
    std::unique_ptr<UltraCanvasImageLoader::Impl> UltraCanvasImageLoader::pImpl = nullptr;

// ===== PUBLIC INTERFACE IMPLEMENTATION =====

    bool UltraCanvasImageLoader::Initialize() {
        if (!pImpl) {
            pImpl = std::make_unique<Impl>();
        }
        return pImpl->Initialize();
    }

    void UltraCanvasImageLoader::Shutdown() {
        if (pImpl) {
            pImpl->Shutdown();
            pImpl.reset();
        }
    }

    bool UltraCanvasImageLoader::IsInitialized() {
        return pImpl && pImpl->IsInitialized();
    }

    std::unique_ptr<UCImage> UltraCanvasImageLoader::LoadFromFile(const std::string& filePath) {
        if (!pImpl) {
            Initialize();
        }
        return pImpl->LoadFromFile(filePath);
    }

    std::unique_ptr<UCImage> UltraCanvasImageLoader::LoadFromMemory(
            const uint8_t* data, size_t size, UCImageFormat format) {
        if (!pImpl) {
            Initialize();
        }
        return pImpl->LoadFromMemory(data, size, format);
    }

    std::unique_ptr<UCImage> UltraCanvasImageLoader::LoadFromMemory(
            const std::vector<uint8_t>& data, UCImageFormat format) {
        return LoadFromMemory(data.data(), data.size(), format);
    }

    UCImageFormat UltraCanvasImageLoader::DetectFormatFromPath(const std::string& filePath) {
        return Impl::DetectFormatFromPath(filePath);
    }

    UCImageFormat UltraCanvasImageLoader::DetectFormatFromMemory(const uint8_t* data, size_t size) {
        return Impl::DetectFormatFromMemory(data, size);
    }

    void UltraCanvasImageLoader::SetMaxCacheSize(size_t maxSize) {
        if (!pImpl) Initialize();
        pImpl->SetMaxCacheSize(maxSize);
    }

    size_t UltraCanvasImageLoader::GetCacheSize() {
        if (!pImpl) return 0;
        return pImpl->GetCacheSize();
    }

    void UltraCanvasImageLoader::ClearCache() {
        if (pImpl) {
            pImpl->ClearCache();
        }
    }

    void UltraCanvasImageLoader::EnableCaching(bool enable) {
        if (!pImpl) Initialize();
        pImpl->EnableCaching(enable);
    }

    bool UltraCanvasImageLoader::IsCachingEnabled() {
        if (!pImpl) return false;
        return pImpl->IsCachingEnabled();
    }

    std::string UltraCanvasImageLoader::GetLastError() {
        if (!pImpl) return "Image loader not initialized";
        return pImpl->GetLastError();
    }

    void UltraCanvasImageLoader::ClearLastError() {
        if (pImpl) {
            pImpl->ClearLastError();
        }
    }

    void UltraCanvasImageLoader::SetJpegQuality(int quality) {
        if (!pImpl) Initialize();
        pImpl->SetJpegQuality(quality);
    }

    void UltraCanvasImageLoader::SetPngCompressionLevel(int level) {
        if (!pImpl) Initialize();
        pImpl->SetPngCompressionLevel(level);
    }

    void UltraCanvasImageLoader::EnableAutoRotation(bool enable) {
        if (!pImpl) Initialize();
        pImpl->EnableAutoRotation(enable);
    }

// Placeholder implementations for functions that need platform-specific code
    bool UltraCanvasImageLoader::SaveToFile(
            const UCImage& image, const std::string& filePath, UCImageFormat format) {
        // To be implemented with platform-specific code
        return false;
    }

    std::vector<uint8_t> UltraCanvasImageLoader::SaveToMemory(
            const UCImage& image, UCImageFormat format) {
        // To be implemented with platform-specific code
        return std::vector<uint8_t>();
    }

    std::vector<std::string> UltraCanvasImageLoader::GetSupportedLoadFormats() {
        // Basic formats most platforms support
        return {"png", "jpg", "jpeg", "bmp", "gif"};
    }

    std::vector<std::string> UltraCanvasImageLoader::GetSupportedSaveFormats() {
        // Basic formats most platforms can save
        return {"png", "jpg", "jpeg", "bmp"};
    }

    bool UltraCanvasImageLoader::IsLoadFormatSupported(const std::string& extension) {
        auto formats = GetSupportedLoadFormats();
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return std::find(formats.begin(), formats.end(), ext) != formats.end();
    }

    bool UltraCanvasImageLoader::IsSaveFormatSupported(const std::string& extension) {
        auto formats = GetSupportedSaveFormats();
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return std::find(formats.begin(), formats.end(), ext) != formats.end();
    }

    std::vector<std::unique_ptr<UCImage>> UltraCanvasImageLoader::LoadMultiple(
            const std::vector<std::string>& filePaths) {

        std::vector<std::unique_ptr<UCImage>> images;
        images.reserve(filePaths.size());

        for (const auto& path : filePaths) {
            auto image = LoadFromFile(path);
            if (image) {
                images.push_back(std::move(image));
            }
        }

        return images;
    }

    std::vector<std::unique_ptr<UCImage>> UltraCanvasImageLoader::LoadFromDirectory(
            const std::string& directoryPath, bool recursive) {
        // To be implemented with platform-specific directory traversal
        return std::vector<std::unique_ptr<UCImage>>();
    }

} // namespace UltraCanvas