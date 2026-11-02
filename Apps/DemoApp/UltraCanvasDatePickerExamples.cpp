// Apps/DemoApp/UltraCanvasDatePickerExamples.cpp
// Demonstrates the UltraCanvas date-selection widgets and the different
// selection philosophies they support.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  section, every label and hint sized to its text
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasDatePicker.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDatePickerExamples() {
        auto page = DemoKit::CreatePage("DatePickerExamples", "UltraCanvas Date Picker & Calendar",
            "Two complementary widgets cover the common date-selection philosophies: "
            "UltraCanvasDatePicker (a compact, typeable field with a pop-up calendar) and "
            "UltraCanvasCalendarView (an always-visible calendar). Both support single, range, "
            "multiple and whole-week selection plus full keyboard control.");

        // ============================================================
        // SECTION 1: Dropdown date picker (single date) + text entry
        // ============================================================
        auto card1 = DemoKit::AddCard(page, "DPCard1", "1. Dropdown Date Picker (Single date, type or pick)");

        auto singleRow = DemoKit::AddRow(card1, "DP_SingleRow");
        singleRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto picker = CreateDatePicker("DP_Single", 0, 0, 200, 28);
        picker->SetSelectedDate(UCDate::Today());
        picker->SetDateFormat("yyyy-MM-dd");
        DemoKit::AddFixed(singleRow, picker);

        auto pickerResult = DemoKit::CreateText("DP_SingleResult",
                                                "Selected: " + UCDate::Today().Format("yyyy-MM-dd"));
        picker->onDateChanged = [pickerResult](const UCDate& d) {
            pickerResult->SetText(d.IsEmpty() ? "Selected: (none)" : "Selected: " + d.Format("yyyy-MM-dd"));
        };
        DemoKit::Add(singleRow, pickerResult, 1);

        // A second picker with a day-month-year (European) format and Monday start.
        auto euRow = DemoKit::AddRow(card1, "DP_EURow");
        euRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto pickerEU = CreateDatePicker("DP_EU", 0, 0, 200, 28);
        pickerEU->SetDateFormat("dd/MM/yyyy");
        pickerEU->SetFirstDayOfWeek(1); // Monday
        pickerEU->SetPlaceholder("dd/mm/yyyy");
        DemoKit::AddFixed(euRow, pickerEU);
        DemoKit::Add(euRow, DemoKit::CreateCaption("DP_EUHint",
            "European format (dd/MM/yyyy), week starts Monday. Type a date and press Enter, "
            "or click the calendar button."), 1);

        // ============================================================
        // SECTION 2: Inline calendar (single)
        // ============================================================
        auto card2 = DemoKit::AddCard(page, "DPCard2", "2. Inline Calendar");
        auto inlineRow = DemoKit::AddRow(card2, "Cal_InlineRow", 30);

        auto inlineCal = CreateCalendarView("Cal_Inline", 0, 0);
        inlineCal->SetSelectedDate(UCDate::Today());
        DemoKit::AddFixed(inlineRow, inlineCal);

        // The hint and the readout beside the calendar take the rest of the width.
        auto inlineInfo = DemoKit::CreateColumn("Cal_InlineInfo", 12);
        DemoKit::AddCaption(inlineInfo, "Cal_InlineHint",
            "An always-on-screen calendar. Click the month/year title to drill up to "
            "month and decade views for fast navigation. Mouse wheel changes month.");
        auto inlineResult = DemoKit::AddText(inlineInfo, "Cal_InlineResult",
                                             "Selected: " + UCDate::Today().Format("yyyy-MM-dd"));
        inlineCal->onDateSelected = [inlineResult](const UCDate& d) {
            inlineResult->SetText(d.IsEmpty() ? "Selected: (none)" : "Selected: " + d.Format("yyyy-MM-dd"));
        };
        DemoKit::Add(inlineRow, inlineInfo, 1);

        // ============================================================
        // SECTION 3: Range + Week selection (inline calendars)
        // ============================================================
        auto card3 = DemoKit::AddCard(page, "DPCard3", "3. Range and Week Selection");
        auto rangeRow = DemoKit::AddRow(card3, "Cal_RangeRow", 30);

        // Each readout sits above its calendar so it is never hidden behind
        // the (taller) calendar widget.
        auto rangeColumn = DemoKit::CreateColumn("Cal_RangeColumn", 6);
        auto rangeResult = DemoKit::AddText(rangeColumn, "Cal_RangeResult", "Range: click start, then end");
        auto rangeCal = CreateCalendarView("Cal_Range", 0, 0);
        rangeCal->SetSelectionMode(DateSelectionMode::Range);
        rangeCal->SetShowWeekNumbers(true);
        // The week-number column widens the calendar: size it for that.
        rangeCal->SetElementSize(rangeCal->GetPreferredSize());
        rangeCal->onRangeSelected = [rangeResult](const UCDate& s, const UCDate& e) {
            long days = e.ToOrdinal() - s.ToOrdinal() + 1;
            rangeResult->SetText("Range: " + s.Format("yyyy-MM-dd") + " to " + e.Format("yyyy-MM-dd") +
                                 "  (" + std::to_string(days) + " days)");
        };
        DemoKit::AddFixed(rangeColumn, rangeCal);
        DemoKit::Add(rangeRow, rangeColumn);

        auto weekColumn = DemoKit::CreateColumn("Cal_WeekColumn", 6);
        auto weekResult = DemoKit::AddText(weekColumn, "Cal_WeekResult", "Week: click any day");
        auto weekCal = CreateCalendarView("Cal_Week", 0, 0);
        weekCal->SetSelectionMode(DateSelectionMode::Week);
        weekCal->SetFirstDayOfWeek(1);
        weekCal->onRangeSelected = [weekResult](const UCDate& s, const UCDate& e) {
            weekResult->SetText("Week of " + s.Format("yyyy-MM-dd") + " - " + e.Format("yyyy-MM-dd"));
        };
        DemoKit::AddFixed(weekColumn, weekCal);
        DemoKit::Add(rangeRow, weekColumn);

        DemoKit::AddCaption(card3, "Cal_RangeHint",
            "Left: contiguous range with a live highlight band and ISO week numbers. "
            "Right: clicking any day selects its whole week (Monday-first).");

        // ============================================================
        // SECTION 4: Multiple dates + constraints + dark theme
        // ============================================================
        auto card4 = DemoKit::AddCard(page, "DPCard4", "4. Multiple Dates, Constraints & Theming");
        auto multiRow = DemoKit::AddRow(card4, "Cal_MultiRow", 30);

        // As in section 3, each readout sits above its calendar.
        // Multiple discrete dates
        auto multiColumn = DemoKit::CreateColumn("Cal_MultiColumn", 6);
        auto multiResult = DemoKit::AddText(multiColumn, "Cal_MultiResult", "Multiple: toggle individual days");
        auto multiCal = CreateCalendarView("Cal_Multiple", 0, 0);
        multiCal->SetSelectionMode(DateSelectionMode::Multiple);
        multiCal->onMultipleChanged = [multiResult](const std::vector<UCDate>& dates) {
            multiResult->SetText(std::to_string(dates.size()) + " date(s) selected");
        };
        DemoKit::AddFixed(multiColumn, multiCal);
        DemoKit::Add(multiRow, multiColumn);

        // Constrained + dark themed: only weekdays in the next 60 days selectable.
        auto darkColumn = DemoKit::CreateColumn("Cal_DarkColumn", 6);
        auto darkResult = DemoKit::AddText(darkColumn, "Cal_DarkResult", "Dark theme: weekdays, next 60 days only");
        auto darkCal = CreateCalendarView("Cal_Dark", 0, 0);
        darkCal->SetStyle(CalendarStyles::Dark());
        darkCal->SetElementSize(darkCal->GetPreferredSize());
        darkCal->SetMinDate(UCDate::Today());
        darkCal->SetMaxDate(UCDate::Today().AddDays(60));
        darkCal->SetDateEnabledPredicate([](const UCDate& d) {
            int dow = d.DayOfWeek();
            return dow != 0 && dow != 6; // weekdays only
        });
        darkCal->onDateSelected = [darkResult](const UCDate& d) {
            if (!d.IsEmpty()) darkResult->SetText("Booked: " + d.Format("yyyy-MM-dd"));
        };
        DemoKit::AddFixed(darkColumn, darkCal);
        DemoKit::Add(multiRow, darkColumn);

        DemoKit::AddCaption(card4, "Cal_MultiHint",
            "Left: a set of independent dates (click to toggle). Right: min/max bounds plus a "
            "per-date predicate disable weekends and dates outside a 60-day window; "
            "the dark theme shows full style customisation.");

        // ============================================================
        // SECTION 5: Range picking (hotel stay) + blocked dates
        // ============================================================
        auto card5 = DemoKit::AddCard(page, "DPCard5", "5. Date Range Picking (hotel stay) with blocked dates");

        // A block of already-booked, unavailable nights.
        UCDate today = UCDate::Today();
        std::vector<UCDate> booked;
        for (int i = 0; i < 3; ++i) booked.push_back(today.AddDays(6 + i));

        // Mode A: separate check-in / check-out fields.
        auto twoRow = DemoKit::AddRow(card5, "RangeTwoRow");
        twoRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto twoField = CreateDateRangePicker("RangeTwo", 0, 0, 320, 28, DateRangePickerMode::TwoFields);
        twoField->SetMinDate(today);
        twoField->SetMinNights(1);
        twoField->SetBlockedDates(booked);
        DemoKit::AddFixed(twoRow, twoField);
        auto twoResult = DemoKit::CreateText("RangeTwoResult", "Mode A: pick check-in, then check-out");
        twoField->onRangeChanged = [twoResult](const UCDate& s, const UCDate& e) {
            long nights = e.ToOrdinal() - s.ToOrdinal();
            twoResult->SetText("Stay: " + s.Format("yyyy-MM-dd") + " -> " + e.Format("yyyy-MM-dd") +
                               "  (" + std::to_string(nights) + " night(s))");
        };
        DemoKit::Add(twoRow, twoResult, 1);

        // Mode B: single field, both endpoints chosen in one calendar.
        auto oneRow = DemoKit::AddRow(card5, "RangeOneRow");
        oneRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto oneField = CreateDateRangePicker("RangeOne", 0, 0, 320, 28, DateRangePickerMode::SingleField);
        oneField->SetMinDate(today);
        oneField->SetBlockedDates(booked);
        // Hotel-style: the pop-up shows two months and scrolls.
        oneField->SetMonthsPerView(2);
        oneField->SetCalendarNavigationMode(CalendarNavMode::Scrolling);
        DemoKit::AddFixed(oneRow, oneField);
        auto oneResult = DemoKit::CreateText("RangeOneResult", "Mode B: click start then end in one calendar");
        oneField->onRangeChanged = [oneResult](const UCDate& s, const UCDate& e) {
            long nights = e.ToOrdinal() - s.ToOrdinal();
            oneResult->SetText("Stay: " + s.Format("yyyy-MM-dd") + " -> " + e.Format("yyyy-MM-dd") +
                               "  (" + std::to_string(nights) + " night(s))");
        };
        DemoKit::Add(oneRow, oneResult, 1);

        DemoKit::AddCaption(card5, "RangeHint",
            "The 3 struck-through days are blocked (already booked): they cannot be selected, and a "
            "stay cannot span them - the live highlight stops at the first unavailable night. "
            "Mode A uses separate check-in/check-out fields (the end field only allows legal "
            "check-outs); Mode B selects both endpoints in one process.");

        // ============================================================
        // SECTION 6: Multi-month blocks & scroll navigation
        // ============================================================
        auto card6 = DemoKit::AddCard(page, "DPCard6", "6. Multi-Month Blocks & Scroll Navigation");
        auto blockRow = DemoKit::AddRow(card6, "Cal_BlockRow", 30);

        // Two months side-by-side (horizontal), paged with the chevrons.
        auto hCal = std::make_shared<UltraCanvasCalendarView>("Cal_Horiz2", 0, 0, 0, 0);
        hCal->SetMonthsPerView(2);
        hCal->SetOrientation(CalendarOrientation::Horizontal);
        hCal->SetSelectionMode(DateSelectionMode::Range);
        hCal->SetElementSize(hCal->GetPreferredSize());
        DemoKit::AddFixed(blockRow, hCal);

        // Two months stacked (vertical) with a scrollbar instead of paging.
        auto vCal = std::make_shared<UltraCanvasCalendarView>("Cal_VertScroll", 0, 0, 0, 0);
        vCal->SetMonthsPerView(2);
        vCal->SetOrientation(CalendarOrientation::Vertical);
        vCal->SetNavigationMode(CalendarNavMode::Scrolling);
        vCal->SetElementSize(vCal->GetPreferredSize());
        DemoKit::AddFixed(blockRow, vCal);

        DemoKit::AddCaption(card6, "Cal_BlockHint",
            "Left: SetMonthsPerView(2) with Horizontal orientation - two months side by side, paged "
            "with the chevrons (range selection spans both). Right: the same two-month block in "
            "Vertical orientation with Scrolling navigation - a scrollbar (and mouse wheel) moves "
            "continuously through months; the chevrons still work too.");

        return page;
    }

} // namespace UltraCanvas
