# UOS-Localisation

## Overview

UOS-Localisation is the ULTRA OS translation editor. It ships as
`Apps/UOSLocalisation` and builds the program `UOS-Localisation`
(`-DBUILD_UOS_LOCALISATION=ON`, the default). Its version is the first line
of `Docs/UOSLocalisation/CHANGELOG.md` and shows in the window title.

It is the framework's localisation editor,
[`UltraCanvasLocalisationEditor`](../UltraCanvas/UltraCanvasLocalisationEditor.md),
in its *Window* arrangement: every set an application's texts come from - its
own set, the base sets it takes, the standard set - in one grid, one row per
key and a column per set, for any application and any language. Until
UOS-Settings 0.28 the same editor was that program's *Language > Translations*
page; UOS-Settings' *Language* page now starts this program.

## The window

| Part | What it does |
|---|---|
| First row | The **Application** and the **Language** edited, the language to **Compare with**; on the right **Undo**, **Export...** (the merged catalogue as JSON or gettext PO), **Save** |
| Second row | The **Search** (key, texts, comparison), **Show** (all rows, untranslated, machine drafts, overrides, collisions between bases, changed), **Sets...** (the base sets the application takes, the Standard column), **Show the whole sets**, **Edit shared sets** |
| The grid | One row per key, grouped by where it lives in English; a column per set, the comparison, what is shown, the status |
| The panel beside it | The selected row whole: the key, its English, the comparison, what is shown and where from, every set's text; at its foot the set the edit writes to, the text, **Apply** and **Clear override** |
| The status line | What the last action did, and where a save went |

A right-click on a row promotes it into a base set, demotes it to the
application or clears an override. A save goes into the source tree when the
catalogues are writable (a clean diff for a pull request), else into
`locale/` in the UltraCanvas settings folder, where a running application
picks it up. The editor's own doc has the details.

## Starting it

```bash
UOS-Localisation
```

```bash
UOS-Localisation --app UltraFiler --lang de
```

Without `--lang` it opens in the language chosen in UOS-Settings
(`Language.conf`), else the one it runs in; without `--app`, the first
application listed. One copy runs per user: a second start brings the first
forward, on the application and language it names when nothing is unsaved.
Closing the window with unsaved edits asks first.

The *Translator* name kept in UOS-Settings' *Language* page is written into
the catalogues it saves (`@translated-by`).

## Files

| File | Role |
|---|---|
| `Apps/UOSLocalisation/main.cpp` | the window, the command line, one copy per user |
| `Apps/UOSLocalisation/UOS-Localisation.desktop` | the freedesktop entry |
| `media/appicon/UOS-Localisation.svg`, `.png` | the icon |
| `UltraCanvas/core/UltraCanvasLocalisationEditor.cpp` | the editor |
