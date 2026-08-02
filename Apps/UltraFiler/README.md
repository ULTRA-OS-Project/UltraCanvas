# UltraFiler

A Windows Explorer style file manager built entirely from UltraCanvas
components:

| Area | Component |
|---|---|
| Folder tree (left pane) | `UltraCanvasTreeView` — lazily populated filesystem tree (Home, drives / mounted volumes) |
| Folder content (center pane) | `UltraCanvasFilerWidget` — details / list / thumbnail grids / size bars / treemap views, full file context menu, clipboard and drag & drop interop |
| Preview (right pane) | `UltraCanvasMediaViewer` — images, video, audio, PDFs, spreadsheets, 3D models and text files |
| Path bar | `UltraCanvasBreadcrumb` via the shared `BuildFolderBreadcrumb` helper |
| Panes | `UltraCanvasSplitPane` with draggable splitters |

## Features

- **Navigation:** Back / Forward history, Up, Refresh, clickable breadcrumb
  path (each segment's dropdown lists sibling folders), folder tree with lazy
  expansion.
- **Command bar:** New folder / New file (inline rename starts
  automatically), Cut / Copy / Paste (system clipboard interop), Rename,
  Delete (with confirmation), sort field + direction, view type selection,
  Preview pane toggle.
- **File display:** everything `UltraCanvasFilerWidget` offers — sortable
  Details columns, thumbnail grids with async decoding, the size-bar and
  treemap views, hover icon menu, selection info bar, archive browsing
  (VirtualFS), compress / extract, drag & drop to and from other
  applications.
- **Preview:** selecting a single media file shows it in the preview pane;
  double-click / Enter opens it there too (the preview pane un-hides when
  needed). The viewer provides zoom, rotation, color adjustments, slideshow,
  and per-kind views for documents, spreadsheets, models, audio and video.
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
