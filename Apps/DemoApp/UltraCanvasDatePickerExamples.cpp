// Apps/DemoApp/UltraCanvasDatePickerExamples.cpp
// Demonstrates the UltraCanvas date-selection widgets and the different
// selection philosophies they support.
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDatePicker.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    static std::shared_ptr<UltraCanvasLabel> DPSectionTitle(float x, float y, const std::string& text) {
        auto title = std::make_shared<UltraCanvasLabel>("DPSecTitle" + std::to_string((int)x) + "_" + std::to_string((int)y),
                                                        x, y, 500, 25);
        title->SetText(text);
        title->SetFontSize(14);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        return title;
    }

    static std::shared_ptr<UltraCanvasLabel> DPHint(float x, float y, float w, const std::string& text) {
        auto l = std::make_shared<UltraCanvasLabel>("DPHint" + std::to_string((int)x) + "_" + std::to_string((int)y),
                                                    x, y, w, 36);
        l->SetText(text);
        l->SetFontSize(12);
        l->SetWrap(TextWrap::WrapWord);
        l->SetTextColor(Color(90, 90, 90, 255));
        return l;
    }

    static std::shared_ptr<UltraCanvasContainer> DPSeparator(float x, float y, float w) {
        auto s = std::make_shared<UltraCanvasContainer>("DPSep" + std::to_string((int)y), x, y, w, 1);
        s->SetBackgroundColor(Color(220, 220, 220, 255));
        return s;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDatePickerExamples() {
        auto root = std::make_shared<UltraCanvasContainer>("DatePickerExamples", 0, 0, 1020, 1100);
        root->SetBackgroundColor(Colors::White);
        root->SetPadding(0, 0, 10, 0);

        float y = 14;

        auto title = std::make_shared<UltraCanvasLabel>("DPMainTitle", 20, y, 900, 30);
        title->SetText("UltraCanvas Date Picker & Calendar");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        root->AddChild(title);
        y += 34;

        root->AddChild(DPHint(20, y, 960,
            "Two complementary widgets cover the common date-selection philosophies: "
            "UltraCanvasDatePicker (a compact, typeable field with a pop-up calendar) and "
            "UltraCanvasCalendarView (an always-visible calendar). Both support single, range, "
            "multiple and whole-week selection plus full keyboard control."));
        y += 48;

        // ============================================================
        // SECTION 1: Dropdown date picker (single date) + text entry
        // ============================================================
        root->AddChild(DPSectionTitle(20, y, "1. Dropdown Date Picker (Single date, type or pick)"));
        y += 32;

        auto picker = CreateDatePicker("DP_Single", 30, y, 200, 28);
        picker->SetSelectedDate(UCDate::Today());
        picker->SetDateFormat("yyyy-MM-dd");

        auto pickerResult = std::make_shared<UltraCanvasLabel>("DP_SingleResult", 250, y + 4, 360, 20);
        pickerResult->SetText("Selected: " + UCDate::Today().Format("yyyy-MM-dd"));
        pickerResult->SetFontSize(12);
        picker->onDateChanged = [pickerResult](const UCDate& d) {
            pickerResult->SetText(d.IsEmpty() ? "Selected: (none)" : "Selected: " + d.Format("yyyy-MM-dd"));
        };
        root->AddChild(picker);
        root->AddChild(pickerResult);
        y += 34;

        // A second picker with a day-month-year (European) format and Monday start.
        auto pickerEU = CreateDatePicker("DP_EU", 30, y, 200, 28);
        pickerEU->SetDateFormat("dd/MM/yyyy");
        pickerEU->SetFirstDayOfWeek(1); // Monday
        pickerEU->SetPlaceholder("dd/mm/yyyy");
        root->AddChild(pickerEU);
        root->AddChild(DPHint(250, y - 4, 380,
            "European format (dd/MM/yyyy), week starts Monday. Type a date and press Enter, "
            "or click the calendar button."));
        y += 44;

        root->AddChild(DPSeparator(20, y, 960));
        y += 16;

        // ============================================================
        // SECTION 2: Inline calendar (single)
        // ============================================================
        root->AddChild(DPSectionTitle(20, y, "2. Inline Calendar"));
        y += 30;

        root->AddChild(DPHint(360, y, 280,
            "An always-on-screen calendar. Click the month/year title to drill up to "
            "month and decade views for fast navigation. Mouse wheel changes month."));

        auto inlineCal = CreateCalendarView("Cal_Inline", 30, y);
        inlineCal->SetSelectedDate(UCDate::Today());
        auto inlineResult = std::make_shared<UltraCanvasLabel>("Cal_InlineResult", 360, y + 50, 300, 20);
        inlineResult->SetText("Selected: " + UCDate::Today().Format("yyyy-MM-dd"));
        inlineResult->SetFontSize(12);
        inlineCal->onDateSelected = [inlineResult](const UCDate& d) {
            inlineResult->SetText(d.IsEmpty() ? "Selected: (none)" : "Selected: " + d.Format("yyyy-MM-dd"));
        };
        root->AddChild(inlineCal);
        root->AddChild(inlineResult);
        y += inlineCal->GetPreferredSize().height + 16;

        root->AddChild(DPSeparator(20, y, 960));
        y += 16;

        // ============================================================
        // SECTION 3: Range + Week selection (inline calendars)
        // ============================================================
        root->AddChild(DPSectionTitle(20, y, "3. Range and Week Selection"));
        y += 30;

        // Range calendar
        auto rangeCal = CreateCalendarView("Cal_Range", 30, y);
        rangeCal->SetSelectionMode(DateSelectionMode::Range);
        rangeCal->SetShowWeekNumbers(true);
        auto rangeResult = std::make_shared<UltraCanvasLabel>("Cal_RangeResult", 30, y - 2, 320, 20);
        rangeResult->SetText("Range: click start, then end");
        rangeResult->SetFontSize(12);
        rangeCal->onRangeSelected = [rangeResult](const UCDate& s, const UCDate& e) {
            long days = e.ToOrdinal() - s.ToOrdinal() + 1;
            rangeResult->SetText("Range: " + s.Format("yyyy-MM-dd") + " to " + e.Format("yyyy-MM-dd") +
                                 "  (" + std::to_string(days) + " days)");
        };

        // Week calendar
        float weekX = 30 + rangeCal->GetPreferredSize().width + 60;
        auto weekCal = CreateCalendarView("Cal_Week", weekX, y);
        weekCal->SetSelectionMode(DateSelectionMode::Week);
        weekCal->SetFirstDayOfWeek(1);
        auto weekResult = std::make_shared<UltraCanvasLabel>("Cal_WeekResult", weekX, y - 2, 320, 20);
        weekResult->SetText("Week: click any day");
        weekResult->SetFontSize(12);
        weekCal->onRangeSelected = [weekResult](const UCDate& s, const UCDate& e) {
            weekResult->SetText("Week of " + s.Format("yyyy-MM-dd") + " - " + e.Format("yyyy-MM-dd"));
        };

        root->AddChild(rangeResult);
        root->AddChild(weekResult);
        root->AddChild(rangeCal);
        root->AddChild(weekCal);
        y += rangeCal->GetPreferredSize().height + 20;

        root->AddChild(DPHint(30, y, 700,
            "Left: contiguous range with a live highlight band and ISO week numbers. "
            "Right: clicking any day selects its whole week (Monday-first)."));
        y += 40;

        root->AddChild(DPSeparator(20, y, 960));
        y += 16;

        // ============================================================
        // SECTION 4: Multiple dates + constraints + dark theme
        // ============================================================
        root->AddChild(DPSectionTitle(20, y, "4. Multiple Dates, Constraints & Theming"));
        y += 30;

        // Multiple discrete dates
        auto multiCal = CreateCalendarView("Cal_Multiple", 30, y);
        multiCal->SetSelectionMode(DateSelectionMode::Multiple);
        auto multiResult = std::make_shared<UltraCanvasLabel>("Cal_MultiResult", 30, y - 2, 320, 20);
        multiResult->SetText("Multiple: toggle individual days");
        multiResult->SetFontSize(12);
        multiCal->onMultipleChanged = [multiResult](const std::vector<UCDate>& dates) {
            multiResult->SetText(std::to_string(dates.size()) + " date(s) selected");
        };

        // Constrained + dark themed: only weekdays in the next 60 days selectable.
        float darkX = 30 + multiCal->GetPreferredSize().width + 60;
        auto darkCal = CreateCalendarView("Cal_Dark", darkX, y);
        darkCal->SetStyle(CalendarStyles::Dark());
        darkCal->SetMinDate(UCDate::Today());
        darkCal->SetMaxDate(UCDate::Today().AddDays(60));
        darkCal->SetDateEnabledPredicate([](const UCDate& d) {
            int dow = d.DayOfWeek();
            return dow != 0 && dow != 6; // weekdays only
        });
        auto darkResult = std::make_shared<UltraCanvasLabel>("Cal_DarkResult", darkX, y - 2, 340, 20);
        darkResult->SetText("Dark theme: weekdays, next 60 days only");
        darkResult->SetFontSize(12);
        darkCal->onDateSelected = [darkResult](const UCDate& d) {
            if (!d.IsEmpty()) darkResult->SetText("Booked: " + d.Format("yyyy-MM-dd"));
        };

        root->AddChild(multiResult);
        root->AddChild(darkResult);
        root->AddChild(multiCal);
        root->AddChild(darkCal);
        y += multiCal->GetPreferredSize().height + 20;

        root->AddChild(DPHint(30, y, 760,
            "Left: a set of independent dates (click to toggle). Right: min/max bounds plus a "
            "per-date predicate disable weekends and dates outside a 60-day window; "
            "the dark theme shows full style customisation."));
        y += 44;

        root->AddChild(DPSeparator(20, y, 960));
        y += 16;

        // ============================================================
        // SECTION 5: Range picking (hotel stay) + blocked dates
        // ============================================================
        root->AddChild(DPSectionTitle(20, y, "5. Date Range Picking (hotel stay) with blocked dates"));
        y += 30;

        // A block of already-booked, unavailable nights.
        UCDate today = UCDate::Today();
        std::vector<UCDate> booked;
        for (int i = 0; i < 3; ++i) booked.push_back(today.AddDays(6 + i));

        // Mode A: separate check-in / check-out fields.
        auto twoField = CreateDateRangePicker("RangeTwo", 30, y, 320, 28, DateRangePickerMode::TwoFields);
        twoField->SetMinDate(today);
        twoField->SetMinNights(1);
        twoField->SetBlockedDates(booked);
        auto twoResult = std::make_shared<UltraCanvasLabel>("RangeTwoResult", 370, y + 4, 380, 20);
        twoResult->SetText("Mode A: pick check-in, then check-out");
        twoResult->SetFontSize(12);
        twoField->onRangeChanged = [twoResult](const UCDate& s, const UCDate& e) {
            long nights = e.ToOrdinal() - s.ToOrdinal();
            twoResult->SetText("Stay: " + s.Format("yyyy-MM-dd") + " -> " + e.Format("yyyy-MM-dd") +
                               "  (" + std::to_string(nights) + " night(s))");
        };
        root->AddChild(twoField);
        root->AddChild(twoResult);
        y += 40;

        // Mode B: single field, both endpoints chosen in one calendar.
        auto oneField = CreateDateRangePicker("RangeOne", 30, y, 320, 28, DateRangePickerMode::SingleField);
        oneField->SetMinDate(today);
        oneField->SetBlockedDates(booked);
        auto oneResult = std::make_shared<UltraCanvasLabel>("RangeOneResult", 370, y + 4, 380, 20);
        oneResult->SetText("Mode B: click start then end in one calendar");
        oneResult->SetFontSize(12);
        oneField->onRangeChanged = [oneResult](const UCDate& s, const UCDate& e) {
            long nights = e.ToOrdinal() - s.ToOrdinal();
            oneResult->SetText("Stay: " + s.Format("yyyy-MM-dd") + " -> " + e.Format("yyyy-MM-dd") +
                               "  (" + std::to_string(nights) + " night(s))");
        };
        root->AddChild(oneField);
        root->AddChild(oneResult);
        y += 40;

        root->AddChild(DPHint(30, y, 800,
            "The 3 struck-through days are blocked (already booked): they cannot be selected, and a "
            "stay cannot span them - the live highlight stops at the first unavailable night. "
            "Mode A uses separate check-in/check-out fields (the end field only allows legal "
            "check-outs); Mode B selects both endpoints in one process."));
        y += 52;

        root->AddChild(DPSeparator(20, y, 960));
        y += 16;

        // ============================================================
        // SECTION 6: Multi-month blocks & scroll navigation
        // ============================================================
        root->AddChild(DPSectionTitle(20, y, "6. Multi-Month Blocks & Scroll Navigation"));
        y += 30;

        // Two months side-by-side (horizontal), paged with the chevrons.
        auto hCal = std::make_shared<UltraCanvasCalendarView>("Cal_Horiz2", 30, y, 0, 0);
        hCal->SetMonthsPerView(2);
        hCal->SetOrientation(CalendarOrientation::Horizontal);
        hCal->SetSelectionMode(DateSelectionMode::Range);
        hCal->SetElementSize(hCal->GetPreferredSize());

        // Two months stacked (vertical) with a scrollbar instead of paging.
        float vX = 30 + hCal->GetPreferredSize().width + 50;
        auto vCal = std::make_shared<UltraCanvasCalendarView>("Cal_VertScroll", vX, y, 0, 0);
        vCal->SetMonthsPerView(2);
        vCal->SetOrientation(CalendarOrientation::Vertical);
        vCal->SetNavigationMode(CalendarNavMode::Scrolling);
        vCal->SetElementSize(vCal->GetPreferredSize());

        root->AddChild(hCal);
        root->AddChild(vCal);
        float blockH = std::max(hCal->GetPreferredSize().height, vCal->GetPreferredSize().height);
        y += blockH + 14;

        root->AddChild(DPHint(30, y, 820,
            "Left: SetMonthsPerView(2) with Horizontal orientation - two months side by side, paged "
            "with the chevrons (range selection spans both). Right: the same two-month block in "
            "Vertical orientation with Scrolling navigation - a scrollbar (and mouse wheel) moves "
            "continuously through months; the chevrons still work too."));
        y += 52;

        root->SetSize(1020, y + 20);
        return root;
    }

} // namespace UltraCanvas
