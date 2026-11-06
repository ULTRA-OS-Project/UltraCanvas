- **UltraClipboard can be translated.** Every text in its window, menus,
  dialogs and messages goes through `Tr` (UltraLocalisation): the English
  catalogue is `media/locale/en/UltraClipboard.json` (126 entries; *Copy*,
  *Delete*, *Settings*, *Cancel* and the other common words come from the
  standard set, so they already show in every language it has). Counts are
  plurals (`TrN`): "1 entry", "2 entries", and the forms a language with more
  than two has. No translation of the application's own entries ships yet;
  a translator adds `media/locale/<lang>/UltraClipboard.json`. The entry
  rows' tooltips and meta line come from the framework's clipboard history
  view and are not covered here.
