// UltraCanvasCheckbox.cpp
// Checkbox indicator rendering and three-state toggle logic.
// Version: 2.0.0
// Last Modified: 2026-05-07
// Author: UltraCanvas Framework

#include "UltraCanvasCheckbox.h"

namespace UltraCanvas {

    UltraCanvasCheckbox::UltraCanvasCheckbox(const std::string& identifier,
                                             float x, float y, float w, float h,
                                             const std::string& labelText)
            : UltraCanvasLabeledToggleBase(identifier, x, y, w, h, labelText) {}

    void UltraCanvasCheckbox::Toggle() {
        switch (checkState) {
            case CheckedState::Unchecked:
                SetCheckState(CheckedState::Checked);
                break;
            case CheckedState::Checked:
                SetCheckState(allowIndeterminate ? CheckedState::Indeterminate
                                                 : CheckedState::Unchecked);
                break;
            case CheckedState::Indeterminate:
                SetCheckState(CheckedState::Unchecked);
                break;
        }
    }

    void UltraCanvasCheckbox::SetIndeterminate(bool indeterminate) {
        if (allowIndeterminate) {
            SetCheckState(indeterminate ? CheckedState::Indeterminate : CheckedState::Unchecked);
        }
    }

    void UltraCanvasCheckbox::SetColors(const Color& box, const Color& checkmark, const Color& text) {
        visualStyle.boxColor = box;
        visualStyle.checkmarkColor = checkmark;
        visualStyle.base.textColor = text;
        RequestRedraw();
    }

    void UltraCanvasCheckbox::SetFont(const std::string& family, float size, FontWeight weight) {
        visualStyle.base.fontFamily = family;
        visualStyle.base.fontSize = size;
        visualStyle.base.fontWeight = weight;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasCheckbox::SetFontSize(float size) {
        visualStyle.base.fontSize = size;
        layoutDirty = true;
        RequestRedraw();
    }

    Color UltraCanvasCheckbox::GetCurrentBoxColor() const {
        switch (GetPrimaryState()) {
            case ElementState::Disabled: return visualStyle.boxDisabledColor;
            case ElementState::Pressed:  return visualStyle.boxPressedColor;
            case ElementState::Hovered:  return visualStyle.boxHoverColor;
            default:                     return visualStyle.boxColor;
        }
    }

    Color UltraCanvasCheckbox::GetCurrentCheckmarkColor() const {
        switch (GetPrimaryState()) {
            case ElementState::Disabled: return visualStyle.checkmarkDisabledColor;
            case ElementState::Hovered:  return visualStyle.checkmarkHoverColor;
            default:                     return visualStyle.checkmarkColor;
        }
    }

    float UltraCanvasCheckbox::GetEffectiveCornerRadius() const {
        switch (style) {
            case CheckboxStyle::Standard: return 0.0f;
            case CheckboxStyle::Rounded:
            case CheckboxStyle::Material: return visualStyle.cornerRadius;
        }
        return 0.0f;
    }

    void UltraCanvasCheckbox::DrawIndicator(IRenderContext* ctx) {
        ctx->DrawFilledRectangle(indicatorRect,
                                 GetCurrentBoxColor(),
                                 visualStyle.borderWidth,
                                 visualStyle.boxBorderColor,
                                 GetEffectiveCornerRadius());

        if (checkState == CheckedState::Checked) {
            DrawCheckmark(ctx);
        } else if (checkState == CheckedState::Indeterminate) {
            DrawIndeterminateMark(ctx);
        }
    }

    void UltraCanvasCheckbox::DrawCheckmark(IRenderContext* ctx) {
        Color color = GetCurrentCheckmarkColor();

        float cx = indicatorRect.x + indicatorRect.width / 2.0f;
        float cy = indicatorRect.y + indicatorRect.height / 2.0f;
        float size = indicatorRect.width * 0.7f;

        float x1 = cx - size * 0.35f;
        float y1 = cy;
        float x2 = cx - size * 0.1f;
        float y2 = cy + size * 0.25f;
        float x3 = cx + size * 0.35f;
        float y3 = cy - size * 0.25f;

        ctx->SetStrokeWidth(visualStyle.checkmarkThickness);
        ctx->SetStrokePaint(color);
        ctx->ClearPath();
        ctx->MoveTo(x1, y1);
        ctx->LineTo(x2, y2);
        ctx->LineTo(x3, y3);
        ctx->Stroke();
    }

    void UltraCanvasCheckbox::DrawIndeterminateMark(IRenderContext* ctx) {
        Color color = GetCurrentCheckmarkColor();
        float margin = indicatorRect.width * 0.25f;
        float y = indicatorRect.y + indicatorRect.height / 2.0f;
        Rect2Dd lineRect(indicatorRect.x + margin,
                         y - visualStyle.checkmarkThickness / 2.0f,
                         indicatorRect.width - 2 * margin,
                         visualStyle.checkmarkThickness);
        ctx->DrawFilledRectangle(lineRect, color);
    }

    std::shared_ptr<UltraCanvasCheckbox> UltraCanvasCheckbox::CreateCheckbox(
            const std::string& identifier,
            float x, float y, float w, float h,
            const std::string& text, bool checked) {
        auto cb = std::make_shared<UltraCanvasCheckbox>(identifier, x, y, w, h, text);
        cb->SetChecked(checked);
        if (w == 0 || h == 0) cb->SetAutoSize(true);
        return cb;
    }

} // namespace UltraCanvas
