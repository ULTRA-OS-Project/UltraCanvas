- **Typing no longer makes the spelling marks below the caret flicker.** Each
  letter typed in a line shifted every later word by a byte, and every red
  squiggle from the caret down disappeared until the background check came
  back, then reappeared: at every keystroke the rest of the document blinked.
  The marks now move with the text and stay where they are; only the word
  being typed loses its mark until it is checked again. The same holds in
  word-processing tabs (`.odt`, `.docx`, `.doc`).
- **The spelling language is detected automatically.** When more than 70% of
  the words in the document being edited are flagged, it is almost certainly
  written in another language than the dictionary in use. Texter then checks
  the text against the other installed dictionaries and switches to the one
  that flags the fewest words, provided it flags fewer than 10% - type a German
  text with English selected and the dictionary changes to German by itself.
  The choice is stored like one made from *Edit > Spelling > Dictionary*. Only
  the active tab switches the language, so two documents in two languages do
  not keep switching it back and forth; changing tab lets the dictionary follow
  the document on screen. On by default; *Edit > Spelling > Detect Language
  Automatically* turns it off.
