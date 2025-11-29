// include/UltraCanvasImage.h
// Base interface for cross-platform image handling in UltraCanvas
// Version: 1.0.0
// Last Modified: 2025-10-24
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASIMAGE_H
#define ULTRACANVASIMAGE_H
#include <cstdint>

class IPixmap {
public:
    virtual ~IPixmap() = default;
    virtual bool Init(int w, int h) = 0;
    virtual void Clear() = 0;
    virtual void Flush() = 0;
    virtual bool IsValid() const = 0;
    virtual uint32_t* GetPixelData() = 0;
    virtual void SetPixel(int x, int y, uint32_t pixel) = 0;
    virtual uint32_t GetPixel(int x, int y) const = 0;
    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
    virtual void MarkDirty() = 0;
};


#include "../libspecific/Cairo/ImageCairo.h"
namespace UltraCanvas {
    using UCPixmap = UltraCanvas::UCPixmapCairo;
    using UCImage = UltraCanvas::UCImageVips;

    typedef std::shared_ptr<UCImage> UCImagePtr;
    typedef std::shared_ptr<UCPixmap> UCPixmapPtr;
}
#endif