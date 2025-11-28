// support/ImageCairo.h
// Base interface for cross-platform image handling in UltraCanvas
// Version: 1.0.0
// Last Modified: 2025-10-24
// Author: UltraCanvas Framework
#pragma once
#ifndef IMAGECAIRO_H
#define IMAGECAIRO_H
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <unordered_map>
#include <vips/vips8>
#include <memory>
#include <chrono>
#include <cairo/cairo.h>

namespace UltraCanvas {
// ===== CROSS-PLATFORM IMAGE CLASS =====

class UCPixmapCairo {
    cairo_surface_t * surface;
public:
    explicit UCPixmapCairo(cairo_surface_t * surf) : surface(surf) {};
    ~UCPixmapCairo();

    [[nodiscard]] cairo_surface_t * GetSurface() const { return surface; };
    int GetWidth();
    int GetHeight();
    size_t GetDataSize();
};

class UCImageVips {
public:
    std::string errorMessage;
    std::string fileName;

    int width = 0;
    int height = 0;

    // ===== CONSTRUCTORS =====
    UCImageVips() {};
    UCImageVips(const std::string& fn) : fileName(fn) {};

    static std::shared_ptr<UCImageVips> Get(const std::string &path);
    static std::shared_ptr<UCImageVips> Load(const std::string &path);
    bool Save(const std::string &path) { return false; };

    std::shared_ptr<UCPixmapCairo> GetPixmap(int width = 0, int height = 0, ImageFitMode fitMode = ImageFitMode::Contain);
    std::shared_ptr<UCPixmapCairo> CreatePixmap(int width, int height, ImageFitMode fitMode = ImageFitMode::Contain);
    std::string MakePixmapCacheKey(int w, int h, ImageFitMode fitMode = ImageFitMode::Contain);

    // Get aspect ratio
    float GetAspectRatio() const {
        if (height == 0) return 1.0f;
        return static_cast<float>(width) / static_cast<float>(height);
    }

    size_t GetDataSize() {
        return sizeof(UCImageVips) + 250;
    }
    bool IsValid() { return !fileName.empty() && errorMessage.empty() && width > 0;};
};
}
#endif