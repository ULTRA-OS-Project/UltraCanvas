# UltraViewer

Universal media viewer built on the UltraCanvas framework: a single
full-window [`UltraCanvasMediaViewer`](../../Docs/UltraCanvas/UltraCanvasMediaViewer.md)
that displays every media kind the framework knows and offers full player
transport controls for video and audio.

This app versions itself: [`Docs/UltraViewer/CHANGELOG.md`](../../Docs/UltraViewer/CHANGELOG.md).

## What it displays

| Kind | Formats | Displayed through |
|---|---|---|
| Bitmaps | JPEG, PNG, GIF (animated), WebP, TIFF, HEIC/HEIF, AVIF, JXL, BMP, TGA, PSD, EXR, … | Image surface (libvips pipeline) |
| Vector graphics | SVG / SVGZ (plus XPM, XBM, …) | Image surface (librsvg rasterization) |
| Video | MP4, MKV, WebM, MOV, AVI, MPEG, OGV, … | `UltraCanvasVideoPlayerElement` with play / pause / seek / scrub / volume controls |
| Audio | MP3, WAV, FLAC, OGG, Opus, M4A, AAC, … | `UltraCanvasAudioPlayerElement` with the same transport controls |
| Documents | PDF | `UltraCanvasPDFView` (MuPDF) |
| e-books | EPUB, FB2, MOBI, PRC, AZW, AZW3 | `UltraCanvasEBookViewer` (chapters, TOC, reflowing text) |
| Spreadsheets | ODS, CSV, TSV | `UltraCanvasSpreadsheet` |
| 3D models | STL | `UltraCanvasSTLElement` (OpenGL viewer) |
| Text / source / markdown | txt, md, json, xml, source code, … | Read-only `UltraCanvasTextArea` (syntax highlighting, markdown rendering) |
| UltraCanvas Documents | `*.ucd` (UCD v2 containers) | Embedded preview thumbnail + container details (full rendering arrives with the UCD v2 engine) |

The right view is chosen automatically from the file kind. Image tools
(zoom, rotate, mirror, gamma / brightness / colour adjustments, sharpen,
save-as) apply to images; zoom also drives the PDF and e-book views.

## Usage

```
UltraViewer                    # empty viewer - use Open or drag & drop
UltraViewer ~/Pictures         # browse a folder
UltraViewer photo.jpg          # show a file, browse the rest of its folder
UltraViewer a.png b.mp4 c.pdf  # view exactly these files
```

- **Browsing:** Left / Right arrows (or left / right mouse click on the
  image) move through the playlist; the folder breadcrumb at the top jumps
  anywhere in the path.
- **Slideshow:** toolbar toggle, selectable interval and transition
  (cross-fade, slide, zoom, …); videos and audio auto-advance when they end.
- **Video preview mode:** autoplay with sound (default), muted preview clip,
  or paused still frame — `SetVideoPreviewMode` on the widget.
- **Drag & drop:** drop a folder to browse it, a file to browse its folder,
  or several files to view just those.
- **e-books:** PageUp / PageDown switch chapters, the TOC panel toggles from
  the reading toolbar.

## Building

Configured by the root CMakeLists (`BUILD_ULTRAVIEWER_APP`, default ON):

```bash
mkdir build && cd build && cmake .. && make UltraViewer
```
