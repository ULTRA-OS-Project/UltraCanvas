- **Input method composition on macOS and in the browser.** An element that
  draws compositions itself (`DrawsTextComposition()`: an editable text
  input, text area or rich text editor) now gets the pre-edit text as `TextComposition` events on these two
  platforms too, as it already did on X11, Windows and Android, with the
  committed text arriving as a KeyDown with `text`.
  - macOS: the window's content view is an `NSTextInputClient`. While such an
    element has focus, key presses without Command or Control go through
    `interpretKeyEvents:`. Marked text becomes `TextComposition` (its caret
    is the end of the input method's selected range, converted from UTF-16
    to a UTF-8 byte offset), inserted text a KeyDown, and keys the input
    method does not turn into text (Return, Delete, the arrows) the same
    KeyDown as before. Dead keys (Option+E on a US layout) now compose
    in place. The candidate window opens at the caret. A click or the window
    losing the keyboard commits a composition in progress. Every other
    element keeps the plain key path, unchanged. The application registers
    `ApplePressAndHoldEnabled = NO` as a default, so holding a key repeats
    it instead of opening the accent menu, which would replace text already
    typed. A user's own setting still wins.
  - WebAssembly: a transparent `<textarea>` at the caret takes the keyboard
    while such an element has focus. Its `compositionupdate` events (and the
    caret from `selectionEnd`) become `TextComposition`, and
    `compositionend` commits the text. Keys the input method is processing
    (`keyCode` 229, or any key mid-composition) are no longer delivered as
    key presses too, and dead keys are let through so the browser composes
    the accent. Moving the keyboard between the canvas and the textarea is
    not reported as the window losing or gaining focus. Other elements keep
    the canvas and the old key path.
