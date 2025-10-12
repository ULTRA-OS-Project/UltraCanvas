// core/UltraCanvasImageElement.cpp
// Image display component with loading, caching, and transformation support
// Version: 1.0.0
// Last Modified: 2024-12-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasImageElement.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <fstream>
#include <iostream>

namespace UltraCanvas {

    UltraCanvasImageElement::UltraCanvasImageElement(const std::string &identifier, long id, long x, long y, long w,
                                                     long h)
            : UltraCanvasUIElement(identifier, id, x, y, w, h), properties(identifier, id, x, y, w, h) {

        properties.MousePtr = MousePointer::Default;
        properties.MouseCtrl = MouseControls::Object2D;
    }

    bool UltraCanvasImageElement::LoadFromFile(const std::string &filePath) {
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

    bool UltraCanvasImageElement::LoadFromMemory(const std::vector<uint8_t> &data, ImageFormat format) {
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

    bool UltraCanvasImageElement::LoadFromMemory(const uint8_t *data, size_t size, ImageFormat format) {
        if (!data || size == 0) {
            SetError("Invalid image data");
            return false;
        }

        std::vector<uint8_t> dataVector(data, data + size);
        return LoadFromMemory(dataVector, format);
    }

    ImageFormat UltraCanvasImageElement::DetectFormatFromPath(const std::string &path) {
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

    void UltraCanvasImageElement::Render() {
        auto ctx = GetRenderContext();
        if (!IsVisible() || !ctx) return;

        ctx->PushState();

        if (IsLoaded()) {
            DrawLoadedImage(ctx);
        } else if (HasError() && showErrorPlaceholder) {
            DrawErrorPlaceholder(ctx);
        } else if (IsLoading()) {
            DrawLoadingPlaceholder(ctx);
        }
        ctx->PopState();
    }

    bool UltraCanvasImageElement::OnEvent(const UCEvent &event) {
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

    ImageFormat UltraCanvasImageElement::DetectFormatFromData(const std::vector<uint8_t> &data) {
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

    bool UltraCanvasImageElement::ProcessImageData(ImageFormat format) {
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

    void UltraCanvasImageElement::SetError(const std::string &message) {
        errorMessage = message;
        loadState = ImageLoadState::Failed;
        loadedImage = ImageData(); // Reset

        std::cerr << "[UltraCanvasImageElement] Error: " << message << std::endl;

        if (onImageLoadFailed) {
            onImageLoadFailed(message);
        }
    }

    void UltraCanvasImageElement::DrawLoadedImage(IRenderContext *ctx) {

        // Apply global alpha
        ctx->SetAlpha(opacity);

        // Apply transformations
        if (rotation != 0.0f || scale.x != 1.0f || scale.y != 1.0f || offset.x != 0.0f || offset.y != 0.0f) {
            ctx->PushState();

            // Translate to center for rotation
            Point2Di center = Point2Di(GetX() + GetWidth() / 2.0f, GetY() + GetHeight() / 2.0f);
            ctx->Translate(center.x, center.y);

            // Apply transformations
            if (rotation != 0.0f) ctx->Rotate(rotation * M_PI/180.0);
            if (scale.x != 1.0f || scale.y != 1.0f) ctx->Scale(scale.x, scale.y);
            if (offset.x != 0.0f || offset.y != 0.0f) ctx->Translate(offset.x, offset.y);

            // Translate back
            ctx->Translate(-center.x, -center.y);
        }

        // Calculate display rectangle based on scale mode
        Rect2Di displayRect = CalculateDisplayRect();

        // Draw the image using unified rendering
        if (!imagePath.empty()) {
            // Load from file path
            ctx->DrawImage(imagePath, displayRect);
        } else {
            // For memory-loaded images, we'd need to save to a temporary file
            // or extend the rendering interface to support raw data
            // For now, draw a placeholder
            DrawImagePlaceholder(displayRect, "IMG");
        }

        if (rotation != 0.0f || scale.x != 1.0f || scale.y != 1.0f || offset.x != 0.0f || offset.y != 0.0f) {
            ctx->PopState();
        }
    }

    void UltraCanvasImageElement::DrawErrorPlaceholder(IRenderContext *ctx) {
        DrawImagePlaceholder(GetBounds(), "ERR", errorColor);

        // Draw error message
        if (!errorMessage.empty()) {
            ctx->SetTextPaint(Colors::Red);
            ctx->SetFontStyle({.fontFamily="Arial", .fontSize=10});

            Rect2Di textRect = GetBounds();
            textRect.y += GetHeight() / 2 + 10;
            textRect.height = 20;

            ctx->DrawTextInRect(errorMessage, textRect);
        }
    }

    void UltraCanvasImageElement::DrawLoadingPlaceholder(IRenderContext *ctx) {
        DrawImagePlaceholder(GetBounds(), "...", Color(220, 220, 220));
    }

    void
    UltraCanvasImageElement::DrawImagePlaceholder(const Rect2Di &rect, const std::string &text, const Color &bgColor) {
        // Draw background
        auto ctx = GetRenderContext();
        ctx->DrawFilledRectangle(rect, bgColor, 1.0f, Colors::Gray);

        // Draw text
        ctx->SetTextPaint(Colors::Gray);
        ctx->SetFontSize(14.0f);
        Point2Di textSize = ctx->MeasureText(text);
        Point2Di textPos(
                rect.x + (rect.width - textSize.x) / 2,
                rect.y + (rect.height + textSize.y) / 2
        );
        ctx->DrawText(text, textPos);
    }

    Rect2Di UltraCanvasImageElement::CalculateDisplayRect() {
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

    void UltraCanvasImageElement::HandleMouseDown(const UCEvent &event) {
        if (!Contains(event.x, event.y)) return;

        if (clickable && onClick) {
            onClick();
        }

        if (draggable) {
            isDragging = true;
            dragStartPos = Point2Di(event.x, event.y);
        }
    }

    void UltraCanvasImageElement::HandleMouseMove(const UCEvent &event) {
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

    void UltraCanvasImageElement::HandleMouseUp(const UCEvent &event) {
        isDragging = false;
    }
}