- **A label built from one string takes it as its identifier.**
  `UltraCanvasLabel("CountLabel")` and `CreateLabel("CountLabel")` make a
  label named CountLabel with no text; until now the one string was the
  label's *text* and the label had no identifier - the reverse of
  `UltraCanvasContainer` and of 38 of the 42 elements with a one-string
  constructor. It read like the others and meant the opposite, and eleven
  callers in this repository had handed it a name: the dialog's severity
  icon, Texter's status bar and search counter, four labels of the demo,
  three of the HTML builder (`MakeId(...)`), and the framework's own
  `LabelBuilder`, whose `LabelBuilder("greeting").Build()` therefore made an
  unnamed label that said "greeting". None showed, because each set its
  text on the next line, but none could be found by name - it was noticed
  when a test looked for the input dialog's "InputLabel" and found nothing -
  and each was one deleted `SetText` away from showing its own variable name.
  Those eleven are right as written now. One caller meant text (the demo's
  info window) and passes a name and the text.
  - **This changes what existing code means.** A label built from its text
    alone - `UltraCanvasLabel(Tr("Hint"))`, `CreateLabel("Two words")` -
    still compiles and now shows nothing. Write the text second:
    `("", Tr("Hint"))`, or `("HintLabel", Tr("Hint"))`. An application
    outside this repository needs the same look through its one-string
    labels.
  - `scripts/check_label_identifiers.py` (CI's `label-identifiers.yml`,
    with tests of its own in `scripts/tests/`) finds them: a label - by
    `make_shared`, `make_unique`, `new`, a declaration, a base-class
    initialiser or `CreateLabel`, on one line or several - built from
    exactly one argument that is a `Tr` / `TrF` / `TrN` / `TrC` call or a
    string literal that is not a name. It runs on any folder:
    `python3 scripts/check_label_identifiers.py path/to/your/app`. An
    identifier that really reads as text says so with
    `// label-identifier-ok: <why>`.
  - What the check cannot read - a variable - the framework reports at run
    time: `WarnIfIdentifierReadsAsText` (`UltraCanvasUIElement.h`) writes one
    line to `debugOutput` when a one-string element is given an identifier
    with a space or a line break in it, for the first twenty.
  - `AGENTS.md` (*Core conventions*) and the label's document state the
    rule.
