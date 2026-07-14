// include/UltraCanvasSpinner.h
// Spinner / spin-button control: select a value with up/down (or left/right)
// arrow buttons, the keyboard arrow keys, the mouse wheel, or by typing.
// Also known as SpinBox, NumericUpDown (WinForms), NSStepper (Cocoa),
// GtkSpinButton (GTK) or an ARIA "spinbutton".
//
// Supports three value modes:
//   * Integer  - whole-number stepping
//   * Decimal  - fixed-precision floating stepping
//   * List     - cycle through an ordered list of arbitrary string values
// and two button layouts:
//   * UpDownRight    - editable field with stacked up/down buttons on the right
//   * SidesHorizontal - [dec] field [inc] with buttons flanking the field
//
// Version: 1.1.0
// Last Modified: 2026-07-13
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    class UltraCanvasMenu;   // popup used by the optional value dropdown

// ===== SPINNER MODE / LAYOUT DEFINITIONS =====
    enum class SpinnerValueType {
        Integer,   // Whole numbers, formatted without decimals
        Decimal,   // Fixed-precision floating point value
        List       // Cycle through an ordered list of string values
    };

    enum class SpinnerLayout {
        UpDownRight,      // Editable field on the left, stacked ▲/▼ buttons on the right
        SidesHorizontal   // Buttons flanking the field:  ◀ / −   value   ▶ / +
    };

    // Glyph drawn inside the step buttons.
    enum class SpinnerButtonGlyph {
        Triangle,   // Filled triangle pointing up/down or left/right
        Chevron,    // Two-stroke chevron (>, <, ^, v)
        PlusMinus   // '+' on the increment button, '−' on the decrement button
    };

// ===== SPINNER VISUAL STYLE =====
    struct SpinnerVisualStyle {
        // ----- value field -----
        Color backgroundColor         = Colors::White;
        Color disabledBackgroundColor = Color(240, 240, 242);
        Color borderColor             = Color(160, 160, 165);
        Color focusBorderColor        = Color(0, 120, 215);
        Color textColor               = Colors::Black;
        Color disabledTextColor       = Color(150, 150, 150);
        Color caretColor              = Color(0, 120, 215);

        // ----- step buttons -----
        Color buttonColor             = Color(236, 236, 238);
        Color buttonHoverColor        = Color(222, 222, 226);
        Color buttonPressedColor      = Color(200, 200, 205);
        Color buttonDisabledColor     = Color(242, 242, 244);
        Color buttonBorderColor       = Color(160, 160, 165);
        Color glyphColor              = Color(70, 70, 75);
        Color glyphDisabledColor      = Color(185, 185, 190);

        // ----- metrics -----
        float buttonThickness = 18.0f;   // width of the button column / side buttons
        float borderWidth     = 1.0f;
        float cornerRadius     = 3.0f;
        float glyphSize        = 8.0f;   // nominal glyph extent in px
        float textPaddingH     = 6.0f;   // horizontal padding inside the value field

        SpinnerButtonGlyph glyph = SpinnerButtonGlyph::Triangle;

        FontStyle fontStyle;
    };

// ===== MAIN SPINNER COMPONENT =====
    class UltraCanvasSpinner : public UltraCanvasUIElement {
    public:
        // Sub-region of the control, used for hit-testing and hover/press state.
        // (NonePart avoids colliding with X11's `#define None 0L`.)
        enum class Part { NonePart, Field, IncButton, DecButton };

    private:
        // ===== VALUE MODEL =====
        SpinnerValueType valueType = SpinnerValueType::Integer;
        double minValue = 0.0;
        double maxValue = 100.0;
        double value    = 0.0;
        double step     = 1.0;
        double pageStep = 10.0;   // step for PageUp / PageDown
        int    decimals = 0;      // used when valueType == Decimal
        bool   wrap     = false;  // wrap around at the ends instead of clamping

        // ===== LIST MODE =====
        // In List mode `value` holds the (integer) index into listItems and the
        // min/max are kept in sync with [0, size-1].
        std::vector<std::string> listItems;

        // ===== DISPLAY =====
        std::string prefix;
        std::string suffix;
        std::string placeholder;
        TextAlignment textAlignment = TextAlignment::Left;
        bool allowTextEditing = true;   // let the user type a value into the field
        std::function<std::string(double)> formatter;   // optional custom formatting

        // ===== VALUE DROPDOWN (optional combobox-style picker) =====
        // When enabled a single click on the field opens a popup listing the
        // available values (the list items in List mode, or the discrete
        // min..max/step grid in numeric modes). Editable numeric spinners can
        // still be typed into via a double-click.
        bool dropdownEnabled = false;
        bool dropdownOpen    = false;
        std::shared_ptr<UltraCanvasMenu> valueMenu;   // lazily created popup
        // Upper bound on how many grid values a numeric dropdown will list, so
        // a wide range with a tiny step cannot spawn a runaway popup.
        static constexpr int kMaxDropdownItems = 512;

        // ===== LAYOUT / STYLE =====
        SpinnerLayout layout = SpinnerLayout::UpDownRight;
        SpinnerVisualStyle style;

        // ===== INTERACTION STATE =====
        Part hoveredPart = Part::NonePart;
        Part pressedPart = Part::NonePart;

        // Inline text editing (numeric modes only).
        bool editing = false;
        std::string editBuffer;
        // True right after the buffer is seeded from the current value (e.g. by
        // clicking the field): the content is treated as "selected", so the
        // next typed character or Backspace replaces it wholesale instead of
        // appending. Mirrors the select-all-on-focus behaviour of native
        // spin boxes.
        bool editBufferFresh = false;

    public:
        // ===== CONSTRUCTORS (REQUIRED PATTERN) =====
        UltraCanvasSpinner(const std::string& identifier, float x, float y, float w, float h);

        UltraCanvasSpinner(const std::string& identifier, float w, float h)
            : UltraCanvasSpinner(identifier, -1, -1, w, h) {}

        explicit UltraCanvasSpinner(const std::string& identifier)
            : UltraCanvasSpinner(identifier, -1, -1, -1, -1) {}

        // ===== VALUE MANAGEMENT =====
        void   SetRange(double minVal, double maxVal);
        void   SetValue(double newValue);        // clamps/wraps, snaps to step grid
        double GetValue() const { return value; }
        int    GetIntValue() const { return static_cast<int>(std::llround(value)); }
        double GetMinValue() const { return minValue; }
        double GetMaxValue() const { return maxValue; }

        void   SetStep(double s)      { step = (s > 0.0) ? s : step; }
        double GetStep() const        { return step; }
        void   SetPageStep(double s)  { pageStep = (s > 0.0) ? s : pageStep; }
        double GetPageStep() const    { return pageStep; }

        // Number of decimal places to display (implies SpinnerValueType::Decimal
        // when > 0). 0 keeps the control in Integer mode.
        void SetDecimals(int d);
        int  GetDecimals() const { return decimals; }

        void SetValueType(SpinnerValueType type);
        SpinnerValueType GetValueType() const { return valueType; }

        void SetWrap(bool enabled) { wrap = enabled; }
        bool IsWrap() const { return wrap; }

        // Step the value up/down by one step (or a caller-supplied multiple).
        void StepUp(double steps = 1.0)   { StepBy(steps); }
        void StepDown(double steps = 1.0) { StepBy(-steps); }
        void StepBy(double steps);

        // ===== LIST MODE MANAGEMENT =====
        void SetListItems(const std::vector<std::string>& items);
        const std::vector<std::string>& GetListItems() const { return listItems; }
        void AddListItem(const std::string& item);
        void ClearListItems();

        void SetSelectedIndex(int index);
        int  GetSelectedIndex() const;
        std::string GetSelectedText() const;

        // ===== DISPLAY CUSTOMISATION =====
        void SetPrefix(const std::string& p) { prefix = p; RequestRedraw(); }
        void SetSuffix(const std::string& s) { suffix = s; RequestRedraw(); }
        const std::string& GetPrefix() const { return prefix; }
        const std::string& GetSuffix() const { return suffix; }

        void SetPlaceholder(const std::string& p) { placeholder = p; }
        void SetTextAlignment(TextAlignment a) { textAlignment = a; RequestRedraw(); }
        TextAlignment GetTextAlignment() const { return textAlignment; }

        // A custom formatter takes precedence over the built-in numeric/list
        // formatting. It receives the raw value (or list index in List mode).
        void SetFormatter(std::function<std::string(double)> fn) { formatter = std::move(fn); RequestRedraw(); }

        void SetEditable(bool enabled);
        bool IsEditable() const { return allowTextEditing; }

        // ===== VALUE DROPDOWN =====
        // Enable a combobox-style popup that lists the available values and
        // opens on a single click of the field. Off by default; a plain
        // spinner keeps its click-to-edit behaviour.
        void SetDropdownEnabled(bool enabled);
        bool IsDropdownEnabled() const { return dropdownEnabled; }
        bool IsDropdownOpen() const { return dropdownOpen; }
        void OpenValueDropdown();
        void CloseValueDropdown();

        // Formatted text currently shown in the field (excludes any live edit buffer).
        std::string GetDisplayText() const;

        // ===== LAYOUT / STYLE =====
        void SetLayout(SpinnerLayout l) { layout = l; RequestRedraw(); }
        SpinnerLayout GetLayout() const { return layout; }

        void SetButtonGlyph(SpinnerButtonGlyph g) { style.glyph = g; RequestRedraw(); }
        void SetButtonThickness(float t) { style.buttonThickness = std::max(8.0f, t); RequestRedraw(); }

        SpinnerVisualStyle& GetStyle() { return style; }
        const SpinnerVisualStyle& GetStyle() const { return style; }
        void SetStyle(const SpinnerVisualStyle& s) { style = s; RequestRedraw(); }

        // ===== BASE OVERRIDES =====
        bool AcceptsFocus() const override { return true; }
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;
        bool SetFocus(bool focus = true) override;

        // ===== CALLBACKS =====
        std::function<void(double)> onValueChanged;   // fired on committed change
        std::function<void(double)> onValueChanging;  // fired on each interactive step
        std::function<void(int, const std::string&)> onSelectionChanged; // List mode
        std::function<void()> onEditingFinished;      // text edit committed / focus lost

    private:
        // ===== VALUE HELPERS =====
        double ClampWrap(double v) const;
        double SnapToStep(double v) const;
        void   ApplyValue(double newValue, bool interactive);

        // ===== GEOMETRY (element-local coordinates) =====
        Rect2Di GetFieldRect(const Rect2Di& bounds) const;
        Rect2Di GetIncButtonRect(const Rect2Di& bounds) const;
        Rect2Di GetDecButtonRect(const Rect2Di& bounds) const;
        Part    HitTest(const Point2Di& localPos) const;

        // ===== TEXT EDITING =====
        void BeginEditing();
        void CommitEditing();
        void CancelEditing();
        bool IsEditableNumeric() const {
            return allowTextEditing && valueType != SpinnerValueType::List;
        }

        // ===== FORMATTING =====
        std::string FormatNumber(double v) const;
        // Text shown for an arbitrary value (index in List mode) in the
        // dropdown, including prefix / suffix.
        std::string FormatValueForDisplay(double v) const;

        // ===== VALUE DROPDOWN HELPERS =====
        // Number of discrete values the dropdown would list (list size, or the
        // min..max/step count in numeric modes), or -1 when it exceeds
        // kMaxDropdownItems and should be suppressed.
        int DropdownItemCount() const;

        // ===== RENDER HELPERS =====
        void RenderField(const Rect2Di& fieldRect, IRenderContext* ctx);
        void RenderButton(const Rect2Di& rect, Part part, bool isIncrement, IRenderContext* ctx);
        void RenderGlyph(const Rect2Di& rect, bool isIncrement, const Color& color, IRenderContext* ctx);

        Color CurrentButtonColor(Part part) const;

        // ===== EVENT HANDLERS =====
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleWheel(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);
        bool HandleKeyChar(const UCEvent& event);
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasSpinner> CreateSpinner(
            const std::string& identifier, float x, float y, float width, float height) {
        return std::make_shared<UltraCanvasSpinner>(identifier, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasSpinner> CreateIntSpinner(
            const std::string& identifier, float x, float y, float width, float height,
            int minVal = 0, int maxVal = 100, int initial = 0, int stepVal = 1) {
        auto s = std::make_shared<UltraCanvasSpinner>(identifier, x, y, width, height);
        s->SetValueType(SpinnerValueType::Integer);
        s->SetRange(minVal, maxVal);
        s->SetStep(stepVal);
        s->SetValue(initial);
        return s;
    }

    inline std::shared_ptr<UltraCanvasSpinner> CreateDecimalSpinner(
            const std::string& identifier, float x, float y, float width, float height,
            double minVal = 0.0, double maxVal = 1.0, double initial = 0.0,
            double stepVal = 0.1, int decimals = 2) {
        auto s = std::make_shared<UltraCanvasSpinner>(identifier, x, y, width, height);
        s->SetDecimals(decimals);
        s->SetRange(minVal, maxVal);
        s->SetStep(stepVal);
        s->SetValue(initial);
        return s;
    }

    inline std::shared_ptr<UltraCanvasSpinner> CreateListSpinner(
            const std::string& identifier, float x, float y, float width, float height,
            const std::vector<std::string>& items = {}) {
        auto s = std::make_shared<UltraCanvasSpinner>(identifier, x, y, width, height);
        s->SetValueType(SpinnerValueType::List);
        s->SetListItems(items);
        return s;
    }

    // Horizontal stepper:  [−] value [+]  with buttons flanking the field.
    inline std::shared_ptr<UltraCanvasSpinner> CreateStepper(
            const std::string& identifier, float x, float y, float width, float height,
            double minVal = 0, double maxVal = 100, double initial = 0, double stepVal = 1) {
        auto s = std::make_shared<UltraCanvasSpinner>(identifier, x, y, width, height);
        s->SetLayout(SpinnerLayout::SidesHorizontal);
        s->SetButtonGlyph(SpinnerButtonGlyph::PlusMinus);
        s->SetTextAlignment(TextAlignment::Center);
        s->SetRange(minVal, maxVal);
        s->SetStep(stepVal);
        s->SetValue(initial);
        return s;
    }

} // namespace UltraCanvas
