- **UltraSnip speaks the chosen language.** Every text it shows goes
  through `Tr` / `TrF` (UltraLocalisation): the bar's buttons, tooltips and
  delay list, the status line (its pieces - the size, "saved as", "copied"
  - as whole phrases with `{0}` for the name), the Save as dialogs' titles
  and filters, the error messages of the capture, the recorder and the
  selection overlay, the recording bar, and the whole settings window. Its
  English set is `media/locale/en/UltraSnip.json` (119 entries of its own,
  10 from the *Image* and *Video* base sets and the standard set); the
  language drafts do not carry it yet. *Translate UltraSnip...* on the
  settings window's start page opens the localisation editor on its own set.
  The command line's help stays English.
- **UltraSnip in thirty languages.** Machine drafts of its set
  (`media/locale/<lang>/UltraSnip.json`, marked `machine draft
  2026-11-04, reviewed by -`) for every language the standard set has:
  Arabic, Catalan, Chinese (Simplified and Traditional), Czech, Danish,
  Dutch, Finnish, French, German, Greek, Hebrew, Hindi, Hungarian,
  Indonesian, Italian, Japanese, Korean, Norwegian Bokmål, Polish,
  Portuguese (and Brazilian Portuguese, holding only what Brazil words
  differently), Romanian, Russian, Spanish, Swedish, Thai, Turkish,
  Ukrainian and Vietnamese. The words the standard and base sets already
  have (Settings, Save as, Clipboard, ...) are taken from them. The
  settings page *Saving* is `TrC("settings page", "Saving")`: the standard
  set's "Saving" is the progress word ("Wird gespeichert"), not a page's
  name. The delay list is as wide as its longest item in the language
  shown, and the status line says *saved* rather than *saved as <file>*
  (the whole path is its tooltip), so it fits the bar in every language.
