// core/UltraCanvasVideoTile.cpp
// A live picture fed frame by frame from any thread
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "UltraCanvasVideoTile.h"
#include "UltraCanvasApplication.h"

#include <algorithm>
#include <cstring>

namespace UltraCanvas {

UltraCanvasVideoTile::UltraCanvasVideoTile(const std::string& identifier, long x, long y, long w, long h)
    : UltraCanvasUIElement(identifier, x, y, w, h), inbox(std::make_shared<Inbox>()) {
    inbox->owner = this;
}

UltraCanvasVideoTile::~UltraCanvasVideoTile() {
    std::lock_guard<std::mutex> lock(inbox->mutex);
    inbox->owner = nullptr;
}

void UltraCanvasVideoTile::PushFrame(const uint8_t* pixels, int width, int height, int stride,
                                     VideoTilePixelFormat format) {
    if (!pixels || width <= 0 || height <= 0) return;
    if (stride <= 0) stride = width * 4;
    bool post = false;
    {
        std::lock_guard<std::mutex> lock(inbox->mutex);
        // Stored as BGRA, the pixmap's byte order, so the UI thread only copies.
        inbox->pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        for (int row = 0; row < height; ++row) {
            const uint8_t* src = pixels + static_cast<size_t>(row) * static_cast<size_t>(stride);
            uint8_t* dst = inbox->pixels.data() + static_cast<size_t>(row) * static_cast<size_t>(width) * 4;
            if (format == VideoTilePixelFormat::BGRA32) {
                std::memcpy(dst, src, static_cast<size_t>(width) * 4);
            } else {
                for (int col = 0; col < width; ++col) {
                    dst[col * 4 + 0] = src[col * 4 + 2];
                    dst[col * 4 + 1] = src[col * 4 + 1];
                    dst[col * 4 + 2] = src[col * 4 + 0];
                    dst[col * 4 + 3] = src[col * 4 + 3];
                }
            }
        }
        inbox->width = width;
        inbox->height = height;
        inbox->fresh = true;
        inbox->cleared = false;
        if (!inbox->uploadPosted) {
            inbox->uploadPosted = true;
            post = true;
        }
    }
    if (!post) return;
    // One upload in flight at a time; frames arriving meanwhile replace the
    // pending one. Without an application the next Render uploads it.
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (!app) {
        std::lock_guard<std::mutex> lock(inbox->mutex);
        inbox->uploadPosted = false;
        return;
    }
    std::shared_ptr<Inbox> box = inbox;
    app->PostToUIThread([box]() {
        UltraCanvasVideoTile* tile = nullptr;
        {
            std::lock_guard<std::mutex> lock(box->mutex);
            box->uploadPosted = false;
            tile = box->owner;      // the element lives on this thread: no race with its destructor
        }
        if (!tile) return;
        tile->Upload();
        tile->RequestRedraw();
    });
}

void UltraCanvasVideoTile::ClearFrame() {
    bool post = false;
    {
        std::lock_guard<std::mutex> lock(inbox->mutex);
        inbox->cleared = true;
        inbox->fresh = false;
        if (!inbox->uploadPosted) {
            inbox->uploadPosted = true;
            post = true;
        }
    }
    if (!post) return;
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (!app) {
        std::lock_guard<std::mutex> lock(inbox->mutex);
        inbox->uploadPosted = false;
        return;
    }
    std::shared_ptr<Inbox> box = inbox;
    app->PostToUIThread([box]() {
        UltraCanvasVideoTile* tile = nullptr;
        {
            std::lock_guard<std::mutex> lock(box->mutex);
            box->uploadPosted = false;
            tile = box->owner;
        }
        if (!tile) return;
        tile->Upload();
        tile->RequestRedraw();
    });
}

bool UltraCanvasVideoTile::HasFrame() const {
    std::lock_guard<std::mutex> lock(inbox->mutex);
    return !inbox->cleared && (inbox->fresh || pixmap != nullptr);
}

void UltraCanvasVideoTile::Upload() {
    std::lock_guard<std::mutex> lock(inbox->mutex);
    if (inbox->cleared) {
        inbox->cleared = false;
        pixmap.reset();
        frameWidth = frameHeight = 0;
        return;
    }
    if (!inbox->fresh) return;
    inbox->fresh = false;
    const int w = inbox->width;
    const int h = inbox->height;
    if (!pixmap || frameWidth != w || frameHeight != h) {
        pixmap = std::make_shared<UCPixmap>();
        if (!pixmap->Init(w, h)) {
            pixmap.reset();
            return;
        }
        frameWidth = w;
        frameHeight = h;
    }
    auto* dst = reinterpret_cast<uint8_t*>(pixmap->GetPixelData());
    if (!dst) return;
    const size_t rowBytes = static_cast<size_t>(w) * 4;     // the pixmap's rows: 32-bit pixels, no padding
    for (int row = 0; row < h; ++row) {
        const uint8_t* src = inbox->pixels.data() + static_cast<size_t>(row) * rowBytes;
        uint8_t* out = dst + static_cast<size_t>(row) * rowBytes;
        if (!mirrored) {
            std::memcpy(out, src, rowBytes);
        } else {
            for (int col = 0; col < w; ++col)
                std::memcpy(out + static_cast<size_t>(col) * 4, src + static_cast<size_t>(w - 1 - col) * 4, 4);
        }
    }
    pixmap->MarkDirty();
}

void UltraCanvasVideoTile::SetCaption(const std::string& text) {
    if (caption == text) return;
    caption = text;
    RequestRedraw();
}

void UltraCanvasVideoTile::SetFit(VideoTileFit newFit) {
    if (fit == newFit) return;
    fit = newFit;
    RequestRedraw();
}

void UltraCanvasVideoTile::SetMirrored(bool value) {
    if (mirrored == value) return;
    mirrored = value;
    // The next frame comes out the other way round.
    RequestRedraw();
}

void UltraCanvasVideoTile::SetStyle(const VideoTileStyle& newStyle) {
    style = newStyle;
    RequestRedraw();
}

void UltraCanvasVideoTile::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx) return;
    (void)dirtyRect;
    Upload();       // a frame pushed with no application loop to post to
    const Rect2Df b = GetLocalBounds();
    const Rect2Dd box(b.x, b.y, b.width, b.height);
    const double r = style.cornerRadius;

    ctx->PushState();
    if (r > 0) ctx->ClipRoundedRectangle(box, r, r, r, r);
    else ctx->ClipRect(box);
    ctx->DrawFilledRectangle(box, style.backgroundColor, 0.0f, Color(0, 0, 0, 0), static_cast<float>(r));

    if (pixmap && pixmap->IsValid() && frameWidth > 0 && frameHeight > 0) {
        const double sx = box.width / frameWidth;
        const double sy = box.height / frameHeight;
        const double s = fit == VideoTileFit::Cover ? std::max(sx, sy) : std::min(sx, sy);
        const double w = frameWidth * s;
        const double h = frameHeight * s;
        ctx->DrawPixmap(*pixmap, Rect2Dd(box.x + (box.width - w) / 2, box.y + (box.height - h) / 2, w, h),
                        ImageFitMode::Fill);
    } else if (!caption.empty()) {
        ctx->SetFontSize(style.captionFontSize);
        ctx->SetTextPaint(style.captionColor);
        ctx->SetTextAlignment(TextAlignment::Center);
        const int textH = ctx->GetTextLineHeight(caption);
        ctx->DrawTextInRect(caption, Rect2Dd(box.x, box.y + (box.height - textH) / 2.0, box.width, textH + 2));
    }
    ctx->PopState();

    if (style.borderWidth > 0 && style.borderColor.a > 0)
        ctx->DrawFilledRectangle(box, Color(0, 0, 0, 0), style.borderWidth, style.borderColor, static_cast<float>(r));
}

} // namespace UltraCanvas
