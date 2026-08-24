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
| `ThumbnailsSmall` / `ThumbnailsMedium` / `ThumbnailsBig` / `ThumbnailsMaximized` | Thumbnail grids with growing tile sizes. Images and SVGs show their real bitmap (via the shared `UCImage` cache); images larger than the tile are scaled down to fit, while images already smaller than the tile keep their original size (centered) instead of being upscaled. Video files show their **poster frame** (a frame from a short way into the clip, grabbed via `CaptureVideoThumbnailPixmap`) when a video backend is available — without one the capture fails once and the tile keeps its glyph. PDFs show their first page, STL models a shaded render, and text / documents / spreadsheets a miniature page of their own content; each of these kinds can be switched off individually — see [Selective previews](#selective-previews). Files without (or with a switched-off) preview draw a category-colored glyph with their extension. Thumbnails are decoded **asynchronously** on background worker threads: the folder page appears immediately (each image tile shows the generic glyph first) and tiles fill in as their decode completes, so opening a folder full of photos never blocks the window. Decoding is **viewport-driven**: only visible tiles plus a prefetch band of one screen ahead in scroll direction are ever decoded, visible tiles always decode first, and queued decodes that scroll out of range are dropped. With `SetCompressedThumbnails(true)` the finished thumbnails are additionally held QOI-compressed in memory (2–6× smaller, bit-exact) and decompressed on demand into a small hot cache while drawn; `GetThumbnailCacheStats()` exposes the footprint for comparison. Tiles are square by the selected edge, so a row of landscape photos would leave a wide empty band above and below each image; by default (`SetShrinkThumbnailRows(true)`) a grid row whose thumbnails **all** display shorter than the tile edge is shortened to the tallest image actually shown in it, while any row that contains a full-height item (a folder, a glyph file, a vector/portrait/square or not-yet-measured image) keeps the full edge. The natural image sizes are read from file headers (no decode) on the same background worker as the folder statistics and cached, so a folder of photos lays out and appears immediately — every row starts at the full edge and shortens as its measurements land. Set it to `false` for a strict square grid. The grid's column count comes from the tile edge, which would leave a too-narrow-for-one-more-column strip empty on the right; by default (`SetFlexibleTileWidths(true)`) that leftover is distributed across the row Explorer-style, so the cells stretch smoothly with the window until the next column fits and the grid always fills the width. Only the cell widens (long names wrap later) — the image box keeps the square edge, centered, so resizing neither changes thumbnail sizes nor re-decodes anything. Set it to `false` for fixed-width tiles with the right-hand gap. |
| `BarSize` | One row per entry with a bar proportional to its size (directories use a recursive size computed asynchronously on a background worker, capped for safety; bars reflow as the walks complete). The name column and the size label column are draggable — see [Resizable columns](#resizable-columns). |
| `TreeMap` | Squarified treemap weighted by entry size, colored by file category. |
| `GourceTree` | Force-directed tree (Gource style) — reserved, shows a placeholder until implemented. |
| `View3D` | 3D view — reserved, shows a placeholder until implemented. |

## Scroll position across a resize

Every view reflows when the display area changes size: a thumbnail grid
re-wraps into a different number of columns, the `List` view re-columns, the
treemap is rebuilt entirely. Keeping the pixel scroll offset through that would
leave the viewport on a completely different part of the folder — in a big
folder the file the user was looking at simply disappeared when the split pane
was dragged or the host's preview pane opened or closed.

The widget therefore **re-derives the scroll offset from a reference entry**
instead of keeping it. Before the relayout it notes which entry the viewport is
anchored to and how far down (or, in `List`, how far right) the viewport's
leading edge it sits; after the new layout is built, the scroll offset is set so
that entry is back at the same place on screen, and a final reveal makes sure it
is fully visible when the reflow changed its size. The reference is

1. the **selected entry while it is on screen** — that is the file the user is
   working with, and what a host preview pane is showing; otherwise
2. the **first entry that is visible**, so the top of the display stays put.

Nothing has to be called for this: it happens inside the layout pass, for every
view type, whenever the widget's width or height changes — a window resize, a
split-pane drag, or a preview pane being added or removed next to it. A relayout
at an unchanged size (a rescan, a view switch) is left alone, because those
bring their own scroll position.

## Empty display

A display with nothing to show says so instead of staying blank: an attention
icon (a vector-drawn warning triangle, so no icon assets are required) with the
message below it, vertically centered in the folder display. A folder without
content shows **"Folder is empty!"**; an empty [file list](#file-list-search-results)
— the UltraFiler's History and Favorites tabs before anything was recorded or
pinned, a search without matches — shows **"No entries"**. A widget that never
had a folder set keeps the plain "(no folder)" text. Icon and text use
`FilerStyle::secondaryTextColor`.

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
Sorting can be switched off for a file-list display whose order matters — see
[File list](#file-list-search-results).

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
- A name that fits its lines completely is broken **balanced**, not greedily:
  the break points are chosen so the lines come out near equal instead of the
  first line taking everything that fits and leaving a stub behind:

  ```
  CoderBox              CoderBox compiler
  compiler.png     not  .png
  ```
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
                  Preview >  Bitmaps / Vector graphics / 3D / PDF / Text /
                             Docs / Spreadsheets / Videos  (checkboxes, all on)
                  Dataset >  Size / Edit date / Creation date / Attributes /
                             Length (audio/video) / Dimensions (bitmaps)
                  Icon-Menu (checkbox: the small hover icon menu)
                  Info-Bar (checkbox: the selection info bar)
──────────
Open with      >  the applications the OS registers for the selected files
                  (default app first), then entries added via
                  AddOpenWithApp(), then "Other application…" (file dialog)
──────────
Compress / Extract
──────────
Print
──────────
Extras         >  Share / Attributes / Copy path / Access
                  (plus the host's items via extrasMenuProvider — in the
                  UltraFiler: Open prompt and the Pin / Unpin submenus)
Settings
```

Notes:

- **Paste** is enabled while the filer clipboard holds entries (shared between
  all filer instances) or the system clipboard offers files, an image or text —
  see [Clipboard interop](#clipboard-interop-with-other-programs).
- Items whose hook callback is not set (Print, Share, Attributes, Access,
  Settings, empty Open with) are shown disabled. "Copy path" has a built-in
  default (system clipboard via `SetClipboardText`).
- **Open with** lists the OS-registered applications through
  [`UltraCanvasFileAssociations`](UltraCanvasFileAssociations.md) — name,
  icon, the default application first. The lookups are prewarmed on that
  service's background worker (the first widget triggers the
  association-database parse, every folder scan pre-resolves the folder's
  extensions), so opening the menu reads a cache instead of parsing anything.
  The OS section appears when the whole selection is real files on disk —
  folders and entries inside archives (virtual paths no external application
  could read) fall back to the manual entries only. `AddOpenWithApp()`
  entries keep working unchanged below the OS section, and
  `SetSystemOpenWithEnabled(false)` restores the manual-only behaviour.
  "Other application…" opens a file dialog (via `UltraCanvasFileLoader`)
  preset to the platform's application filter and directory; the pick is
  launched detached with the selected files. On platforms whose enumeration
  backend is still pending (Windows, macOS — proposal phases P2/P3) the OS
  section is empty but default-open and the picker already work.
- `SetActivateOpensWithDefaultApp(true)` makes double-click / Enter launch a
  file with the OS default application **when no `onFileActivated` callback
  is installed** — activation semantics for simple embedders; hosts with
  their own activation handling (like UltraFiler's preview) keep full
  control and call `FileAssociations::OpenWithDefaultApplication` themselves
  where they want it.
- **Compress** is a submenu of archive formats (ZIP, 7-Zip, TAR, TAR+gzip,
  TAR+bzip2, TAR+xz, TAR+Zstd). Picking one opens a modal compress dialog
  showing the archive's file-type icon, an editable file name with the chosen
  extension fixed beside it, and the destination folder as smaller text. The
  name field is a real `UltraCanvasTextInput`, like the inline rename editor,
  so it has a caret, click-to-position, selection, clipboard and undo; it opens
  with the suggested name selected, so typing replaces it. The suggestion is
  the entry's name without its file-type suffix — a folder keeps its full name
  and so does a file whose tail is not a plausible extension, which is what
  keeps version and architecture fragments (`UCDemo-Windows-0.3.27-x86_64`)
  intact. While the dialog is up it also claims the window's `KeyDown` stream,
  so the field keeps answering the keyboard even when something else in the
  window holds the focus. The icon can be dragged onto any folder in
  the view to change the destination (the target folder highlights while
  dragging); Enter / Compress creates it, Esc / Cancel dismisses. **Extract**
  opens the same dialog in extract mode: the icon shows the selected archive's
  file type with its name (or "N archives") beneath, and the editor holds the
  destination **folder** name instead — suggested from the archive's name
  without its suffix, with no fixed extension beside the field. Enter /
  Extract unpacks into that folder (several selected archives each go into
  their own subfolder of it, so their contents cannot collide), and the icon
  drag retargets the destination exactly like compressing. Both go through
  `UCVFSBridge` and are available when the VirtualFS module is built
  (`ULTRACANVAS_HAS_VIRTUALFS`); without it they report an error through `onError`.
  `CompressSelection(extension)` / `ExtractSelection()` perform the immediate
  operations for programmatic use — `ExtractSelection()` asks (Keep both /
  Extract into the existing folder / Skip) when the destination folder name
  is already taken; `OpenExtractDialog()` opens the extract dialog the menu
  uses.
- **Display > Preview** switches content previews on and off per file kind —
  see [Selective previews](#selective-previews).
- **Display > Dataset** toggles extra per-file facts drawn under the name in the
  thumbnail views: Size, Edit date, Creation date, Attributes, Length
  (audio/video duration) and Dimensions (bitmap pixel size). Each enabled field
  adds a caption line and the tiles grow to fit; Length and Dimensions only
  appear on the file kinds they apply to (their values are probed lazily from
  the file headers and cached). Drive it in code with
  `SetDatasetField(FilerDatasetField::Size, true)` / `SetDatasetFields(mask)`.

## Selective previews

A **content preview** is a tile rendered from the file itself instead of the
generic category glyph. Which kinds of file get one is selectable, and every
kind is enabled by default:

```cpp
filer->SetPreviewType(FilerPreviewType::Videos, false);   // no poster frames
filer->IsPreviewTypeEnabled(FilerPreviewType::PDF);       // true

// Only the cheap ones (a slow network share, say):
filer->SetPreviewTypes(static_cast<uint32_t>(FilerPreviewType::Bitmaps) |
                       static_cast<uint32_t>(FilerPreviewType::Text));

filer->SetPreviewTypes(kFilerAllPreviewTypes);            // back to the default
```

| `FilerPreviewType` | Menu label | Applies to | What is shown |
|---|---|---|---|
| `Bitmaps` | Bitmaps | png, jpeg, gif, webp, avif, heif, tiff, qoi, ico, bmp | the image, decoded through the shared `UCImage` cache |
| `VectorGraphics` | Vector graphics | svg, eps, cdr, xar | the rendered drawing (formats the image pipeline can rasterize) |
| `Models3D` | 3D | stl (plus obj, ply, 3ds, 3mf, gltf, glb, dae, fbx as a file category) | a shaded three-quarter view of the mesh, rasterized in software; only STL is rendered so far, the other formats keep their glyph |
| `PDF` | PDF | pdf | the first page, rendered by the PDF plugin (`ULTRACANVAS_PLUGIN_PDF`) and outlined as a sheet of paper |
| `Text` | Text | txt, log, ini, conf, json, xml, yaml, and source files | a miniature page holding the first lines of the file |
| `Docs` | Docs | odt, doc, docx, rtf, md, html, tex, epub | the same page, with odt / doc / docx read through the rich-document reader and HTML stripped of its tags |
| `Spreadsheets` | Spreadsheets | ods, xlsx, csv, tsv | the first cells of the first sheet as a small grid (xls keeps its glyph) |
| `Videos` | Videos | mp4, mkv, avi, mov, webm, wmv | the poster frame, when a video backend is available |

Notes:

- Switching a kind off repaints its entries with the type glyph immediately and
  stops the widget from opening those files at all — that is the point of the
  switches: a folder of huge photos, videos or PDFs on a slow volume stays
  browsable. Switching it back on re-uses whatever is still cached and reads the
  rest in the background.
- Previews are produced on the same background workers as the image
  thumbnails, in the same viewport-driven order (visible tiles first, then one
  screen of prefetch), so no preview ever blocks a frame. Image work has
  priority over reading text.
- Page-shaped previews (Text, Docs, Spreadsheets, PDF, 3D) are only drawn where
  a page is legible — from roughly a 40 px box up. The small icon column of the
  Details and List rows keeps the type glyph, so a folder listing does not read
  every document in it.
- `FilerPreviewType` values are a bitmask; `GetPreviewTypes()` returns the
  current set and `kFilerAllPreviewTypes` is the default. The preview kinds do
  not map one to one onto `FilerFileCategory`: PDF is split out of the Document
  category because it renders a page, and CSV / TSV count as spreadsheets
  because they preview as a grid (their file category stays `Text`).
  `UltraCanvasFilerWidget::PreviewTypeOf(entry)` reports the kind of an entry
  (`NonePreview` for folders, audio, archives and programs, which never carry a
  content preview).

### Native application icons (Windows)

On Windows, `.exe`, `.dll` and `.ico` files show the **icon embedded in the
file** — what Explorer shows — instead of the generic EXE/DLL glyph, in every
view from the Details icon column up to the largest thumbnail tiles. The icon
is extracted by the shell (`SHDefExtractIconW`, at the nearest embedded size
up to 256 px) on the same background workers as the image thumbnails, so a
folder of executables scrolls as smoothly as one of photos; a file without an
icon resource keeps its glyph. This is an icon, not a content preview, so the
Display > Preview switches do not affect it. The extractor lives behind
`UltraCanvasNativeFileIcons.h` (`NativeFileIconAvailable` /
`LoadNativeFileIconPixmap`); on other platforms it reports no icon and
nothing changes.

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

The media probes (pixel dimensions, play length / codec) run on the same
background worker, ahead of the folder walks: selecting a file — or first
painting its tile when the Length / Dimensions dataset fields are enabled —
never opens the file on the UI thread; the detail appears with the next
posted repaint, typically within a frame or two.

## Folder listing prefetch

With `SetFolderPrefetchEnabled` (default on), a low-priority worker pre-scans
the subfolders of the shown folder — one level deep — shortly after the folder
settles, so entering one of them serves its listing from memory instead of
waiting for a cold directory scan. The win is largest on network volumes and
spinning disks.

- **Idle behavior**: each batch starts after a short grace delay, and a new
  navigation drops the pending batch immediately — quick click-throughs never
  trigger wasted scans, and the folder on screen always gets the disk first.
- **Freshness**: a cached listing is used only if it is under a minute old
  *and* the folder's modification time is unchanged since the pre-scan
  (catching entries added / removed / renamed in between); anything else falls
  back to a normal scan. `Refresh()` — used after every file operation — always
  rescans and never reads the cache.
- **Bounds**: at most 24 listings / 50 000 entries are cached (oldest evicted
  first); an oversized listing is scanned but not stored — the scan still
  warms the OS metadata cache, so the real scan on entry stays fast. Cached
  listings include hidden entries, so toggling hidden files needs no rescan
  of the cache. Archives are excluded (they list through VirtualFS).

Probe results and folder statistics are cached per path and refreshed on every
rescan. Colors and the bar height come from `FilerStyle` (`infoBarBackground`,
`infoBarTextColor`, `infoBarHeight`).

## Hidden entries

`SetShowHiddenFiles(bool)` (default `false`) decides whether hidden entries are
listed; `GetShowHiddenFiles()` reads it back. What counts as hidden is the
**platform's own notion**, not just the Unix dot convention:

- **every platform** — names starting with `.`;
- **Windows** — entries carrying the `FILE_ATTRIBUTE_HIDDEN` attribute. This is
  what keeps a profile folder looking like Explorer's: the `NTUSER.DAT`
  registry hives, `AppData` and the localized pre-Vista compatibility junctions
  (`Anwendungsdaten`, `Lokale Einstellungen`, …) are all hidden by attribute,
  not by name;
- **macOS** — entries carrying the `UF_HIDDEN` file flag (`chflags hidden`),
  e.g. `~/Library`.

The attribute is read inside the single metadata call each scanned entry
already pays for (`GetFileAttributesExW` on Windows, `stat` elsewhere), so the
scan cost is unchanged. Hidden entries show an `H` in the Details view's
attributes column when displayed.

Hosts that filter paths themselves can use the same test through
`UltraCanvas::IsHiddenFileSystemEntry(path)` (`UltraCanvasUtils.h`) — the
UltraFiler's folder tree and recursive search do. The companion
`UltraCanvas::GetWellKnownUserFolders()` returns the user's Desktop /
Documents / Downloads / Music / Pictures / Videos (plus Public / Templates
where the OS defines them) resolved through the platform —
`SHGetKnownFolderPath` on Windows (follows folder redirection, e.g. into
OneDrive), the fixed home subfolders on macOS, `xdg-user-dirs` on Linux
(localized names; entries pointing at `$HOME` are disabled per the spec) — for
building an Explorer/Finder-style curated "Home" section.

## Selection access

`GetSelectedEntries()` returns the selected entries, `ClearSelection()` /
`SelectAll()` / `SelectPath(path)` change the selection programmatically, and
`EnsureSelectionVisible()` scrolls so the first selected entry is fully in
view. The scroll is applied against the **next** recomputed layout, so a host
that resizes the widget in the same frame — e.g. opening a preview pane that
narrows the folder display (the UltraFiler does exactly that) — can call it
right away and the entry stays visible at the new width instead of being
corrected against the stale geometry. A resize on its own already keeps the
view where it was, without the host doing anything — see
[Scroll position across a resize](#scroll-position-across-a-resize).

`SelectPath(path)` makes one entry of the current display the selection and
scrolls it into view, exactly as a click on it would (`onSelectionChanged`
fires); it returns `false` when that path is not among the displayed entries.
Use it to point the view at a file right after opening its folder — the
UltraFiler does that when a tile of its History view is activated.

## Hover icon menu

When enabled (`SetHoverIconMenuEnabled`, default on, also toggled by
Display > Icon-Menu), a small icon strip appears at the top-right of the hovered
item with Copy, Cut, Rename and Delete buttons. The glyphs are drawn as vectors,
so no icon assets are required.

A button acts on the hovered entry — or on the **whole selection** when the
hovered entry is part of it — and, like a drag, **never changes the selection**:
pressing Delete on a file is "delete that file", not "show me that file", so it
does not fire `onSelectionChanged` and cannot re-target (or pop open) a preview
pane fed by it. The selection only moves when the icon menu deletes it, and then
only as described under [Selection after a delete](#selection-after-a-delete).

## File operations

All operations are also available programmatically:

```cpp
filer->CopySelection();       // to the filer clipboard + the system clipboard
filer->CutSelection();
filer->Paste();               // into the current folder, with the conflict
                              // dialog on taken names; a clipboard image /
                              // text becomes a new file
filer->PasteFilesInto(folder, paths, cut, onDone);  // same paste machinery
                              // aimed at any folder (see below)
filer->DeleteSelection();     // gated by confirmDelete when set
filer->DuplicateSelection();  // copy alongside with " (2)" style names
filer->StartRename(index);    // inline rename editor (Enter commits, Esc cancels)
filer->CompressSelection();          // .zip alongside (default)
filer->CompressSelection("tar.gz");  // pick the format via extension
filer->ExtractSelection();           // into sibling folders; a taken folder
                                     // name asks Keep both / Merge / Skip
filer->OpenExtractDialog();          // the context menu's extract dialog
filer->CreateNewDocument({"Text", "txt", ""});
```

### Activating files — running applications

Double-click / Enter on a folder or archive navigates into it; on a file it
fires `onFileActivated` when the host installed one, else (with
`SetActivateOpensWithDefaultApp(true)`) Explorer semantics via
`OpenEntryWithOS(entry)` — which hosts with their own activation handling
(like UltraFiler's media preview) can also call directly for the "launch it"
part:

- On **Windows**, everything goes through the shell's "open" verb, which
  runs `.exe` / `.bat` / … and opens documents with their default
  application — Explorer behavior for free.
- On **POSIX platforms** the MIME machinery only ever *opens* files, so
  executables get their own path: a file with the execute permission whose
  content is a **native binary** (ELF — AppImages included — or Mach-O) is
  run directly, detached, with its own folder as working directory
  (`FileAssociations::ClassifyExecutable` / `LaunchExecutable`). An
  executable **script** (`#!` line) is as much a document as a program, so
  it asks — *""X" is an executable script. Run it, or open it to view its
  contents?"* — with **Run** / **Open** / **Cancel** buttons. A file whose
  execute bit is set but whose content is neither (everything on a FAT
  mount, say) simply opens with its default application.

Entries inside archives are virtual paths nothing external can read, so
activation never tries to run or open them.

### Delete problems (locked / failing entries)

A delete that runs into trouble pauses on a **problem dialog** styled like the
paste conflict dialog — two exclusive switches for the action, a scope switch,
and **Continue** / **Cancel** buttons (Cancel keeps what was already deleted
and drops the rest):

- A **write-protected (locked) entry** asks *before* the attempt —
  `"X" is write-protected.` — with **Delete it anyway** / **Skip this file**
  (Skip preselected) and a *"Do this for all remaining write-protected items"*
  scope switch. Delete-anyway lifts the protection first, so it also works on
  Windows, where a read-only file can never be removed directly.
- A **failed delete** asks *afterwards* — `"X" could not be deleted:
  Permission denied.` ("The file may be locked or in use by another
  program.") — with **Try again** / **Skip this file** (Try again
  preselected) and a *"Do this for all remaining items"* scope switch.
  A stored try-again-for-all grants each later failing entry one silent
  retry before asking again, so a stubborn entry can never loop forever.

Entries inside archives are still deleted in one batched archive rewrite
before the interactive queue; their failures are reported via `onError` as
before. When modal dialogs are unavailable the delete falls back to the old
fixed behavior (attempt everything, report failures).

### Selection after a delete

By default a delete leaves nothing selected. `SetSelectNextAfterDelete(true)`
changes that for the case where the delete takes the **whole** selection away:
the entry that fills its place inherits the selection — the first survivor
after the deleted block, or the last one before it when the deleted entry was
at the end — and the folder display scrolls it into view. Deleting entries that
are *not* selected (the hover icon menu acting on the entry under the cursor)
still leaves the selection alone, and a delete that only takes part of the
selection keeps the rest as before.

```cpp
filer->SetSelectNextAfterDelete(true);   // preview follows the deleted file's neighbour
```

Hosts that feed a preview pane from `onSelectionChanged` turn this on while the
preview is up — the UltraFiler does exactly that — so deleting the previewed
file walks the preview on to the next file instead of folding the pane away and
snapping the folder display back to full width. The new selection is installed
**before** `onFolderRefreshed` fires, so the host sees a single selection change
and never an empty one in between.

`SetNewDocumentTypes()` replaces the default New > entries; each entry may name a
`templatePath` that is copied instead of creating an empty file, and
`onNewDocument` lets the application take over creation entirely (return `true`
when handled). A freshly created document goes straight into rename mode.

The **New >** submenu opens with **Folder** — above the document kinds and set
apart from them by a separator — bound to **Ctrl+F**. `CreateNewFolder()` is the
same action programmatically: it creates `New folder` (numbered `New folder (2)`
and so on when the name is taken) in the shown folder, reports the change to
`onFolderModified` and opens the inline rename editor on it.

```cpp
filer->CreateNewFolder();   // what New > Folder and Ctrl+F do
```

## Watching the shown folder

The folder can change without the widget doing anything: another application
saves a file into it, a download finishes, a script deletes one. The widget
notices and rescans.

```cpp
filer->SetFolderWatchEnabled(false);      // on by default
filer->SetFolderWatchIntervalMs(3000);    // default 1500, minimum 250
```

A background worker re-fingerprints the folder every interval — its own
modification time folded together with each entry's name, size and modification
time — and raises a flag when the number moves. The scan never runs on the UI
thread, and only a real directory is watched (an archive interior or a file list
has no folder whose changes would mean anything).

The rescan itself is held back while the user is busy: no auto-refresh
interrupts an open rename editor, a running drag or marquee, a context menu, a
compress dialog, or a file operation waiting on its own dialog. The flag stays
set, so the refresh lands the moment the interaction ends. `onFolderRefreshed`
fires as it would for any other rescan, so a host's status bar and preview
follow along.

## Compressing and extracting

`CompressSelection()`, `ExtractSelection()` and the context menu's Compress /
Extract dialogs all run the work on a background worker behind an
[`UltraCanvasProgressDialog`](UltraCanvasProgressDialog.md): a ring with the
percentage, the file being handled, and Cancel. The UI stays live throughout —
packing a few hundred megabytes no longer freezes the window.

Cancel stops the backend at its next progress callback. A cancelled **pack**
deletes the half-written archive (nobody wants that in the listing); a cancelled
**unpack** keeps what it already wrote, because those are real files, and stops
the remaining archives of a multi-archive run.

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

### Name conflicts

When a pasted entry's name is already taken in the target folder, the paste
pauses on the **conflict dialog** — "A file named "X" already exists in this
folder." — with the choice set by three exclusive switches (the common
formulations):

- **Keep both** — the pasted entry takes the next free " (2)" style name
  (the default, and what a conflict-free paste always does)
- **Replace the existing file** — the existing entry is removed first
- **Skip this file** — the entry is not pasted

A fourth switch, **"Do this for all remaining conflicts"**, decides the scope:
off (the default) asks again on the next conflict, on applies the same choice
to every remaining conflict of this paste. **Continue** proceeds with the
chosen action; **Cancel** keeps what was already pasted and drops the rest.
Copy-pasting a file alongside its original never asks — the copy simply takes
the next free name, exactly like Duplicate.

An entry that **fails** to move or copy (locked, in use, permissions) asks too.
The dialog is titled "Cannot Move" / "Cannot Copy" and spells the failure out in
full: the operating system's own reason, the source path, the destination folder
and what usually causes it. The choice is **Try again** / **Skip this file**,
with a "Do this for all remaining items" scope switch; a stored
try-again-for-all grants each later failing entry one silent retry before asking
again. Drag & drop, inside the widget and from other applications, runs through
the same machinery, so drops get the same dialogs.

A **move, a rename and a delete** all need the file to themselves: a rename is
refused while another program still has it open — on Windows outright — and a
delete is too. The widget therefore drops the entries out of the selection
before each of those, firing `onSelectionChanged` so a host that feeds a preview
pane from the selection closes the file first. A rename puts the selection back
on the new name afterwards, and a delete hands it to the neighbour when
`SetSelectNextAfterDelete` is on, so neither leaves the user with nothing
selected.
The UltraFiler pairs this with `UltraCanvasMediaViewer::CloseFile()`, which makes
its preview release the document instead of merely stopping playback. Most
previews hold nothing open in the first place — images, text, spreadsheets,
models, e-books and PDFs up to the PDF view's memory limit are read whole — so
this only has work to do for a playing video or audio file and for a PDF too big
to hold in memory.

The same machinery is available programmatically for any target folder:

```cpp
filer->PasteFilesInto(folder, paths, /*cut=*/false,
                      [](bool changed) { /* refresh, history, ... */ });
```

With the `onDone` callback set the caller owns the post-paste work (refreshing
views, recording history) and is told whether anything changed; without it the
widget refreshes itself and reports the change to `onFolderModified`. When
modal dialogs are unavailable the paste falls back to "keep both" for every
conflict — the widget's previous fixed behavior.

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
the files into that folder by default; `SetDropOnFolderCopies(true)` makes a
plain drop **copy** them instead. Either way **Ctrl** at the drop always copies
and **Shift** always moves, so the other action is one modifier away:

```cpp
filer->SetDropOnFolderCopies(true);   // plain drop on a folder copies
```

Drops run through the same machinery as Paste, so a name that already exists there
raises the [conflict dialog](#name-conflicts) and a failing entry the retry
dialog; a folder cannot be dropped into itself, and a drop anywhere but on a
folder simply ends the drag. Escape abandons the drag without moving anything.
Files dropped **into** the view from other widgets or applications are copied
the same way, conflict dialog included.

**Leaving the widget does not end the drag.** The badge keeps following the
cursor over the rest of the window — it is handed to the window's
[drag overlay](#drag-overlay) for that, because an element cannot paint outside
its own bounds — and releasing over another element offers it the files as a
`Drop` event, exactly like a drop arriving from another application. That is
how a file reaches a second Filer pane, a folder tree or any other drop-aware
widget of the same window; a release over something that does not take the drop
just ends the drag.

**Leaving the window** hands the same set over to the native OS drag (XDND on
Linux, OLE `DoDragDrop` on Windows), so it can be dropped on any other
application — an external file manager, an editor, another window of this
application, … There the drop target performs the copy or move itself; when it
reports a move the source folder is rescanned automatically. During that part
of the drag the cursor shows whether the target accepts a copy or a move (hold
Shift to suggest a move on Linux, Ctrl/Shift on Windows) and Escape cancels.
On a platform with no native drag implementation (currently macOS) the
window-wide drag simply keeps running instead of being dropped, so the gesture
is never lost — it just cannot reach another application there.

### Drag overlay

The badge is painted through `UltraCanvasWindowBase::SetDragOverlay()`, a
window-level hook for content that has to be visible above every element:

```cpp
window->SetDragOverlay(this, badgeRectInWindowCoords,
        [this](IRenderContext* ctx, const Rect2Di& rect) { DrawBadge(ctx, rect); });
...
window->ClearDragOverlay(this);   // when the gesture ends
```

Setting it again moves it (both the rectangle it leaves and the one it enters
are repainted), the first owner keeps it until it clears it, and the renderer
draws in window coordinates.

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

A committed rename **keeps the entry selected** under its new name and scrolls
it back into view (the new name usually sorts somewhere else). The rescan that
follows a rename restores the selection by path, so the entry is followed from
its old path to its new one rather than dropped — leaving nothing selected
would silently disable every command that needs a selection, F2 and the Rename
button included, so a second rename in a row would do nothing at all. An entry
renamed while it was *not* selected — the hover icon menu acting on the entry
under the cursor — leaves the selection where it was. Renaming to a name that
differs only in case is allowed: the "already exists" check ignores a target
that resolves to the entry itself, which is what a case-insensitive filesystem
(Windows, macOS) reports for it. Renaming onto a name another entry already
holds asks — *"A file named "X" already exists in this folder. Replacing it
overwrites the existing file."* — with **Replace** / **Cancel** buttons;
Cancel keeps the old name.

The field is placed over the name wherever the name is drawn, which differs per
view: beside the icon in Details / List / Size bars, over the caption band in
the thumbnail grids, and **inside the cell, at the top**, in the treemap — a
treemap cell has no caption band under an icon, the cell *is* the icon rect.
Treemap cells are sized by their content, not by their captions, so a small
cell gets a field widened to a usable minimum (pulled back inside the right
edge when that would overflow it) rather than one a few pixels wide.

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

## Folder modifications

`onFolderRefreshed` answers "the listing changed", which includes plain
rescans. `onFolderModified(folderPath)` answers the different question "the
**user** changed something here": an entry created, pasted, dropped in or out,
renamed, duplicated, deleted, packed or extracted — through the context menu,
the icon menu, the keyboard or the API alike. Navigation, sorting, view
switches and a bare `Refresh()` never fire it.

The reported folder is normally the displayed one, but it is the folder that
actually changed when that differs — files dropped onto a subfolder shown in
the view, an archive written into the folder its dialog icon was dragged to,
or the individual parent folders when a file list spanning several folders is
deleted from. In a file-list display, changes whose folder cannot be named are
not reported at all, since the displayed folder is not where they landed.

```cpp
// "Recently worked in" — folders the user actually did something in.
filer->onFolderModified = [this](const std::string& folder) {
    recentFolders.Record(folder);
};
```

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

A file list is sorted like a folder listing by default. When the order of the
paths itself carries the meaning — a most-recently-used history, a ranked
result list — `SetFileListOrderPreserved(true)` shows them exactly as handed
over (`IsFileListOrderPreserved()` reads the flag back). Sorting is then off
for the file list: `SetSort()` and the Details column headers leave the order
alone until the widget returns to a folder listing, which is always sorted.

```cpp
filer->SetFileListOrderPreserved(true);
filer->ShowFileList(recentlyUsedPaths);   // most recent first, kept that way
```

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
| `onFolderModified(folderPath)` | The **user** changed a folder's content through the widget — see [Folder modifications](#folder-modifications) |
| `onViewTypeChanged(viewType)` | View switched (API or Display > Type) |
| `onSortChanged(field, ascending)` | Sort changed (API, menu or header click) |
| `onColumnWidthsChanged()` | A column splitter drag ended, or a width was set from code |
| `confirmDelete(entries) -> bool` | Before deleting — return false to abort |
| `infoProvider(entry) -> string` | Per entry at scan time (e.g. media duration) |
| `onShare / onPrint / onAttributes / onAccess (entries)` | Their menu items |
| `extrasMenuProvider() -> vector<MenuItemData>` | Called on every context-menu open; non-empty results are appended to the Extras submenu behind a separator, so item flags can follow host state |
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
