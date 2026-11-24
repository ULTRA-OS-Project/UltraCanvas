- **iOS: composing text.** CJK input methods and dead keys now compose on
  iOS. Not yet driven on a simulator.
  - **The editor.** The keyboard's editor view adopts `UITextInput` instead
    of `UIKeyInput`. Its document is only the text being composed (the
    marked text), counted in UTF-16 positions. The widget gets it as
    `TextComposition` events when it draws compositions, with the caret as
    a byte offset, and nothing until the commit when it does not (the
    Android rule).
  - **Committing.** `unmarkText` and lowering the keyboard keep the
    composition as typed. `insertText:` replaces it. Backspace inside a
    composition deletes in the composition.
  - **The candidate window** goes at the focused element's caret, found
    through its `IAccessibleText` as on macOS.
  - **A hardware keyboard.** While a widget edits text, the keys that type,
    plus Return and backspace, go through the text system, so an input
    method composes from a hardware keyboard too. Arrows, Escape, Tab,
    function keys and shortcuts stay key events.
  - **Autocorrection** stays off: the text it rewrites is the widget's.
