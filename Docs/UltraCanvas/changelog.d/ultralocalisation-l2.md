- **UltraLocalisation, phase L2: the catalogues and the scripts.**
  `media/locale/en/Standard.json` is the English standard set - the
  framework's own `Tr` keys and the application vocabulary of the
  proposal's Appendix A, 311 entries - and `media/locale/en/base/` holds
  the sixteen base sets of Appendix B (Computer, Web, Audio, Image, Video,
  Text, Spreadsheet, Presentation, Planning, CAD, Banking, Games,
  Shopping, Fashion, Communication, Security; 1,216 entries), so a
  translation of a language is those files with the values in that
  language. The framework's own dialogs are wrapped in `Tr`: the modal
  dialogs' buttons and titles, the file dialog, the filer widget's menus
  and messages, the colour, date, time and new-document dialogs, the
  progress dialog, the spell-check menu, the media viewer; a title built
  from pieces became a `TrF` with placeholders, and the date picker's
  weekday and month names are context entries (`"month|May"`) until the
  locale's own calendar names arrive with phase L6.
  `scripts/extract_ui_strings.py` rewrites the English catalogues from the
  `Tr` sites (adding to the standard set, never removing; an application's
  file from its own sites with what it inherits listed),
  `scripts/check_locale_catalogues.py` is the CI gate over every
  translation (parses, no stale key, placeholders kept, the plural forms
  the language needs, no entry repeated from a set below, the English in
  sync; `locale-catalogues.yml`, a baseline that only shrinks), and
  `scripts/wrap_ui_strings.py` wraps a file's interface strings for
  review. The plural table gained Breton, Scottish Gaelic, Manx, Cornish,
  Tachelhit, Tamazight, Filipino, Colognian, the Sami languages, Hebrew's
  "many" and European Portuguese.
