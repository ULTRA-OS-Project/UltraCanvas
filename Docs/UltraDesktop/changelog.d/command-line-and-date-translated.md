- **The command line answers in the person's language.** `--help`,
  `--windows`, `--apps`, `--devices`, `--screenshot` and the refusals of a
  wrong argument were printed before any language was loaded, so they were
  English everywhere. The language is loaded first now
  (`UltraCanvasApplicationBase::LoadTranslations`) and the texts go through
  `Tr`. `--devices` prints `label: value` lines without padding the labels
  into a column, since their length differs by language.
- **The clock's date is written as the language writes it**: the order,
  the punctuation and the month's form inside a date come from the
  catalogue (`TrLongDate`, `TrDayMonth`), and the line starts with a
  capital. It was "Day, D Month YYYY" in every language.
