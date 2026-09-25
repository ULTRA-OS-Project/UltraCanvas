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
| `ThumbnailsSmall` / `ThumbnailsMedium` / `ThumbnailsBig` / `ThumbnailsMaximized` | Thumbnail grids with growing tile sizes. Images and SVGs show their real bitmap (via the shared `UCImage` cache); images larger than the tile are scaled down to fit, while images already smaller than the tile keep their original size (centered) instead of being upscaled. Video files show their **poster frame** (a frame from a short way into the clip, grabbed via `CaptureVideoThumbnailPixmap`) when a video backend is available — without one the capture fails once and the tile keeps its glyph. PDFs show their first page, STL models a shaded render, and text / documents / spreadsheets a miniature page of their own content; each of these kinds can be switched off individually — see [Selective previews](#selective-previews). Files without (or with a switched-off) preview draw a category-colored glyph with their extension, and a folder shows the first pictures inside it peeking out of the folder shape (see [Folder previews](#folder-previews)). Thumbnails are decoded **asynchronously** on background worker threads: the folder page appears immediately (each image tile shows the generic glyph first) and tiles fill in as their decode completes, so opening a folder full of photos never blocks the window. Decoding is **viewport-driven**: only visible tiles plus a prefetch band of one screen ahead in scroll direction are ever decoded, visible tiles always decode first, and queued decodes that scroll out of range are dropped. With `SetCompressedThumbnails(true)` the finished thumbnails are additionally held QOI-compressed in memory (2–6× smaller, bit-exact) and decompressed on demand into a small hot cache while drawn; `GetThumbnailCacheStats()` exposes the footprint for comparison. Tiles are square by the selected edge, so a row of landscape photos would leave a wide empty band above and below each image; by default (`SetShrinkThumbnailRows(true)`) a grid row whose thumbnails **all** display shorter than the tile edge is shortened to the tallest image actually shown in it, while any row that contains a full-height item (a folder, a glyph file, a vector/portrait/square or not-yet-measured image) keeps the full edge. The natural image sizes are read from file headers (no decode) on the same background worker as the folder statistics and cached, so a folder of photos lays out and appears immediately — every row starts at the full edge and shortens as its measurements land. Set it to `false` for a strict square grid. The grid's column count comes from the tile edge, which would leave a too-narrow-for-one-more-column strip empty on the right; by default (`SetFlexibleTileWidths(true)`) that leftover is distributed across the row Explorer-style, so the cells stretch smoothly with the window until the next column fits and the grid always fills the width. Only the cell widens (long names wrap later) — the image box keeps the square edge, centered, so resizing neither changes thumbnail sizes nor re-decodes anything. Set it to `false` for fixed-width tiles with the right-hand gap. |
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
pinned, a search without matches — shows **"No entries"**, or what the host
set with `SetFileListEmptyMessage()` (lines separated by `\n`, each centred;
`ShowFileList()` resets it). UltraFiler uses it to say which hidden folders a
search left out. A listing emptied by
the [name filter](#name-filter-filter-as-you-type) shows **"No matches for
"…""** — with the host's escalation button centered under it when one is set
via `SetFilterEmptyAction()`. A widget that never had a folder set keeps the
plain "(no folder)" text. Icon and text use `FilerStyle::secondaryTextColor`.

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

- Lines break after a separator (space, `-`, `_`, `.`) or between whole words.
  A name is only broken **inside** a word when it has to be — file names are
  frequently one long "word" — and never where that leaves a stub of
  `FilerStyle::captionBreakTolerance` characters or fewer on either side of the
  break: cutting `CoderBox` into `CoderBo` / `x` gains the line one character
  and costs a readable name.
- A name written in **PascalCase / camelCase** counts as the words it is made
  of: an upper-case letter that opens a new word — one following a lower-case
  letter or a digit, or the last capital of an acronym before a lower-case
  letter (`PDF` / `Viewer`) — is a break opportunity like a space, so the line
  ends *before* it rather than one letter later
  (`FilerStyle::captionCamelCaseBreaks`, on by default; ASCII letters):

  ```
  UltraCanvas           UltraCanva
  Texter.exe       not  sTexter.exe
  ```
- A line may run `FilerStyle::captionOverflowSlack` pixels past the caption
  width to keep a word (or a whole last line) in one piece. The caption is
  inset from the tile edge, so those pixels are free:

  ```
  Logo CoderBox         Logo CoderBo
  with text.png    not  x with text.png
  ```

- Characters beat typography: when keeping the words whole would push part of
  the name off the caption, the name is re-broken with mid-word breaks allowed
  (and the case rule off) and the version showing more of it wins.
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

## File extensions

Whether a drawn name still ends in its extension, and what a thumbnail tile
shows about the file type instead, are two independent switches
(`Display > File extensions`):

```cpp
filer->SetFileExtensionsInNames(false);                    // "UltraFiler", not "UltraFiler.exe"
filer->SetExtensionBadge(FilerExtensionBadge::Bar);        // "exe" on a strip under the icon
```

| Call | Default | Effect |
|---|---|---|
| `SetFileExtensionsInNames(bool)` | `true` | The names drawn in **every** view — Details, List, the thumbnail grids, BarSize, treemap cells and the name tooltips — keep their extension, or are drawn without it. |
| `SetExtensionBadge(FilerExtensionBadge)` | `NoneBadge` | `Bar` draws a strip across the foot of a thumbnail tile's icon box with the extension in a tag at its right end; `Icon` draws that tag alone in the box's bottom-right corner; `NoneBadge` draws neither. |

Both are **display-only**. `FilerEntry::name` always holds the real name, so
sorting, the Type column, the selection info bar, the inline rename editor and
every file operation keep working on it: a hidden extension cannot be lost by a
rename, and renaming never has to re-append one. The rename editor therefore
shows the full name (with the base name preselected, as always), which is also
what stops a user from typing a second extension onto a name that already has
one.

A name is only shortened where its tail really is a file type. The tag rule and
the name rule are the same one — `ExtensionTagOf()` answers it — so a name that
keeps its tail also gets no tile tag:

| Name | Drawn without extensions | Tile tag |
|---|---|---|
| `UltraFiler.exe` | `UltraFiler` | `exe` |
| `sources.tar.gz` | `sources.tar` | `gz` |
| `.bashrc` | `.bashrc` | — |
| `README` | `README` | — |
| `UCDemo-Windows-0.3.27-x86_64` | unchanged | — (a version, not a type) |
| `Backup.old` (a **folder**) | unchanged | — (folders have no extension) |

The tag is painted **over** the foot of the icon box, not under it, so
switching it on never changes a tile's height and never relays out the grid.
Only the four thumbnail views draw it — the row views have a Type column and a
whole row width for the name. `DisplayNameOf(entry)` returns the name as the
display draws it, for a host that labels the same entry elsewhere (a drag
badge, a breadcrumb, a tile of its own).

Changing either switch — from the menu or through the setters — fires
`onDisplayFormatsChanged`, the same hook the Thumbnails / Detail view switches
use, so an application persists both from one place (UltraFiler:
`Settings > Display > File extensions`).

## File icons

What an entry with no picture of its own is drawn with: the widget's own drawn
icons, or the ones this desktop uses for the type (`Display > File icons`).

```cpp
if (UltraCanvasFilerWidget::AreHostFileIconsAvailable())
    filer->SetFileIconStyle(FilerFileIconStyle::HostOperatingSystem);
```

| Style | What is drawn |
|---|---|
| `Simple` (default) | UltraFiler's own icons: the folder shape and the category-coloured sheet with the extension on it. Identical on every platform, and needs nothing installed — what every earlier release drew. |
| `HostOperatingSystem` | What **this** system draws for the type: the shell's icon on Windows, Finder's on macOS, the installed icon theme's on Linux and BSD, so a folder listing matches the rest of the desktop. |

The setting only governs **type** icons. A file that shows a thumbnail of its
own content keeps showing it, and a program, shortcut or bundle keeps the icon
it carries inside itself ([Native application icons](#native-application-icons))
— those are the file's own picture, and Explorer, Finder and the Linux file
managers all prefer them too. What changes is the fallback underneath: the
sheet glyph becomes the desktop's type icon, and the drawn folder shape becomes
the desktop's folder icon.

A folder the host gave an icon through `folderIconProvider`
([Folder icons](#folder-icons)) still wins over both — that is an explicit
choice about one folder. [Folder previews](#folder-previews) are drawn *into*
the built-in folder shape, so with host icons on there is no shape to draw them
into and the folder is simply the system's folder icon.

The lookups go through
[`UltraCanvasHostFileIcons.h`](UltraCanvasHostFileIcons.md) on one background
thread, and the widget caches what comes back **per type and size**, not per
file: a folder of four thousand `.txt` files performs one lookup and holds one
pixmap. Until an answer lands — and on a system that has no icon for the type,
or no desktop to ask at all — the simple icon is drawn, so the display never
waits on a lookup and never shows an empty box.

`AreHostFileIconsAvailable()` reports whether there is a desktop to ask
(false on WebAssembly and Android): a settings page should say so rather than
offer a choice that changes nothing. `RefreshHostIcons()` throws the resolved
icons away and asks again, for a host that notices the user changing desktop
theme. Switching the style fires `onDisplayFormatsChanged`, the same hook the
other Display switches use, so an application persists it from one place
(UltraFiler: `Settings > Display > File icons`).

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

Like the icon strip, the tooltip describes the file under the cursor even when
it was the view that moved: scrolling with the wheel, the scrollbar or the
keyboard re-points it at whatever name is under the pointer afterwards (see
[The hover follows the cursor, not the content](#the-hover-follows-the-cursor-not-the-content)).

## Context menu

A right-click opens the file menu:

```
Open with      >  clicking the entry opens the selection with the OS default
                  application; the submenu lists the applications the OS
                  registers for the selected files (default app first), then
                  entries added via AddOpenWithApp(), then "Other
                  application…" (file dialog)
──────────
Open Path         (only when SetOpenPathMenuItemVisible(true) — search-result
──────────         displays; the label is configurable)
Copy / Cut / Paste / Delete / Delete Permanently / Duplicate / Rename
──────────
New            >  Text, Doc, Spreadsheet, Bitmap, Vector, Audio, Video
──────────
Compress / Extract
──────────
Print
──────────
Extras         >  Share / Attributes / Copy path / Access
                  (plus the host's items via extrasMenuProvider — in the
                  UltraFiler: Open prompt and the Pin / Unpin submenus)
Display        >  Sort        >  Name / Size / Type / Modified / Created + Ascending / Descending
                  Type        >  all view types
                  File extensions > "Show in names" (checkbox) + None / Bar /
                                 Icon (the thumbnail tile tag)
                  File icons  >  UltraFiler simple / Host OS icons (only
                                 where this system has icons to give)
                  Thumbnails  >  Bitmaps / Vector graphics / 3D / PDF / Text /
                                 Docs / Spreadsheets / Videos / Audio / Fonts
                                 (checkboxes, all on; the host may append its
                                 own entry via formatListMenuProvider)
                  Detail view >  the same ten kinds (checkboxes, all on)
                  Dataset     >  Size / Edit date / Creation date / Attributes /
                                 Length (audio/video) / Dimensions (bitmaps)
                  Icon-Menu (checkbox: the small hover icon menu)
                  Info-Bar (checkbox: the selection info bar)
                  Hidden files (checkbox: hidden entries, and the full listing
                             of a curated home folder — see below)
Settings
```

Notes:

- **Paste** is enabled while the filer clipboard holds entries (shared between
  all filer instances) or the system clipboard offers files, an image or text —
  see [Clipboard interop](#clipboard-interop-with-other-programs).
- Items whose hook callback is not set (Print, Share, Attributes, Access,
  Settings, empty Open with) are shown disabled. "Copy path" has a built-in
  default (system clipboard via `SetClipboardText`).
- **Open with** is the first entry — opening a file is what the menu is
  opened for most often — and the entry itself is clickable: it opens the
  whole selection with the OS default application, the same thing a
  double-click does (a single file that is a program is run, a script asks
  first). Hovering opens the submenu as usual, so the application list stays
  one move away. The click is offered whenever the selection is real files on
  disk, `SetSystemOpenWithEnabled(false)` included — that flag only removes
  the OS-registered section of the submenu.
- The **Open with** submenu lists the OS-registered applications through
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
  launched detached with the selected files. Every desktop platform
  enumerates: freedesktop `.desktop` entries on Linux/BSD, the handlers
  Explorer lists on Windows, Launch Services on macOS 12+. Where a platform
  cannot type a file (Windows and macOS associate by extension, so a name
  without one has no candidates) the OS section stays empty and default-open
  plus the picker still work.
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
- **Display > Thumbnails** switches thumbnails on and off per file kind, and
  **Display > Detail view** does the same for the detail pane a host opens
  beside the display — see [Selective previews](#selective-previews). Both
  submenus end with the host's own entry into the per-format lists when it
  installed a `formatListMenuProvider` (UltraFiler: "File formats…", which
  opens the matching settings page).
- **Display > Dataset** toggles extra per-file facts drawn under the name in the
  thumbnail views: Size, Edit date, Creation date, Attributes, Length
  (audio/video duration) and Dimensions (bitmap pixel size). Each enabled field
  adds a caption line and the tiles grow to fit; Length and Dimensions only
  appear on the file kinds they apply to (their values are probed lazily from
  the file headers and cached). Drive it in code with
  `SetDatasetField(FilerDatasetField::Size, true)` / `SetDatasetFields(mask)`.

## Selective previews

A **content preview** is a tile rendered from the file itself instead of the
generic category glyph. Two independent sets of switches decide what a file
may show, and every kind is enabled in both by default:

- **Thumbnails** (`Display > Thumbnails`) — the tile the widget draws.
- **Detail view** (`Display > Detail view`) — whether the host may open its
  detail pane for the entry. The widget does not own that pane; it keeps the
  answer so that one setting governs both halves of a file manager's display.

```cpp
filer->SetThumbnailKind(FilerPreviewType::Videos, false);   // no poster frames
filer->IsThumbnailKindEnabled(FilerPreviewType::PDF);       // true

// Only the cheap ones (a slow network share, say):
filer->SetThumbnailKinds(static_cast<uint32_t>(FilerPreviewType::Bitmaps) |
                         static_cast<uint32_t>(FilerPreviewType::Text));

filer->SetThumbnailKinds(kFilerAllPreviewTypes);            // back to the default

// The same nine switches for the host's detail pane:
filer->SetDetailViewKind(FilerPreviewType::Videos, false);
if (filer->DetailViewEnabledFor(entry)) { /* open the pane */ }
```

| `FilerPreviewType` | Menu label | Applies to | What is shown |
|---|---|---|---|
| `Bitmaps` | Bitmaps | png, jpeg, gif, webp, avif, heif, tiff, qoi, ico, bmp | the image, decoded through the shared `UCImage` cache |
| `VectorGraphics` | Vector graphics | svg, svgz, eps, epsf, ps, ai, cdr, cdt, cmx, ccx, xar, web, wix, emf, wmf, dxf, dwg, dwt, dws, sv$ | svg / svgz rasterize through the built-in SVG renderer and eps / ps through libvips where that build has a PostScript loader; the formats a registered Vector plugin reads (dxf and the DWG family — dwg, dwt, dws, sv$ — plus emf, wmf, xar, and an ai whose artwork is in its Illustrator private data) are **drawn from the drawing itself**, read through the vector preview seam and rendered at the tile's size; Xara (xar, web, wix), the ZIP-based CorelDRAW documents (cdr, cdt from X4 on) and the PostScript formats (eps, epsf, ps) show the **preview bitmap the file carries inside itself** — see [Embedded preview bitmaps](#embedded-preview-bitmaps) — and a PDF-compatible `.ai`, which the vector reader declines because its artwork is in its PDF page, is rendered as the PDF it is. The rest (ccx, cmx, older RIFF cdr, an EPS written without a preview, and everything in an application that registered no Vector plugin) keeps its glyph |
| `Models3D` | 3D | stl always; obj, ply, 3ds, dae, fbx, x3d/x3dv/wrl/vrml, abc, ms3d, x, blend, step/stp/p21 once `RegisterModelFormatsPlugin()` has been called (plus 3mf, gltf, glb as a file category, with no reader yet) | a shaded three-quarter view of the mesh, rasterized in software — no GL context is involved, the preview projects and shades the triangles itself. A model above `kModelPreviewTriangleCap` triangles keeps its glyph rather than stalling a worker, and so does one in a format this build has no reader for |
| `PDF` | PDF | pdf | the first page, rendered by the PDF plugin (`ULTRACANVAS_PLUGIN_PDF`) and outlined as a sheet of paper |
| `Text` | Text | txt, log, ini, conf, json, xml, yaml, and every source-text extension the syntax highlighter knows (`SyntaxTokenizer::GetLanguageExtensions()`: Swift, Rust, SQL, Go, Kotlin, Java, PHP, Lua, Ruby, C#, CSS, Pascal, R, Scala, MATLAB (.m), VBA (.vba, .cls, .frm), the assemblers, ...; .bas is BASIC, and a .cls / .m whose first lines are LaTeX / Objective-C is named so; binary members of a language's list - .mat, .mlx, .svgz - excluded; the widget's table and registered plugins claim an extension first) | a miniature page holding the first lines of the file; each extension has its own switch under Text, and a source file's type is named after its language ("Swift Text") |
| `Docs` | Docs | odt, doc, docx, rtf, md, html, tex, and the e-book containers | the same page, with odt / doc / docx / tex read through the rich-document reader (a `.tex` shows its title and sections, not its markup) and HTML stripped of its tags |
| `Spreadsheets` | Spreadsheets | ods, xlsx, csv, tsv | the first cells of the first sheet as a small grid (xls keeps its glyph). The grid's column widths follow the content: a column is as wide as its widest shown cell, floored at about six characters so text stays recognizable — unless its own content is narrower (a column of one-digit values takes only what it needs). Columns that then no longer fit are clipped at the right edge instead of squeezing every column down to a letter |
| `Videos` | Videos | mp4, mkv, avi, mov, webm, wmv | the poster frame, when a video backend is available |
| `Audio` | Audio | mp3, flac, wav, ogg, m4a, m4b, aac, opus | **nothing** — no thumbnail producer here reads cover art yet, so the Thumbnails switches report audio as unsupported. The Detail view switches are the point of this kind: a host's viewer does play the file |
| `Fonts` | Fonts | ttf, ttc, otf, otc, woff, woff2, pfa, pfb, bdf, pcf, fon, fnt | a card with a line of the font's own glyphs, rasterized by FreeType — see [`UltraCanvasFontFile.md`](UltraCanvasFontFile.md). The font does not have to be installed, so a folder of downloaded fonts previews like a folder of photos; a symbol or icon face shows its own first glyphs instead. Its Detail view opens the glyph browser ([`UltraCanvasFontViewer`](UltraCanvasFontViewer.md)) — every glyph in the file, scrolling, with a picker for the ranges it covers. woff / woff2 report as unsupported for thumbnails unless the installed FreeType was built with zlib / Brotli |

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
- A preview that comes back empty because the file **was read and holds no
  preview** marks the file as failed (it is not retried) and the tile keeps its
  glyph — indistinguishable, on screen, from a kind that is simply switched
  off. The worker therefore logs `no thumbnail produced for "<path>"` for each
  such file, which is what names the cause when a whole folder loses its
  previews.
- One that comes back empty because the file **could not be read** says nothing
  about the file and is retried: up to four attempts, 300 ms apart and growing,
  before the slot is retired. This is the file that has just arrived in the
  folder — a paste, a drop, a download — and is still held by whatever wrote
  it, by the search indexer or by the virus scanner. Without the retry such a
  tile kept the type glyph for the life of the listing, and only a rescan (F5,
  leaving the folder and coming back, the folder watch reacting to a later
  change) brought its thumbnail in. A file written within the last ten seconds
  gets the same benefit of the doubt even where it reads fine by the time the
  question is asked — the holder may simply have let go in between. The same
  rule governs the text-content previews.
- Page-shaped previews (Text, Docs, Spreadsheets, PDF, 3D, Fonts) are only drawn
  where a page is legible — from roughly a 40 px box up. The small icon column of
  the Details and List rows keeps the type glyph, so a folder listing does not
  read every document in it, and a font specimen squeezed into an icon slot is
  not shown as a smear of ink.
- `FilerPreviewType` values are a bitmask; `GetThumbnailKinds()` /
  `GetDetailViewKinds()` return the current sets and `kFilerAllPreviewTypes` is
  the default of both. The preview kinds do not map one to one onto
  `FilerFileCategory`: PDF is split out of the Document category because it
  renders a page, and CSV / TSV count as spreadsheets because they preview as a
  grid (their file category stays `Text`). Fonts are the one kind that does line
  up exactly with its category (`FilerFileCategory::Font`).
  `UltraCanvasFilerWidget::PreviewTypeOf(entry)` reports the kind of an entry
  (`NonePreview` for folders, audio, archives and programs, which never carry a
  content preview).

### Per-format switches (the list of files)

A kind is coarse: switching *Vector graphics* off to be rid of one expensive
format costs the thumbnails of all seventeen. Each set therefore takes
per-format exceptions — a single extension switched off while its kind stays
on:

```cpp
filer->SetThumbnailFormatEnabled("eps", false);      // no EPS thumbnails
filer->IsThumbnailFormatEnabled("eps");              // false
filer->SetDetailViewFormatEnabled(".PSD", false);    // dot and case are ignored

// What an application persists (both sorted, lowercase, dot-less):
std::vector<std::string> off = filer->GetDisabledThumbnailFormats();
filer->SetDisabledThumbnailFormats(off);
```

The lists hold the **exceptions**, not an allow-list, so a format the widget
learns about later — a plugin registering a new vector format — is enabled by
default like every other one.

`GetPreviewableFormats()` is what such a list of files is built from: every
format the switches can address, in menu order (by kind, then by extension),
each with the readable label and whether **this build** can produce a thumbnail
for it at all.

The list is complete with respect to the FileLoader: every format
`UltraCanvasFileLoader::GetSupportedFormats()` reports for this build — its
canonical extension and every alias — appears in it, filed under the preview
kind of its media category. That is what the nine kinds are for: they cover
all seven `MediaFormatCategory` values (Documents split three ways), so a
format the application can open always has a switch. `FilerFormatListTest`
holds the widget to it.

```cpp
for (const FilerFormatInfo& f : UltraCanvasFilerWidget::GetPreviewableFormats()) {
    // f.extension "eps", f.label "EPS", f.kind VectorGraphics,
    // f.thumbnailSupported — false without a PostScript loader AND without an
    // embedded preview, so a settings page can grey the entry out instead of
    // offering a switch that changes nothing.
}
```

`thumbnailSupported` answers for the format in **this** build, and it answers
honestly: false for audio (nothing reads cover art), for the vector formats
with no renderer and no embedded preview (ccx, cmx — and emf, wmf, dxf, dwg and its dwt/dws/sv$ siblings in a build with no Vector plugin registered), for PDF without
the plugin, for video without a backend, and for the container formats no
reader here unpacks (xls, epub, mobi, prc, azw, azw3, fb2.zip) — those last
ones are refused by the text-preview extractor too, so the tile keeps its type
glyph instead of drawing a "page" holding the file's ZIP magic.

`onDisplayFormatsChanged` fires after any of the four sets changes, whoever
changed it (the Display menu included) — that is where an application saves
the choice and mirrors it into its other file displays.

### Embedded preview bitmaps

Most vector formats have no renderer that works without a window, so a
background worker cannot rasterize them. Three families do not need one: Xara
documents (`.xar`, `.web`, `.wix`) store a GIF/JPEG/PNG preview among the first
records of the file head, the ZIP-based CorelDRAW documents (`.cdr`, `.cdt`,
X4 and newer) keep one as `previews/thumbnail.png`, and PostScript documents
(`.eps`, `.epsf`, `.epsi`, `.ps` and the pre-CS2 `.ai` files, which are EPS)
carry one either as the TIFF section of a DOS EPS binary header or as the
hex-encoded EPSI preview in their comment block. The first two are ordinary
images once lifted out; the EPSI preview is converted to a greyscale PGM. All
of them decode on the thumbnail workers like any bitmap.

The extraction is plain file parsing — no graphics plugin, no render context —
and lives in `UltraCanvasEmbeddedPreview.h`, so anything else that wants the
same picture can use it:

```cpp
if (FormatCarriesEmbeddedPreview(path)) {               // asks about the format
    std::vector<uint8_t> bytes = ExtractEmbeddedPreviewBytes(path);
    if (!bytes.empty()) auto img = UCImage::LoadFromMemory(bytes);
}
```

`ExtractEmbeddedPreviewBytes` returns an empty vector for a file that carries
no preview (a plain ASCII EPS, for one — rendering that needs a PostScript
interpreter), an older RIFF-based `.cdr`, or a document too damaged to parse —
it never throws, so a worker can hand it any file the user points at.

`UltraCanvasMediaViewer` shows the same picture: a vector document it cannot
rasterize is displayed from its embedded preview, which is what gives these
formats a detail pane as well as a tile.

### File types the FileLoader knows

The widget's own extension table names the well-known formats. An extension it
does not list is looked up in the runtime format inventory
(`UltraCanvasSupportedFormats`, the same inventory the FileLoader's dialogs are
built from) before the entry is written off as "some file", so a format that
arrives with a graphics, document or media plugin the application registered
lands in its real `FilerFileCategory` — and with it gets the right colour, the
right grouping, and the Display > Thumbnails / Detail view switches that
govern it. The inventory
is consulted once and cached, so it costs nothing per directory entry.

### Native application icons

`.exe`, `.dll` and `.ico` files show the **icon inside the file** — what
Explorer shows — instead of the generic EXE/DLL glyph, in every view from the
Details icon column up to the largest thumbnail tiles. A file without an icon
resource keeps its glyph. This is an icon, not a content preview, so the
Display > Thumbnails switches do not affect it.

The extraction lives behind `UltraCanvasNativeFileIcons.h`
(`NativeFileIconAvailable` / `LoadNativeFileIconPixmap`) and runs on the same
background workers as the image thumbnails, so a folder of programs scrolls
as smoothly as one of photos. Two implementations answer it:

- **On Windows** through the shell (`SHDefExtractIconW` at the nearest
  embedded size, up to 256 px), which also covers what only a registry
  association knows — the icon of a document or a folder a shortcut points at.
- **Everywhere else** by reading the files
  (`UltraCanvasIconResource.h`): the PE resource directory of an `.exe` /
  `.dll`, the frames of an `.ico`, and the renditions of an `.icns`, decoded
  without a platform API and without a new dependency. This is what makes a
  Windows disk mounted on ULTRA OS, Linux or macOS — or the `drive_c` of a
  Wine prefix — show its programs with their own icons, and a Mac disk read
  anywhere show its applications with theirs.

### Thumbnail memory

Finished pictures are retained so scrolling back is instant, inside two byte
budgets that bound what a huge folder at a large tile size can hold:

| Pool | Budget | Holds |
|---|---|---|
| Content previews | 96 MB | bitmaps, vectors, poster frames, PDF pages, model renders, font specimens |
| Application icons | 16 MB | the native `.exe` / `.dll` / `.ico` / `.lnk` icons above |

Overflowing a budget drops that pool's **least recently drawn** entries, and
only as many as it takes to get back under — never the entry that just
finished, and never entries of the other pool. The two properties matter
together: a single video poster frame used to be able to empty the whole
cache, which blanked every tile on screen at once, and one shared budget let a
folder of photos push out the executables' icons even though those cost a
rounding error of the memory. Anything dropped that is still on screen is
re-queued by the next frame and usually comes straight back from the shared
`UCImage` cache.

`GetThumbnailCacheStats()` reports what is held (entries, stored bytes, and
the uncompressed size those bytes stand for — they differ under
`SetCompressedThumbnails(true)`, which additionally keeps a 32 MB hot cache of
the decompressed tiles being drawn). Rescanning the folder or changing the
view drops everything.

### Thumbnails between runs

The budgets above are memory, and memory ends with the process. What a folder
of photos, videos or documents *cost* does not: it is minutes of decoding, and
paying it again on every launch — and again after any browsing wide enough to
push the folder out of the budget — is the difference between a file manager
that opens a folder and one that thinks about it.

So a finished **content preview** is also written to a per-user cache
directory, and asked for before any decode is queued:

| Platform | Location |
|---|---|
| Windows | `%LOCALAPPDATA%\UltraCanvas\thumbnails` |
| macOS | `~/Library/Caches/UltraCanvas/thumbnails` |
| Linux / ULTRA OS | `$XDG_CACHE_HOME/UltraCanvas/thumbnails`, else `~/.cache/UltraCanvas/thumbnails` |

Entries are QOI blobs — the same compression `SetCompressedThumbnails()` holds
in memory, so a cached thumbnail is written once and, with compression on,
served to the tile without being touched at all.

**Application icons are never stored.** The shell extracts one faster than
this could read a file, and an icon that changed because the program was
upgraded must not come from yesterday.

**What makes an entry stale is the file it was made from**, not a timer: every
entry records its source's size and modification time, and a mismatch is a
miss — the entry is deleted and the tile decodes the file as it now is. So
editing a picture shows the edit.

**Retention is two weeks since the entry was last served.** Serving one stamps
it with the day (at most one write per file per day, so scrolling a folder of
a thousand pictures costs no disk writes after the first), and entries not
served for two weeks are swept by the first thumbnail worker to start. A
folder the user keeps visiting keeps its thumbnails indefinitely; one they
opened once pays for itself and then goes away. This is the same policy, and
the same code (`UltraCanvasDiskCache.h`), that expires the "Open with" handler
icons — two caches orphaned by the same kinds of event should not expire on
two different rules.

| Call | Does |
|---|---|
| `SetThumbnailDiskCacheEnabled(bool)` | On by default. Off stops reading and writing; what is on disk is left to expire on its own, so switching back on costs nothing |
| `GetThumbnailDiskCacheDirectory()` | Where the files are — whether or not the cache is switched on, since switching it off does not move them. Empty only when the platform offered nowhere writable |
| `GetThumbnailDiskCacheUsage()` | Files and bytes currently held |
| `ClearThumbnailDiskCache()` | Throws it all away now; returns how many files went |
| `ClearThumbnailMemoryCache()` | Drops the retained pictures; the tiles on screen re-decode on the next frame |

`GetThumbnailCacheStats()` reports the three ceilings above beside what is
used, and counts application icons apart from previews, so a settings page can
show "x of y" without keeping its own copy of y. UltraFiler's
*Settings > Extras > Cache* is built entirely from these calls: two switches,
the four figures, and an Empty cache button. Note that one figure — the
decompressed tiles — holds only what is being painted at that moment, so it
reads as empty whenever the display is not being drawn; say so wherever it is
shown, or it looks like a fault.

A disk cache is an optimisation and never a requirement: a full disk, a
read-only cache directory or a platform with nowhere to write costs a
re-decode, never a missing thumbnail.

## Shortcuts

A shortcut is drawn with **the icon of what it points at**, and reads as the
thing it stands for rather than as a file called "LNK" or as a text file with
a reverse-DNS name. Every format the three desktops use is read, on every
platform: the Windows `.lnk`, the freedesktop `.desktop`, the macOS `.webloc`
and — on macOS, the only system that can follow one — a Finder alias.

- Its **type** is `Shortcut`, and its **category** — the colour, the grouping,
  the preview switch that governs it — comes from its target, so a shortcut to
  a folder groups with folders and one to a program with programs.
- The **info column and the info bar show the target** as the link stores it
  (`C:\Program Files\…`), which is the string the shortcut's own properties
  show on Windows, and which stays informative for a link whose target is not
  on this machine.
- A small **arrow badge** in the bottom-left corner of the icon marks it as a
  shortcut — the only thing that tells it apart from the file it points at,
  whose icon it otherwise wears exactly. Below 24 px the badge is left off
  rather than smudged over the icon it annotates.
- **Double-clicking** it opens what it points at: a shortcut to a folder
  navigates into that folder, and one to a file opens the file. On Windows the
  shell resolves the link itself, which keeps the arguments and working
  directory it carries.

### Windows shortcuts (.lnk)

The reading is [`UltraCanvasShellLink.h`](UltraCanvasShellLink.md)
(`ReadShellLink`), and it works on every platform — the shortcut's Windows
path is mapped onto the host by looking for the drive it names (a Wine
prefix, or the root of a mounted Windows disk).

### Desktop entries (.desktop)

A freedesktop launcher gets the same treatment, read with
[`UltraCanvasDesktopEntry.h`](UltraCanvasDesktopEntry.md), plus the one thing
a `.lnk` never needs:

- **It is drawn by the name it calls itself.** The file name of a desktop
  entry is an id — `org.mozilla.firefox.desktop` — while its `Name=` is what
  every menu on the machine calls it. That name is what the display draws
  (`FilerEntry::linkDisplayName`), and what the filter-as-you-type box matches
  in addition to the file name. Only the drawn name changes: renaming, sorting
  and every file operation still use the real file name, so nothing on disk is
  ever addressed by a display string.
- **Its icon is looked up in the icon themes**, not read out of the file:
  `Icon=` is a name, resolved through the configured theme, what that theme
  inherits, hicolor, then the pixmap directories, at the size the tile needs.
- A `Type=Application` entry's **category is Program** and its target is the
  executable it starts, resolved on this machine (`/usr/bin/firefox`); a
  `Type=Link` entry shows its **address** in the info column, and a
  `Type=Directory` groups with folders.
- **Activating one runs what it says**: the `Exec=` line, expanded and
  launched detached through the platform's launcher, with the entry's own
  `Path=` as the working directory — a `Type=Link` opens its address instead.
  Opening the file itself (what happened before) handed a text file to a text
  editor.

### macOS shortcuts and application bundles

A `.webloc` shows its address in the info column and opens it when activated,
like a `Type=Link` desktop entry. A **Finder alias** carries no extension to
recognise it by, so the file itself is asked (its first bytes are bookmark
data) — and only on macOS, which is the only system that can resolve one;
elsewhere an alias stays the plain file nothing can follow.

An **application bundle** (`.app`) is the odd one: not a shortcut but a
*directory the platform presents as one object*. It is drawn with the icon
inside it and by the application's own name (`Example Editor`, not
`Example Editor.app`), typed `Application`, categorised as a program rather
than a folder, and `FilerEntry::isBundle` marks it. It carries no shortcut
badge — it is not a reference to something else, it *is* the application.

**Activating one depends on where you are.** On macOS it launches, which is
the Finder's rule; everywhere else it opens as the folder it is, because
navigating in is the only thing that machine can do with a Mac application.
The reading — Info.plist, the executable, the `.icns` — is
[`UltraCanvasMacBundle`](UltraCanvasMacBundle.md) and works on every platform,
so a Mac disk mounted on Linux still shows its applications properly.

### What the host sees

`FilerEntry::isShortcut`, `FilerEntry::isBundle`, `FilerEntry::linkTarget`
and `FilerEntry::linkDisplayName` carry the result to the application.
`linkTarget` is the target **as this machine opens it** — empty when the
target is not here, or is not a file at all (a shell item, a web address) — so
an application that can run Windows programs itself (`onFileActivated`) uses
it to launch the real target.

Because an icon is the file's identity rather than a courtesy preview, it is
held apart from the content thumbnails: the two have **separate memory
budgets**, so a folder of photos or videos filling the thumbnail budget can
never evict the application icons on screen (see
[Thumbnail memory](#thumbnail-memory)). Where extraction goes through the OS
shell it can fail on a file it would serve a moment later, so an icon is
retried a few times before the tile settles on its glyph — and the worker
threads join a COM apartment, which the shell expects of its caller.

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

## Files in use

`SetShowLockState(bool)` (default **on**) marks files another program is
holding — the reason an overwrite, a rename or a delete of one fails with
*"the file is open in another program"*. It is a no-op where the platform
cannot answer (`FileLockProbeAvailable()`, see
[UltraCanvasFileLock](UltraCanvasFileLock.md)).

A held file is marked three ways, so the state is visible in every view:

- a **padlock badge** in the bottom-left corner of its icon, for a file the
  system actually refuses — a quarter of the icon's edge (capped at 22 px, so
  a maximized tile does not carry a padlock the size of a file) and drawn only
  where the icon is at least 24 px. On a shortcut, whose arrow badge already
  has that corner, it stacks directly above the arrow instead. **Hovering it
  says who is holding the file**: the tooltip opens with what the listing
  already knows (*"In use by another program (cannot be replaced)"*) and
  fills in the program's name as soon as the holder probe it started comes
  back — that probe is run for the hovered file only, exactly because naming
  the holder is the expensive half of the question;
- an **attribute letter** among `D` / `L` / `R` / `H` / `A`: `X` for a file
  that cannot be replaced right now, `O` for one merely open elsewhere (which
  on Unix blocks nothing). It shows in the Details view's `Attr` column, in
  the thumbnail dataset line and in the info bar's `[...]` group;
- the **info bar**, which spells it out for a single selected file:
  *"In use by another program (cannot be replaced)"*.

The probe is one open per file — closed again, nothing written — and runs on
the **same background worker** as the folder statistics, ahead of every other
job on it, in one batch per pass. Only files the view actually draws are ever
asked about, and each is asked once per listing: the answers are dropped and
re-taken on a rescan, which is what a refresh (F5) and the folder watch's
reaction to a change both trigger. Nothing is probed on the UI thread, so a
folder on a slow volume opens at the same speed either way.

`GetEntryLockState(path)` reads back what the last probe found, without
probing. Holders are deliberately **not** collected for a listing — naming the
program costs a Restart Manager session per file on Windows; they are asked
for one file at a time, when the cursor comes to rest on that file's padlock
badge, and a host that wants the name elsewhere asks
`ProbeFileLock(path, true)` itself, the way UltraFiler's Attributes dialog
does.

Directories are never probed: what holds a folder open is usually a program's
*working directory*, which no probe here can see.

## Remote folders

The widget can show a folder that is not on this machine — a drive the host
carries for an FTP / SFTP server or a cloud account — through two hooks. It
gains no network dependency of its own: it only asks, in the same spirit as
the VirtualFS branch that lists the inside of an archive.

```cpp
filer->isRemotePath = [](const std::string& path) {
    return path.compare(0, 13, "ultracloud://") == 0;
};
filer->remoteListing = [drives](const std::string& path,
                                std::vector<FilerEntry>& out,
                                std::string& error) {
    return drives->List(path, out, error);   // from a cache, never blocking
};
```

- **`isRemotePath` is asked first**, before the local filesystem is consulted.
  That is the point of it: handing a remote path to `std::filesystem` would at
  best fail, and at worst — for a path that looks like a dead network mount —
  block the UI thread until the OS times out.
- **`remoteListing` runs on the UI thread**, inside the folder scan. A host
  that has to go to the network must answer from what it already holds,
  returning an empty listing while a fetch is in flight, and call `Refresh()`
  when the answer arrives. Blocking here freezes the window for as long as the
  server takes.
- **What an entry needs**: `name`, `path`, `isDirectory`, and for files `size`
  and `modifiedTime`. The widget derives the extension and the type
  information itself, so a remote file gets the same icon and category as a
  local one of the same name.
- **Returning `false`** with `error` set reports the message the way any
  listing error is reported, and the display shows that message where the
  empty-folder notice would go — a folder the server refused is not known to
  be empty; returning `true` with an empty listing means "nothing yet".

### Showing that a folder is on its way

"Nothing yet" and "nothing at all" look the same in a listing, so a third,
optional hook tells them apart:

```cpp
filer->remoteListingStatus = [drives](const std::string& path) {
    return drives->ListingStatus(path);   // "" once the listing is in
};
```

It is asked after `remoteListing` answered with an empty listing, and again
on every tick while the notice is up. A non-empty answer — "Connecting to
Backup NAS (ftp://nas.local) and reading /photos - 7 s", "Waiting for Backup
NAS - 2 requests ahead" — puts a turning progress ring, **Loading folder**
and that line in place of "Folder is empty!", and the words follow the fetch
as the host's answer changes. An empty answer means the folder really is
empty. Left unset, a folder being fetched shows as empty until the host's
`Refresh()`, as before. The ring runs on a 50 ms application timer that stops
as soon as the listing arrives, the folder changes or the widget is
destroyed.
### Changing a remote folder

Four more hooks let the host carry out the changes that act on the drive
itself, and a fifth takes files back off it. Unlike `remoteListing` they do not answer with the result: the host
queues the work and refreshes the display once the server has replied, so a
slow drive never holds the UI thread.

```cpp
filer->remoteDelete = [drives](const std::vector<FilerEntry>& victims,
                               std::string& error) { … };
filer->remoteRename = [drives](const std::string& path,
                               const std::string& newName,
                               std::string& error) { … };
filer->remoteMakeDirectory = [drives](const std::string& folderPath,
                                      const std::string& name,
                                      std::string& error) { … };
filer->remoteUpload = [drives](const std::string& folderPath,
                               const std::vector<std::string>& localFiles,
                               std::string& error) { … };
filer->remoteDownload = [drives](const std::string& folderPath,
                                 const std::vector<std::string>& remoteFiles,
                                 std::string& error) { … };
```

- `remoteUpload` is what a **drop onto a remote folder** shown in the widget
  goes through: the dropped paths are handed over for the host to put onto
  the drive under their own names, one request each. Return `true` when at
  least one was accepted; fill `error` with the first refusal even then (a
  folder, a remote entry, a drive that cannot take uploads) so the widget
  can say what was left out. Left unset, the drop is refused with a message.
  Dragging the widget's own entries onto one of its folder tiles is still
  refused on a remote drive — a move within a drive is not a provider verb.

- `remoteDownload` is its mirror: what a **drop of a drive's entries onto a
  local folder** goes through, so a file can be dragged off a server the same
  way one is dragged onto it. The host is handed the local folder and the
  remote paths and fetches them into it under their own names, one request
  each; the return value and `error` mean exactly what they do for
  `remoteUpload`. A drop carrying entries from a drive *and* files from this
  computer at once — a selection dragged out of a drive pane and one out of a
  local pane — is split, each half taking its own route. Left unset, such a
  drop is refused with a message rather than reaching `std::filesystem` with
  an `ultracloud://` path it cannot open.

- **The clipboard uses the same two hooks.** `Paste()` into a remote folder
  goes to `remoteUpload`; pasting a drive's entries into a local folder goes
  to `remoteDownload`. Copying a drive's entries puts their *names* on the
  system clipboard as text and keeps the paths on the widget's own clipboard
  (static, so shared between panes): another application cannot open an
  `ultracloud://` path, and for that same reason a drag of them that leaves
  the window is not handed to the OS — it stays an in-window drag. `Cut` and
  `Duplicate` refuse on a drive, and are greyed out in the context menu: a
  move off a drive is a download plus a destructive delete, and a duplicate
  is a server-side copy no provider offers.

- Each returns `true` when the request was **accepted**, not when it finished;
  `false` with `error` is for what can be refused outright — a drive that
  cannot be written to, a name that is really a path.
- **Each entry carries its own `isDirectory`**, which is what lets a backend
  pick the right call (FTP's `DELE` against `RMD`) without a probe per entry.
- `remoteRename` takes a **bare name**: a rename in place, never a move.
- A remote **new folder** cannot go straight into rename mode the way a local
  one does — the entry does not exist until the server has answered and the
  refresh has landed. The widget names it from the listing on screen
  (`UniqueRemoteChildName`) and the user renames it afterwards.
- **Left unset, the matching command refuses** rather than reaching
  `std::filesystem` with a path that resolves to nothing, which would fail
  with an error about a missing file instead of an answer about where it was
  pointed. That is still what happens for the operations with no hook:
  duplicate, paste and new file. Copying between the local disk and a drive is
  a transfer with progress, conflicts and a cancel, and belongs with the paste
  machinery rather than in a hook like these.
- The read-only badge is the host's to set: it fills `FilerEntry::isReadOnly`
  from what the drive can do, so an entry says so before a command is tried.

- **`listingIsRealDirectory` stays false** for a remote listing, which turns
  off the features that read the local filesystem per entry: the folder
  previews and the in-use (lock) column.

UltraFiler's remote drives are built on these two hooks; see
`Apps/UltraFiler/UltraFilerRemoteDrives.h`.

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

## Hidden-items notice

A display that drops entries without saying so is how a user comes to believe a
folder is empty, deletes it and loses what was in it. Where a host asks for it,
the display says so instead:

```cpp
filer->SetHiddenItemsNotice(FilerHiddenNotice::WhenAnyHidden);  // NoNotice by default
int held    = filer->GetHiddenItemCount();    // what the last scan left out
int ignored = filer->GetIgnoredItemCount();   // how much of that a pattern dropped
```

The mode decides *when* it speaks up. `NoNotice` is the default;
`WhenAnyHidden` announces anything the listing leaves out; `WhenIgnored`
announces only what the ignored-name patterns (below) dropped — what a
**setting** hid, which the user has no other way of noticing — and stays quiet
about the platform's own hidden files, which every file manager leaves out
silently. (`None` and `Always` are X11 macros, hence the spelled-out
enumerators — the same reason `FilerExtensionBadge::NoneBadge` is spelled that
way.)

While something IS held back, a strip across the foot of the display reads
*"3 items are hidden here"* and carries a **Show hidden files** button that
does exactly what the `Display > Hidden files` context-menu entry does — for
this display, leaving `SetShowHiddenFiles` elsewhere alone. The strip
disappears as soon as nothing is held back (including the moment its own
button is pressed).

`GetHiddenItemCount()` counts what the current listing leaves out: the hidden
entries, the names the ignore patterns drop, and the subfolders a curated home
folder (below) keeps back; `GetIgnoredItemCount()` is the patterns' share of
that, which is what `WhenIgnored` keys on. Both are `0` whenever hidden files
are shown, since then nothing is held back. The count is taken while the
listing is built — after it, the dropped entries are gone.

The strip takes its height out of the file area, exactly as the selection info
bar does (it sits directly above it), so no entry is ever drawn under it, and
it is left out of the whole-area views (`GourceTree`, `View3D`) and of a pane
too short to hold both files and strip. Its colours come from the same
`FilerStyle` fields as the info bar (`infoBarBackground`, `gridLineColor`,
`secondaryTextColor`).

Hosts pick the mode per folder. The UltraFiler uses `WhenAnyHidden` in the home
folder — the one folder where every filter bites at once: the profile's hidden
files, the curation below, and the ignore patterns — and `WhenIgnored`
everywhere else, so an ordinary folder stays quiet about its dot names but says
so when a setting dropped something from it.

## Ignored names

`SetIgnoredNamePatterns(patterns, onlyInFolder)` is the answer to clutter a
system leaves in a folder under a perfectly ordinary, **unhidden** name — no
dot, no attribute, nothing for a hidden-file filter to catch:

```cpp
filer->SetIgnoredNamePatterns({"Sti_Trace.log", "Thumbs.db", "*.bak"},
                              UserHomeDir());   // empty = every folder
```

Patterns are globs matched against the entry's name, case-insensitively, with
`*` for any run of characters and `?` for exactly one; folders are matched the
same way. `onlyInFolder` confines them to a single folder — the UltraFiler
points it at the home folder by default, since that is where the clutter
collects — and an empty string applies them everywhere.

The canonical case is `Sti_Trace.log`: the Windows Still Image (WIA) subsystem
writes it into whatever directory the process that touched a scanner or camera
was started in, which for a desktop app is the user's profile. It carries no
hidden attribute, so no hidden-file setting can reach it. The same holds in
reverse across platforms: a Windows share browsed from Linux or macOS shows
`Thumbs.db` and `desktop.ini` with their hidden attribute invisible, so the
name is the only thing left to filter on — which is why a single pattern list
serves every platform rather than one list per OS.

What it does **not** do matters as much: nothing is moved or deleted, the
entries still exist, a path still navigates to them, and a **file-list display
(a search) is exempt** — a search is a question the user asked, and hiding its
answers would be a bug, not a tidy-up. `SetShowHiddenFiles(true)` suspends the
patterns like every other filter ("show me everything"), and what they drop
counts into `GetHiddenItemCount()` / `GetIgnoredItemCount()`, so the notice
above offers it. Changing the patterns re-reads the folder, since the dropped
entries are not kept anywhere to be put back.

## Curated home folder

`SetCuratedHomeFolder(homePath, mainFolders)` curates one folder's display —
the user's home. While set, displaying `homePath` lists only the given main
folders plus the folder's regular files; every other subfolder is left out.
Each main folder is named by its resolved full path, so one redirected out of
the home folder (a Documents moved into OneDrive) is listed too, by its real
location. Every other folder is displayed untouched, and the curated folders
themselves behave like any other entry — navigation, context menu, drag & drop.

Display > Hidden files suspends the curation: that toggle means "show me
everything", so it reveals the untouched physical listing (hidden entries
included). An empty `homePath` turns curation off. What the curation keeps back
is counted into `GetHiddenItemCount()`, so the hidden-items notice above
offers it the same way it offers hidden entries.

The UltraFiler sets this on its tab filers and its folder-preview pane, with
the same main-folder set its folder tree shows for Home
(`GetWellKnownUserFolders()` filtered to Desktop / Documents / Downloads /
Music / Pictures / Videos), so the tree and the display agree on what Home
contains. Note the caveat that follows from the design: a non-main folder
created or pasted into the home folder exists but is not displayed until
Hidden files is switched on — curation is a view over the folder, not a
constraint on it — which is why the home folder is exactly where UltraFiler
turns the hidden-items notice on.

`UltraCanvas::GetCloudStorageFolders()` (`UltraCanvasCloudStorage.h`) is its
counterpart for a
"Cloud Storage" section: the sync folders the machine actually has, as
`CloudStorageInfo { CloudStorageKind kind; std::string path, label; }` in the
canonical OneDrive, Google Drive, Dropbox, iCloud Drive order. Each provider is
asked where it put its folder rather than guessed at:

| Platform | Where each provider is found |
|---|---|
| Windows | the `OneDrive` / `OneDriveConsumer` / `OneDriveCommercial` environment variables; the Google Drive mount recorded under `HKCU\Software\Google\DriveFS` plus the fixed drives whose volume label reads *Google Drive* (a default install mounts a virtual drive, not a folder); the Dropbox `info.json` under `LOCALAPPDATA`/`APPDATA`, which is where a relocated or a second, business folder is recorded; `%USERPROFILE%\iCloudDrive`; and the profile defaults for each |
| macOS | the per-provider folders macOS 12+ keeps under `~/Library/CloudStorage` (`OneDrive-Contoso`, `GoogleDrive-me@gmail.com`, `Dropbox`) — what Finder's sidebar lists — plus `~/Library/Mobile Documents/com~apple~CloudDocs` and the pre-CloudStorage locations |
| Linux | the GVFS mount table (`$XDG_RUNTIME_DIR/gvfs`, GNOME Online Accounts) and the defaults of the native sync clients |

Only folders that exist right now are returned, each once: a client that is
installed but signed out has no folder and is not listed. Nothing is mounted,
signed in to or contacted — but the lookup does read a registry key, a config
file and the mount table, so call it off the UI thread (the UltraFiler does).

It gets a header of its own rather than joining `GetWellKnownUserFolders()` in
`UltraCanvasUtils.h` because reading the Dropbox configuration needs
`UltraCanvasJSON`: `UltraCanvasUtils.cpp` sits at the bottom of the stack and is
compiled **standalone**, without the framework library, by several test targets
that only want `Trim()` — a JSON dependency inside it leaves every one of them
with undefined references at link time.

## Entry names

An entry is drawn under its file name. `displayNameProvider(entry)` lets the
host draw another one: return the name to show, or `""` to keep the file name.
Only the drawn name changes — sorting, renaming, the clipboard and every file
operation still work on the real one — so it is for entries that *mean*
something other than a file of that name.

```cpp
filer->displayNameProvider = [](const FilerEntry& e) -> std::string {
    if (e.path == UserHomeDir()) return "Home";   // not the account name
    return {};                                    // everything else as-is
};
```

Like `folderIconProvider` below it is asked while the entry is painted, so it
must be a lookup rather than a disk walk. It is asked ahead of every built-in
rule, including a desktop launcher's own `Name=`
(`FilerEntry::linkDisplayName`). UltraFiler answers it on its Computer page,
where the home folder is shown as *Home* — what the folder tree's row and the
folder tab call it too — instead of the account the folder is named after.

### Names in every script, and names that are not UTF-8

Names are UTF-8 throughout: German umlauts, Thai, Cyrillic, CJK and emoji are
listed, drawn, wrapped under a tile, ellipsized in a column and renamed as
whole characters (a caption never breaks inside a multibyte character).

A file name on disk, though, is whatever bytes the program that made it
wrote, and some are not UTF-8: an old Latin-1 tool writes "Namensänderung" as
`Namens\xE4nderung`, and a ZIP made on Windows and unpacked by a tool that did
not re-encode it leaves `Namens\x84nderung` (IBM437). Drawn as they were, each
such byte became U+FFFD — "Namens•nderung". `DisplayNameOf` now shows such a
name decoded (`RepairLegacyEncodedName` in `UltraCanvasTextUtils.h` picks
Windows-1252 or IBM437, whichever makes letters of the stray bytes), while
`FilerEntry::name` / `path` keep the real bytes, so opening, copying and
deleting the file still work. The rename field opens on the decoded name, and
an edited name is written as UTF-8. Archives the widget unpacks through
VirtualFS already come out with UTF-8 names (see the VirtualFS README).

## File type colours

Every colour the display gives an entry — the band across the foot of its
glyph, its TreeMap cell, the folder shape — comes from
`UltraCanvasFilerWidget::EntryColorOf(entry)`. Three independent channels carry
three facts, and none of them is the file's name:

- **Hue is the family.** Blue images, green video, yellow-to-orange audio, cyan
  vector, teal 3D models, purple documents, violet spreadsheets, grey text and
  code, dark red applications, steel grey libraries, magenta archives, sepia
  fonts. The media hues are saturated and the working files muted, so a folder
  of photographs looks unlike a source tree before a single name is read.
- **Brightness is efficiency.** Inside a family the modern format takes the
  brightest rung and the legacy one sinks to the dark end. Two formats share a
  rung when they share a compressor: zip, jar, tgz and gz are all deflate, and
  colouring them apart would invent a difference the bytes do not have.
- **A hue tilt separates lossless from lossy**, at the same chroma rather than
  by dulling it — indigo beside azure for images, pure yellow beside orange for
  audio. Lossless is a sibling family, not a washed-out version of its lossy
  neighbour.

| Family | Rungs, most efficient first |
|---|---|
| Images, lossy | `avif #2D86EA` · `heic/heif #1672DB` · `webp #1260BA` · `jpg #0F4F98` · `gif #0C3F7A` |
| Images, lossless | `png #6F79CC` · `qoi #5761C4` · `tif #414DB8` · `ico #363F98` · `bmp #2B337A` |
| Video | `webm #1CA04B` · `mkv #1A9545` · `mp4 #18883F` · `mov #157938` · `avi #12632E` · `wmv #0F5326` |
| Audio, lossy | `opus #FFAA54` · `aac/m4a #FF9830` · `m4b #FF8408` · `ogg #F37900` · `mp3 #E27100` |
| Audio, lossless | `flac #FFDC4D` · *(ALAC #FDCA00)* · `wav #EDBD00` · `aiff #DEB200` |
| Vector | `svgz #0E93AE` · `svg #0D88A0` · `ai/cdr #0C798E` · `eps/ps/dwg #0A677A` · `dxf/emf/wmf #085666` |
| 3D models | `glb #26998A` · `3mf #249182` · `stl/fbx #218577` · `ply/3ds #1E776B` · `gltf #1A665C` · `obj/dae #16584F` |
| Documents | `pdf #A876D4` · `epub #9F69CF` · `odt #975BCB` · `docx #8B49C6` · `doc #7E3AB9` · `rtf #6D33A0` · `md/html/tex #5C2B87` |
| Spreadsheets | `xlsx #893589` · `ods #A741A7` · `xls #BD57BD` |
| Text and code | source `#818B98` · config and data `#949DA8` · `txt #ABB1BA` · `log #BABFC7` |
| Applications | `exe #DC3644` · `appimage #CA2431` · `msi #AE1F2B` · `deb/rpm #911A24` |
| Libraries | `so #445662` · `dll #506573` · `dylib #5C7384` · `a/lib #688396` |
| Archives | `7z/zst #842A57` · `xz/lzma #9E3268` · `rar/bz2 #B93A79` · `gz/zip/jar/tgz #C74E8A` · `tar #CF669B` |
| Fonts | `woff2 #6E4A36` · `woff #81573F` · `otf #946449` · `ttf #A47051` · `ttc/otc #AE7A5B` · legacy `#B48467` |

Folders keep the amber `#F7BE50` they have always had — it is the one icon
nobody should have to relearn — and a directory is coloured by what it is, so a
folder called `render.mp4` is not drawn as a video. Anything the format table
and the plugin inventory both miss is the neutral `#9E9E9E`.

### Programs are not libraries

`.exe` and `.dll` used to be one category, one noun and one colour, which is
how a folder of system plumbing came to look exactly like a folder of programs.
They are now `FilerFileCategory::Executable` and `FilerFileCategory::Library`:
separate colours (dark red against steel grey), separate nouns in the Type
column (`core.dll` is a *Dynamic Link Library*, not a *Library Program*), and
separate positions in a sort by type. A host that asks "is this something the
user launches?" can now trust the category — UltraFiler's History *Apps* tab
does — instead of carrying its own list of program extensions.

### Caption ink

The TreeMap draws file names on top of these colours, so the ink has to answer
to them: `EntryCaptionInkOf(entry)` returns white for the dark families and
near-black for the light ones (audio, text and code, folders, unrecognised
files). The ink is a property of the **family**, never of the single file:
every rung of a family clears 3.2:1 against one ink, so no ramp ever switches
ink halfway down itself — a GIF does not get black text because it happens to
be the palest blue. Where the ink does change, between families, the change
itself says which half of the palette you are looking at.

### What the extension cannot say

The colour does not claim to know more than the file name does. `.webp` and
`.jxl` are both lossy and lossless and are coloured lossy, which is what almost
every one of them is. `.m4a` holds AAC or ALAC. `.mp4`, `.mkv` and `.mov` name
a container, not a codec, so an AV1 MKV and an MPEG-2 MKV share a rung until
something reads the file — the widget already probes audio and video lazily for
the Length column, and that probe is where a codec-accurate rung would come
from, filling in behind the extension's shade the way a thumbnail fills in
behind its glyph.

A host drawing its own file lists can ask for the same colours directly:

```cpp
const Color band = UltraCanvasFilerWidget::EntryColorOf(entry);
const Color ink  = UltraCanvasFilerWidget::EntryCaptionInkOf(entry);
// by extension alone, with the category as the fallback for formats the
// ladders do not rank (anything a plugin registered):
const Color c = UltraCanvasFilerWidget::FormatColorOf("avif", FilerFileCategory::Image);
```

## Folder icons

Folders are drawn as a colored folder shape. `folderIconProvider(entry)` lets
the host replace that shape per folder: return the path of an image — any
format the image pipeline loads, so SVG, PNG, QOI and the rest — and the entry
is drawn from it in every view, from the 16 px icon column of the Details rows
to a maximized thumbnail tile (`FilerStyle::folderIconScale` still applies).
Return `""` and the built-in shape is drawn as before.

```cpp
filer->folderIconProvider = [](const FilerEntry& e) -> std::string {
    if (e.name == "Music") return "media/icons/folder-music.svg";
    return {};                    // everything else keeps the folder shape
};
```

It is asked while the folder is painted, so it must be a lookup, not a disk
walk or a platform query — cache whatever answering it costs. The images
themselves are not a concern: they go through the shared image cache, keyed by
path and size, so one icon on a hundred folders is rasterized once per size.

The UltraFiler answers it with the icons of the well-known user folders
(Desktop, Documents, Downloads, Music, Pictures, Videos — `media/icons/`), and
before those with whatever the user set through the context menu's *Extras >
Set folder icon*: that entry converts any picture to a QOI file in the
application's config directory (`SaveImageFileAsQoi`, `ImageCairo.h`) and shows
that copy, so the icon survives the original being moved or deleted. *Extras >
Remove folder icon* takes it away again.

## Folder previews

A folder drawn as the built-in shape shows the **first pictures inside it
peeking out of the folder**, the way Explorer's folder icons do: up to two
cards stand in the open folder, their upper part above the front flap. On by
default (`SetFolderPreviewsEnabled`, `AreFolderPreviewsEnabled`), also toggled
by the context menu's *Display > Folder previews*; the switch fires
`onDisplayFormatsChanged` like the other Display switches, so a host that
persists those persists this one the same way.

```cpp
filer->SetFolderPreviewsEnabled(false);   // plain folder shapes only
```

They are drawn into the **built-in** folder shape, so they only appear where
that shape is what a folder is drawn with: a folder with an icon from
`folderIconProvider` ([Folder icons](#folder-icons)) keeps that icon, and with
`Display > File icons` on `HostOperatingSystem` ([File icons](#file-icons))
every folder is the system's folder icon and none of them peek.

What it costs, and where it is drawn:

- Only the **tile-sized icons** carry previews — the four thumbnail grids, and
  any other view whose icon box is at least 32 px. The 16 px icon column of the
  Details and List rows keeps the plain shape, where a picture would be a
  smudge.
- A folder the host gave an icon through [`folderIconProvider`](#folder-icons)
  keeps that icon; an application bundle keeps its own; the folders of an
  archive interior (VirtualFS) are never listed for it.
- **Listing the folder happens on the background workers** that decode the
  thumbnails — one directory listing per folder on screen, no file opened, no
  metadata call: the kind comes from the name, file-or-folder from the listing
  itself. The listing keeps the folder's first eight previewable files by name
  (bitmaps, vector graphics, videos, PDFs, 3D models and fonts; text-shaped
  files preview as a page of their own content, which a card this size cannot
  show) and gives up after 4096 entries. Like the thumbnails it is
  **viewport-driven**: only the folders the current frame draws (plus the
  prefetch band) are listed, and a pending listing that scrolls out of range
  is dropped.
- The pictures are the **ordinary thumbnails** of those files, requested at
  the card size through the same cache and the same budget — so the
  [Selective previews](#selective-previews) switches govern them exactly as
  they govern the file's own tile (a kind switched off never shows inside a
  folder either), and a picture is decoded once per size however many folders
  and views show it.
- Until the listing lands the folder is drawn as the plain shape; a folder
  holding nothing previewable stays that way, and one that cannot be listed
  (no permission) does too until the next rescan. A card whose decode is still
  on its way — or failed — shows as a blank sheet, so a folder never pops from
  "open" back to "closed".

The listings are dropped with the thumbnail cache — on a rescan, so the folder
watch's rescan after a change is what makes the previews current — and are
capped at 4096 folders, past which everything outside the current frame goes.

`FolderPreviewCardRects(box, count)` is the card geometry, public and pure so
it can be tested (`Tests/FilerFolderPreviewTest.cpp`).

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

### The hover follows the cursor, not the content

The hovered item is whatever sits under the pointer *now*, and the pointer is
not the only thing that moves it: the wheel, dragging the scrollbar, keyboard
navigation revealing an entry, a resize and a rescan all slide the files past a
cursor that never moved. The widget therefore re-derives the hover at the start
of every paint whose scroll offset or layout changed, so the icon strip (and the
hover highlight, and the name tooltip) jumps to the file that is under the
cursor after the scroll instead of riding away with the one it started on.

While a gesture owns the pointer the hover stays off: an item drag and a rubber
band drop it for their duration, a splitter drag keeps the pointer on the
splitter, and pressing the scrollbar drops it too — the pointer is parked on the
bar for the whole drag, not on a file. The first paint after such a gesture ends
works the hover out again, without waiting for the pointer to move.

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
filer->DeleteSelection();     // asks: Move to Trash (chosen) / Delete permanently
filer->DeleteSelection(FilerDeleteMode::Permanently);  // asks, opened on
                              // "Delete permanently" (what Shift+Del does)
filer->ConfirmDeletePaths(paths, onDone);  // the same dialog for paths the
                              // display is not showing (a folder-tree delete)
filer->DeletePaths(paths, onDone, FilerDeleteMode::MoveToTrash);  // no
                              // question - for a host that ran its own
                              // confirmation (default mode: Permanently)
filer->DuplicateSelection();  // copy alongside with " (2)" style names
                              // (the paste machinery, aimed at this folder)
filer->StartRename(index);    // inline rename editor (Enter commits, Esc cancels)
filer->CompressSelection();          // .zip alongside (default)
filer->CompressSelection("tar.gz");  // pick the format via extension
filer->ExtractSelection();           // into sibling folders; a taken folder
                                     // name asks Keep both / Merge / Skip
filer->OpenExtractDialog();          // the context menu's extract dialog
filer->CreateNewDocument({"Text", "txt", ""});
```

### Delete: to the Trash, or permanently

**Del** and **Shift+Del** open the same confirmation, `Delete "X"?`, with the
choice as two radio buttons: **Move to the Trash** (*Recycle Bin* on Windows)
and **Delete permanently**. Del opens it on the trash, Shift+Del on the
permanent delete — the keys Explorer gives the two — and the line under the
question follows the choice: *It can be restored from the Trash.* or *This
cannot be undone.* The context menu has both, **Delete** (Del) and **Delete
Permanently** (Shift+Del).

The trash is `UltraCanvasTrash` (`MoveToTrash`): the Recycle Bin through the
shell, the Finder's Trash through NSFileManager (so *Put Back* works), and the
freedesktop.org trash on Linux and the BSDs — the drive's own
`.Trash-$uid` for a file on a USB stick, never a copy into the home folder.
Moving to the trash is one move per entry, so a folder of any size goes at
once, and a write-protected entry is not asked about (moving it does not
write to it). An entry the trash refuses stops at the problem dialog below
(*Cannot Move to the Trash*); it is **never** deleted for good instead.

Under the choice the dialog lists **what is about to go**, in an
`UltraCanvasListView` with the Details view's columns - the entry's icon (the
display's own, through `DrawEntryIcon`, so type glyphs and host icons alike)
and name, size, modified - at most 40 rows, ten at a time with a scrollbar.
Several items selected: the list is those items, under a caption that counts
the folders and files and adds up the files' size. One folder: the list is
what the folder holds, folders first and then by name, under *Folder "X"
contains 147 items (first 40 shown)*. A single file gets no list - the
question already names it.

Where the trash cannot take the entries — no trash on this platform (Android,
WebAssembly), entries inside an archive, entries on a remote drive — the
trash option is greyed out, the dialog opens on **Delete permanently**, and
the line says why. `CanMoveToTrash(victims)` answers the same question for a
host. A host `confirmDelete` veto replaces the dialog and so has no choice to
offer: the delete then goes the way `DeleteSelection` was asked for, as far as
the trash can take the entries.

### Progress window (copy / move / delete)

Copying, moving and deleting run on a **background worker**, and a
[progress window](UltraCanvasProgressDialog.md) — the ring with the
percentage, the file being handled and **Cancel** — opens over them **once the
operation has been running for two seconds**. Anything shorter never shows a
window at all: a file manager that flashes a dialog for every copied text file
is worse than one that shows none. (Packing, unpacking and the
["Delete as administrator" helper run](#delete-problems-locked--failing-entries)
open theirs immediately instead: none of those is ever the quick case, and the
last one is waiting on a consent prompt the user has to answer.)

The window is the same for every route into these operations — Ctrl+V, the
context menu, `Delete`, a drag & drop between panes, `Duplicate`,
`PasteFilesInto()`, `DeletePaths()` — because they all go through the same two
queues.

What the ring shows: every entry of the queue is worth an equal slice of it,
and the bytes copied (or the entries removed) inside an entry move the ring
within its slice. So a single large file fills the ring smoothly and a
thousand small ones fill it a step at a time. The size of an entry is counted
just before it is worked on, never for the whole queue up front — for a move,
where each entry is one instant rename, walking every tree first would take
longer than the move.

Files larger than 8 MB are copied in 1 MB chunks so the ring moves *inside*
the file and **Cancel** does not have to wait for it; smaller files go through
`std::filesystem::copy_file` in one call, which lets the platform hand the
copy to the filesystem itself.

**Cancel** stops at the next file. What was already copied, moved or deleted
stays; the entry the cancel interrupted does not: a half-copied file or folder
is removed, so nothing partial is left in the listing. The one step that is
never interrupted is the *second half of a cross-volume move* — once the copy
is safely on the other volume, the original is removed to the end, because
stopping there would leave the entry half in both places.

The queues themselves stay on the UI thread, because their conflict and
problem dialogs are answers only the user can give: the worker walks the queue
until it reaches an entry that needs one and hands the queue back. The
progress window closes while such a dialog is up (two modal windows at once is
nobody's idea of a file manager) and reopens when the work resumes — without a
second two-second wait, because the delay is measured from the start of the
whole operation.

The application window stays live throughout: the folder display keeps
painting and scrolling while a long copy runs. Auto-refresh is held back until the
operation ends, exactly as it is for an open rename editor or a drag.

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

A **shortcut** is activated as the thing it points at: one to a folder
navigates into that folder, and one to a file opens the file. A Windows
`.lnk` is resolved first off Windows, since nothing there knows what one is,
and on Windows goes through the shell, which does it better (it keeps the
arguments and working directory the link carries); a `.desktop` launcher runs
its own `Exec=` line. See [Shortcuts](#shortcuts).

Entries inside archives are virtual paths nothing external can read, so
activation never tries to run or open them.

### Launch feedback — the busy pointer

Spawning is over in milliseconds; the program the double-click started is not.
It appears when it appears, and nothing tells the file manager when that is —
so a heavy application looks for a few seconds exactly like a double-click
that never arrived, and gets double-clicked again.

Every launch path therefore arms the window's **busy pointer**
(`UltraCanvasWindowBase::ShowBusyPointer`, `UltraCanvasWindow.h`): after
**one second** the pointer changes to `UCMouseCursor::AppStarting` — the arrow with a busy
sign, so the window stays as usable as it was — and it goes back on its own
after **eight seconds**. The delay is the point: a program that is on screen
before the second is up never changes the pointer at all, so the feedback only
appears where it is telling the user something.

```cpp
// What the window offers; the filer arms it for you on every launch.
win->ShowBusyPointer();                 // 1 s delay, 8 s hold, AppStarting
win->ShowBusyPointer(1500, 120000);     // slower to appear, long-running work
win->HideBusyPointer();                 // work finished (or failed) early
```

Nothing in a plain spawn can end it early, so the hold does. A caller that
*does* learn when its work finished calls `HideBusyPointer()` instead —
UltraFiler does exactly that around the UltraWin launch path, whose first run
prepares a Windows environment and is given a two-minute hold. A launch that
fails immediately takes the pointer down through `onError`.

While it is up the busy shape wins over the cursor of whatever element the
pointer is over; when it comes down, the element under the pointer gets its
own cursor back without waiting for the next mouse move.

On macOS the pointer does not change: AppKit has no arrow-with-busy-sign
shape, and launch feedback there belongs to the Dock's bouncing icon.

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
- A delete that fails with **"Access is denied"** on Windows is the one Explorer
  answers with its shield button: the entry is deletable, just not by this
  user. Where the host has wired
  [`UltraCanvasElevatedFileOperations`](UltraCanvasElevatedFileOperations.md)
  (UltraFiler has), the dialog is **Administrator Permission Needed** —
  "Deleting this file needs administrator permission. Windows will ask you to
  confirm before it is deleted." — with **Delete as administrator**
  (preselected) / **Try again** / **Skip this file** and the same scope switch.
  Entries handed to the administrator are collected while the queue runs and go
  to the elevated helper in **one run at the end**, so the whole delete costs
  one consent prompt however many entries need it; a "Deleting as
  Administrator" progress window stands in for the wait, and the widget stays
  responsive because the helper is waited for off the UI thread. What the
  helper still could not delete comes back in a **Cannot Delete** dialog with
  the system's reason per entry; a declined prompt is reported through
  `onError` and leaves the entries in place. Sharing violations ("in use by
  another program") are not permission failures and keep the plain dialog,
  as does a process that already runs as administrator — asking again cannot
  change the system's answer there.

Entries inside archives are still deleted in one batched archive rewrite
before the interactive queue; their failures are reported via `onError` as
before. When modal dialogs are unavailable the delete falls back to the old
fixed behavior (attempt everything, report failures). A problem dialog's
**Cancel** keeps what was already deleted and drops the rest — the entries
waiting for the administrator retry included.

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

`SetNewDocumentTypes()` replaces the default New > entries (Text `txt`, Doc
`odt`, Spreadsheet `ods`, Bitmap `png`, Vector `svg`, Audio `wav`, Video
`mp4`); `GetNewDocumentTypes()` reads the current set back, so a host can
mirror the submenu elsewhere — the UltraFiler's command-bar "New folder ▾"
split button lists exactly these. Each entry may name a
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

Creating anything — `CreateNewFolder()` or `CreateNewDocument()` — first
returns a [file-list display](#file-list-search-results) to the folder and
ends an active [name filter](#name-filter-filter-as-you-type): the fresh
entry lands in the shown folder and has to be visible there, with its inline
rename editor reachable, which neither a result display nor a narrowed
listing can guarantee.

## Watching the shown folder

The folder can change without the widget doing anything: another application
saves a file into it, a download finishes, a script deletes one. The widget
notices and rescans.

```cpp
filer->SetFolderWatchEnabled(false);      // on by default
filer->IsFolderWatchNative();             // OS notifications, or polling?
filer->SetFolderWatchIntervalMs(3000);    // default 1500, minimum 250
```

Where the operating system can report changes itself the widget uses that —
inotify on Linux, `ReadDirectoryChangesW` on Windows, through
[`UltraCanvasFolderWatcher`](UltraCanvasFolderWatcher.md). A change is then seen
the moment it happens, and an idle folder costs nothing at all.

Where no backend exists (macOS, Android, WebAssembly) it falls back to polling:
a background worker re-fingerprints the folder every interval — its own
modification time folded together with each entry's name, size and modification
time — and raises a flag when the number moves. The scan never runs on the UI
thread. `IsFolderWatchNative()` reports which of the two is in force.

Either way only a real directory is watched: an archive interior or a file list
has no folder whose changes would mean anything. The interval governs detection
only while polling; with a native watcher it bounds just how quickly the UI
applies what the watcher already reported.

A native watch can also die after it started — the volume the folder is on is
unmounted, the share drops, the handle goes bad. The widget is told (the
watcher's failure callback) and moves that folder to polling, so it keeps
noticing changes instead of quietly freezing on a listing that no longer
exists; `IsFolderWatchNative()` then reports `false`. It keeps polling the
folder even while it is gone, which is what makes the same stick plugged back
in re-list itself rather than needing a manual refresh.

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

Unlike a copy, move or delete (see
[Progress window](#progress-window-copy--move--delete)), the window opens
**immediately** rather than after two seconds: packing and unpacking are never
over in a blink.

The progress window is opened **without the severity badge**
(`showIcon = false`), so the ring is horizontally centred in the dialog instead
of being pushed to the right of a blue `i` that says nothing the ring does not.

Cancel stops the backend at its next progress callback. A cancelled **pack**
deletes the half-written archive (nobody wants that in the listing); a cancelled
**unpack** keeps what it already wrote, because those are real files, and stops
the remaining archives of a multi-archive run.

An archive that is **extracted only in part** - entries VirtualFS refused
because they would have been written outside the destination (`../x`, an
absolute path, a hard link climbing out), or entries it could not write (a
file through a symbolic link the archive created) - opens an **Extraction
Incomplete** dialog: *Not everything in "Download.zip" was extracted. The rest
of the archive was unpacked.*, then each kind of problem with the entries it
held back, one per line. The status line (`onError`) gets one sentence saying
the archive was extracted only in part. An archive that could not be extracted
at all still reports `Extraction failed for <archive>` there, now followed by
the reason. The text comes from `UCVFSBridge::ExtractArchive`'s `outError`
parameter, not the bridge's shared `GetLastError()`, because extractions run on
worker threads.

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

A drag is the one file operation that can be started by accident — a press that
wandered a few pixels — and it is carried out before it is noticed, so the drop
can be made to ask first with `SetDropConfirmation(FilerDropConfirmation)`:

| Mode | The drop asks |
|---|---|
| `NeverConfirm` | never — the drop is carried out straight away (the default, and what earlier releases did) |
| `MoveOnly` | only when the drop **moves** the files; a copy is carried out |
| `AlwaysConfirm` | for every drop, copies and files dragged in from other programs included |

```cpp
filer->SetDropConfirmation(FilerDropConfirmation::MoveOnly);
```

The question names what is about to happen — how many entries, moved or copied,
into which folder, with the folder's full path as the detail line — and nothing
is touched until it is answered; the other answer abandons the drop. Files
arriving from another program (or another pane of the same window) are copies,
so only `AlwaysConfirm` asks about those. With dialogs disabled the drop is
carried out rather than lost.

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

Enter activates (folders / archives are entered, files fire `onFileActivated`;
the numeric keypad's Enter counts as Enter everywhere the widget reads it),
Delete deletes, F2 renames, Ctrl+A / Ctrl+C / Ctrl+X / Ctrl+V select all / copy /
cut / paste, Ctrl+D duplicates, Ctrl+P prints (when `onPrint` is set), and the
arrow keys move the selection (grid-aware in the thumbnail and list views). The
same shortcuts are shown next to their commands in the right-click context menu.

A plain **printable character** is Explorer-style type-ahead: it selects the
first entry whose name starts with that character (case-insensitive), and the
same key again walks on to the next such entry, wrapping around at the end.
The step is also callable — `SelectNextEntryStartingWith(ch)` returns whether
a displayed entry matched — so a host can route characters typed elsewhere in
its window into the visible filer (the UltraFiler does this with a window
event filter that stands back while a text input holds the focus).
Click, Ctrl+click and Shift+click select single items, toggle, and ranges.
Dragging from **empty space** draws a **rubber band**: every entry the
rectangle touches becomes the selection, live while the band is dragged
(with Ctrl the rectangle *adds* to the selection held before). The band
auto-scrolls at the viewport edge, Escape abandons it (the previous
selection returns), and a press-and-release without movement keeps its old
meaning — a plain click on empty space clears the selection (a Ctrl click
leaves it alone).
Double-clicking an entry with the **left** button opens/activates it (folders
and archives are entered, files fire `onFileActivated`); the right and middle
buttons do not, although Windows reports a double-click for those too — a
second right-click on an entry is aimed at the context menu. A single click on the **name** of the entry that
is already the only selected one starts an inline rename after a short delay
(Windows style — the delay is what separates a rename click from the first
click of a double-click).

The rename editor is a real `UltraCanvasTextInput` overlaid on the item's
name, so editing behaves like any text field: the caret moves with the
arrow keys / Home / End, a click puts it at that position, Shift extends a
selection, and Ctrl+C/X/V/Z work. It opens with the base name selected (the
extension stays, Explorer-style; folders select the whole name) in the same
font size as the displayed name, commits on Enter — the numeric keypad's Enter
included — cancels on Esc, and a click anywhere outside the field commits too,
because the field losing the keyboard focus ends the edit. The name is written
in `FilerStyle::renameTextColor` (a dark gray, a step lighter than the
near-black of a displayed name) so an entry being edited reads as being
edited; the caret uses the same color.

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

A **move reports both ends**: the folder the entries were pasted or dropped
into, and every folder they were taken out of. The second one matters for a
cut and paste across folders (`Ctrl+X` here, `Ctrl+V` there) — the folder that
lost the entry is not the one the paste names, and without it a host's folder
tree kept showing a folder that had moved away. A move that is a no-op (a cut
pasted back into the folder it came from) reports nothing, and each folder is
reported once however many entries came out of it.

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

### Growing a file list while it is produced

`AppendToFileList(paths)` adds to the list already on display. Only the new
paths are stat-ed, and the scroll position and the selection stay where they
are — so a search that is still walking the disk can show what it has found
so far and keep adding to it:

```cpp
filer->ShowFileList({});                 // empty result display, at once
// ... on the UI thread, per batch the worker delivers:
filer->AppendToFileList(batch);          // grows the list, view stays put
```

Handing the whole grown list to `ShowFileList()` per batch would instead
re-stat every path already listed (O(n²) over the search) and jump the view
back to the top each time. Called before any `ShowFileList()`,
`AppendToFileList()` behaves like one. The list is sorted like a folder
listing after every batch unless `SetFileListOrderPreserved(true)` keeps the
given order.

## Name filter (filter-as-you-type)

`SetNameFilter(text)` narrows the displayed listing to the entries whose name
contains `text` (case-insensitive); `""` shows everything again and
`GetNameFilter()` reads the filter back. The narrowed listing is re-derived
from the already scanned entries — no disk rescan per keystroke — which is
what makes wiring a search field's `onTextChanged` straight to it cheap:

```cpp
searchField->onTextChanged = [filer](const std::string& text) {
    filer->SetNameFilter(text);   // filter-as-you-type
};
```

The filter applies to whatever is displayed — the folder listing or a
[file list](#file-list-search-results) — and stays applied through rescans
(file operations, the folder watch), so entries created or dropped in while a
filter is active only show when they match. Selection is kept on the entries
that stay visible, each change resets the scroll to the top, and
`onFolderRefreshed` fires so the host can refresh its counts. `SetPath()`
**clears the filter**: it belonged to the listing it was typed against, so
entering another folder starts unfiltered.

When the filter hides every entry the widget shows "No matches for "…"", and
a host can center an escalation button under that notice:

```cpp
filer->SetFilterEmptyAction("Scan sub folder", [this]() {
    StartSubfolderScan(filer->GetNameFilter());   // e.g. AppendToFileList(batch)
});
```

The button (a real `UltraCanvasButton` child) appears only while a filter is
active, matches nothing, and both a label and a callback are set; an empty
label removes it. The UltraFiler uses exactly this pair: typing in its search
field filters the shown folder, and the button — like Enter in the field, and
like the button inside the field — escalates to the background sub-folder
scan, which feeds its matches in through `AppendToFileList()` while it runs.

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
| `displayNameProvider(entry) -> string` | Per entry while it is drawn — return the name to draw instead of the file name, `""` to keep it (see [Entry names](#entry-names)) |
| `folderIconProvider(entry) -> string` | Per folder entry while it is drawn — return an image path to draw instead of the folder shape, `""` to keep it (see [Folder icons](#folder-icons)); a folder drawn as the shape shows the first pictures inside it (see [Folder previews](#folder-previews)) |
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
`captionMaxLines`, `captionLineHeight`, `captionBreakTolerance`,
`captionOverflowSlack` and `captionCamelCaseBreaks` size the tile caption and
its name wrapping (see [Long names](#long-names)):

```cpp
FilerStyle s = filer->GetStyle();
s.captionMaxLines = 3;        // let tile names run over up to three lines
s.captionLineHeight = 16;     // 0 = derived from smallFontSize
s.captionBreakTolerance = 3;  // never split a word for 3 characters or fewer
s.captionOverflowSlack = 0;   // 0 = derived from smallFontSize
s.captionCamelCaseBreaks = true; // "UltraCanvas" / "Texter.exe", not "UltraCanva" / "sTexter.exe"
filer->SetStyle(s);
```

`FilerStyle::extensionBarBackground`, `extensionTagBackground`,
`extensionTagTextColor` and `extensionBadgeHeight` (0 = derived from
`smallFontSize`) style the tile's extension tag — see
[File extensions](#file-extensions).

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
