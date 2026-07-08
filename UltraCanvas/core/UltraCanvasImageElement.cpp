// core/UltraCanvasImageElement.cpp
// Image display component with loading, caching, and transformation support
// Version: 1.1.0
// Last Modified: 2026-06-02
// Author: UltraCanvas Framework

#include "UltraCanvasImageElement.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasFileError.h"
#include "CSSLayout/LayoutUtils.h"
#include <optional>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <fstream>
#include <iostream>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    UltraCanvasImageElement::UltraCanvasImageElement(const std::string &identifier, float x, float y, float w,
                                                     float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {

    }

    UltraCanvasImageElement::UltraCanvasImageElement(const std::string &identifier, float w, float h)
            : UltraCanvasUIElement(identifier, w, h) {

    }

    UltraCanvasImageElement::UltraCanvasImageElement(const std::string &identifier)
            : UltraCanvasUIElement(identifier) {

    }

    bool UltraCanvasImageElement::LoadFromFile(const std::string &filePath) {
        errorMessage.clear();
        loadedImage = UCImage::Get(filePath);
        // Intrinsic size changed — re-measure (for auto-sized elements) and repaint.
        InvalidateLayout();
        RequestRedraw();
        if (loadedImage && loadedImage->IsValid()) {
            if (onImageLoaded) onImageLoaded();
            return true;
        }

        // Surface the real reason: prefer the loader's message, fall back to a
        // file-access diagnosis (missing / locked / no permission).
        std::string reason;
        if (loadedImage && !loadedImage->errorMessage.empty()) {
            reason = loadedImage->errorMessage;
        }
        if (reason.empty()) reason = DescribeFileReadError(filePath);
        if (reason.empty()) reason = "Could not load image: " + filePath;
        SetError(reason);  // resets loadedImage and fires onImageLoadFailed
        return false;
    }

    bool UltraCanvasImageElement::LoadFromImage(std::shared_ptr<UCImage> img) {
        loadedImage = img;
        // Intrinsic size changed — re-measure (for auto-sized elements) and repaint.
        InvalidateLayout();
        RequestRedraw();
        if (loadedImage) {
            return true;
        }
        return false;
    }

    Size2Df UltraCanvasImageElement::NaturalImageSize() const {
        if (loadedImage && loadedImage->IsValid()) {
            return Size2Df((float)loadedImage->GetWidth(), (float)loadedImage->GetHeight());
        }
        return Size2Df(0.f, 0.f);
    }

    Size2Df UltraCanvasImageElement::MeasureOwnContent(std::optional<float> definiteContentWidth,
                                                       const CSSLayout::LayoutContext& /*ctx*/) {
        // Content box = the image's natural pixel size ({0,0} if none loaded).
        // The block layout adds padding/border and applies size.*/constraints.
        // Replaced-element behavior: when the resolved content width is
        // narrower than the natural width (explicit width or max-width
        // clamp), report the aspect-preserving scaled height so the image
        // shrinks instead of letterboxing inside a natural-height box.
        Size2Df natural = NaturalImageSize();
        if (definiteContentWidth && natural.width > 0.f &&
            *definiteContentWidth < natural.width) {
            float scale = *definiteContentWidth / natural.width;
            return Size2Df(natural.width * scale, natural.height * scale);
        }
        return natural;
    }

    void UltraCanvasImageElement::ComputeIntrinsicSizes(const CSSLayout::LayoutContext& /*ctx*/) {
        const float padH = GetTotalPaddingHorizontal() + GetTotalBorderHorizontal();
        const float padV = GetTotalPaddingVertical()   + GetTotalBorderVertical();
        Size2Df content = NaturalImageSize();
        intrinsic.valid = true;
        intrinsic.maxContentWidth  = content.width  + padH;
        intrinsic.minContentWidth  = content.width  + padH;
        intrinsic.maxContentHeight = content.height + padV;
        intrinsic.minContentHeight = content.height + padV;
    }

    void UltraCanvasImageElement::Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) {
        // No sub-rects to position: the image is drawn from GetLocalBounds() + fitMode at
        // render time. The base sets finalBounds + damage tracking.
        UltraCanvasUIElement::Arrange(finalRect, ctx);
    }

    void UltraCanvasImageElement::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (!IsVisible() || finalBounds.width == 0 || finalBounds.height == 0) return;

        ctx->PushState();

        if (loadedImage && loadedImage->IsValid()) {
            DrawLoadedImage(ctx);
//        } else if (loadedImage->IsLoading()) {
//            DrawLoadingPlaceholder(ctx);
        } else if (loadedImage && !loadedImage->errorMessage.empty() && showErrorPlaceholder) {
            DrawErrorPlaceholder(ctx);
        }
        ctx->PopState();
    }

    bool UltraCanvasImageElement::OnEvent(const UCEvent &event) {
        if (IsDisabled() || !IsVisible()) return false;
        
        if (UltraCanvasUIElement::OnEvent(event)) {
            return true;
        }        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(event);
                return true;

            case UCEventType::MouseMove:
                HandleMouseMove(event);
                return true;

            case UCEventType::MouseUp:
                HandleMouseUp(event);
                return true;
        }
        return false;
    }


    void UltraCanvasImageElement::SetError(const std::string &message) {
        errorMessage = message;
//        loadState = ImageLoadState::Failed;
        loadedImage = std::make_shared<UCImage>(); // Reset

        debugOutput << "[UltraCanvasImageElement] Error: " << message << std::endl;

        if (onImageLoadFailed) {
            onImageLoadFailed(message);
        }
    }

    void UltraCanvasImageElement::DrawLoadedImage(IRenderContext *ctx) {
        // Apply global alpha
        ctx->SetAlpha(opacity);

        // Apply transformations (ctx is already translated to element origin)
        if (rotation != 0.0f || scale.x != 1.0f || scale.y != 1.0f || offset.x != 0.0f || offset.y != 0.0f) {
            ctx->PushState();

            // Translate to center for rotation (element-local center)
            Point2Di center = Point2Di(GetWidth() / 2.0f, GetHeight() / 2.0f);
            ctx->Translate(center.x, center.y);

            // Apply transformations
            if (rotation != 0.0f) ctx->Rotate(rotation * M_PI/180.0);
            if (scale.x != 1.0f || scale.y != 1.0f) ctx->Scale(scale.x, scale.y);
            if (offset.x != 0.0f || offset.y != 0.0f) ctx->Translate(offset.x, offset.y);

            // Translate back
            ctx->Translate(-center.x, -center.y);
        }

        // Draw the image using unified rendering (element-local bounds)
        if (loadedImage->IsValid()) {
            // Load from file path
            ctx->DrawImage(*loadedImage.get(), GetLocalBounds(), fitMode);
        } else {
            // For memory-loaded images, we'd need to save to a temporary file
            // or extend the rendering interface to support raw data
            // For now, draw a placeholder
            DrawImagePlaceholder(GetLocalBounds(), "IMG");
        }

        if (rotation != 0.0f || scale.x != 1.0f || scale.y != 1.0f || offset.x != 0.0f || offset.y != 0.0f) {
            ctx->PopState();
        }
    }

    void UltraCanvasImageElement::DrawErrorPlaceholder(IRenderContext *ctx) {
        DrawImagePlaceholder(GetLocalBounds(), "ERR", errorColor);

        // Draw error message (element-local coordinates)
        if (!loadedImage->errorMessage.empty()) {
            ctx->SetTextPaint(Colors::Red);
            ctx->SetFontStyle({.fontSize=10});

            Rect2Dd textRect = GetLocalBounds();
            textRect.y += static_cast<double>(GetHeight()) / 2.0f + 10;
            textRect.height = 20;

            ctx->DrawTextInRect(loadedImage->errorMessage, textRect);
        }
    }

    void UltraCanvasImageElement::DrawLoadingPlaceholder(IRenderContext *ctx) {
        DrawImagePlaceholder(GetLocalBounds(), "...", Color(220, 220, 220));
    }

    void
    UltraCanvasImageElement::DrawImagePlaceholder(const Rect2Di &rect, const std::string &text, const Color &bgColor) {
        // Draw background
        auto ctx = GetRenderContext();
        ctx->DrawFilledRectangle(rect, bgColor, 1.0f, Colors::Gray);

        // Draw text
        ctx->SetTextPaint(Colors::Gray);
        ctx->SetFontSize(14.0f);
        Point2Di textSize = ctx->GetTextDimension(text);
        Point2Di textPos(
                rect.x + (rect.width - textSize.x) / 2,
                rect.y + (rect.height + textSize.y) / 2
        );
        ctx->DrawText(text, textPos);
    }

    void UltraCanvasImageElement::HandleMouseDown(const UCEvent &event) {
        if (!Contains(event.pointer)) return;

        if (clickable && onClick) {
            onClick();
        }

        if (draggable) {
            isDragging = true;
            dragStartPos = Point2Di(event.pointer.x, event.pointer.y);
        }
    }

    void UltraCanvasImageElement::HandleMouseMove(const UCEvent &event) {
        if (isDragging && draggable) {
            Point2Di currentPos(event.pointer.x, event.pointer.y);
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