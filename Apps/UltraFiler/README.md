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
| Panes | `UltraCanvasSplitPane` with draggable splitters |

## Features

- **Tabs:** the "+" button on the left side of the toolbar opens an
  additional tab showing the current folder. Every tab has its own folder
  view, Back / Forward history, sort and view settings; tabs can be
  reordered by dragging and closed (the last one stays open).
- **Navigation:** Back / Forward history (per tab), Up, Refresh, clickable
  breadcrumb path (each segment's dropdown lists sibling folders), folder
  tree with lazy expansion.
- **Search:** the field on the right of the path bar searches the current
  folder recursively for names containing the text (case-insensitive, up to
  1000 matches). Enter runs it; the matches are displayed in the tab's
  current view mode, with a *Path* column after the name in Details view.
  The context menu's first entry, **Open path (in new tab)**, opens the
  selected match's folder in a new tab. Clearing the field (or navigating
  anywhere) returns to the normal folder display. Each tab keeps its own
  search.
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
