// UltraCanvasCheckbox.cpp
// Interactive checkbox component with multiple states and customizable appearance
// Version: 1.1.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasCheckbox.h"
#include "UltraCanvasApplication.h"
#include <algorithm>

namespace UltraCanvas {

// ===== CONSTRUCTOR =====
    UltraCanvasCheckbox::UltraCanvasCheckbox(const std::string& identifier, long id,
                                             long x, long y, long w, long h,
                                             const std::string& labelText)
            : UltraCanvasUIElement(identifier, id, x, y, w, h), text(labelText) {

        checkState = CheckboxState::Unchecked;
        style = CheckboxStyle::Standard;
        allowIndeterminate = false;

        layoutDirty = true;
    }

// ===== LAYOUT CALCULATION =====
    void UltraCanvasCheckbox::CalculateLayout() {
        auto ctx = GetRenderContext();
        if (!ctx) return;

        // Calculate checkbox box position (left-aligned with padding)
        float padding = 4.0f;
        boxRect.x = GetX() + padding;
        boxRect.y = GetY() + (GetHeight() - visualStyle.boxSize) / 2.0f;
        boxRect.width = visualStyle.boxSize;
        boxRect.height = visualStyle.boxSize;

        // Calculate text position
        if (!text.empty()) {
            textRect.x = boxRect.x + boxRect.width + visualStyle.textSpacing;
            textRect.y = GetY();
            textRect.width = GetWidth() - (textRect.x - GetX()) - padding;
            textRect.height = GetHeight();
        }

        // Calculate total bounds for hit testing
        totalBounds.x = GetX();
        totalBounds.y = GetY();
        totalBounds.width = GetWidth();
        totalBounds.height = GetHeight();

        layoutDirty = false;
    }

// ===== STATE MANAGEMENT =====
    void UltraCanvasCheckbox::SetChecked(bool checked) {
        SetCheckState(checked ? CheckboxState::Checked : CheckboxState::Unchecked);
    }

    void UltraCanvasCheckbox::SetCheckState(CheckboxState state) {
        if (checkState != state) {
            CheckboxState oldState = checkState;
            checkState = state;

            // Fire callbacks
            if (onStateChanged) {
                onStateChanged(oldState, state);
            }

            switch (state) {
                case CheckboxState::Checked:
                    if (onChecked) onChecked();
                    break;
                case CheckboxState::Unchecked:
                    if (onUnchecked) onUnchecked();
                    break;
                case CheckboxState::Indeterminate:
                    if (onIndeterminate) onIndeterminate();
                    break;
            }

            RequestRedraw();
        }
    }

    void UltraCanvasCheckbox::SetIndeterminate(bool indeterminate) {
        if (allowIndeterminate) {
            SetCheckState(indeterminate ? CheckboxState::Indeterminate : CheckboxState::Unchecked);
        }
    }

    void UltraCanvasCheckbox::Toggle() {
        switch (checkState) {
            case CheckboxState::Unchecked:
                SetCheckState(CheckboxState::Checked);
                break;
            case CheckboxState::Checked:
                if (allowIndeterminate) {
                    SetCheckState(CheckboxState::Indeterminate);
                } else {
                    SetCheckState(CheckboxState::Unchecked);
                }
                break;
            case CheckboxState::Indeterminate:
                SetCheckState(CheckboxState::Unchecked);
                break;
        }
    }

// ===== APPEARANCE SETTERS =====
    void UltraCanvasCheckbox::SetColors(const Color& box, const Color& checkmark, const Color& text) {
        visualStyle.boxColor = box;
        visualStyle.checkmarkColor = checkmark;
        visualStyle.textColor = text;
        RequestRedraw();
    }

    void UltraCanvasCheckbox::SetFont(const std::string& family, float size, FontWeight weight) {
        visualStyle.fontFamily = family;
        visualStyle.fontSize = size;
        visualStyle.fontWeight = weight;
        layoutDirty = true;
        RequestRedraw();
    }

// ===== COLOR HELPERS =====
    Color UltraCanvasCheckbox::GetCurrentBoxColor() const {
        if (!IsActive()) {
            return visualStyle.boxDisabledColor;
        }

        if (isPressed) {
            return visualStyle.boxPressedColor;
        }

        if (isHovered) {
            return visualStyle.boxHoverColor;
        }

        return visualStyle.boxColor;
    }

    Color UltraCanvasCheckbox::GetCurrentCheckmarkColor() const {
        if (!IsActive()) {
            return visualStyle.checkmarkDisabledColor;
        }

        if (isHovered) {
            return visualStyle.checkmarkHoverColor;
        }

        return visualStyle.checkmarkColor;
    }

// ===== RENDERING =====
    void UltraCanvasCheckbox::Render() {
        if (!IsVisible()) return;

        auto ctx = GetRenderContext();
        if (!ctx) return;

        if (layoutDirty) {
            if (autoSize) {
                CalculateAutoSize();
            }
            CalculateLayout();
        }

        ctx->PushState();

        // Draw based on style
        switch (style) {
            case CheckboxStyle::Standard:
            case CheckboxStyle::Rounded:
            case CheckboxStyle::Material:
                DrawCheckbox(ctx);
                break;
            case CheckboxStyle::Switch:
                // TODO: Implement switch style rendering
                DrawCheckbox(ctx);
                break;
            case CheckboxStyle::Radio:
                // TODO: Implement radio button style
                DrawCheckbox(ctx);
                break;
        }

        // Draw label text
        if (!text.empty()) {
            DrawLabel(ctx);
        }

        // Draw focus ring if needed
        if (hasFocus && visualStyle.hasFocusRing) {
            DrawFocusRing(ctx);
        }

        ctx->PopState();
    }

    void UltraCanvasCheckbox::DrawCheckbox(IRenderContext* ctx) {
        // Get current colors
        Color boxColor = GetCurrentBoxColor();
        Color borderColor = visualStyle.boxBorderColor;

        // Draw checkbox box
        float radius = (style == CheckboxStyle::Rounded) ? visualStyle.cornerRadius : 0.0f;

        ctx->DrawFilledRectangle(boxRect, boxColor, visualStyle.borderWidth, visualStyle.boxBorderColor, radius);

        // Draw check mark or indeterminate mark
        if (checkState == CheckboxState::Checked) {
            DrawCheckmark(ctx);
        } else if (checkState == CheckboxState::Indeterminate) {
            DrawIndeterminateMark(ctx);
        }
    }

    void UltraCanvasCheckbox::DrawCheckmark(IRenderContext* ctx) {
        Color checkColor = GetCurrentCheckmarkColor();

        // Calculate checkmark path points
        float boxCenterX = boxRect.x + boxRect.width / 2.0f;
        float boxCenterY = boxRect.y + boxRect.height / 2.0f;
        float checkSize = boxRect.width * 0.7f;

        // Draw checkmark as two lines forming a check
        float x1 = boxCenterX - checkSize * 0.35f;
        float y1 = boxCenterY;
        float x2 = boxCenterX - checkSize * 0.1f;
        float y2 = boxCenterY + checkSize * 0.25f;
        float x3 = boxCenterX + checkSize * 0.35f;
        float y3 = boxCenterY - checkSize * 0.25f;

        ctx->SetStrokeWidth(visualStyle.checkmarkThickness);
        ctx->SetStrokePaint(checkColor);

        // Draw the checkmark
        ctx->ClearPath();
        ctx->MoveTo(x1, y1);
        ctx->LineTo(x2, y2);
        ctx->LineTo(x3, y3);
        ctx->Stroke();
    }

    void UltraCanvasCheckbox::DrawIndeterminateMark(IRenderContext* ctx) {
        Color markColor = GetCurrentCheckmarkColor();

        // Draw horizontal line in the middle of the box
        float margin = boxRect.width * 0.25f;
        float y = boxRect.y + boxRect.height / 2.0f;

        Rect2Df lineRect(
                boxRect.x + margin,
                y - visualStyle.checkmarkThickness / 2.0f,
                boxRect.width - 2 * margin,
                visualStyle.checkmarkThickness
        );

        ctx->DrawFilledRectangle(lineRect, markColor);
    }

    void UltraCanvasCheckbox::DrawLabel(IRenderContext* ctx) {
        Color textColor = IsActive() ?
                          (isHovered ? visualStyle.textHoverColor : visualStyle.textColor) :
                          visualStyle.textDisabledColor;

        ctx->SetFontFace(visualStyle.fontFamily, visualStyle.fontWeight, FontSlant::Normal);
        ctx->SetFontSize(visualStyle.fontSize);
        ctx->SetTextPaint(textColor);

        // Draw text aligned to the middle of the text rect
        float textY = textRect.y + (textRect.height - ctx->GetTextHeight(text)) / 2.0f;
        ctx->DrawText(text, textRect.x, textY);
    }

    void UltraCanvasCheckbox::DrawFocusRing(IRenderContext* ctx) {
        Rect2Df focusRect(
                boxRect.x - visualStyle.focusRingWidth,
                boxRect.y - visualStyle.focusRingWidth,
                boxRect.width + 2 * visualStyle.focusRingWidth,
                boxRect.height + 2 * visualStyle.focusRingWidth
        );

        float radius = (style == CheckboxStyle::Rounded) ?
                       visualStyle.cornerRadius + visualStyle.focusRingWidth : 0.0f;

        ctx->DrawFilledRectangle(focusRect, visualStyle.focusRingColor,
                                  visualStyle.focusRingWidth, visualStyle.focusRingColor, radius);
    }

// ===== EVENT HANDLING =====
    bool UltraCanvasCheckbox::OnEvent(const UCEvent& event) {
        if (!IsActive() || !IsVisible()) return false;

        bool handled = false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (totalBounds.Contains(event.x, event.y)) {
                    isPressed = true;
                    handled = true;
                    RequestRedraw();
                }
                break;

            case UCEventType::MouseUp:
                if (isPressed && totalBounds.Contains(event.x, event.y)) {
                    Toggle();
                    handled = true;
                }
                isPressed = false;
                RequestRedraw();
                break;

            case UCEventType::MouseMove:
            {
                bool wasHovered = isHovered;
                isHovered = totalBounds.Contains(event.x, event.y);
                if (wasHovered != isHovered) {
                    RequestRedraw();
                }
            }
                break;

            case UCEventType::MouseEnter:
                isHovered = true;
                RequestRedraw();
                break;

            case UCEventType::MouseLeave:
                isHovered = false;
                isPressed = false;
                RequestRedraw();
                break;

            case UCEventType::KeyDown:
                if (hasFocus) {
                    if (event.virtualKey == UCKeys::Space || event.virtualKey == UCKeys::Enter) {
                        Toggle();
                        handled = true;
                        RequestRedraw();
                    }
                }
                break;

            case UCEventType::FocusGained:
                hasFocus = true;
                RequestRedraw();
                handled = true;
                break;

            case UCEventType::FocusLost:
                hasFocus = false;
                RequestRedraw();
                handled = true;
                break;

            default:
                break;
        }

        // Call base class if not handled
        if (!handled) {
            return UltraCanvasUIElement::OnEvent(event);
        }

        return handled;
    }

// ===== AUTO-SIZING =====
    void UltraCanvasCheckbox::CalculateAutoSize() {
        auto ctx = GetRenderContext();
        if (!ctx || text.empty()) return;

        ctx->SetFontFace(visualStyle.fontFamily, visualStyle.fontWeight, FontSlant::Normal);
        ctx->SetFontSize(visualStyle.fontSize);
        int textWidth, textHeight;
        ctx->GetTextDimension(text, textWidth, textHeight);

        // Calculate total width needed
        float totalWidth = 8.0f + visualStyle.boxSize + visualStyle.textSpacing + textWidth + 8.0f;
        float totalHeight = std::max(visualStyle.boxSize + 8.0f, static_cast<float>(textHeight + 8));

        SetSize(static_cast<int>(totalWidth), static_cast<int>(totalHeight));
    }

// ===== FACTORY METHODS =====
    std::shared_ptr<UltraCanvasCheckbox> UltraCanvasCheckbox::CreateCheckbox(
            const std::string& identifier, long id,
            long x, long y, long w, long h,
            const std::string& text, bool checked) {

        auto checkbox = std::make_shared<UltraCanvasCheckbox>(identifier, id, x, y, w, h, text);
        checkbox->SetChecked(checked);

        if (w == 0 || h == 0) {
            checkbox->SetAutoSize(true);
        }

        return checkbox;
    }

    std::shared_ptr<UltraCanvasCheckbox> UltraCanvasCheckbox::CreateSwitch(
            const std::string& identifier, long id,
            long x, long y,
            const std::string& text, bool checked) {

        auto switchBox = std::make_shared<UltraCanvasCheckbox>(identifier, id, x, y, 200, 30, text);
        switchBox->SetStyle(CheckboxStyle::Switch);
        switchBox->SetChecked(checked);

        // Configure switch-specific visual style
        switchBox->visualStyle.boxSize = 20.0f;
        switchBox->visualStyle.cornerRadius = 10.0f;

        switchBox->SetAutoSize(true);
        return switchBox;
    }

    std::shared_ptr<UltraCanvasCheckbox> UltraCanvasCheckbox::CreateRadioButton(
            const std::string& identifier, long id,
            long x, long y,
            const std::string& text, bool checked) {

        auto radio = std::make_shared<UltraCanvasCheckbox>(identifier, id, x, y, 150, 24, text);
        radio->SetStyle(CheckboxStyle::Radio);
        radio->SetChecked(checked);
        radio->SetAllowIndeterminate(false);  // Radio buttons don't support indeterminate

        // Configure radio-specific visual style
        radio->visualStyle.cornerRadius = radio->visualStyle.boxSize / 2.0f;  // Make it circular

        radio->SetAutoSize(true);
        return radio;
    }

// ===== RADIO GROUP IMPLEMENTATION =====
    void UltraCanvasRadioGroup::AddRadioButton(std::shared_ptr<UltraCanvasCheckbox> button) {
        if (!button || button->GetStyle() != CheckboxStyle::Radio) return;

        radioButtons.push_back(button);

        // Set up callback to handle exclusive selection
        button->onChecked = [this, button]() {
            SelectButton(button);
        };
    }

    void UltraCanvasRadioGroup::RemoveRadioButton(std::shared_ptr<UltraCanvasCheckbox> button) {
        radioButtons.erase(
                std::remove(radioButtons.begin(), radioButtons.end(), button),
                radioButtons.end()
        );

        if (selectedButton == button) {
            selectedButton = nullptr;
        }
    }

    void UltraCanvasRadioGroup::SelectButton(std::shared_ptr<UltraCanvasCheckbox> button) {
        if (!button || std::find(radioButtons.begin(), radioButtons.end(), button) == radioButtons.end()) {
            return;
        }

        // Uncheck all other buttons
        for (auto& radioButton : radioButtons) {
            if (radioButton != button && radioButton->IsChecked()) {
                radioButton->SetChecked(false);
            }
        }

        selectedButton = button;

        if (onSelectionChanged) {
            onSelectionChanged(button);
        }
    }

    void UltraCanvasRadioGroup::ClearSelection() {
        for (auto& button : radioButtons) {
            button->SetChecked(false);
        }
        selectedButton = nullptr;

        if (onSelectionChanged) {
            onSelectionChanged(nullptr);
        }
    }

} // namespace UltraCanvas