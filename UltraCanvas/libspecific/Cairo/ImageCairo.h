// libspecific/Cairo/ImageCairo.h
// Base interface for cross-platform image handling in UltraCanvas
// Version: 1.0.0
// Last Modified: 2025-10-24
// Author: UltraCanvas Framework
#pragma once
#ifndef IMAGECAIRO_H
#define IMAGECAIRO_H
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasImage.h"
#ifdef HAS_LIBVIPS
#include "PixelFX/PixelFX.h"
#endif
#include <string>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <cairo/cairo.h>
#ifdef HAS_LIBVIPS
#include <vips/vips8>
#endif
#undef Rect

namespace UltraCanvas {
// ===== CROSS-PLATFORM IMAGE CLASS =====
    class UCImageError : public std::runtime_error {
    public:
        UCImageError(const std::string& msg) : std::runtime_error(msg) {}
    };

    class UCPixmapCairo : public IPixmap {
        cairo_surface_t * surface = nullptr;
        uint32_t* pixelsPtr = nullptr;
        // Raw pixel dimensions of the underlying Cairo surface buffer.
        // SetPixel/GetPixel/Clear and any direct buffer access use these.
        int width = 0;
        int height = 0;
        // 1.0 unless SetDeviceScale was called. When > 1 the surface holds
        // raw pixels for HiDPI but presents itself at logical size = raw / scale.
        double deviceScale = 1.0;
    public:
        UCPixmapCairo() = default;
        explicit UCPixmapCairo(int w, int h);
        explicit UCPixmapCairo(cairo_surface_t * surf);
        ~UCPixmapCairo();

        bool Init(int w, int h) override;
        cairo_surface_t * GetSurface() const { return surface; };
        void SetPixel(int x, int y, uint32_t pixel) override;
        uint32_t GetPixel(int x, int y) const override;
        // Logical dimensions presented to drawing code. These are what
        // DrawPixmap should use for layout math; the source pattern's
        // device_scale handles the raw-to-logical mapping.
        int GetWidth() const override {
            return deviceScale > 0.0 ? static_cast<int>(width / deviceScale) : width;
        }
        int GetHeight() const override {
            return deviceScale > 0.0 ? static_cast<int>(height / deviceScale) : height;
        }
        // Raw pixel buffer dimensions (e.g. for direct memcpy/byte-level
        // operations that need the actual stride/footprint).
        int GetRawWidth() const { return width; }
        int GetRawHeight() const { return height; }
        uint32_t* GetPixelData() override;
        bool IsValid() const override { return pixelsPtr != nullptr; }
        void Flush() override;
        void MarkDirty() override;
        void Clear() override;

        // Tag this pixmap as HiDPI: pixels were rasterized at scale × the
        // intended logical size. Surface gets cairo_surface_set_device_scale
        // so DrawPixmap composes correctly; GetWidth/GetHeight start
        // returning the logical size.
        void SetDeviceScale(double scale);
        double GetDeviceScale() const { return deviceScale; }
    };

    class UCImageRaster {
    private:
        int width = 0;
        int height = 0;
        uint8_t *imgDataPtr = nullptr;
        size_t imgDataSize = 0;
        bool ownData = false;
        std::string fileName;

        bool LoadFileToMemory(const std::string &imagePath);

    public:
        std::string errorMessage;

        // ===== CONSTRUCTORS =====
        UCImageRaster() {};
        UCImageRaster(const std::string& fn) : fileName(fn) {};
        ~UCImageRaster();

        static std::shared_ptr<UCImageRaster> Get(const std::string &path);
        static std::shared_ptr<UCImageRaster> Load(const std::string &path, bool loadOnlyHeader = true);
        static std::shared_ptr<UCImageRaster> LoadFromMemory(const uint8_t* data, size_t dataSize);
        static std::shared_ptr<UCImageRaster> LoadFromMemory(const std::vector<uint8_t>& data) {
            return LoadFromMemory(data.data(), data.size());
        };
        static std::shared_ptr<UCImageRaster> GetFromMemory(const uint8_t* data, size_t dataSize);

        std::string Save(const std::string &imagePath, const UCImageSave::ImageExportOptions& options);

        // `scale` is the device pixel ratio of the destination render context
        // (1.0 = standard, 2.0 = Retina). When > 1.0 the pixmap is rasterized
        // at `width*scale × height*scale` raw pixels and tagged with
        // cairo_surface_set_device_scale() so it presents the requested
        // logical `width × height` to consumers. This keeps SVG icons crisp
        // on HiDPI displays without changing draw call sites.
        std::shared_ptr<UCPixmapCairo> GetPixmap(int width = 0, int height = 0,
                                                 ImageFitMode fitMode = ImageFitMode::Contain,
                                                 float scale = 1.0f);
        std::shared_ptr<UCPixmapCairo> CreatePixmap(int width, int height,
                                                    ImageFitMode fitMode = ImageFitMode::Contain,
                                                    float scale = 1.0f);
        std::string MakePixmapCacheKey(int w, int h, ImageFitMode fitMode, float scale);

        // Get aspect ratio
        float GetAspectRatio() const {
            if (height == 0) return 1.0f;
            return static_cast<float>(width) / static_cast<float>(height);
        }
        int GetWidth() const { return width; }
        int GetHeight() const { return height; }

#ifdef HAS_LIBVIPS
        vips::VImage GetVImage();
#endif

        size_t GetDataSize() {
            return sizeof(UCImageRaster) + 250 + imgDataSize;
        }
        bool IsValid() { return !fileName.empty() && errorMessage.empty() && width > 0;};


        static bool InitializeImageSubsysterm(const char* programName);
        static void ShutdownImageSubsysterm();
    };

#ifdef HAS_LIBVIPS
    std::shared_ptr<UCPixmapCairo> CreatePixmapFromVImage(vips::VImage vipsImage);
    std::string ExportVImage(vips::VImage vImg, const std::string &imagePath, const UCImageSave::ImageExportOptions& opts);
#endif
}
#endif