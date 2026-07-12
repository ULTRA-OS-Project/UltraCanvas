# UltraCanvasFilerWidget

The Filer folder widget displays the content of one folder. It is self-rendered
(like `UltraCanvasAlbum`), so folders with thousands of entries stay cheap: rows,
tiles, treemap cells, the hover icon menu and the scrollbar are all painted
inside `Render()` rather than being child elements.

```cpp
#include "UltraCanvasFilerWidget.h"

auto filer = CreateFilerWidget("my-filer", 0, 0, 900, 600);
filer->SetPath("/home/user/Documents");
parent->AddChild(filer);
```

A second factory overload sets the folder immediately:

```cpp
auto filer = CreateFilerWidget("my-filer", "/home/user/Documents", 0, 0, 900, 600);
```

## View types

`SetViewType(FilerViewType)` selects how the folder is presented:

| View type | Description |
|---|---|
| `Details` | Text columns: name (with mini thumbnail), size, type, modified date, created date, attributes and an info column (play duration via `infoProvider`, compression factor of archive-compressed entries). Column headers are clickable and toggle the sort. |
| `List` | Compact icon + name entries flowing top-to-bottom into columns (horizontal scrolling). |
| `ThumbnailsSmall` / `ThumbnailsMedium` / `ThumbnailsBig` / `ThumbnailsMaximized` | Thumbnail grids with growing tile sizes. Images and SVGs show their real bitmap (via the shared `UCImage` cache); other files draw a category-colored glyph with their extension. |
| `BarSize` | One row per entry with a bar proportional to its size (directories use a lazily computed recursive size, capped for safety). |
| `TreeMap` | Squarified treemap weighted by entry size, colored by file category. |
| `GourceTree` | Force-directed tree (Gource style) — reserved, shows a placeholder until implemented. |
| `View3D` | 3D view — reserved, shows a placeholder until implemented. |

## Sorting

```cpp
filer->SetSort(FilerSortField::Size, /*ascending=*/false);
filer->SetSortField(FilerSortField::ModifiedDate);
filer->SetSortAscending(true);
```

Fields: `Name`, `Size`, `Type`, `ModifiedDate`, `CreatedDate`. Directories always
list before files. In the Details view a click on a sortable column header
selects that field (a second click flips the direction) and the header shows a
▲ / ▼ indicator. `onSortChanged(field, ascending)` fires on every change.

## Context menu

A right-click opens the file menu:

```
Copy / Cut / Paste / Delete / Duplicate / Rename
──────────
New            >  Text, Doc, Spreadsheet, Bitmap, Vector, Audio, Video
──────────
Display        >  Sort  >  Name / Size / Type / Modified / Created + Ascending / Descending
                  Type  >  all view types
                  Icon-Menu (checkbox: the small hover icon menu)
──────────
Open with      >  applications registered via AddOpenWithApp()
──────────
Compress / Extract
──────────
Print
──────────
Open Path         (only when SetOpenPathMenuItemVisible(true) — search-result displays)
Extras         >  Share / Attributes / Copy path / Access
Settings
```

Notes:

- **Paste** is offered (enabled while the filer clipboard holds entries) so Copy /
  Cut are actually usable; the clipboard is shared between all filer instances.
- Items whose hook callback is not set (Print, Share, Attributes, Access,
  Settings, empty Open with) are shown disabled. "Copy path" has a built-in
  default (system clipboard via `SetClipboardText`).
- **Compress** packs the selection (or the whole folder) into a unique `.zip`
  alongside, **Extract** unpacks selected archives into sibling folders — both
  via `UCVFSBridge` and available when the VirtualFS module is built
  (`ULTRACANVAS_HAS_VIRTUALFS`); without it they report an error through `onError`.

## Hover icon menu

When enabled (`SetHoverIconMenuEnabled`, default on, also toggled by
Display > Icon-Menu), a small icon strip appears at the top-right of the hovered
item with Copy, Cut, Rename and Delete buttons. The glyphs are drawn as vectors,
so no icon assets are required.

## File operations

All operations are also available programmatically:

```cpp
filer->CopySelection();       // to the shared filer clipboard
filer->CutSelection();
filer->Paste();               // into the current folder (unique names)
filer->DeleteSelection();     // gated by confirmDelete when set
filer->DuplicateSelection();  // copy alongside with " (2)" style names
filer->StartRename(index);    // inline rename editor (Enter commits, Esc cancels)
filer->CompressSelection();
filer->ExtractSelection();
filer->CreateNewDocument({"Text", "txt", ""});
```

`SetNewDocumentTypes()` replaces the default New > entries; each entry may name a
`templatePath` that is copied instead of creating an empty file, and
`onNewDocument` lets the application take over creation entirely (return `true`
when handled). A freshly created document goes straight into rename mode.

## Keyboard

Enter activates (folders / archives are entered, files fire `onFileActivated`),
Delete deletes, F2 renames, Ctrl+A / Ctrl+C / Ctrl+X / Ctrl+V select all / copy /
cut / paste, and the arrow keys move the selection (grid-aware in the thumbnail
and list views). Click, Ctrl+click and Shift+click select single items, toggle,
and ranges.

## Directory scanning

Real directories are scanned with `std::filesystem` (times via `stat`). When the
VirtualFS module is part of the build, paths that are not real directories are
listed through `VirtualFS_ListDirectory`, so double-clicking a browsable archive
(`zip`, `7z`, `tar`, ...) descends into it transparently and archive members show
their compression factor in the Info column.

## Callbacks

| Callback | Fired |
|---|---|
| `onFileActivated(entry)` | Double-click / Enter on a file |
| `onPathChanged(path)` | After `SetPath` / entering a folder or archive |
| `onSelectionChanged(entries)` | Selection changed |
| `onViewTypeChanged(viewType)` | View switched (API or Display > Type) |
| `onSortChanged(field, ascending)` | Sort changed (API, menu or header click) |
| `confirmDelete(entries) -> bool` | Before deleting — return false to abort |
| `infoProvider(entry) -> string` | Per entry at scan time (e.g. media duration) |
| `onShare / onPrint / onAttributes / onAccess (entries)` | Their menu items |
| `onSettings()` | Settings menu item |
| `onOpenPath(entry)` | Open Path item (default: `SetPath(parent)`) |
| `onNewDocument(type, folder) -> bool` | New > item — return true when handled |
| `onError(message)` | Failed file operations |

## Styling

`SetStyle(FilerStyle)` controls colors (background, selection, hover, bars,
grid lines, icon-menu), fonts, row heights, thumbnail tile sizes and paddings —
see the `FilerStyle` struct in `UltraCanvasFilerWidget.h`.

## Demo

`Apps/DemoApp/UltraCanvasFilerExamples.cpp` (Widgets > Filer) shows the bundled
`media` folder with buttons for every view type, sort field and direction, an Up
button, and a status line wired to the callbacks.
