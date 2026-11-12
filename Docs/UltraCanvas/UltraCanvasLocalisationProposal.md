# UltraLocalisation — Localisation Proposal

<!-- doc-check: std::shared_ptr<UltraCanvasMenu> menu; std::shared_ptr<UltraCanvasWindow> window; -->

Status: **Phase L1 implemented** (2026-11-02: the `UltraLocalisation`
module, `Docs/Modules/UltraLocalisation/README.md` - the three layers, the
override folder, the fallback walk, plurals, `Language.conf`, the language
choice, the start-up wiring; one difference from §3 below: the module
takes its folders and the desktop's languages as arguments, `SetFolders`
and `Initialize`, so that it stands without the core). **Phase L2
implemented** (2026-11-02): `media/locale/en/Standard.json` from Appendix A
and the framework's `Tr` sites, the sixteen base sets of Appendix B under
`media/locale/en/base/`, the framework's own dialogs wrapped (the modal
dialogs, the file dialog, the filer widget, the colour, time and
new-document dialogs, the media viewer - the remaining framework strings
are wrapped as their files are touched), and `extract_ui_strings.py`,
`check_locale_catalogues.py` with `locale-catalogues.yml`, and
`wrap_ui_strings.py`. **Phase L3 implemented** (2026-11-02): the
`ultralocalisation-draft` tool over UltraAI's translator with
`translate_catalogue.py` driving it per language and set, the first
drafts of the standard and base sets in de, fr, es, it, nl, pt, ja and
zh-Hans (machine drafts, marked so until reviewed), and the UOS-Settings
Language page with the translator's name; on 2026-11-03 twenty-two more
drafts followed (ru, uk, pl, cs, sv, da, nb, fi, tr, el, ko, zh-Hant, ar,
he, hu, ro, hi, id, vi, th, ca, and pt-BR as a variant of pt holding only
what differs). **L4 implemented 2026-11-03**: `UltraCanvasLocalisationEditor` ([`UltraCanvasLocalisationEditor.md`](UltraCanvasLocalisationEditor.md)) - the merged grid over the columns tree view with a column per set, the Sets picker, the four merge operations, the two save paths, the compare column, the drafting button (through a drafter the host wires), `ShowLocalisationEditorDialog`, the UOS-Settings Translations page, the DemoApp page, `LocalisationEditorTest`; the one departure from §5 is that a cell is edited in the row under the grid rather than in place, since the columns tree view has no in-place editing. **L5 in progress** (one pull request per application, paired with the host theme proposal's T6 review of its surfaces): UltraFiler, UltraTexter, UltraMail, UltraPassword and UOS-Settings have their texts through `Tr`, their own set and the Translate entry; the rest follow in the order below. L6 is still to do. Revised 2026-11-02: the module is
`UltraLocalisation`, it loads a **standard set** UltraCanvas ships for every
application, the **base sets** of the application's type (video editing,
banking, shopping, ...) and the application's **own set** over them, and a
**localisation editor** shows the sets in one view and merges them. The companion document
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
   a catalogue of its own. **Base sets** come with it too, one per kind of
   application - the words every video editor, every shop, every banking
   program needs - which an application takes by naming them. Its own set
   holds what is its own, and may amend the standard or a base where its
   wording differs.
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
// first, then its base sets in the order it names them, then the standard
// set, then `source` itself, so an untranslated string is always shown,
// never empty.
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
enum class LocalisationOrigin { Application, Base, Standard, Source };

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

    // Loads the standard set, the base sets the application takes (§4:
    // the "@bases" of its catalogue, or UseBaseSets) and the application's
    // set for the language, the settings-folder override of each over the
    // shipped file. Called by Initialize() with the name it was given
    // ("UltraMail"); an application calls it again to switch at run time,
    // and every change handler runs.
    static bool Load(const std::string& language, const std::string& application);
    // The base sets the application takes, most specific first, for an
    // application that has no catalogue yet or decides at run time
    // ({"Video", "Audio", "Image"}); the catalogue's "@bases" otherwise.
    static void UseBaseSets(const std::vector<std::string>& bases);
    static std::vector<std::string> BaseSets();
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
`Tr` hashes the source once, looks in the application's map, its base
sets' and then the standard one, and returns a copy - a miss costs one
probe per set, three or four for a typical application. Elements store the translated string
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

### Base sets by application type

Between the standard set and an application's own there is a layer the
survey (Appendix A) makes plain: UltraPaint and ArtCreator share forty
words of layers and tools that no other application has; a video editor,
a shop and a bookkeeping program each have a vocabulary of their own
that every other program of that kind shares. A **base set** is that
vocabulary, shipped with the framework like the standard set, translated
once, and taken by an application by name:

| Base set | For | In the tree today |
|---|---|---|
| `Computer` | system tools: files, drives, devices, processes, network, settings | UltraFiler, DeviceExplorer, UltraCleaner, UOS-Settings, UltraNetMonitor, UltraDesktop, UltraClipboard |
| `Web` | browsers, site builders, web publishing | UltraWeb, UltraPress |
| `Audio` | audio editing and playback | UltraVideo's audio, the audio player and recorder elements |
| `Image` | raster and vector image editing | UltraPaint, ArtCreator, UltraViewer |
| `Video` | video editing | UltraVideo |
| `Text` | text editors, word processors, code editors | Texter |
| `Spreadsheet` | spreadsheets | the spreadsheet element and its formula engine |
| `Presentation` | slide decks | the presentation reader, the slideshow element |
| `Planning` | tasks, calendars, Gantt and Kanban | the Gantt, Kanban and calendar elements |
| `CAD` | technical drawing and modelling | the vector canvas and the Bezier editor |
| `Banking` | accounts, payments, invoices, bookkeeping | UltraFIBU |
| `Games` | games: play, score, levels, controls | - |
| `Shopping` | shops and online stores | UltraPress's shop |
| `Fashion` | clothing: collections, sizes, fabrics, care | - |
| `Communication` | mail, chat, social, assistants - a set the tree asks for | UltraMail, EmailCleaner, UltraSocial, UltraClaude, UltraAI |
| `Security` | passwords, vaults, authenticators - likewise | UltraPassword, UltraAuthenticator |

The first fourteen are the kinds named for the framework; the last two
are what the survey adds, since seven of the applications in the tree
are one or the other. Appendix B drafts each set.

An application names the base sets it takes in its catalogue, most
specific first, and the lookup runs its own set, then those, then the
standard set:

```json
{
  "@language": "en",
  "@bases": ["Video", "Audio", "Image"],
  "Fade out the audio": "Fade out the audio"
}
```

UltraVideo takes `Video`, `Audio` and `Image`; UltraPress takes `Web` and
`Shopping`; UltraPaint takes `Image`; a program with no catalogue yet
says the same in code with `UseBaseSets`. A word in two of an
application's base sets is taken from the first named, so a video editor
whose "Track" is the timeline's track lists `Video` before `Audio`; the
check (§8) reports every such collision once, so the two sets converge
on one translation where they mean the same thing.

Three rules keep the layers honest:

- **A base set holds what its kind of application shares and the
  standard set does not.** "Cut" and "Copy" are standard; "Trim" and
  "Split" are `Video` and `Audio`; "Ripple delete" is `Video`. An entry
  that is in a base set and the standard set with the same translation
  is a duplicate the check reports, as between an application's set and
  the standard one.
- **A base set's source is English**, like the standard set's, whatever
  the first application of its kind was written in - UltraFIBU's interface
  is German today, so its own set is keyed on German (`"@source-language":
  "de"`, with an `en` file that is its first translation), while the
  `Banking` base it takes is keyed on English like every other.
- **An application's own set amends; a base set offers.** An application
  that wants "Delete" to read "Remove from album" overrides it in its own
  set, never in the base, which other applications take as it is.

What the editor (§5) does with these - showing them in one view,
choosing which an application takes, moving an entry up into a base,
building a base from two - is the *merge* the sets exist for.

### Where the files live

Two places, read in this order; the first hit for a key wins, the
application's set before its base sets before the standard one within
each:

```
<UltraCanvasSettingsFolder>/locale/<lang>/<App>.json          a person's own, or a translator's draft
<UltraCanvasSettingsFolder>/locale/<lang>/base/<Base>.json
<UltraCanvasSettingsFolder>/locale/<lang>/Standard.json
<GetResourcesDir()>media/locale/<lang>/<App>.json             shipped with the build
<GetResourcesDir()>media/locale/<lang>/base/<Base>.json
<GetResourcesDir()>media/locale/<lang>/Standard.json
```

| Path | Example |
|---|---|
| The standard set | `media/locale/de/Standard.json` |
| A base set | `media/locale/de/base/Video.json` - under `base/`, so no application name can collide with one |
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

### One view, all the sets

The grid shows the application's set, the base sets it takes and the
standard set together, one row per key, with where each text comes from
visible in the row. The **Sets** picker on the toolbar lists them in
lookup order - the application, its bases, Standard - with a checkbox
each; unticking one hides its column, and reordering the bases there
reorders the application's `@bases`:

| Key (English) | Context | Standard | Video | Audio | Application | Shown | Status |
|---|---|---|---|---|---|---|---|
| Save | | Speichern | | | | Speichern | standard |
| Trim | | | Trimmen | Beschneiden | | Trimmen | base: Video (Audio differs) |
| Delete | | Löschen | | | Aus dem Album entfernen | Aus dem Album entfernen | **override** |
| Fade out | | | Ausblenden | Ausblenden | | Ausblenden | base |
| Fade out the audio | | | | | | Fade out the audio | **untranslated** |
| Open | button | Öffnen | | | | Öffnen | standard |
| {n} file | | one: {n} Datei / other: {n} Dateien | | | | | standard, plural |

Rows are grouped - *This application*, *From its base sets*, *From the
standard set* - and, when "Show the whole set" is on for a base or for
Standard, that set's unused entries too. They are filtered by the
search field and by status (untranslated, machine drafts, overrides,
collisions between bases, changed since the shipped file). The
**Application** column is always editable. A base column and the
**Standard** column are read-only for an application's translator and
editable when the toolbar's "Edit shared sets" is on, because a change
there reaches every application that takes the set. Typing in the
Application cell of an inherited row makes it an override; clearing it
removes the override and the row is inherited again. A plural row opens
its categories as child rows, one per form the language's rule needs, so
a missing `few` is a visible empty cell.

### Merging

The sets are merged in the editor, not in files, by four operations on
the toolbar and the row menu:

- **Take a base set** - the Sets picker: tick `Audio` and the
  application's `@bases` gains it, its column appears, and every row the
  application had that `Audio` already translates shows as a duplicate to
  drop.
- **Promote to a base** - a row of the application's set that belongs to
  its kind ("Ripple delete" written in UltraVideo's set) is moved into a
  base set the application takes, in every language the application has
  it in; the application's row becomes inherited.
- **Demote to the application** - the reverse, for a word a base set
  carries that only one application wants.
- **Merge two base sets** - "Audio" into "Video" for a product that
  wants one, or a new base from two: the entries of both, collisions
  listed for a decision each, written as a new file under `base/`.

Every operation is a change to files the person can see, saved by the
rules below, and reversible with Undo while the editor is open.

A second language column pair can be shown beside the first ("Compare
with: en" by default, so the English is always in sight; "fr" to
translate from a sister language), which is the whole of what a
translator usually wants from a two-pane tool.

The rows come from the sets' `en` catalogues, merged by key: the
application's (every key its sources use, from §8's extraction, and its
`@bases`), each base set's, the standard one's, and the files of the
chosen language for each in both folders. Keys the application uses that
a base or the standard set has are the inherited groups; that is
`OriginOf`, applied to files rather than to the running process, so the
editor can show an application that is not running.

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
with the existing `// locale-ok:` marker. The platform gives a date's
*shape* - the order of day, month and year, the separators, the clock -
but its month and weekday *names* are the standard set's `month|May`,
`month-short|May` and `weekday|Mo` entries in the chosen language, which
the date picker shows already: the person chose the language once, and a
German interface with an English "May" because the C locale is English
would not be what they chose. So the calendar entries are not a stopgap
for L6; they are what L6 formats with. File formats keep
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
  measured against - with the keys a base set or the standard set has
  listed under `"@inherited"` rather than repeated, so the editor and the
  check know them as inherited, and `"@bases"` kept as the developer
  wrote it. For `Standard.json` it adds what the framework's
  sources use and removes nothing (§4). Generated, never edited by hand
  for an application; regenerated like `llms.txt`.
- **`check_locale_catalogues.py`** (CI, `locale-catalogues.yml`): the `en`
  catalogues are in sync with the sources; every other language's file
  parses, has no key the source lacks (stale), repeats no entry of a set
  below it with the same translation (§4), names in `@bases` only base
  sets that exist, and reports the collisions between its base sets; the
  standard set is translated whole in every language (an entry a language
  lacks fails, a regional variant counting its parent's - added
  2026-11-04, so a feature's new standard texts arrive translated); a
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
| The settings page kit: `MakePage`, `MakeButton`, `MakeText`, `MakeLabel`, `MakeChoice`, `MakeCheckbox`, `MakeLabelledRow`, `MakeSliderRow`, `AddNote`, `AddTreeNode` | the text after the id (the second argument; `MakePage`'s title and caption; `AddNote`'s and `AddTreeNode`'s third) |
| A dropdown's `AddItem`, `AddCustomButton`, `AddFilter`, `SetFilterEmptyAction`, `SetOpenPathMenuItemVisible`, `MakeFolderNodeData` | the shown text (a dropdown item's value stays) |

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
| **L2** | The standard set: the framework's own strings wrapped (file dialog, modal dialogs, alert, filer widget, colour picker, date picker, spell-check menu, tooltips, media viewer) and the curated vocabulary written into `en/Standard.json`; the sixteen base sets of Appendix B written into `en/base/`; `extract_ui_strings.py`, `check_locale_catalogues.py` with its baseline, `wrap_ui_strings.py` | core, scripts | 6 days (every application gains translated dialogs and the vocabulary at once) |
| **L3** | `translate_catalogue.py`; the standard set and the base sets drafted in de, fr, es, it, nl, pt, ja, zh-Hans, the standard set reviewed; UOS-Settings Language page | scripts, UOSSettings | 3 days |
| **L4** | The editor element: the merged grid over the columns tree view with a column per set, the Sets picker, the four merge operations, the two save paths, the compare column, the drafting button; its dialog opener; the UOS-Settings Translations page; the DemoApp page; `UltraCanvasLocalisationEditor.md`. **Implemented 2026-11-03.** | core, UOSSettings, DemoApp | 7 days |
| **L5** | The applications, one PR each so a reviewer sees one app's diff: wrap, extract, name the bases, draft, build, run; the Help menu item. Order by use: UltraFiler, Texter, UltraMail, UltraPassword, UOSSettings, UltraDesktop, then the rest; DemoApp last, or its demo pages left English (they are the catalogue of the elements, read by developers) | Apps | ½ day per application - most of an application's strings are standard or base after L2 - ~15 days for 29 |
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
- **One large standard set with everything in it** rather than base
  sets: a shop's "Add to basket" and a video editor's "Ripple delete" in
  every application's lookup, 2,000 entries to translate before the
  first application ships in a language, and no way for a translator who
  knows bookkeeping to take the bookkeeping words alone. Base sets are
  translated by kind and taken by name.
- **Base sets as tags on standard entries** (`"Trim": {"text": ...,
  "sets": ["Video", "Audio"]}`): one file, but a translator of `Audio`
  edits the whole standard file, the editor's "take a set" becomes a
  filter rather than a choice, and an application cannot order two sets
  that disagree.
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
`fw` marks the framework's own dialogs and widgets. Appendix B drafts the
base sets the same way. An entry with no
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
Yesterday, Last seen 2, Time 2, and the month and weekday names the
survey found in 4 applications as context entries - `month|May`,
`month-short|May`, `weekday|Mo` - which the date picker shows and §6's
date formatting uses for the names, while the platform gives the order
and the separators.

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

## Appendix B. The base sets, drafted

One list per base set: the vocabulary of that kind of application that
the standard set does not already hold (a base set never repeats
"Cancel", "Delete" or "Name"). Where the tree has applications of the
kind, the list was read against their strings with
`scripts/survey_ui_strings.py` and the words two of them share are
marked `2`; the rest is the vocabulary every program of the kind carries,
written here so the first application in a language is not the one to
discover it. Each is a draft of 60 to 110 entries for `en/base/<Base>.json`;
the editor's promote and demote (§5) are how they settle.

**Computer** - Applications 2, Processes, Services, Drives, Drive, Add a
drive, Partition, Volume, Mount, Unmount, Eject, Format..., Free 2, Free
space, Used space, Capacity, Permissions, Owner, Read-only, Hidden files,
Show hidden files, Archive, Compress..., Extract..., Shortcut, Symbolic
link, Trash, Empty Trash, Restore, Recent, Favourites, Bookmarks, Printer,
Scanner, Camera, Keyboard, Mouse, Monitor, Resolution, Battery 2, Power,
Sleep, Restart, Shut Down, Log Out, Lock Screen, Wallpaper 2, Taskbar 2,
Notifications, Clipboard, Clipboard history, Clear clipboard history 2,
Pin 2, Unpin 2, Pinned 2, Processor, Memory, Storage, Graphics, Wi-Fi,
Ethernet, Bluetooth 2, Connected, Disconnected, IP address, Port,
Protocol, Host, Download 2, Upload 2, Install 2, Uninstall, Update,
Updates available, Up to date, Scan 2, Not scanned yet 2, Clean, Cache,
Caches, Logs, Temporary files, Operating system 2, Administrator, This
computer 2, Computer 2, User, Users, Location 2, Recording 2, System 2,
Address 2, History 2, Resume 2.

**Web** - Address, Go, Reload, Home page, New tab 2, Close tab,
Bookmarks, Add bookmark, Downloads, Open link, Open in new tab, Copy
link, Save link as..., Save page as..., Find in page, Full screen, Reader
view, Private window, Cookies, Site data, Clear browsing data, Allow,
Block, Secure connection, Not secure, Certificate, Page, Pages, Site,
Publish, Publish..., Preview, Theme, Template, Header, Footer,
Navigation, Hero, Section, Columns, Card, Button, Call to action,
Contact, Contact form, Newsletter, Subscribe, Blog, Post, Posts, Draft,
Published, Tags, Categories, Keywords, Domain, Web host, Add a web host,
Media folder, Alt text, Anchor, Embed, Accept, Decline, Accepted files,
Choose a file to upload, Page not found, Body text, Quotation, Brand.

**Audio** - Track, Tracks, Audio track, Clip, Record, Play, Rewind, Fast
forward, Loop, Mute, Solo, Volume, Gain, Pan, Fade in, Fade out,
Crossfade, Normalise, Amplify, Silence, Trim, Split, Join, Reverse,
Speed, Tempo, Pitch, Equaliser, Bass, Treble, Compressor, Limiter,
Reverb, Delay, Echo, Noise reduction, Waveform, Spectrogram, Level
meter, Peak, Clipping, Sample rate, Bit depth, Channels, Mono, Stereo,
Left channel, Right channel, Metronome, Beat, Beats per minute, Bar,
Marker, Add marker, Input, Output, Microphone, Speakers, Headphones,
Latency, Export audio..., Import audio..., Voice-over, Music, Sound
effect, Position, Duration, Selection, Zoom to selection, Snap to grid,
Recording, Silence at start.

**Image** - Canvas, Image size..., Canvas size..., Resize 2, Crop, Rotate,
Rotate left, Rotate right, Flip horizontal, Flip vertical, Layer 2,
Layers 2, New layer 2, Delete layer 2, Duplicate layer, Merge down,
Flatten image, Move layer up 2, Move layer down 2, Opacity, Blend mode,
Mask, Selection, Deselect, Invert selection, Feather 2, Fill, Stroke,
Brush, Pencil, Eraser, Bucket fill, Gradient 2, Linear 2, Radial, Text
tool, Shape 2, Rectangle 2, Ellipse 2, Line, Polygon, Path, Eyedropper,
Zoom tool, Hand tool, Move tool, Foreground colour, Background colour,
Palette, Swatches, Hue 2, Saturation, Brightness, Contrast 2, Levels,
Curves, Exposure, Sharpen, Blur, Gaussian blur, Noise, Pixelate, Darken 2,
Lighten 2, Invert, Grayscale, Sepia, Filters, Effects, Tool options 2,
Hardness, Line width 2, Radius 2, Angle, Scale 2, Align 2, Distribute,
Grid, Guides, Rulers, Snap to grid, Transparent, Export as..., DPI,
Pixels, Round 2, Level 2.

**Video** - Project, New project, Open project, Timeline, Video track,
Audio track, Clip, Clips, Media, Import media, Media bin, Preview,
Playhead, In point, Out point, Trim, Split, Blade, Ripple delete, Move
clip, Delete clip, Duplicate clip, Mute clip, Transition, Cross dissolve,
Dissolve, Dip to black, Fade in, Fade out, Wipe, Title, Opening title,
Closing card, Lower third, Caption, Subtitles, Overlay, Overlay video,
Picture in picture, Effect, Effects, Colour correction, Colour look,
Speed, Slow motion, Freeze frame, Reverse, Crop, Letterbox, Pan, Pan
left, Pan right, Pan up, Pan down, Zoom, Stabilise, Volume, Voice-over,
Add voice-over, Music, Add music, Loop music, Marker, Add marker, Beat
marks, Frame, Frames, Frame rate, Resolution, Aspect ratio, Duration,
Timecode, Render, Export video, Export to, Format, Quality, Bitrate,
Codec, Rendering..., Exporting..., Export complete, Export failed, Export
cancelled.

**Text** - Document, New document, Plain text, Rich text, Paragraph,
Heading, Heading 1, Heading 2, Heading 3, Body, Subtitle, Quote, Code,
List, Bulleted list, Numbered list, Table, Insert table, Insert image,
Insert link, Hyperlink, Footnote, Endnote, Caption, Cross-reference, Table
of contents, Bookmark, Add bookmark, Comment, Add comment, Track changes,
Accept change, Reject change, Next change, Previous change, Find what,
Replace with, Match case, Whole word, Regular expression, Find next, Find
previous, Replace all, Go to line, Line, Column, Lines, Words,
Characters, Word count, Spelling, Check spelling, Dictionary, Add to
dictionary, Ignore all, Encoding, Character encoding, Line endings, Tab
size, Indent, Outdent, Line numbers, Word wrap, Syntax, Font size,
Strikethrough, Superscript, Subscript, Align left, Align right, Line
spacing, Paragraph style, Character style, Page, Page break, Margins,
Page number, Read only, Autosave, Print preview, Page setup, Left to
right, Right to left.

**Spreadsheet** - Sheet, Sheets, Insert sheet, Delete sheet, Rename
sheet, Duplicate sheet, Move sheet, Cell, Cells, Row, Rows, Column,
Columns, Insert row, Insert column, Delete row, Delete column, Row
height, Column width, Freeze panes, Merge cells, Unmerge cells, Wrap
text, Formula, Formulas, Function, Functions, Sum, Average, Count,
Minimum, Maximum, Median, Round, Lookup, Reference, Absolute reference,
Range, Named range, Names, Sort ascending, Sort descending, AutoFilter,
Criteria, Data, Validation, Conditional formatting, Number format,
General, Number, Currency, Percentage, Fraction, Scientific, Decimals,
Increase decimals, Decrease decimals, Thousands separator, Borders, Fill
colour, Font colour, Horizontal, Vertical, Chart, Insert chart, Pivot
table, Invalid value, Circular reference, Division by zero, Recalculate,
Workbook, Protect sheet, Print area, Export as CSV, Financial, Logical,
Statistical, Math, Date and time, Text functions.

**Presentation** - Presentation, Slide, Slides, New slide, Duplicate
slide, Delete slide, Slide layout, Title slide, Title and content,
Section header, Two content, Blank, Master slide, Layouts, Theme, Design,
Background, Transition, Animation, Animations, Entrance, Emphasis, Exit,
Delay, Start slideshow, From beginning, From current slide, Presenter
view, Speaker notes, Notes, Rehearse timings, Next slide, Previous slide,
End of slideshow, Black screen, Pointer, Pen, Highlighter, Slide number,
Date and time, Handout, Outline, Slide sorter, Thumbnail, Placeholder,
Text box, Picture, Shape, Chart, Table, Hyperlink, Action button, Group,
Ungroup, Bring to front, Send to back, Fit to window, Export as PDF,
Export as video, Loop continuously, Advance automatically, Every {n}
seconds.

**Planning** - Project, Projects, Task, Tasks, New task, Subtask,
Milestone, Deadline, Due date, Start date, End date, Duration, Progress,
Complete, Completed, In progress, Not started, Overdue, Today, This week,
Next week, Priority, High, Medium, Low, Assignee, Assigned to, Owner,
Team, Members, To do, Doing, Done, Backlog, Board, Card, Move card,
Archive card, Checklist, Label, Labels, Attachment, Reminder, Recurring,
Repeat, Daily, Weekly, Monthly, Yearly, Calendar, Day, Week, Month, Year,
Agenda, Event, All day, Timeline, Gantt chart, Dependency, Predecessor,
Successor, Critical path, Baseline, Estimate, Actual, Remaining, Hours,
Resource, Resources, Workload, Budget, Cost, Report, Burndown.

**CAD** - Drawing, Model, Model space, Paper space, Layer, Layers, Line,
Polyline, Arc, Circle, Ellipse, Rectangle, Polygon, Spline, Point,
Hatch, Dimension, Linear dimension, Angular dimension, Radius, Diameter,
Leader, Annotation, Block, Insert block, Explode, Group, Ungroup, Offset,
Trim, Extend, Fillet, Chamfer, Mirror, Array, Rotate, Scale, Move,
Stretch, Join, Break, Snap, Object snap, Endpoint, Midpoint, Centre
point, Intersection, Perpendicular, Tangent, Nearest, Grid, Ortho, Polar
tracking, Units, Millimetres, Centimetres, Metres, Inches, Feet,
Coordinates, Origin, Length, Angle, Area, Perimeter, Measure, Distance,
Line type, Line weight, Colour by layer, Viewport, Zoom extents, Zoom
window, Pan, Orbit, Top view, Front view, Side view, Isometric,
Wireframe, Shaded, Extrude, Revolve, Sweep, Loft, Union, Subtract,
Intersect, Plot..., Print to scale, Title block, Revision, Export DXF,
Export STEP.

**Banking** - Bank account, Balance, Available balance, Transaction,
Transactions, Transfer, Standing order, Direct debit, Payment, Payments,
Payee, Recipient, Payer, Sender, Reference, Amount, Currency, Exchange
rate, Value date, Booking date, Statement, Statements, Credit, Debit,
Deposit, Withdrawal, Fee, Fees, Interest, Interest rate, Overdraft,
Limit, Credit card, Debit card, Card number, Expiry date, IBAN, BIC,
Account number, Sort code, Routing number, Invoice, Invoices, Incoming
invoice, Outgoing invoice, Invoice number, Invoice date, Due date, Paid,
Unpaid, Overdue, Partially paid, Net, Gross, VAT, Tax, Tax rate, Total,
Subtotal, Discount, Credit note, Receipt, Receipts, Voucher, Journal,
Ledger, General ledger, Debtors, Creditors, Chart of accounts, Cost
centre, Fiscal year, Period, Opening balance, Closing balance, Reconcile,
Reconciled, Categorise, Budget, Profit and loss, Balance sheet, Cash
flow, Import statement, Book, Booked, Post, Posted, Void, Refund,
Receivable, Payable. (UltraFIBU's own set is keyed on its German source
and carries the DATEV and tax vocabulary that is Germany's, not
banking's.)

**Games** - New game, Continue, Load game, Save game, Quick save, Quick
load, Restart, Paused, Game over, You win, You lose, Level, Levels,
Stage, World, Score, High score, Best, Lives, Health, Energy, Mana,
Shield, Armour, Damage, Experience, Points, Coins, Gold, Gems, Inventory,
Equip, Unequip, Use, Drop, Pick up, Craft, Upgrade, Skills, Abilities,
Quest, Quests, Mission, Objective, Map, Minimap, Checkpoint, Respawn,
Player, Players, Player {0}, Single player, Multiplayer, Co-op, Versus,
Difficulty, Easy, Hard, Expert, Controls, Gamepad, Mouse sensitivity,
Invert Y axis, Fullscreen, Windowed, V-sync, Sound effects, Voice,
Credits, Achievements, Leaderboard, Rank, Best time, Turn, Your turn,
Opponent, Draw, Victory, Defeat, Go!, Press any key, Exit to main menu,
Quit game, Are you sure you want to quit?

**Shopping** - Shop, Store, Product, Products, Catalogue, Collection,
Search products, Price, Sale price, Regular price, Offer, Deal, On sale,
New arrivals, Best sellers, Featured, In stock, Out of stock, Low stock,
Pre-order, Quantity, Add to basket, Basket, Your basket is empty, Remove
from basket, Update basket, Continue shopping, Checkout, Proceed to
checkout, Guest checkout, Create account, Shipping, Shipping address,
Billing address, Delivery, Delivery options, Standard delivery, Express
delivery, Free shipping, Estimated delivery, Payment method, Bank
transfer, Cash on delivery, Voucher code, Coupon, Gift card, Apply code,
Shipping cost, VAT included, Place order, Order, Orders, Order number,
Order confirmation, Thank you for your order, Order status, Processing,
Shipped, Delivered, Returned, Track order, Return, Returns, Exchange,
Wishlist, Add to wishlist, Compare, Reviews, Write a review, Rating,
Customer, Customers, Supplier, SKU, Barcode, Variant, Variants, Material,
Weight, Dimensions, Stock level, Reorder, Terms and conditions, Privacy
policy, Sold out.

**Fashion** - Collection, Season, Spring/Summer, Autumn/Winter, Look,
Lookbook, Outfit, Garment, Garments, Designer, Brand, Label, Sizes, Size
chart, Size guide, XS, S, M, L, XL, XXL, One size, Fit, Slim fit, Regular
fit, Relaxed fit, Oversized, Measurements, Bust, Chest, Waist, Hips,
Inseam, Sleeve length, Shoulder, Colourway, Pattern, Print, Plain,
Striped, Checked, Floral, Fabric, Fabrics, Cotton, Wool, Silk, Linen,
Leather, Denim, Polyester, Viscose, Lining, Composition, Care, Care
instructions, Machine wash, Hand wash, Dry clean only, Do not tumble dry,
Iron on low, Tops, Bottoms, Dresses, Skirts, Trousers, Jeans, Jackets,
Coats, Knitwear, Shirts, T-shirts, Blouses, Suits, Shoes, Boots,
Trainers, Sandals, Heels, Accessories, Bags, Belts, Hats, Scarves,
Jewellery, Watches, Sunglasses, Women, Men, Kids, Unisex, Model, Sketch,
Pattern piece, Sample, Prototype, Fitting, Alteration, Hem, Seam, Stitch,
Button, Zip, Pocket, Collar, Cuff, Limited edition, Made in {0}.

**Communication** - Inbox, Sent, Drafts, Outbox, Spam, Compose, Reply,
Reply all, Forward, To, Cc, Bcc, From, Subject, Attachment 2, Attach
file, Mark as read, Mark as unread, Mark as spam 2, Not spam, Flag,
Unflag, Move to..., Delete folder 2, Add account 2, Remove account 2,
Server 2, Sign-in 2, Sign out, App password 2, Contacts, Contact, New
message, Message 2, Messages 2, Conversation, New conversation 2, Chat,
New chat 2, Sending..., Delivered, Read, Typing..., Online, Away,
Notifications, Unmute, Search messages, Newsletter 2, Unsubscribe 2,
Email address 2, Username 2, Mastodon 2, Bluesky 2, Reddit 2, Telegram 2.

**Security** - Passwords, Master password 2, Change master password 2,
Choose a master password 2, A master password is required. 2, Confirm
password, New password 2, Type it again 2, The two passwords do not
match. 2, Password strength, Weak, Fair, Strong, Lock after, Locked. 2,
Vault, Entry, Entries, New entry, Delete group 2, Website, Expires,
Two-factor code, One-time code, Authenticator, Secret key, Scan QR code,
Backup codes, Export vault..., Encrypted, Decrypt, Fingerprint, PIN,
Session, Trusted device, Remember this device, Generate 2, Show 2.

## 11. Related

- `UltraCanvasHostThemeProposal.md` - the shared settings watcher, UOS-Settings Appearance, and the host font a CJK or Thai language needs (the bundled Ubuntu family has neither script).
- `UltraCanvasEmojiSettings.md` - the shared-setting pattern this follows.
- `UltraCanvasSettingsFolder.h`, `UltraCanvasDataFolder.h` - the folders.
- `UltraCanvasJSON.md` - the catalogue parser.
- `UltraCanvasTreeViewExamples.md` (*Columnar Layout: UltraCanvasColumnsTreeView*), `UltraCanvasListSortFilterProxy.md` - the editor's grid.
- `UltraCanvasMenuExamples.md` - `commandId` and the registry a translated menu is rebuilt from.
- `AGENTS.md`, *One shared core, on every platform* - the module pattern UltraLocalisation follows.
