- **DemoApp: a Localisation page under ULTRA OS modules.** `Tr`, `TrC`,
  `TrN` and `TrF` answered live in a language picked on the page (every
  language a catalogue ships for, named as it names itself), each with the
  layer that answered it - the application's set, a base set, the standard
  set, or the English at the site; a count for the plural with its CLDR
  category, a value for `TrF`; the catalogue files of the three layers of
  UltraTexter in that language, shown as they ship with their entry counts,
  `@bases` and `@inherited`; the scripts and the editor around the module.
  The pick calls `UltraLocalisation::SetLanguage`, as UOS-Settings does.
