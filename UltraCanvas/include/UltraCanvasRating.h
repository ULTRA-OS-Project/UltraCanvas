// include/UltraCanvasRating.h
// Rating control: a row of symbols (stars by default) for entering or showing
// a score. Supports whole- and half-step values, hover preview, read-only
// display, and — crucially — user-supplied **vector (SVG) files** for the three
// per-symbol states: on (filled), off (empty) and half.
//
// Symbol source options:
//   * Built-in vector shapes drawn directly (Star, Circle, Square).
//   * Custom image/SVG files via SetCustomSymbols(onPath, offPath, halfPath).
//     If no half file is supplied, the half state is synthesised by clipping the
//     "on" symbol to its left half over the "off" symbol — so half rendering
//     works for any symbol, built-in or custom.
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
#include <cmath>

namespace UltraCanvas {

// ===== BUILT-IN SYMBOL SHAPES =====
    enum class RatingSymbol {
        Star,     // classic 5-point star
        Circle,   // filled dot
        Square,   // rounded square
        Custom    // user-supplied image / SVG files (see SetCustomSymbols)
    };

// ===== RATING STYLE =====
    struct RatingStyle {
        Color onColor      = Color(255, 179, 0, 255);    // filled (amber)
        Color offColor     = Color(214, 214, 220, 255);  // empty (light grey)
        Color hoverColor   = Color(255, 202, 64, 255);   // filled while hovering
        Color borderColor  = Color(198, 150, 0, 255);    // optional outline
        float borderWidth  = 0.0f;                       // 0 = no outline on built-in shapes

        float symbolSize   = 28.0f;
        float spacing      = 4.0f;

        // When drawing Custom symbols, treat the image as a monochrome mask and
        // tint it with the state colour (via DrawMask) instead of drawing the
        // image's own colours. Leave false for full-colour SVG icons.
        bool  tintCustom   = false;

        FontStyle fontStyle;
    };

// ===== RATING COMPONENT =====
    class UltraCanvasRating : public UltraCanvasUIElement {
    private:
        // ===== MODEL =====
        int   maxRating   = 5;
        float value       = 0.0f;      // 0 .. maxRating, snapped to the step
        bool  allowHalf   = false;
        bool  readOnly    = false;
        bool  allowClear  = true;      // clicking the current value clears to 0

        // ===== SYMBOL =====
        RatingSymbol symbol = RatingSymbol::Star;
        std::string onPath, offPath, halfPath;
        std::shared_ptr<UCImage> onImage, offImage, halfImage;

        RatingStyle style;

        // ===== INTERACTION =====
        float hoverValue = -1.0f;      // -1 = not hovering

    public:
        // ===== CONSTRUCTORS (REQUIRED PATTERN) =====
        UltraCanvasRating(const std::string& identifier, float x, float y, float w, float h);
        UltraCanvasRating(const std::string& identifier, float w, float h)
            : UltraCanvasRating(identifier, -1, -1, w, h) {}
        explicit UltraCanvasRating(const std::string& identifier)
            : UltraCanvasRating(identifier, -1, -1, -1, -1) {}

        // ===== VALUE =====
        void  SetMaxRating(int count);
        int   GetMaxRating() const { return maxRating; }

        void  SetValue(float v, bool runCallback = false);
        float GetValue() const { return value; }

        void  SetAllowHalf(bool allow);
        bool  GetAllowHalf() const { return allowHalf; }

        void  SetReadOnly(bool ro) { readOnly = ro; hoverValue = -1.0f; RequestRedraw(); }
        bool  IsReadOnly() const { return readOnly; }

        void  SetAllowClear(bool allow) { allowClear = allow; }
        bool  GetAllowClear() const { return allowClear; }

        // ===== SYMBOL =====
        void SetSymbol(RatingSymbol s) { symbol = s; RequestRedraw(); }
        RatingSymbol GetSymbol() const { return symbol; }

        // Supply vector/image files for the three states. `halfPath` is optional;
        // when empty the half state is synthesised by clipping `on` over `off`.
        // Passing an empty onPath reverts to the current built-in shape.
        void SetCustomSymbols(const std::string& onPath,
                              const std::string& offPath,
                              const std::string& halfPath = "");

        // ===== APPEARANCE =====
        void SetSymbolSize(float s) { style.symbolSize = std::max(6.0f, s); RequestRedraw(); }
        void SetSpacing(float s)    { style.spacing = std::max(0.0f, s); RequestRedraw(); }
        void SetColors(const Color& on, const Color& off) {
            style.onColor = on; style.offColor = off; RequestRedraw();
        }
        RatingStyle& GetStyle() { return style; }
        const RatingStyle& GetStyle() const { return style; }
        void SetStyle(const RatingStyle& s) { style = s; RequestRedraw(); }

        // Preferred width for maxRating symbols at the current size/spacing.
        float GetPreferredWidth() const {
            return maxRating * style.symbolSize + std::max(0, maxRating - 1) * style.spacing;
        }

        // ===== OVERRIDES =====
        bool AcceptsFocus() const override { return !readOnly; }
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

        // ===== CALLBACKS =====
        std::function<void(float)> onRatingChanged;   // committed value
        std::function<void(float)> onHoverChanged;    // live hover value (-1 when cleared)

    private:
        float SnapToStep(float v) const;
        float Step() const { return allowHalf ? 0.5f : 1.0f; }

        Rect2Df SymbolRect(int index) const;      // element-local
        float   ValueAtPointer(float localX) const;

        void RenderSymbol(IRenderContext* ctx, const Rect2Df& rect, float fill, bool hovered);
        void DrawBuiltinShape(IRenderContext* ctx, const Rect2Df& rect, const Color& color);
        void DrawCustomImage(IRenderContext* ctx, const std::shared_ptr<UCImage>& img,
                             const Rect2Df& rect, const Color& tint);

        bool UseCustom() const { return symbol == RatingSymbol::Custom && onImage && offImage; }

        bool HandleKeyDown(const UCEvent& event);
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasRating> CreateRating(
            const std::string& identifier, float x, float y, float w, float h,
            int maxRating = 5, float value = 0.0f) {
        auto r = std::make_shared<UltraCanvasRating>(identifier, x, y, w, h);
        r->SetMaxRating(maxRating);
        r->SetValue(value, false);
        return r;
    }

    inline std::shared_ptr<UltraCanvasRating> CreateHalfRating(
            const std::string& identifier, float x, float y, float w, float h,
            int maxRating = 5, float value = 0.0f) {
        auto r = CreateRating(identifier, x, y, w, h, maxRating, value);
        r->SetAllowHalf(true);
        return r;
    }

    // Rating that renders from vector/image files for its on/off/half states.
    inline std::shared_ptr<UltraCanvasRating> CreateVectorRating(
            const std::string& identifier, float x, float y, float w, float h,
            const std::string& onPath, const std::string& offPath,
            const std::string& halfPath = "", int maxRating = 5) {
        auto r = std::make_shared<UltraCanvasRating>(identifier, x, y, w, h);
        r->SetMaxRating(maxRating);
        r->SetCustomSymbols(onPath, offPath, halfPath);
        if (!halfPath.empty()) r->SetAllowHalf(true);
        return r;
    }

} // namespace UltraCanvas
