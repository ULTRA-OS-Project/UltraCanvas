// include/UltraCanvasImageElement.h
// Image display component with loading, caching, and transformation support
// Version: 1.0.0
// Last Modified: 2024-12-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <fstream>
#include <iostream>

namespace UltraCanvas {

// ===== IMAGE FORMATS =====
enum class ImageFormat {
    Unknown,
    PNG,
    JPEG,
    JPG,
    BMP,
    GIF,
    TIFF,
    WEBP,
    SVG,
    ICO,
    AVIF
};

// ===== IMAGE SCALING MODES =====
enum class ImageScaleMode {
    NoScale,           // No scaling - original size
    Stretch,        // Stretch to fit bounds (may distort)
    Uniform,        // Scale uniformly to fit (maintain aspect ratio)
    UniformToFill,  // Scale uniformly to fill (may crop)
    Center,         // Center image without scaling
    Tile            // Tile image to fill bounds
};

// ===== IMAGE LOADING STATE =====
enum class ImageLoadState {
    NotLoaded,
    Loading,
    Loaded,
    Failed
};

// ===== IMAGE ELEMENT COMPONENT =====
class UltraCanvasImageElement : public UltraCanvasUIElement {
private:
    // Image source
    std::string imagePath;
    std::shared_ptr<UCImage> loadedImage;
    ImageLoadState loadState = ImageLoadState::NotLoaded;
    
    // Display properties
    ImageScaleMode scaleMode = ImageScaleMode::Uniform;
    Color tintColor = Colors::White;
    float opacity = 1.0f;
    bool smoothScaling = true;
    
    // Transform properties
    float rotation = 0.0f;
    Point2Df scale = Point2Df(1.0f, 1.0f);
    Point2Df offset = Point2Df(0.0f, 0.0f);
    
    // Interaction
    bool clickable = false;
    bool draggable = false;
    Point2Di dragStartPos;
    bool isDragging = false;
    
    // Error handling
    std::string errorMessage;
    bool showErrorPlaceholder = true;
    Color errorColor = Color(200, 200, 200);
    
    // Performance
    bool cacheEnabled = true;
    bool asyncLoading = false;
    
public:
    // ===== EVENTS =====
    std::function<void()> onImageLoaded;
    std::function<void(const std::string&)> onImageLoadFailed;
    std::function<void()> onClick;
    std::function<void(const Point2Di&)> onImageDragged;
    
    // ===== CONSTRUCTOR =====
    UltraCanvasImageElement(const std::string& identifier = "ImageElement", long id = 0,
                           long x = 0, long y = 0, long w = 100, long h = 100);

    // ===== IMAGE LOADING =====
    bool LoadFromFile(const std::string& filePath);
    
    bool LoadFromMemory(const std::vector<uint8_t>& data, ImageFormat format = ImageFormat::Unknown);
    
    bool LoadFromMemory(const uint8_t* data, size_t size, ImageFormat format = ImageFormat::Unknown);
    
    // ===== IMAGE PROPERTIES =====
    void SetScaleMode(ImageScaleMode mode) { scaleMode = mode; }
    
    ImageScaleMode GetScaleMode() const { return scaleMode; }
    void SetTintColor(const Color& color) { tintColor = color; }
    void SetOpacity(float alpha) { opacity = std::max(0.0f, std::min(1.0f, alpha)); RequestRedraw(); }
    float GetOpacity() const { return opacity; }
    void SetRotation(float degrees) { rotation = degrees; RequestRedraw(); }
    void SetScale(float sx, float sy) {
        scale.x = sx;
        scale.y = sy;
    }
    
    void SetOffset(float ox, float oy) {
        offset.x = ox;
        offset.y = oy;
    }
    
    Point2Di GetImageSize() const {
        if (loadedImage->IsValid()) {
            return Point2Di(loadedImage->width, loadedImage->height);
        }
        return Point2Di(0, 0);
    }
    
//    ImageFormat GetImageFormat() const { return loadedImage.format; }
    const std::string& GetImagePath() const { return imagePath; }
    
    // ===== INTERACTION =====
    void SetClickable(bool enable) {
        clickable = enable;
        SetMousePointer(enable ? MousePointer::Hand : MousePointer::Default);
    }
    
    void SetDraggable(bool enable) { draggable = enable; }
    
    // ===== RENDERING =====
    void Render(IRenderContext* ctx) override;
    
    // ===== EVENT HANDLING =====
    bool OnEvent(const UCEvent& event) override;
    
private:
    ImageFormat DetectFormatFromPath(const std::string& path);
    ImageFormat DetectFormatFromData(const std::vector<uint8_t>& data);

    bool ProcessImageData(ImageFormat format);
    void SetError(const std::string& message);

    void DrawLoadedImage(IRenderContext* ctx);
    void DrawErrorPlaceholder(IRenderContext* ctx);
    void DrawLoadingPlaceholder(IRenderContext* ctx);
    void DrawImagePlaceholder(const Rect2Di& rect, const std::string& text, const Color& bgColor = Color(240, 240, 240));
    
    Rect2Di CalculateDisplayRect();
    
    void HandleMouseDown(const UCEvent& event);
    void HandleMouseMove(const UCEvent& event);
    void HandleMouseUp(const UCEvent& event);
};

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasImageElement> CreateImageElement(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasUIElementFactory::Create<UltraCanvasImageElement>(identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasImageElement> CreateImageElement(
        const std::string& identifier, long w = 0, long h = 0) {
    return UltraCanvasUIElementFactory::Create<UltraCanvasImageElement>(identifier, 0, 0, 0, w, h);
}

inline std::shared_ptr<UltraCanvasImageElement> CreateImageFromFile(
    const std::string& identifier, long id, long x, long y, long w, long h, const std::string& imagePath) {
    auto image = CreateImageElement(identifier, id, x, y, w, h);
    image->LoadFromFile(imagePath);
    return image;
}

inline std::shared_ptr<UltraCanvasImageElement> CreateImageFromMemory(
    const std::string& identifier, long id, long x, long y, long w, long h, 
    const std::vector<uint8_t>& imageData, ImageFormat format = ImageFormat::Unknown) {
    auto image = CreateImageElement(identifier, id, x, y, w, h);
    image->LoadFromMemory(imageData, format);
    return image;
}

// ===== CONVENIENCE FUNCTIONS =====
inline std::shared_ptr<UltraCanvasImageElement> CreateScaledImage(
    const std::string& identifier, long id, long x, long y, long w, long h,
    const std::string& imagePath, ImageScaleMode scaleMode) {
    auto image = CreateImageFromFile(identifier, id, x, y, w, h, imagePath);
    image->SetScaleMode(scaleMode);
    return image;
}

inline std::shared_ptr<UltraCanvasImageElement> CreateClickableImage(
    const std::string& identifier, long id, long x, long y, long w, long h,
    const std::string& imagePath, std::function<void()> clickCallback) {
    auto image = CreateImageFromFile(identifier, id, x, y, w, h, imagePath);
    image->SetClickable(true);
    image->onClick = clickCallback;
    return image;
}

//// ===== LEGACY C-STYLE INTERFACE =====
//extern "C" {
//    static UltraCanvasImageElement* g_currentImageElement = nullptr;
//
//    void* LoadImageFromFile(const char* imagePath) {
//        if (!imagePath) return nullptr;
//
//        g_currentImageElement = new UltraCanvasImageElement("legacy_image", 9996, 0, 0, 100, 100);
//
//        if (g_currentImageElement->LoadFromFile(imagePath)) {
//            return g_currentImageElement;
//        } else {
//            delete g_currentImageElement;
//            g_currentImageElement = nullptr;
//            return nullptr;
//        }
//    }
//
//    void* LoadImageFromMemory(const unsigned char* data, int size, int format) {
//        if (!data || size <= 0) return nullptr;
//
//        g_currentImageElement = new UltraCanvasImageElement("legacy_image", 9996, 0, 0, 100, 100);
//
//        ImageFormat imgFormat = static_cast<ImageFormat>(format);
//        if (g_currentImageElement->LoadFromMemory(data, size, imgFormat)) {
//            return g_currentImageElement;
//        } else {
//            delete g_currentImageElement;
//            g_currentImageElement = nullptr;
//            return nullptr;
//        }
//    }
//
//    void SetImagePosition(void* imageHandle, int x, int y) {
//        if (imageHandle) {
//            auto* image = static_cast<UltraCanvasImageElement*>(imageHandle);
//            image->SetX(x);
//            image->SetY(y);
//        }
//    }
//
//    void SetImageSize(void* imageHandle, int width, int height) {
//        if (imageHandle) {
//            auto* image = static_cast<UltraCanvasImageElement*>(imageHandle);
//            image->SetWidth(width);
//            image->SetHeight(height);
//        }
//    }
//
//    void SetImageScaleMode(void* imageHandle, int mode) {
//        if (imageHandle) {
//            auto* image = static_cast<UltraCanvasImageElement*>(imageHandle);
//            image->SetScaleMode(static_cast<ImageScaleMode>(mode));
//        }
//    }
//
//    int IsImageLoaded(void* imageHandle) {
//        if (imageHandle) {
//            auto* image = static_cast<UltraCanvasImageElement*>(imageHandle);
//            return image->IsLoaded() ? 1 : 0;
//        }
//        return 0;
//    }
//
//    void DestroyImage(void* imageHandle) {
//        if (imageHandle) {
//            delete static_cast<UltraCanvasImageElement*>(imageHandle);
//        }
//    }
//}

} // namespace UltraCanvas