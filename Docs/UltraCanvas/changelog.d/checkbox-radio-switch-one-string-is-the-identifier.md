- **The checkbox, the radio button and the switch take one string as their
  identifier too.** `UltraCanvasCheckbox("WrapLines")`,
  `UltraCanvasRadio("SizeLarge")` and `UltraCanvasSwitch("DarkMode")` name
  the element and leave its text empty; the one string was the text, as the
  label's was. With the label these were the four elements, of the 42 that
  have a one-string constructor, that did not take the identifier there.
  Nothing in this repository built any of the three from one string, so
  nothing here changes; an application outside it that wrote
  `UltraCanvasCheckbox(Tr("Wrap long lines"))` now gets a box without a
  caption, and writes `("WrapLines", Tr("Wrap long lines"))` or
  `("", Tr("Wrap long lines"))`.
  - `scripts/check_label_identifiers.py` reports all four, and reads the
    framework's headers as well: an element - any class that derives from
    `UltraCanvasUIElement`, 179 of them - whose explicit one-string
    constructor names its parameter as a text (`labelText`, `caption`,
    `title`) is refused, so the rule has no fifth exception.
  - `WarnIfIdentifierReadsAsText` covers the three, and
    `Tests/ElementIdentifierTest.cpp` pins them.
