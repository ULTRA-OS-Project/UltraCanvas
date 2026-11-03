# UltraLocalisation — The Text of Every Application in the Person's Language

<!-- doc-check: int count = 3; int done = 1; std::vector<std::string> systemLanguages; -->

**Status:** Phases L1 to L3 of [`UltraCanvasLocalisationProposal.md`](../../UltraCanvas/UltraCanvasLocalisationProposal.md) implemented: the module, the three layers of catalogues, the override folder, the fallback walk, plurals, `Language.conf`, the language choice, the start-up wiring (L1); the English standard set and the sixteen base sets under `media/locale/en/`, the framework's own dialogs wrapped in `Tr`, and the three scripts with their CI check (L2); the drafting tool over UltraAI with its script, the first drafts of the standard and base sets in German, French, Spanish, Italian, Dutch, Portuguese, Japanese and Simplified Chinese, and the UOS-Settings Language page (L3). The editor (L4), the applications (L5) and locale formatting (L6) follow.
**Version:** 1.2.0
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
- Registry: `Masterfile_modules.md` §20
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

## The catalogues and the scripts

`media/locale/en/Standard.json` is the English standard set: the
framework's own strings, collected from the `Tr` sites in
`UltraCanvas/`, and the application vocabulary of the proposal's
Appendix A, curated by hand. `media/locale/en/base/<Base>.json` are the
sixteen base sets of Appendix B: Computer, Web, Audio, Image, Video, Text,
Spreadsheet, Presentation, Planning, CAD, Banking, Games, Shopping,
Fashion, Communication and Security. An English file's value is its key;
a translation (`de/Standard.json`, `de/base/Video.json`) is the same file
with the values in its language.

| Script | Does |
|---|---|
| `scripts/extract_ui_strings.py` | rewrites the English catalogues from the sources: adds the framework's `Tr` keys to `Standard.json` (never removes one, so the vocabulary stays) and rewrites each application's `en/<Name>.json` from its sites, keeping its metadata and listing what it inherits under `@inherited`; `--check` is the CI sync test |
| `scripts/check_locale_catalogues.py` | the CI gate (`locale-catalogues.yml`): every translation parses, has no stale key, keeps the source's placeholders, has the plural forms its language needs, repeats no entry of a set below it, names only base sets that exist; and the English files are in sync. Findings that predate it are in `scripts/locale_catalogues_baseline.txt` |
| `scripts/wrap_ui_strings.py` | the migration tool: wraps the literal in the text-taking parameter of every known entry point in `Tr(...)` and writes the file (or, with `--diff`, prints the change) for a person to review; a sentence built from pieces is rewritten as `TrF` by hand |

## Drafting a language

`ultralocalisation-draft` (`Apps/UltraLocalisationDraft`, built when the
UltraAI module is) reads an English source catalogue, translates every
entry the target lacks through UltraAI's translator - any text-LLM
provider the person has configured, or a translation provider - and
writes the target marked `"@translated-by": "UltraAI draft <date> ...,
reviewed by -"` until a person changes it. A context key sends its text
alone; a plural entry sends its singular for the `one` form and its
plural for every other form the language's rule needs; a translation
that lost a placeholder is left untranslated and reported, so the
catalogue check never sees it. `scripts/translate_catalogue.py` runs the
tool over a language's sets - the standard set first, then the base
sets, then the applications' - with a one-line context per set:

```
python3 scripts/translate_catalogue.py de fr --provider anthropic --api-key-env ANTHROPIC_API_KEY
python3 scripts/translate_catalogue.py ja --sets Standard base/Video --dry-run
```

The editor's drafting button (L4) is this same step. The first drafts of
the standard and base sets ship in eight languages - `de`, `fr`, `es`,
`it`, `nl`, `pt`, `ja`, `zh-Hans` - each file marked as a machine draft
in `@translated-by` until reviewed; the UOS-Settings Language page lists
a language as soon as its `Standard.json` is there.

The framework's own dialogs - the modal dialogs' buttons and titles, the
file dialog, the filer widget's menus and messages, the colour, date,
time and new-document dialogs, the progress dialog, the spell-check
menu, the media viewer - are wrapped, so they show in the person's
language as soon as a translation of `Standard.json` exists. The date
picker's weekday and month names are the standard set's context entries
(`"weekday|Mo"`, `"month|May"`, `"month-short|May"`), re-read when the
language changes while a picker is open, and they are what phase L6's
date formatting will take the names from: the platform gives a date's
shape, the chosen language its words.

## Related

- [`UltraCanvasLocalisationProposal.md`](../../UltraCanvas/UltraCanvasLocalisationProposal.md) - the design, the standard set (Appendix A) and the base sets (Appendix B)
- [`UltraCanvasSystemLanguage.md`](../../UltraCanvas/UltraCanvasSystemLanguage.md) - the desktop's languages
- [`UltraCanvasEmojiSettings.md`](../../UltraCanvas/UltraCanvasEmojiSettings.md) - the shared-setting pattern `Language.conf` follows
