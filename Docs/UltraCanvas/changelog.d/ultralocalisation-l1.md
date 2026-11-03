- **UltraLocalisation, phase L1: the localisation module.** `Tr("Save")`
  gives the translation of the English text written at the site, from
  three layers of JSON catalogues loaded at start-up - the application's
  own set (`media/locale/<lang>/<App>.json`), the base sets of its kind
  (`media/locale/<lang>/base/<Base>.json`, named in the catalogue's
  `@bases` or with `UseBaseSets`) and the standard set
  (`media/locale/<lang>/Standard.json`) - the English itself when none has
  it, so nothing is ever blank. A file of the same name under the
  UltraCanvas settings folder's `locale/` is read first, so a translator's
  draft shows on the next start; a regional tag walks down (`zh-Hant-TW`,
  `zh-Hant`, `zh`) and the more specific file wins per key. `TrC` keys on
  a context (`"button|Open"`), `TrN` picks the plural form by the
  language's CLDR rule from a table of forty languages, `TrF` fills `{0}`,
  `{1}`, .... `Language.conf` in the settings folder (`language=de` or
  `system`, `translator=`) is the one choice for every application;
  `Initialize` takes `--lang` (`SetLanguageOverride`), `ULTRACANVAS_LANG`,
  that file, then the first desktop language a catalogue exists for, then
  English, and `UltraCanvasApplicationBase::Initialize` runs it before the
  first window. The module is UI-free and free of the core (yyjson and the
  path helpers only), the `uc-localisation` archive absorbed into the
  shared core, public name `UltraLocalisation`, `Masterfile_modules.md`
  §18, `Docs/Modules/UltraLocalisation/README.md`;
  `UltraLocalisationTest` runs it over a temporary catalogue tree. The
  catalogues' contents (L2), the scripts, the editor (L4) and locale
  formatting (L6) follow; until L2 ships a catalogue every application
  shows its English as before.
