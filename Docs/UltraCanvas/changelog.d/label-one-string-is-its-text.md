- **A label built from one string that reads as a name is refused.**
  `UltraCanvasLabel`'s single-string constructor takes the label's text,
  where `UltraCanvasContainer`'s takes the identifier, so
  `make_shared<UltraCanvasLabel>("CountLabel")` is an unnamed label that says
  "CountLabel". Seven places had written it: the dialog's severity icon
  (`UltraCanvasModalDialog.cpp`), the demo's header title and three labels
  of its info window, and Texter's status bar and search counter. None
  showed - each set its real text on the next line - but none could be found
  by name, which is how it was noticed: a test looked for the input dialog's
  "InputLabel" and found nothing. Each was one deleted `SetText` away from
  showing its own variable name to the user. All seven now pass the
  identifier and the text, `("CountLabel", "")`.
  `scripts/check_label_identifiers.py` (CI's `label-identifiers.yml`, with
  tests of its own in `scripts/tests/`) reports a label or a checkbox - by
  `make_shared`, `make_unique`, `new`, a declaration, `CreateLabel` or
  `CreateCheckbox`, on one line or several - built from exactly one string
  literal that is a single token with a capital inside it, a digit, or
  parts joined by `-`, `_` or `.`. A plain word ("Ready") is taken for text,
  and so is anything through `Tr`. A one-string label whose text really is
  shaped like a name says so with `// label-text-ok: <why>`. There is no
  baseline: the tree is clean. `AGENTS.md`, the label's document and the
  two constructors' comments say which string is which.
