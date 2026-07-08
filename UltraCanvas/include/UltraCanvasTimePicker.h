// include/UltraCanvasTimePicker.h
// Time-of-day selection widget for UltraCanvas.
//
// UltraCanvasTimePicker is the time-of-day sibling of UltraCanvasDatePicker:
// a compact combobox-style field that shows the selected time as editable text
// and pops up a small panel of hour / minute (/ second) spinners plus an
// optional AM/PM selector. It reuses UltraCanvasSpinner for the popup fields.
//
// Supports 12- and 24-hour formats, optional seconds, a configurable minute
// step, min/max constraints, direct keyboard entry and the mouse wheel.
//
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include <string>
#include <functional>
#include <memory>

namespace UltraCanvas {

// ===== UCTime: lightweight time-of-day value =====
// hour is 0-23, minute/second are 0-59. A default-constructed UCTime
// (present == false) is treated as "empty/unset".
    struct UCTime {
        int hour = 0;
        int minute = 0;
        int second = 0;
        bool present = false;   // false = empty/unset

        UCTime() = default;
        UCTime(int h, int m, int s = 0)
            : hour(h), minute(m), second(s), present(true) {}

        bool IsEmpty() const { return !present; }
        bool IsValid() const {
            return present && hour >= 0 && hour < 24 &&
                   minute >= 0 && minute < 60 && second >= 0 && second < 60;
        }

        int  ToSecondOfDay() const { return ((hour * 60) + minute) * 60 + second; }
        static UCTime FromSecondOfDay(int s) {
            s = ((s % 86400) + 86400) % 86400;   // wrap into a single day
            return UCTime(s / 3600, (s % 3600) / 60, s % 60);
        }
        UCTime AddSeconds(int n) const { return FromSecondOfDay(ToSecondOfDay() + n); }
        UCTime AddMinutes(int n) const { return AddSeconds(n * 60); }

        // 12-hour clock helpers.
        int  Hour12() const { int h = hour % 12; return h == 0 ? 12 : h; }
        bool IsPM() const { return hour >= 12; }

        static UCTime Now();

        // Format tokens: HH, H (24h), hh, h (12h), mm, m, ss, s, tt (AM/PM),
        // t (A/P). Anything else is copied literally.
        std::string Format(const std::string& fmt) const;
        // Loose parse: pulls the numeric groups out of the text (hour, minute,
        // optional second) and honours a trailing/leading AM/PM marker.
        // Returns false when no usable time can be read (out is left untouched).
        static bool Parse(const std::string& text, bool is24h, bool hasSeconds, UCTime& out);

        bool operator==(const UCTime& o) const {
            return present == o.present && hour == o.hour &&
                   minute == o.minute && second == o.second;
        }
        bool operator!=(const UCTime& o) const { return !(*this == o); }
        bool operator<(const UCTime& o) const { return ToSecondOfDay() < o.ToSecondOfDay(); }
        bool operator>(const UCTime& o) const { return o < *this; }
        bool operator<=(const UCTime& o) const { return !(o < *this); }
        bool operator>=(const UCTime& o) const { return !(*this < o); }
    };

// ===== TIME PICKER STYLE =====
    struct TimePickerStyle {
        // Field
        Color backgroundColor   = Colors::White;
        Color hoverColor        = Color(247, 250, 255, 255);
        Color disabledColor     = Color(245, 245, 245, 255);
        Color borderColor       = Color(180, 180, 180, 255);
        Color focusBorderColor  = Colors::Selection;
        Color textColor         = Colors::Black;
        Color placeholderColor  = Color(160, 160, 160, 255);
        Color disabledTextColor = Color(150, 150, 150, 255);
        Color iconColor         = Color(90, 90, 90, 255);
        Color caretColor        = Colors::Black;

        float borderWidth  = 1.0f;
        float cornerRadius  = 3.0f;
        float paddingLeft   = 8.0f;
        float buttonWidth   = 28.0f;

        // Popup panel
        Color popupBackgroundColor = Colors::White;
        Color popupBorderColor     = Color(180, 180, 180, 255);
        Color separatorTextColor   = Color(60, 60, 60, 255);
        Color footerTextColor      = Colors::Selection;

        std::string fontFamily;
        float fontSize      = 12.0f;
    };

// ===== TIME PICKER (dropdown spinner popup) =====
    class UltraCanvasTimePicker : public UltraCanvasUIElement {
    public:
        // ===== CALLBACKS =====
        std::function<void(const UCTime&)> onTimeChanged;
        std::function<void()> onPopupOpened;
        std::function<void()> onPopupClosed;

        UltraCanvasTimePicker(const std::string& identifier, float x, float y, float w, float h);
        UltraCanvasTimePicker(const std::string& identifier, float w, float h)
            : UltraCanvasTimePicker(identifier, -1, -1, w, h) {}
        explicit UltraCanvasTimePicker(const std::string& identifier)
            : UltraCanvasTimePicker(identifier, -1, -1, -1, -1) {}
        ~UltraCanvasTimePicker() override = default;

        // ===== VALUE =====
        void   SetTime(const UCTime& t, bool runCallbacks = false);
        UCTime GetTime() const { return value; }
        void   Clear(bool runCallbacks = false);
        void   SetNow(bool runCallbacks = true) { SetTime(UCTime::Now(), runCallbacks); }

        // ===== CONFIG =====
        void SetUse24HourFormat(bool use24);
        bool GetUse24HourFormat() const { return use24h; }

        void SetShowSeconds(bool show);
        bool GetShowSeconds() const { return showSeconds; }

        void SetMinuteStep(int step) { minuteStep = (step > 0 ? step : 1); }
        int  GetMinuteStep() const { return minuteStep; }
        void SetSecondStep(int step) { secondStep = (step > 0 ? step : 1); }
        int  GetSecondStep() const { return secondStep; }

        // Explicit format string overrides the derived one (see UCTime::Format).
        void SetTimeFormat(const std::string& fmt) { explicitFormat = fmt; RequestRedraw(); }
        std::string GetTimeFormat() const { return EffectiveFormat(); }

        void SetPlaceholder(const std::string& text) { placeholder = text; RequestRedraw(); }
        const std::string& GetPlaceholder() const { return placeholder; }

        void SetAllowTextInput(bool allow);
        bool GetAllowTextInput() const { return allowTextInput; }

        // Optional constraints; an empty UCTime means unbounded on that side.
        void SetMinTime(const UCTime& t) { minTime = t; }
        void SetMaxTime(const UCTime& t) { maxTime = t; }

        // ===== POPUP STATE =====
        void OpenPopup();
        void ClosePopup();
        bool IsPopupOpen() const { return popupOpen; }

        // ===== STYLE =====
        void SetStyle(const TimePickerStyle& s) { style = s; RequestRedraw(); }
        const TimePickerStyle& GetStyle() const { return style; }

        // ===== OVERRIDES =====
        void SetWindow(UltraCanvasWindowBase* win) override;
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;
        bool AcceptsFocus() const override { return true; }
        bool SetFocus(bool focus = true) override;

    private:
        // ----- rendering / geometry -----
        void RenderField(IRenderContext* ctx);
        void RenderClockIcon(IRenderContext* ctx, const Rect2Df& rect, const Color& color);
        Rect2Df ButtonRect() const;
        Rect2Df TextRect() const;
        Point2Df CalculatePopupPosition() const;

        // ----- value / text -----
        std::string EffectiveFormat() const;
        std::string BuildDisplayText() const;   // formatted value (or empty)
        void ApplyConstraints(UCTime& t) const; // clamp into [minTime, maxTime]
        void CommitTextInput();                  // parse editBuffer -> value

        // ----- popup -----
        void BuildPopup();               // (re)create the popup container + spinners
        void SyncSpinnersFromValue();    // push value into the spinners
        void RecomputeFromSpinners();    // pull value back out of the spinners

        // ----- events -----
        bool HandleMouseDown(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);
        bool HandleKeyChar(const UCEvent& event);
        bool HandleWheel(const UCEvent& event);

        // ----- state -----
        UCTime value;                    // may be empty (present == false)
        bool use24h = true;
        bool showSeconds = false;
        int  minuteStep = 1;
        int  secondStep = 1;
        std::string explicitFormat;      // empty => derive from flags
        std::string placeholder = "Select time";
        bool allowTextInput = true;
        UCTime minTime, maxTime;         // empty => unbounded

        // popup
        std::shared_ptr<UltraCanvasContainer> popup;
        std::shared_ptr<UltraCanvasSpinner> hourSpin, minuteSpin, secondSpin, ampmSpin;
        bool popupOpen = false;
        bool updatingSpinners = false;   // guard: suppress spinner feedback

        // text editing (active only when allowTextInput && focused)
        std::string editBuffer;
        bool editing = false;

        TimePickerStyle style;
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasTimePicker> CreateTimePicker(
            const std::string& identifier, float x, float y, float w, float h = 26) {
        return std::make_shared<UltraCanvasTimePicker>(identifier, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasTimePicker> CreateTimePicker24h(
            const std::string& identifier, float x, float y, float w, float h = 26,
            bool showSeconds = false) {
        auto p = std::make_shared<UltraCanvasTimePicker>(identifier, x, y, w, h);
        p->SetUse24HourFormat(true);
        p->SetShowSeconds(showSeconds);
        return p;
    }

    inline std::shared_ptr<UltraCanvasTimePicker> CreateTimePicker12h(
            const std::string& identifier, float x, float y, float w, float h = 26,
            bool showSeconds = false) {
        auto p = std::make_shared<UltraCanvasTimePicker>(identifier, x, y, w, h);
        p->SetUse24HourFormat(false);
        p->SetShowSeconds(showSeconds);
        return p;
    }

} // namespace UltraCanvas
