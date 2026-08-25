# UltraFiler

A Windows Explorer style file manager built entirely from UltraCanvas
components:

| Area | Component |
|---|---|
| Folder tree (left pane) | `UltraCanvasTreeView` — lazily populated filesystem tree (Home, drives / mounted volumes) |
| Folder content (center pane) | `UltraCanvasTabbedContainer` hosting one `UltraCanvasFilerWidget` per tab — details / list / thumbnail grids / size bars / treemap views, full file context menu, clipboard and drag & drop interop |
| Preview (right pane) | `UltraCanvasMediaViewer` — images, video, audio, PDFs, spreadsheets, 3D models and text files |
| Path bar | `UltraCanvasBreadcrumb` via the shared `BuildFolderBreadcrumb` helper |
| Search field | `UltraCanvasTextInput` driving `UltraCanvasFilerWidget::ShowFileList()` |
| History view | `UltraCanvasTabbedContainer` (Files / Folders / Apps) hosting one small-thumbnail `UltraCanvasFilerWidget` per tab, fed with `ShowFileList()` from `UltraFilerHistory` |
| Favorites view | the same tabbed layout, fed with `ShowFileList()` from `UltraFilerFavorites` (the pinned paths) |
| Panes | `UltraCanvasSplitPane` with draggable splitters |

## Features

- **Tabs:** the "+" button on the left side of the toolbar opens an
  additional tab showing the current folder. Every tab has its own folder
  view, Back / Forward history, sort and view settings; tabs can be
  reordered by dragging and closed (the last one stays open).
- **Navigation:** Back / Forward history (per tab), Up, Refresh, clickable
  breadcrumb path (each segment's dropdown lists sibling folders), folder
  tree with lazy expansion, and the History toggle (see below).
- **Search:** the field on the right of the path bar searches the current
  folder recursively for names containing the text (case-insensitive, up to
  1000 matches). Enter runs it; the matches are displayed in the tab's
  current view mode, with a *Path* column after the name in Details view.
  The context menu's first entry, **Open path (in new tab)**, opens the
  selected match's folder in a new tab. Clearing the field (or navigating
  anywhere) returns to the normal folder display. Each tab keeps its own
  search.
- **History:** the clock button in the navigation row replaces the folder tree
  and folder display with the History view — a tabbed container with **Files**,
  **Folders** and **Apps** tabs, each a filer widget in *small thumbnails* mode
  listing the recently used paths (most recent first) instead of a folder's
  content. Files and applications are remembered as they are opened
  (double-click / Enter). A folder is remembered only once **work has been
  done in it** — a file opened there, or something created, pasted, dropped in
  or out, renamed, duplicated, deleted, packed or extracted; browsing through
  a folder does not put it in the list. An entry whose file has meanwhile been
  deleted drops out of the list. Activating a tile leaves the History view
  and shows the entry in the folder display — a folder is opened, a file is
  selected inside the folder it lives in (so the preview picks it up when it is
  media); the context menu's **Open path (in new tab)** opens its folder in a
  new tab instead. Clicking the clock again, **Esc**, or
  any browsing action (navigation, search, a file command) returns to the
  folder view. The lists survive restarts — they are stored next to the
  settings as `history.txt` — and *Settings ▸ Clear History* empties them.
- **Favorites (pinning):** the heart button next to the clock shows the
  Favorites view — the same **Files** / **Folders** / **Apps** layout as the
  History view, but listing what was pinned deliberately instead of what was
  used recently. The file context menu's **Extras** submenu does the pinning:
  its **Pin** and **Unpin** submenus each offer **To Favorites** — acting on
  the selection of the visible view (or, with nothing selected, the folder
  currently shown) — and **To Treeview** (folders only), which pins the
  folder into the folder tree's **Pinned** section, where it navigates like a
  bookmark on click. The entries are check items whose flag shows whether the
  selection is pinned there right now; Pin is enabled while something is
  still unpinned, Unpin while something is pinned. Entries keep the order
  they were pinned in and drop out when their path disappears from disk.
  Right-clicking the folder tree opens its context menu: **Copy** /
  **Delete** (with confirmation) / **Paste** act on the folder under the
  cursor — Paste only when a folder is under the cursor and the clipboard
  holds files, Delete never on the top-level roots — a **Pin** submenu whose
  **To Treeview** / **To Favorites** flags directly toggle where that folder
  is pinned, and **Unpin** (pinned entries only) removes the bookmark
  without touching the folder. The pins survive restarts — they are stored
  next to the settings as `favorites.txt` — and *Settings ▸ Clear Favorites*
  empties them (History and Favorites are separate stores).
- **Command bar:** New folder (also **Ctrl+F**, and *New > Folder* at the top
  of the file display's context menu) / New file — inline rename starts
  automatically — Cut / Copy / Paste (system clipboard interop), Rename,
  Delete (with confirmation), sort field + direction, view type selection,
  video preview mode, Preview toggle.
- **Live folder:** the file display rescans by itself when the folder changes
  behind it — another application saving a file into it, a download finishing,
  a script deleting one. The check runs on a background worker, and the refresh
  waits for any interaction it would interrupt (an open rename editor, a drag,
  a context menu, a file operation and its dialog).
- **Folder views:** the view type and sort order are remembered **per folder**,
  so a picture folder can stay on large thumbnails by date while a source folder
  stays on details by name. Entering a folder puts its own settings back; a
  folder that has none keeps whatever the previous one used. They are stored
  next to the settings as `folderviews.txt` (the 400 most recently entered
  folders) and *Settings > History & Favorites > Clear Folder views* forgets
  them.
- **Folder tree:** a **Pinned** section on top — above *Computer*, open, and
  shown only while something is pinned — then *Computer* with Home and the
  drives / volumes below it.
- **Archives:** packing and unpacking run in the background behind a progress
  window: a ring with the percentage, the file being handled and Cancel.
  Cancelling a pack removes the half-written archive; cancelling an unpack keeps
  what it already wrote.
- **File display:** everything `UltraCanvasFilerWidget` offers — sortable
  Details columns, thumbnail grids with async decoding, the size-bar and
  treemap views, hover icon menu, selection info bar, archive browsing
  (VirtualFS), compress / extract, drag & drop to and from other
  applications. Dropping dragged files onto a folder shown in the view moves
  them there; *Settings > Handling > Drag & Drop* switches that to copying.
  Ctrl at the drop always copies and Shift always moves. A move takes the
  dragged files out of the selection first, so the preview lets go of the file
  before it is renamed away.
- **Preview:** selecting a single previewable file shows it in the preview
  pane; double-click / Enter opens it there too. While nothing previewable
  is selected the pane folds away, so the folder display always gets the
  whole width — the Preview toggle only enables / disables the feature, and
  **Esc** closes an open preview (turning the toggle off). The pane takes
  its width from the folder display only, so the folder tree and its
  splitter never move when the preview opens or closes; the width the pane
  is dragged to is restored on reopen, and the selected file is kept
  scrolled into view when the narrowed folder display would cut it off.
  The viewer provides zoom, rotation, color adjustments, slideshow, and
  per-kind views for documents, spreadsheets, models, audio and video.
  A file with real transparency - an alpha channel that is used, or a vector
  document (SVG) - shows a strip of backdrop colours right under the picture:
  greys and colours to click, and the checkered swatch to go back to the
  transparency pattern. What is picked there is saved, so the next preview
  opens with it (it is the same setting as *Settings > Media Viewer >
  Transparent Images*).
  While the preview is enabled, **deleting the previewed file selects its
  neighbour** (the next entry, or the previous one when it was the last), so
  the pane moves on to that file instead of folding away and snapping the
  folder display back to full width. The hover icon menu's Copy / Cut /
  Rename / Delete buttons act on the entry under the cursor without selecting
  it, so pressing one never re-targets or pops open the preview.
- **Video preview mode:** the command bar "Video" dropdown selects how a
  selected video plays in the preview: *Autoplay* (full playback with
  sound, the default), *5 s clip* (a five-second muted preview in the
  `UltraCanvasAlbum` hover style, then pause) or *Still image* (paused
  first frame).
- **Open files:** double-click / Enter shows a previewable file in the
  preview pane and launches every other file with the application the OS
  registers for it (`UltraCanvasFileAssociations`), like a double-click in
  Explorer. **Open with >** is the first entry of the context menu, and
  clicking it does the same as a double-click — opens the selection with the
  default application; its submenu lists all registered applications for the
  selection (default first, with icons) plus **Other application…**, a
  file-dialog picker. Launches are detached, so closing UltraFiler leaves the
  opened applications running. The application lists
  are prewarmed in the background while a folder is shown, so the menu opens
  without any lookup delay.
- **Status bar:** entry count of the folder, selection count and summed size.
- **Extras > Open prompt** (in the file context menu's Extras submenu):
  starts the operating system's command line program
  in the folder of the active tab, detached from UltraFiler (closing the file
  manager leaves the terminal running). Without configuration it uses the
  platform default — `%COMSPEC%` (cmd.exe) on Windows, Terminal.app on macOS,
  `$TERMINAL` or the first installed terminal emulator on Linux.

## Settings

The **Settings > Settings...** menu entry opens the settings window: a tree of
pages on the left, the selected page on the right. Every change applies to the
running application immediately and is saved to the config file
(`~/.config/UltraFiler/config.ini`, `%APPDATA%\UltraFiler\config.ini`,
`~/Library/Application Support/UltraFiler/config.ini`).

| Page | Setting |
|---|---|
| Display > Treeview | The folder tree's colours: the row background of the drive entries and the highlight of the selected folder, each picked with `UltraCanvasColorPicker` |
| Display > PDF Inventory | **PDF-Inventory thumbnails width** — how wide the page thumbnails beside a PDF shown in the preview are: a fixed width in pixels (a slider from 32 to 120 px, 56 px by default) or a share of the preview's own width (5–40 %, 25 % by default), so the inventory grows with the window. Moving either slider selects its mode |
| Media Viewer > Transparent Images | Backdrop shown behind transparent images in the preview: checkered pattern or a preset colour picked with `UltraCanvasColorPicker`. The colour strip under a transparent image in the preview writes to the same setting |
| Handling > Drag & Drop | **Drop on folder** — whether dragging files onto a folder of the file display moves them (the default) or copies them. Ctrl at the drop always copies, Shift always moves |
| History & Favorites | Clears the recently-used lists, the pinned entries, and the per-folder view settings |
| Extras > Open prompt | The command line application started by **Extras > Open prompt** |

On the *Open prompt* page the folder button next to the path field opens the
file dialog filtered to applications (`*.exe`, `*.com`, `*.bat`, `*.cmd` on
Windows, `*.app` on macOS, all files on Linux); the chosen program lands in the
field and **Save app** stores it. **Use system default** clears the setting so
the platform's own command line program is used again, and **Test** starts the
program in the field right away to check the path.

## Usage

```bash
UltraFiler                  # opens the home folder
UltraFiler /path/to/folder  # opens a specific folder
```

## Building

Built by the top-level CMake project when `BUILD_ULTRAFILER_APP` is `ON`
(the default):

```bash
mkdir build && cd build && cmake .. && make UltraFiler
```
