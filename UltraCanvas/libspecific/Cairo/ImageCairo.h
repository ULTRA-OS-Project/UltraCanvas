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
#include <string>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <cairo/cairo.h>
#include <vips/vips8>
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
        int width = 0;
        int height = 0;
    public:
        UCPixmapCairo() = default;
        explicit UCPixmapCairo(int w, int h);
        explicit UCPixmapCairo(cairo_surface_t * surf);
        ~UCPixmapCairo();

        bool Init(int w, int h) override;
        cairo_surface_t * GetSurface() const { return surface; };
        void SetPixel(int x, int y, uint32_t pixel) override;
        uint32_t GetPixel(int x, int y) const override;
        int GetWidth() const override { return width; };
        int GetHeight() const override { return height; };
        uint32_t* GetPixelData() override;
        bool IsValid() const override { return pixelsPtr != nullptr; }
        void Flush() override;
        void MarkDirty() override;
        void Clear() override;
        size_t GetDataSize();
    };


    class UCImageVips {
    private:
        int width = 0;
        int height = 0;
        uint8_t *imgDataPtr = nullptr;
        size_t imgDataSize = 0;
        std::string fileName;
        std::string formatHint;

    public:
        std::string errorMessage;

        // ===== CONSTRUCTORS =====
        UCImageVips() {};
        UCImageVips(const std::string& fn) : fileName(fn) {};
        ~UCImageVips();

        static std::shared_ptr<UCImageVips> Get(const std::string &path);
        static std::shared_ptr<UCImageVips> Load(const std::string &path, bool loadOnlyHeader = true);
        static std::shared_ptr<UCImageVips> LoadFromMemory(const uint8_t* data, size_t dataSize, const std::string& formatHint = "");
        static std::shared_ptr<UCImageVips> LoadFromMemory(const std::vector<uint8_t>& data, const std::string& formatHint = "") {
            return LoadFromMemory(data.data(), data.size(), formatHint);
        };
        static std::shared_ptr<UCImageVips> GetFromMemory(const uint8_t* data, size_t dataSize, const std::string& formatHint = "");

        bool Save(const std::string &path) { return false; };

        std::shared_ptr<UCPixmapCairo> GetPixmap(int width = 0, int height = 0, ImageFitMode fitMode = ImageFitMode::Contain);
        std::shared_ptr<UCPixmapCairo> CreatePixmap(int width, int height, ImageFitMode fitMode = ImageFitMode::Contain);
        std::string MakePixmapCacheKey(int w, int h, ImageFitMode fitMode = ImageFitMode::Contain);

        // Get aspect ratio
        float GetAspectRatio() const {
            if (height == 0) return 1.0f;
            return static_cast<float>(width) / static_cast<float>(height);
        }
        int GetWidth() const { return width; }
        int GetHeight() const { return height; }

        size_t GetDataSize() {
            return sizeof(UCImageVips) + 250 + imgDataSize;
        }
        bool IsValid() { return !fileName.empty() && errorMessage.empty() && width > 0;};
    };

    std::shared_ptr<UCPixmapCairo> CreatePixmapFromVImage(vips::VImage vipsImage);
}
#endif