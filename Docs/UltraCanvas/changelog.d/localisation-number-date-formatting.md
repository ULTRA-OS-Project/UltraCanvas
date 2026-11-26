- **Numbers, dates and times in the person's conventions (UltraLocalisation
  phase L6).** A German desktop read `1,234.5`, `10/21/2026` and `1.5 MB`
  in German text. `UltraLocalisation` now writes them as the region does:
  `FormatNumber` (`1.234,5`), `FormatInteger`, `FormatPercent` (`12,5 %`),
  `FormatFileSize` (`1,2 MB`, `512 Bytes`), `FormatDate` with
  `DateStyle::Short` / `Long` / `Full` (`21.10.2026`, `21. Oktober 2026`,
  `Mittwoch, 21. Oktober 2026`), `FormatTime` with `TimeStyle::Short` /
  `Medium` (`14:05`, `2:05 PM`), `FormatDateTime`, `FormatPattern` for any
  CLDR pattern, and `DecimalSeparator` for a numeric field.
  - The conventions are a `LocaleFormats`: the platform's regional settings
    where it has them - `SystemRegionalFormats()` (`UltraCanvasSystemLanguage.h`):
    `GetLocaleInfoEx` on Windows, `NSLocale` and `NSDateFormatter` on macOS
    and iOS, the person's own changes included, and on Linux and the BSDs
    the regions `LC_TIME` and `LC_NUMERIC` name - handed in by the
    application base at start-up. They apply to the language shown, or to
    any language the desktop chose; otherwise `FormatsFor` gives the
    built-in CLDR conventions of the language, for the thirty shipped and
    the common regional variants, its regional tag kept (an `en-GB` desktop
    loads the `en` catalogue and writes British dates).
  - The month and weekday names and the AM/PM words are the standard set's,
    in the person's language: every `Standard.json` gains
    `date-month|January` ... `date-month|December` (the month as written in
    a date - genitive in Russian, Polish, Czech, Ukrainian and Greek,
    partitive in Finnish), `date-weekday|Sunday` ... `date-weekday|Saturday`,
    `time|AM` and `time|PM`.
  - `TrF` writes a number argument in the language's digits and decimal
    separator (`2,5` in German, Arabic-Indic digits in Arabic) but does not
    group it, since an argument is as often a year or a port as a quantity;
    `TrN` groups its count (`1.234 Dateien`).
  - `FormatFileSize` (`UltraCanvasUtils.h`) and the filer widget's sizes are
    `UltraLocalisation::FormatFileSize`: one decimal, the unit names of the
    standard set, `512 bytes` rather than `512 B`.
  - `scripts/check_locale_numbers.py` points a person-read number at these
    functions.
  - DemoApp, *ULTRA OS modules > Localisation*: the numbers, dates and
    times in the language picked, and whose conventions they are.

  Covered in `UltraLocalisationTest` and `SystemLanguageTest`.
