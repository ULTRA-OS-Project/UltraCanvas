// include/UltraCanvasLabel.h
// Modern text display label control with styling and alignment options
// Version: 1.0.0
// Last Modified: 2025-08-17
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
        int maxWidth = 0;
        bool autoResize = false;
        bool isMarkup = false;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasLabel(const std::string &identifier,
                         long id, long x, long y, long w, long h,
                         const std::string &labelText = "");

        explicit UltraCanvasLabel(const std::string &identifier = "Label",
                                  long w = 100, long h = 25,
                                  const std::string &labelText = "");

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
        void SetAutoResize(bool autoResize);
        void SetMaxWidth(int mWidth);
        void SetTextIsMarkup(bool markup);

        int GetPreferredWidth() override;
        int GetPreferredHeight() override;

        // ===== RENDERING =====
        void Render(IRenderContext* ctx) override;
        void UpdateGeometry(IRenderContext *ctx) override;

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;

        // ===== SIZE CHANGES =====
        void SetBounds(const Rect2Di& bounds) override;

        // ===== EVENT CALLBACKS =====
        std::function<void()> onClick;
        std::function<void()> onHoverEnter;
        std::function<void()> onHoverLeave;
        std::function<void(const std::string&)> onTextChanged;

    protected:
        // ===== LAYOUT CALCULATION =====
//        void CalculateLayout(IRenderContext *ctx);
        // ===== SIZING =====
//        void AutoResize(const Size2Di &textDimensions);

    };
// ===== FACTORY FUNCTIONS =====
    std::shared_ptr<UltraCanvasLabel> CreateLabel(
            const std::string& identifier, long id, long x, long y, long w, long h,
            const std::string& text = "");

    std::shared_ptr<UltraCanvasLabel> CreateLabel(
            const std::string& identifier, long x, long y, long w, long h,
            const std::string& text = "");

    std::shared_ptr<UltraCanvasLabel> CreateLabel(
            const std::string& identifier, long w, long h,
            const std::string& text = "");

    std::shared_ptr<UltraCanvasLabel> CreateLabel(const std::string& text);

    std::shared_ptr<UltraCanvasLabel> CreateAutoLabel(
            const std::string& identifier, long id, long x, long y,
            const std::string& text);

    std::shared_ptr<UltraCanvasLabel> CreateHeaderLabel(
            const std::string& identifier, long id, long x, long y, long w, long h,
            const std::string& text);

    std::shared_ptr<UltraCanvasLabel> CreateStatusLabel(
            const std::string& identifier, long id, long x, long y, long w, long h,
            const std::string& text = "Ready");

// ===== BUILDER PATTERN =====
    class LabelBuilder {
    private:
        std::shared_ptr<UltraCanvasLabel> label;

    public:
        LabelBuilder(const std::string& identifier, long id, long x, long y, long w = 100, long h = 25);

        LabelBuilder& SetText(const std::string& text);
        LabelBuilder& SetFont(const std::string& fontFamily, float fontSize = 12.0f);
        LabelBuilder& SetTextColor(const Color& color);
        LabelBuilder& SetBackgroundColor(const Color& color);
        LabelBuilder& SetAlignment(TextAlignment align);
        LabelBuilder& SetPadding(float padding);
        LabelBuilder& SetAutoResize(bool autoResize = true);
        LabelBuilder& SetStyle(const LabelStyle& style);
        LabelBuilder& OnClick(std::function<void()> callback);

        std::shared_ptr<UltraCanvasLabel> Build() {
            return label;
        }
    };

// ===== CONVENIENCE BUILDER =====
    inline LabelBuilder CreateLabelBuilder(const std::string& identifier, long id, long x, long y, long w = 100, long h = 25) {
        return LabelBuilder(identifier, id, x, y, w, h);
    }

} // namespace UltraCanvas
