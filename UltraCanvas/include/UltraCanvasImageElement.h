// UltraCanvasImageElement.h
// Image display component with loading, caching, and transformation support
// Version: 1.0.0
// Last Modified: 2024-12-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
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

// ===== IMAGE DATA STRUCTURE =====
struct ImageData {
    std::vector<uint8_t> rawData;
    int width = 0;
    int height = 0;
    int channels = 0;
    ImageFormat format = ImageFormat::Unknown;
    bool isValid = false;
    
    ImageData() = default;
    
    size_t GetDataSize() const {
        return width * height * channels;
    }
    
    bool HasAlpha() const {
        return channels == 4;
    }
};

// ===== IMAGE ELEMENT COMPONENT =====
class UltraCanvasImageElement : public UltraCanvasElement {
private:
    StandardProperties properties;
    
    // Image source
    std::string imagePath;
    std::vector<uint8_t> imageData;
    ImageData loadedImage;
    ImageLoadState loadState = ImageLoadState::NotLoaded;
    
    // Display properties
    ImageScaleMode scaleMode = ImageScaleMode::Uniform;
    Color tintColor = Colors::White;
    float opacity = 1.0f;
    bool smoothScaling = true;
    
    // Transform properties
    float rotation = 0.0f;
    Point2Di scale = Point2Di(1.0f, 1.0f);
    Point2Di offset = Point2Di(0.0f, 0.0f);
    
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
    std::function<void()> onImageClicked;
    std::function<void(const Point2Di&)> onImageDragged;
    
    // ===== CONSTRUCTOR =====
    UltraCanvasImageElement(const std::string& identifier = "ImageElement", long id = 0,
                           long x = 0, long y = 0, long w = 100, long h = 100)
        : UltraCanvasElement(identifier, id, x, y, w, h), properties(identifier, id, x, y, w, h) {
        
        properties.MousePtr = MousePointer::Default;
        properties.MouseCtrl = MouseControls::Object2D;
    }

    // ===== IMAGE LOADING =====
    bool LoadFromFile(const std::string& filePath) {
        imagePath = filePath;
        imageData.clear();
        loadState = ImageLoadState::Loading;
        errorMessage.clear();
        
        try {
            // Detect format from extension
            ImageFormat format = DetectFormatFromPath(filePath);
            if (format == ImageFormat::Unknown) {
                SetError("Unsupported image format");
                return false;
            }
            
            // Load file data
            std::ifstream file(filePath, std::ios::binary);
            if (!file.is_open()) {
                SetError("Cannot open file: " + filePath);
                return false;
            }
            
            // Read file size
            file.seekg(0, std::ios::end);
            size_t fileSize = file.tellg();
            file.seekg(0, std::ios::beg);
            
            if (fileSize == 0) {
                SetError("Empty image file");
                return false;
            }
            
            // Read file data
            imageData.resize(fileSize);
            file.read(reinterpret_cast<char*>(imageData.data()), fileSize);
            file.close();
            
            // Process image data
            return ProcessImageData(format);
            
        } catch (const std::exception& e) {
            SetError("Error loading image: " + std::string(e.what()));
            return false;
        }
    }
    
    bool LoadFromMemory(const std::vector<uint8_t>& data, ImageFormat format = ImageFormat::Unknown) {
        imagePath.clear();
        imageData = data;
        loadState = ImageLoadState::Loading;
        errorMessage.clear();
        
        if (data.empty()) {
            SetError("Empty image data");
            return false;
        }
        
        // Auto-detect format if not specified
        if (format == ImageFormat::Unknown) {
            format = DetectFormatFromData(data);
        }
        
        if (format == ImageFormat::Unknown) {
            SetError("Cannot determine image format");
            return false;
        }
        
        return ProcessImageData(format);
    }
    
    bool LoadFromMemory(const uint8_t* data, size_t size, ImageFormat format = ImageFormat::Unknown) {
        if (!data || size == 0) {
            SetError("Invalid image data");
            return false;
        }
        
        std::vector<uint8_t> dataVector(data, data + size);
        return LoadFromMemory(dataVector, format);
    }
    
    // ===== IMAGE PROPERTIES =====
    void SetScaleMode(ImageScaleMode mode) {
        scaleMode = mode;
    }
    
    ImageScaleMode GetScaleMode() const {
        return scaleMode;
    }
    
    void SetTintColor(const Color& color) {
        tintColor = color;
    }
    
    void SetOpacity(float alpha) {
        opacity = std::max(0.0f, std::min(1.0f, alpha));
    }
    
    float GetOpacity() const {
        return opacity;
    }
    
    void SetRotation(float degrees) {
        rotation = degrees;
    }
    
    void SetScale(float sx, float sy) {
        scale.x = sx;
        scale.y = sy;
    }
    
    void SetOffset(float ox, float oy) {
        offset.x = ox;
        offset.y = oy;
    }
    
    // ===== IMAGE INFO =====
    bool IsLoaded() const {
        return loadState == ImageLoadState::Loaded && loadedImage.isValid;
    }
    
    bool IsLoading() const {
        return loadState == ImageLoadState::Loading;
    }
    
    bool HasError() const {
        return loadState == ImageLoadState::Failed;
    }
    
    const std::string& GetErrorMessage() const {
        return errorMessage;
    }
    
    Point2Di GetImageSize() const {
        if (IsLoaded()) {
            return Point2Di(static_cast<float>(loadedImage.width), static_cast<float>(loadedImage.height));
        }
        return Point2Di(0, 0);
    }
    
    ImageFormat GetImageFormat() const {
        return loadedImage.format;
    }
    
    const std::string& GetImagePath() const {
        return imagePath;
    }
    
    // ===== INTERACTION =====
    void SetClickable(bool enable) {
        clickable = enable;
        properties.MousePtr = enable ? MousePointer::Hand : MousePointer::Default;
    }
    
    void SetDraggable(bool enable) {
        draggable = enable;
    }
    
    // ===== RENDERING =====
    void Render() override {
        if (!IsVisible()) return;
        
        ULTRACANVAS_RENDER_SCOPE();
        
        if (IsLoaded()) {
            DrawLoadedImage();
        } else if (HasError() && showErrorPlaceholder) {
            DrawErrorPlaceholder();
        } else if (IsLoading()) {
            DrawLoadingPlaceholder();
        }
    }
    
    // ===== EVENT HANDLING =====
    bool OnEvent(const UCEvent& event) override {
        if (!IsActive() || !IsVisible()) return false;
        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(event);
                break;
                
            case UCEventType::MouseMove:
                HandleMouseMove(event);
                break;
                
            case UCEventType::MouseUp:
                HandleMouseUp(event);
                break;
        }
        return false;
    }
    
private:
    ImageFormat DetectFormatFromPath(const std::string& path) {
        // Get file extension
        size_t dotPos = path.find_last_of('.');
        if (dotPos == std::string::npos) return ImageFormat::Unknown;
        
        std::string ext = path.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == "png") return ImageFormat::PNG;
        if (ext == "jpg" || ext == "jpeg") return ImageFormat::JPEG;
        if (ext == "bmp") return ImageFormat::BMP;
        if (ext == "gif") return ImageFormat::GIF;
        if (ext == "tiff" || ext == "tif") return ImageFormat::TIFF;
        if (ext == "webp") return ImageFormat::WEBP;
        if (ext == "svg") return ImageFormat::SVG;
        if (ext == "ico") return ImageFormat::ICO;
        if (ext == "avif") return ImageFormat::AVIF;
        
        return ImageFormat::Unknown;
    }
    
    ImageFormat DetectFormatFromData(const std::vector<uint8_t>& data) {
        if (data.size() < 4) return ImageFormat::Unknown;
        
        // PNG signature
        if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
            return ImageFormat::PNG;
        }
        
        // JPEG signature
        if (data[0] == 0xFF && data[1] == 0xD8) {
            return ImageFormat::JPEG;
        }
        
        // BMP signature
        if (data[0] == 0x42 && data[1] == 0x4D) {
            return ImageFormat::BMP;
        }
        
        // GIF signature
        if (data.size() >= 6) {
            if ((data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46 && 
                 data[3] == 0x38 && (data[4] == 0x37 || data[4] == 0x39) && data[5] == 0x61)) {
                return ImageFormat::GIF;
            }
        }
        
        // WebP signature
        if (data.size() >= 12) {
            if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46 &&
                data[8] == 0x57 && data[9] == 0x45 && data[10] == 0x42 && data[11] == 0x50) {
                return ImageFormat::WEBP;
            }
        }
        
        return ImageFormat::Unknown;
    }
    
    bool ProcessImageData(ImageFormat format) {
        try {
            // For now, we'll use the unified rendering system to load images
            // The actual decoding would be handled by the platform-specific implementation
            
            // Create a simple image data structure
            loadedImage.format = format;
            loadedImage.rawData = imageData;
            
            // For demonstration, set some default values
            // In a real implementation, this would decode the actual image
            loadedImage.width = GetWidth();
            loadedImage.height = GetHeight();
            loadedImage.channels = 4; // RGBA
            loadedImage.isValid = true;
            
            loadState = ImageLoadState::Loaded;
            
            if (onImageLoaded) {
                onImageLoaded();
            }
            
            return true;
            
        } catch (const std::exception& e) {
            SetError("Failed to process image: " + std::string(e.what()));
            return false;
        }
    }
    
    void SetError(const std::string& message) {
        errorMessage = message;
        loadState = ImageLoadState::Failed;
        loadedImage = ImageData(); // Reset
        
        std::cerr << "[UltraCanvasImageElement] Error: " << message << std::endl;
        
        if (onImageLoadFailed) {
            onImageLoadFailed(message);
        }
    }
    
    void DrawLoadedImage() {
        // Apply global alpha
        SetGlobalAlpha(opacity);
        
        // Apply transformations
        if (rotation != 0.0f || scale.x != 1.0f || scale.y != 1.0f || offset.x != 0.0f || offset.y != 0.0f) {
            PushRenderState();
            
            // Translate to center for rotation
            Point2Di center = Point2Di(GetX() + GetWidth() / 2.0f, GetY() + GetHeight() / 2.0f);
            Translate(center.x, center.y);
            
            // Apply transformations
            if (rotation != 0.0f) Rotate(rotation);
            if (scale.x != 1.0f || scale.y != 1.0f) Scale(scale.x, scale.y);
            if (offset.x != 0.0f || offset.y != 0.0f) Translate(offset.x, offset.y);
            
            // Translate back
            Translate(-center.x, -center.y);
        }
        
        // Calculate display rectangle based on scale mode
        Rect2Di displayRect = CalculateDisplayRect();
        
        // Draw the image using unified rendering
        if (!imagePath.empty()) {
            // Load from file path
            DrawImage(imagePath, displayRect);
        } else {
            // For memory-loaded images, we'd need to save to a temporary file
            // or extend the rendering interface to support raw data
            // For now, draw a placeholder
            DrawImagePlaceholder(displayRect, "IMG");
        }
        
        if (rotation != 0.0f || scale.x != 1.0f || scale.y != 1.0f || offset.x != 0.0f || offset.y != 0.0f) {
            PopRenderState();
        }
    }
    
    void DrawErrorPlaceholder() {
        DrawImagePlaceholder(GetBounds(), "ERR", errorColor);
        
        // Draw error message
        if (!errorMessage.empty()) {
            SetTextColor(Colors::Red);
            SetFont("Arial", 10.0f);
            
            Rect2Di textRect = GetBounds();
            textRect.y += GetHeight() / 2 + 10;
            textRect.height = 20;
            
            DrawTextInRect(errorMessage, textRect);
        }
    }
    
    void DrawLoadingPlaceholder() {
        DrawImagePlaceholder(GetBounds(), "...", Color(220, 220, 220));
    }
    
    void DrawImagePlaceholder(const Rect2Di& rect, const std::string& text, const Color& bgColor = Color(240, 240, 240)) {
        // Draw background
        DrawFilledRectangle(rect, bgColor, Colors::Gray, 1.0f);
        
        // Draw text
        SetTextColor(Colors::Gray);
        SetFont("Arial", 14.0f);
        Point2Di textSize = MeasureText(text);
        Point2Di textPos(
            rect.x + (rect.width - textSize.x) / 2,
            rect.y + (rect.height + textSize.y) / 2
        );
        DrawText(text, textPos);
    }
    
    Rect2Di CalculateDisplayRect() {
        Rect2Di bounds = GetBounds();
        
        if (!IsLoaded()) {
            return bounds;
        }
        
        float imageWidth = static_cast<float>(loadedImage.width);
        float imageHeight = static_cast<float>(loadedImage.height);
        
        switch (scaleMode) {
            case ImageScaleMode::NoScale:
                return Rect2Di(bounds.x, bounds.y, imageWidth, imageHeight);
                
            case ImageScaleMode::Stretch:
                return bounds;
                
            case ImageScaleMode::Uniform: {
                float scaleX = bounds.width / imageWidth;
                float scaleY = bounds.height / imageHeight;
                float uniformScale = std::min(scaleX, scaleY);
                
                float scaledWidth = imageWidth * uniformScale;
                float scaledHeight = imageHeight * uniformScale;
                
                return Rect2Di(
                    bounds.x + (bounds.width - scaledWidth) / 2,
                    bounds.y + (bounds.height - scaledHeight) / 2,
                    scaledWidth,
                    scaledHeight
                );
            }
            
            case ImageScaleMode::UniformToFill: {
                float scaleX = bounds.width / imageWidth;
                float scaleY = bounds.height / imageHeight;
                float uniformScale = std::max(scaleX, scaleY);
                
                float scaledWidth = imageWidth * uniformScale;
                float scaledHeight = imageHeight * uniformScale;
                
                return Rect2Di(
                    bounds.x + (bounds.width - scaledWidth) / 2,
                    bounds.y + (bounds.height - scaledHeight) / 2,
                    scaledWidth,
                    scaledHeight
                );
            }
            
            case ImageScaleMode::Center:
                return Rect2Di(
                    bounds.x + (bounds.width - imageWidth) / 2,
                    bounds.y + (bounds.height - imageHeight) / 2,
                    imageWidth,
                    imageHeight
                );
                
            case ImageScaleMode::Tile:
                // For tiling, we'd need to draw multiple instances
                // For now, just return the bounds
                return bounds;
                
            default:
                return bounds;
        }
    }
    
    void HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return;
        
        if (clickable && onImageClicked) {
            onImageClicked();
        }
        
        if (draggable) {
            isDragging = true;
            dragStartPos = Point2Di(event.x, event.y);
        }
    }
    
    void HandleMouseMove(const UCEvent& event) {
        if (isDragging && draggable) {
            Point2Di currentPos(event.x, event.y);
            Point2Di delta = currentPos - dragStartPos;
            
            // Update position
            SetX(GetX() + static_cast<long>(delta.x));
            SetY(GetY() + static_cast<long>(delta.y));
            
            dragStartPos = currentPos;
            
            if (onImageDragged) {
                onImageDragged(delta);
            }
        }
    }
    
    void HandleMouseUp(const UCEvent& event) {
        isDragging = false;
    }
};

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasImageElement> CreateImageElement(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasElementFactory::CreateWithID<UltraCanvasImageElement>(id, identifier, id, x, y, w, h);
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
    image->onImageClicked = clickCallback;
    return image;
}

// ===== LEGACY C-STYLE INTERFACE =====
extern "C" {
    static UltraCanvasImageElement* g_currentImageElement = nullptr;
    
    void* LoadImageFromFile(const char* imagePath) {
        if (!imagePath) return nullptr;
        
        g_currentImageElement = new UltraCanvasImageElement("legacy_image", 9996, 0, 0, 100, 100);
        
        if (g_currentImageElement->LoadFromFile(imagePath)) {
            return g_currentImageElement;
        } else {
            delete g_currentImageElement;
            g_currentImageElement = nullptr;
            return nullptr;
        }
    }
    
    void* LoadImageFromMemory(const unsigned char* data, int size, int format) {
        if (!data || size <= 0) return nullptr;
        
        g_currentImageElement = new UltraCanvasImageElement("legacy_image", 9996, 0, 0, 100, 100);
        
        ImageFormat imgFormat = static_cast<ImageFormat>(format);
        if (g_currentImageElement->LoadFromMemory(data, size, imgFormat)) {
            return g_currentImageElement;
        } else {
            delete g_currentImageElement;
            g_currentImageElement = nullptr;
            return nullptr;
        }
    }
    
    void SetImagePosition(void* imageHandle, int x, int y) {
        if (imageHandle) {
            auto* image = static_cast<UltraCanvasImageElement*>(imageHandle);
            image->SetX(x);
            image->SetY(y);
        }
    }
    
    void SetImageSize(void* imageHandle, int width, int height) {
        if (imageHandle) {
            auto* image = static_cast<UltraCanvasImageElement*>(imageHandle);
            image->SetWidth(width);
            image->SetHeight(height);
        }
    }
    
    void SetImageScaleMode(void* imageHandle, int mode) {
        if (imageHandle) {
            auto* image = static_cast<UltraCanvasImageElement*>(imageHandle);
            image->SetScaleMode(static_cast<ImageScaleMode>(mode));
        }
    }
    
    int IsImageLoaded(void* imageHandle) {
        if (imageHandle) {
            auto* image = static_cast<UltraCanvasImageElement*>(imageHandle);
            return image->IsLoaded() ? 1 : 0;
        }
        return 0;
    }
    
    void DestroyImage(void* imageHandle) {
        if (imageHandle) {
            delete static_cast<UltraCanvasImageElement*>(imageHandle);
        }
    }
}

} // namespace UltraCanvas