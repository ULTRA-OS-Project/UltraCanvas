# UltraCanvas Date Picker & Calendar

`UltraCanvasDatePicker.h` provides date-selection widgets for UltraCanvas. The
design brief was *good usability above all*, so this document first surveys the
common date-selection philosophies, explains the trade-offs, and then documents
the API that was chosen.

## 1. Selection philosophies surveyed

Picking a date sounds trivial but the "right" UI depends heavily on **what kind
of date** the user is entering. The classic usability research (Nielsen Norman
Group, GOV.UK Design System, Material Design) repeatedly lands on the same
conclusions:

| Philosophy | Strength | Weakness | Best for |
|---|---|---|---|
| **Free text field** (`yyyy-MM-dd`) | Fastest for someone who *knows* the date; fully keyboard-driven; tiny footprint | Ambiguous formats (is `03/04` Mar-4 or Apr-3?); needs forgiving parsing & validation | Birthdays, far-past/future dates |
| **Three dropdowns / spinners** (D, M, Y) | Unambiguous; no calendar logic; touch-friendly | Slow (3 interactions); poor for "which day-of-week?" | Birthdays, fixed forms |
| **Pop-up calendar grid** (dropdown) | Visual; great for near-term dates; shows weekdays & context | Hides behind a click; slow for distant dates unless navigation is good | Appointments, bookings |
| **Inline calendar** (always visible) | No discovery cost; ideal when the date *is* the task | Takes permanent screen space | Booking flows, dashboards |
| **Range pickers** (two endpoints) | Natural for "from–to" | Two-step mental model; needs live preview | Reports, hotel stays |

### Key usability findings baked into this widget

1. **Don't force one philosophy.** The single biggest usability win is letting
   the user *both* type a date *and* pick it from a calendar. Keyboard users get
   speed; mouse/touch users get visual context. The `UltraCanvasDatePicker`
   field is therefore editable by default and also opens a calendar.
2. **Make distant navigation cheap.** A calendar that only steps one month at a
   time is painful for "1987". The calendar supports **drill-up navigation**:
   click the title to go *Days → Months → Years (decade)* and back down. Mouse
   wheel steps the month.
3. **Forgiving parsing.** Typed input is split on any separator (`/`, `-`, `.`,
   space) and mapped onto the configured format's field order, so `2026/6/7`,
   `2026-06-07` and `7.6.26` all work for the appropriate format. Invalid input
   reverts to the last good value instead of throwing it away.
4. **Strong affordances & feedback.** Today is ringed, the selection is filled,
   hover is highlighted, out-of-range / disabled days are greyed and ignored,
   weekends are tinted, and a range shows a live highlight band while you drag
   the second endpoint.
5. **Full keyboard control / accessibility.** The day grid follows the
   WAI-ARIA date-picker pattern: arrows move by day/week, `PageUp`/`PageDown`
   move by month (with `Shift` = year), `Home`/`End` jump to the start/end of
   the week, and `Enter`/`Space` selects. `Esc` closes the pop-up.
6. **Localisation hooks.** First-day-of-week (Sunday vs Monday), weekday and
   month names, weekend days and an optional ISO week-number column are all
   configurable.

## 2. The two widgets

Rather than overloading one class with a "mode" switch, the philosophies are
split across two widgets with a shared calendar engine:

* **`UltraCanvasCalendarView`** — the reusable, always-visible calendar grid
  (the *inline* philosophy). Use it directly when the calendar should be
  permanently on screen.
* **`UltraCanvasDatePicker`** — a compact, editable field with a calendar button
  that pops up an `UltraCanvasCalendarView` (the *dropdown* philosophy, combined
  with free-text entry).

Both support all four selection modes via `DateSelectionMode`:
`Single`, `Range`, `Multiple`, `Week`.

## 3. `UCDate`

A lightweight, dependency-free date value (`{year, month, day}`, no time):

```cpp
UCDate d(2026, 6, 13);
d.DayOfWeek();              // 0=Sun..6=Sat  (Sakamoto's algorithm)
d.AddDays(30);             // proleptic-Gregorian day arithmetic
d.AddMonths(-2);           // clamps day to the target month length
d.ISOWeek();               // ISO-8601 week number
d.Format("dd/MM/yyyy");    // "13/06/2026"
UCDate out;
UCDate::Parse("13.6.26", "dd-MM-yy", out);   // forgiving parse
UCDate::Today();
```

Date arithmetic uses Howard Hinnant's *days-from-civil* algorithm, so it is
correct across leap years and century boundaries (verified by round-trip tests
over ±1000 years).

## 4. `UltraCanvasDatePicker` (dropdown + text entry)

```cpp
auto picker = CreateDatePicker("dueDate", 30, 30, 200, 28);
picker->SetDateFormat("yyyy-MM-dd");
picker->SetSelectedDate(UCDate::Today());
picker->SetMinDate(UCDate::Today());          // no past dates
picker->onDateChanged = [](const UCDate& d) {
    /* d.IsEmpty() when cleared */
};
```

Useful methods:

| Method | Purpose |
|---|---|
| `SetSelectionMode(mode)` | `Single` (typeable) / `Range` / `Multiple` / `Week` |
| `SetDateFormat(fmt)` | Display & parse format (`yyyy MM dd` tokens) |
| `SetAllowTextInput(bool)` | Toggle free-text entry (default on for `Single`) |
| `SetPlaceholder(text)` | Empty-state hint |
| `SetMinDate/SetMaxDate` | Hard bounds |
| `SetDateEnabledPredicate(fn)` | Arbitrary per-date enable/disable |
| `SetFirstDayOfWeek(0..6)` | 0=Sunday (default), 1=Monday |
| `GetCalendar()` | Access the underlying `UltraCanvasCalendarView` |

Interaction: click the field to type, click the button (or press ↑/↓) to open
the calendar; selecting a single date commits and closes; completing a range
commits and closes; `Esc` cancels.

## 5. `UltraCanvasCalendarView` (inline)

```cpp
auto cal = CreateCalendarView("cal", 30, 30);   // auto-sized to its style
cal->SetSelectionMode(DateSelectionMode::Range);
cal->SetShowWeekNumbers(true);
cal->onRangeSelected = [](const UCDate& s, const UCDate& e) { /* ... */ };
```

Selection callbacks:

* `onDateSelected(UCDate)` — Single/Multiple (empty `UCDate` ⇒ cleared)
* `onRangeSelected(UCDate start, UCDate end)` — Range/Week, span completed
* `onMultipleChanged(vector<UCDate>)` — Multiple, full set after each toggle
* `onDateActivated(UCDate)` — commit gesture (the date picker uses this to close)
* `onVisibleMonthChanged(year, month)`

Appearance: `SetStyle(CalendarStyle)` with predefined `CalendarStyles::Default()`,
`CalendarStyles::Compact()` and `CalendarStyles::Dark()`; or tweak any colour /
metric on `CalendarStyle` directly. `SetShowFooter`, `SetShowAdjacentMonthDays`,
`SetWeekdayNames`, `SetMonthNames`, `SetWeekendDays` cover the rest.

## 6. Date range picking & blocked dates

Ranges (a hotel stay, a report period) are entered through
`UltraCanvasDateRangePicker`, which offers the **two interaction philosophies**
users expect:

* `DateRangePickerMode::TwoFields` — separate **check-in** and **check-out**
  fields, each with its own pop-up calendar. The endpoints are entered with
  separate gestures; the end field only offers legal check-outs.
* `DateRangePickerMode::SingleField` — one field whose calendar is in `Range`
  mode, so the start and end are chosen **in one process** (click start, click
  end, with a live highlight band in between).

```cpp
auto stay = CreateDateRangePicker("stay", 30, 30, 320, 28,
                                  DateRangePickerMode::TwoFields);
stay->SetMinDate(UCDate::Today());      // no past dates
stay->SetMinNights(1);                  // check-out strictly after check-in
stay->SetBlockedDates(alreadyBooked);   // unavailable nights
stay->onRangeChanged = [](const UCDate& in, const UCDate& out) { /* ... */ };
```

`SetMinNights` / `SetMaxNights` bound the stay length; `onStartChanged`,
`onEndChanged` and `onRangeChanged` report progress. The two modes are
interchangeable at runtime via `SetMode`.

### Blocking dates from selection

Every calendar supports an explicit set of **blocked / unavailable dates**, in
addition to `SetMinDate`/`SetMaxDate` and the arbitrary `SetDateEnabledPredicate`:

```cpp
calendar->SetBlockedDates({ ... });          // replace the whole set
calendar->AddBlockedDate(UCDate(2026, 7, 4));
calendar->BlockDateRange(start, end);        // e.g. an existing reservation
calendar->ClearBlockedDates();
```

Blocked dates are **struck through**, cannot be selected, and — the key rule
for stays — **a range may not stretch across a blocked date**. While the user
drags the second endpoint the highlight is capped at the last available day
before the first blocked night (`ClampRangeEnd`); clicking past a blocked date
simply starts a new range. The same blocking API is exposed on
`UltraCanvasDatePicker` and `UltraCanvasDateRangePicker`, which propagate it to
their internal calendars. In the two-field mode the check-out field's
availability is derived from the chosen check-in so that the whole stay stays on
free nights.

## 7. Keyboard reference (day grid)

| Key | Action |
|---|---|
| ← / → | Previous / next day |
| ↑ / ↓ | Previous / next week |
| PageUp / PageDown | Previous / next month |
| Shift+PageUp / PageDown | Previous / next year |
| Home / End | First / last day of the focused week |
| Enter / Space | Select the focused date |
| Esc | Close the pop-up (date picker) |
| Mouse wheel | Step the visible month |

## 8. Demo

`Apps/DemoApp/UltraCanvasDatePickerExamples.cpp` (Basic UI → *Date Picker /
Calendar*) shows: dropdown picker with text entry, US & European formats, an
inline calendar, range + week calendars with week numbers, multiple-date
selection, a min/max + predicate-constrained dark-themed calendar, and a
hotel-style range section demonstrating both two-field and single-field range
picking with blocked (already-booked) dates.
