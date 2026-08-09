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
- **Command bar:** New folder / New file (inline rename starts
  automatically), Cut / Copy / Paste (system clipboard interop), Rename,
  Delete (with confirmation), sort field + direction, view type selection,
  video preview mode, Preview toggle.
- **File display:** everything `UltraCanvasFilerWidget` offers — sortable
  Details columns, thumbnail grids with async decoding, the size-bar and
  treemap views, hover icon menu, selection info bar, archive browsing
  (VirtualFS), compress / extract, drag & drop to and from other
  applications.
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
- **Status bar:** entry count of the folder, selection count and summed size.
- **Extras > Open prompt:** starts the operating system's command line program
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
| Media Viewer > Transparent Images | Backdrop shown behind transparent images in the preview: checkered pattern or a preset colour picked with `UltraCanvasColorPicker` |
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
