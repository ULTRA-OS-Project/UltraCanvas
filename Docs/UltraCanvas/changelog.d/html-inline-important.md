- **HTML reader: an inline `!important` beats a style sheet's `!important`.**
  The resolver applied a whole `style` attribute at once and then every
  `!important` rule of the style sheets on top, so
  `style="color: #ffffff !important"` lost to
  `a.intercom-content-link { color: #FF4554 !important }`. CSS orders the
  cascade the other way round (Cascading 4, 6.1: normal rules, inline,
  `!important` rules, inline `!important`). In a Lexware newsletter (Intercom)
  three red buttons came out empty - "Zum Artikel", "Anmelden" - their white
  text painted in the button's own red, and the footer's white links ("E-Mails
  abbestellen", "Kontakt") came out red on black. The inline `!important`
  declarations are now applied last; this also means a later normal
  declaration in the same `style` attribute no longer replaces an earlier
  `!important` one. `HTMLStyleResolver::Resolve`; test
  `TestInlineImportantBeatsSheetImportant` in `HTMLReaderTest.cpp`.
