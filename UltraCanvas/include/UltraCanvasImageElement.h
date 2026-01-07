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

// ===== IMAGE SCALING MODES =====
//enum class ImageScaleMode {
//    NoScale,           // No scaling - original size
//    Stretch,        // Stretch to fit bounds (may distort)
//    Uniform,        // Scale uniformly to fit (maintain aspect ratio)
//    UniformToFill,  // Scale uniformly to fill (may crop)
//    Center,         // Center image without scaling
//    Tile            // Tile image to fill bounds
//};

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
    std::shared_ptr<UCImage> loadedImage;
    ImageLoadState loadState = ImageLoadState::NotLoaded;
    
    // Display properties
    ImageFitMode fitMode = ImageFitMode::Contain;
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
    bool LoadFromImage(std::shared_ptr<UCImage> img);

    // ===== IMAGE PROPERTIES =====
    void SetFitMode(ImageFitMode mode) { fitMode = mode; }
    
    ImageFitMode GetFitMode() const { return fitMode; }
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
            return Point2Di(loadedImage->GetWidth(), loadedImage->GetHeight());
        }
        return Point2Di(0, 0);
    }
    
//    ImageFormat GetImageFormat() const { return loadedImage.format; }
    //const std::string& GetImagePath() const { return loadedImage.imagePath; }
    
    // ===== INTERACTION =====
    void SetClickable(bool enable) {
        clickable = enable;
        SetMouseCursor(enable ? UCMouseCursor::Hand : UCMouseCursor::Default);
    }
    
    void SetDraggable(bool enable) { draggable = enable; }
    
    // ===== RENDERING =====
    void Render(IRenderContext* ctx) override;
    
    // ===== EVENT HANDLING =====
    bool OnEvent(const UCEvent& event) override;
    
private:
    void SetError(const std::string& message);

    void DrawLoadedImage(IRenderContext* ctx);
    void DrawErrorPlaceholder(IRenderContext* ctx);
    void DrawLoadingPlaceholder(IRenderContext* ctx);
    void DrawImagePlaceholder(const Rect2Di& rect, const std::string& text, const Color& bgColor = Color(240, 240, 240));

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
    const std::vector<uint8_t>& imageData, UCImageLoadFormat format = UCImageLoadFormat::Autodetect) {
    auto image = CreateImageElement(identifier, id, x, y, w, h);
    auto img = UCImageRaster::LoadFromMemory(imageData);
    image->LoadFromImage(img);
    return image;
}

// ===== CONVENIENCE FUNCTIONS =====
inline std::shared_ptr<UltraCanvasImageElement> CreateScaledImage(
    const std::string& identifier, long id, long x, long y, long w, long h,
    const std::string& imagePath, ImageFitMode fitMode) {
    auto image = CreateImageFromFile(identifier, id, x, y, w, h, imagePath);
    image->SetFitMode(fitMode);
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

} // namespace UltraCanvas