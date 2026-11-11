// Apps/UltraSnip/ui/UltraSnipOverlay.cpp
// See the header.
// Version: 0.4.0 - Window mode: the desktop darkened, the window under the pointer in full
//                  colour (no tint), lit as soon as the overlay opens under the pointer
// Version: 0.3.0 - SetMode while open; onReleased, for the bar kept above it
// Version: 0.2.0 - the texts go through Tr (UltraLocalisation)
// Version: 0.1.1 - the frozen screen shown from its pixels (UCImage::FromBgra), no PNG round trip
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraSnipOverlay.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cmath>

using namespace UltraCanvas;

namespace UltraSnip {

namespace {
const Color kDim(0, 0, 0, 110);
// Window mode: the desktop darker still, so the window under the pointer -
// in its own colours, untinted - is plainly the one a click takes
const Color kWindowDim(0, 0, 0, 170);
const Color kFrame(255, 255, 255, 255);
const Color kFrameShadow(0, 0, 0, 160);
const Color kCrosshair(255, 255, 255, 150);
const Color kWindowFrame(80, 160, 255, 255);
const Color kPillBackground(20, 20, 20, 210);
const Color kPillText(255, 255, 255, 255);
constexpr float kPillPadX = 10.0f;
constexpr float kPillPadY = 5.0f;
constexpr float kPillRadius = 6.0f;
} // namespace

// ===== THE MARQUEE =====

UltraSnipSelectionElement::UltraSnipSelectionElement(const std::string& identifier)
    : UltraCanvasUIElement(identifier, 0, 0, 0, 0) {}

int UltraSnipSelectionElement::WindowUnder(const Point2Di& point) const {
    for (int i = static_cast<int>(windows_.size()) - 1; i >= 0; --i) {
        const Rect2Df& r = windows_[static_cast<size_t>(i)].bounds;
        if (r.width <= 0 || r.height <= 0) continue;
        if (point.x >= r.x && point.x < r.x + r.width && point.y >= r.y && point.y < r.y + r.height) return i;
    }
    return -1;
}

bool UltraSnipSelectionElement::Finish(const Rect2Df& rect) {
    if (done_ || rect.width <= 0 || rect.height <= 0) return false;
    done_ = true;
    if (onSelected) onSelected(rect);
    return true;
}

void UltraSnipSelectionElement::SelectCurrent() {
    if (done_) return;
    if (dragging_) {
        const Rect2Di r = RectBetween(anchor_, current_);
        Finish(Rect2Df(r));
        return;
    }
    if (mode_ == SnipMode::Window && hoverWindow_ >= 0) {
        Finish(windows_[static_cast<size_t>(hoverWindow_)].bounds);
        return;
    }
    Finish(Rect2Df(0, 0, GetWidth(), GetHeight()));
}

void UltraSnipSelectionElement::SetMode(SnipMode mode) {
    mode_ = mode;
    hoverWindow_ = mode_ == SnipMode::Window && pointerKnown_ && !dragging_ ? WindowUnder(current_) : -1;
    RequestRedraw();
}

void UltraSnipSelectionElement::SetPointer(const Point2Di& point) {
    if (done_ || dragging_ || pointerKnown_) return;   // a real movement already said where it is
    pointerKnown_ = true;
    current_ = point;
    if (mode_ == SnipMode::Window) hoverWindow_ = WindowUnder(current_);
    RequestRedraw();
}

bool UltraSnipSelectionElement::OnEvent(const UCEvent& event) {
    if (done_) return false;
    switch (event.type) {
        case UCEventType::MouseDown:
            if (event.button == UCMouseButton::Right) {
                done_ = true;
                if (onCancelled) onCancelled();
                return true;
            }
            if (event.button != UCMouseButton::Left) return false;
            dragging_ = true;
            anchor_ = current_ = event.pointer;
            RequestRedraw();
            return true;
        case UCEventType::MouseEnter:   // the overlay opens under the pointer: light its window at once
        case UCEventType::MouseMove:
            pointerKnown_ = true;
            current_ = event.pointer;
            if (mode_ == SnipMode::Window && !dragging_) hoverWindow_ = WindowUnder(current_);
            RequestRedraw();
            return true;
        case UCEventType::MouseLeave:
            pointerKnown_ = false;
            if (!dragging_) hoverWindow_ = -1;
            RequestRedraw();
            return true;
        case UCEventType::MouseUp: {
            if (event.button != UCMouseButton::Left || !dragging_) return false;
            dragging_ = false;
            current_ = event.pointer;
            const Rect2Di r = RectBetween(anchor_, current_);
            // A click (no drag to speak of): the window under it in Window
            // mode, nothing in Rectangle mode
            if (r.width < 4 && r.height < 4) {
                if (mode_ == SnipMode::Window) {
                    const int under = WindowUnder(current_);
                    if (under >= 0) Finish(windows_[static_cast<size_t>(under)].bounds);
                }
                RequestRedraw();
                return true;
            }
            Finish(Rect2Df(r));
            return true;
        }
        default:
            return false;
    }
}

void UltraSnipSelectionElement::DrawDim(IRenderContext* ctx, const Rect2Df& hole) {
    const float w = GetWidth(), h = GetHeight();
    ctx->SetFillPaint(mode_ == SnipMode::Window ? kWindowDim : kDim);
    if (hole.width <= 0 || hole.height <= 0) {
        ctx->FillRectangle(Rect2Dd(0, 0, w, h));
        return;
    }
    const float left = std::max(0.0f, hole.x), top = std::max(0.0f, hole.y);
    const float right = std::min(w, hole.x + hole.width), bottom = std::min(h, hole.y + hole.height);
    if (top > 0) ctx->FillRectangle(Rect2Dd(0, 0, w, top));
    if (bottom < h) ctx->FillRectangle(Rect2Dd(0, bottom, w, h - bottom));
    if (left > 0) ctx->FillRectangle(Rect2Dd(0, top, left, bottom - top));
    if (right < w) ctx->FillRectangle(Rect2Dd(right, top, w - right, bottom - top));
}

void UltraSnipSelectionElement::DrawPill(IRenderContext* ctx, const std::string& text, float centreX, float y,
                                         bool below) {
    ctx->SetFontSize(13.0);
    const Point2Di size = ctx->GetTextDimension(text);
    const float pw = size.x + 2 * kPillPadX, ph = size.y + 2 * kPillPadY;
    float px = centreX - pw / 2;
    px = std::clamp(px, 4.0f, std::max(4.0f, GetWidth() - pw - 4));
    float py = below ? y : y - ph;
    py = std::clamp(py, 4.0f, std::max(4.0f, GetHeight() - ph - 4));
    ctx->SetFillPaint(kPillBackground);
    ctx->FillRoundedRectangle(Rect2Dd(px, py, pw, ph), kPillRadius);
    ctx->SetTextPaint(kPillText);
    ctx->SetTextAlignment(TextAlignment::Center);
    ctx->DrawTextInRect(text, Rect2Dd(px, py, pw, ph));
}

void UltraSnipSelectionElement::Render(IRenderContext* ctx, const Rect2Df&) {
    const float w = GetWidth(), h = GetHeight();
    if (w <= 0 || h <= 0) return;
    ctx->PushState();

    Rect2Df hole(0, 0, 0, 0);
    if (dragging_) {
        hole = Rect2Df(RectBetween(anchor_, current_));
    } else if (mode_ == SnipMode::Window && hoverWindow_ >= 0) {
        hole = windows_[static_cast<size_t>(hoverWindow_)].bounds;
    }
    DrawDim(ctx, hole);

    if (hole.width > 0 && hole.height > 0) {
        const bool window = !dragging_;
        ctx->SetStrokeWidth(3.0);
        ctx->SetStrokePaint(kFrameShadow);
        ctx->DrawRectangle(Rect2Dd(hole.x - 1, hole.y - 1, hole.width + 2, hole.height + 2));
        ctx->SetStrokeWidth(1.5);
        ctx->SetStrokePaint(window ? kWindowFrame : kFrame);
        ctx->DrawRectangle(Rect2Dd(hole.x, hole.y, hole.width, hole.height));
        const int pw = static_cast<int>(std::lround(hole.width * scale_));
        const int ph = static_cast<int>(std::lround(hole.height * scale_));
        std::string label = std::to_string(pw) + " \xC3\x97 " + std::to_string(ph);
        if (window) {
            const std::string& title = windows_[static_cast<size_t>(hoverWindow_)].title;
            if (!title.empty()) label = title + "  -  " + label;
        }
        const bool below = hole.y + hole.height + 30 < h;
        DrawPill(ctx, label, hole.x + hole.width / 2, below ? hole.y + hole.height + 6 : hole.y - 6, below);
    } else if (pointerKnown_ && mode_ == SnipMode::Rectangle) {
        ctx->SetStrokeWidth(1.0);
        ctx->SetStrokePaint(kCrosshair);
        ctx->DrawLine(Point2Dd(current_.x + 0.5, 0), Point2Dd(current_.x + 0.5, h));
        ctx->DrawLine(Point2Dd(0, current_.y + 0.5), Point2Dd(w, current_.y + 0.5));
    }

    const std::string hint = mode_ == SnipMode::Window
                           ? Tr("Click a window, or drag an area  \xC2\xB7  Enter: the window under the pointer  \xC2\xB7  Esc: cancel")
                           : Tr("Drag an area  \xC2\xB7  Enter: the whole screen  \xC2\xB7  Esc: cancel");
    DrawPill(ctx, hint, w / 2, hintTop_, true);
    ctx->PopState();
}

// ===== THE OVERLAY WINDOW =====

UltraSnipOverlay::UltraSnipOverlay() = default;

UltraSnipOverlay::~UltraSnipOverlay() {
    if (window_) {
        window_->onWindowClosed = nullptr;
        if (open_) window_->Close();
    }
}

bool UltraSnipOverlay::Open(const DesktopScreenImage& shot, SnipMode mode, const std::vector<SnipWindowBounds>& windows,
                            std::string* error) {
    Close();
    if (!shot.IsValid()) {
        if (error) *error = Tr("There is no screen picture to choose from.");
        return false;
    }
    // The capture as the picture the image element shows: its B G R x rows
    // taken as they are, no encoding and no decoding on the way
    std::shared_ptr<UCImage> picture =
        UCImage::FromBgra(shot.pixels.data(), shot.width, shot.height, shot.stride, /*opaque=*/true);
    if (!picture) {
        if (error) *error = Tr("The screen picture could not be prepared.");
        return false;
    }

    WindowConfig config;
    config.title = "UltraSnip";
    config.type = WindowType::Borderless;
    config.width = shot.width;
    config.height = shot.height;
    config.x = shot.x;
    config.y = shot.y;
    config.resizable = false;
    config.minimizable = false;
    config.maximizable = false;
    config.minWidth = 1;
    config.minHeight = 1;
    config.backgroundColor = Colors::Black;
    window_ = CreateWindow(config);
    if (!window_) {
        if (error) *error = Tr("The selection window could not be opened.");
        return false;
    }
    shotX_ = shot.x;
    shotY_ = shot.y;
    scale_ = window_->GetDeviceScale() > 0.0f ? window_->GetDeviceScale() : 1.0f;
    // The window covers the capture's pixels exactly (SetWindowSize takes
    // physical pixels, as the capture is)
    window_->SetWindowPosition(shot.x, shot.y);
    window_->SetWindowSize(shot.width, shot.height);

    const CSSLayout::Position fill{CSSLayout::Dimension::Px(0), CSSLayout::Dimension::Px(0),
                                               CSSLayout::Dimension::Px(0), CSSLayout::Dimension::Px(0)};
    picture_ = CreateImageElement("snip.overlay.picture", 0, 0, static_cast<float>(shot.width),
                                  static_cast<float>(shot.height));
    picture_->layoutItem.SetPositionType(CSSLayout::PositionType::Absolute).SetPositionInsets(fill);
    picture_->SetFitMode(ImageFitMode::Fill);
    picture_->LoadFromImage(picture);
    window_->AddChild(picture_);

    marquee_ = std::make_shared<UltraSnipSelectionElement>("snip.overlay.marquee");
    marquee_->layoutItem.SetPositionType(CSSLayout::PositionType::Absolute).SetPositionInsets(fill);
    marquee_->SetMode(mode);
    marquee_->SetScale(scale_);
    std::vector<OverlayWindow> logical;
    for (const SnipWindowBounds& w : windows) {
        OverlayWindow ow;
        ow.title = w.title;
        ow.bounds = Rect2Df((w.bounds.x - shot.x) / scale_, (w.bounds.y - shot.y) / scale_, w.bounds.width / scale_,
                            w.bounds.height / scale_);
        logical.push_back(ow);
    }
    marquee_->SetWindows(std::move(logical));
    marquee_->onSelected = [this](const Rect2Df& r) {
        const Rect2Di area(static_cast<int>(std::lround(r.x * scale_)) + shotX_,
                           static_cast<int>(std::lround(r.y * scale_)) + shotY_,
                           std::max(1, static_cast<int>(std::lround(r.width * scale_))),
                           std::max(1, static_cast<int>(std::lround(r.height * scale_))));
        Finish(true, area);
    };
    marquee_->onCancelled = [this]() { Finish(false, Rect2Di()); };
    window_->AddChild(marquee_);

    open_ = true;
    window_->onWindowClosed = [this]() {
        const bool wasOpen = open_;
        open_ = false;
        window_.reset();
        picture_.reset();
        marquee_.reset();
        if (wasOpen && onCancelled) onCancelled();
    };
    window_->InstallEventFilter("snip-overlay-keys", [this](const UCEvent& event) {
        if (event.type != UCEventType::KeyDown || !marquee_) return false;
        if (event.virtualKey == UCKeys::Escape) {
            Finish(false, Rect2Di());
            return true;
        }
        if (event.virtualKey == UCKeys::Return || event.virtualKey == UCKeys::Space) {
            marquee_->SelectCurrent();
            return true;
        }
        return false;
    }, {UCEventType::KeyDown});
    // Ahead of the marquee, which then has the release as well
    window_->InstallEventFilter("snip-overlay-release", [this](const UCEvent&) {
        if (open_ && onReleased) onReleased();
        return false;
    }, {UCEventType::MouseUp});
    // The overlay opens under the pointer, which has not moved: the window
    // is told where it is when it appears (its enter event), and the
    // marquee lights the window under it from that
    window_->InstallEventFilter("snip-overlay-enter", [this](const UCEvent& event) {
        if (event.type == UCEventType::MouseEnter && marquee_) marquee_->SetPointer(event.pointerWindow);
        return false;
    }, {UCEventType::MouseEnter});
    window_->Show();
    window_->RaiseAndFocus();
    return true;
}

void UltraSnipOverlay::Finish(bool selected, const Rect2Di& area) {
    if (!open_) return;
    open_ = false;
    if (window_) {
        window_->onWindowClosed = nullptr;
        window_->Close();
    }
    window_.reset();
    picture_.reset();
    marquee_.reset();
    if (selected) {
        if (onSelected) onSelected(area);
    } else if (onCancelled) {
        onCancelled();
    }
}

void UltraSnipOverlay::SetMode(SnipMode mode) {
    if (marquee_) marquee_->SetMode(mode);
}

void UltraSnipOverlay::SetHintTop(float top) {
    if (marquee_) marquee_->SetHintTop(top);
}

void UltraSnipOverlay::Close() {
    if (!window_) return;
    open_ = false;
    window_->onWindowClosed = nullptr;
    window_->Close();
    window_.reset();
    picture_.reset();
    marquee_.reset();
}

} // namespace UltraSnip
