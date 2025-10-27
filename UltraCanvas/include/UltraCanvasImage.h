// include/UltraCanvasImage.h
// Base interface for cross-platform image handling in UltraCanvas
// Version: 1.0.0
// Last Modified: 2025-10-24
// Author: UltraCanvas Framework
#pragma once
#include <string>
#ifndef ULTRACANVASIMAGE_H
#define ULTRACANVASIMAGE_H

namespace UltraCanvas {
// ===== CROSS-PLATFORM IMAGE CLASS =====
class UCBaseImage {
    public:
        std::string errorMessage;
        int width = 0;
        int height = 0;

        // ===== CONSTRUCTORS =====
        UCBaseImage() = default;
        virtual ~UCBaseImage() = default;

        // ===== CROSS-PLATFORM METHODS =====

        // Get source path if loaded from file
//        const std::string& GetSourcePath() const;
//        void SetSourcePath(const std::string& path);
//
//        // Check if image was loaded from memory
//        bool IsFromMemory() const { return isFromMemory; }
//        void SetFromMemory(bool fromMem) { isFromMemory = fromMem; }

        // Get memory buffer if available
//        const std::vector<uint8_t>& GetMemoryBuffer() const { return memoryBuffer; }
//        void SetMemoryBuffer(const std::vector<uint8_t>& buffer) { memoryBuffer = buffer; }

        // Cross-platform image information
//        bool IsLoaded() const { return IsValid(); }

        // Get image dimensions as Size2D
//        Size2D GetSize() const { return Size2D(GetWidth(), GetHeight()); }
//
//        // Get image bounds as Rect2D
//        Rect2D GetBounds() const { return Rect2D(0, 0, GetWidth(), GetHeight()); }

        // ===== STATIC FACTORY METHODS =====

        // Create empty image
//        static std::shared_ptr<UCImage> Create() {
//            return std::make_shared<UCImage>();
//        }

        // Create from platform-specific image
//        static std::shared_ptr<UCImage> CreateFromPlatform(const UCPlatformImage& platformImage) {
//            return std::make_shared<UCImage>(platformImage);
//        }

        // Check if image is valid and loaded
        virtual bool IsLoading() const = 0;
        virtual bool IsValid() const = 0;
        virtual int GetDataSize() const = 0;


        // ===== UTILITY METHODS =====

        // Get aspect ratio
        float GetAspectRatio() const {
            if (height == 0) return 1.0f;
            return static_cast<float>(width) / static_cast<float>(height);
        }

        // Check if image has alpha channel
//        bool HasAlpha() const;
//        bool HasAlpha() const {
//            return GetChannels() == 4;
//        }

        // Get total pixel count
//        size_t GetPixelCount() const {
//            return static_cast<size_t>(GetWidth()) * static_cast<size_t>(GetHeight());
//        }
//
//        // Clone image (creates a new instance with same data)
//        std::shared_ptr<UCImage> Clone() const {
//            auto cloned = std::make_shared<UCImage>(*this);
//            return cloned;
//        }
    };

}
#ifdef __linux__
    #include "../OS/Linux/UltraCanvasLinuxImage.h"
    namespace UltraCanvas { using UCImage = UltraCanvas::UCLinuxImage; }
#elif defined(_WIN32) || defined(_WIN64)
    #include "../OS/MSWindows/UltraCanvasWindowsImage.h"
    namespace UltraCanvas { using UCImage = UltraCanvas::UCWindowsImage; }
#endif
#endif