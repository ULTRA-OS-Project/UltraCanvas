// core/UltraCanvasScreenAreaPicker.cpp
// See the header.
// Version: 1.0.0 - from UltraSnip's overlay 0.4.0 and its capture helpers;
//                  on macOS placed in points, above the menu bar (WindowType::Overlay)
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework

#include "UltraCanvasScreenAreaPicker.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// ===== THE ARITHMETIC =====

namespace ScreenArea {

Rect2Di RectBetween(const Point2Di& a, const Point2Di& b) {
    const int left = std::min(a.x, b.x);
    const int top = std::min(a.y, b.y);
    const int right = std::max(a.x, b.x);
    const int bottom = std::max(a.y, b.y);
    return Rect2Di(left, top, right - left + 1, bottom - top + 1);
}

Rect2Di ClipRect(const Rect2Di& rect, const Rect2Di& bounds) {
    const int left = std::max(rect.x, bounds.x);
    const int top = std::max(rect.y, bounds.y);
    const int right = std::min(rect.x + rect.width, bounds.x + bounds.width);
    const int bottom = std::min(rect.y + rect.height, bounds.y + bounds.height);
    if (right <= left || bottom <= top) return Rect2Di(left, top, 0, 0);
    return Rect2Di(left, top, right - left, bottom - top);
}

bool CropToRgba(const DesktopScreenImage& shot, const Rect2Di& area, ClipboardDib::RgbaImage& out, Rect2Di* taken) {
    out = ClipboardDib::RgbaImage();
    if (!shot.IsValid()) return false;
    const Rect2Di inside = ClipRect(area, Rect2Di(shot.x, shot.y, shot.width, shot.height));
    if (inside.width <= 0 || inside.height <= 0) return false;
    out.width = inside.width;
    out.height = inside.height;
    out.pixels.resize(static_cast<size_t>(inside.width) * inside.height * 4);
    for (int row = 0; row < inside.height; ++row) {
        const uint8_t* src = shot.pixels.data() +
                             static_cast<size_t>(inside.y - shot.y + row) * static_cast<size_t>(shot.stride) +
                             static_cast<size_t>(inside.x - shot.x) * 4;
        uint8_t* dst = out.pixels.data() + static_cast<size_t>(row) * inside.width * 4;
        for (int col = 0; col < inside.width; ++col, src += 4, dst += 4) {
            dst[0] = src[2];    // the capture is B G R x
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = 255;
        }
    }
    if (taken) *taken = inside;
    return true;
}

int TopmostWindowAt(const std::vector<ScreenAreaWindow>& windows, const Point2Di& point) {
    for (int i = static_cast<int>(windows.size()) - 1; i >= 0; --i) {
        const Rect2Di& r = windows[static_cast<size_t>(i)].bounds;
        if (r.width <= 0 || r.height <= 0) continue;
        if (point.x >= r.x && point.x < r.x + r.width && point.y >= r.y && point.y < r.y + r.height) return i;
    }
    return -1;
}

std::vector<ScreenAreaWindow> ListPickableWindows() {
    std::vector<ScreenAreaWindow> windows;
    for (const DesktopWindowInfo& w : UltraCanvasDesktopShell::ListWindows()) {
        if (w.minimized || w.skipTaskbar) continue;
        ScreenAreaWindow b;
        if (!UltraCanvasDesktopShell::GetWindowBounds(w.id, b.bounds.x, b.bounds.y, b.bounds.width, b.bounds.height))
            continue;
        b.id = w.id;
        b.title = w.title;
        windows.push_back(b);
    }
    return windows;
}

} // namespace ScreenArea

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

UltraCanvasScreenAreaSelection::UltraCanvasScreenAreaSelection(const std::string& identifier)
    : UltraCanvasUIElement(identifier, 0, 0, 0, 0) {}

int UltraCanvasScreenAreaSelection::WindowUnder(const Point2Di& point) const {
    for (int i = static_cast<int>(windows_.size()) - 1; i >= 0; --i) {
        const Rect2Df& r = windows_[static_cast<size_t>(i)].bounds;
        if (r.width <= 0 || r.height <= 0) continue;
        if (point.x >= r.x && point.x < r.x + r.width && point.y >= r.y && point.y < r.y + r.height) return i;
    }
    return -1;
}

bool UltraCanvasScreenAreaSelection::Finish(const Rect2Df& rect) {
    if (done_ || rect.width <= 0 || rect.height <= 0) return false;
    done_ = true;
    if (onSelected) onSelected(rect);
    return true;
}

void UltraCanvasScreenAreaSelection::SelectCurrent() {
    if (done_) return;
    if (dragging_) {
        const Rect2Di r = ScreenArea::RectBetween(anchor_, current_);
        Finish(Rect2Df(r));
        return;
    }
    if (mode_ == ScreenAreaMode::Window && hoverWindow_ >= 0) {
        Finish(windows_[static_cast<size_t>(hoverWindow_)].bounds);
        return;
    }
    Finish(Rect2Df(0, 0, GetWidth(), GetHeight()));
}

void UltraCanvasScreenAreaSelection::SetMode(ScreenAreaMode mode) {
    mode_ = mode;
    hoverWindow_ = mode_ == ScreenAreaMode::Window && pointerKnown_ && !dragging_ ? WindowUnder(current_) : -1;
    RequestRedraw();
}

void UltraCanvasScreenAreaSelection::SetPointer(const Point2Di& point) {
    if (done_ || dragging_ || pointerKnown_) return;   // a real movement already said where it is
    pointerKnown_ = true;
    current_ = point;
    if (mode_ == ScreenAreaMode::Window) hoverWindow_ = WindowUnder(current_);
    RequestRedraw();
}

bool UltraCanvasScreenAreaSelection::OnEvent(const UCEvent& event) {
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
            if (mode_ == ScreenAreaMode::Window && !dragging_) hoverWindow_ = WindowUnder(current_);
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
            const Rect2Di r = ScreenArea::RectBetween(anchor_, current_);
            // A click (no drag to speak of): the window under it in Window
            // mode, nothing in Rectangle mode
            if (r.width < 4 && r.height < 4) {
                if (mode_ == ScreenAreaMode::Window) {
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

void UltraCanvasScreenAreaSelection::DrawDim(IRenderContext* ctx, const Rect2Df& hole) {
    const float w = GetWidth(), h = GetHeight();
    ctx->SetFillPaint(mode_ == ScreenAreaMode::Window ? kWindowDim : kDim);
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

void UltraCanvasScreenAreaSelection::DrawPill(IRenderContext* ctx, const std::string& text, float centreX, float y,
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

void UltraCanvasScreenAreaSelection::Render(IRenderContext* ctx, const Rect2Df&) {
    const float w = GetWidth(), h = GetHeight();
    if (w <= 0 || h <= 0) return;
    ctx->PushState();

    Rect2Df hole(0, 0, 0, 0);
    if (dragging_) {
        hole = Rect2Df(ScreenArea::RectBetween(anchor_, current_));
    } else if (mode_ == ScreenAreaMode::Window && hoverWindow_ >= 0) {
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
    } else if (pointerKnown_ && mode_ == ScreenAreaMode::Rectangle) {
        ctx->SetStrokeWidth(1.0);
        ctx->SetStrokePaint(kCrosshair);
        ctx->DrawLine(Point2Dd(current_.x + 0.5, 0), Point2Dd(current_.x + 0.5, h));
        ctx->DrawLine(Point2Dd(0, current_.y + 0.5), Point2Dd(w, current_.y + 0.5));
    }

    const std::string hint = mode_ == ScreenAreaMode::Window
                           ? Tr("Click a window, or drag an area  \xC2\xB7  Enter: the window under the pointer  \xC2\xB7  Esc: cancel")
                           : Tr("Drag an area  \xC2\xB7  Enter: the whole screen  \xC2\xB7  Esc: cancel");
    DrawPill(ctx, hint, w / 2, hintTop_, true);
    ctx->PopState();
}

// ===== THE OVERLAY WINDOW =====

UltraCanvasScreenAreaPicker::UltraCanvasScreenAreaPicker() = default;

UltraCanvasScreenAreaPicker::~UltraCanvasScreenAreaPicker() {
    if (window_) {
        window_->onWindowClosed = nullptr;
        if (open_) window_->Close();
    }
}

bool UltraCanvasScreenAreaPicker::Open(const DesktopScreenImage& shot, ScreenAreaMode mode, const std::vector<ScreenAreaWindow>& windows,
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
    config.title = title_;
#if defined(__APPLE__)
    // Cocoa keeps a borderless window under the menu bar and the Dock and
    // never makes it key (no Escape); an Overlay is above both and takes
    // the keyboard
    config.type = WindowType::Overlay;
#else
    config.type = WindowType::Borderless;
#endif
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
#if defined(__APPLE__)
    // ...except on macOS, where a window is placed and sized in points: the
    // capture's pixels over the backing scale
    window_->SetWindowPosition(static_cast<int>(std::lround(shot.x / scale_)),
                               static_cast<int>(std::lround(shot.y / scale_)));
    window_->SetWindowSize(static_cast<int>(std::lround(shot.width / scale_)),
                           static_cast<int>(std::lround(shot.height / scale_)));
#else
    window_->SetWindowPosition(shot.x, shot.y);
    window_->SetWindowSize(shot.width, shot.height);
#endif

    const CSSLayout::Position fill{CSSLayout::Dimension::Px(0), CSSLayout::Dimension::Px(0),
                                               CSSLayout::Dimension::Px(0), CSSLayout::Dimension::Px(0)};
    picture_ = CreateImageElement("screen-area.picture", 0, 0, static_cast<float>(shot.width),
                                  static_cast<float>(shot.height));
    picture_->layoutItem.SetPositionType(CSSLayout::PositionType::Absolute).SetPositionInsets(fill);
    picture_->SetFitMode(ImageFitMode::Fill);
    picture_->LoadFromImage(picture);
    window_->AddChild(picture_);

    marquee_ = std::make_shared<UltraCanvasScreenAreaSelection>("screen-area.marquee");
    marquee_->layoutItem.SetPositionType(CSSLayout::PositionType::Absolute).SetPositionInsets(fill);
    marquee_->SetMode(mode);
    marquee_->SetScale(scale_);
    std::vector<UltraCanvasScreenAreaSelection::Window> logical;
    for (const ScreenAreaWindow& w : windows) {
        UltraCanvasScreenAreaSelection::Window ow;
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
    window_->InstallEventFilter("screen-area-keys", [this](const UCEvent& event) {
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
    window_->InstallEventFilter("screen-area-release", [this](const UCEvent&) {
        if (open_ && onReleased) onReleased();
        return false;
    }, {UCEventType::MouseUp});
    // The overlay opens under the pointer, which has not moved: the window
    // is told where it is when it appears (its enter event), and the
    // marquee lights the window under it from that
    window_->InstallEventFilter("screen-area-enter", [this](const UCEvent& event) {
        if (event.type == UCEventType::MouseEnter && marquee_) marquee_->SetPointer(event.pointerWindow);
        return false;
    }, {UCEventType::MouseEnter});
    window_->Show();
    window_->RaiseAndFocus();
    return true;
}

void UltraCanvasScreenAreaPicker::Finish(bool selected, const Rect2Di& area) {
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

void UltraCanvasScreenAreaPicker::SetMode(ScreenAreaMode mode) {
    if (marquee_) marquee_->SetMode(mode);
}

void UltraCanvasScreenAreaPicker::SetHintTop(float top) {
    if (marquee_) marquee_->SetHintTop(top);
}

void UltraCanvasScreenAreaPicker::Close() {
    if (!window_) return;
    open_ = false;
    window_->onWindowClosed = nullptr;
    window_->Close();
    window_.reset();
    picture_.reset();
    marquee_.reset();
}

} // namespace UltraCanvas
