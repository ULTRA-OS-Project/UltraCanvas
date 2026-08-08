# UltraCanvasFilerWidget

The Filer folder widget displays the content of one folder. It is self-rendered
(like `UltraCanvasAlbum`), so folders with thousands of entries stay cheap: rows,
tiles, treemap cells, the hover icon menu and the scrollbar are all painted
inside `Render()` rather than being child elements. Entries can be dragged —
onto a folder shown in the view, or out to other windows and applications (and
dropped in from them) — and Copy / Cut / Paste go through the system clipboard
so files can be exchanged with external file managers; pasting a clipboard
image or text creates a file with that content — see
[Clipboard interop](#clipboard-interop-with-other-programs) and
[Drag & drop](#drag--drop).

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
| `Details` | Text columns: name (with mini thumbnail), size, type, modified date, created date, attributes and an info column (play duration via `infoProvider`, compression factor of archive-compressed entries). Column headers are clickable and toggle the sort, and every column can be resized by dragging the splitter on its right edge — see [Resizable columns](#resizable-columns). |
| `List` | Compact icon + name entries flowing top-to-bottom into columns (horizontal scrolling). The column width is draggable — see [Resizable columns](#resizable-columns). |
| `ThumbnailsSmall` / `ThumbnailsMedium` / `ThumbnailsBig` / `ThumbnailsMaximized` | Thumbnail grids with growing tile sizes. Images and SVGs show their real bitmap (via the shared `UCImage` cache); images larger than the tile are scaled down to fit, while images already smaller than the tile keep their original size (centered) instead of being upscaled. Video files show their **poster frame** (a frame from a short way into the clip, grabbed via `CaptureVideoThumbnailPixmap`) when a video backend is available — without one the capture fails once and the tile keeps its glyph. Other files draw a category-colored glyph with their extension. Thumbnails are decoded **asynchronously** on background worker threads: the folder page appears immediately (each image tile shows the generic glyph first) and tiles fill in as their decode completes, so opening a folder full of photos never blocks the window. Decoding is **viewport-driven**: only visible tiles plus a prefetch band of one screen ahead in scroll direction are ever decoded, visible tiles always decode first, and queued decodes that scroll out of range are dropped. With `SetCompressedThumbnails(true)` the finished thumbnails are additionally held QOI-compressed in memory (2–6× smaller, bit-exact) and decompressed on demand into a small hot cache while drawn; `GetThumbnailCacheStats()` exposes the footprint for comparison. Tiles are square by the selected edge, so a row of landscape photos would leave a wide empty band above and below each image; by default (`SetShrinkThumbnailRows(true)`) a grid row whose thumbnails **all** display shorter than the tile edge is shortened to the tallest image actually shown in it, while any row that contains a full-height item (a folder, a glyph file, a vector/portrait/square or not-yet-measured image) keeps the full edge. The natural image sizes are read from file headers (no decode) on the same background worker as the folder statistics and cached, so a folder of photos lays out and appears immediately — every row starts at the full edge and shortens as its measurements land. Set it to `false` for a strict square grid. |
| `BarSize` | One row per entry with a bar proportional to its size (directories use a recursive size computed asynchronously on a background worker, capped for safety; bars reflow as the walks complete). The name column and the size label column are draggable — see [Resizable columns](#resizable-columns). |
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

## Resizable columns

The three column-based views carry draggable splitters between their columns.
They are `UltraCanvasSplitPane` dividers in look and feel — the style is a
`SplitPaneStyle` (`FilerStyle::columnSplitter`), the hovered and dragged
divider highlight the same way, the cursor turns into the `SizeWE` resize
cursor, and a drag re-splits the two neighbouring columns instead of moving the
whole table. They are painted by the widget itself (like the rows, the hover
icon menu and the scrollbar) rather than being child elements, so a folder with
thousands of entries stays as cheap as before.

| View | Splitters | Dragging one |
|---|---|---|
| `Details` | On the right edge of every column, inside the header strip (the rows stay fully clickable). A guide line follows the drag down the entries. | Moves width between that column and the one after it, so the table keeps spanning the widget. The Name column is the flexible one: it absorbs whatever the others leave, so dragging the first splitter widens or narrows the name. |
| `List` | In every gap between the flowing columns, full height. | Re-widths **all** columns (the list columns are uniform) and reflows the entries. |
| `BarSize` | Between the name column and the bar, and between the bar and the size label, full height. | Resizes the name / label column; the bar takes what is left between them. |

Widths survive rescans, sorting and view switches, and can be driven from code
— e.g. to restore a layout the application saved:

```cpp
filer->SetDetailsColumnWidth(FilerDetailsColumn::Size, 110);
filer->SetDetailsColumnWidth(FilerDetailsColumn::Info, 200);
int nameWidth = filer->GetDetailsColumnWidth(FilerDetailsColumn::Name);
filer->ResetDetailsColumnWidths();       // back to the built-in widths

filer->SetListColumnWidth(260);          // == FilerStyle::listColumnWidth
filer->SetBarSizeNameColumnWidth(180);
filer->SetBarSizeValueColumnWidth(0);    // 0 = auto (fits the widest size)

filer->onColumnWidthsChanged = [] { /* persist the layout */ };
filer->SetColumnResizeEnabled(false);    // fixed columns
```

`FilerDetailsColumn` names the Details columns left to right: `Name`, `Path`,
`Size`, `Type`, `ModifiedDate`, `CreatedDate`, `Attributes`, `Info`. `Path`
(the entry's containing folder) is only shown while a file list is displayed —
see *File list (search results)* below — so a normal folder display has the
columns it always had. Columns cannot be
dragged below a usable minimum (44 px, 120 px for the name).
`onColumnWidthsChanged` fires when a drag ends and on every programmatic change.

## Long names

In the tile-shaped views — the four thumbnail grids and the treemap — the space
a name gets is only as wide as the tile, which is far less than most file names
need. A name that does not fit therefore **wraps onto the next line** instead of
being cut off after one:

- Lines break after a separator (space, `-`, `_`, `.`) when one sits in the back
  half of the line, otherwise at the exact character that still fits — file names
  are frequently one long "word".
- At most `FilerStyle::captionMaxLines` lines are used (**2** by default; `1`
  restores the old single-line caption).
- What does not fit even then is dropped from the **front of the last line**,
  which then opens with an `…` overflow marker, so the end of the name — its
  extension — always stays readable:

  ```
  Holiday photos
  …Rome 2024.jpg
  ```

- Tiles grow to fit: every line past the first adds `FilerStyle::captionLineHeight`
  (`0` = derived from `smallFontSize`) to the tile. All tiles of a grid row share
  the caption height of the deepest name in that row, so the grid stays aligned.
  Dataset lines (Display > Dataset) follow underneath as before.
- Treemap cells wrap into whatever height the cell has above its size line.

The row-based views (`Details`, `List`, `BarSize`) keep their single-line,
ellipsized names — their rows are fixed height and the name has a whole column
width available.

## Name tooltips

Names that do not fit the space they are drawn in are ellipsized; hovering such
a name pops a tooltip with the full name (`SetNameTooltipsEnabled`, default on).
It applies to every view — the Name column in Details, the rows in List and
BarSize, the captions in the thumbnail grids, and treemap cells too small to
show any caption at all. Names that fit are not repeated in a tooltip; in the
tile views that now includes the names that fit *after wrapping* (see
[Long names](#long-names)), so only a name still carrying the `…` overflow
marker pops one.

The hover icon-menu buttons keep their own action tooltips and win wherever the
two overlap, so in the Details view the name column describes the file while the
icon strip — which sits over the columns to its right — describes its buttons.

## Context menu

A right-click opens the file menu:

```
Open Path         (only when SetOpenPathMenuItemVisible(true) — search-result
──────────         displays; the label is configurable)
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
Extras         >  Share / Attributes / Copy path / Access
Settings
```

Notes:

- **Paste** is enabled while the filer clipboard holds entries (shared between
  all filer instances) or the system clipboard offers files, an image or text —
  see [Clipboard interop](#clipboard-interop-with-other-programs).
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

## Selection access

`GetSelectedEntries()` returns the selected entries, `ClearSelection()` /
`SelectAll()` change the selection programmatically, and
`EnsureSelectionVisible()` scrolls so the first selected entry is fully in
view. The scroll is applied against the **next** recomputed layout, so a host
that resizes the widget in the same frame — e.g. opening a preview pane that
narrows the folder display (the UltraFiler does exactly that) — can call it
right away and the entry stays visible at the new width instead of being
corrected against the stale geometry.

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
filer->Paste();               // into the current folder (unique names); a
                              // clipboard image / text becomes a new file
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

When the clipboard holds **raw data instead of files** — an image copied in a
browser or screenshot tool, text copied in an editor — Paste creates a new
file with that content in the current folder: `Pasted image.png` (extension
following the clipboard MIME type, e.g. `.bmp` on Windows `CF_DIB` data) or
`Pasted text.txt`, made unique with " (2)" style suffixes on collision. An
image wins over text when both are offered (copying a browser image also
places its URL as text). The Paste context-menu item lights up accordingly
whenever files, an image or text are available.

## Drag & drop

Press an item and move a few pixels: the entry — or the whole selection, when
the press landed inside it — is picked up. The mouse is captured for the
gesture, so even a fast flick out of the widget starts the drag instead of
losing it. `SetDragEnabled(false)` turns the whole gesture off and leaves
presses as plain clicks.

**Inside the widget** the drag is drawn by the widget itself: a badge with the
entry's icon and its name (or "N items") follows the cursor, and the folder
under the cursor is highlighted as the drop target. Dropping on it **moves**
the files into that folder — hold **Ctrl** to drop a **copy** instead. Names
that already exist there get a " (2)" style suffix, a folder cannot be dropped
into itself, and a drop anywhere but on a folder simply ends the drag. Escape
abandons the drag without moving anything.

**Leaving the widget** hands the same set over to the native OS drag (XDND on
Linux), so it can be dropped on any other window or application — another
Filer widget, another window of the same application, an external file manager,
an editor, … There the drop target performs the copy or move itself; when it
reports a move the source folder is rescanned automatically. During that part
of the drag the cursor shows whether the target accepts a copy or a move (hold
Shift to suggest a move) and Escape cancels.

A drag **never changes the selection**: what a plain press would select is
applied on the release instead, so dragging a file does not fire
`onSelectionChanged` and does not re-target a preview pane fed by it. When no
drag started, the release selects exactly as a click always did (including the
collapse of a multi-selection to the pressed item).

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
Dragging from **empty space** draws a **rubber band**: every entry the
rectangle touches becomes the selection, live while the band is dragged
(with Ctrl the rectangle *adds* to the selection held before). The band
auto-scrolls at the viewport edge, Escape abandons it (the previous
selection returns), and a press-and-release without movement keeps its old
meaning — a plain click on empty space clears the selection (a Ctrl click
leaves it alone).
Double-clicking an entry opens/activates it (folders and archives are entered,
files fire `onFileActivated`). A single click on the **name** of the entry that
is already the only selected one starts an inline rename after a short delay
(Windows style — the delay is what separates a rename click from the first
click of a double-click).

The rename editor is a real `UltraCanvasTextInput` overlaid on the item's
name, so editing behaves like any text field: the caret moves with the
arrow keys / Home / End, a click puts it at that position, Shift extends a
selection, and Ctrl+C/X/V/Z work. It opens with the base name selected (the
extension stays, Explorer-style; folders select the whole name) in the same
font size as the displayed name, commits on Enter, cancels on Esc — and a
click anywhere outside the field commits too, because the field losing the
keyboard focus ends the edit.

Escape is also the cancel key of a running item drag, a rubber-band
selection and the compress dialog. A host that binds its own window-level
Escape shortcut (the UltraFiler closes its preview pane with it) should
check `WantsEscapeKey()` first and stand back while it returns true, so
those interactions keep their cancel key.

## Directory scanning

Real directories are scanned with `std::filesystem` (times via `stat`). When the
VirtualFS module is part of the build, paths that are not real directories are
listed through `VirtualFS_ListDirectory`, so double-clicking a browsable archive
(`zip`, `7z`, `tar`, ...) descends into it transparently and archive members show
their compression factor in the Info column.

A rescan (`Refresh()`, or the automatic one after a file operation or a drop)
keeps the selection on the files that are still there — it is remembered by
path, not by row index. Files that vanished drop out of it, which is reported
through `onSelectionChanged`; every rescan also fires `onFolderRefreshed` so a
host can refresh what it shows about the folder (item counts, status bar).

## File list (search results)

`ShowFileList(paths)` displays an explicit list of paths — typically search
results — instead of the folder listing, in whatever view mode is selected.
Each path is stat-ed like a scanned entry (type, size, times, attributes), and
paths that no longer exist are skipped. The Details view adds the `Path` column
(the entry's containing folder) directly after the name, since the entries come
from different folders.

`GetPath()` keeps returning the folder shown before, so navigation state is
untouched; `SetPath()` returns to the normal folder display and
`IsShowingFileList()` reports which mode is active. `Refresh()` re-stats the
list, dropping entries that vanished.

Pair it with `SetOpenPathMenuItemVisible(true, label)`, which puts an
Open-Path item at the *top* of the context menu (followed by a separator) and
lets you name it — e.g. `"Open path (in new tab)"`. The item calls
`onOpenPath(entry)` if set, otherwise it browses the entry's parent folder.

```cpp
filer->SetOpenPathMenuItemVisible(true, "Open path (in new tab)");
filer->onOpenPath = [this](const FilerEntry& e) {
    OpenInNewTab(std::filesystem::path(e.path).parent_path().string());
};
filer->ShowFileList(matches);   // shown in the current view mode
```

## Callbacks

| Callback | Fired |
|---|---|
| `onFileActivated(entry)` | Double-click / Enter on a file |
| `onPathChanged(path)` | After `SetPath` / entering a folder or archive |
| `onSelectionChanged(entries)` | Selection changed |
| `onFolderRefreshed()` | After every (re)scan of the shown folder — the listing changed (file operation, drop, rename, `Refresh()`) |
| `onViewTypeChanged(viewType)` | View switched (API or Display > Type) |
| `onSortChanged(field, ascending)` | Sort changed (API, menu or header click) |
| `onColumnWidthsChanged()` | A column splitter drag ended, or a width was set from code |
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
see the `FilerStyle` struct in `UltraCanvasFilerWidget.h`. `captionHeight`,
`captionMaxLines` and `captionLineHeight` size the tile caption and its name
wrapping (see [Long names](#long-names)):

```cpp
FilerStyle s = filer->GetStyle();
s.captionMaxLines = 3;     // let tile names run over up to three lines
s.captionLineHeight = 16;  // 0 = derived from smallFontSize
filer->SetStyle(s);
```

`FilerStyle::folderIconScale` (default 1.0) shrinks the folder glyph inside a
thumbnail tile's image box, centered — e.g. 0.7 draws folders at 70% so they
read lighter next to image thumbnails. The column splitters
are styled through `FilerStyle::columnSplitter`, a `SplitPaneStyle`: thickness,
the idle / hover / drag colors, the extra grab margin around the painted strip
and `showSplitterBackground` (off paints no divider while the drag handle stays
live).

## Demo

`Apps/DemoApp/UltraCanvasFilerExamples.cpp` (Widgets > Filer) shows the bundled
`media` folder with buttons for every view type, sort field and direction, an Up
button, and a status line wired to the callbacks.
