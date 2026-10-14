- **Android shows the word being composed inline, and stops losing it.** A
  soft keyboard composes each word before committing it: the underlined word
  with suggestions, or a CJK reading waiting for its candidate. The Android
  backend kept that composition inside the keyboard. It now passes it on the
  way the Linux and Windows backends do:
  - An editable `UltraCanvasTextInput`, `UltraCanvasTextArea` or
    `UltraCanvasRichTextEdit` (any element whose `DrawsTextComposition()` is
    true) receives `TextComposition` events and shows the word at its caret
    while it is typed. Any other element still sees only the committed text.
  - A word was lost when the keyboard ended the composition without
    committing it, for example when it closed mid-word. Such a word is now
    kept as typed.
  - A tap while a word is being composed keeps that word as typed where it
    was, as Android's own text fields do. It no longer follows the caret into
    whatever the tap focused.
  - Backspacing into a word no longer types it a second time: the keyboard
    used to start composing that word again and commit all of it after the
    copy that was already there.
  - Emoji and other characters outside the Basic Multilingual Plane reached
    widgets as six bytes no font could draw, because JNI strings were read as
    JNI's modified UTF-8. They are now read as real UTF-8. This affected
    everything the Java side passes to C++: committed text, dialog answers,
    clipboard text and file names.
  - None of this has run on a device yet; like the rest of the Android
    backend it is compiled, not observed.
