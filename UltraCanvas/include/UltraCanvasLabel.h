// include/UltraCanvasLabel.h
// Modern text display label control with styling and alignment options.
// Exemplar of the new CSSLayout intrinsic-sizing protocol: overrides
// MeasureOwnContent (constraint-aware content sizing) and ComputeIntrinsicSizes
// (constraint-free max/min-content) so the engine can place the label
// without the widget mutating finalBounds itself.
// Version: 2.0.1
// Last Modified: 2026-05-29
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <functional>

namespace UltraCanvas {

// ===== LABEL STYLE CONFIGURATION =====
    struct LabelStyle {
        // Text appearance
        FontStyle fontStyle;
        Color textColor = Colors::Black;

        // Text alignment
        TextAlignment horizontalAlign = TextAlignment::Left;
        VerticalAlignment verticalAlign = VerticalAlignment::Top;
        // Word wrapping
        TextWrap wrap = TextWrap::WrapNone;

        // Text effects
        bool hasShadow = false;
        Color shadowColor = Color(0, 0, 0, 128);
        Point2Di shadowOffset = Point2Di(1, 1);

        static LabelStyle DefaultStyle() {
            return LabelStyle{};
        }

        static LabelStyle HeaderStyle();
        static LabelStyle SubHeaderStyle();
        static LabelStyle CaptionStyle();
        static LabelStyle StatusStyle();
    };

// ===== LABEL COMPONENT =====
    class UltraCanvasLabel : public UltraCanvasUIElement {
    private:
        // ===== LABEL PROPERTIES =====
        std::string text;
        LabelStyle style;

        // ===== COMPUTED LAYOUT =====
        Rect2Di textArea;
        std::unique_ptr<ITextLayout> textLayout = nullptr;
        bool isMarkup = false;

        bool internalLayoutValid = false;
    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasLabel(const std::string &identifier, float x, float y, float w, float h,
                         const std::string &labelText = "");

        UltraCanvasLabel(const std::string &identifier, float w, float h, const std::string &labelText = "")
                : UltraCanvasLabel(identifier, -1, -1, w, h, labelText) {}

        UltraCanvasLabel(const std::string &identifier, const std::string &labelText)
                : UltraCanvasLabel(identifier, -1, -1, -1, -1, labelText) {}

        explicit UltraCanvasLabel(const std::string &labelText = "")
                : UltraCanvasLabel("", -1, -1, -1, -1, labelText) {}

        virtual ~UltraCanvasLabel() = default;

        // ===== TEXT MANAGEMENT =====
        void SetText(const std::string &newText);

        const std::string &GetText() const {
            return text;
        }

        // ===== STYLE MANAGEMENT =====
        void SetStyle(const LabelStyle &newStyle);
        const LabelStyle &GetStyle() const {
            return style;
        }

        // ===== CONVENIENCE STYLE SETTERS =====
        void SetFont(const std::string &fontFamily, float fontSize = 12.0f, FontWeight weight = FontWeight::Normal);
        void SetFontSize(float fontSize);
        void SetFontWeight(const FontWeight w);
        void SetTextColor(const Color &color);
        void SetAlignment(TextAlignment horizontal, VerticalAlignment vertical = VerticalAlignment::Top);
        void SetWrap(TextWrap wrap);
        void SetTextIsMarkup(bool markup);

        // ===== ENGINE-DRIVEN LAYOUT =====
        // Content-box size of the text. nullopt width → max-content width;
        // a definite width → height at that width (reflects wrapping). The
        // block layout adds padding/border and applies size.*/constraints.
        Size2Df MeasureOwnContent(std::optional<float> definiteContentWidth,
                                  const CSSLayout::LayoutContext& ctx) override;

        // Constraint-free: publishes intrinsic min/max-content via the
        // inherited `intrinsic` cache, in BORDER-BOX units (i.e. including
        // padding + border) to match what measured.* would return.
        void ComputeIntrinsicSizes(const CSSLayout::LayoutContext& ctx) override;

        void InvalidateLayout() override;

        // ===== RENDERING =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void UpdateInternalLayout(IRenderContext *ctx);

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;

        // ===== EVENT CALLBACKS =====
        std::function<void()> onClick;
        std::function<void()> onHoverEnter;
        std::function<void()> onHoverLeave;
        std::function<void(const std::string&)> onTextChanged;

    protected:
        // Build the cached ITextLayout if missing and configure it with the
        // current font/wrap/alignment. Does NOT set explicit width — callers
        // (MeasureOwnContent / ComputeIntrinsicSizes) own that.
        // Returns true if the layout is now valid, false if no render context
        // is available (in which case callers should bail gracefully).
        bool EnsureTextLayout();
    };


// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasLabel>
    CreateLabel(const std::string &identifier, float x, float y, float w, float h,
                const std::string &text = "") {
        return std::make_shared<UltraCanvasLabel>(identifier, x, y, w, h, text);
    }

    inline std::shared_ptr<UltraCanvasLabel>
    CreateLabel(const std::string &identifier, float w, float h, const std::string &text = "") {
        return std::make_shared<UltraCanvasLabel>(identifier, 0, 0, w, h, text);
    }

    inline std::shared_ptr<UltraCanvasLabel> CreateLabel(const std::string &text) {
        return std::make_shared<UltraCanvasLabel>("", text);
    }

    inline std::shared_ptr<UltraCanvasLabel> CreateLabel(const std::string &identifier,
                                                  const std::string &text) {
        return std::make_shared<UltraCanvasLabel>(identifier, text);
    }

// ===== BUILDER PATTERN =====
    class LabelBuilder {
    private:
        std::shared_ptr<UltraCanvasLabel> label;

    public:
        LabelBuilder(const std::string& identifier);

        LabelBuilder& SetText(const std::string& text);
        LabelBuilder& SetFont(const std::string& fontFamily, float fontSize = 12.0f);
        LabelBuilder& SetTextColor(const Color& color);
        LabelBuilder& SetBackgroundColor(const Color& color);
        LabelBuilder& SetAlignment(TextAlignment align);
        LabelBuilder& SetPadding(float padding);
        LabelBuilder& SetStyle(const LabelStyle& style);
        LabelBuilder& OnClick(std::function<void()> callback);

        std::shared_ptr<UltraCanvasLabel> Build() {
            return label;
        }
    };
} // namespace UltraCanvas
