// include/UltraCanvasImage.h
// Base interface for cross-platform image handling in UltraCanvas
// Version: 1.0.0
// Last Modified: 2025-10-24
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASIMAGE_H
#define ULTRACANVASIMAGE_H

#include "../libspecific/ImageCairo.h"
namespace UltraCanvas {
    using UCPixmap = UltraCanvas::UCPixmapCairo;
    using UCImage = UltraCanvas::UCImageVips;

    typedef std::shared_ptr<UCImage> UCImagePtr;
    typedef std::shared_ptr<UCPixmap> UCPixmapPtr;
}
#endif