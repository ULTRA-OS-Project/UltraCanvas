# UltraLocalisation — Localisation Proposal

<!-- doc-check: std::shared_ptr<UltraCanvasMenu> menu; std::shared_ptr<UltraCanvasWindow> window; -->

Status: **Proposal, nothing implemented.** Revised 2026-11-02: the module is
`UltraLocalisation`, it loads a **standard set** UltraCanvas ships for every
application and the application's **own set** over it, and a **localisation
editor** shows both sets in one view. The companion document
[`UltraCanvasHostThemeProposal.md`](UltraCanvasHostThemeProposal.md) covers
the second half of the same job - following the host's colours, font and
font sizes - and shares two pieces with this one: the settings file in the
UltraCanvas settings folder and the watcher that tells a running
application the file changed (§7 here, §6 there).

Author: UltraCanvas Framework
Last Modified: 2026-11-02

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

Most of those 1,600 application strings are the same few hundred words:
every application has File, Edit, View and Help, Open, Save, Cancel, OK,
Delete, Name, Size, Modified, "Do you want to save the changes?". That is
what the standard set is for (§4).

What does exist and is reused by this design:

- **The language the user wants is detected once, for everyone**:
  `UltraCanvasSystemLanguage.h` (`SystemLanguageTags()`, 2026-11-02) reads
  the POSIX variables as gettext does and the platform's own list (Windows'
  preferred UI languages, macOS's Language & Region, the browser's
  `navigator.languages`, Android's configuration); the spell checker's
  `DetectPreferredLanguage` already uses it. `UltraCanvasDesktopEntry`
  reads the raw locale itself for a `.desktop` file's `Name[de_DE@euro]`.
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
- **UI-free modules inside the shared core**: UltraNet, UltraWin,
  UltraCrypt and the rest are each a static archive the core absorbs
  whole (`_ultracanvas_module_home`, *One shared core* in `AGENTS.md`), so
  a module's state - here the loaded catalogues - is one copy per process,
  shared by the application and every plugin.
- **Stable command ids** on menu items (`MenuItemData::commandId`,
  `UltraCanvasMenuRegistry`), which a translated menu can be rebuilt from.
- **A columns tree view** (`UltraCanvasColumnsTreeView`, documented in
  `UltraCanvasTreeViewExamples.md`) with aligned columns, a header band and
  section-header rows, and `UltraCanvasListSortFilterProxy` - what the
  editor's grid is made of, not a grid of its own. Its cells are not
  editable yet; the editor adds in-place editing to it rather than a grid
  of its own.
- **Numbers**: the rule that file formats are dot-decimal and UI numbers
  follow the person (`locale-ok`), and `UltraCanvasMoney`'s three explicit
  styles - the groundwork for §6.

## 2. Goal and scope

1. **One call translates a string**, at the site where the English is
   written today, with the English kept in the source: `Tr("Save")`.
2. **A standard set comes with the framework**: the words every
   application needs, translated once, in every language UltraCanvas
   ships. An application writes `Tr("Cancel")` and has it in Thai without
   a catalogue of its own. Its own set holds what is its own, and may
   amend the standard where its wording differs.
3. **Translations are files, not code**: one JSON per language per set,
   in the resources tree, found by the same rule on every platform; a
   translator or a tester drops a file in their settings folder and sees
   it on the next start, without a build.
4. **The language is chosen once for all UltraCanvas applications**, in
   UOS-Settings, following the operating system unless told otherwise.
5. **An editor, not a text editor**: one view with the standard set and
   the application's set side by side, per language, with what is
   missing, what is inherited and what the application overrides visible
   at a glance, saving where the person has the right to save.
6. **Cheap at run time**: a hash lookup when a label is built, nothing in
   the render path, no change to how elements store text.
7. **Mechanical migration**: a script wraps the existing literals, a check
   keeps the catalogues complete, and UltraAI drafts the first translation
   of every language so a release never ships half-English.

Out of scope, named so they are not mistaken for oversights: right-to-left
layout mirroring (Pango shapes Arabic and Hebrew text already; mirroring
the element tree is a layout-engine change), translating documentation
and the bundled sample documents, and per-language keyboard shortcuts.

## 3. The module and its API

**UltraLocalisation** is a UI-free module in the pattern of UltraNet and
UltraCrypt: headers under `UltraCanvas/include/UltraLocalisation/`, sources
under `UltraCanvas/core/UltraLocalisation/`, built as `uc-localisation` and
absorbed into the shared core, registered in `Masterfile_modules.md` with
its own section, linked by its public name `UltraLocalisation`. UI-free
so that an application's headless engine (UltraMailEngine, UltraPasswordCore)
can translate a message it hands to the UI, and so a test loads a
catalogue without a display. The editor (§5) is UI and lives in the core
proper as an element.

One header, `UltraLocalisation/UltraLocalisation.h`, header-light (a
`std::string` in, a `std::string` out, no element types):

```cpp
namespace UltraCanvas {

// The text for `source` in the current language: the application's set
// first, the standard set second, `source` itself when neither has it,
// so an untranslated string is always shown, never empty.
std::string Tr(std::string_view source);

// The same, disambiguated: "Open" the verb on a button and "Open" the
// state of a file are two entries, "button|Open" and "state|Open".
std::string TrC(std::string_view context, std::string_view source);

// Singular or plural by `count`, with "{n}" replaced. A language with
// more forms than two (Polish, Russian, Arabic) lists them in its
// catalogue under the CLDR category names; the source has the two.
std::string TrN(std::string_view singular, std::string_view plural, long long count);

// Tr with "{0}", "{1}", ... replaced by the arguments, each formatted
// through the locale (§6) - never through std::to_string.
template <typename... Args>
std::string TrF(std::string_view source, const Args&... args);

// Which set answered: for the editor's "inherited" marking and for a
// start-up report of what an application still shows in English.
enum class LocalisationOrigin { Application, Standard, Source };

class UltraLocalisation {
public:
    // "de", "de-AT", "zh-Hant": the language in force, from §4's order.
    static std::string CurrentLanguage();
    // The languages the standard set exists for, with any the application
    // set adds, in the resources tree or the settings folder, for a
    // language picker: {"en", "de", "fr", ...}.
    static std::vector<std::string> AvailableLanguages();
    // The language's own name for itself ("Deutsch", "日本語"), from the
    // standard set's "@name" entry, for the picker.
    static std::string NativeName(const std::string& language);

    // Loads the standard set and the application's set for the language
    // (§4): the settings-folder override of each over the shipped file.
    // Called by Initialize() with the name it was given ("UltraMail");
    // an application calls it again to switch at run time, and every
    // change handler runs.
    static bool Load(const std::string& language, const std::string& application);
    static bool SetLanguage(const std::string& language);
    using ChangeHandler = std::function<void(const std::string& language)>;
    static int AddChangeHandler(ChangeHandler handler);
    static void RemoveChangeHandler(int id);

    // Which set has `source`, in the current language.
    static LocalisationOrigin OriginOf(std::string_view source);
    // Every source the standard set knows, for the editor and for the
    // check: an application's catalogue need not repeat them.
    static std::vector<std::string> StandardKeys();

    // A library an application brings that has strings of its own (a
    // third-party plugin) registers its set once, looked up after the
    // application's and before the standard one.
    static void AddSet(const std::string& name);
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
- The standard set works by it: an application that writes `Tr("Cancel")`
  meets the standard entry without naming it.

The cost is that changing an English string invalidates its translations,
which the check in §8 reports as a new untranslated key - the right
outcome, since the old translation no longer says what the English says.

**Placeholders are `{0}`, `{1}` and `{n}`**, never `%s` / `%1`: a translation
may reorder them, they survive `printf`-illiterate translators, and the
same braces are what `TrN` and the catalogue's plural forms use.

### What `Tr` costs

A set is one `std::unordered_map<std::string, std::string>` per language;
`Tr` hashes the source once, looks in the application's map and then the
standard one, and returns a copy. Elements store the translated string
exactly as they store a literal today, so nothing changes in
`UltraCanvasLabel`, `UltraCanvasButton` or the menu, and nothing in the
render path calls `Tr`. A caller that rebuilds a string every frame (a
status line with a counter) keeps the translated template in a member and
formats that - the same thing it should do with a literal.

### What is not translated

Identifiers (`CreateButton("SaveButton", ...)`'s first argument, menu
`commandId`s, settings keys), debug output, file-format tokens, URLs, and
anything the person typed. A rule for the script in §8: only the
text-taking parameters of the known entry points are wrapped.

## 4. The two sets, and where the files live

### The standard set

`Standard.json` is what UltraCanvas ships, in every language it supports.
It holds two things that the lookup does not distinguish:

1. **The framework's own strings** - what its dialogs, the file dialog,
   the filer widget, the colour picker, the spell-check menu and the
   tooltips show. These are extracted from the framework's sources.
2. **The application vocabulary** - the words and sentences every
   application needs, curated by hand and kept whether or not a framework
   source uses them: the menu names (File, Edit, View, Window, Help), the
   actions (New, Open, Open Recent, Save, Save As..., Close, Quit, Undo,
   Redo, Cut, Copy, Paste, Delete, Select All, Find, Replace, Print,
   Preferences..., About), the dialog buttons (OK, Cancel, Yes, No, Apply,
   Retry, Ignore, Don't Save), the states (Ready, Loading..., Done,
   Error, Warning), the columns (Name, Size, Type, Date, Modified,
   Created, Folder, Path), the words a file dialog and a filer share
   (Untitled, New Folder, Rename, Move to Trash, Properties), the
   confirmations with placeholders ("Do you want to save the changes to
   {0}?", "{0} could not be opened", "Are you sure you want to delete
   {n} item" / "{n} items"), and the units (B, KB, MB, GB, {n} second /
   seconds, minute, hour).

The English file `media/locale/en/Standard.json` is therefore maintained,
not only generated: the extraction script (§8) adds to it what the
framework's sources use and never removes an entry, so a vocabulary word
no framework source happens to use stays for the applications. An entry
that nothing uses is a report, never an error.

An application gets the standard set for free and writes `Tr("Save")`
like everyone else. Its own set, `<App>.json`, holds:

- its own strings - the ones no other application has ("Compose",
  "Mark as spam", "Fade out the audio");
- **amendments** of standard entries whose wording differs in this
  application: a photo viewer whose "Delete" must read "Remove from
  album" lists `"Delete": "Aus dem Album entfernen"` in its German set, and
  the lookup takes it before the standard one. The editor (§5) shows such
  a row as an override, with the standard text beside it.

The check (§8) treats a key that is in both files with the same
translation as a duplicate to remove: an application set that repeats the
standard set is the maintenance burden the standard set exists to end.

### Where the files live

Two places, read in this order; the first hit for a key wins, the
application's set before the standard one within each:

```
<UltraCanvasSettingsFolder>/locale/<lang>/<App>.json       a person's own, or a translator's draft
<UltraCanvasSettingsFolder>/locale/<lang>/Standard.json
<GetResourcesDir()>media/locale/<lang>/<App>.json          shipped with the build
<GetResourcesDir()>media/locale/<lang>/Standard.json
```

| Path | Example |
|---|---|
| The standard set | `media/locale/de/Standard.json` |
| An application's set | `media/locale/de/UltraMail.json` - the name given to `Initialize("UltraMail")` |
| A library's own set, if it has one | `media/locale/de/UltraCanvasCharts.json`, registered with `AddSet` |
| A regional variant | `media/locale/de-AT/Standard.json` - holds only what differs from `de` |
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
in the real window, how the editor saves on a machine where the resources
tree is read-only, and how a person fixes one string they dislike.

### The file

One JSON object per language and set. Keys are the source strings (with
their context prefix when they have one); values are the translation.
Entries starting with `@` are metadata.

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
format is kept reachable: §8's script can export and import it for a
translator whose tool wants one.

### Choosing the language

`Initialize()` picks the language in this order, first answer wins:

1. `--lang=<code>` on the command line, for testing.
2. `ULTRACANVAS_LANG` in the environment, for a script or a CI screenshot.
3. `Language.conf` in the UltraCanvas settings folder (`language=de`),
   written by UOS-Settings; `language=system` or no file means step 4.
4. The operating system: `SystemLanguageTags()` from
   `UltraCanvasSystemLanguage.h`, best first - the POSIX variables, then
   `GetUserPreferredUILanguages` on Windows, `NSLocale.preferredLanguages`
   on macOS, `navigator.languages` on WebAssembly, the device configuration
   on Android - the first tag a catalogue exists for.
5. `en`, which needs no catalogue: the source is the text.

A set is looked up from the most specific tag down: `zh-Hant-TW` →
`zh-Hant` → `zh` → source.

## 5. The localisation editor

One element, `UltraCanvasLocalisationEditor`
(`include/UltraCanvasLocalisationEditor.h`, a row in the element
catalogue), built from the elements the framework has - a toolbar, two
dropdowns, a search field, a columns tree view with editable cells, a
status label - and hosted in three places:

- **UOS-Settings > Language > Translations**, the page a translator or an
  administrator works in: any application, any language.
- **Help > Translate this application...** in an application, one menu
  item that opens the editor in a modal dialog on the application's own
  set and the current language (`UltraCanvasLocalisationEditor::OpenDialog`),
  so the person who notices a wrong word fixes it where they saw it.
- **DemoApp**, as the element's demo page.

### One view, both sets

The grid shows the application's set and the standard set together,
one row per key, with where each text comes from visible in the row:

| Key (English) | Context | Standard | Application | Shown | Status |
|---|---|---|---|---|---|
| Save | | Speichern | | Speichern | inherited |
| Delete | | Löschen | Aus dem Album entfernen | Aus dem Album entfernen | **override** |
| Mark as spam | | | Als Spam markieren | Als Spam markieren | own |
| Fade out the audio | | | | Fade out the audio | **untranslated** |
| Open | button | Öffnen | | Öffnen | inherited |
| {n} file | | one: {n} Datei / other: {n} Dateien | | | inherited, plural |

Rows are grouped - *This application*, *Inherited from the standard set*,
and, when "Show the whole standard set" is on, *Standard, not used here* -
and filtered by the search field and by status (untranslated, machine
drafts, overrides, changed since the shipped file). The **Standard**
column is read-only for an application's translator and editable when
the toolbar's "Edit the standard set" is on, because a change there
reaches every application; the **Application** column is always
editable. Typing in the Application cell of an inherited row makes it an
override; clearing it removes the override and the row is inherited
again. A plural row opens its categories as child rows, one per form the
language's rule needs, so a missing `few` is a visible empty cell.

A second language column pair can be shown beside the first ("Compare
with: en" by default, so the English is always in sight; "fr" to
translate from a sister language), which is the whole of what a
translator usually wants from a two-pane tool.

The rows come from three sources, merged by key: the application's `en`
catalogue (every key its sources use, from §8's extraction), the standard
`en` catalogue, and the two files of the chosen language in both folders.
Keys the application uses that the standard set has are the *inherited*
group; that is `OriginOf`, applied to a file rather than to the running
process, so the editor can show an application that is not running.

### Saving

Where a save goes is decided by what is writable, shown in the status
line and never silent:

- **Into the source tree** when the editor was opened on a checkout (the
  resources dir is `build/share/media`, a link into the repository, or a
  folder named on the toolbar): the shipped `media/locale/<lang>/<Set>.json`,
  keys in the `en` file's order, formatted as `extract_ui_strings.py`
  writes it, so a save is a clean diff for a pull request. This is the
  developer's and the project translator's path.
- **Into the settings folder** otherwise (a packaged build, a read-only
  resources tree): `<settings>/locale/<lang>/<Set>.json`, holding only the
  entries that differ from the shipped file. A running application picks
  it up through the settings-folder watcher (§7) - a translator sees their
  change in the real window as they type, which is the test every other
  tool makes them restart for. "Export..." writes the merged file or a
  `.po` to a place of their choosing, to send upstream.

Metadata is kept honest by the editor: a cell a person edits clears the
row's machine-draft mark; the file's `@translated-by` gains the person's
name from `Language.conf`'s `translator=` line, when they set one.

### Drafting

"Draft with UltraAI" on the toolbar fills the selected rows, or every
untranslated row, through the framework's own LLM module (any provider
the person configured), marks them as drafts, and leaves them selected
for review. The same code is what `translate_catalogue.py` (§8) runs
headless.

## 6. Numbers, dates and sizes

Half of what a person sees as "the wrong language" is `1,234.5` on a
German desktop. The same header gives the formatting the locale wants,
and `TrF` uses it for every numeric argument:

```cpp
class UltraLocalisation {
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

## 7. The setting, UOS-Settings and running applications

`Language.conf` follows `Emoji.conf` exactly: one line, written beside and
renamed over, `UltraCanvas::LanguageSettings::Load/Save/FilePath`
(`language=de`, and `translator=<name>` for the editor's metadata).
UOS-Settings gains a **Language** page: a dropdown of `AvailableLanguages()`
with their `NativeName`, "System" first; the formatting examples of §6
below it as a preview; and the **Translations** sub-page that hosts the
editor (§5).

A language change reaches applications started after it, like the emoji
setting does today. A running application can follow it too: the
settings-folder watcher proposed in `UltraCanvasHostThemeProposal.md` §6
(one `UltraCanvasFolderWatcher` on the UltraCanvas settings folder, a
handler per file name) calls `UltraLocalisation::SetLanguage`, and the
application's change handler rebuilds what it built from strings - its
menus through `UltraCanvasMenuRegistry`, its windows the way UltraPress
re-applies its theme. The same watcher, on `locale/<lang>/`, is what lets
the editor's saves show in a running application. Re-labelling every
element in place would mean every element storing its source key next to
its text; that doubles the memory of every label for a switch that
happens once a year, so it is not proposed. Elements keep storing text;
applications that want live switching rebuild.

## 8. Tooling and the check

Three scripts under `scripts/`, in the style of the checks that already
run in CI:

- **`extract_ui_strings.py`** walks the sources for `Tr(`, `TrC(`, `TrN(`
  and `TrF(` with a literal argument and rewrites
  `media/locale/en/<App>.json` for each application - the source
  catalogue, key equals value, the template every other language is
  measured against - with the keys the standard set has listed under
  `"@standard"` rather than repeated, so the editor and the check know
  them as inherited. For `Standard.json` it adds what the framework's
  sources use and removes nothing (§4). Generated, never edited by hand
  for an application; regenerated like `llms.txt`.
- **`check_locale_catalogues.py`** (CI, `locale-catalogues.yml`): the `en`
  catalogues are in sync with the sources; every other language's file
  parses, has no key the source lacks (stale), repeats no standard entry
  with the same translation (§4), and reports its untranslated count; a
  placeholder `{0}` present in the source and missing in a translation is
  an error; a plural entry has every category its language's rule needs.
  Like `check_locale_numbers.py` it has a baseline for what it finds on
  day one and only lets that shrink.
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
  key of a set in one request per 200 keys, with the context prefixes and
  the application's name as context, written with
  `"@translated-by": "UltraAI draft <date>, reviewed by -"` until a person
  changes it. The standard set is drafted first and reviewed first: it is
  the one file whose quality every application inherits. A draft ships; a
  release note says which languages are reviewed. This is what makes
  "every application in eight languages" a week's work rather than a
  year's.

## 9. The phases

| Phase | What | Where | Effort |
|---|---|---|---|
| **L1** | The UltraLocalisation module: `Tr`/`TrC`/`TrN`/`TrF`, the two sets from both folders, `OriginOf`, the fallback walk over `SystemLanguageTags()`, the plural table, `Language.conf`, `Initialize()` wiring, the Masterfile section, `Tests/UltraLocalisationTest.cpp` (no display) | module | 3 days |
| **L2** | The standard set: the framework's own strings wrapped (file dialog, modal dialogs, alert, filer widget, colour picker, date picker, spell-check menu, tooltips, media viewer) and the curated vocabulary written into `en/Standard.json`; `extract_ui_strings.py`, `check_locale_catalogues.py` with its baseline, `wrap_ui_strings.py` | core, scripts | 5 days (every application gains translated dialogs and the vocabulary at once) |
| **L3** | `translate_catalogue.py`; the standard set drafted and reviewed in de, fr, es, it, nl, pt, ja, zh-Hans; UOS-Settings Language page | scripts, UOSSettings | 2 days |
| **L4** | The editor element: the merged grid over the columns tree view, the two save paths, the compare column, the drafting button; its dialog opener; the UOS-Settings Translations page; the DemoApp page; `UltraCanvasLocalisationEditor.md` | core, UOSSettings, DemoApp | 5 days |
| **L5** | The applications, one PR each so a reviewer sees one app's diff: wrap, extract, draft, build, run; the Help menu item. Order by use: UltraFiler, Texter, UltraMail, UltraPassword, UOSSettings, UltraDesktop, then the rest; DemoApp last, or its demo pages left English (they are the catalogue of the elements, read by developers) | Apps | ½ day per application - most of an application's strings are standard after L2 - ~15 days for 29 |
| **L6** | §6 formatting with its four backends; `FormatFileSize` replacing the filer's, the cleaner's and the mail client's own; `check_locale_numbers.py` learns the new functions are `locale-ok` | module, Apps | 3 days |

L1-L4 are the deliverable that makes the rest routine; L5 is parallel
work for several sessions once L2 is merged.

## 10. Decisions and alternatives considered

- **Translate inside the elements, with no `Tr` at the site?** Every
  `SetText` and every factory would look its text up, and 30 applications
  would be translated with zero code change. Rejected: a file called
  `Open` in a filer row would be shown as `Öffnen`; a text area's
  `SetText` of a 5 MB document would hash the document; and nothing could
  tell a UI string from data, so there would be nothing to extract and
  nothing for a translator to translate. The wrapping script gives the
  same economy with the site explicit.
- **One set per application with the standard entries copied in**, which
  some frameworks' tooling does: 29 copies of "Cancel" to keep in step,
  and a fix to the standard German reaching nobody. The lookup chain
  costs one more hash per miss and ends that.
- **A separate `Framework.json` beside `Standard.json`** for the strings
  only the framework's dialogs show: nobody can hold the line between
  "the framework's" and "every application's" - "Show hidden files" is
  both - so there is one shipped set.
- **gettext** (`libintl`): a dependency on Windows and macOS, `.mo`
  compilation in the build, `%s` placeholders, and a `bindtextdomain`
  path model that knows neither the settings-folder override nor the
  two-set chain. Its one strength - the translator tool ecosystem - is
  kept through `.po` export/import.
- **A standalone editor application** rather than an element: a 31st
  application to package on five platforms, for a tool whose natural home
  is the settings application that already chooses the language and the
  Help menu of the application being translated. The element can still be
  wrapped in an application later in an afternoon.
- **Symbolic keys**: see §3.
- **Catalogue per application folder** (`media/locale/UltraMail/de.json`):
  see §4.
- **Live re-labelling of every element**: see §7.

## Appendix A. The standard set, measured

What the applications actually share, counted on 2026-11-02 with
`scripts/survey_ui_strings.py`: every string literal in `Apps/<App>/` that
looks like interface text (a capital first letter, letters and light
punctuation, 2-60 characters, no identifier shapes), counted once per
application, DemoApp left out; the same over `UltraCanvas/core/` for the
framework's own. It over-counts a little (a log line can look like a
label) and under-counts a little (a string built from pieces), so the
numbers are a measure, not a gate.

| | |
|---|---|
| Applications surveyed | 27 |
| Distinct UI-looking strings in them | 5,158 |
| ... in 2 or more applications | 423 |
| ... in 3 or more | 166 |
| ... in 4 or more | 56 |
| Framework's own (`core/`), distinct | 311 |

The long tail is each application's own vocabulary ("Mark as spam",
"Fade out the audio", "Move Layer Up") and stays in its set. The head is
the standard set. Three things the survey turned up that shape it:

- **Shortcuts are written into labels.** "Undo (Ctrl+Z)" is in 4
  applications, "Save (Ctrl+S)" in 3, "Open (Ctrl+O)" in 2, and the bare
  "Undo" in 6 more. The standard entry is "Undo"; the shortcut is the
  menu item's own `shortcut` field, which the menu draws right-aligned
  and the wrapping script (§8) splits off. A label that embeds its key
  cannot be translated once.
- **Sentences are concatenated.** "Could not read " (4 applications),
  "Saved " (5), "Opening " (4), "Not saved: " (3), "Could not write " (3,
  and 4 framework files) are prefixes with a trailing space, joined to a
  file name at the site. A translator cannot reorder a prefix, so the
  standard entry is the sentence with its placeholder - "Could not read
  {0}" - and the site becomes `TrF`.
- **Two spellings of the same word.** "Colour" (4 applications, 2
  framework files) and "Color" (3 framework files); "Centre" (4) and
  "Center" (2). The source language of the standard set is the
  framework's own British English - `Colour`, `Centre`, `Cancelled`,
  `Grey` - and the check (§8) reports the other spelling as a key the
  standard set already has.

### A.1 The draft contents

The number after an entry is how many applications write it today;
`fw` marks the framework's own dialogs and widgets. An entry with no
number is vocabulary the survey did not find shared yet but every
application will need the moment it is translated - a word it is cheaper
to translate once than to leave each application to discover.

**Menus.** File 4, Edit 9, View 4, Window, Help 2, Tools, Settings 7,
Options, Preferences..., Recent.

**Actions.** New... 2, Open 9 `fw`, Open... 3, Open Recent, Save 9 `fw`,
Save As... 3, Save a Copy..., Close 14 `fw`, Close All, Quit 4, Undo 6,
Redo 3, Cut 4 `fw`, Copy 8 `fw`, Paste 3 `fw`, Delete 8 `fw`, Remove 6,
Rename 2 `fw`, Duplicate 2, Select All 4, Deselect, Find 3, Find... 2,
Find Next, Replace 3 `fw`, Refresh 4, Reload, Import, Export 4, Print...,
Add 3 `fw`, Create 4 `fw`, Edit 9, Clear 3 `fw`, Reset `fw`, Restore
defaults 4, Apply 2 `fw`, Run 2, Start 3, Stop 5, Pause, Resume 2,
Continue 2, Send 5, Back 2, Forward 3, Up, Down, Move up 2, Move down 2,
Zoom In 2, Zoom Out 2, Zoom 3, Fit, Show 2, Hide, Pin 2, Unpin 2, Lock 3,
Unlock 4, Install 2, Download 2, Upload 2, Preview 2, Browse..., Choose...,
Change 2, Check 2, Generate 2, Scan 2, Submit 2, Sort `fw`, Filter, Search,
Help 2, About.

**Dialog buttons and answers.** OK, Cancel 14 `fw`, Yes `fw`, No `fw`,
Apply, Retry `fw`, Ignore `fw`, Abort `fw`, Don't Save, Save Changes, Quit
anyway 2, Discard, Overwrite, Replace File `fw`, Skip, Skip All, Try Again.

**States and messages.** Ready 4, Done 2, Failed 4, Error 2 `fw`, Warning,
Information `fw`, Success `fw`, Cancelled 2 `fw`, Unknown 6 `fw`, None 6
`fw`, Never 3, On, Off 3, Auto 3 `fw`, Default 2 `fw`, Custom 2 `fw`,
Normal 2 `fw`, Connected `fw`, Offline 3, Locked 3, Visible 2, Hidden
`fw`, Loading..., Saving..., Working..., No results, Nothing selected 2,
No file chosen `fw`, Unsaved changes 2, Copied to the clipboard 2, Saved
{0} 5, Opening {0} 4, Could not read {0} 4 `fw`, Could not write {0} 3
`fw`, Could not save {0} 3, Cannot open file {0} `fw`, Not saved: {0} 3,
Do you want to save the changes to {0}?, {0} could not be opened, Are you
sure you want to delete {n} item / {n} items, The two passwords do not
match. 2, Version `fw`.

**Nouns and column headers.** Name 13 `fw`, Size 7 `fw`, Type 5 `fw`, Date
2, Modified 2, Created, Path 2 `fw`, Folder 3 `fw`, Folders 2, File 4
`fw`, Files 2, Title 4 `fw`, Status 5, State 6, Details 4 `fw`, Description
2, Value 2 `fw`, Category 3, Group 5, Tags, Account 2, Accounts 3, Password
3, Username 2, Email 3, Address 2, Device 2, Devices 3, Network 2 `fw`,
History 2, Text 9 `fw`, Image 2 `fw`, Images 6, Documents 2 `fw`, Pictures
4, Downloads 4, Music 2, Videos `fw`, Desktop 3, Home 5, Library 4,
Computer 2, This computer 2, Untitled 4, New Folder, Move to Trash 2, All
files 10 `fw`, Properties, Location 2 `fw`, Comment `fw`, Icon `fw`,
Application `fw`, Program `fw`, Link 3 `fw`, Font 3 `fw`.

**Layout and formatting.** Left 5 `fw`, Right 4 `fw`, Top 5 `fw`, Bottom
4 `fw`, Centre 4, Middle 2, Justify `fw`, Width 2, Height 2, Bold 3 `fw`,
Italic 3 `fw`, Underline 2 `fw`, Colour 4 `fw`, Black 3 `fw`, White 2, Red
`fw`, Green `fw`, Blue `fw`, Yellow `fw`, Opacity 2, Brightness 3 `fw`,
Display 5, Sans 3 `fw`, Serif 2, Monospace 3, Regular `fw`, Fill `fw`,
Background `fw`, Style `fw`, Alignment `fw`.

**Units and time.** {n} byte / bytes, KB, MB, GB, TB, {n} second /
seconds, {n} minute / minutes, {n} hour / hours, {n} day / days, Today,
Yesterday, Last seen 2, Time 2. The month and weekday names the survey
found in 4 applications are not entries: a date is formatted through §6,
not assembled from translated words.

About 330 entries. The framework's own dialogs add the strings only they
show - the file dialog's view names and settings, the colour picker's
labels, the spell-check menu's "Add to dictionary", the paper sizes, the
media viewer's controls - which the extraction script (§8) collects from
`core/` into the same file; the survey counts 311 candidates there, of
which perhaps 150 are interface text.

### A.2 What this means for the applications

After the standard set, an application's own set is what the survey's
long tail says it is: UltraMail's mail words, UltraPaint's and
ArtCreator's tool and layer words (which the two share - a candidate for
a `UltraCanvasGraphics` library set registered with `AddSet`, since both
draw with the same engine), UltraFIBU's bookkeeping, SmartHomeCenter's
devices. For the median application that is 40 to 80 entries; for
UltraMail about 150. The half-day per application in §9 rests on that.

## 11. Related

- `UltraCanvasHostThemeProposal.md` - the shared settings watcher, UOS-Settings Appearance, and the host font a CJK or Thai language needs (the bundled Ubuntu family has neither script).
- `UltraCanvasEmojiSettings.md` - the shared-setting pattern this follows.
- `UltraCanvasSettingsFolder.h`, `UltraCanvasDataFolder.h` - the folders.
- `UltraCanvasJSON.md` - the catalogue parser.
- `UltraCanvasTreeViewExamples.md` (*Columnar Layout: UltraCanvasColumnsTreeView*), `UltraCanvasListSortFilterProxy.md` - the editor's grid.
- `UltraCanvasMenuExamples.md` - `commandId` and the registry a translated menu is rebuilt from.
- `AGENTS.md`, *One shared core, on every platform* - the module pattern UltraLocalisation follows.
