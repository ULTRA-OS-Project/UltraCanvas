- **HTML reader: structural pseudo-classes.** `:first-child`, `:last-child`,
  `:only-child`, `:nth-child(an+b)` (with `odd` / `even`), `:nth-last-child()`,
  the `-of-type` forms, `:root` and `:empty` now match; a rule naming one was
  dropped before (Mailchimp's mobile padding rule uses `:last-child`). They
  count like a class in the cascade. Dynamic pseudo-classes (`:hover`,
  `:focus`, `:visited`) still drop the rule - a mail is never hovered.
  Test: `HTMLReaderTest` (`TestStructuralPseudoClasses`).
- **HTML reader: floats.** `float: left / right`, `<table align="left|right">`
  and `<img align="left|right">` (which browsers float) were ignored, so a
  mail template's two-column block - two 300px `<table align="left">` in a
  600px cell - stacked, and a picture's caption sat below it. A float now
  goes to its edge and the content after it flows beside it (CSSLayout
  floats, below); `clear` and `<br clear>` start below the floats. Test:
  `HTMLTableLayoutTest` ("floats sit side by side", "content flows around
  floats").
