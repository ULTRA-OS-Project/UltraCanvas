- **UltraCanvasTextArea: auto-completion.** `ShowAutoComplete(suggestions)`
  opens a list under the caret - it did nothing before. Up, Down and the page
  keys move in it, Enter or Tab (or a click) puts the suggestion in place of
  the word before the caret (`GetAutoCompletePrefix()`), Escape closes it;
  other keys edit as usual, so the host narrows the list as the word is typed.
  `IsAutoCompleteOpen()` says whether it is shown.
