# UltraCanvasFilerWidget

The Filer folder widget displays the content of one folder. It is self-rendered
(like `UltraCanvasAlbum`), so folders with thousands of entries stay cheap: rows,
tiles, treemap cells, the hover icon menu and the scrollbar are all painted
inside `Render()` rather than being child elements. Files can be dragged out to
other windows and applications (and dropped in from them), and Copy / Cut /
Paste go through the system clipboard so files can be exchanged with external
file managers — see [Clipboard interop](#clipboard-interop-with-other-programs)
and [Drag & drop](#drag--drop).

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
| `ThumbnailsSmall` / `ThumbnailsMedium` / `ThumbnailsBig` / `ThumbnailsMaximized` | Thumbnail grids with growing tile sizes. Images and SVGs show their real bitmap (via the shared `UCImage` cache); images larger than the tile are scaled down to fit, while images already smaller than the tile keep their original size (centered) instead of being upscaled. Other files draw a category-colored glyph with their extension. Thumbnails are decoded **asynchronously** on background worker threads: the folder page appears immediately (each image tile shows the generic glyph first) and tiles fill in as their decode completes, so opening a folder full of photos never blocks the window. Decoding is **viewport-driven**: only visible tiles plus a prefetch band of one screen ahead in scroll direction are ever decoded, visible tiles always decode first, and queued decodes that scroll out of range are dropped. With `SetCompressedThumbnails(true)` the finished thumbnails are additionally held QOI-compressed in memory (2–6× smaller, bit-exact) and decompressed on demand into a small hot cache while drawn; `GetThumbnailCacheStats()` exposes the footprint for comparison. |
| `BarSize` | One row per entry with a bar proportional to its size (directories use a recursive size computed asynchronously on a background worker, capped for safety; bars reflow as the walks complete). |
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
Display        >  Sort    >  Name / Size / Type / Modified / Created + Ascending / Descending
                  Type    >  all view types
                  Dataset >  Size / Edit date / Creation date / Attributes /
                             Length (audio/video) / Dimensions (bitmaps)
                  Icon-Menu (checkbox: the small hover icon menu)
                  Info-Bar (checkbox: the selection info bar)
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
- **Compress** is a submenu of archive formats (ZIP, 7-Zip, TAR, TAR+gzip,
  TAR+bzip2, TAR+xz, TAR+Zstd). Picking one opens a modal compress dialog
  showing the archive's file-type icon, an editable file name, and the
  destination folder as smaller text. The icon can be dragged onto any folder in
  the view to change the destination (the target folder highlights while
  dragging); Enter / Compress creates it, Esc / Cancel dismisses. **Extract**
  unpacks selected archives into sibling folders. Both go through `UCVFSBridge`
  and are available when the VirtualFS module is built
  (`ULTRACANVAS_HAS_VIRTUALFS`); without it they report an error through `onError`.
  `CompressSelection(extension)` still performs an immediate, dialog-free
  compress for programmatic use.
- **Display > Dataset** toggles extra per-file facts drawn under the name in the
  thumbnail views: Size, Edit date, Creation date, Attributes, Length
  (audio/video duration) and Dimensions (bitmap pixel size). Each enabled field
  adds a caption line and the tiles grow to fit; Length and Dimensions only
  appear on the file kinds they apply to (their values are probed lazily from
  the file headers and cached). Drive it in code with
  `SetDatasetField(FilerDatasetField::Size, true)` / `SetDatasetFields(mask)`.

## Selection info bar

A one-line bar under the folder display (`SetSelectionInfoVisible`, default on,
also toggled by Display > Info-Bar) describes the current selection:

- **Single file** — name, type, size, modified date and attributes, plus:
  - **bitmaps**: pixel dimensions (`1920 × 1080 px`), parsed from the file
    header for PNG / JPEG / GIF / BMP / WebP / TIFF / QOI / ICO (other formats
    fall back to the shared `UCImage` cache);
  - **audio / video**: play length and codec (`3:45 · H.264`), parsed from the
    container headers of WAV / MP3 / FLAC / OGG / Opus / MP4 / M4A / MOV / AVI /
    MKV / WebM / WMV — no decoding, only a few bounded reads. When nothing can
    be probed the entry's `info` value (e.g. from `infoProvider`) is shown.
- **Single folder** — recursive file / folder counts and total size (capped at
  50 000 entries for safety; a `≥` prefix marks a capped result).
- **Multiple items** — item counts and the summed size of the selection
  (folders counted recursively).
- **No selection** — a summary of the displayed folder (entry counts + size).

Recursive folder statistics are computed **asynchronously** on a background
worker: selecting a folder shows `…` (or a `≥` lower bound for multi
selections) immediately and the exact counts fill in when the subtree walk
finishes, so clicking or opening a folder with a deep subtree never blocks the
window. The same statistics provide the directory weights of the BarSize and
TreeMap views, whose layout reflows as the walks complete.

Probe results and folder statistics are cached per path and refreshed on every
rescan. Colors and the bar height come from `FilerStyle` (`infoBarBackground`,
`infoBarTextColor`, `infoBarHeight`).

## Hover icon menu

When enabled (`SetHoverIconMenuEnabled`, default on, also toggled by
Display > Icon-Menu), a small icon strip appears at the top-right of the hovered
item with Copy, Cut, Rename and Delete buttons. The glyphs are drawn as vectors,
so no icon assets are required.

## File operations

All operations are also available programmatically:

```cpp
filer->CopySelection();       // to the filer clipboard + the system clipboard
filer->CutSelection();
filer->Paste();               // into the current folder (unique names)
filer->DeleteSelection();     // gated by confirmDelete when set
filer->DuplicateSelection();  // copy alongside with " (2)" style names
filer->StartRename(index);    // inline rename editor (Enter commits, Esc cancels)
filer->CompressSelection();          // .zip alongside (default)
filer->CompressSelection("tar.gz");  // pick the format via extension
filer->ExtractSelection();
filer->CreateNewDocument({"Text", "txt", ""});
```

`SetNewDocumentTypes()` replaces the default New > entries; each entry may name a
`templatePath` that is copied instead of creating an empty file, and
`onNewDocument` lets the application take over creation entirely (return `true`
when handled). A freshly created document goes straight into rename mode.

## Clipboard interop with other programs

Copy and Cut place the selection on the **system clipboard** in the standard
file-manager formats (`text/uri-list`, `x-special/gnome-copied-files` and the
KDE cut marker on Linux; `CF_HDROP` plus `Preferred DropEffect` on Windows) in
addition to the internal filer clipboard, so files copied in the widget can be
pasted in external file managers — and a cut there is honoured as a move.
Plain-text targets are offered too, so pasting into an editor or terminal
inserts the file paths.

Paste prefers the system clipboard (whatever was copied last, in this widget
or in another program) and falls back to the internal filer clipboard. A cut
paste moves the files; the paste of a file into the folder it already lives in
is skipped for a cut and duplicated with a unique " (2)" style name for a copy.

## Drag & drop

Files can be **dragged out** of the widget: press on a (selected) item, move a
few pixels, and the selection leaves as a native OS drag (XDND on Linux) that
can be dropped on any other window or application — another Filer widget,
another window of the same application, an external file manager, an editor,
… The drop target performs the copy or move itself; when it reports a move the
source folder is rescanned automatically. During the drag the cursor shows
whether the target accepts a copy or a move (hold Shift to suggest a move) and
Escape cancels. Pressing an item that is part of a multi-selection keeps the
selection so the whole set can be dragged; the usual collapse to a single item
happens on release when no drag started.

Files **dropped onto** the widget from other applications (or dragged over
from another Filer widget) are copied into the shown folder with unique names;
sources already inside the folder, and folders dropped into themselves, are
skipped.

## Keyboard

Enter activates (folders / archives are entered, files fire `onFileActivated`),
Delete deletes, F2 renames, Ctrl+A / Ctrl+C / Ctrl+X / Ctrl+V select all / copy /
cut / paste, Ctrl+D duplicates, Ctrl+P prints (when `onPrint` is set), and the
arrow keys move the selection (grid-aware in the thumbnail and list views). The
same shortcuts are shown next to their commands in the right-click context menu.
Click, Ctrl+click and Shift+click select single items, toggle, and ranges.
Double-clicking a file's name starts an inline rename (using the same font size
as the displayed name); double-clicking its icon opens/activates the entry. The
rename editor commits on Enter and cancels on Esc.

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
