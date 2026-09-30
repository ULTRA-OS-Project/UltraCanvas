// core/UltraCanvasImageElement.cpp
// Image display component with loading, caching, and transformation support
// Version: 1.3.0 - a repeating image (SetImageRepeat) is one pattern fill over the
//                 tiled area; drawn tile by tile where a backend has no patterns
// Version: 1.2.0 - an image positioned off-centre (SetImagePosition) is drawn into
//                 ImageDrawRect, clipped to the content box
// Last Modified: 2026-09-30
// Author: UltraCanvas Framework

#include "UltraCanvasImageElement.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasFileError.h"
#include "CSSLayout/LayoutUtils.h"
#include <algorithm>
#include <cmath>
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
        animator.onFrameChanged = [this]() { RequestRedraw(); };
    }

    UltraCanvasImageElement::UltraCanvasImageElement(const std::string &identifier, float w, float h)
            : UltraCanvasUIElement(identifier, w, h) {
        animator.onFrameChanged = [this]() { RequestRedraw(); };
    }

    UltraCanvasImageElement::UltraCanvasImageElement(const std::string &identifier)
            : UltraCanvasUIElement(identifier) {
        animator.onFrameChanged = [this]() { RequestRedraw(); };
    }

    void UltraCanvasImageElement::SetupAnimation() {
        animator.SetAnimation(nullptr);
        if (!animationEnabled || !loadedImage || !loadedImage->IsValid() ||
            !loadedImage->IsAnimated()) {
            return;
        }
        auto anim = loadedImage->GetAnimation();   // lazy full decode, shared + cached
        if (anim && anim->GetFrameCount() > 1) {
            animator.SetAnimation(anim);
            animator.Play();
        }
    }

    void UltraCanvasImageElement::SetAnimationEnabled(bool enable) {
        if (animationEnabled == enable) return;
        animationEnabled = enable;
        SetupAnimation();          // start playing / fall back to frame 0
        RequestRedraw();
    }

    bool UltraCanvasImageElement::LoadFromFile(const std::string &filePath, bool forceLoad) {
        errorMessage.clear();
        if (forceLoad) {
            UCImage::RemoveFromCache(filePath);
        }
        loadedImage = UCImage::Get(filePath);
        SetupAnimation();
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
        SetupAnimation();
        // Intrinsic size changed — re-measure (for auto-sized elements) and repaint.
        InvalidateLayout();
        RequestRedraw();
        if (loadedImage) {
            return true;
        }
        return false;
    }

    void UltraCanvasImageElement::DrawRepeatedImage(IRenderContext* ctx, const Rect2Df& contentRect) {
        const Rect2Df tile = ImageDrawRect();
        if (tile.width <= 0.f || tile.height <= 0.f) return;
        // The tiled area: the whole content box along a repeating axis, the
        // tile's own row / column along the other.
        Rect2Df area = contentRect;
        if (!repeatX) { area.x = tile.x; area.width = tile.width; }
        if (!repeatY) { area.y = tile.y; area.height = tile.height; }
        const float x0 = std::max(area.x, contentRect.x);
        const float y0 = std::max(area.y, contentRect.y);
        const float x1 = std::min(area.x + area.width, contentRect.x + contentRect.width);
        const float y1 = std::min(area.y + area.height, contentRect.y + contentRect.height);
        if (x1 <= x0 || y1 <= y0) return;
        const Rect2Dd fillRect(x0, y0, x1 - x0, y1 - y0);
        const Rect2Dd anchor(tile.x, tile.y, tile.width, tile.height);

        // The tile's pixels, at its fitted size (the animation's current frame
        // for an animated image).
        std::shared_ptr<UCPixmap> pixmap = animator.GetCurrentFramePixmap();
        if (!pixmap && loadedImage && loadedImage->IsValid()) {
            pixmap = loadedImage->GetPixmap(std::max(1, static_cast<int>(std::lround(tile.width))),
                                            std::max(1, static_cast<int>(std::lround(tile.height))),
                                            ImageFitMode::Fill, ctx->GetDeviceScale());
        }
        if (!pixmap) return;

        ctx->PushState();
        ctx->ClipRect(Rect2Dd(contentRect.x, contentRect.y, contentRect.width, contentRect.height));
        if (auto pattern = ctx->CreatePixmapPattern(*pixmap, anchor, PatternExtend::Repeat)) {
            ctx->SetFillPaint(pattern);
            ctx->FillRectangle(fillRect);
        } else {
            // No pattern support: draw the tiles, within a sane count.
            const double startX = anchor.x - std::ceil((anchor.x - fillRect.x) / anchor.width) * anchor.width;
            const double startY = anchor.y - std::ceil((anchor.y - fillRect.y) / anchor.height) * anchor.height;
            int drawn = 0;
            for (double y = startY; y < fillRect.y + fillRect.height && drawn < 4096; y += anchor.height)
                for (double x = startX; x < fillRect.x + fillRect.width && drawn < 4096; x += anchor.width, ++drawn)
                    ctx->DrawPixmap(*pixmap, Rect2Dd(x, y, anchor.width, anchor.height), ImageFitMode::Fill);
        }
        ctx->PopState();
    }

    Rect2Df UltraCanvasImageElement::ImageDrawRect() const {
        const Size2Df natural = NaturalImageSize();
        if (natural.width <= 0.f || natural.height <= 0.f) return Rect2Df();
        const Rect2Df content = GetLocalContentRect();
        const float cw = content.width, ch = content.height;
        if (cw <= 0.f || ch <= 0.f) return Rect2Df();
        const float fitW = cw / natural.width, fitH = ch / natural.height;
        float w = natural.width, h = natural.height;
        switch (fitMode) {
            case ImageFitMode::Fill:      w = cw; h = ch; break;
            case ImageFitMode::Contain:   { float k = std::min(fitW, fitH); w *= k; h *= k; break; }
            case ImageFitMode::Cover:     { float k = std::max(fitW, fitH); w *= k; h *= k; break; }
            case ImageFitMode::ScaleDown: { float k = std::min(1.f, std::min(fitW, fitH)); w *= k; h *= k; break; }
            case ImageFitMode::NoScale:   break;
        }
        return Rect2Df(content.x + imagePosition.x.OffsetIn(cw, w),
                       content.y + imagePosition.y.OffsetIn(ch, h), w, h);
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
        UltraCanvasUIElement::Render(ctx, dirtyRect);

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
        animator.SetAnimation(nullptr);

        debugOutput << "[UltraCanvasImageElement] Error: " << message << std::endl;

        if (onImageLoadFailed) {
            onImageLoadFailed(message);
        }
    }

    void UltraCanvasImageElement::DrawLoadedImage(IRenderContext *ctx) {
        // Apply global alpha
        ctx->SetAlpha(opacity);
        auto contentRect = GetLocalContentRect();
        // Apply transformations (ctx is already translated to element origin)
        if (rotation != 0.0f || scale.x != 1.0f || scale.y != 1.0f || offset.x != 0.0f || offset.y != 0.0f) {
            ctx->PushState();

            // Translate to center for rotation (element-local center)
            Point2Di center = Point2Di(contentRect.width / 2.0f, contentRect.height / 2.0f);
            ctx->Translate(center.x, center.y);

            // Apply transformations
            if (rotation != 0.0f) ctx->Rotate(rotation * M_PI/180.0);
            if (scale.x != 1.0f || scale.y != 1.0f) ctx->Scale(scale.x, scale.y);
            if (offset.x != 0.0f || offset.y != 0.0f) ctx->Translate(offset.x, offset.y);

            // Translate back
            ctx->Translate(-center.x, -center.y);
        }

        // Repeating: one pattern fill over the tiled area, anchored on the
        // positioned tile.
        if (repeatX || repeatY) {
            DrawRepeatedImage(ctx, contentRect);
        }
        // Positioned off-centre: fit and place the image here, clipped to the
        // content box (the backends centre what they fit).
        else if (!imagePosition.x.IsCentred() || !imagePosition.y.IsCentred()) {
            const Rect2Df dest = ImageDrawRect();
            if (dest.width > 0 && dest.height > 0) {
                ctx->PushState();
                ctx->ClipRect(Rect2Dd(contentRect.x, contentRect.y, contentRect.width, contentRect.height));
                const Rect2Dd d(dest.x, dest.y, dest.width, dest.height);
                if (auto framePm = animator.GetCurrentFramePixmap())
                    ctx->DrawPixmap(*framePm, d, ImageFitMode::Fill);
                else if (loadedImage && loadedImage->IsValid())
                    ctx->DrawImage(*loadedImage.get(), d, ImageFitMode::Fill);
                ctx->PopState();
            }
        }
        // Draw the image using unified rendering (element-local bounds)
        else if (auto framePm = animator.GetCurrentFramePixmap()) {
            // Animated image: draw the controller's current frame directly.
            ctx->DrawPixmap(*framePm, contentRect, fitMode);
        } else if (loadedImage->IsValid()) {
            // Load from file path
            ctx->DrawImage(*loadedImage.get(), contentRect, fitMode);
        } else {
            // For memory-loaded images, we'd need to save to a temporary file
            // or extend the rendering interface to support raw data
            // For now, draw a placeholder
            DrawImagePlaceholder(contentRect, "IMG");
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