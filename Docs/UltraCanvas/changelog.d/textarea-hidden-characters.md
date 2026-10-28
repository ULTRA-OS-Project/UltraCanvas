- **`UltraCanvasTextArea` can show the characters that print nothing.**
  `SetShowHiddenCharacters(true)` / `GetShowHiddenCharacters()` draw faint
  marks over the text, in the new `TextAreaStyle::hiddenCharacterColor`: a dot
  for a space (and U+3000), an arrow for a tab, a ring for a no-break space
  (U+00A0, U+202F), a tick for a zero-width character (U+200B-U+200D, U+2060,
  U+FEFF, U+00AD) and a pilcrow at the end of each line. Display only - the
  text, the caret and selection are untouched. Resting the pointer on a mark
  shows a tooltip with the character's name, `U+` code, decimal value and
  UTF-8 bytes; on a pilcrow, the document's line ending. The light, dark and theme-role
  presets set the colour. UltraTexter's *Hidden Characters* switch uses it.
