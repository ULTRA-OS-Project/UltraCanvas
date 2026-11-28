- **Numbers as the person's language writes them.** `UltraLocalisation` has
  `DecimalSeparator`, `GroupSeparator`, `FormatNumber` and `ParseNumber`, the
  first part of the localisation proposal's §6. They use CLDR's symbols for
  the current language and its region, so German writes "1.234,5", French
  "1 234,5" (a narrow no-break space), de-CH "1’234.5" and Hindi "12,34,567".
  Spanish and Polish group only from 10 000. `ParseNumber` reads what a
  person types: either "." or "," as the decimal point on its own, and a
  grouping separator before three digits. A `TrF` floating-point argument
  now has the language's decimal separator. `UltraCanvasSpinner` shows a
  decimal value that way, and its "." and "," keys both put the language's
  decimal point in. The platform backends of §6 are still to come.
