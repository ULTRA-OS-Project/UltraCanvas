// core/ImageCairo.cpp
// Cross-platform image loader implementation using PIMPL idiom
// Version: 2.0.0
// Last Modified: 2025-10-24
// Author: UltraCanvas Framework

#include "UltraCanvasImage.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <mutex>
#include <memory>
#include <iostream>
#include <unordered_map>

namespace UltraCanvas {
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
    typedef UCCache<UCPixmapCairo> UCPixmapsCache;
    UCPixmapsCache g_PixmapsCache(50 * 1024 * 1024);

    typedef UCCache<UCImageVips> UCImagesCache;
    UCImagesCache g_ImagesCache(50 * 1024 * 1024);


    UCPixmapCairo::~UCPixmapCairo() {
        if (surface) {
            cairo_surface_destroy(surface);
        }
        surface = nullptr;
    }

    int UCPixmapCairo::GetWidth() {
        return cairo_image_surface_get_width(surface);
    }

    int UCPixmapCairo::GetHeight() {
        return cairo_image_surface_get_height(surface);
    }

    size_t UCPixmapCairo::GetDataSize() {
        return cairo_image_surface_get_stride(surface) * cairo_image_surface_get_height(surface);
    }


    std::shared_ptr<UCImageVips> UCImageVips::Get(const std::string &imagePath) {
        std::shared_ptr<UCImageVips> im = g_ImagesCache.GetFromCache(imagePath);
        if (!im) {
            im = UCImageVips::Load(imagePath);
            if (im->IsValid()) {
                g_ImagesCache.AddToCache(imagePath, im);
            }
        }
        return im;
    }

    std::shared_ptr<UCImageVips> UCImageVips::Load(const std::string &imagePath) {
        auto result = std::make_shared<UCImageVips>(imagePath);
        try {
            vips::VImage vipsImage = vips::VImage::new_from_file(imagePath.c_str());
            result->width = vipsImage.width();
            result->height = vipsImage.height();
        } catch (vips::VError& err) {
            std::cerr << "UCImage::Load: Failed Failed to load image for " << imagePath << " Err:" << err.what() << std::endl;
            result->errorMessage = std::string("Failed to load image ") + imagePath + " Err:" + err.what();
        }

        return result;
    }

    std::string UCImageVips::MakePixmapCacheKey(int w, int h, ImageFitMode fitMode) {
        char key[300];
        snprintf(key, sizeof(key) - 1, "%s?w:%dh:%dc:%d", fileName.c_str(), w, h, static_cast<int>(fitMode));
        return std::string(key);
    }

    std::shared_ptr<UCPixmapCairo> UCImageVips::GetPixmap(int w, int h, ImageFitMode fitMode) {
        if (!errorMessage.empty() || fileName.empty()) {
            return nullptr;
        }
        if (!w || !h) {
            w = width;
            h = height;
        }
        std::string key = MakePixmapCacheKey(w, h, fitMode);
        std::shared_ptr<UCPixmapCairo> pm = g_PixmapsCache.GetFromCache(key);
        if (!pm) {
            pm = CreatePixmap(width, height, fitMode);
            if (pm) {
                g_PixmapsCache.AddToCache(key, pm);
            }
        }
        return pm;
    }

    std::shared_ptr<UCPixmapCairo> UCImageVips::CreatePixmap(int w, int h, ImageFitMode fitMode) {
        if (!errorMessage.empty() || fileName.empty()) {
            return nullptr;
        }
        try {
            auto options = vips::VImage::option();
            switch (fitMode) {
                case ImageFitMode::Fill:
                    options = options->set("height", h)->set("size", VipsSize::VIPS_SIZE_FORCE);
                    break;
                case ImageFitMode::Contain:
                    options = options->set("height", h);
                    break;
                case ImageFitMode::Cover:
                    options = options->set("height", h)->set("crop", VipsInteresting::VIPS_INTERESTING_CENTRE);
                    break;
                case ImageFitMode::ScaleDown:
                    options = options->set("height", h)->set("size", VipsSize::VIPS_SIZE_DOWN);
                    break;
                case ImageFitMode::NoScale:
                    w = width;
                    break;
            }
            auto vipsImage = vips::VImage::thumbnail(fileName.c_str(), w, options);

            // Ensure 3-band RGB (handles grayscale)
            if (vipsImage.bands() < 3) {
                vipsImage = vipsImage.colourspace(VIPS_INTERPRETATION_sRGB);
            }

            // Add alpha channel if missing
            if (!vipsImage.has_alpha()) {
                vipsImage = vipsImage.bandjoin(255);
            } else {
                // 4. Premultiply alpha (SIMD optimized in libvips)
                vipsImage = vipsImage.premultiply();
            }

            // Reorder RGBA -> BGRA for Cairo ARGB32 (little-endian)
            // Single operation using band indexing
            vipsImage = vips::VImage::bandjoin({vipsImage[2], vipsImage[1], vipsImage[0], vipsImage[3]});

            // 6. Cast to uint8 (premultiply outputs float)
            vipsImage = vipsImage.cast(VIPS_FORMAT_UCHAR);

            int w = vipsImage.width();
            int h = vipsImage.height();
            int cairoStride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
            int vipsRowBytes = width * 4;

            // Execute pipeline and write to memory (single pass)
            size_t vipsSize;
            size_t cairoSize = cairoStride * height;
            void* vipsBuffer = vipsImage.write_to_memory(&vipsSize);
            if (!vipsBuffer) {
                errorMessage = "Failed to write image to buffer";
                std::cerr << "UCImage::CreatePixmap: Failed to write image to buffer. " << fileName << std::endl;
                return nullptr;
            }
            if (cairoSize != vipsSize) {
                errorMessage = "Cairo surface buffer size != vips image buffer size";
                std::cerr << "UCImage::CreatePixmap: Cairo buffer size != vips image size. " << fileName << std::endl;
                g_free(vipsBuffer);
                return nullptr;
            }

            unsigned char* pixelData = nullptr;
            if (cairoStride == vipsRowBytes) {
                // Strides match - use vips buffer directly
                pixelData = static_cast<unsigned char*>(vipsBuffer);
            } else {
                // Stride mismatch - copy with padding
                pixelData = static_cast<unsigned char*>(g_malloc(cairoStride * height));

                for (int y = 0; y < height; ++y) {
                    memcpy(
                            pixelData + y * cairoStride,
                            static_cast<char*>(vipsBuffer) + y * vipsRowBytes,
                            vipsRowBytes
                    );
                }
                g_free(vipsBuffer);
            }

            cairo_surface_t* surface = cairo_image_surface_create_for_data(
                    pixelData,
                    CAIRO_FORMAT_ARGB32,
                    width,
                    height,
                    cairoStride
            );
            if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
                std::cerr << "UCImage::CreatePixmap: Failed to create Cairo surface. " << fileName << std::endl;
                g_free(pixelData);
                errorMessage = "Failed to create Cairo surface";
                return nullptr;
            }
            return std::make_shared<UCPixmapCairo>(surface);
        } catch (vips::VError& err) {
            std::cerr << "UCImage::CreatePixmap: Failed to make pixmap for " << fileName << " Err:" << err.what() << std::endl;
            errorMessage = std::string("Failed to make pixmap Err:") + err.what();
        }
        return nullptr;
    }

}
