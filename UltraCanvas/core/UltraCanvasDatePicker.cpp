// core/UltraCanvasDatePicker.cpp
// Implementation of UCDate, UltraCanvasCalendarView and UltraCanvasDatePicker.
#include "UltraCanvasDatePicker.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasLabel.h"
#include <ctime>
#include <cstdio>
#include <cctype>
#include <algorithm>

namespace UltraCanvas {

// =====================================================================
//  UCDate
// =====================================================================

    bool UCDate::IsLeapYear(int y) {
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    }

    int UCDate::DaysInMonth(int y, int m) {
        static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (m < 1 || m > 12) return 30;
        if (m == 2 && IsLeapYear(y)) return 29;
        return d[m - 1];
    }

    bool UCDate::IsValid() const {
        if (year == 0 || month < 1 || month > 12) return false;
        return day >= 1 && day <= DaysInMonth(year, month);
    }

    // Sakamoto's algorithm: 0 = Sunday .. 6 = Saturday.
    int UCDate::DayOfWeek() const {
        static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        int y = year;
        if (month < 3) y -= 1;
        int r = (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;
        return (r + 7) % 7;
    }

    // Days from civil (Howard Hinnant), shifted so 1970-01-01 == 0.
    long UCDate::ToOrdinal() const {
        long y = year;
        y -= (month <= 2);
        long era = (y >= 0 ? y : y - 399) / 400;
        long yoe = y - era * 400;
        long mp = (month + (month > 2 ? -3 : 9));
        long doy = (153 * mp + 2) / 5 + day - 1;
        long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }

    UCDate UCDate::FromOrdinal(long z) {
        z += 719468;
        long era = (z >= 0 ? z : z - 146096) / 146097;
        long doe = z - era * 146097;
        long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        long y = yoe + era * 400;
        long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        long mp = (5 * doy + 2) / 153;
        long d = doy - (153 * mp + 2) / 5 + 1;
        long m = mp < 10 ? mp + 3 : mp - 9;
        return UCDate(static_cast<int>(y + (m <= 2)), static_cast<int>(m), static_cast<int>(d));
    }

    UCDate UCDate::AddMonths(int n) const {
        long zeroBased = static_cast<long>(year) * 12 + (month - 1) + n;
        long y = zeroBased / 12;
        long m = zeroBased % 12;
        if (m < 0) { m += 12; y -= 1; }
        int mm = static_cast<int>(m) + 1;
        int yy = static_cast<int>(y);
        int dd = std::min(day, DaysInMonth(yy, mm));
        return UCDate(yy, mm, dd);
    }

    int UCDate::ISOWeek() const {
        long ord = ToOrdinal();
        int dowMon = (DayOfWeek() + 6) % 7;      // 0 = Monday .. 6 = Sunday
        long thursdayOrd = ord - dowMon + 3;     // Thursday of this week
        UCDate th = FromOrdinal(thursdayOrd);
        long jan1 = UCDate(th.year, 1, 1).ToOrdinal();
        return static_cast<int>((thursdayOrd - jan1) / 7 + 1);
    }

    UCDate UCDate::Today() {
        std::time_t t = std::time(nullptr);
        std::tm tmv {};
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        return UCDate(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    }

    std::string UCDate::Format(const std::string& fmt) const {
        if (IsEmpty()) return "";
        std::string out;
        size_t i = 0;
        auto run = [&](char c) {
            size_t n = 0;
            while (i + n < fmt.size() && fmt[i + n] == c) ++n;
            return n;
        };
        char buf[16];
        while (i < fmt.size()) {
            char c = fmt[i];
            if (c == 'y') {
                size_t n = run('y');
                if (n <= 2) snprintf(buf, sizeof buf, "%02d", (year % 100 + 100) % 100);
                else        snprintf(buf, sizeof buf, "%04d", year);
                out += buf; i += n;
            } else if (c == 'M') {
                size_t n = run('M');
                if (n >= 2) snprintf(buf, sizeof buf, "%02d", month);
                else        snprintf(buf, sizeof buf, "%d", month);
                out += buf; i += n;
            } else if (c == 'd') {
                size_t n = run('d');
                if (n >= 2) snprintf(buf, sizeof buf, "%02d", day);
                else        snprintf(buf, sizeof buf, "%d", day);
                out += buf; i += n;
            } else {
                out += c; ++i;
            }
        }
        return out;
    }

    bool UCDate::Parse(const std::string& text, const std::string& fmt, UCDate& out) {
        // Field order from the format string.
        std::vector<char> order;
        for (size_t i = 0; i < fmt.size();) {
            char c = fmt[i];
            if (c == 'y' || c == 'M' || c == 'd') {
                order.push_back(c);
                while (i < fmt.size() && fmt[i] == c) ++i;
            } else {
                ++i;
            }
        }
        if (order.empty()) return false;

        // Numeric groups from the input.
        std::vector<int> nums;
        for (size_t i = 0; i < text.size();) {
            if (std::isdigit(static_cast<unsigned char>(text[i]))) {
                int v = 0;
                while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
                    v = v * 10 + (text[i] - '0');
                    ++i;
                }
                nums.push_back(v);
            } else {
                ++i;
            }
        }
        if (nums.size() < order.size()) return false;

        int y = 0, m = 0, d = 0;
        for (size_t k = 0; k < order.size(); ++k) {
            int v = nums[k];
            switch (order[k]) {
                case 'y': y = v; break;
                case 'M': m = v; break;
                case 'd': d = v; break;
            }
        }
        if (y > 0 && y < 100) y += 2000;
        UCDate candidate(y, m, d);
        if (!candidate.IsValid()) return false;
        out = candidate;
        return true;
    }

// =====================================================================
//  UltraCanvasCalendarView
// =====================================================================

    UltraCanvasCalendarView::UltraCanvasCalendarView(const std::string& identifier,
                                                     float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        weekdayNames = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
        monthNamesLong = {"January", "February", "March", "April", "May", "June",
                          "July", "August", "September", "October", "November", "December"};
        monthNamesShort = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        UCDate today = UCDate::Today();
        visibleYear = today.year;
        visibleMonth = today.month;
    }

    Size2Df UltraCanvasCalendarView::GetPreferredSize() const {
        float gridW = (showWeekNumbers ? style.weekNumberColumnWidth : 0.0f) + 7.0f * style.cellWidth;
        float w = gridW + style.panelPadding * 2.0f;
        float h = style.panelPadding * 2.0f + style.headerHeight + style.weekdayRowHeight
                  + 6.0f * style.cellHeight + (showFooter ? style.footerHeight : 0.0f);
        return Size2Df(w, h);
    }

    void UltraCanvasCalendarView::SetSelectionMode(DateSelectionMode mode) {
        selectionMode = mode;
        selectingRange = false;
        RequestRedraw();
    }

    void UltraCanvasCalendarView::SetSelectedDate(const UCDate& date, bool runCallbacks) {
        selectedDate = date;
        if (date.IsValid()) {
            visibleYear = date.year;
            visibleMonth = date.month;
            focusedDate = date;
            hasFocusCell = true;
        }
        if (runCallbacks && onDateSelected) onDateSelected(selectedDate);
        RequestRedraw();
    }

    void UltraCanvasCalendarView::ClearSelection(bool runCallbacks) {
        selectedDate = UCDate();
        selectedOrdinals.clear();
        rangeStart = rangeEnd = UCDate();
        selectingRange = false;
        if (runCallbacks) {
            if (onDateSelected) onDateSelected(UCDate());
            if (onMultipleChanged) onMultipleChanged({});
        }
        RequestRedraw();
    }

    void UltraCanvasCalendarView::SetSelectedDates(const std::vector<UCDate>& dates) {
        selectedOrdinals.clear();
        for (const auto& d : dates) {
            if (d.IsValid()) selectedOrdinals.insert(d.ToOrdinal());
        }
        RequestRedraw();
    }

    std::vector<UCDate> UltraCanvasCalendarView::GetSelectedDates() const {
        std::vector<UCDate> result;
        result.reserve(selectedOrdinals.size());
        for (long ord : selectedOrdinals) result.push_back(UCDate::FromOrdinal(ord));
        return result;
    }

    void UltraCanvasCalendarView::SetRange(const UCDate& start, const UCDate& end, bool runCallbacks) {
        UCDate s = start, e = end;
        if (s.IsValid() && e.IsValid() && e < s) std::swap(s, e);
        rangeStart = s;
        rangeEnd = e;
        selectingRange = false;
        if (s.IsValid()) { visibleYear = s.year; visibleMonth = s.month; }
        if (runCallbacks && onRangeSelected && s.IsValid() && e.IsValid()) onRangeSelected(s, e);
        RequestRedraw();
    }

    void UltraCanvasCalendarView::SetVisibleMonth(int year, int month) {
        while (month < 1) { month += 12; year--; }
        while (month > 12) { month -= 12; year++; }
        if (year == visibleYear && month == visibleMonth) return;
        visibleYear = year;
        visibleMonth = month;
        if (onVisibleMonthChanged) onVisibleMonthChanged(visibleYear, visibleMonth);
        RequestRedraw();
    }

    void UltraCanvasCalendarView::GoToToday() {
        UCDate t = UCDate::Today();
        SetVisibleMonth(t.year, t.month);
    }

    void UltraCanvasCalendarView::SetWeekdayNames(const std::vector<std::string>& shortNames) {
        if (shortNames.size() == 7) { weekdayNames = shortNames; RequestRedraw(); }
    }

    void UltraCanvasCalendarView::SetMonthNames(const std::vector<std::string>& longNames,
                                                const std::vector<std::string>& shortNames) {
        if (longNames.size() == 12) monthNamesLong = longNames;
        if (shortNames.size() == 12) monthNamesShort = shortNames;
        RequestRedraw();
    }

    void UltraCanvasCalendarView::SetShowWeekNumbers(bool show) {
        showWeekNumbers = show;
        RequestRedraw();
    }

    bool UltraCanvasCalendarView::IsDateSelectable(const UCDate& d) const {
        if (!d.IsValid()) return false;
        if (minDate.IsValid() && d < minDate) return false;
        if (maxDate.IsValid() && d > maxDate) return false;
        if (IsDateBlocked(d)) return false;
        if (dateEnabledPredicate && !dateEnabledPredicate(d)) return false;
        return true;
    }

    // ---- blocked dates ----------------------------------------------

    void UltraCanvasCalendarView::SetBlockedDates(const std::vector<UCDate>& dates) {
        blockedOrdinals.clear();
        for (const auto& d : dates)
            if (d.IsValid()) blockedOrdinals.insert(d.ToOrdinal());
        RequestRedraw();
    }

    void UltraCanvasCalendarView::AddBlockedDate(const UCDate& d) {
        if (d.IsValid()) { blockedOrdinals.insert(d.ToOrdinal()); RequestRedraw(); }
    }

    void UltraCanvasCalendarView::BlockDateRange(const UCDate& start, const UCDate& end) {
        if (!start.IsValid() || !end.IsValid()) return;
        long a = start.ToOrdinal(), b = end.ToOrdinal();
        if (b < a) std::swap(a, b);
        for (long o = a; o <= b; ++o) blockedOrdinals.insert(o);
        RequestRedraw();
    }

    void UltraCanvasCalendarView::ClearBlockedDates() {
        blockedOrdinals.clear();
        RequestRedraw();
    }

    bool UltraCanvasCalendarView::IsDateBlocked(const UCDate& d) const {
        return d.IsValid() && blockedOrdinals.count(d.ToOrdinal()) > 0;
    }

    bool UltraCanvasCalendarView::RangeHasBlocked(const UCDate& a, const UCDate& b) const {
        long lo = a.ToOrdinal(), hi = b.ToOrdinal();
        if (hi < lo) std::swap(lo, hi);
        for (long o = lo; o <= hi; ++o)
            if (!IsDateSelectable(UCDate::FromOrdinal(o))) return true;
        return false;
    }

    UCDate UltraCanvasCalendarView::ClampRangeEnd(const UCDate& start, const UCDate& target) const {
        if (!start.IsValid()) return target;
        if (target == start) return start;
        int dir = (target < start) ? -1 : 1;
        UCDate cur = start, last = start;
        while (!(cur == target)) {
            UCDate nxt = cur.AddDays(dir);
            if (!IsDateSelectable(nxt)) break;
            last = nxt;
            cur = nxt;
        }
        return last;
    }

    UCDate UltraCanvasCalendarView::FirstVisibleCell() const {
        UCDate first(visibleYear, visibleMonth, 1);
        int offset = (first.DayOfWeek() - firstDayOfWeek + 7) % 7;
        return first.AddDays(-offset);
    }

    UCDate UltraCanvasCalendarView::DateForDayCell(int index) const {
        return FirstVisibleCell().AddDays(index);
    }

    bool UltraCanvasCalendarView::IsSelected(const UCDate& d) const {
        switch (selectionMode) {
            case DateSelectionMode::Single:
                return d == selectedDate;
            case DateSelectionMode::Multiple:
                return selectedOrdinals.count(d.ToOrdinal()) > 0;
            case DateSelectionMode::Range:
            case DateSelectionMode::Week:
                return (rangeStart.IsValid() && d == rangeStart) ||
                       (rangeEnd.IsValid() && d == rangeEnd) ||
                       (selectingRange && rangeHoverEnd.IsValid() && d == rangeHoverEnd);
        }
        return false;
    }

    bool UltraCanvasCalendarView::InRangeHighlight(const UCDate& d) const {
        if (selectionMode != DateSelectionMode::Range && selectionMode != DateSelectionMode::Week)
            return false;
        UCDate lo = rangeStart, hi = rangeEnd;
        if (selectingRange && rangeStart.IsValid()) {
            lo = rangeStart;
            hi = rangeHoverEnd.IsValid() ? rangeHoverEnd : rangeStart;
        }
        if (!lo.IsValid() || !hi.IsValid()) return false;
        if (hi < lo) std::swap(lo, hi);
        return !(d < lo) && !(hi < d);
    }

    bool UltraCanvasCalendarView::IsRangeEndpoint(const UCDate& d) const {
        return IsSelected(d);
    }

    // ---- layout -----------------------------------------------------

    UltraCanvasCalendarView::Layout UltraCanvasCalendarView::ComputeLayout() const {
        Rect2Df b = GetLocalBounds();
        Layout L;
        float pad = style.panelPadding;
        float innerX = pad;
        float innerW = b.width - pad * 2.0f;

        L.header = Rect2Df(innerX, pad, innerW, style.headerHeight);
        float navW = style.headerHeight;
        L.prevButton = Rect2Df(L.header.x, L.header.y, navW, L.header.height);
        L.nextButton = Rect2Df(L.header.Right() - navW, L.header.y, navW, L.header.height);
        L.titleButton = Rect2Df(L.prevButton.Right(), L.header.y,
                                L.nextButton.x - L.prevButton.Right(), L.header.height);

        float footerH = showFooter ? style.footerHeight : 0.0f;
        L.footer = Rect2Df(innerX, b.height - pad - footerH, innerW, footerH);
        L.todayButton = Rect2Df(L.footer.x, L.footer.y, L.footer.width / 2.0f, L.footer.height);
        L.clearButton = Rect2Df(L.footer.x + L.footer.width / 2.0f, L.footer.y,
                                L.footer.width / 2.0f, L.footer.height);

        L.weekColW = showWeekNumbers ? style.weekNumberColumnWidth : 0.0f;

        float bodyBottom = showFooter ? L.footer.y : (b.height - pad);
        float bodyTop;
        if (level == CalendarViewLevel::Days) {
            L.weekdayRow = Rect2Df(innerX, L.header.Bottom(), innerW, style.weekdayRowHeight);
            bodyTop = L.weekdayRow.Bottom();
            L.cols = 7; L.rows = 6;
            L.body = Rect2Df(innerX, bodyTop, innerW, bodyBottom - bodyTop);
            L.cellW = (L.body.width - L.weekColW) / 7.0f;
            L.cellH = L.body.height / 6.0f;
            L.gridOriginX = L.body.x + L.weekColW;
            L.gridOriginY = L.body.y;
        } else {
            L.weekdayRow = Rect2Df(innerX, L.header.Bottom(), innerW, 0.0f);
            bodyTop = L.header.Bottom();
            L.cols = 4; L.rows = 3;
            L.body = Rect2Df(innerX, bodyTop, innerW, bodyBottom - bodyTop);
            L.cellW = L.body.width / 4.0f;
            L.cellH = L.body.height / 3.0f;
            L.gridOriginX = L.body.x;
            L.gridOriginY = L.body.y;
        }
        return L;
    }

    int UltraCanvasCalendarView::DayCellAtPoint(const Layout& l, const Point2Df& p) const {
        if (p.x < l.gridOriginX || p.y < l.gridOriginY) return -1;
        int col = static_cast<int>((p.x - l.gridOriginX) / l.cellW);
        int row = static_cast<int>((p.y - l.gridOriginY) / l.cellH);
        if (col < 0 || col > 6 || row < 0 || row > 5) return -1;
        return row * 7 + col;
    }

    int UltraCanvasCalendarView::GridCellAtPoint(const Layout& l, const Point2Df& p, int cols, int rows) const {
        if (p.x < l.gridOriginX || p.y < l.gridOriginY) return -1;
        int col = static_cast<int>((p.x - l.gridOriginX) / l.cellW);
        int row = static_cast<int>((p.y - l.gridOriginY) / l.cellH);
        if (col < 0 || col >= cols || row < 0 || row >= rows) return -1;
        return row * cols + col;
    }

    // ---- rendering --------------------------------------------------

    void UltraCanvasCalendarView::DrawCenteredText(IRenderContext* ctx, const std::string& text,
                                                   const Rect2Df& rect, const Color& color,
                                                   float fontSize, FontWeight weight) {
        if (text.empty()) return;
        ctx->SetFontFace(style.fontFamily, weight, FontSlant::Normal);
        ctx->SetFontSize(fontSize);
        ctx->SetTextPaint(color);
        Size2Di ts = ctx->GetTextLineDimensions(text);
        float tx = rect.x + (rect.width - ts.width) / 2.0f;
        float ty = rect.y + (rect.height - ts.height) / 2.0f;
        ctx->DrawText(text, Point2Dd(tx, ty));
    }

    void UltraCanvasCalendarView::DrawHeader(IRenderContext* ctx, const Layout& l) {
        // Title (with hover background)
        if (hoverNavRegion == 3) {
            ctx->DrawFilledRectangle(l.titleButton, style.headerHoverColor, 0.0f, Colors::Transparent, style.cellCornerRadius);
        }
        DrawCenteredText(ctx, HeaderTitle(), l.titleButton, style.headerTextColor,
                         style.headerFontSize, FontWeight::Bold);

        // Navigation chevrons
        auto drawChevron = [&](const Rect2Df& r, bool left, bool hovered) {
            Color c = hovered ? style.navArrowHoverColor : style.navArrowColor;
            ctx->SetStrokePaint(c);
            ctx->SetStrokeWidth(1.6f);
            float cx = r.x + r.width / 2.0f;
            float cy = r.y + r.height / 2.0f;
            float s = 4.0f;
            if (left) {
                ctx->DrawLine(Point2Dd(cx + s * 0.5, cy - s), Point2Dd(cx - s * 0.5, cy));
                ctx->DrawLine(Point2Dd(cx - s * 0.5, cy), Point2Dd(cx + s * 0.5, cy + s));
            } else {
                ctx->DrawLine(Point2Dd(cx - s * 0.5, cy - s), Point2Dd(cx + s * 0.5, cy));
                ctx->DrawLine(Point2Dd(cx + s * 0.5, cy), Point2Dd(cx - s * 0.5, cy + s));
            }
        };
        drawChevron(l.prevButton, true, hoverNavRegion == 1);
        drawChevron(l.nextButton, false, hoverNavRegion == 2);
    }

    void UltraCanvasCalendarView::DrawFooter(IRenderContext* ctx, const Layout& l) {
        if (!showFooter || l.footer.height <= 0) return;
        ctx->SetStrokePaint(style.footerSeparatorColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Dd(l.footer.x, l.footer.y), Point2Dd(l.footer.Right(), l.footer.y));

        if (hoverNavRegion == 4)
            ctx->DrawFilledRectangle(l.todayButton, style.footerHoverColor, 0.0f, Colors::Transparent, style.cellCornerRadius);
        if (hoverNavRegion == 5)
            ctx->DrawFilledRectangle(l.clearButton, style.footerHoverColor, 0.0f, Colors::Transparent, style.cellCornerRadius);

        DrawCenteredText(ctx, "Today", l.todayButton, style.footerTextColor, style.fontSize, FontWeight::Normal);
        DrawCenteredText(ctx, "Clear", l.clearButton, style.footerTextColor, style.fontSize, FontWeight::Normal);
    }

    void UltraCanvasCalendarView::DrawDaysLevel(IRenderContext* ctx, const Layout& l) {
        // Weekday header row
        for (int col = 0; col < 7; ++col) {
            int dow = (firstDayOfWeek + col) % 7;
            Rect2Df r(l.gridOriginX + col * l.cellW, l.weekdayRow.y, l.cellW, l.weekdayRow.height);
            Color c = IsWeekend(dow) ? style.weekendWeekdayColor : style.weekdayTextColor;
            DrawCenteredText(ctx, weekdayNames[dow], r, c, style.fontSize, FontWeight::Normal);
        }

        UCDate today = UCDate::Today();

        // Week numbers
        if (showWeekNumbers) {
            for (int row = 0; row < 6; ++row) {
                UCDate d = DateForDayCell(row * 7);
                Rect2Df r(l.body.x, l.gridOriginY + row * l.cellH, l.weekColW, l.cellH);
                DrawCenteredText(ctx, std::to_string(d.ISOWeek()), r, style.weekNumberColor,
                                 style.fontSize - 1.0f, FontWeight::Normal);
            }
        }

        // Day cells
        for (int i = 0; i < 42; ++i) {
            UCDate d = DateForDayCell(i);
            bool inMonth = (d.month == visibleMonth && d.year == visibleYear);
            if (!inMonth && !showAdjacentDays) continue;

            int col = i % 7, row = i / 7;
            Rect2Df cell(l.gridOriginX + col * l.cellW, l.gridOriginY + row * l.cellH, l.cellW, l.cellH);

            bool selectable = IsDateSelectable(d);
            bool selected = IsSelected(d);
            bool inRange = InRangeHighlight(d);
            bool isToday = (d == today);
            bool isFocus = hasFocusCell && d == focusedDate && IsFocused();
            bool isHover = (d == hoverDate);

            // Continuous range band fills the whole cell.
            if (inRange && !selected) {
                ctx->DrawFilledRectangle(cell, style.rangeFillColor, 0.0f, Colors::Transparent, 0.0f);
            }

            // Centred rounded highlight for selected / hover / today / focus.
            float side = std::min(l.cellW, l.cellH) - 4.0f;
            if (side < 6.0f) side = std::min(l.cellW, l.cellH);
            Rect2Df hi(cell.x + (cell.width - side) / 2.0f, cell.y + (cell.height - side) / 2.0f, side, side);

            if (selected) {
                ctx->DrawFilledRectangle(hi, style.selectedColor, 0.0f, Colors::Transparent, style.cellCornerRadius);
            } else if (isHover && selectable) {
                ctx->DrawFilledRectangle(hi, style.hoverColor, 0.0f, Colors::Transparent, style.cellCornerRadius);
            }
            if (isToday && !selected) {
                ctx->DrawFilledRectangle(hi, Colors::Transparent, 1.0f, style.todayBorderColor, style.cellCornerRadius);
            }
            if (isFocus && !selected) {
                ctx->DrawFilledRectangle(hi, Colors::Transparent, 1.0f, style.focusBorderColor, style.cellCornerRadius);
            }

            // Text colour
            Color tc;
            if (selected)            tc = style.selectedTextColor;
            else if (!selectable)    tc = style.disabledTextColor;
            else if (!inMonth)       tc = style.adjacentMonthColor;
            else if (IsWeekend(d.DayOfWeek())) tc = style.weekendTextColor;
            else                     tc = style.cellTextColor;

            DrawCenteredText(ctx, std::to_string(d.day), cell, tc, style.fontSize, FontWeight::Normal);

            // Strike through explicitly-blocked (unavailable) days.
            if (IsDateBlocked(d) && (inMonth || showAdjacentDays)) {
                float my = cell.y + cell.height / 2.0f;
                float inset = std::min(l.cellW, l.cellH) * 0.30f;
                ctx->SetStrokePaint(style.blockedStrikeColor);
                ctx->SetStrokeWidth(1.2f);
                ctx->DrawLine(Point2Dd(cell.x + inset, my), Point2Dd(cell.Right() - inset, my));
            }
        }
    }

    void UltraCanvasCalendarView::DrawMonthsLevel(IRenderContext* ctx, const Layout& l) {
        for (int i = 0; i < 12; ++i) {
            int col = i % 4, row = i / 4;
            Rect2Df cell(l.gridOriginX + col * l.cellW, l.gridOriginY + row * l.cellH, l.cellW, l.cellH);
            int month = i + 1;
            bool current = (month == visibleMonth);
            bool hover = (hoverCellIndex == i);

            float padX = 6.0f, padY = 5.0f;
            Rect2Df hi(cell.x + padX, cell.y + padY, cell.width - padX * 2, cell.height - padY * 2);
            if (current)      ctx->DrawFilledRectangle(hi, style.selectedColor, 0.0f, Colors::Transparent, style.cellCornerRadius);
            else if (hover)   ctx->DrawFilledRectangle(hi, style.hoverColor, 0.0f, Colors::Transparent, style.cellCornerRadius);

            Color tc = current ? style.selectedTextColor : style.cellTextColor;
            DrawCenteredText(ctx, monthNamesShort[i], cell, tc, style.fontSize, FontWeight::Normal);
        }
    }

    void UltraCanvasCalendarView::DrawYearsLevel(IRenderContext* ctx, const Layout& l) {
        int startYear = DecadeStartYear();
        for (int i = 0; i < 12; ++i) {
            int col = i % 4, row = i / 4;
            Rect2Df cell(l.gridOriginX + col * l.cellW, l.gridOriginY + row * l.cellH, l.cellW, l.cellH);
            int year = startYear + i;
            bool current = (year == visibleYear);
            bool hover = (hoverCellIndex == i);
            bool outside = (i == 0 || i == 11);  // padding years from adjacent decades

            float padX = 6.0f, padY = 5.0f;
            Rect2Df hi(cell.x + padX, cell.y + padY, cell.width - padX * 2, cell.height - padY * 2);
            if (current)      ctx->DrawFilledRectangle(hi, style.selectedColor, 0.0f, Colors::Transparent, style.cellCornerRadius);
            else if (hover)   ctx->DrawFilledRectangle(hi, style.hoverColor, 0.0f, Colors::Transparent, style.cellCornerRadius);

            Color tc = current ? style.selectedTextColor
                               : (outside ? style.adjacentMonthColor : style.cellTextColor);
            DrawCenteredText(ctx, std::to_string(year), cell, tc, style.fontSize, FontWeight::Normal);
        }
    }

    std::string UltraCanvasCalendarView::HeaderTitle() const {
        switch (level) {
            case CalendarViewLevel::Days: {
                std::string mn = (visibleMonth >= 1 && visibleMonth <= 12) ? monthNamesLong[visibleMonth - 1] : "";
                return mn + " " + std::to_string(visibleYear);
            }
            case CalendarViewLevel::Months:
                return std::to_string(visibleYear);
            case CalendarViewLevel::Years: {
                int s = DecadeStartYear();
                return std::to_string(s + 1) + " - " + std::to_string(s + 10);
            }
        }
        return "";
    }

    void UltraCanvasCalendarView::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        Rect2Df b = GetLocalBounds();
        ctx->DrawFilledRectangle(b, style.backgroundColor, style.borderWidth, style.borderColor, style.cornerRadius);

        Layout l = ComputeLayout();
        DrawHeader(ctx, l);
        switch (level) {
            case CalendarViewLevel::Days:   DrawDaysLevel(ctx, l);   break;
            case CalendarViewLevel::Months: DrawMonthsLevel(ctx, l); break;
            case CalendarViewLevel::Years:  DrawYearsLevel(ctx, l);  break;
        }
        DrawFooter(ctx, l);
    }

    // ---- interaction ------------------------------------------------

    void UltraCanvasCalendarView::HandleDayClick(const UCDate& d) {
        if (!IsDateSelectable(d)) return;
        focusedDate = d;
        hasFocusCell = true;

        switch (selectionMode) {
            case DateSelectionMode::Single:
                selectedDate = d;
                if (onDateSelected) onDateSelected(d);
                if (onDateActivated) onDateActivated(d);
                break;

            case DateSelectionMode::Multiple: {
                long ord = d.ToOrdinal();
                if (selectedOrdinals.count(ord)) selectedOrdinals.erase(ord);
                else                              selectedOrdinals.insert(ord);
                if (onDateSelected) onDateSelected(d);
                if (onMultipleChanged) onMultipleChanged(GetSelectedDates());
                break;
            }

            case DateSelectionMode::Range:
                if (!selectingRange || !rangeStart.IsValid()) {
                    rangeStart = d;
                    rangeEnd = UCDate();
                    rangeHoverEnd = d;
                    selectingRange = true;
                } else {
                    UCDate s = rangeStart, e = d;
                    if (e < s) std::swap(s, e);
                    if (RangeHasBlocked(s, e)) {
                        // The span would cross an unavailable date — start over
                        // from the new click instead of selecting across it.
                        rangeStart = d;
                        rangeEnd = UCDate();
                        rangeHoverEnd = d;
                        selectingRange = true;
                    } else {
                        rangeStart = s;
                        rangeEnd = e;
                        selectingRange = false;
                        if (onRangeSelected) onRangeSelected(s, e);
                    }
                }
                break;

            case DateSelectionMode::Week: {
                int offset = (d.DayOfWeek() - firstDayOfWeek + 7) % 7;
                UCDate ws = d.AddDays(-offset);
                UCDate we = ws.AddDays(6);
                rangeStart = ws;
                rangeEnd = we;
                selectingRange = false;
                if (onRangeSelected) onRangeSelected(ws, we);
                break;
            }
        }
        RequestRedraw();
    }

    void UltraCanvasCalendarView::NavigatePrev() {
        if (level == CalendarViewLevel::Days)        SetVisibleMonth(visibleYear, visibleMonth - 1);
        else if (level == CalendarViewLevel::Months) { visibleYear--; RequestRedraw(); }
        else                                         { visibleYear -= 10; RequestRedraw(); }
    }

    void UltraCanvasCalendarView::NavigateNext() {
        if (level == CalendarViewLevel::Days)        SetVisibleMonth(visibleYear, visibleMonth + 1);
        else if (level == CalendarViewLevel::Months) { visibleYear++; RequestRedraw(); }
        else                                         { visibleYear += 10; RequestRedraw(); }
    }

    void UltraCanvasCalendarView::CycleTitle() {
        if (level == CalendarViewLevel::Days)        level = CalendarViewLevel::Months;
        else if (level == CalendarViewLevel::Months) level = CalendarViewLevel::Years;
        // Years is the top level.
        hoverCellIndex = -1;
        RequestRedraw();
    }

    void UltraCanvasCalendarView::EnsureFocusVisible() {
        if (focusedDate.year != visibleYear || focusedDate.month != visibleMonth) {
            SetVisibleMonth(focusedDate.year, focusedDate.month);
        }
    }

    void UltraCanvasCalendarView::MoveFocus(int days) {
        if (!hasFocusCell) {
            if (selectedDate.IsValid()) focusedDate = selectedDate;
            else {
                UCDate t = UCDate::Today();
                focusedDate = (t.year == visibleYear && t.month == visibleMonth)
                              ? t : UCDate(visibleYear, visibleMonth, 1);
            }
            hasFocusCell = true;
        } else {
            focusedDate = focusedDate.AddDays(days);
        }
        EnsureFocusVisible();
        RequestRedraw();
    }

    bool UltraCanvasCalendarView::HandleKeyDown(const UCEvent& event) {
        if (level != CalendarViewLevel::Days) {
            switch (event.virtualKey) {
                case UCKeys::Left:
                    if (level == CalendarViewLevel::Months) visibleMonth = std::max(1, visibleMonth - 1);
                    else visibleYear--;
                    RequestRedraw(); return true;
                case UCKeys::Right:
                    if (level == CalendarViewLevel::Months) visibleMonth = std::min(12, visibleMonth + 1);
                    else visibleYear++;
                    RequestRedraw(); return true;
                case UCKeys::Up:
                    if (level == CalendarViewLevel::Months) visibleMonth = std::max(1, visibleMonth - 4);
                    else visibleYear -= 4;
                    RequestRedraw(); return true;
                case UCKeys::Down:
                    if (level == CalendarViewLevel::Months) visibleMonth = std::min(12, visibleMonth + 4);
                    else visibleYear += 4;
                    RequestRedraw(); return true;
                case UCKeys::Return:
                case UCKeys::Space:
                    level = (level == CalendarViewLevel::Years) ? CalendarViewLevel::Months : CalendarViewLevel::Days;
                    RequestRedraw(); return true;
                default:
                    return false;
            }
        }

        // Days level
        switch (event.virtualKey) {
            case UCKeys::Left:  MoveFocus(-1); return true;
            case UCKeys::Right: MoveFocus(1);  return true;
            case UCKeys::Up:    MoveFocus(-7); return true;
            case UCKeys::Down:  MoveFocus(7);  return true;
            case UCKeys::PageUp:
                if (!hasFocusCell) MoveFocus(0);
                focusedDate = focusedDate.AddMonths(event.shift ? -12 : -1);
                EnsureFocusVisible(); RequestRedraw(); return true;
            case UCKeys::PageDown:
                if (!hasFocusCell) MoveFocus(0);
                focusedDate = focusedDate.AddMonths(event.shift ? 12 : 1);
                EnsureFocusVisible(); RequestRedraw(); return true;
            case UCKeys::Home:
                if (!hasFocusCell) MoveFocus(0);
                focusedDate = focusedDate.AddDays(-((focusedDate.DayOfWeek() - firstDayOfWeek + 7) % 7));
                EnsureFocusVisible(); RequestRedraw(); return true;
            case UCKeys::End:
                if (!hasFocusCell) MoveFocus(0);
                focusedDate = focusedDate.AddDays(6 - ((focusedDate.DayOfWeek() - firstDayOfWeek + 7) % 7));
                EnsureFocusVisible(); RequestRedraw(); return true;
            case UCKeys::Return:
            case UCKeys::Space:
                if (!hasFocusCell) { MoveFocus(0); return true; }
                HandleDayClick(focusedDate);
                return true;
            default:
                return false;
        }
    }

    bool UltraCanvasCalendarView::OnEvent(const UCEvent& event) {
        switch (event.type) {
            case UCEventType::MouseDown: {
                Layout l = ComputeLayout();
                Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
                if (l.prevButton.Contains(p)) { NavigatePrev(); return true; }
                if (l.nextButton.Contains(p)) { NavigateNext(); return true; }
                if (l.titleButton.Contains(p)) { CycleTitle(); return true; }
                if (showFooter && l.footer.height > 0) {
                    if (l.todayButton.Contains(p)) { GoToToday(); HandleDayClick(UCDate::Today()); return true; }
                    if (l.clearButton.Contains(p)) { ClearSelection(true); return true; }
                }
                if (level == CalendarViewLevel::Days) {
                    int i = DayCellAtPoint(l, p);
                    if (i >= 0) {
                        UCDate d = DateForDayCell(i);
                        if (showAdjacentDays || (d.month == visibleMonth && d.year == visibleYear))
                            HandleDayClick(d);
                        return true;
                    }
                } else {
                    int i = GridCellAtPoint(l, p, 4, 3);
                    if (i >= 0 && i < 12) {
                        if (level == CalendarViewLevel::Months) {
                            visibleMonth = i + 1;
                            level = CalendarViewLevel::Days;
                            if (onVisibleMonthChanged) onVisibleMonthChanged(visibleYear, visibleMonth);
                        } else {
                            visibleYear = DecadeStartYear() + i;
                            level = CalendarViewLevel::Months;
                        }
                        hoverCellIndex = -1;
                        RequestRedraw();
                        return true;
                    }
                }
                return false;
            }

            case UCEventType::MouseMove: {
                Layout l = ComputeLayout();
                Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
                int region = 0;
                UCDate newHover;
                int newHoverIndex = -1;
                if (l.prevButton.Contains(p)) region = 1;
                else if (l.nextButton.Contains(p)) region = 2;
                else if (l.titleButton.Contains(p)) region = 3;
                else if (showFooter && l.todayButton.Contains(p)) region = 4;
                else if (showFooter && l.clearButton.Contains(p)) region = 5;
                else if (level == CalendarViewLevel::Days) {
                    int i = DayCellAtPoint(l, p);
                    if (i >= 0) {
                        UCDate d = DateForDayCell(i);
                        if (showAdjacentDays || (d.month == visibleMonth && d.year == visibleYear))
                            newHover = d;
                    }
                } else {
                    newHoverIndex = GridCellAtPoint(l, p, 4, 3);
                }
                bool changed = (region != hoverNavRegion) || (newHover != hoverDate) || (newHoverIndex != hoverCellIndex);
                hoverNavRegion = region;
                hoverDate = newHover;
                hoverCellIndex = newHoverIndex;
                if (selectingRange && newHover.IsValid()) {
                    // Cap the live preview so it cannot stretch across a blocked date.
                    UCDate capped = ClampRangeEnd(rangeStart, newHover);
                    if (!(capped == rangeHoverEnd)) {
                        rangeHoverEnd = capped;
                        changed = true;
                    }
                }
                bool overClickable = region != 0 || newHover.IsValid() || newHoverIndex >= 0;
                SetMouseCursor(overClickable ? UCMouseCursor::Hand : UCMouseCursor::Default);
                if (changed) RequestRedraw();
                return false;
            }

            case UCEventType::MouseLeave: {
                if (hoverNavRegion != 0 || hoverDate.IsValid() || hoverCellIndex >= 0) {
                    hoverNavRegion = 0;
                    hoverDate = UCDate();
                    hoverCellIndex = -1;
                    RequestRedraw();
                }
                return false;
            }

            case UCEventType::MouseWheel: {
                if (event.wheelDelta > 0) NavigatePrev();
                else if (event.wheelDelta < 0) NavigateNext();
                return true;
            }

            case UCEventType::KeyDown:
                return HandleKeyDown(event);

            default:
                return false;
        }
    }

// =====================================================================
//  UltraCanvasDatePicker
// =====================================================================

    UltraCanvasDatePicker::UltraCanvasDatePicker(const std::string& identifier,
                                                 float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        calendar = std::make_shared<UltraCanvasCalendarView>(identifier + "_calendar", 0, 0, 0, 0);
        calendar->SetStyle(style.calendarStyle);

        // Wire calendar -> picker.
        calendar->onDateActivated = [this](const UCDate& d) {
            SetValueFromCalendarSingle(d);
            CloseCalendar();
        };
        calendar->onDateSelected = [this](const UCDate& d) {
            // Live update / clear (does not close the popup).
            if (calendar->GetSelectionMode() == DateSelectionMode::Single) {
                SyncTextFromValue();
                if (d.IsEmpty() && onDateChanged) onDateChanged(UCDate());
                RequestRedraw();
            }
        };
        calendar->onRangeSelected = [this](const UCDate& s, const UCDate& e) {
            SyncTextFromValue();
            if (onRangeChanged) onRangeChanged(s, e);
            RequestRedraw();
            CloseCalendar();
        };
        calendar->onMultipleChanged = [this](const std::vector<UCDate>&) {
            SyncTextFromValue();
            RequestRedraw();
        };

        SyncTextFromValue();
    }

    void UltraCanvasDatePicker::SetSelectionMode(DateSelectionMode mode) {
        calendar->SetSelectionMode(mode);
        // Free-text typing only makes sense for a single date.
        editing = false;
        SyncTextFromValue();
        RequestRedraw();
    }

    DateSelectionMode UltraCanvasDatePicker::GetSelectionMode() const {
        return calendar->GetSelectionMode();
    }

    void UltraCanvasDatePicker::SetSelectedDate(const UCDate& date, bool runCallbacks) {
        calendar->SetSelectedDate(date, false);
        SyncTextFromValue();
        if (runCallbacks && onDateChanged) onDateChanged(date);
        RequestRedraw();
    }

    UCDate UltraCanvasDatePicker::GetSelectedDate() const { return calendar->GetSelectedDate(); }

    void UltraCanvasDatePicker::SetRange(const UCDate& start, const UCDate& end, bool runCallbacks) {
        calendar->SetRange(start, end, false);
        SyncTextFromValue();
        if (runCallbacks && onRangeChanged) onRangeChanged(calendar->GetRangeStart(), calendar->GetRangeEnd());
        RequestRedraw();
    }

    UCDate UltraCanvasDatePicker::GetRangeStart() const { return calendar->GetRangeStart(); }
    UCDate UltraCanvasDatePicker::GetRangeEnd() const { return calendar->GetRangeEnd(); }

    void UltraCanvasDatePicker::Clear(bool runCallbacks) {
        calendar->ClearSelection(false);
        editBuffer.clear();
        caretPos = 0;
        SyncTextFromValue();
        if (runCallbacks && onDateChanged) onDateChanged(UCDate());
        RequestRedraw();
    }

    void UltraCanvasDatePicker::SetAllowTextInput(bool allow) {
        allowTextInput = allow;
        if (!allow) editing = false;
        RequestRedraw();
    }

    void UltraCanvasDatePicker::SetStyle(const DatePickerStyle& s) {
        style = s;
        calendar->SetStyle(style.calendarStyle);
        RequestRedraw();
    }

    void UltraCanvasDatePicker::SetWindow(UltraCanvasWindowBase* win) {
        UltraCanvasUIElement::SetWindow(win);
        if (calendar) calendar->SetWindow(win);
    }

    std::string UltraCanvasDatePicker::BuildDisplayText() const {
        DateSelectionMode mode = calendar->GetSelectionMode();
        if (mode == DateSelectionMode::Single) {
            return calendar->GetSelectedDate().Format(dateFormat);
        }
        if (mode == DateSelectionMode::Range || mode == DateSelectionMode::Week) {
            UCDate s = calendar->GetRangeStart(), e = calendar->GetRangeEnd();
            if (s.IsValid() && e.IsValid()) return s.Format(dateFormat) + "  -  " + e.Format(dateFormat);
            if (s.IsValid()) return s.Format(dateFormat) + "  -  ...";
            return "";
        }
        // Multiple
        auto dates = calendar->GetSelectedDates();
        if (dates.empty()) return "";
        if (dates.size() == 1) return dates[0].Format(dateFormat);
        return std::to_string(dates.size()) + " dates selected";
    }

    void UltraCanvasDatePicker::SyncTextFromValue() {
        editBuffer = BuildDisplayText();
        caretPos = static_cast<int>(editBuffer.size());
    }

    void UltraCanvasDatePicker::CommitTextInput() {
        // Only meaningful for single-date mode.
        if (calendar->GetSelectionMode() != DateSelectionMode::Single) {
            SyncTextFromValue();
            return;
        }
        std::string trimmed = editBuffer;
        // strip surrounding whitespace
        size_t a = trimmed.find_first_not_of(" \t");
        size_t b = trimmed.find_last_not_of(" \t");
        trimmed = (a == std::string::npos) ? "" : trimmed.substr(a, b - a + 1);

        if (trimmed.empty()) {
            if (!calendar->GetSelectedDate().IsEmpty()) {
                calendar->ClearSelection(false);
                if (onDateChanged) onDateChanged(UCDate());
            }
            SyncTextFromValue();
            return;
        }

        UCDate parsed;
        if (UCDate::Parse(trimmed, dateFormat, parsed) && calendar->IsDateSelectable(parsed)) {
            calendar->SetSelectedDate(parsed, false);
            SyncTextFromValue();
            if (onDateChanged) onDateChanged(parsed);
        } else {
            // Revert to the last valid value.
            SyncTextFromValue();
        }
    }

    void UltraCanvasDatePicker::SetValueFromCalendarSingle(const UCDate& d) {
        SyncTextFromValue();
        if (onDateChanged) onDateChanged(d);
        RequestRedraw();
    }

    Rect2Df UltraCanvasDatePicker::ButtonRect() const {
        Rect2Df b = GetLocalBounds();
        return Rect2Df(b.Right() - style.buttonWidth, b.y, style.buttonWidth, b.height);
    }

    Rect2Df UltraCanvasDatePicker::TextRect() const {
        Rect2Df b = GetLocalBounds();
        return Rect2Df(b.x + style.paddingLeft, b.y,
                       b.width - style.paddingLeft - style.buttonWidth, b.height);
    }

    void UltraCanvasDatePicker::RenderCalendarIcon(IRenderContext* ctx, const Rect2Df& rect, const Color& color) {
        // Small calendar glyph centred in rect.
        float w = 14.0f, h = 14.0f;
        float x = rect.x + (rect.width - w) / 2.0f;
        float y = rect.y + (rect.height - h) / 2.0f;
        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(1.2f);
        // body
        ctx->DrawFilledRectangle(Rect2Df(x, y + 2, w, h - 2), Colors::Transparent, 1.2f, color, 1.5f);
        // top binding bar
        ctx->DrawLine(Point2Dd(x, y + 5), Point2Dd(x + w, y + 5));
        // hanger ticks
        ctx->DrawLine(Point2Dd(x + 3.5, y), Point2Dd(x + 3.5, y + 3));
        ctx->DrawLine(Point2Dd(x + w - 3.5, y), Point2Dd(x + w - 3.5, y + 3));
    }

    void UltraCanvasDatePicker::RenderField(IRenderContext* ctx) {
        Rect2Df b = GetLocalBounds();

        Color bg = style.backgroundColor;
        Color border = style.borderColor;
        if (IsDisabled()) bg = style.disabledColor;
        else if (IsHovered()) bg = style.hoverColor;
        if ((IsFocused() || calendarOpen)) border = style.focusBorderColor;

        ctx->DrawFilledRectangle(b, bg, style.borderWidth, border, style.cornerRadius);

        Rect2Df tr = TextRect();
        ctx->PushState();
        ctx->ClipRect(Rect2Dd(tr.x, tr.y, tr.width, tr.height));
        ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(style.fontSize);

        bool typing = editing && IsFocused() && allowTextInput &&
                      calendar->GetSelectionMode() == DateSelectionMode::Single;
        std::string shown = typing ? editBuffer : BuildDisplayText();

        if (shown.empty() && !typing) {
            ctx->SetTextPaint(style.placeholderColor);
            Size2Di ts = ctx->GetTextLineDimensions(placeholder);
            ctx->DrawText(placeholder, Point2Dd(tr.x, tr.y + (tr.height - ts.height) / 2.0f));
        } else {
            Color tc = IsDisabled() ? style.disabledTextColor : style.textColor;
            ctx->SetTextPaint(tc);
            Size2Di ts = ctx->GetTextLineDimensions(shown.empty() ? " " : shown);
            float textY = tr.y + (tr.height - ts.height) / 2.0f;
            if (!shown.empty())
                ctx->DrawText(shown, Point2Dd(tr.x, textY));

            // caret
            if (typing) {
                int cp = std::min(std::max(caretPos, 0), static_cast<int>(shown.size()));
                std::string upto = shown.substr(0, cp);
                int cx = upto.empty() ? 0 : ctx->GetTextLineDimensions(upto).width;
                ctx->SetStrokePaint(style.caretColor);
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawLine(Point2Dd(tr.x + cx, textY), Point2Dd(tr.x + cx, textY + ts.height));
            }
        }
        ctx->PopState();

        // Button + calendar icon
        Rect2Df btn = ButtonRect();
        ctx->SetStrokePaint(style.borderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Dd(btn.x, btn.y + 3), Point2Dd(btn.x, btn.Bottom() - 3));
        RenderCalendarIcon(ctx, btn, IsDisabled() ? style.disabledTextColor : style.iconColor);
    }

    void UltraCanvasDatePicker::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        RenderField(ctx);
    }

    Point2Df UltraCanvasDatePicker::CalculatePopupPosition() const {
        Point2Df globalPos = GetPositionInWindow();
        Size2Df fieldSize = const_cast<UltraCanvasDatePicker*>(this)->GetSize();

        float windowHeight = window ? window->GetHeight() : 9999.0f;
        float windowWidth = window ? window->GetWidth() : 9999.0f;

        float popupW = calendar->GetBounds().width;
        float popupH = calendar->GetBounds().height;

        float spaceBelow = windowHeight - (globalPos.y + fieldSize.height);
        float spaceAbove = globalPos.y;

        float py;
        if (popupH <= spaceBelow || spaceBelow >= spaceAbove)
            py = globalPos.y + fieldSize.height;   // below
        else
            py = globalPos.y - popupH;              // above

        float px = globalPos.x;
        if (px + popupW > windowWidth) px = std::max(0.0f, windowWidth - popupW);

        return Point2Df(px, py);
    }

    void UltraCanvasDatePicker::OpenCalendar() {
        if (isPopup || !window || calendarOpen) return;

        Size2Df pref = calendar->GetPreferredSize();
        calendar->SetElementSize(pref);

        Point2Df pos = CalculatePopupPosition();

        PopupElementSettings settings;
        settings.popupOwner = shared_from_this();
        settings.closeByEscapeKey = true;
        settings.closeByClickOutside = true;

        calendarOpen = true;
        window->OpenPopup(Point2Di(static_cast<int>(pos.x), static_cast<int>(pos.y)), *calendar, settings);

        calendar->onPopupClosed = [this](ClosePopupReason) {
            calendarOpen = false;
            if (onCalendarClosed) onCalendarClosed();
            RequestRedraw();
        };

        if (onCalendarOpened) onCalendarOpened();
        RequestRedraw();
    }

    void UltraCanvasDatePicker::CloseCalendar() {
        if (window && calendar && calendarOpen) {
            window->ClosePopup(*calendar);
        }
    }

    void UltraCanvasDatePicker::InsertText(const std::string& s) {
        if (caretPos < 0) caretPos = 0;
        if (caretPos > static_cast<int>(editBuffer.size())) caretPos = static_cast<int>(editBuffer.size());
        editBuffer.insert(static_cast<size_t>(caretPos), s);
        caretPos += static_cast<int>(s.size());
    }

    bool UltraCanvasDatePicker::HandleMouseDown(const UCEvent& event) {
        Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
        Rect2Df b = GetLocalBounds();
        if (!b.Contains(p)) return false;

        SetFocus(true);

        bool onButton = ButtonRect().Contains(p);
        bool canType = allowTextInput && calendar->GetSelectionMode() == DateSelectionMode::Single;

        if (onButton || !canType) {
            // Toggle the calendar.
            if (calendarOpen) CloseCalendar();
            else OpenCalendar();
        } else {
            // Begin text editing in the field.
            editing = true;
            if (BuildDisplayText().empty()) { editBuffer.clear(); caretPos = 0; }
            else SyncTextFromValue();
        }
        RequestRedraw();
        return true;
    }

    bool UltraCanvasDatePicker::HandleKeyDown(const UCEvent& event) {
        // Forward navigation keys to an open calendar.
        if (calendarOpen) {
            switch (event.virtualKey) {
                case UCKeys::Up: case UCKeys::Down: case UCKeys::Left: case UCKeys::Right:
                case UCKeys::PageUp: case UCKeys::PageDown: case UCKeys::Home: case UCKeys::End:
                case UCKeys::Return: case UCKeys::Space:
                    return calendar->OnEvent(event);
                case UCKeys::Escape:
                    CloseCalendar();
                    return true;
                default:
                    break;
            }
        } else {
            // Open with arrow keys when the field is focused and closed.
            if (event.virtualKey == UCKeys::Down || event.virtualKey == UCKeys::Up) {
                OpenCalendar();
                return true;
            }
        }

        bool canType = allowTextInput && calendar->GetSelectionMode() == DateSelectionMode::Single;
        if (!canType) return false;

        // Printable characters: digits and common date separators only.
        if (event.character != 0 && event.character >= 32 && event.character < 127) {
            char c = event.character;
            if (std::isdigit(static_cast<unsigned char>(c)) ||
                c == '/' || c == '-' || c == '.' || c == ' ') {
                editing = true;
                InsertText(std::string(1, c));
                RequestRedraw();
                return true;
            }
            return true; // swallow other printables
        }

        switch (event.virtualKey) {
            case UCKeys::Backspace:
                if (caretPos > 0) {
                    editBuffer.erase(static_cast<size_t>(caretPos - 1), 1);
                    caretPos--;
                    editing = true;
                    RequestRedraw();
                }
                return true;
            case UCKeys::Delete:
                if (caretPos < static_cast<int>(editBuffer.size())) {
                    editBuffer.erase(static_cast<size_t>(caretPos), 1);
                    editing = true;
                    RequestRedraw();
                }
                return true;
            case UCKeys::Left:
                if (caretPos > 0) { caretPos--; RequestRedraw(); }
                return true;
            case UCKeys::Right:
                if (caretPos < static_cast<int>(editBuffer.size())) { caretPos++; RequestRedraw(); }
                return true;
            case UCKeys::Home:
                caretPos = 0; RequestRedraw(); return true;
            case UCKeys::End:
                caretPos = static_cast<int>(editBuffer.size()); RequestRedraw(); return true;
            case UCKeys::Return:
                CommitTextInput();
                editing = false;
                RequestRedraw();
                return true;
            case UCKeys::Escape:
                SyncTextFromValue();
                editing = false;
                RequestRedraw();
                return true;
            default:
                return false;
        }
    }

    bool UltraCanvasDatePicker::OnEvent(const UCEvent& event) {
        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);
            case UCEventType::MouseMove: {
                Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
                SetMouseCursor(GetLocalBounds().Contains(p)
                               ? (ButtonRect().Contains(p) ? UCMouseCursor::Hand : UCMouseCursor::Text)
                               : UCMouseCursor::Default);
                return false;
            }
            case UCEventType::KeyDown:
                return HandleKeyDown(event);
            case UCEventType::FocusLost:
                if (editing) {
                    CommitTextInput();
                    editing = false;
                    RequestRedraw();
                }
                return false;
            default:
                return false;
        }
    }

    bool UltraCanvasDatePicker::SetFocus(bool focus) {
        bool r = UltraCanvasUIElement::SetFocus(focus);
        if (!focus && editing) {
            CommitTextInput();
            editing = false;
        }
        RequestRedraw();
        return r;
    }

// =====================================================================
//  UltraCanvasDateRangePicker
// =====================================================================

    UltraCanvasDateRangePicker::UltraCanvasDateRangePicker(const std::string& identifier,
                                                           float x, float y, float w, float h)
            : UltraCanvasContainer(identifier, x, y, w, h) {
        Rebuild();
    }

    std::vector<UCDate> UltraCanvasDateRangePicker::BlockedList() const {
        std::vector<UCDate> v;
        v.reserve(blockedOrdinals.size());
        for (long o : blockedOrdinals) v.push_back(UCDate::FromOrdinal(o));
        return v;
    }

    bool UltraCanvasDateRangePicker::BaseSelectable(const UCDate& d) const {
        if (!d.IsValid()) return false;
        if (minDate.IsValid() && d < minDate) return false;
        if (maxDate.IsValid() && d > maxDate) return false;
        if (blockedOrdinals.count(d.ToOrdinal()) > 0) return false;
        if (userPredicate && !userPredicate(d)) return false;
        return true;
    }

    bool UltraCanvasDateRangePicker::IntervalAllSelectable(const UCDate& a, const UCDate& b) const {
        long lo = a.ToOrdinal(), hi = b.ToOrdinal();
        if (hi < lo) std::swap(lo, hi);
        for (long o = lo; o <= hi; ++o)
            if (!BaseSelectable(UCDate::FromOrdinal(o))) return false;
        return true;
    }

    void UltraCanvasDateRangePicker::Rebuild() {
        ClearChildren();
        startPicker = endPicker = singlePicker = nullptr;
        sepLabel = nullptr;

        if (mode == DateRangePickerMode::TwoFields) {
            startPicker = std::make_shared<UltraCanvasDatePicker>(GetIdentifier() + "_start", 0, 0, 10, 10);
            startPicker->SetSelectionMode(DateSelectionMode::Single);
            startPicker->SetDateFormat(dateFormat);
            startPicker->SetPlaceholder(startLabel);
            startPicker->onDateChanged = [this](const UCDate& d) {
                if (d.IsEmpty()) {
                    startDate = UCDate();
                    if (onStartChanged) onStartChanged(UCDate());
                } else {
                    HandleStartChosen(d);
                }
            };

            endPicker = std::make_shared<UltraCanvasDatePicker>(GetIdentifier() + "_end", 0, 0, 10, 10);
            endPicker->SetSelectionMode(DateSelectionMode::Single);
            endPicker->SetDateFormat(dateFormat);
            endPicker->SetPlaceholder(endLabel);
            endPicker->onDateChanged = [this](const UCDate& d) {
                if (d.IsEmpty()) {
                    endDate = UCDate();
                    if (onEndChanged) onEndChanged(UCDate());
                } else {
                    HandleEndChosen(d);
                }
            };

            auto sep = std::make_shared<UltraCanvasLabel>(GetIdentifier() + "_sep", 0, 0, 10, 10);
            sep->SetText("\xE2\x86\x92"); // → (UTF-8)
            sep->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
            sep->SetTextColor(Color(120, 120, 120, 255));
            sepLabel = sep;

            AddChild(startPicker);
            AddChild(sepLabel);
            AddChild(endPicker);
        } else {
            singlePicker = std::make_shared<UltraCanvasDatePicker>(GetIdentifier() + "_single", 0, 0, 10, 10);
            singlePicker->SetSelectionMode(DateSelectionMode::Range);
            singlePicker->SetDateFormat(dateFormat);
            singlePicker->SetPlaceholder(startLabel + " - " + endLabel);
            singlePicker->onRangeChanged = [this](const UCDate& s, const UCDate& e) {
                HandleSingleRange(s, e);
            };
            AddChild(singlePicker);
        }

        ApplyConstraintsToCalendars();
        LayoutFields();

        // Restore current values into the freshly built fields.
        if (startPicker && startDate.IsValid()) startPicker->SetSelectedDate(startDate, false);
        if (endPicker && endDate.IsValid()) endPicker->SetSelectedDate(endDate, false);
        if (singlePicker && startDate.IsValid() && endDate.IsValid())
            singlePicker->SetRange(startDate, endDate, false);
    }

    void UltraCanvasDateRangePicker::LayoutFields() {
        float w = GetWidth();
        float h = GetHeight();
        if (w <= 0) w = 320;
        if (h <= 0) h = 28;

        if (mode == DateRangePickerMode::TwoFields) {
            float sepW = 26.0f;
            float fieldW = (w - sepW) / 2.0f;
            if (fieldW < 1.0f) fieldW = 1.0f;
            if (startPicker) {
                startPicker->SetElementSize(Size2Df(fieldW, h));
                startPicker->SetElementAbsolutePosition(Point2Df(0, 0));
            }
            if (sepLabel) {
                sepLabel->SetElementSize(Size2Df(sepW, h));
                sepLabel->SetElementAbsolutePosition(Point2Df(fieldW, 0));
            }
            if (endPicker) {
                endPicker->SetElementSize(Size2Df(fieldW, h));
                endPicker->SetElementAbsolutePosition(Point2Df(fieldW + sepW, 0));
            }
        } else if (singlePicker) {
            singlePicker->SetElementSize(Size2Df(w, h));
            singlePicker->SetElementAbsolutePosition(Point2Df(0, 0));
        }
    }

    void UltraCanvasDateRangePicker::ApplyConstraintsToCalendars() {
        std::vector<UCDate> blocked = BlockedList();

        if (singlePicker) {
            singlePicker->SetMinDate(minDate);
            singlePicker->SetMaxDate(maxDate);
            singlePicker->SetFirstDayOfWeek(firstDayOfWeek);
            singlePicker->SetBlockedDates(blocked);
            singlePicker->SetDateEnabledPredicate(userPredicate ? userPredicate : std::function<bool(const UCDate&)>());
        }
        if (startPicker) {
            startPicker->SetMinDate(minDate);
            startPicker->SetMaxDate(maxDate);
            startPicker->SetFirstDayOfWeek(firstDayOfWeek);
            startPicker->SetBlockedDates(blocked);
            startPicker->SetDateEnabledPredicate(userPredicate ? userPredicate : std::function<bool(const UCDate&)>());
        }
        if (endPicker) {
            endPicker->SetMaxDate(maxDate);
            endPicker->SetFirstDayOfWeek(firstDayOfWeek);
            endPicker->SetBlockedDates(blocked);
            // End candidate is valid only if the whole stay [start, end] is free
            // and within the nights bounds.
            endPicker->SetDateEnabledPredicate([this](const UCDate& e) {
                if (!BaseSelectable(e)) return false;
                if (!startDate.IsValid()) return true;
                long nights = e.ToOrdinal() - startDate.ToOrdinal();
                if (nights < minNights) return false;
                if (maxNights > 0 && nights > maxNights) return false;
                return IntervalAllSelectable(startDate, e);
            });
            if (startDate.IsValid())
                endPicker->SetMinDate(startDate.AddDays(minNights > 0 ? minNights : 0));
            else
                endPicker->SetMinDate(minDate);
        }
    }

    void UltraCanvasDateRangePicker::HandleStartChosen(const UCDate& d) {
        startDate = d;

        // Invalidate an end that no longer forms a legal stay.
        if (endDate.IsValid()) {
            long nights = endDate.ToOrdinal() - startDate.ToOrdinal();
            bool ok = nights >= minNights &&
                      (maxNights <= 0 || nights <= maxNights) &&
                      IntervalAllSelectable(startDate, endDate);
            if (!ok) {
                endDate = UCDate();
                if (endPicker) endPicker->Clear(false);
            }
        }

        // Refresh the end field's constraints and point it near the start.
        ApplyConstraintsToCalendars();
        if (endPicker) endPicker->GetCalendar()->SetVisibleMonth(d.year, d.month);

        if (onStartChanged) onStartChanged(d);
        FireRangeIfComplete();
    }

    void UltraCanvasDateRangePicker::HandleEndChosen(const UCDate& d) {
        endDate = d;
        if (onEndChanged) onEndChanged(d);
        FireRangeIfComplete();
    }

    void UltraCanvasDateRangePicker::HandleSingleRange(const UCDate& s, const UCDate& e) {
        startDate = s;
        endDate = e;
        if (onStartChanged) onStartChanged(s);
        if (onEndChanged) onEndChanged(e);
        FireRangeIfComplete();
    }

    void UltraCanvasDateRangePicker::FireRangeIfComplete() {
        if (startDate.IsValid() && endDate.IsValid() && onRangeChanged)
            onRangeChanged(startDate, endDate);
    }

    // ---- public configuration ---------------------------------------

    void UltraCanvasDateRangePicker::SetMode(DateRangePickerMode m) {
        if (m == mode && (startPicker || singlePicker)) return;
        mode = m;
        Rebuild();
        RequestRedraw();
    }

    void UltraCanvasDateRangePicker::SetStartDate(const UCDate& d, bool runCallbacks) {
        startDate = d;
        if (startPicker) startPicker->SetSelectedDate(d, false);
        if (singlePicker && endDate.IsValid()) singlePicker->SetRange(d, endDate, false);
        ApplyConstraintsToCalendars();
        if (runCallbacks && onStartChanged) onStartChanged(d);
        if (runCallbacks) FireRangeIfComplete();
        RequestRedraw();
    }

    void UltraCanvasDateRangePicker::SetEndDate(const UCDate& d, bool runCallbacks) {
        endDate = d;
        if (endPicker) endPicker->SetSelectedDate(d, false);
        if (singlePicker && startDate.IsValid()) singlePicker->SetRange(startDate, d, false);
        if (runCallbacks && onEndChanged) onEndChanged(d);
        if (runCallbacks) FireRangeIfComplete();
        RequestRedraw();
    }

    void UltraCanvasDateRangePicker::SetRange(const UCDate& start, const UCDate& end, bool runCallbacks) {
        startDate = start;
        endDate = end;
        if (startPicker) startPicker->SetSelectedDate(start, false);
        if (endPicker) endPicker->SetSelectedDate(end, false);
        if (singlePicker) singlePicker->SetRange(start, end, false);
        ApplyConstraintsToCalendars();
        if (runCallbacks) {
            if (onStartChanged) onStartChanged(start);
            if (onEndChanged) onEndChanged(end);
            FireRangeIfComplete();
        }
        RequestRedraw();
    }

    void UltraCanvasDateRangePicker::Clear(bool runCallbacks) {
        startDate = endDate = UCDate();
        if (startPicker) startPicker->Clear(false);
        if (endPicker) endPicker->Clear(false);
        if (singlePicker) singlePicker->Clear(false);
        ApplyConstraintsToCalendars();
        if (runCallbacks) {
            if (onStartChanged) onStartChanged(UCDate());
            if (onEndChanged) onEndChanged(UCDate());
        }
        RequestRedraw();
    }

    void UltraCanvasDateRangePicker::SetDateFormat(const std::string& fmt) {
        dateFormat = fmt;
        if (startPicker) startPicker->SetDateFormat(fmt);
        if (endPicker) endPicker->SetDateFormat(fmt);
        if (singlePicker) singlePicker->SetDateFormat(fmt);
        RequestRedraw();
    }

    void UltraCanvasDateRangePicker::SetFieldLabels(const std::string& startLbl, const std::string& endLbl) {
        startLabel = startLbl;
        endLabel = endLbl;
        if (startPicker) startPicker->SetPlaceholder(startLabel);
        if (endPicker) endPicker->SetPlaceholder(endLabel);
        if (singlePicker) singlePicker->SetPlaceholder(startLabel + " - " + endLabel);
        RequestRedraw();
    }

    void UltraCanvasDateRangePicker::SetMinNights(int n) { minNights = n < 0 ? 0 : n; ApplyConstraintsToCalendars(); }
    void UltraCanvasDateRangePicker::SetMaxNights(int n) { maxNights = n; ApplyConstraintsToCalendars(); }

    void UltraCanvasDateRangePicker::SetFirstDayOfWeek(int dow) {
        firstDayOfWeek = ((dow % 7) + 7) % 7;
        ApplyConstraintsToCalendars();
    }

    void UltraCanvasDateRangePicker::SetMinDate(const UCDate& d) { minDate = d; ApplyConstraintsToCalendars(); }
    void UltraCanvasDateRangePicker::SetMaxDate(const UCDate& d) { maxDate = d; ApplyConstraintsToCalendars(); }

    void UltraCanvasDateRangePicker::SetDateEnabledPredicate(std::function<bool(const UCDate&)> pred) {
        userPredicate = std::move(pred);
        ApplyConstraintsToCalendars();
    }

    void UltraCanvasDateRangePicker::SetBlockedDates(const std::vector<UCDate>& dates) {
        blockedOrdinals.clear();
        for (const auto& d : dates)
            if (d.IsValid()) blockedOrdinals.insert(d.ToOrdinal());
        ApplyConstraintsToCalendars();
        RequestRedraw();
    }

    void UltraCanvasDateRangePicker::AddBlockedDate(const UCDate& d) {
        if (d.IsValid()) {
            blockedOrdinals.insert(d.ToOrdinal());
            ApplyConstraintsToCalendars();
            RequestRedraw();
        }
    }

    void UltraCanvasDateRangePicker::BlockDateRange(const UCDate& start, const UCDate& end) {
        if (!start.IsValid() || !end.IsValid()) return;
        long a = start.ToOrdinal(), b = end.ToOrdinal();
        if (b < a) std::swap(a, b);
        for (long o = a; o <= b; ++o) blockedOrdinals.insert(o);
        ApplyConstraintsToCalendars();
        RequestRedraw();
    }

    void UltraCanvasDateRangePicker::ClearBlockedDates() {
        blockedOrdinals.clear();
        ApplyConstraintsToCalendars();
        RequestRedraw();
    }

    void UltraCanvasDateRangePicker::SetStyle(const DatePickerStyle& s) {
        style = s;
        if (startPicker) startPicker->SetStyle(s);
        if (endPicker) endPicker->SetStyle(s);
        if (singlePicker) singlePicker->SetStyle(s);
        RequestRedraw();
    }

    void UltraCanvasDateRangePicker::SetBounds(const Rect2Df& b) {
        UltraCanvasContainer::SetBounds(b);
        LayoutFields();
    }

} // namespace UltraCanvas
