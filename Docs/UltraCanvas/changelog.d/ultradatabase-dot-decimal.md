- **UltraDatabase read and wrote PostgreSQL numbers in the desktop's
  locale.** On a comma-decimal desktop (German, French, …) a `float8` or
  `numeric` column holding `3.14` came back as `3`, and a `Text` value read
  with `AsDouble` did the same, because both went through `strtod`, which
  follows `LC_NUMERIC` - and every UltraCanvas GUI program on Linux has it
  set. Double parameters went the other way through `std::to_string`, which
  wrote `3,140000` (refused by the server) and kept six decimals, so `1e-9`
  was sent as zero. Numbers now cross the driver dot-decimal both ways,
  doubles are sent with enough digits to read back exactly, and PostgreSQL's
  `NaN` / `Infinity` / `-Infinity` still read. `UltraDbValue::AsString()` of
  a `Double` had the same fault - `std::to_string`, so `3,140000` on a
  German desktop and `0.000000` for `1e-9`; it now gives the shortest
  dot-decimal text that reads back exactly (`3.14`, `2`, `1e-09`). No caller
  in the tree read a floating-point column as a string. The two sites are off
  `scripts/locale_numbers_baseline.txt`; `Tests/UltraDatabase` checks it
  under `de_DE` against a live server.
