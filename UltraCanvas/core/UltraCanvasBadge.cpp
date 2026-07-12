// core/UltraCanvasBadge.cpp
// Platform-independent badge (count / status / overlay) implementation.
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework

#include "UltraCanvasBadge.h"
#include "UltraCanvasApplication.h"
#include <string>

namespace UltraCanvas {

    UltraCanvasBadge::UltraCanvasBadge(const std::string& identifier,
                                       float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        mouseCursor = UCMouseCursor::Default;
        if (w <= 0 || h <= 0) SetBounds(GetX(), GetY(), style.minWidth, style.height);
    }

    bool UltraCanvasBadge::IsBadgeVisible() const {
        if (dot) return true;
        if (useCount) return count > 0 || showZero;
        return !text.empty();
    }

    std::string UltraCanvasBadge::DisplayText() const {
        if (dot) return "";
        if (useCount) {
            if (count > maxCount) return std::to_string(maxCount) + "+";
            return std::to_string(count);
        }
        return text;
    }

    Color UltraCanvasBadge::VariantColor() const {
        if (hasCustomColor) return customColor;
        switch (variant) {
            case BadgeVariant::Primary: return Color(0, 120, 215, 255);
            case BadgeVariant::Successful: return Color(40, 167, 69, 255);
            case BadgeVariant::Warning: return Color(255, 193, 7, 255);
            case BadgeVariant::Danger:  return Color(220, 53, 69, 255);
            case BadgeVariant::Info:    return Color(23, 162, 184, 255);
            case BadgeVariant::Neutral:
            default:                    return Color(108, 117, 125, 255);
        }
    }

    Color UltraCanvasBadge::CurrentTextColor() const {
        if (!hasCustomColor && variant == BadgeVariant::Warning) return style.warningTextColor;
        return style.textColor;
    }

    Size2Df UltraCanvasBadge::MeasureContent(IRenderContext* ctx) const {
        if (dot) return Size2Df(style.dotSize, style.dotSize);
        FontStyle fs;
        fs.fontFamily = style.fontFamily;
        fs.fontSize = style.fontSize;
        fs.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(fs);
        float tw = static_cast<float>(ctx->GetTextDimension(DisplayText()).x);
        float w = std::max(style.minWidth, 2 * style.paddingH + tw);
        return Size2Df(w, style.height);
    }

    Size2Df UltraCanvasBadge::ContentSize() const {
        IRenderContext* rc = GetRenderContext();
        if (!rc) {
            // Not attached to a window yet — no surface to measure text
            // against. The dot and the minimum pill are style-driven, so a
            // usable size still exists; the first measured pass refines it.
            if (dot) return Size2Df(style.dotSize, style.dotSize);
            return Size2Df(style.minWidth, style.height);
        }
        rc->PushState();
        Size2Df sz = MeasureContent(rc);
        rc->PopState();
        return sz;
    }

    Size2Df UltraCanvasBadge::MeasureOwnContent(std::optional<float> /*definiteContentWidth*/,
                                                const CSSLayout::LayoutContext& /*ctx*/) {
        // The pill never wraps, so the size is constraint-independent.
        return ContentSize();
    }

    void UltraCanvasBadge::ComputeIntrinsicSizes(const CSSLayout::LayoutContext& /*ctx*/) {
        // paddingH is visual style, not CSS box padding/border (both zero
        // here), so the measured pill/dot IS the border-box size.
        Size2Df sz = ContentSize();
        intrinsic.valid = true;
        intrinsic.minContentWidth  = intrinsic.maxContentWidth  = sz.width;
        intrinsic.minContentHeight = intrinsic.maxContentHeight = sz.height;
    }

    Point2Df UltraCanvasBadge::AnchorTopLeft(const Rect2Df& ab, float w, float h) const {
        float cornerX = (corner == BadgeCorner::TopRight || corner == BadgeCorner::BottomRight)
                            ? (ab.x + ab.width) : ab.x;
        float cornerY = (corner == BadgeCorner::BottomLeft || corner == BadgeCorner::BottomRight)
                            ? (ab.y + ab.height) : ab.y;
        return Point2Df(cornerX - w / 2.0f + anchorOffsetX,
                        cornerY - h / 2.0f + anchorOffsetY);
    }

    void UltraCanvasBadge::AnchorTo(const std::shared_ptr<UltraCanvasUIElement>& anchorElement,
                                    BadgeCorner c, float offsetX, float offsetY) {
        anchor = anchorElement;
        corner = c;
        anchorOffsetX = offsetX;
        anchorOffsetY = offsetY;
        if (anchorElement) {
            // Take the badge out of flow: it overlays a sibling, so the
            // parent's layout must neither stack it as a flow child nor let
            // it displace other children. Seed the engine position from the
            // anchor's current bounds (text metrics may be unavailable yet);
            // Render keeps it in sync as sizes resolve and the anchor moves.
            Size2Df sz = ContentSize();
            Point2Df tl = AnchorTopLeft(anchorElement->GetBounds(), sz.width, sz.height);
            if (!layoutItem.position) layoutItem.position = CSSLayout::Position();
            layoutItem.position->left = CSSLayout::Dimension::Px(tl.x);
            layoutItem.position->top  = CSSLayout::Dimension::Px(tl.y);
            layoutItem.SetPositionType(CSSLayout::PositionType::AbsoluteUI);
            InvalidateLayout();
        }
        SetZIndex(OverlayZOrder::Overlays);
        RequestRedraw();
    }

    void UltraCanvasBadge::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!IsBadgeVisible()) return;

        Size2Df sz = MeasureContent(ctx);
        float w = sz.width, h = sz.height;

        // Anchored badges follow the anchor's corner (badge straddles it).
        // Standalone badges are sized/placed by the layout engine (via
        // MeasureOwnContent), so their bounds are left alone here.
        if (auto a = anchor.lock()) {
            Point2Df tl = AnchorTopLeft(a->GetBounds(), w, h);
            if (std::abs(tl.x - GetX()) > 0.5f || std::abs(tl.y - GetY()) > 0.5f ||
                std::abs(w - GetWidth()) > 0.5f || std::abs(h - GetHeight()) > 0.5f) {
                SetBounds(tl.x, tl.y, w, h);
                // Keep the engine insets in sync so the next layout pass
                // doesn't snap the badge back to a stale position.
                if (layoutItem.position) {
                    layoutItem.position->left = CSSLayout::Dimension::Px(tl.x);
                    layoutItem.position->top  = CSSLayout::Dimension::Px(tl.y);
                }
            }
        }

        bool ring = overlayRing && !anchor.expired();
        float rw = ring ? style.overlayRingWidth : 0.0f;
        Color fill = VariantColor();

        if (dot) {
            Point2Dd center(w / 2.0f, h / 2.0f);
            float r = style.dotSize / 2.0f;
            if (ring) {
                ctx->SetFillPaint(style.overlayRingColor);
                ctx->FillCircle(center, r);
                ctx->SetFillPaint(fill);
                ctx->FillCircle(center, std::max(1.0f, r - rw));
            } else {
                ctx->SetFillPaint(fill);
                ctx->FillCircle(center, r);
            }
            return;
        }

        // Pill: inset by the ring so the border stays inside our bounds.
        Rect2Df rect(rw / 2.0f, rw / 2.0f, w - rw, h - rw);
        float radius = rect.height / 2.0f;
        Color border = ring ? style.overlayRingColor : style.borderColor;
        ctx->DrawFilledRectangle(rect, fill, ring ? rw : style.borderWidth, border, radius);

        FontStyle fs;
        fs.fontFamily = style.fontFamily;
        fs.fontSize = style.fontSize;
        fs.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(fs);
        ctx->SetTextPaint(CurrentTextColor());
        std::string t = DisplayText();
        Point2Di ts = ctx->GetTextDimension(t);
        ctx->DrawText(t, Point2Di(static_cast<int>((w - ts.x) / 2.0f),
                                  static_cast<int>((h - ts.y) / 2.0f)));
    }

    bool UltraCanvasBadge::OnEvent(const UCEvent& event) {
        if (!IsVisible() || IsDisabled() || !onClick) return false;
        if (event.type == UCEventType::MouseDown) {
            Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
            if (GetLocalBounds().Contains(p) && IsBadgeVisible()) {
                onClick();
                return true;
            }
        }
        return false;
    }

} // namespace UltraCanvas
