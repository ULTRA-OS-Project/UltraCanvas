// OS/Linux/UltraCanvasLinuxImage.h
// cross-platform image handling in UltraCanvas.
// !! NEVER INCLUDE THIS FILE DIRECTLY, ALWAYS INCLUDE UltraCanvasImage.h !!
// Version: 1.0.0
// Last Modified: 2025-10-24
// Author: UltraCanvas Framework
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cairo/cairo.h>

#ifndef ULTRACANVASIMAGE_H
#include "UltraCanvasImage.h" // make CLion happy
#endif

namespace UltraCanvas {
    class UCLinuxImage : public UCBaseImage {
    private:
        cairo_surface_t* surface;
        bool ownsSurface;
    public:

        UCLinuxImage() : surface(nullptr), ownsSurface(false) {}

        // Destructor - clean up Cairo surface if owned
        virtual ~UCLinuxImage();

        // Set Cairo surface (takes ownership by default)
        void SetSurface(cairo_surface_t* surf, bool takeOwnership = true);

        // Get Cairo surface
        cairo_surface_t* GetSurface() const { return surface; };

        int GetDataSize() const override;

        // Optional overrides
        bool IsValid() const override;
        bool IsLoading() const override;

        std::shared_ptr<UCLinuxImage> Clone() const;

        //const char *GetFormatString() const override { return "Cairo Surface"; }
    };
}
