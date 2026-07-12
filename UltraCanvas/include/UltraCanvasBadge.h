// include/UltraCanvasBadge.h
// Badge: a small count / status indicator. Works two ways:
//   * Standalone — a coloured pill with a number or short label, or a minimal
//                  status dot.
//   * Overlay    — anchored to a corner of another element (the classic
//                  notification count on a bell / inbox icon).
//
// Count badges support a max ("99+"), show-zero suppression, and colour
// variants (Neutral / Primary / Success / Warning / Danger / Info). The badge
// auto-sizes to its content and, when anchored, follows its anchor.
//
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <memory>
#include <functional>
#include <algorithm>

namespace UltraCanvas {

// ===== BADGE COLOUR VARIANT =====
    enum class BadgeVariant {
        Neutral,
        Primary,
        Successful,   // "Success" is an X11 macro (#define Success 0), so we spell it out
        Warning,
        Danger,
        Info
    };

// ===== OVERLAY CORNER =====
    enum class BadgeCorner {
        TopRight,
        TopLeft,
        BottomRight,
        BottomLeft
    };

// ===== BADGE STYLE =====
    struct BadgeStyle {
        Color textColor        = Colors::White;
        Color warningTextColor = Color(60, 50, 0, 255);   // dark text on amber
        Color borderColor      = Color(0, 0, 0, 0);       // transparent by default

        // Ring drawn behind an overlay badge so it separates from the icon below.
        Color overlayRingColor = Colors::White;
        float overlayRingWidth = 2.0f;

        float height       = 18.0f;    // pill height
        float minWidth     = 18.0f;    // keeps single digits circular
        float paddingH     = 6.0f;
        float dotSize       = 10.0f;
        float borderWidth   = 0.0f;

        float fontSize     = 11.0f;
        std::string fontFamily;
    };

// ===== BADGE COMPONENT =====
    class UltraCanvasBadge : public UltraCanvasUIElement {
    private:
        std::string text;          // status/text mode
        int  count = 0;            // count mode
        bool useCount = false;
        int  maxCount = 99;
        bool showZero = false;
        bool dot = false;          // dot mode (no text)

        BadgeVariant variant = BadgeVariant::Danger;
        bool  hasCustomColor = false;
        Color customColor;

        BadgeStyle style;

        // Overlay anchoring.
        std::weak_ptr<UltraCanvasUIElement> anchor;
        BadgeCorner corner = BadgeCorner::TopRight;
        float anchorOffsetX = 0.0f;
        float anchorOffsetY = 0.0f;
        bool  overlayRing = true;

    public:
        UltraCanvasBadge(const std::string& identifier, float x, float y, float w, float h);
        UltraCanvasBadge(const std::string& identifier, float w, float h)
            : UltraCanvasBadge(identifier, -1, -1, w, h) {}
        explicit UltraCanvasBadge(const std::string& identifier)
            : UltraCanvasBadge(identifier, -1, -1, -1, -1) {}

        // ===== CONTENT =====
        // Content changes resize the badge, so they invalidate engine layout
        // (the badge publishes its size via MeasureOwnContent) besides redrawing.
        void SetText(const std::string& t) { text = t; useCount = false; dot = false; InvalidateLayout(); RequestRedraw(); }
        const std::string& GetText() const { return text; }

        void SetCount(int c) { count = c; useCount = true; dot = false; InvalidateLayout(); RequestRedraw(); }
        int  GetCount() const { return count; }
        void SetMaxCount(int m) { maxCount = std::max(1, m); InvalidateLayout(); RequestRedraw(); }
        void SetShowZero(bool s) { showZero = s; InvalidateLayout(); RequestRedraw(); }

        void SetDot(bool d) { dot = d; InvalidateLayout(); RequestRedraw(); }
        bool IsDot() const { return dot; }

        // Visible for the current content (count 0 with !showZero, or empty text,
        // renders nothing).
        bool IsBadgeVisible() const;

        // ===== APPEARANCE =====
        void SetVariant(BadgeVariant v) { variant = v; hasCustomColor = false; RequestRedraw(); }
        BadgeVariant GetVariant() const { return variant; }
        void SetColor(const Color& c) { customColor = c; hasCustomColor = true; RequestRedraw(); }

        BadgeStyle& GetStyle() { return style; }
        const BadgeStyle& GetStyle() const { return style; }
        void SetStyle(const BadgeStyle& s) { style = s; InvalidateLayout(); RequestRedraw(); }

        // ===== OVERLAY ANCHORING =====
        // Position this badge over `anchorElement` (a sibling in the same
        // container). The badge straddles the chosen corner and follows the
        // anchor. Pass nullptr to detach.
        void AnchorTo(const std::shared_ptr<UltraCanvasUIElement>& anchorElement,
                      BadgeCorner corner = BadgeCorner::TopRight,
                      float offsetX = 0.0f, float offsetY = 0.0f);
        void SetOverlayRing(bool on) { overlayRing = on; RequestRedraw(); }

        // ===== OVERRIDES =====
        bool AcceptsFocus() const override { return false; }
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

        // ===== CSS LAYOUT (intrinsic sizing) =====
        // The badge auto-sizes to its content. Publishing that size to the
        // layout engine is what keeps Arrange from collapsing the (auto-sized)
        // badge to 0x0 — a zero-sized child is culled and never rendered.
        Size2Df MeasureOwnContent(std::optional<float> definiteContentWidth,
                                  const CSSLayout::LayoutContext& ctx) override;
        void ComputeIntrinsicSizes(const CSSLayout::LayoutContext& ctx) override;

        // ===== CALLBACKS =====
        std::function<void()> onClick;

    private:
        std::string DisplayText() const;
        Color VariantColor() const;
        Color CurrentTextColor() const;
        Size2Df MeasureContent(IRenderContext* ctx) const;
        Size2Df ContentSize() const;   // MeasureContent via GetRenderContext(), style-based fallback
        Point2Df AnchorTopLeft(const Rect2Df& anchorBounds, float w, float h) const;
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasBadge> CreateBadge(
            const std::string& identifier, float x, float y, const std::string& text,
            BadgeVariant variant = BadgeVariant::Neutral) {
        auto b = std::make_shared<UltraCanvasBadge>(identifier, x, y, 0, 0);
        b->SetText(text);
        b->SetVariant(variant);
        return b;
    }

    inline std::shared_ptr<UltraCanvasBadge> CreateCountBadge(
            const std::string& identifier, float x, float y, int count,
            BadgeVariant variant = BadgeVariant::Danger) {
        auto b = std::make_shared<UltraCanvasBadge>(identifier, x, y, 0, 0);
        b->SetCount(count);
        b->SetVariant(variant);
        return b;
    }

    inline std::shared_ptr<UltraCanvasBadge> CreateDotBadge(
            const std::string& identifier, float x, float y,
            BadgeVariant variant = BadgeVariant::Successful) {
        auto b = std::make_shared<UltraCanvasBadge>(identifier, x, y, 0, 0);
        b->SetDot(true);
        b->SetVariant(variant);
        return b;
    }

} // namespace UltraCanvas
