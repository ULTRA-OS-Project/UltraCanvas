// libspecific/Cairo/ImageCairo.cpp
// Cross-platform image loader implementation using PIMPL idiom
// Version: 2.0.0
// Last Modified: 2025-10-24
// Author: UltraCanvas Framework

#include "UltraCanvasImage.h"
#include "UltraCanvasUtils.h"
#include "ImageCairo.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <mutex>
#include <memory>
#include <iostream>
#include <unordered_map>

namespace UltraCanvas {
    typedef UCCache<UCPixmapCairo> UCPixmapsCache;
    UCPixmapsCache g_PixmapsCache(50 * 1024 * 1024);

    typedef UCCache<UCImageVips> UCImagesCache;
    UCImagesCache g_ImagesCache(50 * 1024 * 1024);


    UCPixmapCairo::UCPixmapCairo(cairo_surface_t *surf) {
        surface = surf;
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            std::cerr << "UCPixmapCairo: Invalid surface" << std::endl;
            return;
        }
        pixelsPtr = (uint32_t *)cairo_image_surface_get_data(surface);
        width = cairo_image_surface_get_width(surf);
        height = cairo_image_surface_get_height(surf);
    }

    UCPixmapCairo::UCPixmapCairo(int w, int h) {
        Init(w, h);
    }

    UCPixmapCairo::~UCPixmapCairo() {
        if (surface) {
            cairo_surface_destroy(surface);
        }
    }

    bool UCPixmapCairo::Init(int w, int h) {
        if (pixelsPtr && w == width && h == height) {
            Clear();
            return true;
        }

        width = w;
        height = h;
        if (surface) {
            cairo_surface_destroy(surface);
            surface = nullptr;
        }
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            std::cerr << "UCPixmapCairo: Cant create surface" << std::endl;
            return false;
        }
        pixelsPtr = (uint32_t *)cairo_image_surface_get_data(surface);
        return true;
    }

    void UCPixmapCairo::SetPixel(int x, int y, uint32_t pixel) {
        if (pixelsPtr && x >= 0 && x < width && y >= 0 && y < height) {
            pixelsPtr[y * width + x] = pixel;
        }
    }

    uint32_t UCPixmapCairo::GetPixel(int x, int y) const {
        if (pixelsPtr && x >= 0 && x < width && y >= 0 && y < height) {
            return pixelsPtr[y * width + x];
        }
        return 0;
    }
    void UCPixmapCairo::Clear() {
        if (pixelsPtr) {
            memset(pixelsPtr, 0, width * height * 4);
            cairo_surface_mark_dirty(surface);
        }
    }

    void UCPixmapCairo::Flush() {
        cairo_surface_flush(surface);
    }

    void UCPixmapCairo::MarkDirty() {
        if (pixelsPtr) {
            cairo_surface_mark_dirty(surface);
        }
    }

    uint32_t* UCPixmapCairo::GetPixelData() {
        return pixelsPtr;
    }

    size_t UCPixmapCairo::GetDataSize() {
        return width * height * 4;
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

    std::shared_ptr<UCImageVips> UCImageVips::GetFromMemory(const uint8_t* data, size_t dataSize, const std::string& formatHint) {
        char filename[200];
        snprintf(filename, sizeof(filename), ":mem:%p:%d", data, dataSize);
        std::shared_ptr<UCImageVips> im = g_ImagesCache.GetFromCache(filename);
        if (!im) {
            im = UCImageVips::LoadFromMemory(data, dataSize, formatHint);
            if (im->IsValid()) {
                g_ImagesCache.AddToCache(filename, im);
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

// With these two functions:
    std::shared_ptr<UCImageVips> UCImageVips::LoadFromMemory(const uint8_t* data, size_t dataSize, const std::string& formatHint) {
        char filename[200];
        snprintf(filename, sizeof(filename), ":mem:%p:%d", data, dataSize);
        auto result = std::make_shared<UCImageVips>(filename);

        if (!data || dataSize == 0) {
            result->errorMessage = "Invalid data: null pointer or zero size";
            return result;
        }

        try {
            // Create VImage from memory buffer
            // formatHint can be empty for auto-detection, or specify format like "png", "jpeg", etc.
            vips::VImage vipsImage;
            if (formatHint.empty()) {
                vipsImage = vips::VImage::new_from_buffer(data, dataSize, "");
            } else {
                vipsImage = vips::VImage::new_from_buffer(data, dataSize, "",
                                                          vips::VImage::option()->set("loader", formatHint.c_str()));
            }

            result->width = vipsImage.width();
            result->height = vipsImage.height();
            result->vImage = vipsImage;
        } catch (vips::VError& err) {
            std::cerr << "UCImageVips::LoadFromMemory: Failed to load image from buffer. Err:" << err.what() << std::endl;
            result->errorMessage = std::string("Failed to load image from memory buffer. Err:") + err.what();
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
            if (vImage.get_image()) {
                return CreatePixmapFromVImage(vImage);
            }

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
            return CreatePixmapFromVImage(vips::VImage::thumbnail(fileName.c_str(), w, options));
        } catch (vips::VError& err) {
            std::cerr << "UCImage::CreatePixmap: Failed to make pixmap for " << fileName << " Err:" << err.what() << std::endl;
            errorMessage = std::string("Failed to make pixmap Err:") + err.what();
        }
        return nullptr;
    }

    void rgba2bgra_premultiplied(uint32_t *src, uint32_t *dst, int n) {
        for (int x = 0; x < n; x++) {
            uint32_t rgba = GUINT32_FROM_BE(src[x]);
            uint8_t a = rgba & 0xff;

            uint32_t bgra;

            if (a == 0)
                bgra = 0;
            else if (a == 255)
                bgra = (rgba & 0x00ff00ff) |
                       (rgba & 0x0000ff00) << 16 |
                       (rgba & 0xff000000) >> 16;
            else {
                int r = (rgba >> 24) & 0xff;
                int g = (rgba >> 16) & 0xff;
                int b = (rgba >> 8) & 0xff;

                r = ((r * a) + 128) >> 8;
                g = ((g * a) + 128) >> 8;
                b = ((b * a) + 128) >> 8;

                bgra = (b << 24) | (g << 16) | (r << 8) | a;
            }

            dst[x] = GUINT32_TO_BE(bgra);
        }
    }

    std::shared_ptr<UCPixmapCairo> CreatePixmapFromVImage(vips::VImage vipsImage) {
        // Ensure 3-band RGB (handles grayscale)
        if (vipsImage.bands() < 3) {
            vipsImage = vipsImage.colourspace(VIPS_INTERPRETATION_sRGB);
        }

        vipsImage = vipsImage.cast(VIPS_FORMAT_UCHAR);

        // Add alpha channel if missing
        if (vipsImage.bands() == 3) {
            vipsImage = vipsImage.bandjoin(255);
        } else {
            if (vipsImage.bands() > 4) {
                vipsImage = vipsImage.extract_band(0, vips::VImage::option()->set("n", 4));
            }
        }

        int w = vipsImage.width();
        int h = vipsImage.height();

        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w,h);
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            throw UCImageError("Failed to create Cairo surface");
        }
        uint32_t *src = (uint32_t*)vipsImage.data();
        uint32_t *dst = (uint32_t*)cairo_image_surface_get_data(surface);
        if (!dst) {
            throw UCImageError("Failed to get surface data");
        }

        rgba2bgra_premultiplied(src, dst, w * h);

        cairo_surface_mark_dirty(surface);

        return std::make_shared<UCPixmapCairo>(surface);
    }

}
