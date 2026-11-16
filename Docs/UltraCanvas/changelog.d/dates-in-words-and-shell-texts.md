- **A date in words, in the language's own order.** `TrLongDate(weekday,
  day, month, year)` and `TrDayMonth(day, month)`
  (`UltraLocalisation/UltraLocalisation.h` 1.1.0) take the order and
  punctuation from a catalogue entry ("Monday, 16 November 2026", "Montag,
  16. November 2026", "2026年11月16日 月曜日"), the month from a form of its
  own for the inside of a date (`month-in-date|...`: "16 listopada" in
  Polish, where the month alone is "listopad"), and start the line with a
  capital where the language writes its weekdays small
  (`CapitaliseFirstLetter`: Latin with its accents, Greek, Cyrillic).
  `TrWeekdayName`, `TrMonthName`, `TrMonthNameInDate` and `TrMonthShortName`
  give the names alone. Translated in all thirty languages. Tests:
  `UltraLocalisationTest`. Docs: *Dates in words* in
  `Docs/Modules/UltraLocalisation/README.md`.
- **The wireless and desktop-shell layers answer in the person's
  language.** The reasons `UltraCanvasWireless` and `UltraCanvasDesktopShell`
  hand up when something fails - no adapter, a network out of reach, a
  wrong password, a device that would not pair, a screenshot that could not
  be written - were English literals in the core and in the Linux, Windows
  and macOS backends, shown as they were in a translated panel. They go
  through `Tr` / `TrF` now, the three backends in the same words where they
  mean the same thing, and are translated in all thirty languages. The
  backends that tell an absent adapter by its message compare with the
  translated text.
- **`UltraCanvasApplicationBase::LoadTranslations(application)`** loads the
  catalogues without a display or a window (`UltraCanvasApplication.h`
  1.20.0), for what a program prints to a terminal before `Initialize` -
  its `--help`, its command-line answers. `Initialize` uses it for its own
  step.
