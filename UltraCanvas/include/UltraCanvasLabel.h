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
        TextVerticalAlignment verticalAlign = TextVerticalAlignment::Middle;

        // Text effects
        bool hasShadow = false;
        Color shadowColor = Color(0, 0, 0, 128);
        Point2Di shadowOffset = Point2Di(1, 1);

        // Word wrapping
        bool wordWrap = false;
        bool autoResize = false;
        bool isMarkup = false;

        static LabelStyle DefaultStyle() {
            return LabelStyle{};
        }

        static LabelStyle HeaderStyle() {
            LabelStyle style;
            style.fontStyle.fontSize = 18.0f;
            style.fontStyle.fontWeight = FontWeight::Bold;
            style.textColor = Color(40, 40, 40, 255);
            return style;
        }

        static LabelStyle SubHeaderStyle() {
            LabelStyle style;
            style.fontStyle.fontSize = 14.0f;
            style.fontStyle.fontWeight = FontWeight::Bold;
            style.textColor = Color(60, 60, 60, 255);
            return style;
        }

        static LabelStyle CaptionStyle() {
            LabelStyle style;
            style.fontStyle.fontSize = 10.0f;
            style.textColor = Color(120, 120, 120, 255);
            return style;
        }

        static LabelStyle StatusStyle() {
            LabelStyle style;
            style.fontStyle.fontSize = 11.0f;
            style.textColor = Color(100, 100, 100, 255);
            return style;
        }
    };

// ===== LABEL COMPONENT =====
    class UltraCanvasLabel : public UltraCanvasUIElement {
    private:
        // ===== LABEL PROPERTIES =====
        std::string text;
        LabelStyle style;

        // ===== COMPUTED LAYOUT =====
        Rect2Di textArea;
        Point2Di textPosition;
        bool layoutDirty = true;

        // ===== AUTO-SIZING =====
        Size2Di preferredSize;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasLabel(const std::string& identifier,
                         long id, long x, long y, long w, long h,
                         const std::string& labelText = "")
                : UltraCanvasUIElement(identifier, id, x, y, w, h), text(labelText) {

            // Initialize style
            style = LabelStyle::DefaultStyle();
            SetText(labelText);
        }

        explicit UltraCanvasLabel(const std::string& identifier = "Label",
                         long w = 100, long h = 25,
                         const std::string& labelText = "")
                : UltraCanvasUIElement(identifier, w, h), text(labelText) {

            // Initialize style
            style = LabelStyle::DefaultStyle();
            SetText(labelText);
        }

        virtual ~UltraCanvasLabel() = default;

        // ===== TEXT MANAGEMENT =====
        void SetText(const std::string& newText) {
            if (text != newText) {
                text = newText;
                layoutDirty = true;
                RequestRedraw();

                if (onTextChanged) {
                    onTextChanged(text);
                }
            }
        }

        const std::string& GetText() const {
            return text;
        }

        void AppendText(const std::string& additionalText) {
            SetText(text + additionalText);
        }

        void ClearText() {
            SetText("");
        }

        bool IsEmpty() const {
            return text.empty();
        }

        // ===== STYLE MANAGEMENT =====
        void SetStyle(const LabelStyle& newStyle) {
            style = newStyle;
            layoutDirty = true;
            RequestRedraw();
        }

        const LabelStyle& GetStyle() const {
            return style;
        }

        // ===== CONVENIENCE STYLE SETTERS =====
        void SetFont(const std::string& fontFamily, float fontSize = 12.0f, FontWeight weight = FontWeight::Normal) {
            style.fontStyle.fontFamily = fontFamily;
            style.fontStyle.fontSize = fontSize;
            style.fontStyle.fontWeight = weight;
            layoutDirty = true;
            RequestRedraw();
        }

        void SetFontSize(float fontSize) {
            style.fontStyle.fontSize = fontSize;
            layoutDirty = true;
            RequestRedraw();
        }

        void SetFontWeight(const FontWeight w) {
            style.fontStyle.fontWeight = w;
            layoutDirty = true;
            RequestRedraw();
        }

        void SetTextColor(const Color& color) {
            style.textColor = color;
            RequestRedraw();
        }

        void SetAlignment(TextAlignment horizontal, TextVerticalAlignment vertical = TextVerticalAlignment::Middle) {
            style.horizontalAlign = horizontal;
            style.verticalAlign = vertical;
            layoutDirty = true;
            RequestRedraw();
        }

        void SetWordWrap(bool wrap) {
            style.wordWrap = wrap;
            layoutDirty = true;
            RequestRedraw();
        }

        void SetAutoResize(bool autoResize) {
            style.autoResize = autoResize;
            layoutDirty = true;
            RequestRedraw();
        }

        void SetTextIsMarkup(bool markup) {
            style.isMarkup = markup;
            layoutDirty = true;
            RequestRedraw();
        }

        // ===== SIZING =====
        void AutoResize(const Size2Di& textDimensions) {
            if (text.empty()) {
                preferredSize = Size2Di(GetTotalPaddingHorizontal() + GetTotalBorderHorizontal() + 20,
                                        GetTotalPaddingVertical() + GetTotalBorderVertical() + style.fontStyle.fontSize + 4);
            } else {
                // Set text style for measurement
                if (textDimensions.width > 0) {
                    preferredSize = Size2Di(
                            textDimensions.width + GetTotalPaddingHorizontal() + GetTotalBorderHorizontal(),
                            textDimensions.height + GetTotalPaddingVertical() + GetTotalBorderVertical()
                    );
                } else {
                    preferredSize = GetSize();
                }
            }

            SetSize(preferredSize.width, preferredSize.height);
        }

        int GetPreferredWidth() override {
            if (style.autoResize && layoutDirty) {
                CalculateLayout(GetRenderContext());
            }
            return preferredSize.width > 0 ? preferredSize.width : bounds.width;
        }

        int GetPreferredHeight() override {
            if (style.autoResize && layoutDirty) {
                CalculateLayout(GetRenderContext());
            }
            return preferredSize.height > 0 ? preferredSize.height : bounds.height;
        }

        // ===== LAYOUT CALCULATION =====
        void CalculateLayout(IRenderContext *ctx) {
            Rect2Di bounds = GetBounds();
            ctx->PushState();
            ctx->SetFontStyle(style.fontStyle);
            Size2Di textDimensions;
            ctx->SetTextIsMarkup(style.isMarkup);
            ctx->GetTextLineDimensions(text, textDimensions.width, textDimensions.height);
            if (style.autoResize || GetWidth() == 0) {
                AutoResize(textDimensions);
            }

            // Calculate text area (inside padding and borders)
            textArea = GetContentRect();

            if (!text.empty()) {
                // Set text style for measurement

                // Calculate horizontal position
                float textX = textArea.x;
                switch (style.horizontalAlign) {
                    case TextAlignment::Left:
                        textX = textArea.x;
                        break;
                    case TextAlignment::Center:
                        textX = textArea.x + (textArea.width - textDimensions.width) / 2;
                        break;
                    case TextAlignment::Right:
                        textX = textArea.x + textArea.width - textDimensions.width;
                        break;
                    case TextAlignment::Justify:
                        textX = textArea.x; // For single line, same as left
                        break;
                }

                // Calculate vertical position
                float textY = textArea.y;
                switch (style.verticalAlign) {
                    case TextVerticalAlignment::Top:
                        textY = textArea.y; // Baseline offset
                        break;
                    case TextVerticalAlignment::Middle:
                        textY = textArea.y + (textArea.height / 2) - textDimensions.height / 2;
                        break;
                    case TextVerticalAlignment::Bottom:
                        textY = textArea.y + textArea.height;
                        break;
                }

                textPosition = Point2Di(textX, textY);
            }

            ctx->PopState();
            layoutDirty = false;
        }

        // ===== RENDERING =====
        void Render(IRenderContext* ctx) override {
            if (!IsVisible()) return;

            ctx->PushState();

            if (layoutDirty) {
                CalculateLayout(ctx);
            }

             UltraCanvasUIElement::Render(ctx);

            Rect2Di bounds = GetBounds();
            // Draw text
            ctx->SetTextIsMarkup(style.isMarkup);
            if (!text.empty()) {
                // Draw shadow if enabled
                ctx->SetTextWrap(style.wordWrap ? TextWrap::WrapWordChar : TextWrap::WrapNone);
                if (style.hasShadow) {
                    ctx->SetTextPaint(style.shadowColor);
                    ctx->SetFontStyle(style.fontStyle);
                    Rect2Di shadowRect = textArea;
                    shadowRect.x += style.shadowOffset.x,
                    shadowRect.y += style.shadowOffset.y;
                    ctx->DrawTextInRect(text, shadowRect);
                }

                // Draw main text
                ctx->SetTextPaint(style.textColor);
                ctx->SetFontStyle(style.fontStyle);
                ctx->DrawTextInRect(text, textArea);
            }

            // Draw selection/focus indicator if needed
            if (IsFocused()) {
                ctx->SetStrokePaint(Color(0, 120, 215, 200));
                ctx->SetStrokeWidth(2.0f);
                ctx->DrawRectangle(bounds);
            }

            ctx->PopState();
        }

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override {
            switch (event.type) {
                case UCEventType::MouseDown:
                    if (Contains(event.x, event.y)) {
                        SetFocus(true);
                        if (onClick) {
                            onClick();
                        }
                        return true;
                    }
                    break;

                case UCEventType::MouseMove:
                    if (Contains(event.x, event.y)) {
                        if (!IsHovered()) {
                            SetHovered(true);
                            if (onHoverEnter) {
                                onHoverEnter();
                            }
                        }
                    } else {
                        if (IsHovered()) {
                            SetHovered(false);
                            if (onHoverLeave) {
                                onHoverLeave();
                            }
                        }
                    }
                    break;
            }

            return UltraCanvasUIElement::OnEvent(event);
        }

        // ===== SIZE CHANGES =====
        void SetBounds(const Rect2Di& bounds) {
            UltraCanvasUIElement::SetBounds(bounds);
            layoutDirty = true;
        }

        // ===== EVENT CALLBACKS =====
        std::function<void()> onClick;
        std::function<void()> onHoverEnter;
        std::function<void()> onHoverLeave;
        std::function<void(const std::string&)> onTextChanged;
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasLabel> CreateLabel(
            const std::string& identifier, long id, long x, long y, long w, long h,
            const std::string& text = "") {
        return std::make_shared<UltraCanvasLabel>(identifier, id, x, y, w, h, text);
    }

    inline std::shared_ptr<UltraCanvasLabel> CreateLabel(
            const std::string& identifier, long x, long y, long w, long h,
            const std::string& text = "") {
        return std::make_shared<UltraCanvasLabel>(identifier, 0, x, y, w, h, text);
    }

    inline std::shared_ptr<UltraCanvasLabel> CreateLabel(
            const std::string& identifier, long w, long h,
            const std::string& text = "") {
        return std::make_shared<UltraCanvasLabel>(identifier, 0, 0, 0, w, h, text);
    }

    inline std::shared_ptr<UltraCanvasLabel> CreateLabel(const std::string& text) {
        return std::make_shared<UltraCanvasLabel>("", 0, 0, 0, 0, 0, text);
    }

    inline std::shared_ptr<UltraCanvasLabel> CreateAutoLabel(
            const std::string& identifier, long id, long x, long y,
            const std::string& text) {
        auto label = std::make_shared<UltraCanvasLabel>(identifier, id, x, y, 100, 25, text);
        label->SetAutoResize(true);
        return label;
    }

    inline std::shared_ptr<UltraCanvasLabel> CreateHeaderLabel(
            const std::string& identifier, long id, long x, long y, long w, long h,
            const std::string& text) {
        auto label = CreateLabel(identifier, id, x, y, w, h, text);
        label->SetStyle(LabelStyle::HeaderStyle());
        return label;
    }

    inline std::shared_ptr<UltraCanvasLabel> CreateStatusLabel(
            const std::string& identifier, long id, long x, long y, long w, long h,
            const std::string& text = "Ready") {
        auto label = CreateLabel(identifier, id, x, y, w, h, text);
        label->SetStyle(LabelStyle::StatusStyle());
        label->SetPadding(4);
        return label;
    }

// ===== BUILDER PATTERN =====
    class LabelBuilder {
    private:
        std::shared_ptr<UltraCanvasLabel> label;

    public:
        LabelBuilder(const std::string& identifier, long id, long x, long y, long w = 100, long h = 25) {
            label = CreateLabel(identifier, id, x, y, w, h);
        }

        LabelBuilder& SetText(const std::string& text) {
            label->SetText(text);
            return *this;
        }

        LabelBuilder& SetFont(const std::string& fontFamily, float fontSize = 12.0f) {
            label->SetFont(fontFamily, fontSize);
            return *this;
        }

        LabelBuilder& SetTextColor(const Color& color) {
            label->SetTextColor(color);
            return *this;
        }

        LabelBuilder& SetBackgroundColor(const Color& color) {
            label->SetBackgroundColor(color);
            return *this;
        }

        LabelBuilder& SetAlignment(TextAlignment align) {
            label->SetAlignment(align);
            return *this;
        }

        LabelBuilder& SetPadding(float padding) {
            label->SetPadding(padding);
            return *this;
        }

        LabelBuilder& SetAutoResize(bool autoResize = true) {
            label->SetAutoResize(autoResize);
            return *this;
        }

        LabelBuilder& SetStyle(const LabelStyle& style) {
            label->SetStyle(style);
            return *this;
        }

        LabelBuilder& OnClick(std::function<void()> callback) {
            label->onClick = callback;
            return *this;
        }

        std::shared_ptr<UltraCanvasLabel> Build() {
            return label;
        }
    };

// ===== CONVENIENCE BUILDER =====
    inline LabelBuilder CreateLabelBuilder(const std::string& identifier, long id, long x, long y, long w = 100, long h = 25) {
        return LabelBuilder(identifier, id, x, y, w, h);
    }

} // namespace UltraCanvas
