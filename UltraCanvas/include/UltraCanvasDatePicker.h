// include/UltraCanvasDatePicker.h
// Calendar / date-selection widgets for UltraCanvas.
//
// This header provides two complementary widgets that cover the common
// date-selection philosophies (see Docs/UltraCanvas/UltraCanvasDatePicker.md
// for the full design rationale):
//
//   * UltraCanvasCalendarView  - a reusable, always-visible month calendar
//                                grid. Supports single-date, date-range,
//                                multiple-date and whole-week selection, plus
//                                drill-up navigation (days -> months -> years)
//                                and full keyboard control. Use it directly
//                                when the calendar should be permanently
//                                on-screen (the "inline" philosophy).
//
//   * UltraCanvasDatePicker    - a compact combobox-style field that shows the
//                                selected date as editable text and pops up a
//                                UltraCanvasCalendarView on demand (the
//                                "dropdown" philosophy). Combines fast keyboard
//                                entry with point-and-click browsing.
//
// Last Modified: 2026-06-13
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include <string>
#include <vector>
#include <set>
#include <functional>
#include <memory>

namespace UltraCanvas {

// ===== UCDate: lightweight calendar date value =====
// A pure value type (no time-of-day). month is 1-12, day is 1-31.
// A default-constructed UCDate (year == 0) is treated as "empty/invalid".
    struct UCDate {
        int year = 0;
        int month = 0;
        int day = 0;

        UCDate() = default;
        UCDate(int y, int m, int d) : year(y), month(m), day(d) {}

        bool IsValid() const;
        bool IsEmpty() const { return year == 0 && month == 0 && day == 0; }

        // 0 = Sunday .. 6 = Saturday
        int DayOfWeek() const;
        // ISO-8601 week number (1-53)
        int ISOWeek() const;

        // Proleptic Gregorian day count relative to 1970-01-01 (can be negative).
        long ToOrdinal() const;
        static UCDate FromOrdinal(long ordinal);

        UCDate AddDays(int n) const { return FromOrdinal(ToOrdinal() + n); }
        UCDate AddMonths(int n) const;
        UCDate AddYears(int n) const { return AddMonths(n * 12); }

        static bool IsLeapYear(int y);
        static int DaysInMonth(int y, int m);
        static UCDate Today();

        // Format using tokens: yyyy, yy, MM, M, dd, d. Anything else is literal.
        std::string Format(const std::string& fmt) const;
        // Parse "loosely": split on any non-digit run and map the numeric groups
        // according to the order of y/M/d tokens found in fmt. Returns false on
        // failure (out is left untouched).
        static bool Parse(const std::string& text, const std::string& fmt, UCDate& out);

        bool operator==(const UCDate& o) const { return year == o.year && month == o.month && day == o.day; }
        bool operator!=(const UCDate& o) const { return !(*this == o); }
        bool operator<(const UCDate& o) const {
            if (year != o.year) return year < o.year;
            if (month != o.month) return month < o.month;
            return day < o.day;
        }
        bool operator>(const UCDate& o) const { return o < *this; }
        bool operator<=(const UCDate& o) const { return !(o < *this); }
        bool operator>=(const UCDate& o) const { return !(*this < o); }
    };

// ===== SELECTION PHILOSOPHY =====
    enum class DateSelectionMode {
        Single,     // one date
        Range,      // a contiguous start..end span (two clicks)
        Multiple,   // an arbitrary set of individual dates (toggle)
        Week        // clicking any day selects its entire week (a 7-day span)
    };

// ===== INTERNAL DRILL-NAVIGATION LEVEL =====
    enum class CalendarViewLevel {
        Days,       // day grid for one month
        Months,     // month grid for one year
        Years       // year grid for one decade
    };

// ===== TRIGGER PHILOSOPHY (UltraCanvasDatePicker) =====
    enum class DatePickerVariant {
        DropdownCalendar    // editable text field + button -> popup calendar
    };

// ===== CALENDAR STYLE =====
    struct CalendarStyle {
        // Panel
        Color backgroundColor       = Colors::White;
        Color borderColor           = Color(180, 180, 180, 255);
        float borderWidth           = 1.0f;
        float cornerRadius          = 4.0f;

        // Header / navigation bar
        Color headerBackgroundColor = Colors::White;
        Color headerTextColor       = Color(30, 30, 30, 255);
        Color headerHoverColor      = Color(235, 242, 255, 255);
        Color navArrowColor         = Color(90, 90, 90, 255);
        Color navArrowHoverColor    = Colors::Selection;

        // Weekday header row
        Color weekdayTextColor      = Color(130, 130, 130, 255);
        Color weekendWeekdayColor   = Color(196, 90, 90, 255);

        // Day / month / year cells
        Color cellTextColor         = Color(40, 40, 40, 255);
        Color adjacentMonthColor    = Color(185, 185, 185, 255);
        Color weekendTextColor      = Color(196, 90, 90, 255);
        Color disabledTextColor     = Color(210, 210, 210, 255);
        Color hoverColor            = Color(230, 240, 255, 255);
        Color selectedColor         = Colors::Selection;
        Color selectedTextColor     = Colors::White;
        Color todayBorderColor      = Colors::Selection;
        Color focusBorderColor      = Color(120, 170, 255, 255);
        Color rangeFillColor        = Color(224, 238, 255, 255);
        Color weekNumberColor       = Color(170, 170, 170, 255);
        Color blockedStrikeColor    = Color(200, 110, 110, 200);  // strike over blocked days

        // Footer (Today / Clear shortcuts)
        Color footerTextColor       = Colors::Selection;
        Color footerHoverColor      = Color(235, 242, 255, 255);
        Color footerSeparatorColor  = Color(225, 225, 225, 255);

        // Metrics (logical pixels)
        float cellWidth             = 36.0f;
        float cellHeight            = 32.0f;
        float headerHeight          = 40.0f;
        float weekdayRowHeight      = 22.0f;
        float footerHeight          = 32.0f;
        float panelPadding          = 8.0f;
        float weekNumberColumnWidth = 26.0f;
        float cellCornerRadius      = 4.0f;

        // Fonts
        std::string fontFamily;
        float fontSize              = 12.0f;
        float headerFontSize        = 13.0f;
    };

// ===== CALENDAR VIEW =====
// A standalone, always-visible calendar grid. Can be embedded in any container
// (inline philosophy) or hosted inside an UltraCanvasDatePicker popup.
    class UltraCanvasCalendarView : public UltraCanvasUIElement {
    public:
        // ===== CALLBACKS =====
        std::function<void(const UCDate&)> onDateSelected;                 // Single/Multiple: a date was (de)selected
        std::function<void(const UCDate&, const UCDate&)> onRangeSelected; // Range/Week: a span was completed
        std::function<void(const std::vector<UCDate>&)> onMultipleChanged; // Multiple: full set after a toggle
        std::function<void(const UCDate&)> onDateActivated;                // commit gesture (host should close popup)
        std::function<void(int, int)> onVisibleMonthChanged;               // (year, month) when the visible month changes

        UltraCanvasCalendarView(const std::string& identifier, float x, float y, float w, float h);
        UltraCanvasCalendarView(const std::string& identifier, float w, float h)
            : UltraCanvasCalendarView(identifier, -1, -1, w, h) {}
        explicit UltraCanvasCalendarView(const std::string& identifier)
            : UltraCanvasCalendarView(identifier, -1, -1, -1, -1) {}
        ~UltraCanvasCalendarView() override = default;

        // ===== SELECTION MODE =====
        void SetSelectionMode(DateSelectionMode mode);
        DateSelectionMode GetSelectionMode() const { return selectionMode; }

        // ===== SINGLE / MULTIPLE =====
        void SetSelectedDate(const UCDate& date, bool runCallbacks = false);
        UCDate GetSelectedDate() const { return selectedDate; }
        void ClearSelection(bool runCallbacks = false);

        void SetSelectedDates(const std::vector<UCDate>& dates);
        std::vector<UCDate> GetSelectedDates() const;

        // ===== RANGE / WEEK =====
        void SetRange(const UCDate& start, const UCDate& end, bool runCallbacks = false);
        UCDate GetRangeStart() const { return rangeStart; }
        UCDate GetRangeEnd() const { return rangeEnd; }

        // ===== VISIBLE MONTH =====
        void SetVisibleMonth(int year, int month);
        int GetVisibleYear() const { return visibleYear; }
        int GetVisibleMonth() const { return visibleMonth; }
        void GoToToday();

        // ===== CONSTRAINTS =====
        void SetMinDate(const UCDate& d) { minDate = d; RequestRedraw(); }
        void SetMaxDate(const UCDate& d) { maxDate = d; RequestRedraw(); }
        void SetDateEnabledPredicate(std::function<bool(const UCDate&)> pred) { dateEnabledPredicate = std::move(pred); RequestRedraw(); }
        bool IsDateSelectable(const UCDate& d) const;

        // ===== BLOCKED DATES =====
        // An explicit set of unavailable dates (e.g. already-booked nights).
        // Blocked dates cannot be selected, and — crucially for range/week
        // selection — a span may not stretch across a blocked date.
        void SetBlockedDates(const std::vector<UCDate>& dates);
        void AddBlockedDate(const UCDate& d);
        void BlockDateRange(const UCDate& start, const UCDate& end);
        void ClearBlockedDates();
        bool IsDateBlocked(const UCDate& d) const;
        // True if any date in the closed interval [a, b] is not selectable.
        bool RangeHasBlocked(const UCDate& a, const UCDate& b) const;
        // Furthest date reachable from 'start' toward 'target' without crossing
        // an unselectable/blocked day (used to cap a range's live preview).
        UCDate ClampRangeEnd(const UCDate& start, const UCDate& target) const;

        // ===== LOCALISATION / APPEARANCE =====
        // firstDayOfWeek: 0 = Sunday (default), 1 = Monday, ...
        void SetFirstDayOfWeek(int dow) { firstDayOfWeek = ((dow % 7) + 7) % 7; RequestRedraw(); }
        int GetFirstDayOfWeek() const { return firstDayOfWeek; }
        void SetWeekdayNames(const std::vector<std::string>& shortNames); // size 7, Sunday-first
        void SetMonthNames(const std::vector<std::string>& longNames,
                           const std::vector<std::string>& shortNames);   // size 12
        void SetShowWeekNumbers(bool show);
        void SetShowAdjacentMonthDays(bool show) { showAdjacentDays = show; RequestRedraw(); }
        void SetShowFooter(bool show) { showFooter = show; RequestRedraw(); }
        void SetWeekendDays(int dow1, int dow2) { weekendDay1 = dow1; weekendDay2 = dow2; RequestRedraw(); }

        // ===== STYLE =====
        void SetStyle(const CalendarStyle& s) { style = s; RequestRedraw(); }
        const CalendarStyle& GetStyle() const { return style; }
        CalendarStyle& GetStyle() { return style; }

        // Preferred size for the current style/configuration (used when hosted
        // in a popup or to size an inline calendar).
        Size2Df GetPreferredSize() const;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;
        bool AcceptsFocus() const override { return true; }

    private:
        // ----- internal layout description (recomputed from bounds) -----
        struct Layout {
            Rect2Df header;
            Rect2Df prevButton;
            Rect2Df titleButton;
            Rect2Df nextButton;
            Rect2Df weekdayRow;
            Rect2Df body;       // grid area (days/months/years)
            Rect2Df footer;     // valid only when showFooter
            Rect2Df todayButton;
            Rect2Df clearButton;
            float gridOriginX = 0;
            float gridOriginY = 0;
            float cellW = 0;
            float cellH = 0;
            float weekColW = 0; // 0 when week numbers hidden
            int cols = 7;
            int rows = 6;
        };
        Layout ComputeLayout() const;

        UCDate FirstVisibleCell() const;           // top-left day cell of the day grid
        UCDate DateForDayCell(int index) const;    // index 0..41
        int DayCellAtPoint(const Layout& l, const Point2Df& p) const; // -1 if none
        int GridCellAtPoint(const Layout& l, const Point2Df& p, int cols, int rows) const;

        void DrawDaysLevel(IRenderContext* ctx, const Layout& l);
        void DrawMonthsLevel(IRenderContext* ctx, const Layout& l);
        void DrawYearsLevel(IRenderContext* ctx, const Layout& l);
        void DrawHeader(IRenderContext* ctx, const Layout& l);
        void DrawFooter(IRenderContext* ctx, const Layout& l);
        void DrawCenteredText(IRenderContext* ctx, const std::string& text,
                              const Rect2Df& rect, const Color& color, float fontSize, FontWeight weight);

        bool InRangeHighlight(const UCDate& d) const;   // for fill between endpoints
        bool IsRangeEndpoint(const UCDate& d) const;
        bool IsSelected(const UCDate& d) const;

        void HandleDayClick(const UCDate& d);
        void NavigatePrev();
        void NavigateNext();
        void CycleTitle();        // drill up: Days -> Months -> Years
        void MoveFocus(int days); // keyboard focus movement within day grid
        void EnsureFocusVisible();
        bool HandleKeyDown(const UCEvent& event);

        std::string HeaderTitle() const;
        int DecadeStartYear() const { return (visibleYear / 10) * 10 - 1; }

        // ----- state -----
        DateSelectionMode selectionMode = DateSelectionMode::Single;
        CalendarViewLevel level = CalendarViewLevel::Days;

        UCDate selectedDate;                 // Single
        std::set<long> selectedOrdinals;     // Multiple (stored as ordinals for ordering)
        UCDate rangeStart, rangeEnd;         // Range / Week (completed span)
        bool selectingRange = false;         // Range: first endpoint chosen, awaiting second
        UCDate rangeHoverEnd;                // Range: live preview endpoint under cursor

        UCDate focusedDate;                  // keyboard focus cell (day grid)
        bool hasFocusCell = false;

        int visibleYear = 2026;
        int visibleMonth = 6;

        UCDate minDate, maxDate;             // empty = unbounded
        std::function<bool(const UCDate&)> dateEnabledPredicate;
        std::set<long> blockedOrdinals;      // explicitly unavailable dates

        int firstDayOfWeek = 0;              // 0 = Sunday
        int weekendDay1 = 0;                 // Sunday
        int weekendDay2 = 6;                 // Saturday
        bool showWeekNumbers = false;
        bool showAdjacentDays = true;
        bool showFooter = true;

        std::vector<std::string> weekdayNames;   // Sunday-first, short
        std::vector<std::string> monthNamesLong;
        std::vector<std::string> monthNamesShort;

        // hover tracking (for hover highlight + range preview)
        UCDate hoverDate;
        int hoverNavRegion = 0;  // 0 none, 1 prev, 2 next, 3 title, 4 today, 5 clear
        int hoverCellIndex = -1; // hovered cell in Months/Years grids (-1 none)

        CalendarStyle style;

        bool IsWeekend(int dow) const { return dow == weekendDay1 || dow == weekendDay2; }
    };

// ===== DATE PICKER STYLE =====
    struct DatePickerStyle {
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

        float borderWidth   = 1.0f;
        float cornerRadius   = 3.0f;
        float paddingLeft    = 8.0f;
        float buttonWidth    = 28.0f;

        std::string fontFamily;
        float fontSize       = 12.0f;

        CalendarStyle calendarStyle;
    };

// ===== DATE PICKER (dropdown calendar) =====
    class UltraCanvasDatePicker : public UltraCanvasUIElement {
    public:
        // ===== CALLBACKS =====
        std::function<void(const UCDate&)> onDateChanged;
        std::function<void(const UCDate&, const UCDate&)> onRangeChanged;
        std::function<void()> onCalendarOpened;
        std::function<void()> onCalendarClosed;

        UltraCanvasDatePicker(const std::string& identifier, float x, float y, float w, float h);
        UltraCanvasDatePicker(const std::string& identifier, float w, float h)
            : UltraCanvasDatePicker(identifier, -1, -1, w, h) {}
        explicit UltraCanvasDatePicker(const std::string& identifier)
            : UltraCanvasDatePicker(identifier, -1, -1, -1, -1) {}
        ~UltraCanvasDatePicker() override = default;

        // ===== CONFIG =====
        void SetSelectionMode(DateSelectionMode mode);
        DateSelectionMode GetSelectionMode() const;

        void SetSelectedDate(const UCDate& date, bool runCallbacks = false);
        UCDate GetSelectedDate() const;
        void SetRange(const UCDate& start, const UCDate& end, bool runCallbacks = false);
        UCDate GetRangeStart() const;
        UCDate GetRangeEnd() const;
        void Clear(bool runCallbacks = false);

        void SetDateFormat(const std::string& fmt) { dateFormat = fmt; SyncTextFromValue(); RequestRedraw(); }
        const std::string& GetDateFormat() const { return dateFormat; }
        void SetPlaceholder(const std::string& text) { placeholder = text; RequestRedraw(); }

        // Whether the user may type the date directly into the field.
        void SetAllowTextInput(bool allow);
        bool GetAllowTextInput() const { return allowTextInput; }

        // Constraints / localisation forwarded to the calendar.
        void SetMinDate(const UCDate& d) { calendar->SetMinDate(d); }
        void SetMaxDate(const UCDate& d) { calendar->SetMaxDate(d); }
        void SetDateEnabledPredicate(std::function<bool(const UCDate&)> pred) { calendar->SetDateEnabledPredicate(std::move(pred)); }
        void SetFirstDayOfWeek(int dow) { calendar->SetFirstDayOfWeek(dow); }

        // Blocked / unavailable dates (forwarded to the calendar). For a Range
        // picker these also prevent a span from crossing a blocked date.
        void SetBlockedDates(const std::vector<UCDate>& v) { calendar->SetBlockedDates(v); }
        void AddBlockedDate(const UCDate& d) { calendar->AddBlockedDate(d); }
        void BlockDateRange(const UCDate& s, const UCDate& e) { calendar->BlockDateRange(s, e); }
        void ClearBlockedDates() { calendar->ClearBlockedDates(); }

        UltraCanvasCalendarView* GetCalendar() const { return calendar.get(); }

        // ===== POPUP STATE =====
        void OpenCalendar();
        void CloseCalendar();
        bool IsCalendarOpen() const { return calendarOpen; }

        // ===== STYLE =====
        void SetStyle(const DatePickerStyle& s);
        const DatePickerStyle& GetStyle() const { return style; }

        // ===== OVERRIDES =====
        void SetWindow(UltraCanvasWindowBase* win) override;
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;
        bool AcceptsFocus() const override { return true; }
        bool SetFocus(bool focus = true) override;

    private:
        void RenderField(IRenderContext* ctx);
        void RenderCalendarIcon(IRenderContext* ctx, const Rect2Df& rect, const Color& color);
        Rect2Df ButtonRect() const;
        Rect2Df TextRect() const;
        Point2Df CalculatePopupPosition() const;

        std::string BuildDisplayText() const;   // formatted value (or empty)
        void SyncTextFromValue();             // refresh editBuffer from current value
        void CommitTextInput();               // parse editBuffer -> value
        void SetValueFromCalendarSingle(const UCDate& d);

        bool HandleMouseDown(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);
        void InsertText(const std::string& s);

        std::shared_ptr<UltraCanvasCalendarView> calendar;
        bool calendarOpen = false;

        std::string dateFormat = "yyyy-MM-dd";
        std::string placeholder = "Select a date";
        bool allowTextInput = true;

        // text editing state (active only when allowTextInput && focused)
        std::string editBuffer;
        int caretPos = 0;
        bool editing = false;

        DatePickerStyle style;
    };

// ===== DATE RANGE PICKER =====
// How a from-to span (e.g. a hotel stay) is entered.
    enum class DateRangePickerMode {
        SingleField,   // one field; start and end chosen in one process (two clicks in one calendar)
        TwoFields      // separate check-in and check-out fields, each with its own calendar
    };

// A composite from-to picker built on top of UltraCanvasDatePicker. It links
// the endpoints (end >= start + minNights, capped by maxNights) and honours a
// shared set of blocked dates so a stay can never span an unavailable night.
    class UltraCanvasDateRangePicker : public UltraCanvasContainer {
    public:
        std::function<void(const UCDate&)> onStartChanged;
        std::function<void(const UCDate&)> onEndChanged;
        std::function<void(const UCDate&, const UCDate&)> onRangeChanged; // both endpoints valid

        UltraCanvasDateRangePicker(const std::string& identifier, float x, float y, float w, float h);
        UltraCanvasDateRangePicker(const std::string& identifier, float w, float h)
            : UltraCanvasDateRangePicker(identifier, -1, -1, w, h) {}
        ~UltraCanvasDateRangePicker() override = default;

        // ===== MODE =====
        void SetMode(DateRangePickerMode m);
        DateRangePickerMode GetMode() const { return mode; }

        // ===== VALUES =====
        void SetStartDate(const UCDate& d, bool runCallbacks = false);
        void SetEndDate(const UCDate& d, bool runCallbacks = false);
        void SetRange(const UCDate& start, const UCDate& end, bool runCallbacks = false);
        UCDate GetStartDate() const { return startDate; }
        UCDate GetEndDate() const { return endDate; }
        void Clear(bool runCallbacks = false);

        // ===== CONFIG =====
        void SetDateFormat(const std::string& fmt);
        void SetFieldLabels(const std::string& startLbl, const std::string& endLbl);
        void SetMinNights(int n);             // end >= start + n (default 1)
        void SetMaxNights(int n);             // <= 0 means unlimited
        void SetFirstDayOfWeek(int dow);

        // ===== CONSTRAINTS / BLOCKED DATES =====
        void SetMinDate(const UCDate& d);
        void SetMaxDate(const UCDate& d);
        void SetDateEnabledPredicate(std::function<bool(const UCDate&)> pred);
        void SetBlockedDates(const std::vector<UCDate>& dates);
        void AddBlockedDate(const UCDate& d);
        void BlockDateRange(const UCDate& start, const UCDate& end);
        void ClearBlockedDates();

        // ===== STYLE =====
        void SetStyle(const DatePickerStyle& s);

        // ===== OVERRIDES =====
        void SetBounds(const Rect2Df& b) override;

    private:
        void Rebuild();
        void LayoutFields();
        void ApplyConstraintsToCalendars();
        bool BaseSelectable(const UCDate& d) const;     // min/max + blocked + user predicate
        bool IntervalAllSelectable(const UCDate& a, const UCDate& b) const;
        std::vector<UCDate> BlockedList() const;
        void HandleStartChosen(const UCDate& d);
        void HandleEndChosen(const UCDate& d);
        void HandleSingleRange(const UCDate& s, const UCDate& e);
        void FireRangeIfComplete();

        DateRangePickerMode mode = DateRangePickerMode::TwoFields;
        UCDate startDate, endDate;
        UCDate minDate, maxDate;
        int minNights = 1;
        int maxNights = 0;        // <= 0 = unlimited
        int firstDayOfWeek = 0;
        std::string dateFormat = "yyyy-MM-dd";
        std::string startLabel = "Check-in";
        std::string endLabel = "Check-out";
        std::set<long> blockedOrdinals;
        std::function<bool(const UCDate&)> userPredicate;
        DatePickerStyle style;

        std::shared_ptr<UltraCanvasDatePicker> startPicker;
        std::shared_ptr<UltraCanvasDatePicker> endPicker;
        std::shared_ptr<UltraCanvasDatePicker> singlePicker;
        std::shared_ptr<UltraCanvasUIElement> sepLabel;
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasDatePicker> CreateDatePicker(
            const std::string& identifier, float x, float y, float w, float h = 26) {
        return std::make_shared<UltraCanvasDatePicker>(identifier, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasDateRangePicker> CreateDateRangePicker(
            const std::string& identifier, float x, float y, float w, float h = 28,
            DateRangePickerMode mode = DateRangePickerMode::TwoFields) {
        auto p = std::make_shared<UltraCanvasDateRangePicker>(identifier, x, y, w, h);
        p->SetMode(mode);
        return p;
    }

    inline std::shared_ptr<UltraCanvasCalendarView> CreateCalendarView(
            const std::string& identifier, float x, float y) {
        auto cal = std::make_shared<UltraCanvasCalendarView>(identifier, x, y, 0, 0);
        Size2Df pref = cal->GetPreferredSize();
        cal->SetElementSize(pref);
        return cal;
    }

// ===== PREDEFINED CALENDAR STYLES =====
    namespace CalendarStyles {
        inline CalendarStyle Default() { return CalendarStyle(); }

        inline CalendarStyle Compact() {
            CalendarStyle s;
            s.cellWidth = 28.0f;
            s.cellHeight = 26.0f;
            s.headerHeight = 32.0f;
            s.weekdayRowHeight = 18.0f;
            s.footerHeight = 28.0f;
            s.panelPadding = 6.0f;
            s.fontSize = 11.0f;
            s.headerFontSize = 12.0f;
            return s;
        }

        inline CalendarStyle Dark() {
            CalendarStyle s;
            s.backgroundColor       = Color(43, 47, 54, 255);
            s.borderColor           = Color(70, 76, 86, 255);
            s.headerBackgroundColor = Color(43, 47, 54, 255);
            s.headerTextColor       = Color(230, 232, 236, 255);
            s.headerHoverColor      = Color(58, 64, 74, 255);
            s.navArrowColor         = Color(170, 176, 186, 255);
            s.navArrowHoverColor    = Color(120, 170, 255, 255);
            s.weekdayTextColor      = Color(140, 146, 156, 255);
            s.weekendWeekdayColor   = Color(225, 130, 130, 255);
            s.cellTextColor         = Color(220, 224, 230, 255);
            s.adjacentMonthColor    = Color(95, 101, 111, 255);
            s.weekendTextColor      = Color(225, 130, 130, 255);
            s.disabledTextColor     = Color(80, 85, 94, 255);
            s.hoverColor            = Color(58, 64, 74, 255);
            s.selectedColor         = Color(70, 130, 230, 255);
            s.selectedTextColor     = Colors::White;
            s.todayBorderColor      = Color(120, 170, 255, 255);
            s.focusBorderColor      = Color(120, 170, 255, 255);
            s.rangeFillColor        = Color(54, 72, 102, 255);
            s.weekNumberColor       = Color(110, 116, 126, 255);
            s.footerTextColor       = Color(120, 170, 255, 255);
            s.footerHoverColor      = Color(58, 64, 74, 255);
            s.footerSeparatorColor  = Color(70, 76, 86, 255);
            return s;
        }
    }

} // namespace UltraCanvas
