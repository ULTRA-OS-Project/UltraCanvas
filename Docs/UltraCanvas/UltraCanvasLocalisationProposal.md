# UltraCanvasLocale — Localisation Proposal

<!-- doc-check: std::shared_ptr<UltraCanvasMenu> menu; -->

Status: **Proposal, nothing implemented.** The companion document
[`UltraCanvasHostThemeProposal.md`](UltraCanvasHostThemeProposal.md) covers
the second half of the same job - following the host's colours, font and
font sizes - and shares two pieces with this one: the settings file in the
UltraCanvas settings folder and the watcher that tells a running
application the file changed (§6 here, §6 there).

Author: UltraCanvas Framework
Last Modified: 2026-10-21

---

## 1. Where we are

Nothing in the framework or in any application translates a string. Every
label, menu entry, dialog title, tooltip, column header and message is an
English literal at the call site:

```cpp
auto button = CreateButton("Save", 10, 10, 90, 28, "Save");
menu->AddItem(MenuItemData::Action("Open File...", [] { /* open */ }));
UltraCanvasDialogManager::ShowError("The file could not be written", "Save failed");
```

The size of it, counted on 2026-10-21 (lines that hand a literal of two
or more characters to a text-taking entry point - `CreateButton`,
`CreateLabel`, `MenuItemData`, `SetText`, `SetTooltip`, `ShowMessage`,
`AddColumn`, `TreeNodeData` and the rest):

| Where | UI string sites |
|---|---|
| The framework's own UI (file dialog, modal dialogs, filer widget, colour picker, spell-check menu, ...) | ~1,600 literals in the three largest files alone |
| 29 applications, DemoApp excluded | ~1,600 (UltraMail 159, UltraVideo 118, EmailCleaner 117, UltraPaint 94, UltraAI 87, UltraFiler 78, Texter 77, UltraPassword 74, UltraFIBU 68, ...) |
| DemoApp | ~3,360 (the showcase: every element's demo page) |

What does exist and is reused by this design:

- **The language the user wants is already detected once**, for spelling:
  `UltraCanvasSpellChecker::DetectPreferredLanguage` reads `LC_ALL`,
  `LC_MESSAGES`, `LANG`, `LANGUAGE`, then asks the backend
  (`GetPreferredLanguageHint`: `GetUserDefaultLocaleName` on Windows,
  `NSLocale` on macOS). `UltraCanvasDesktopEntry` reads `LC_MESSAGES` /
  `LC_ALL` again to pick a `.desktop` file's localised `Name[de]`.
- **Shared per-user settings have a folder and a file pattern**:
  `UltraCanvasSettingsFolder()` (`%APPDATA%\UltraCanvas`,
  `~/Library/Application Support/UltraCanvas`, `~/.config/UltraCanvas`),
  one `key=value` file per setting (`Emoji.conf`, `FileDialog.conf`),
  written beside and renamed over, edited in UOS-Settings, read by an
  application when it starts.
- **Bundled resources have one place on every platform**:
  `GetResourcesDir()` + `media/...`, found in the build tree
  (`build/share/media`), the Windows `Resources/` folder and the macOS
  bundle's `Contents/Resources/` without an install step.
- **A JSON engine**: `UltraCanvasJSON` (yyjson), `JSON::ParseFile`,
  insertion-ordered objects.
- **Stable command ids** on menu items (`MenuItemData::commandId`,
  `UltraCanvasMenuRegistry`), which a translated menu can be rebuilt from.
- **Numbers**: the rule that file formats are dot-decimal and UI numbers
  follow the person (`locale-ok`), and `UltraCanvasMoney`'s three explicit
  styles - the groundwork for §5.

## 2. Goal and scope

1. **One call translates a string**, at the site where the English is
   written today, with the English kept in the source: `Tr("Save")`.
2. **Translations are files, not code**: one JSON per language per
   application, in the resources tree, found by the same rule on every
   platform; a translator or a tester drops a file in their settings
   folder and sees it on the next start, without a build.
3. **The language is chosen once for all UltraCanvas applications**, in
   UOS-Settings, following the operating system unless told otherwise.
4. **Cheap at run time**: a hash lookup when a label is built, nothing in
   the render path, no change to how elements store text.
5. **Mechanical migration**: a script wraps the existing literals, a check
   keeps the catalogues complete, and UltraAI drafts the first translation
   of every language so a release never ships half-English.

Out of scope, named so they are not mistaken for oversights: right-to-left
layout mirroring (Pango shapes Arabic and Hebrew text already; mirroring
the element tree is a layout-engine change), translating documentation
and the bundled sample documents, and per-language keyboard shortcuts.

## 3. The API

One header, `include/UltraCanvasLocale.h`, header-light (a `std::string`
in, a `std::string` out, no element types), its state in
`core/UltraCanvasLocale.cpp` inside the shared core, so every application
and plugin in a process shares one catalogue set.

```cpp
namespace UltraCanvas {

// The text for `source` in the current language; `source` itself when no
// catalogue has it, so an untranslated string is always shown, never empty.
std::string Tr(std::string_view source);

// The same, disambiguated: "Open" the verb on a button and "Open" the
// state of a file are two entries, "button|Open" and "state|Open".
std::string TrC(std::string_view context, std::string_view source);

// Singular or plural by `count`, with "{n}" replaced. A language with
// more forms than two (Polish, Russian, Arabic) lists them in its
// catalogue under the CLDR category names; the source has the two.
std::string TrN(std::string_view singular, std::string_view plural, long long count);

// Tr with "{0}", "{1}", ... replaced by the arguments, each formatted
// through the locale (§5) - never through std::to_string.
template <typename... Args>
std::string TrF(std::string_view source, const Args&... args);

class UltraCanvasLocale {
public:
    // "de", "de-AT", "zh-Hant": the language in force, from §4's order.
    static std::string CurrentLanguage();
    // The languages a catalogue exists for, in the resources tree or the
    // settings folder, for a language picker: {"en", "de", "fr", ...}.
    static std::vector<std::string> AvailableLanguages();
    // The language's own name for itself ("Deutsch", "日本語"), from its
    // catalogue's "@name" entry, for the picker.
    static std::string NativeName(const std::string& language);

    // Loads the catalogues for the language: the framework's domain and
    // the application's, from the settings folder first, then the
    // resources tree (§4). Called by Initialize(); an application calls
    // it again to switch at run time, and every change handler runs.
    static bool SetLanguage(const std::string& language);
    using ChangeHandler = std::function<void(const std::string& language)>;
    static int AddChangeHandler(ChangeHandler handler);
    static void RemoveChangeHandler(int id);

    // A plugin or a module with strings of its own registers its domain
    // once (the name of its catalogue file); "UltraCanvas" and the
    // application's name are registered by Initialize().
    static void AddDomain(const std::string& domain);
};

}
```

Why these names: `Tr` is written thousands of times, and a name that long
at every site is the cost that made gettext choose `_()`. Two letters is
what Qt chose for the same reason, and `Tr` is PascalCase as the
repository asks. There is no `Translate` alias: one name is what a grep,
the extraction script and a reader need.

**Keys are the English text.** Not symbolic ids (`"file.save.button"`):

- The code stays readable - `Tr("Save")` says what the button shows,
  `Tr(Strings::FileSaveButton)` does not.
- A missing translation falls back to something a person can read.
- Migration is wrapping, not renaming: no 7,000 identifiers to invent.
- Extraction is a grep for `Tr(`.

The cost is that changing an English string invalidates its translations,
which the check in §7 reports as a new untranslated key - the right
outcome, since the old translation no longer says what the English says.

**Placeholders are `{0}`, `{1}` and `{n}`**, never `%s` / `%1`: a translation
may reorder them, they survive `printf`-illiterate translators, and the
same braces are what `TrN` and the catalogue's plural forms use.

### What `Tr` costs

A catalogue is one `std::unordered_map<std::string, std::string>` per
domain; `Tr` hashes the source once and returns a copy. Elements store the
translated string exactly as they store a literal today, so nothing
changes in `UltraCanvasLabel`, `UltraCanvasButton` or the menu, and
nothing in the render path calls `Tr`. A caller that rebuilds a string
every frame (a status line with a counter) keeps the translated template
in a member and formats that - the same thing it should do with a literal.

### What is not translated

Identifiers (`CreateButton("SaveButton", ...)`'s first argument, menu
`commandId`s, settings keys), debug output, file-format tokens, URLs, and
anything the person typed. A rule for the script in §7: only the
text-taking parameters of the known entry points are wrapped.

## 4. Where the files live - the folder system

Two places, read in this order; the first hit for a key wins:

```
<UltraCanvasSettingsFolder>/locale/<lang>/<Domain>.json     a person's own, or a translator's draft
<GetResourcesDir()>media/locale/<lang>/<Domain>.json        shipped with the build
```

| Path | Example |
|---|---|
| Framework strings | `media/locale/de/UltraCanvas.json` |
| An application's strings | `media/locale/de/UltraMail.json` - the domain is the name given to `Initialize("UltraMail")` |
| A plugin's own strings, if it has any | `media/locale/de/UltraCanvasCharts.json`, registered with `AddDomain` |
| A regional variant | `media/locale/de-AT/UltraCanvas.json` - holds only what differs from `de` |
| A translator's draft on their own machine | `~/.config/UltraCanvas/locale/de/UltraMail.json` |

Why language folder over application folder: `AvailableLanguages()` is a
directory listing; a translator receives one folder; packaging a language
is copying one folder; and a regional variant's fallback (`de-AT` → `de` →
source) is a directory walk, not a naming rule. It is also what gettext's
`locale/<lang>/LC_MESSAGES/<domain>.mo` does, so the layout is familiar.

Why `media/`: it is the one tree every platform's packaging already
carries - `build/share/media` in the build tree, `Resources\media` on
Windows, `Contents/Resources/media` in the macOS bundle, the preloaded
tree on WebAssembly - so no packaging script changes. Under it, `locale/`
is a sibling of `fonts/`, `icons/` and `emoji/`.

Why the settings folder for overrides: it is where every UltraCanvas
application already shares per-user files, and a file there needs no
build, no root and no install. It is how a translator checks their work
in the real window, and how a person fixes one string they dislike.

### The file

One JSON object per language and domain. Keys are the source strings
(with their context prefix when they have one); values are the
translation. Entries starting with `@` are metadata.

```json
{
  "@language": "de",
  "@name": "Deutsch",
  "@plural": "one|other",
  "@translated-by": "UltraAI draft 2026-10-21, reviewed by -",

  "Save": "Speichern",
  "Open File...": "Datei öffnen...",
  "button|Open": "Öffnen",
  "state|Open": "Geöffnet",
  "The file could not be written": "Die Datei konnte nicht geschrieben werden",
  "Deleted {0} of {1}": "{0} von {1} gelöscht",
  "{n} file": { "one": "{n} Datei", "other": "{n} Dateien" }
}
```

A plural entry is an object keyed by CLDR category (`zero`, `one`, `two`,
`few`, `many`, `other`); `TrN` picks the category from the count with the
language's rule, a table of the twenty-odd rules that cover every language
the applications will ship in (the same table ICU and gettext carry,
written as C++ switch cases, no dependency).

JSON rather than gettext's `.po`/`.mo` because the engine is already in
the tree (`UltraCanvasJSON`), there is no new dependency to record in
`Docs/Dependencies.md`, every editor and every LLM handles it, and the
yyjson parse of a 2,000-entry file is well under a millisecond. The `.po`
format is kept reachable: §7's script can export and import it for a
translator whose tool wants one.

### Choosing the language

`Initialize()` picks the language in this order, first answer wins:

1. `--lang=<code>` on the command line, for testing.
2. `ULTRACANVAS_LANG` in the environment, for a script or a CI screenshot.
3. `Language.conf` in the UltraCanvas settings folder (`language=de`),
   written by UOS-Settings; `language=system` or no file means step 4.
4. The operating system: `DetectPreferredLanguage` moved out of the spell
   checker into `UltraCanvasLocale` (the spell checker then calls it), with
   the backends it lacks today - `GetUserPreferredUILanguages` on Windows,
   `NSLocale.preferredLanguages` on macOS, `navigator.language` on
   WebAssembly, `Locale.getDefault()` through the JNI bridge on Android.
5. `en`, which needs no catalogue: the source is the text.

A catalogue is looked up from the most specific tag down: `zh-Hant-TW` →
`zh-Hant` → `zh` → source.

## 5. Numbers, dates and sizes

Half of what a person sees as "the wrong language" is `1,234.5` on a
German desktop. The same header gives the formatting the locale wants,
and `TrF` uses it for every numeric argument:

```cpp
class UltraCanvasLocale {
public:
    static std::string FormatNumber(double value, int decimals);      // 1.234,5
    static std::string FormatInteger(long long value);                // 1.234
    static std::string FormatPercent(double fraction, int decimals);  // 12,5 %
    static std::string FormatFileSize(unsigned long long bytes);      // 1,2 MB - the one every filer rewrites
    static std::string FormatDate(std::time_t when, DateStyle style); // 21.10.2026, 21. Oktober 2026
    static std::string FormatTime(std::time_t when, TimeStyle style); // 14:05, 2:05 PM
    static std::string DecimalSeparator();                            // for a numeric field's input
};
```

Sources: `GetLocaleInfoEx` / `GetDateFormatEx` on Windows, `NSNumberFormatter`
/ `NSDateFormatter` on macOS, `localeconv()` and `strftime` on Linux (the
backend already calls `setlocale(LC_ALL, "")` for XIM, which is exactly what
these need), `Intl` through an `EM_ASM` bridge on WebAssembly. This is the
one place that is allowed to depend on `LC_NUMERIC`, and each site says so
with the existing `// locale-ok:` marker. File formats keep
`FormatFloatClassic` / `TryParseFloat`, as the rule in `AGENTS.md` says.
`UltraCanvasMoney`'s formatting is untouched: an invoice amount's style is
the document's, not the desktop's.

## 6. The setting, UOS-Settings and running applications

`Language.conf` follows `Emoji.conf` exactly: one line, written beside and
renamed over, `UltraCanvas::LanguageSettings::Load/Save/FilePath`. UOS-Settings
gains a **Language** page (a dropdown of `AvailableLanguages()` with
their `NativeName`, "System" first; the formatting examples of §5 below it
as a preview).

A language change reaches applications started after it, like the emoji
setting does today. A running application can follow it too: the
settings-folder watcher proposed in `UltraCanvasHostThemeProposal.md` §6
(one `UltraCanvasFolderWatcher` on the UltraCanvas settings folder, a
handler per file name) calls `UltraCanvasLocale::SetLanguage`, and the
application's change handler rebuilds what it built from strings - its
menus through `UltraCanvasMenuRegistry`, its windows the way UltraPress
re-applies its theme. Re-labelling every element in place would mean every
element storing its source key next to its text; that doubles the memory
of every label for a switch that happens once a year, so it is not
proposed. Elements keep storing text; applications that want live
switching rebuild.

## 7. Tooling and the check

Three scripts under `scripts/`, in the style of the checks that already
run in CI:

- **`extract_ui_strings.py`** walks the sources for `Tr(`, `TrC(`, `TrN(`
  and `TrF(` with a literal argument and rewrites
  `media/locale/en/<Domain>.json` - the source catalogue, key equals value,
  the template every other language is measured against. Generated, never
  edited by hand, regenerated like `llms.txt`.
- **`check_locale_catalogues.py`** (CI, `locale-catalogues.yml`): the `en`
  catalogues are in sync with the sources; every other language's file
  parses, has no key the source lacks (stale), and reports its untranslated
  count; a placeholder `{0}` present in the source and missing in a
  translation is an error; a plural entry has every category its
  language's rule needs. Like `check_locale_numbers.py` it has a baseline
  for what it finds on day one and only lets that shrink.
- **`wrap_ui_strings.py`** - the migration tool, run once per file by the
  person migrating it: wraps the literal in the text-taking parameter of
  each known entry point (the table below) in `Tr(...)`, leaves every
  other literal alone, and writes a diff for review. It is not a CI check
  and never runs unattended; its table is the list of what counts as UI
  text:

| Entry point | Wrapped parameter |
|---|---|
| `CreateButton`, `CreateLabel`, `CreateCheckbox`, `CreateRadio` ... | the text, not the identifier |
| `MenuItemData::Action/Checkbox/Header/Submenu`, `MenuBuilder::AddItem` | `label`, `tooltip` |
| `UltraCanvasDialogManager::Show*`, `UltraCanvasAlert::Show*` | message, title, button labels |
| `SetText`, `SetTitle`, `SetWindowTitle`, `SetTooltip`, `SetPlaceholder`, `SetStatus` | the text |
| `AddColumn`, `TreeNodeData`, `AddTab`, `FormLayout::AddRow` | the header / label |

- **`translate_catalogue.py`** drafts a language with UltraAI (the
  framework's own LLM module, any configured provider): every untranslated
  key of a domain in one request per 200 keys, with the context prefixes
  and the application's name as context, written with
  `"@translated-by": "UltraAI draft <date>, reviewed by -"` until a person
  changes it. A draft ships; a release note says which languages are
  reviewed. This is what makes "every application in eight languages" a
  week's work rather than a year's.

## 8. The phases

| Phase | What | Where | Effort |
|---|---|---|---|
| **L1** | `UltraCanvasLocale.h/.cpp`: `Tr`/`TrC`/`TrN`/`TrF`, catalogue loading from both folders, the fallback walk, the plural table, `DetectPreferredLanguage` moved from the spell checker with the missing backends, `Language.conf`, `Initialize()` wiring, `Tests/LocaleTest.cpp` (no display) | core, OS backends | 3-4 days |
| **L2** | The framework's own strings: file dialog, modal dialogs, alert, filer widget, colour picker, date picker, spell-check menu, tooltip texts, media viewer - domain `UltraCanvas`; `extract_ui_strings.py`, `check_locale_catalogues.py` with its baseline, `wrap_ui_strings.py` | core | 4-5 days (every application gains translated dialogs at once) |
| **L3** | UOS-Settings Language page; `translate_catalogue.py`; the first drafts for de, fr, es, it, nl, pt, ja, zh-Hans | UOSSettings, scripts | 2 days |
| **L4** | The applications, one PR each so a reviewer sees one app's diff: wrap, extract, draft, build, run. Order by use: UltraFiler, Texter, UltraMail, UltraPassword, UOSSettings, UltraDesktop, then the rest; DemoApp last, or its demo pages left English (they are the catalogue of the elements, read by developers) | Apps | ½-1 day per application, ~20 days for 29 |
| **L5** | §5 formatting with its four backends; `FormatFileSize` replacing the filer's, the cleaner's and the mail client's own; `check_locale_numbers.py` learns the new functions are `locale-ok` | core, Apps | 3 days |

L1-L3 are the deliverable that makes the rest routine; L4 is parallel
work for several sessions once L2 is merged.

## 9. Decisions and alternatives considered

- **Translate inside the elements, with no `Tr` at the site?** Every
  `SetText` and every factory would look its text up, and 30 applications
  would be translated with zero code change. Rejected: a file called
  `Open` in a filer row would be shown as `Öffnen`; a text area's
  `SetText` of a 5 MB document would hash the document; and nothing could
  tell a UI string from data, so there would be nothing to extract and
  nothing for a translator to translate. The wrapping script gives the
  same economy with the site explicit.
- **gettext** (`libintl`): a dependency on Windows and macOS, `.mo`
  compilation in the build, `%s` placeholders, and a `bindtextdomain`
  path model that does not know the settings-folder override. Its one
  strength - the translator tool ecosystem - is kept through `.po`
  export/import.
- **Symbolic keys**: see §3.
- **Catalogue per application folder** (`media/locale/UltraMail/de.json`):
  see §4.
- **Live re-labelling of every element**: see §6.
- **A global `SetLanguage` that elements observe**: the theme does this
  for colours because the colours live in a style struct the element
  rebuilds; text has no such struct. Applications rebuild; the change
  handler is the hook.

## 10. Related

- `UltraCanvasHostThemeProposal.md` - the shared settings watcher, UOS-Settings Appearance, and the host font a CJK or Thai language needs (the bundled Ubuntu family has neither script).
- `UltraCanvasEmojiSettings.md` - the shared-setting pattern this follows.
- `UltraCanvasSettingsFolder.h`, `UltraCanvasDataFolder.h` - the folders.
- `UltraCanvasJSON.md` - the catalogue parser.
- `UltraCanvasMenuExamples.md` - `commandId` and the registry a translated menu is rebuilt from.
