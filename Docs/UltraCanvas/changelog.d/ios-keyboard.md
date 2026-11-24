- **iOS: the keyboards, host appearance and opening files.** Like the rest of
  the iOS backend, it has not yet been compiled or run.
  - **Software keyboard.** The root view is the editor (`UIKeyInput`), and
    it becomes first responder only while a text widget holds the caret
    (`UltraCanvasCaret::onTextEditingChanged`). A hide waits one loop turn,
    as on Android. Typed text arrives as committed text, and Return and
    backspace as key presses. Autocorrection is off, since there is no
    composition yet.
  - **Hardware keyboard.** Presses become key events: `UIKey` HID usages
    map to `UCKeys`, and the key's own text is used, so nothing types twice.
  - **Host appearance** comes from the trait collection: dark mode, contrast,
    Reduce Motion, the tint as the accent, and Dynamic Type as the text scale.
    A trait change refreshes it.
  - **Opening files.** `OpenFile` and `OpenMultipleFiles` show the document
    picker, which copies the documents into the sandbox so callers get plain
    paths.
