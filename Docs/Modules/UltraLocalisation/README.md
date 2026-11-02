# UltraLocalisation — The Text of Every Application in the Person's Language

<!-- doc-check: int count = 3; int done = 1; std::vector<std::string> systemLanguages; -->

**Status:** Phase L1 of [`UltraCanvasLocalisationProposal.md`](../../UltraCanvas/UltraCanvasLocalisationProposal.md) implemented: the module, the three layers of catalogues, the override folder, the fallback walk, plurals, `Language.conf`, the language choice, the start-up wiring. The standard and base sets' contents (L2), the scripts (L2), the editor (L4) and locale formatting (L6) follow.
**Version:** 1.0.0
**Author:** UltraCanvas Framework / ULTRA OS
**Last Modified:** 2026-11-02

`Tr("Save")` gives the translation of the English text written at the
site, in the language the person's desktop is set to, from three layers of
catalogues that the application's start-up loads for it. Nothing changes
in how an element stores text: `Tr` is a hash lookup when a label is
built, and the element keeps the string it is given.

- Header: `UltraCanvas/include/UltraLocalisation/UltraLocalisation.h`
- Implementation: `UltraCanvas/core/UltraLocalisation/UltraLocalisation.cpp`
- Build: the `uc-localisation` archive, absorbed whole into the shared core;
  linked by its public name `UltraLocalisation` (a headless engine links it
  alone, as it links UltraVault)
- Tests: `Tests/UltraLocalisationTest.cpp` (target `UltraLocalisationTest`, no display)
- Registry: `Masterfile_modules.md` §18
- Wired in: `UltraCanvasApplicationBase::Initialize` (the step "translation catalogues")

```cpp
std::string save = Tr("Save");                                    // "Speichern" on a German desktop
std::string open = TrC("button", "Open");                         // the "button|Open" entry: "Öffnen"
std::string files = TrN("{n} file", "{n} files", count);          // "3 Dateien": the language's plural rule
std::string progress = TrF("Deleted {0} of {1}", done, count);    // "1 von 3 gelöscht"
```

## The three layers

| Layer | File | Holds |
|---|---|---|
| The application's own set | `media/locale/<lang>/<App>.json` (`<App>` is the name given to `Initialize`) | what only this application says, and amendments of the layers below |
| The base sets of its kind | `media/locale/<lang>/base/<Base>.json` | the vocabulary of video editing, banking, shopping, ... (the proposal's Appendix B); the application names the ones it takes |
| The standard set | `media/locale/<lang>/Standard.json` | the framework's own dialogs and the words every application needs (Appendix A) |

A key is looked up in that order and the English itself is the answer
when no layer has it, so an untranslated string is always shown. A file
of the same name under `<settings>/locale/<lang>/` - the UltraCanvas
settings folder, `~/.config/UltraCanvas` on Linux - is read before the
shipped one for the same layer: that is where a translator drops a draft
and sees it on the next start, and where the editor will save on a
packaged build. A regional tag walks down, `zh-Hant-TW` → `zh-Hant` →
`zh`, and the more specific file wins per key.

An application names its base sets in its catalogue's metadata, most
specific first, or in code when it has no catalogue yet; a plugin with
strings of its own adds a library set, looked up after the application's
and before the bases:

```json
{ "@language": "en", "@bases": ["Video", "Audio", "Image"], "Fade out the audio": "Fade out the audio" }
```

```cpp
UltraLocalisation::UseBaseSets({"Video", "Audio"});     // instead of the catalogue's "@bases"
UltraLocalisation::AddSet("UltraCanvasCharts");          // a library's own set
std::vector<std::string> bases = UltraLocalisation::BaseSets();
LocalisationOrigin origin = UltraLocalisation::OriginOf("Trim");   // Application, Library, Base, Standard or Source
```

## The catalogue file

One JSON object per language and set. Keys are the source text, with a
context prefix when there is one; values are the translation; a plural
entry is an object keyed by CLDR category; entries starting with `@` are
metadata. `LocalisationCatalogue` reads and writes it:

```json
{
  "@language": "de",
  "@name": "Deutsch",
  "@translated-by": "UltraAI draft 2026-11-02, reviewed by -",
  "Save": "Speichern",
  "button|Open": "Öffnen",
  "Deleted {0} of {1}": "{0} von {1} gelöscht",
  "{n} file": { "one": "{n} Datei", "other": "{n} Dateien" }
}
```

```cpp
LocalisationCatalogue catalogue;
std::string error;
bool ok = UltraLocalisation::ReadCatalogue("media/locale/de/Standard.json", catalogue, error);
std::string text = catalogue.Serialize();                          // pretty, metadata first, keys in order
bool parsed = LocalisationCatalogue::Parse(text, catalogue, error);
```

`TrN` picks the category from the count with the language's rule -
`UltraLocalisation::PluralCategory("ru", 22)` is `"few"` - from a table of
the CLDR rules for some forty languages (the one-form East Asian
languages, French's zero-as-singular, the Slavic three and four forms,
Arabic's six, Irish, Welsh, Maltese, Latvian, Lithuanian, Romanian,
Slovenian, Hebrew). A language with one form keeps a plain string entry.
Placeholders are `{0}`, `{1}`, ... and `{n}`; a translation may reorder
them or leave one out. `TrF` formats integers as digits and floating
point dot-decimal; the locale's own number and date formatting is phase
L6.

## The language

`Initialize` chooses the language in this order, first answer wins, and
loads it:

1. `UltraLocalisation::SetLanguageOverride` - what an application parsed
   from `--lang=de`.
2. The `ULTRACANVAS_LANG` environment variable, for a script or a CI
   screenshot.
3. `Language.conf` in the UltraCanvas settings folder (`language=de`;
   `language=system` or no file means the next step), which UOS-Settings
   will write; `LanguageSettings` reads and writes it, with the
   `translator=` line the editor puts into a catalogue's `@translated-by`.
4. The desktop's languages, best first
   ([`UltraCanvasSystemLanguage.md`](../../UltraCanvas/UltraCanvasSystemLanguage.md)):
   the first one a catalogue exists for, each walked down (`de-CH` finds
   `de`).
5. `en`, which needs no catalogue.

```cpp
UltraLocalisation::SetFolders("/usr/share/ultracanvas/media/locale/", "/home/erika/.config/UltraCanvas");
UltraLocalisation::Initialize("UltraMail", systemLanguages);         // what the application base does
std::string language = UltraLocalisation::CurrentLanguage();          // "de"
std::vector<std::string> available = UltraLocalisation::AvailableLanguages();   // {"de", "en", "fr", ...}
std::string name = UltraLocalisation::NativeName("de");               // "Deutsch", from the standard set's "@name"
int handler = UltraLocalisation::AddChangeHandler([](const std::string& now) { (void)now; });
UltraLocalisation::SetLanguage("fr");                                 // reloads, then every handler runs
UltraLocalisation::RemoveChangeHandler(handler);
LanguageSettings settings = LanguageSettings::Load("/home/erika/.config/UltraCanvas");
settings.language = "de";
settings.Save("/home/erika/.config/UltraCanvas");
```

A language change reaches applications started after it; a running one
that wants to follow rebuilds what it built from strings in a change
handler, as the proposal's §7 says, since elements store text, not keys.

## Related

- [`UltraCanvasLocalisationProposal.md`](../../UltraCanvas/UltraCanvasLocalisationProposal.md) - the design, the standard set (Appendix A) and the base sets (Appendix B)
- [`UltraCanvasSystemLanguage.md`](../../UltraCanvas/UltraCanvasSystemLanguage.md) - the desktop's languages
- [`UltraCanvasEmojiSettings.md`](../../UltraCanvas/UltraCanvasEmojiSettings.md) - the shared-setting pattern `Language.conf` follows
