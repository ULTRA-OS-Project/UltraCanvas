# UltraCanvasMediaViewer

A comprehensive, self-contained media viewer widget
(`include/UltraCanvasMediaViewer.h`, `core/UltraCanvasMediaViewer.cpp`).
One widget displays every media kind the framework knows, chooses the right
display view automatically from the file kind, and brings its own chrome: a
folder breadcrumb, two toolbar rows (open / navigation / slideshow /
transitions, zoom / rotate / mirror / adjust / curves / save / info), an image
adjustments panel and a bottom info bar with a details popup.

Used full-window by the **UltraViewer** app (`Apps/UltraViewer`) and as the
embedded preview pane of **UltraFiler** (`Apps/UltraFiler`, with
`SetTopBarsVisible(false)`).

## Media kinds and display views

| `MediaKind` | Formats | Display view |
|---|---|---|
| `Image` | JPEG, PNG, GIF (animated), WebP, TIFF, HEIC/HEIF, AVIF, JXL, BMP, TGA, PSD, EXR, SVG/SVGZ, … | `UltraCanvasMediaSurface` (image pipeline; SVG via librsvg) |
| `Document` | PDF | `UltraCanvasPDFView` (MuPDF, `ULTRACANVAS_PLUGIN_PDF`) |
| `Sheet` | ODS, CSV, TSV | `UltraCanvasSpreadsheet` |
| `Model` | STL | `UltraCanvasSTLElement` (OpenGL viewer, 2D fallback) |
| `Text` | txt, md, json, xml, source code, … | Read-only `UltraCanvasTextArea` (syntax highlighting, markdown) |
| `Book` | EPUB, FB2, MOBI, PRC, AZW, AZW3 | `UltraCanvasEBookViewer` (chapter toolbar, TOC, reflowing content) |
| `UCDoc` | UCD v2 containers (`*.ucd`) | Image surface (embedded preview thumbnail) or text view (header summary) |
| `Video` | MP4, MKV, WebM, MOV, AVI, … | `UltraCanvasVideoPlayerElement` (`ULTRACANVAS_ENABLE_VIDEO`) |
| `Audio` | MP3, WAV, FLAC, OGG, Opus, M4A, … | `UltraCanvasAudioPlayerElement` (`ULTRACANVAS_ENABLE_AUDIO`) |

The audio / video player elements carry their own transport controls —
play / pause, a scrubbing seek bar, the time readout and a volume slider —
so the viewer gets full player control for free.

Image-only tools (rotate, mirror, tone/colour adjustments, curves, save-as) apply to
images; the zoom toolbar also drives the PDF view and, for e-books, the
reading text scale (books reflow, so zoom is a text scale and "Fit" means a
comfortable line measure across the pane).

### e-books (`MediaKind::Book`)

E-books open in an embedded `UltraCanvasEBookViewer`. Engines come from the
eBook engine registry; the viewer registers the built-ins
(`RegisterBuiltinEBookEngines()`, idempotent) itself, so it works standalone.
Keyboard: Left / Right keep browsing the folder playlist; PageUp / PageDown
switch chapters (Home / End jump to the first / last chapter) because those
keys are forwarded to the active display view.

### UltraCanvas Documents (`MediaKind::UCDoc`)

`*.ucd` containers (see `Docs/UltraCanvas/UCD-FileFormat-v2.md`) are
recognised by the 12-byte signature of the fixed header. Until the UCD v2
engine lands the viewer shows what the format deliberately makes available
without parsing the body:

- the **embedded preview thumbnail** (raw HEIC/PNG stored directly after the
  fixed header) on the image surface, when present — respecting the
  `PRIVATE` flag, which suppresses the preview;
- otherwise a **header summary** in the text view.

The info bar labels the file `UC DOCUMENT`, and the details popup lists the
container fields: type descriptor (`UCDoc`, `UCForm`, `UCVector`, …),
version, body encoding, compression, encryption and thumbnail info. A
`*.ucd` file without a valid v2 signature (e.g. a v1 XML/JSON document) gets
a summary saying so.

## Content, navigation, slideshow

```cpp
#include "UltraCanvasMediaViewer.h"
using namespace UltraCanvas;

auto viewer = CreateMediaViewer("Viewer", 0, 0, 1200, 800);
window->AddChild(viewer);

// Content — three ways in (plus drag & drop and the Open toolbar button):
viewer->OpenFolder("/home/me/Pictures");            // browse a folder
viewer->OpenFile("/home/me/Pictures/photo.jpg");    // file first, folder around it
viewer->SetFiles({ "a.png", "b.mp4", "c.pdf" });    // exactly these files

// Navigation
viewer->Next();
viewer->Previous();
viewer->GoTo(3, true);           // animated jump

// Slideshow
viewer->SetSlideshowIntervalSeconds(5.0);
viewer->SetTransition(MediaTransition::CrossFade);
viewer->PlaySlideshow();
```

`IsSupportedMedia(path)` answers whether the viewer can display a path
(static — the UltraFiler preview uses it to decide whether to open the
pane). Keyboard: Left / Right (or left / right mouse click on the picture)
browse, Space toggles the slideshow; the widget claims the window keyboard
focus when attached (`SetGrabFocusOnAttach(false)` opts out) and filters the
window's key events so browsing works while a display view holds the focus.

## Video preview behaviour

What happens when a video becomes the shown item (`SetVideoPreviewMode`):

```cpp
viewer->SetVideoPreviewMode(VideoPreviewMode::Autoplay);     // sound on (default)
viewer->SetVideoPreviewMode(VideoPreviewMode::PreviewClip);  // few seconds muted, then pause
viewer->SetVideoPreviewMode(VideoPreviewMode::Still);        // paused first frame
viewer->SetVideoPreviewClipSeconds(5.0f);                    // PreviewClip length
viewer->StopPlayback();   // for hosts that hide/detach the viewer
```

`PreviewClip` is silent end to end: the mute is decided before the source is
opened (so the engine builds a muted session rather than muting one already
wired for sound), and the clip stays muted while it sits paused at the end of
the preview. The sound returns when the viewer resumes playback itself — press
play on the transport bar and the clip continues audibly.

## Embedding as a preview pane

```cpp
auto preview = CreateMediaViewer("Preview", 0, 0, 0, 0);
preview->SetTopBarsVisible(false);      // host provides the navigation
preview->SetGrabFocusOnAttach(false);   // don't steal the host's keyboard
preview->SetTransparentBackground(TransparentImageBackground::Checkered);
```

### Letting go of the previewed file

Most kinds are read into memory and hold no handle on the file: images are
rasterized into a pixmap, text, spreadsheets, 3D models and e-books are parsed
from a buffer, and PDFs up to
[`SetMaxInMemoryBytes()`](UltraCanvasPDFExamples.md#document) (256 MiB by
default) are loaded whole. Those files stay movable, renamable and deletable
while they are shown.

Two cases genuinely keep the file open: **video and audio**, which the playback
backends stream, and a **PDF past the memory limit**, which the engine streams
page by page. `StopPlayback()` stops the sound but does not release anything.

```cpp
preview->CloseFile();   // show nothing, and release the file
```

`CloseFile()` stops playback, releases every backend's document and clears the
playlist, so the file is free the moment the call returns. A host that lets the
user act on the previewed file should call it first — the UltraFiler does that
when its preview pane folds away, which is why its folder display drops a file
out of the selection before moving it.

## Image adjustments and saving

The adjustments panel (toolbar toggle *Adjust*) drives `MediaAdjustments`
(gamma, brightness, per-channel multipliers, sharpen, auto-optimise) through
PixelFX/libvips; *Save as* bakes the current adjustments + geometry
(rotation, mirror) into a new file in any save-capable format.

Each slider is **continuous over its whole range** and carries its live value in
its caption (`Gamma  1.37`), so an edit can be read off and repeated. *Reset*
puts the controls and the values back in one pass — curves included.

### Curves (highlights, midtones, shadows)

The sliders move the whole tone range at once. To reach one part of it — lift
the shadows without blowing the highlights, drop a colour cast out of the
whites — the *Curves* toolbar button opens
[`UltraCanvasCurvesDialog`](UltraCanvasCurveEditor.md): a master (RGB) curve
plus one per colour channel, over the histogram of the shown image.

```cpp
viewer->GetSurface()->GetAdjustments().curves;   // the ToneCurveSet in force
```

The curves live in `MediaAdjustments::curves` (a `ToneCurveSet`) and are applied
**first** in the colour pipeline, as a per-channel lookup table
(`PixelFX::Colour::MapLut`); the sliders then act on the curve's result, the way
an image editor stacks a Curves layer under its brightness controls. They are
part of the adjustment like everything else, so *Save as* bakes them in too.

Dragging a point previews on the image itself at full size (untick *Preview* to
compare against the original); *Cancel*, or closing the window, restores the
curves the viewer had. The button applies to images — for any other kind of file
the info bar says so and nothing opens.
