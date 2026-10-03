- **HTML reader: structural pseudo-classes.** `:first-child`, `:last-child`,
  `:only-child`, `:nth-child(an+b)` (with `odd` / `even`), `:nth-last-child()`,
  the `-of-type` forms, `:root` and `:empty` now match; a rule naming one was
  dropped before (Mailchimp's mobile padding rule uses `:last-child`). They
  count like a class in the cascade. Dynamic pseudo-classes (`:hover`,
  `:focus`, `:visited`) still drop the rule - a mail is never hovered.
  Test: `HTMLReaderTest` (`TestStructuralPseudoClasses`).
- **HTML reader: floats.** `float: left / right` and `<table align="left|right">`
  (which browsers float) were ignored, so a mail template's two-column block -
  two 300px `<table align="left">` in a 600px cell - stacked. Floats next to
  each other now share one wrapping row, left floats at its start and right
  floats at its end; a float too wide for the row goes to the next line.
  Approximation: what follows starts below the floats instead of flowing
  around them. Test: `HTMLTableLayoutTest` ("floats sit side by side").
