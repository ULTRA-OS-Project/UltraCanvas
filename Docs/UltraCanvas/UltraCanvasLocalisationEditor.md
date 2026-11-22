# Localisation Editor

<!-- doc-check: UltraCanvasWindowBase* window; std::shared_ptr<UltraCanvasContainer> page; -->

`UltraCanvasLocalisationEditor` shows every set an application's texts come
from in one grid - the application's own set, the base sets it takes and
the standard set - one row per key, a column per set, with where each text
comes from visible in the row. The sets are merged in the editor, not in
files: take a base set, promote a row into a base, demote one to the
application, merge two bases. A save goes into the source tree where the
catalogues are writable (a clean diff for a pull request) and into the
UltraCanvas settings folder otherwise (what a running application picks
up). Phase L4 of
[`UltraCanvasLocalisationProposal.md`](UltraCanvasLocalisationProposal.md)
§5; the module it edits is
[`UltraLocalisation`](../Modules/UltraLocalisation/README.md).

- Header: `UltraCanvas/include/UltraCanvasLocalisationEditor.h`
- Source: `UltraCanvas/core/UltraCanvasLocalisationEditor.cpp`
- Test: `Tests/LocalisationEditorTest.cpp` (target `LocalisationEditorTest`, headless)
- Demo: `Apps/DemoApp/UltraCanvasLocalisationEditorExamples.cpp` (page *Localisation Editor*)
- Hosts: UOS-Localisation, the translation editor (`Apps/UOSLocalisation`, the Window arrangement); `ShowLocalisationEditorDialog` for an application's own menu item

## Contents

- [Two layers](#two-layers)
- [The element](#the-element)
  - [In a page](#in-a-page)
  - [In a dialog](#in-a-dialog)
  - [In a window of its own](#in-a-window-of-its-own)
  - [The grid](#the-grid)
  - [Editing](#editing)
  - [Merging](#merging)
  - [Saving and exporting](#saving-and-exporting)
  - [Drafting](#drafting)
- [The workspace](#the-workspace)
- [Files](#files)

## Two layers

`LocalisationWorkspace` holds the files and knows nothing of the UI: it
opens an application and a language from a shipped locale tree
(`media/locale/`) and the settings folder's override tree, lists the rows
with their status, edits the working copies, merges the sets, undoes,
saves and exports. A test drives it on a folder of its own; a headless tool
could too.

`UltraCanvasLocalisationEditor` is the element over it: four rows of
controls - the application and the language; the search, the status filter
and the comparison language; the *Sets...* picker and the two switches; Undo,
Draft, Export and Save to the right - then a columns tree view, the edit row
under it and a status line. The rows are kept short so that each fits a
narrow page (a settings page leaves the editor about 620 px): no control
loses its label to another, and the search field is the one that gives way. It derives from
`UltraCanvasContainer` and lays itself out with the CSS flex layout, so it
fills whatever it is put in.

## The element

### In a page

```cpp
auto editor = std::make_shared<UltraCanvasLocalisationEditor>("editor");
editor->SetFolders(GetResourcesDir() + "media/locale", PathToUtf8(UltraCanvasSettingsFolder()));
editor->SetTranslatorName(LanguageSettings::Load(PathToUtf8(UltraCanvasSettingsFolder())).translator);
editor->Open("UltraVideo", "de");          // "" opens the first application listed
editor->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
page->AddChild(editor);
editor->onStatus = [](const std::string& text) { /* the status line, for a host that shows it elsewhere */ };
editor->onSaved = [](const std::string& report) { /* what was written */ };
```

`SetFolders` fills the application and language pickers from the folders:
every application with an English set (`en/<App>.json`, in either tree),
every language with a standard set, `en` first. `Open` reads the
application's bases from its catalogue (`@bases`, the language's file else
the English one) and loads the sets in lookup order.

### In a dialog

```cpp
ShowLocalisationEditorDialog(window, UltraLocalisation::CurrentApplication(), UltraLocalisation::CurrentLanguage());
```

opens the editor in a dialog window on the application's own set and the
language it runs in, with a Close button - the "Help > Translate this
application..." item of an application. The folders are the module's
(`UltraLocalisation::ShippedFolder()`, `SettingsFolder()`), the translator's
name comes from `Language.conf`.

### In a window of its own

```cpp
auto editor = std::make_shared<UltraCanvasLocalisationEditor>("editor", LocalisationEditorLayout::Window);
editor->SetFolders(GetResourcesDir() + "media/locale", PathToUtf8(UltraCanvasSettingsFolder()));
editor->Open("", "de");
editor->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
window->AddChild(editor);
```

The editor places the same parts in one of two arrangements, chosen when it
is made (`LocalisationEditorLayout`):

| Arrangement | For | Where the parts go |
|---|---|---|
| `Compact` (the default) | a page, a dialog - about 620 px and up | four rows of controls over the grid (what is edited; search, *Show*, *Compare with*; *Sets...* and the two switches; the actions on the right), the edit on one row under the grid, the status line |
| `Window` | a window of its own (UOS-Localisation, 900 px and up) | two rows across the width - the application, the language, *Compare with* and the actions; the search, *Show*, *Sets...* and the switches - then the grid beside a 380 px panel, the status line under both |

The `Window` arrangement's panel shows the selected row whole, wrapped
rather than cut to a column: the key, its English (the category's form for
a plural form), the comparison text, what the application shows with its
status, and every set's text in lookup order. The edit is at the panel's
foot - the set it writes to, the field, *Apply* and *Clear override*;
Enter applies too, and so does leaving the field.

### The grid

One row per key the application uses: its own entries, and the inherited
ones its extraction lists (`@inherited`). The columns:

| Column | What it shows |
|---|---|
| Key (English) | the source text; the key with its context is the row's tooltip |
| Context | the context of a `TrC` key (`button` of `button|Open`) |
| one per set, in lookup order | that set's text in the language; grey where the set is not the one that answers |
| Compare (*lang*) | the text in the comparison language, English by default |
| Shown | what the application shows: the first set in lookup order that has the key, else the English in red |
| Status | where it comes from and what is wrong with it |

The statuses: *application* (its own word), *override* (the application
translates a key a set below also has), *duplicate: drop the override*
(with the same text), *base: Video*, *base: Video (Audio differs)* (a
collision between two bases: the first in order is shown), *standard*,
*untranslated*; then *plural*, *draft* (the answering file is a machine
draft, or the row was drafted here) and *changed* (edited, not saved) where
they apply. A plural row opens its categories as child rows, one per form
the language's rule needs, so a missing `few` is a visible empty cell.

Rows are grouped - *This application*, *From its base sets*, *From the
standard set* - and, with *Show the whole sets* on, *Unused entries of the
shared sets* holds what the bases and Standard carry that the application
does not use. The search field matches the key, the texts and the compared
text; the *Show* list filters by status (untranslated, machine drafts,
overrides, collisions between bases, changed since the shipped file).

The marks follow the look. In a light one a text from a set that does not
answer is grey, an untranslated row red and a changed one dark green; in a
dark one they are the theme's muted ink, its danger colour and a light
green, and the status line is the muted ink. A theme can name its own as
the element roles `localisation-editor.muted`, `.untranslated`, `.changed`
and `.status`. When the look changes while the editor is open, the rows are
filled again in the new colours. Every other cell is the grid's text colour
(the theme's ink).

### Editing

The edit row under the grid writes the selected row: the key, the set the
text goes into, the text. The application's set is always the target; a
base or Standard can be chosen once *Edit shared sets* is on, because a
change there reaches every application that takes the set. Enter, or
leaving the field, writes it. Typing in the application's text of an
inherited row makes it an override; *Clear override* (the button, or the
row's menu) removes it and the row is inherited again. Undo takes any
change back while the editor is open.

The same is open to code and to tests:

```cpp
editor->SelectRow("Fade out the audio");
editor->SetSelectedTranslation("Ton ausblenden");       // into GetEditSet(), the application by default
editor->SetEditSharedSets(true);
editor->SetEditSet("Standard");
editor->SetSelectedTranslation("{n} Dateien", "other");  // a plural form
editor->ClearSelectedOverride();
editor->Undo();
editor->SetSearch("trim");
editor->SetStatusFilter("untranslated");                 // "", "untranslated", "draft", "override", "collision", "changed"
editor->SetShowWholeSets(true);
editor->SetColumnVisible("Standard", false);
```

### Merging

The four operations of §5, on the *Sets...* picker and the row's right-click
menu, each reversible with Undo:

- **Take a base set** - tick it in *Sets...*: the application's `@bases`
  gains it in every language the application has a file in (the English
  first), its column appears, and every row the application had that the
  base already translates shows as *duplicate*. Untick to drop it.
- **Promote to a base set** - the row's menu, for a row of the application's
  own: moved into the base in every language the application has it in; the
  application's English lists it as inherited.
- **Demote to the application** - the reverse, for a word a base carries
  that only this application wants: the base loses it in every language.
- **Merge two base sets** - `GetWorkspace().MergeBaseSets("Audio", "Video", "Media")`
  (into one of them, or a new base): the entries of both in every language,
  the first's text on a collision, the colliding keys returned for a
  decision each; the application's `@bases` names the result.

```cpp
editor->TakeBaseSet("Audio");
editor->PromoteSelected("Video");
editor->DemoteSelected("Video");
std::vector<std::string> collisions = editor->GetWorkspace().MergeBaseSets("Video", "Audio", "Media");
```

### Saving and exporting

*Save* writes every changed file where `LocalisationWorkspace::DefaultSaveTarget()`
says, and the status line says where:

- **the source tree** when the shipped folder is writable (a checkout, a
  folder named with `SetFolders`): the whole catalogue, keys in the
  extraction's order, a plural's forms one per line, as
  `extract_ui_strings.py` writes it, so the diff is clean. The file's
  `@translated-by` gains the translator's name: a machine draft's
  "reviewed by -" becomes "reviewed by *name*".
- **the settings folder** otherwise: `<settings>/locale/<lang>/<Set>.json`
  with only the entries that differ from the shipped file (an override file
  that would be empty is removed), which a running application picks up.

`Save(LocalisationSaveTarget::SettingsFolder)` forces either. *Export...*
writes the application's merged set in the language - every row with the
text of the set that answers - as a catalogue file, or as gettext PO when
the name ends in `.po`, to a place the person chooses in the save dialog.

### Drafting

*Draft with UltraAI* fills the selected row, or every untranslated row of
the application's keys, through a `LocalisationDrafter` the host sets - the
button is shown only then - and marks the rows as drafts until a person
edits them:

```cpp
editor->SetDrafter([](const std::string& language, const std::vector<std::string>& texts,
                      std::vector<std::string>& out, std::string& error) {
    // UltraAI::CreateTranslator(...)->Translate(...) as Apps/UltraLocalisationDraft/main.cpp does
    for (const std::string& text : texts) out.push_back(text);
    return true;
});
int drafted = editor->Draft(true);
```

A plural row sends the singular for the `one` form and the plural for every
other form the language's rule needs.

## The workspace

`LocalisationWorkspace` is the model under the element, usable on its own:

```cpp
LocalisationWorkspace w;
w.SetFolders("/checkout/media/locale", "/home/me/.config/UltraCanvas");
std::vector<std::string> apps = w.Applications();     // every en/<App>.json
w.Open("UltraVideo", "de");
for (const LocalisationRow& row : w.Rows()) {
    if (row.status == LocalisationRowStatus::Untranslated) { /* row.key, row.source */ }
}
w.SetText("UltraVideo", "Ripple delete", "Ripple-Löschen");
w.TakeBaseSet("Audio");
std::string report;
w.Save(LocalisationSaveTarget::SourceTree, report);
w.ExportPo("/tmp/UltraVideo.de.po", report);
```

`Rows(wholeSets)` lists the rows as the grid shows them, with every set's
text, the one shown and its status; `Text`, `Has` and `EnglishText` read
one set; `SetText` (an empty text removes the entry), `Clear` and `AddKey`
edit; `TakeBaseSet`, `DropBaseSet`, `ReorderBases`, `PromoteToBase`,
`DemoteToApplication` and `MergeBaseSets` merge; `Undo` takes the last
operation back; `Dirty` and `DirtyFiles` say what a save would write.
`Categories()` gives the open language's CLDR plural categories.

## Files

| File | Role |
|---|---|
| `include/UltraCanvasLocalisationEditor.h` | the workspace and the element |
| `core/UltraCanvasLocalisationEditor.cpp` | both, the catalogue writer in the extraction's format, the PO export |
| `Tests/LocalisationEditorTest.cpp` | the workspace and the element on a locale tree under the temp folder |
| `Apps/DemoApp/UltraCanvasLocalisationEditorExamples.cpp` | the demo page, on a copy of the shipped catalogues |
| `Apps/UOSLocalisation/main.cpp` | UOS-Localisation: the editor in its Window arrangement, a window of its own |
| `media/locale/<lang>/<App>.json`, `base/<Base>.json`, `Standard.json` | what it edits ([the catalogue file](../Modules/UltraLocalisation/README.md#the-catalogue-file)) |
