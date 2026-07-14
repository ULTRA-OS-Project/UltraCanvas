// core/UltraCanvasSpinner.cpp
// Platform-independent spinner / spin-button component implementation.
// Version: 1.1.0
// Last Modified: 2026-07-13
// Author: UltraCanvas Framework

#include "UltraCanvasSpinner.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasMenu.h"
#include <cstdio>
#include <cmath>
#include <sstream>

namespace UltraCanvas {

    UltraCanvasSpinner::UltraCanvasSpinner(const std::string& identifier,
                                           float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        mouseCursor = UCMouseCursor::Default;
    }

// ===================================================================
// VALUE MODEL
// ===================================================================

    void UltraCanvasSpinner::SetRange(double minVal, double maxVal) {
        if (minVal > maxVal) std::swap(minVal, maxVal);
        minValue = minVal;
        maxValue = maxVal;
        // Re-clamp the current value into the new range.
        ApplyValue(value, false);
    }

    void UltraCanvasSpinner::SetValue(double newValue) {
        ApplyValue(newValue, false);
    }

    void UltraCanvasSpinner::SetDecimals(int d) {
        decimals = std::max(0, d);
        if (decimals > 0 && valueType == SpinnerValueType::Integer) {
            valueType = SpinnerValueType::Decimal;
        }
        RequestRedraw();
    }

    void UltraCanvasSpinner::SetValueType(SpinnerValueType type) {
        valueType = type;
        if (type == SpinnerValueType::Integer) decimals = 0;
        if (type == SpinnerValueType::List) {
            // Keep the range locked to the list bounds.
            minValue = 0.0;
            maxValue = listItems.empty() ? 0.0 : static_cast<double>(listItems.size() - 1);
            step = 1.0;
        }
        RequestRedraw();
    }

    void UltraCanvasSpinner::StepBy(double steps) {
        double delta = (valueType == SpinnerValueType::List ? 1.0 : step) * steps;
        ApplyValue(value + delta, true);
    }

    double UltraCanvasSpinner::ClampWrap(double v) const {
        if (maxValue <= minValue) return minValue;
        if (wrap) {
            double span = maxValue - minValue;
            if (valueType == SpinnerValueType::List || valueType == SpinnerValueType::Integer) {
                // Integer wrap over an inclusive range of (span+1) slots.
                double slots = span + 1.0;
                double rel = std::fmod(v - minValue, slots);
                if (rel < 0) rel += slots;
                return minValue + rel;
            }
            // Continuous wrap.
            double rel = std::fmod(v - minValue, span);
            if (rel < 0) rel += span;
            return minValue + rel;
        }
        return std::max(minValue, std::min(maxValue, v));
    }

    double UltraCanvasSpinner::SnapToStep(double v) const {
        if (valueType == SpinnerValueType::List || valueType == SpinnerValueType::Integer) {
            return std::llround(v);
        }
        if (step > 0.0) {
            double steps = std::round((v - minValue) / step);
            v = minValue + steps * step;
        }
        // Guard against floating drift beyond the range after snapping.
        return v;
    }

    void UltraCanvasSpinner::ApplyValue(double newValue, bool interactive) {
        double snapped = SnapToStep(newValue);
        double clamped = ClampWrap(snapped);
        // After a wrap the snapped value can still land off-grid at the far end;
        // re-snap once more so wrapped values stay on the step grid.
        clamped = ClampWrap(SnapToStep(clamped));

        bool changed = std::abs(clamped - value) > 1e-9;
        value = clamped;

        if (changed) {
            if (interactive && onValueChanging) onValueChanging(value);
            if (onValueChanged) onValueChanged(value);
            if (valueType == SpinnerValueType::List && onSelectionChanged) {
                onSelectionChanged(GetSelectedIndex(), GetSelectedText());
            }
        }
        RequestRedraw();
    }

// ===================================================================
// LIST MODE
// ===================================================================

    void UltraCanvasSpinner::SetListItems(const std::vector<std::string>& items) {
        listItems = items;
        valueType = SpinnerValueType::List;
        minValue = 0.0;
        maxValue = listItems.empty() ? 0.0 : static_cast<double>(listItems.size() - 1);
        step = 1.0;
        value = std::min(value, maxValue);
        value = std::max(0.0, value);
        RequestRedraw();
    }

    void UltraCanvasSpinner::AddListItem(const std::string& item) {
        listItems.push_back(item);
        if (valueType == SpinnerValueType::List) {
            maxValue = static_cast<double>(listItems.size() - 1);
        }
        RequestRedraw();
    }

    void UltraCanvasSpinner::ClearListItems() {
        listItems.clear();
        if (valueType == SpinnerValueType::List) {
            minValue = maxValue = value = 0.0;
        }
        RequestRedraw();
    }

    void UltraCanvasSpinner::SetSelectedIndex(int index) {
        SetValue(static_cast<double>(index));
    }

    int UltraCanvasSpinner::GetSelectedIndex() const {
        if (listItems.empty()) return -1;
        return static_cast<int>(std::llround(value));
    }

    std::string UltraCanvasSpinner::GetSelectedText() const {
        int idx = GetSelectedIndex();
        if (idx < 0 || idx >= static_cast<int>(listItems.size())) return "";
        return listItems[idx];
    }

// ===================================================================
// DISPLAY / FORMATTING
// ===================================================================

    void UltraCanvasSpinner::SetEditable(bool enabled) {
        allowTextEditing = enabled;
        if (!enabled && editing) CancelEditing();
    }

    std::string UltraCanvasSpinner::FormatNumber(double v) const {
        if (formatter) return formatter(v);
        if (valueType == SpinnerValueType::List) return GetSelectedText();

        char buffer[64];
        if (valueType == SpinnerValueType::Integer || decimals <= 0) {
            std::snprintf(buffer, sizeof(buffer), "%lld",
                          static_cast<long long>(std::llround(v)));
        } else {
            std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, v);
        }
        return std::string(buffer);
    }

    std::string UltraCanvasSpinner::GetDisplayText() const {
        std::string body = FormatNumber(value);
        if (body.empty() && !placeholder.empty()) return placeholder;
        return prefix + body + suffix;
    }

    std::string UltraCanvasSpinner::FormatValueForDisplay(double v) const {
        // FormatNumber() reports the *current* selection in List mode, so the
        // list index has to be resolved explicitly here; numeric / formatter
        // modes format the passed value directly.
        std::string body;
        if (valueType == SpinnerValueType::List) {
            int idx = static_cast<int>(std::llround(v));
            if (idx >= 0 && idx < static_cast<int>(listItems.size())) {
                body = listItems[idx];
            }
        } else {
            body = FormatNumber(v);
        }
        return prefix + body + suffix;
    }

// ===================================================================
// GEOMETRY (element-local coordinates; ctx is translated to our origin)
// ===================================================================

    Rect2Di UltraCanvasSpinner::GetFieldRect(const Rect2Di& b) const {
        float t = style.buttonThickness;
        if (layout == SpinnerLayout::SidesHorizontal) {
            return Rect2Di(b.x + t, b.y, b.width - 2 * t, b.height);
        }
        // UpDownRight: buttons take a column on the right.
        return Rect2Di(b.x, b.y, b.width - t, b.height);
    }

    Rect2Di UltraCanvasSpinner::GetIncButtonRect(const Rect2Di& b) const {
        float t = style.buttonThickness;
        if (layout == SpinnerLayout::SidesHorizontal) {
            // Increment button on the right.
            return Rect2Di(b.x + b.width - t, b.y, t, b.height);
        }
        // Up button = top half of the right column.
        int half = b.height / 2;
        return Rect2Di(b.x + b.width - t, b.y, t, half);
    }

    Rect2Di UltraCanvasSpinner::GetDecButtonRect(const Rect2Di& b) const {
        float t = style.buttonThickness;
        if (layout == SpinnerLayout::SidesHorizontal) {
            // Decrement button on the left.
            return Rect2Di(b.x, b.y, t, b.height);
        }
        // Down button = bottom half of the right column.
        int half = b.height / 2;
        return Rect2Di(b.x + b.width - t, b.y + half, t, b.height - half);
    }

    UltraCanvasSpinner::Part UltraCanvasSpinner::HitTest(const Point2Di& p) const {
        Rect2Di b = GetLocalBounds();
        if (GetIncButtonRect(b).Contains(p)) return Part::IncButton;
        if (GetDecButtonRect(b).Contains(p)) return Part::DecButton;
        if (GetFieldRect(b).Contains(p))     return Part::Field;
        return Part::NonePart;
    }

// ===================================================================
// TEXT EDITING
// ===================================================================

    void UltraCanvasSpinner::BeginEditing() {
        if (!IsEditableNumeric()) return;
        editing = true;
        editBuffer = FormatNumber(value);   // seed with current value (no prefix/suffix)
        // The seeded text starts out "selected": the first keystroke replaces
        // it, matching how a native spin box behaves when you focus/click it.
        editBufferFresh = true;
        RequestRedraw();
    }

    void UltraCanvasSpinner::CommitEditing() {
        if (!editing) return;
        editing = false;
        editBufferFresh = false;
        if (!editBuffer.empty()) {
            try {
                double parsed = std::stod(editBuffer);
                ApplyValue(parsed, false);
            } catch (...) {
                // Unparseable input reverts to the previous value.
            }
        }
        editBuffer.clear();
        if (onEditingFinished) onEditingFinished();
        RequestRedraw();
    }

    void UltraCanvasSpinner::CancelEditing() {
        if (!editing) return;
        editing = false;
        editBufferFresh = false;
        editBuffer.clear();
        RequestRedraw();
    }

// ===================================================================
// VALUE DROPDOWN (optional combobox-style value picker)
// ===================================================================

    void UltraCanvasSpinner::SetDropdownEnabled(bool enabled) {
        if (dropdownEnabled == enabled) return;
        dropdownEnabled = enabled;
        if (!enabled && dropdownOpen) CloseValueDropdown();
        RequestRedraw();
    }

    int UltraCanvasSpinner::DropdownItemCount() const {
        if (valueType == SpinnerValueType::List) {
            return static_cast<int>(listItems.size());
        }
        if (maxValue < minValue) return 0;
        double s = (valueType == SpinnerValueType::Integer) ? std::max(1.0, step) : step;
        if (s <= 0.0) return -1;   // no usable grid
        long long n = static_cast<long long>(std::floor((maxValue - minValue) / s + 1e-9)) + 1;
        if (n <= 0) return 0;
        if (n > kMaxDropdownItems) return -1;   // too many to list
        return static_cast<int>(n);
    }

    void UltraCanvasSpinner::OpenValueDropdown() {
        UltraCanvasWindowBase* win = GetWindow();
        if (!win) return;

        int count = DropdownItemCount();
        if (count <= 0) return;   // nothing to show, or too many grid values

        if (!valueMenu) {
            valueMenu = std::make_shared<UltraCanvasMenu>(GetIdentifier() + "_ValueMenu");
            valueMenu->SetMenuType(MenuType::PopupMenu);
            valueMenu->onMenuClosed = [this]() {
                dropdownOpen = false;
                RequestRedraw();
            };
            // Radio items don't self-close the popup, so close on any pick.
            valueMenu->onItemSelected = [this](int) { CloseValueDropdown(); };
        }

        valueMenu->Clear();
        double s = (valueType == SpinnerValueType::Integer) ? std::max(1.0, step) : step;
        const double eps = (valueType == SpinnerValueType::Decimal && s > 0.0) ? s * 0.5 : 0.5;
        for (int i = 0; i < count; ++i) {
            double v = (valueType == SpinnerValueType::List)
                           ? static_cast<double>(i)
                           : SnapToStep(minValue + i * s);
            bool checked = std::abs(v - value) < eps;
            valueMenu->AddItem(MenuItemData::Radio(
                    FormatValueForDisplay(v), /*group*/ 1, checked,
                    [this, v]() { SetValue(v); }));
        }

        Point2Df origin = GetPositionInWindow();
        Rect2Df local   = GetLocalBounds();
        Point2Di pos(static_cast<int>(origin.x),
                     static_cast<int>(origin.y + local.height));

        PopupElementSettings settings;
        settings.popupOwner = shared_from_this();
        settings.closeByEscapeKey = true;
        settings.closeByClickOutside = true;

        dropdownOpen = true;
        valueMenu->OpenMenu(pos, *win, settings);
        RequestRedraw();
    }

    void UltraCanvasSpinner::CloseValueDropdown() {
        if (valueMenu && dropdownOpen) {
            valueMenu->CloseMenu();
        }
        dropdownOpen = false;
    }

// ===================================================================
// RENDERING
// ===================================================================

    void UltraCanvasSpinner::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        Rect2Di bounds = GetLocalBounds();

        Rect2Di fieldRect = GetFieldRect(bounds);
        RenderField(fieldRect, ctx);

        RenderButton(GetIncButtonRect(bounds), Part::IncButton, true,  ctx);
        RenderButton(GetDecButtonRect(bounds), Part::DecButton, false, ctx);
    }

    void UltraCanvasSpinner::RenderField(const Rect2Di& fieldRect, IRenderContext* ctx) {
        Color bg = IsDisabled() ? style.disabledBackgroundColor : style.backgroundColor;
        Color border = IsFocused() ? style.focusBorderColor : style.borderColor;
        ctx->DrawFilledRectangle(fieldRect, bg, style.borderWidth, border, style.cornerRadius);

        // Text to draw: live edit buffer while editing, otherwise formatted value.
        std::string text = editing ? editBuffer : GetDisplayText();
        bool isPlaceholder = (!editing && text == placeholder && FormatNumber(value).empty());

        ctx->SetFontStyle(style.fontStyle);
        Color txtColor = IsDisabled() ? style.disabledTextColor : style.textColor;
        if (isPlaceholder) txtColor = style.disabledTextColor;
        ctx->SetTextPaint(txtColor);

        Point2Di textSize = ctx->GetTextDimension(text);
        int innerLeft  = fieldRect.x + static_cast<int>(style.textPaddingH);
        int innerRight = fieldRect.x + fieldRect.width - static_cast<int>(style.textPaddingH);
        int textY = fieldRect.y + (fieldRect.height - textSize.y) / 2;

        int textX = innerLeft;
        switch (textAlignment) {
            case TextAlignment::Center:
                textX = fieldRect.x + (fieldRect.width - textSize.x) / 2;
                break;
            case TextAlignment::Right:
                textX = innerRight - textSize.x;
                break;
            default:
                textX = innerLeft;
                break;
        }
        // Keep text within the field's padded area.
        textX = std::max(innerLeft, textX);

        ctx->DrawText(text, Point2Di(textX, textY));

        // Caret at the end of the edit buffer.
        if (editing && IsFocused()) {
            int caretX = std::min(textX + textSize.x + 1, innerRight);
            int caretTop = fieldRect.y + 3;
            int caretBottom = fieldRect.y + fieldRect.height - 3;
            ctx->SetStrokePaint(style.caretColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(Point2Di(caretX, caretTop), Point2Di(caretX, caretBottom));
        }
    }

    Color UltraCanvasSpinner::CurrentButtonColor(Part part) const {
        if (IsDisabled()) return style.buttonDisabledColor;
        // A button whose value cannot move any further reads as disabled.
        if (!wrap) {
            if (part == Part::IncButton && value >= maxValue) return style.buttonDisabledColor;
            if (part == Part::DecButton && value <= minValue) return style.buttonDisabledColor;
        }
        if (pressedPart == part) return style.buttonPressedColor;
        if (hoveredPart == part) return style.buttonHoverColor;
        return style.buttonColor;
    }

    void UltraCanvasSpinner::RenderButton(const Rect2Di& rect, Part part,
                                          bool isIncrement, IRenderContext* ctx) {
        ctx->DrawFilledRectangle(rect, CurrentButtonColor(part),
                                 style.borderWidth, style.buttonBorderColor);

        bool atLimit = !wrap &&
                       ((isIncrement && value >= maxValue) ||
                        (!isIncrement && value <= minValue));
        Color glyphColor = (IsDisabled() || atLimit) ? style.glyphDisabledColor : style.glyphColor;
        RenderGlyph(rect, isIncrement, glyphColor, ctx);
    }

    void UltraCanvasSpinner::RenderGlyph(const Rect2Di& rect, bool isIncrement,
                                         const Color& color, IRenderContext* ctx) {
        int cx = rect.x + rect.width / 2;
        int cy = rect.y + rect.height / 2;
        float g = style.glyphSize;
        float h = g / 2.0f;

        // Direction the glyph points: vertical for the stacked layout,
        // horizontal for the side-button layout.
        bool horizontal = (layout == SpinnerLayout::SidesHorizontal);

        ctx->SetFillPaint(color);
        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(1.5f);

        switch (style.glyph) {
            case SpinnerButtonGlyph::PlusMinus: {
                // Horizontal bar (always present), plus a vertical bar for increment.
                ctx->DrawLine(Point2Dd(cx - h, cy), Point2Dd(cx + h, cy));
                if (isIncrement) {
                    ctx->DrawLine(Point2Dd(cx, cy - h), Point2Dd(cx, cy + h));
                }
                break;
            }
            case SpinnerButtonGlyph::Chevron: {
                if (horizontal) {
                    int dir = isIncrement ? 1 : -1;   // right / left
                    ctx->DrawLine(Point2Dd(cx - dir * h, cy - h), Point2Dd(cx + dir * h, cy));
                    ctx->DrawLine(Point2Dd(cx + dir * h, cy),     Point2Dd(cx - dir * h, cy + h));
                } else {
                    int dir = isIncrement ? -1 : 1;   // up / down (y grows downward)
                    ctx->DrawLine(Point2Dd(cx - h, cy - dir * h), Point2Dd(cx, cy + dir * h));
                    ctx->DrawLine(Point2Dd(cx, cy + dir * h),     Point2Dd(cx + h, cy - dir * h));
                }
                break;
            }
            case SpinnerButtonGlyph::Triangle:
            default: {
                std::vector<Point2Dd> tri;
                if (horizontal) {
                    if (isIncrement) {   // pointing right
                        tri = { Point2Dd(cx - h, cy - h), Point2Dd(cx + h, cy), Point2Dd(cx - h, cy + h) };
                    } else {             // pointing left
                        tri = { Point2Dd(cx + h, cy - h), Point2Dd(cx - h, cy), Point2Dd(cx + h, cy + h) };
                    }
                } else {
                    if (isIncrement) {   // pointing up
                        tri = { Point2Dd(cx, cy - h), Point2Dd(cx + h, cy + h), Point2Dd(cx - h, cy + h) };
                    } else {             // pointing down
                        tri = { Point2Dd(cx, cy + h), Point2Dd(cx - h, cy - h), Point2Dd(cx + h, cy - h) };
                    }
                }
                ctx->FillLinePath(tri);
                break;
            }
        }
    }

// ===================================================================
// EVENT HANDLING
// ===================================================================

    bool UltraCanvasSpinner::OnEvent(const UCEvent& event) {
        if (!IsVisible() || IsDisabled()) return false;

        if (UltraCanvasUIElement::OnEvent(event)) return true;

        switch (event.type) {
            case UCEventType::MouseDown:        return HandleMouseDown(event);
            case UCEventType::MouseUp:          return HandleMouseUp(event);
            case UCEventType::MouseMove:        return HandleMouseMove(event);
            case UCEventType::MouseWheel:       return HandleWheel(event);
            case UCEventType::KeyDown:          return HandleKeyDown(event);
            case UCEventType::MouseDoubleClick:
                // A rapid second click arrives as a double-click instead of a
                // MouseDown; on the arrow buttons every click must step.
                if (HitTest(event.pointer) == Part::Field) {
                    // With the dropdown enabled the single clicks already toggle
                    // the popup; don't also start inline editing.
                    if (!dropdownEnabled) BeginEditing();
                    return true;
                }
                return HandleMouseDown(event);
            case UCEventType::MouseEnter:
                SetHovered(true);
                return true;
            case UCEventType::MouseLeave:
                SetHovered(false);
                hoveredPart = Part::NonePart;
                pressedPart = Part::NonePart;
                RequestRedraw();
                return true;
            case UCEventType::FocusLost:
                if (editing) CommitEditing();
                return false;
            default:
                break;
        }
        return false;
    }

    bool UltraCanvasSpinner::HandleMouseDown(const UCEvent& event) {
        Part part = HitTest(event.pointer);
        if (part == Part::NonePart) return false;

        SetFocus(true);
        pressedPart = part;

        if (part == Part::IncButton) {
            if (editing) CommitEditing();
            StepBy(1.0);
        } else if (part == Part::DecButton) {
            if (editing) CommitEditing();
            StepBy(-1.0);
        } else if (part == Part::Field) {
            if (dropdownEnabled) {
                // Combobox-style: the field click toggles the value dropdown.
                if (editing) CommitEditing();
                if (dropdownOpen) CloseValueDropdown();
                else             OpenValueDropdown();
            } else if (IsEditableNumeric()) {
                BeginEditing();
            }
        }
        RequestRedraw();
        return true;
    }

    bool UltraCanvasSpinner::HandleMouseUp(const UCEvent& /*event*/) {
        if (pressedPart == Part::NonePart) return false;
        pressedPart = Part::NonePart;
        RequestRedraw();
        return true;
    }

    bool UltraCanvasSpinner::HandleMouseMove(const UCEvent& event) {
        Part part = HitTest(event.pointer);
        // Cursor: text caret over an editable field, arrow over the buttons.
        // A dropdown-enabled field opens a popup on click, so it keeps the
        // default arrow rather than an editing caret.
        mouseCursor = (part == Part::Field && IsEditableNumeric() && !dropdownEnabled)
                          ? UCMouseCursor::Text : UCMouseCursor::Default;
        if (part != hoveredPart) {
            hoveredPart = part;
            RequestRedraw();
        }
        return part != Part::NonePart;
    }

    bool UltraCanvasSpinner::HandleWheel(const UCEvent& event) {
        if (!IsHovered() && !IsFocused()) return false;
        if (event.wheelDelta == 0) return false;
        if (editing) CommitEditing();
        StepBy(event.wheelDelta > 0 ? 1.0 : -1.0);
        return true;
    }

    bool UltraCanvasSpinner::HandleKeyDown(const UCEvent& event) {
        if (!IsFocused()) return false;

        switch (event.virtualKey) {
            case UCKeys::Up:
            case UCKeys::Right:
                if (editing) CommitEditing();
                StepBy(1.0);
                return true;
            case UCKeys::Down:
            case UCKeys::Left:
                if (editing) CommitEditing();
                StepBy(-1.0);
                return true;
            case UCKeys::PageUp:
                if (editing) CommitEditing();
                ApplyValue(value + (valueType == SpinnerValueType::List ? 1.0 : pageStep), true);
                return true;
            case UCKeys::PageDown:
                if (editing) CommitEditing();
                ApplyValue(value - (valueType == SpinnerValueType::List ? 1.0 : pageStep), true);
                return true;
            case UCKeys::Home:
                if (editing) CancelEditing();
                ApplyValue(minValue, true);
                return true;
            case UCKeys::End:
                if (editing) CancelEditing();
                ApplyValue(maxValue, true);
                return true;
            case UCKeys::Return:
            case UCKeys::NumPadEnter:
                if (editing) CommitEditing();
                return true;
            case UCKeys::Escape:
                if (editing) { CancelEditing(); return true; }
                return false;
            case UCKeys::Backspace:
                if (editing) {
                    if (editBufferFresh) {
                        // Backspace over the "selected" seed clears it entirely.
                        editBuffer.clear();
                        editBufferFresh = false;
                        RequestRedraw();
                        return true;
                    }
                    if (!editBuffer.empty()) {
                        editBuffer.pop_back();
                        RequestRedraw();
                        return true;
                    }
                }
                return false;
            default:
                return HandleKeyChar(event);
        }
        return false;
    }

    bool UltraCanvasSpinner::HandleKeyChar(const UCEvent& event) {
        if (!IsFocused() || !IsEditableNumeric()) return false;

        char c = event.character;
        bool isDigit = (c >= '0' && c <= '9');
        bool isDot   = (c == '.' && valueType == SpinnerValueType::Decimal &&
                        editBuffer.find('.') == std::string::npos);
        bool isSign  = ((c == '-' || c == '+') && editBuffer.empty() && minValue < 0);

        if (!isDigit && !isDot && !isSign) return false;

        if (!editing) {
            editing = true;
            editBuffer.clear();   // typing starts a fresh value
        } else if (editBufferFresh) {
            // The seeded value was "selected"; the first keystroke replaces it.
            editBuffer.clear();
        }
        editBufferFresh = false;
        editBuffer.push_back(c);
        RequestRedraw();
        return true;
    }

    bool UltraCanvasSpinner::SetFocus(bool focus) {
        bool result = UltraCanvasUIElement::SetFocus(focus);
        if (!focus && editing) CommitEditing();
        RequestRedraw();
        return result;
    }

} // namespace UltraCanvas
