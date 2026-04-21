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
            textLayout.reset();
            RequestRedraw();

            if (onTextChanged) {
                onTextChanged(text);
            }
        }
    }

    void UltraCanvasLabel::SetStyle(const LabelStyle &newStyle) {
        style = newStyle;
        textLayout.reset();
        RequestRedraw();
    }

    void UltraCanvasLabel::SetFont(const std::string &fontFamily, float fontSize, FontWeight weight) {
        style.fontStyle.fontFamily = fontFamily;
        style.fontStyle.fontSize = fontSize;
        style.fontStyle.fontWeight = weight;
        textLayout.reset();
        RequestRedraw();
    }

    void UltraCanvasLabel::SetFontSize(float fontSize) {
        style.fontStyle.fontSize = fontSize;
        textLayout.reset();
        RequestRedraw();
    }

    void UltraCanvasLabel::SetFontWeight(const FontWeight w) {
        style.fontStyle.fontWeight = w;
        textLayout.reset();
        RequestRedraw();
    }

    void UltraCanvasLabel::SetTextColor(const Color &color) {
        style.textColor = color;
        RequestRedraw();
    }

    void UltraCanvasLabel::SetAlignment(TextAlignment horizontal, VerticalAlignment vertical) {
        style.horizontalAlign = horizontal;
        style.verticalAlign = vertical;
        textLayout.reset();
        RequestRedraw();
    }

    void UltraCanvasLabel::SetWrap(TextWrap wrap) {
        style.wrap = wrap;
        textLayout.reset();
        RequestRedraw();
    }

    void UltraCanvasLabel::SetAutoResize(bool autoRes) {
        autoResize = autoRes;
        textLayout.reset();
        RequestRedraw();
    }

    void UltraCanvasLabel::SetTextIsMarkup(bool markup) {
        isMarkup = markup;
        textLayout.reset();
        RequestRedraw();
    }

    int UltraCanvasLabel::GetPreferredWidth() {
        if (!textLayout) {
            UpdateGeometry(GetRenderContext());
        }
        return textLayout->GetLayoutWidth() + GetTotalPaddingHorizontal() + GetTotalBorderHorizontal();
    }

    int UltraCanvasLabel::GetPreferredHeight() {
        if (!textLayout) {
            UpdateGeometry(GetRenderContext());
        }
        return textLayout->GetLayoutHeight() + GetTotalPaddingVertical() + GetTotalBorderVertical();
    }

//    void UltraCanvasLabel::AutoResize(const Size2Di &textDimensions) {
//        if (text.empty()) {
//            preferredSize = Size2Di(GetTotalPaddingHorizontal() + GetTotalBorderHorizontal() + 20,
//                                    GetTotalPaddingVertical() + GetTotalBorderVertical() + style.fontStyle.fontSize +
//                                    4);
//        } else {
//            // Set text style for measurement
//            if (textDimensions.width > 0) {
//                preferredSize = Size2Di(
//                        textDimensions.width + GetTotalPaddingHorizontal() + GetTotalBorderHorizontal(),
//                        textDimensions.height + GetTotalPaddingVertical() + GetTotalBorderVertical()
//                );
//            } else {
//                preferredSize = GetSize();
//            }
//        }
//
//        SetSize(preferredSize.width, preferredSize.height);
//    }
//
//    void UltraCanvasLabel::CalculateLayout(IRenderContext *ctx) {
//        ctx->PushState();
//        Rect2Di bounds = GetBounds();
//        ctx->SetFontStyle(style.fontStyle);
//        ctx->SetTextIsMarkup(isMarkup);
//        Size2Di textDimensions;
//        if (autoResize) {
//            ctx->GetTextDimensions(text, 99999, 0, textDimensions.width, textDimensions.height);
//            AutoResize(textDimensions);
//        } else if (GetHeight() == 0 && GetWidth() > 0) {
//            ctx->GetTextDimensions(text, GetWidth(), 0, textDimensions.width, textDimensions.height);
//            AutoResize(textDimensions);
//        } else if (GetWidth() == 0 && GetHeight() > 0) {
//            ctx->GetTextDimensions(text, 0, GetHeight(), textDimensions.width, textDimensions.height);
//            AutoResize(textDimensions);
//        }
//
//        // Calculate text area (inside padding and borders)
//        textArea = GetContentRect();
//
//        layoutDirty = false;
//        ctx->PopState();
//    }

    // ===== EVENT HANDLING =====
    bool UltraCanvasLabel::OnEvent(const UCEvent &event) {
        if (UltraCanvasUIElement::OnEvent(event)) {
            return true;
        }        

        switch (event.type) {
            case UCEventType::MouseDown:
                if (Contains(event.pointer)) {
                    SetFocus(true);
                    if (onClick) {
                        onClick();
                    }
                    return true;
                }
                break;

            case UCEventType::MouseMove:
                if (Contains(event.pointer)) {
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

        return false;
    }

    // ===== SIZE CHANGES =====
    void UltraCanvasLabel::SetBounds(const Rect2Di &bnds) {
        if (bnds != bounds) {
            UltraCanvasUIElement::SetBounds(bnds);
            textLayout.reset();
            RequestRedraw();
        }
    }

    void UltraCanvasLabel::UpdateGeometry(IRenderContext* ctx) {
        // Update layout if needed
        if (!textLayout) {
            auto crect = GetContentRect();
            textLayout = ctx->CreateTextLayout(text, isMarkup);
            textLayout->SetFontStyle(style.fontStyle);
            textLayout->SetWrap(style.wrap);
            textLayout->SetAlignment(style.horizontalAlign);
            textLayout->SetVerticalAlignment(style.verticalAlign);
            if (autoResize) {
                auto lsize = textLayout->GetLayoutSize();
                bounds.width = lsize.width + GetTotalBorderHorizontal() + GetTotalPaddingHorizontal();
                bounds.height = lsize.height + GetTotalBorderVertical() + GetTotalPaddingVertical();
            } else if (GetHeight() == 0 && GetWidth() > 0) {
                textLayout->SetExplicitWidth(crect.width);
                bounds.height = textLayout->GetLayoutHeight() + GetTotalBorderVertical() + GetTotalPaddingVertical();
            } else if (GetWidth() == 0 && GetHeight() > 0) {
                textLayout->SetExplicitHeight(crect.height);
                bounds.width = textLayout->GetLayoutWidth() + GetTotalBorderHorizontal() + GetTotalPaddingHorizontal();
            } else {
                textLayout->SetEllipsize(EllipsizeMode::EllipsizeEnd);
                textLayout->SetExplicitWidth(crect.width);
                textLayout->SetExplicitHeight(crect.height);
            }
        }
//        if (layoutDirty) {
//            CalculateLayout(ctx);
//        }
        UltraCanvasUIElement::UpdateGeometry(ctx);
    }

    void UltraCanvasLabel::Render(IRenderContext *ctx) {
        UpdateGeometry(ctx);

        UltraCanvasUIElement::Render(ctx);

        if (!text.empty()) {
            // Element-local content rect: ctx is already translated to element origin
            int contentX = GetBorderLeftWidth() + padding.left;
            int contentY = GetBorderTopWidth() + padding.top;
            if (style.hasShadow) {
                ctx->SetCurrentPaint(style.shadowColor);
                //textLayout->ChangeAttribute(TextAttributeFactory::CreateForeground(style.shadowColor));
                ctx->DrawTextLayout(*textLayout, {contentX + style.shadowOffset.x, contentY + style.shadowOffset.y});
            }
//            textLayout->ChangeAttribute(TextAttributeFactory::CreateForeground(style.textColor));
            ctx->SetCurrentPaint(style.textColor);
            ctx->DrawTextLayout(*textLayout, Point2Di(contentX, contentY));
        }
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