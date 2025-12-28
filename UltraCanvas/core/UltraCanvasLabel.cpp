// core/UltraCanvasLabel.cpp
// Implementation of base layout class
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasLabel.h"

namespace UltraCanvas {

    LabelStyle LabelStyle::HeaderStyle() {
        LabelStyle style;
        style.fontStyle.fontSize = 18.0f;
        style.fontStyle.fontWeight = FontWeight::Bold;
        style.textColor = Color(40, 40, 40, 255);
        return style;
    }

    LabelStyle LabelStyle::SubHeaderStyle() {
        LabelStyle style;
        style.fontStyle.fontSize = 14.0f;
        style.fontStyle.fontWeight = FontWeight::Bold;
        style.textColor = Color(60, 60, 60, 255);
        return style;
    }

    LabelStyle LabelStyle::CaptionStyle() {
        LabelStyle style;
        style.fontStyle.fontSize = 10.0f;
        style.textColor = Color(120, 120, 120, 255);
        return style;
    }

    LabelStyle LabelStyle::StatusStyle() {
        LabelStyle style;
        style.fontStyle.fontSize = 11.0f;
        style.textColor = Color(100, 100, 100, 255);
        return style;
    }

    UltraCanvasLabel::UltraCanvasLabel(const std::string &identifier, long id, long x, long y, long w, long h,
                                       const std::string &labelText)
            : UltraCanvasUIElement(identifier, id, x, y, w, h), text(labelText) {

        // Initialize style
        style = LabelStyle::DefaultStyle();
        SetText(labelText);
    }

    UltraCanvasLabel::UltraCanvasLabel(const std::string &identifier, long w, long h, const std::string &labelText)
            : UltraCanvasUIElement(identifier, w, h), text(labelText) {

        // Initialize style
        style = LabelStyle::DefaultStyle();
        SetText(labelText);
    }

    void UltraCanvasLabel::SetText(const std::string &newText) {
        if (text != newText) {
            text = newText;
            layoutDirty = true;
            RequestRedraw();

            if (onTextChanged) {
                onTextChanged(text);
            }
        }
    }

    bool UltraCanvasLabel::IsEmpty() const {
        return text.empty();
    }

    void UltraCanvasLabel::ClearText() {
        SetText("");
    }

    void UltraCanvasLabel::AppendText(const std::string &additionalText) {
        SetText(text + additionalText);
    }

    void UltraCanvasLabel::SetStyle(const LabelStyle &newStyle) {
        style = newStyle;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasLabel::SetFont(const std::string &fontFamily, float fontSize, FontWeight weight) {
        style.fontStyle.fontFamily = fontFamily;
        style.fontStyle.fontSize = fontSize;
        style.fontStyle.fontWeight = weight;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasLabel::SetFontSize(float fontSize) {
        style.fontStyle.fontSize = fontSize;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasLabel::SetFontWeight(const FontWeight w) {
        style.fontStyle.fontWeight = w;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasLabel::SetTextColor(const Color &color) {
        style.textColor = color;
        RequestRedraw();
    }

    void UltraCanvasLabel::SetAlignment(TextAlignment horizontal, TextVerticalAlignment vertical) {
        style.horizontalAlign = horizontal;
        style.verticalAlign = vertical;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasLabel::SetWordWrap(bool wrap) {
        style.wordWrap = wrap;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasLabel::SetAutoResize(bool autoResize) {
        style.autoResize = autoResize;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasLabel::SetTextIsMarkup(bool markup) {
        style.isMarkup = markup;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasLabel::AutoResize(const Size2Di &textDimensions) {
        if (text.empty()) {
            preferredSize = Size2Di(GetTotalPaddingHorizontal() + GetTotalBorderHorizontal() + 20,
                                    GetTotalPaddingVertical() + GetTotalBorderVertical() + style.fontStyle.fontSize +
                                    4);
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

    int UltraCanvasLabel::GetPreferredWidth() {
        if (layoutDirty) {
            CalculateLayout(GetRenderContext());
        }
        return preferredSize.width > 0 ? preferredSize.width : bounds.width;
    }

    int UltraCanvasLabel::GetPreferredHeight() {
        if (layoutDirty) {
            CalculateLayout(GetRenderContext());
        }
        return preferredSize.height > 0 ? preferredSize.height : bounds.height;
    }

    void UltraCanvasLabel::CalculateLayout(IRenderContext *ctx) {
        Rect2Di bounds = GetBounds();
        ctx->PushState();
        ctx->SetFontStyle(style.fontStyle);
        ctx->SetTextIsMarkup(style.isMarkup);
        Size2Di textDimensions;
        if (style.autoResize) {
            ctx->GetTextDimensions(text, 99999, 0, textDimensions.width, textDimensions.height);
            AutoResize(textDimensions);
        } else if (GetHeight() == 0 && GetWidth() > 0) {
            ctx->GetTextDimensions(text, GetWidth(), 0, textDimensions.width, textDimensions.height);
            AutoResize(textDimensions);
        } else if (GetWidth() == 0 && GetHeight() > 0) {
            ctx->GetTextDimensions(text, 0, GetHeight(), textDimensions.width, textDimensions.height);
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

    // ===== EVENT HANDLING =====
    bool UltraCanvasLabel::OnEvent(const UCEvent &event) {
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
    void UltraCanvasLabel::SetBounds(const Rect2Di &bounds) {
        UltraCanvasUIElement::SetBounds(bounds);
        layoutDirty = true;
    }

    void UltraCanvasLabel::Render(IRenderContext *ctx) {
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


    std::shared_ptr<UltraCanvasLabel>
    CreateLabel(const std::string &identifier, long id, long x, long y, long w, long h,
                const std::string &text) {
        return std::make_shared<UltraCanvasLabel>(identifier, id, x, y, w, h, text);
    }

    std::shared_ptr<UltraCanvasLabel>
    CreateLabel(const std::string &identifier, long x, long y, long w, long h,
                const std::string &text) {
        return std::make_shared<UltraCanvasLabel>(identifier, 0, x, y, w, h, text);
    }

    std::shared_ptr<UltraCanvasLabel>
    CreateLabel(const std::string &identifier, long w, long h, const std::string &text) {
        return std::make_shared<UltraCanvasLabel>(identifier, 0, 0, 0, w, h, text);
    }

    std::shared_ptr<UltraCanvasLabel> CreateLabel(const std::string &text) {
        return std::make_shared<UltraCanvasLabel>("", 0, 0, 0, 0, 0, text);
    }

    std::shared_ptr<UltraCanvasLabel>
    CreateAutoLabel(const std::string &identifier, long id, long x, long y, const std::string &text) {
        auto label = std::make_shared<UltraCanvasLabel>(identifier, id, x, y, 100, 25, text);
        label->SetAutoResize(true);
        return label;
    }

    std::shared_ptr<UltraCanvasLabel>
    CreateHeaderLabel(const std::string &identifier, long id, long x, long y, long w, long h,
                      const std::string &text) {
        auto label = CreateLabel(identifier, id, x, y, w, h, text);
        label->SetStyle(LabelStyle::HeaderStyle());
        return label;
    }

    std::shared_ptr<UltraCanvasLabel>
    CreateStatusLabel(const std::string &identifier, long id, long x, long y, long w, long h,
                      const std::string &text) {
        auto label = CreateLabel(identifier, id, x, y, w, h, text);
        label->SetStyle(LabelStyle::StatusStyle());
        label->SetPadding(4);
        return label;
    }

    LabelBuilder::LabelBuilder(const std::string &identifier, long id, long x, long y, long w, long h) {
        label = CreateLabel(identifier, id, x, y, w, h);
    }

    LabelBuilder &LabelBuilder::SetText(const std::string &text) {
        label->SetText(text);
        return *this;
    }

    LabelBuilder &
    LabelBuilder::SetFont(const std::string &fontFamily, float fontSize) {
        label->SetFont(fontFamily, fontSize);
        return *this;
    }

    LabelBuilder &LabelBuilder::SetTextColor(const Color &color) {
        label->SetTextColor(color);
        return *this;
    }

    LabelBuilder &LabelBuilder::SetBackgroundColor(const Color &color) {
        label->SetBackgroundColor(color);
        return *this;
    }

    LabelBuilder &LabelBuilder::SetAlignment(TextAlignment align) {
        label->SetAlignment(align);
        return *this;
    }

    LabelBuilder &LabelBuilder::SetPadding(float padding) {
        label->SetPadding(padding);
        return *this;
    }

    LabelBuilder &LabelBuilder::SetAutoResize(bool autoResize) {
        label->SetAutoResize(autoResize);
        return *this;
    }

    LabelBuilder &LabelBuilder::SetStyle(const LabelStyle &style) {
        label->SetStyle(style);
        return *this;
    }

    LabelBuilder &LabelBuilder::OnClick(std::function<void()> callback) {
        label->onClick = callback;
        return *this;
    }

}