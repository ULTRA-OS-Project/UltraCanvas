- **Hidden characters can be shown.** The editor's right-click menu has a
  *Display* submenu with *Hidden Characters*, *Line Numbers* and *Word Wrap*;
  *View > Hidden Characters* is the same switch. With it on, every space is
  marked with a dot, a tab with an arrow across its width, a no-break space
  with a small ring, a zero-width character (zero-width space and joiners,
  word joiner, soft hyphen, byte order mark) with a tick, and the end of every
  line with a pilcrow (¶) - so trailing spaces, tabs mixed with spaces and
  invisible characters pasted from the web can be seen. Resting the pointer
  on a mark names the character in a tooltip with its code, decimal value
  and UTF-8 bytes (*NO-BREAK SPACE, U+00A0, 160, C2 A0*); on a pilcrow it
  gives the file's line ending (LF, CRLF or CR). The text itself is
  unchanged. Applies to text tabs (word-processing tabs keep their own view)
  and is remembered with the other View settings. Framework:
  `UltraCanvasTextArea::SetShowHiddenCharacters`.
