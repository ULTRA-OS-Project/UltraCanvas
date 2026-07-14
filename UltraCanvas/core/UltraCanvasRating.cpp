// core/UltraCanvasRating.cpp
// Platform-independent rating control implementation.
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework

#include "UltraCanvasRating.h"
#include "UltraCanvasApplication.h"
#include <cmath>
#include <vector>

namespace UltraCanvas {

    UltraCanvasRating::UltraCanvasRating(const std::string& identifier,
                                         float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        mouseCursor = UCMouseCursor::Default;
    }

// ===================================================================
// MODEL
// ===================================================================

    float UltraCanvasRating::SnapToStep(float v) const {
        float step = Step();
        float snapped = std::round(v / step) * step;
        return std::max(0.0f, std::min(static_cast<float>(maxRating), snapped));
    }

    void UltraCanvasRating::SetMaxRating(int count) {
        maxRating = std::max(1, count);
        value = std::min(value, static_cast<float>(maxRating));
        RequestRedraw();
    }

    void UltraCanvasRating::SetValue(float v, bool runCallback) {
        float nv = SnapToStep(v);
        if (std::abs(nv - value) > 0.0001f) {
            value = nv;
            if (runCallback && onRatingChanged) onRatingChanged(value);
            RequestRedraw();
        }
    }

    void UltraCanvasRating::SetAllowHalf(bool allow) {
        allowHalf = allow;
        value = SnapToStep(value);   // re-snap (rounds to whole when disabling half)
        RequestRedraw();
    }

    void UltraCanvasRating::SetCustomSymbols(const std::string& on,
                                             const std::string& off,
                                             const std::string& half) {
        onPath = on;
        offPath = off;
        halfPath = half;
        onImage   = on.empty()   ? nullptr : UCImage::Get(on);
        offImage  = off.empty()  ? nullptr : UCImage::Get(off);
        halfImage = half.empty() ? nullptr : UCImage::Get(half);
        symbol = on.empty() ? RatingSymbol::Star : RatingSymbol::Custom;
        RequestRedraw();
    }

// ===================================================================
// GEOMETRY
// ===================================================================

    Rect2Df UltraCanvasRating::SymbolRect(int index) const {
        Rect2Df b = GetLocalBounds();
        float size = style.symbolSize;
        float cell = size + style.spacing;
        float x = b.x + index * cell;
        float y = b.y + std::max(0.0f, (b.height - size) / 2.0f);
        return Rect2Df(x, y, size, size);
    }

    float UltraCanvasRating::ValueAtPointer(float localX) const {
        if (localX < 0.0f) return 0.0f;
        float cell = style.symbolSize + style.spacing;
        int idx = static_cast<int>(std::floor(localX / cell));
        if (idx < 0) idx = 0;
        if (idx >= maxRating) idx = maxRating - 1;
        float within = localX - idx * cell;
        float v = (allowHalf && within < style.symbolSize / 2.0f) ? (idx + 0.5f)
                                                                  : (idx + 1.0f);
        return std::max(0.0f, std::min(static_cast<float>(maxRating), v));
    }

// ===================================================================
// RENDERING
// ===================================================================

    void UltraCanvasRating::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        float displayValue = (hoverValue >= 0.0f) ? hoverValue : value;
        bool hovering = (hoverValue >= 0.0f);

        for (int i = 0; i < maxRating; ++i) {
            Rect2Df r = SymbolRect(i);
            float fill = std::max(0.0f, std::min(1.0f, displayValue - static_cast<float>(i)));
            RenderSymbol(ctx, r, fill, hovering);
        }
    }

    void UltraCanvasRating::RenderSymbol(IRenderContext* ctx, const Rect2Df& rect,
                                         float fill, bool hovered) {
        if (UseCustom()) {
            Color tint = hovered ? style.hoverColor : style.onColor;
            if (fill >= 1.0f) {
                DrawCustomImage(ctx, onImage, rect, tint);
            } else if (fill <= 0.0f) {
                DrawCustomImage(ctx, offImage, rect, style.offColor);
            } else if (halfImage) {
                DrawCustomImage(ctx, halfImage, rect, tint);
            } else {
                // Synthesise the half state: "on" clipped to its left half over "off".
                DrawCustomImage(ctx, offImage, rect, style.offColor);
                ctx->PushState();
                ctx->ClipRect(Rect2Dd(rect.x, rect.y, rect.width * fill, rect.height));
                DrawCustomImage(ctx, onImage, rect, tint);
                ctx->PopState();
            }
            return;
        }

        // Built-in shape: empty base, then the filled portion clipped to `fill`.
        DrawBuiltinShape(ctx, rect, style.offColor);
        if (fill > 0.0f) {
            Color fc = hovered ? style.hoverColor : style.onColor;
            ctx->PushState();
            ctx->ClipRect(Rect2Dd(rect.x, rect.y, rect.width * std::min(fill, 1.0f), rect.height));
            DrawBuiltinShape(ctx, rect, fc);
            ctx->PopState();
        }
    }

    void UltraCanvasRating::DrawBuiltinShape(IRenderContext* ctx, const Rect2Df& rect,
                                             const Color& color) {
        float cx = rect.x + rect.width / 2.0f;
        float cy = rect.y + rect.height / 2.0f;
        float R = std::min(rect.width, rect.height) * 0.48f;

        switch (symbol) {
            case RatingSymbol::Circle: {
                // Draw the disc as a filled polygon via FillLinePath rather than
                // FillCircle. RenderSymbol paints the "on" state by re-drawing the
                // symbol inside a ClipRect (clipped to the filled fraction); some
                // render back-ends fill an arc/circle path incorrectly when a clip
                // region is active, so the fill state of a circle rating never
                // appeared while the (unclipped) empty base still did. FillLinePath
                // is the same primitive the Star uses and clips correctly, so this
                // keeps all built-in shapes on one code path that honours the clip.
                std::vector<Point2Dd> pts;
                const int segments = 48;
                float rr = R * 0.92f;
                pts.reserve(segments);
                for (int i = 0; i < segments; ++i) {
                    float ang = (i / static_cast<float>(segments)) * 2.0f * 3.14159265f;
                    pts.emplace_back(cx + rr * std::cos(ang), cy + rr * std::sin(ang));
                }
                ctx->SetFillPaint(color);
                ctx->FillLinePath(pts);
                if (style.borderWidth > 0.0f) {
                    ctx->SetStrokePaint(style.borderColor);
                    ctx->SetStrokeWidth(style.borderWidth);
                    ctx->DrawLinePath(pts, true);
                }
                break;
            }
            case RatingSymbol::Square: {
                Rect2Dd sq(cx - R * 0.85f, cy - R * 0.85f, R * 1.7f, R * 1.7f);
                ctx->DrawFilledRectangle(sq, color, style.borderWidth,
                                         style.borderWidth > 0.0f ? style.borderColor
                                                                  : Colors::Transparent,
                                         R * 0.25f);
                break;
            }
            case RatingSymbol::Star:
            default: {
                std::vector<Point2Dd> pts;
                pts.reserve(10);
                float innerR = R * 0.42f;
                for (int i = 0; i < 10; ++i) {
                    float ang = (-90.0f + i * 36.0f) * 3.14159265f / 180.0f;
                    float rr = (i % 2 == 0) ? R : innerR;
                    pts.emplace_back(cx + rr * std::cos(ang), cy + rr * std::sin(ang));
                }
                ctx->SetFillPaint(color);
                ctx->FillLinePath(pts);
                if (style.borderWidth > 0.0f) {
                    ctx->SetStrokePaint(style.borderColor);
                    ctx->SetStrokeWidth(style.borderWidth);
                    ctx->DrawLinePath(pts, true);
                }
                break;
            }
        }
    }

    void UltraCanvasRating::DrawCustomImage(IRenderContext* ctx,
                                            const std::shared_ptr<UCImage>& img,
                                            const Rect2Df& rect, const Color& tint) {
        if (!img) return;
        Rect2Dd r(rect.x, rect.y, rect.width, rect.height);
        if (style.tintCustom) {
            ctx->DrawMask(tint, *img, r, ImageFitMode::Contain);
        } else {
            ctx->DrawImage(*img, r, ImageFitMode::Contain);
        }
    }

// ===================================================================
// EVENTS
// ===================================================================

    bool UltraCanvasRating::OnEvent(const UCEvent& event) {
        if (!IsVisible() || IsDisabled()) return false;
        if (UltraCanvasUIElement::OnEvent(event)) return true;
        if (readOnly) return false;

        switch (event.type) {
            case UCEventType::MouseMove: {
                float v = ValueAtPointer(static_cast<float>(event.pointer.x));
                mouseCursor = UCMouseCursor::Hand;
                if (std::abs(v - hoverValue) > 0.0001f) {
                    hoverValue = v;
                    if (onHoverChanged) onHoverChanged(hoverValue);
                    RequestRedraw();
                }
                return true;
            }
            case UCEventType::MouseLeave: {
                if (hoverValue >= 0.0f) {
                    hoverValue = -1.0f;
                    if (onHoverChanged) onHoverChanged(-1.0f);
                    RequestRedraw();
                }
                return true;
            }
            case UCEventType::MouseDown: {
                SetFocus(true);
                float v = ValueAtPointer(static_cast<float>(event.pointer.x));
                if (allowClear && std::abs(v - value) < 0.001f) {
                    SetValue(0.0f, true);
                } else {
                    SetValue(v, true);
                }
                return true;
            }
            case UCEventType::KeyDown:
                return HandleKeyDown(event);
            default:
                break;
        }
        return false;
    }

    bool UltraCanvasRating::HandleKeyDown(const UCEvent& event) {
        if (!IsFocused()) return false;
        float step = Step();
        switch (event.virtualKey) {
            case UCKeys::Right:
            case UCKeys::Up:
                SetValue(value + step, true);
                return true;
            case UCKeys::Left:
            case UCKeys::Down:
                SetValue(value - step, true);
                return true;
            case UCKeys::Home:
                SetValue(0.0f, true);
                return true;
            case UCKeys::End:
                SetValue(static_cast<float>(maxRating), true);
                return true;
            default:
                break;
        }
        return false;
    }

} // namespace UltraCanvas
