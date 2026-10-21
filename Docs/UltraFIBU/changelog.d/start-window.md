- **The start window is the framework's start page.** Title, the reason
  this window, *Zuletzt geöffnet*, the open button, the setup form and the
  status line are laid out by `UltraCanvasStartPage`, the page UltraPress's
  start window is built on too; the form's rows (Firma, the start of the
  fiscal year, the chart of accounts, the note, the reason the button is
  greyed, the button) are placed by the page (`AddField`, `AddNote`,
  `AddButton`), a little taller than before, and the window has no
  coordinates of its own left. What UltraFIBU says and asks is unchanged.
- **The bookkeepings opened last, in the start window.** The program
  remembers the files it opened (eight at most, the last first, under
  `zuletzt` in `<settings folder>/UltraFIBU/settings.json`, the framework's
  `UltraCanvasAppSettings` and the place for the next preference too) and
  the start window offers the five newest as buttons under *Zuletzt
  geöffnet*, the whole path in a tooltip; a file that is gone, or holds no
  bookkeeping any more, is said so and dropped from the list.
- **The date field says *TT.MM.JJJJ* again.** The framework's date picker
  kept its own "Select a date" whatever the window set; fixed in the picker.
- **The start window has a headless test: `UltraFIBUStartTests`.**
  `StartFenster::SeiteBauen` builds the page without a window, and the
  test reads it back: the hint, *Zuletzt geöffnet* as buttons, a gone file
  said and dropped, an existing one handed over, the setup form refusing
  until the company and a first of a month are in, the page's height.
  Built where the UI library is, with no display. It and the engine tests
  run on macOS and Windows too, in a CI step of their own.
