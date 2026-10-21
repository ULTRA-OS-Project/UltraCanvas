# UltraCanvasMediaViewer

A comprehensive, self-contained media viewer widget
(`include/UltraCanvasMediaViewer.h`, `core/UltraCanvasMediaViewer.cpp`).
One widget displays every media kind the framework knows, chooses the right
display view automatically from the file kind, and brings its own chrome: a
folder breadcrumb, one compact toolbar (open / navigation / slideshow /
transitions, then zoom / rotate / mirror / adjust / curves / save), an image
adjustments panel and a bottom info bar with a Details panel.

The toolbar is **flat, small (16 px) icons with tooltips**, not captions, so
one row holds every control; a window too narrow for it scrolls the row (mouse
wheel, or the chevron at its edge) instead of clipping it. Every icon comes from the
framework's own set under `media/icons/` and is drawn as a mask, so it takes the
toolbar's foreground colour and greys out with the button; hovering shows the
caption, with the key where there is one:

| Group | Icon | Tooltip |
|---|---|---|
| Browse | `folder-open` | Open a file or folder |
| Browse | `angle-left`, `angle-right` | Previous file (Left), Next file (Right) |
| Browse | `slideshow` (toggle) | Slideshow (Space) |
| Browse | text dropdowns | Slideshow interval (`5 s` …), Slideshow transition (`Cross fade` …) |
| View | `zoom-out`, `zoom-in`, `zoom-fit` | Zoom out (-), Zoom in (+), Fit to window |
| View | text dropdown | Zoom level (`Fit`, `25%` … `400%`) |
| View | `rotate-left`, `rotate-right` | Rotate left, Rotate right |
| View | `mirror-h`, `mirror-v` | Mirror horizontally, Mirror vertically |
| Edit | `settings-sliders` (toggle) | Adjustments: gamma, brightness, colour, sharpen |
| Edit | `curves` | Curves |
| Edit | `save` | Save as… |

The browse group is always there; the view and edit groups only as far as the
file on show takes them - see
[Tools per kind of file](#tools-per-kind-of-file). *Details* is not on the
toolbar: its button sits at the right end of the info bar.

The three dropdowns stay text because a value picker shows its value; the
interval and transition ones start on the defaults. The Slideshow toggle
follows the state however it changed: Space and `PlaySlideshow()` /
`PauseSlideshow()` press and release it too.

Used full-window by the **UltraViewer** app (`Apps/UltraViewer`) and as the
embedded preview pane of **UltraFiler** (`Apps/UltraFiler`, with
`SetTopBarsVisible(false)`). To open one file full size in a window of its own —
what a double-click asks for when a pane is too small for the file — see
[UltraCanvasMediaViewerWindow](UltraCanvasMediaViewerWindow.md), which wraps a
viewer in a top-level window.

## Choosing this over a single-format view

Prefer this widget over embedding a format-specific view
([UltraCanvasPDFView](UltraCanvasPDFExamples.md#choosing-a-widget),
`UltraCanvasSpreadsheet`, `UltraCanvasEBookViewer`, …) whenever the pane shows
*whatever the user put there*. The single-format views are not an alternative to
this one — this widget owns instances of them and dispatches by file kind, so
using it costs nothing in capability and gains two things:

- **Kinds you did not plan for.** A pane built for PDFs meets a receipt
  photographed as a JPEG, an `.ods` attachment, a `.eml` body. Each is a
  different widget; the dispatch is already written here.
- **Builds where a plugin is absent.** Every plugin-backed view is behind its
  `#ifdef` inside this file, so the viewer compiles and runs without it and the
  remaining kinds still display. The PDF gate is the common case:
  `ULTRACANVAS_PLUGIN_PDF` defaults ON, but a machine without MuPDF disables the
  plugin at configure time rather than failing the build, so an application that
  embeds `UltraCanvasPDFView` itself must repeat the guards and supply its own
  fallback.

Embed it as UltraFiler does, with `SetTopBarsVisible(false)` for a pane too
small for the toolbar (`Apps/UltraFiler/UltraFilerWindow.cpp:829`).
`UltraCanvasMediaViewer::IsSupportedMedia(path)` answers, before you load
anything, whether this build can show a given file at all.

Go direct to a single-format view only when the application *is* that format's
tool and a missing plugin should stop it running.

## Media kinds and display views

| `MediaKind` | Formats | Display view |
|---|---|---|
| `Image` | JPEG, PNG, GIF (animated), WebP, TIFF, HEIC/HEIF, AVIF, JXL, BMP, TGA, PSD, EXR, SVG/SVGZ, … | `UltraCanvasMediaSurface` (image pipeline; SVG via librsvg) |
| `Document` | PDF | `UltraCanvasPDFView` (MuPDF, `ULTRACANVAS_PLUGIN_PDF`); a password-protected one is unlocked first (*Password-protected files*). Classified before `Vector`: a libvips with a PDF loader would otherwise take a PDF for a drawing and show its first page flat. Without the plugin a PDF is an image the pipeline rasterizes |
| `Sheet` | ODS, XLSX, XLS (Excel 97-2003 and 5.0/95 — see [`UltraCanvasSpreadsheetXls`](UltraCanvasSpreadsheetXls.md)), CSV, TSV | `UltraCanvasSpreadsheet` |
| `Model` | STL always; OBJ, PLY, 3DS, COLLADA, FBX, X3D/VRML, Alembic, MilkShape 3D, DirectX `.x`, `.blend` and STEP once the application has called `RegisterModelFormatsPlugin()` | `UltraCanvasSTLElement` — an OpenGL viewer where GL is enabled, and otherwise a **software-rendered still** of the mesh (the same shaded three-quarter view the Filer thumbnails with, from `UltraCanvasModelRaster.h`) rather than a text placeholder |
| `Vector` | SVG/SVGZ and EPS/PS arrive as `Image` (the pipeline rasterizes them). Everything else is a drawing: DXF, the DWG family (`.dwg`, `.dwt`, `.dws`, `.sv$`, and a `.bak` whose header says it is one), EMF, WMF, XAR and `.ai` (from its Illustrator private data) once the application has called `RegisterVectorFormatsPlugin()` | `UltraCanvasVectorElement` — the `VectorDocument` itself, sharp at any zoom, read through the [vector preview seam](UltraCanvasVectorConverters.md#previews-what-the-media-viewer-and-the-filer-show). A drawing this build has no reader for falls back to the **preview bitmap the file carries inside itself** (Xara, CorelDRAW, an EPS written with one) on the image surface, and says so when there is not even that |
| `Text` | txt, md, json, xml, source code, tex, … | Read-only `UltraCanvasTextArea` (syntax highlighting, markdown; a `.txt` / `.text` is shown as the language `DetectLanguageFromContent` reads in it, plain when it names none; a `.tex` is imported by the [LaTeX document reader](UltraCanvasLaTeXDocumentReader.md) and shown as the rendered document, formulas typeset) |
| `Book` | EPUB, FB2, MOBI, PRC, AZW, AZW3 | `UltraCanvasEBookViewer` (chapter toolbar, TOC, reflowing content) |
| `Font` | TTF, TTC, OTF, OTC, WOFF, WOFF2, PFA/PFB (Type 1), BDF, PCF, FON/FNT | `UltraCanvasFontViewer` (scrolling glyph grid, range picker) |
| `UCDoc` | UCD v2 containers (`*.ucd`) | Image surface (a layered image at full size, else the embedded preview thumbnail) or the note view (header summary) |
| `Video` | MP4/M4V, MKV, WebM, MOV, AVI, WMV/ASF, FLV, MPG, OGV, 3GP, M2TS | `UltraCanvasVideoPlayerElement` (`ULTRACANVAS_ENABLE_VIDEO`) |
| `Audio` | MP3, WAV, FLAC, OGG, Opus, M4A/M4B, AAC, WMA, AIFF | `UltraCanvasAudioPlayerElement` (`ULTRACANVAS_ENABLE_AUDIO`) |

Those two rows are not a list in this file: `IsAudioFile` and `IsVideoFile` ask
the codec registry
([UltraCanvasMediaCodecRegistry.md](UltraCanvasMediaCodecRegistry.md)) what this
build was compiled with, so a format an application registers is classified too,
and a format the build cannot decode still gets a player-shaped error naming
what is missing rather than a broken image. `UltraCanvasSupportedFormats` — also
built from the registry — is the authoritative answer to what actually decodes.
`.ts` is the one extension settled by content instead of by name: it is
TypeScript far more often than an MPEG transport stream, so its registration
carries a probe for the 188-byte packet sync bytes.

The audio / video player elements carry their own transport controls —
play / pause, a scrubbing seek bar, the time readout and a volume slider —
so the viewer gets full player control for free.

## Tools per kind of file

The toolbar's view and edit groups offer only the tools that mean something for the file
on show; the rest are hidden, not greyed, so a video or a spreadsheet is not
handed colour sliders it cannot apply. Each load decides it, and a host with
its own controls can ask the same question:

```cpp
MediaViewerTools tools = viewer->GetAvailableTools();
if (tools.colour) { /* the adjustments panel and Curves apply */ }
```

| Shown | Zoom | Rotate / mirror | Adjustments + Curves | Save as |
|---|:-:|:-:|:-:|:-:|
| Bitmap image (JPEG, PNG, WebP, HEIC, …) | yes | yes | yes | yes |
| Drawing on the image surface (SVG, an EPS the pipeline rasterizes, a vector file's embedded preview) | yes | yes | — | yes (as a bitmap) |
| Drawing in the vector view (DXF, DWG, EMF, …) | yes | — | — | — |
| `*.ucd` layered image or preview thumbnail | yes | — | — | — |
| PDF, e-book | yes | — | — | — |
| Text, spreadsheet, font, 3D model, video, audio, nothing loaded | — | — | — | — |

*Unlock* (`tools.unlock`) is there only while the shown file is
password-protected and not unlocked yet - see *Password-protected files*.
Separators follow the groups, so none is left standing alone. Text,
spreadsheet, font, 3D, video and audio views carry their own controls (the
players' transport bar, the model's orbit and dolly), so the toolbar is down to
the browse group for them; *Details* stays in the info bar for every kind.

A drawing's pixels are only a rendering of it at the size it is shown, so it
takes no gamma, colour or curve: the adjustments carry from one bitmap to the
next, but a drawing or a document preview browsed to in between is shown
unaltered. The adjustments panel folds away for such a file and comes back with
the next bitmap while its toggle is still on.

The zoom toolbar also drives the PDF view, the vector drawing view and, for
e-books, the reading text scale (books reflow, so zoom is a text scale and "Fit"
means a comfortable line measure across the pane).

## Zooming the shown page

Images and PDF pages zoom the same way: the **mouse wheel** zooms about the
pointer — what is under the cursor stays under it — and the keyboard steps with
`+` / `-`, fits with `0` and shows actual size with `1` (the PDF view adds `W`
for fit-width). The info bar reports the resulting zoom for both. The other
wheel action stays on Ctrl+wheel: Ctrl+wheel scrolls the PDF page and turns
pages at its edges.

```cpp
viewer->SetDocumentWheelZoom(false);   // plain wheel scrolls the PDF instead
```

## Backdrop under transparent images

A file that really has transparency — an alpha channel that is used, or a
vector document (SVG), which paints over whatever is behind it — gets a strip of
colours directly under the picture: an
[`UltraCanvasColorSwatchBar`](UltraCanvasColorSwatchBar.md) with the checkered
swatch first, then greys, then colours. Clicking a colour makes it the backdrop;
clicking the checkerboard goes back to the transparency pattern. Files without
transparency never show the strip, so it costs no space where it would mean
nothing — that check is `UCImage::HasTransparency` (the Cairo image,
`libspecific/Cairo/ImageCairo.h`), which ignores the fully opaque alpha
channel a PNG export routinely carries.

```cpp
viewer->SetTransparencyPaletteVisible(false);   // host provides its own chooser
// Remember what the user picked:
viewer->onTransparentBackgroundChanged =
        [&](TransparentImageBackground mode, const Color& color) {
    settings.checkered = (mode == TransparentImageBackground::Checkered);
    settings.color = color;
};
// Different colours, or different metrics:
viewer->GetTransparencyPalette()->SetColors({ Colors::White, Colors::Black });
```

The strip stays in step with `SetTransparentBackground()` / `SetTransparentColor()`
however they are called: the matching swatch is marked, and a colour the palette
does not hold (one picked in a settings dialog) simply marks none of them.

## PDF page inventory

The thumbnail strip beside a PDF page takes its width one of two ways: an
absolute pixel width — the same strip whatever the viewer's size, which is what
a preview pane wants — or a share of the viewer's own width, so the inventory
grows with the window (the default, a quarter of the width capped by the PDF
view's style).

```cpp
viewer->SetPDFThumbnailWidth(56);           // absolute: 56 px thumbnails
viewer->SetPDFThumbnailWidthFraction(0.2f); // relative: a fifth of the width
viewer->IsPDFThumbnailWidthAbsolute();      // which mode is in force
```

The viewer remembers the choice, so it also applies to documents opened later.
The strip only appears for documents with more than one page. UltraFiler
exposes this as *Settings > Display > PDF Inventory* (a slider from 32 to
120 px, 56 px by default) and applies it to its preview pane.

### e-books (`MediaKind::Book`)

E-books open in an embedded `UltraCanvasEBookViewer`. Engines come from the
eBook engine registry; the viewer registers the built-ins
(`RegisterBuiltinEBookEngines()`, idempotent) itself, so it works standalone.
Keyboard: Left / Right keep browsing the folder playlist; PageUp / PageDown
switch chapters (Home / End jump to the first / last chapter) because those
keys are forwarded to the active display view.

### Fonts (`MediaKind::Font`)

Font files open in an embedded [`UltraCanvasFontViewer`](UltraCanvasFontViewer.md)
— every glyph in the file, scrolling, with a picker for the ranges it covers.
Nothing is installed or registered to show one: the grid rasterizes straight
from the file, so a folder of downloaded candidates browses exactly like a
folder of installed ones. The info bar labels the file `FONT` and names its
family and glyph count, which is the number you compare two downloads by.

Classification happens **before** the text check, because a Type 1 `.pfa` is
ASCII that the syntax tokenizer would otherwise recognise and open as source.

### UltraCanvas Documents (`MediaKind::UCDoc`)

`*.ucd` containers (see `Docs/UltraCanvas/UCD-FileFormat-v2.md`) are
recognised by the 12-byte signature of the fixed header, read through the
container engine (`UltraCanvasUCDContainer.h`). The viewer shows what the
format deliberately makes available without parsing the body:

- a **layered image** (`UCBitmap`) whole: its layers read through
  `UCRasterDocument::LoadUCD` and composited, at full size - the picture,
  not the 256 px thumbnail - and zoomable like a photo (it is still not an
  image to turn or save from here); a password-protected one once it is
  unlocked (*Password-protected files*);
- otherwise the **embedded preview thumbnail** (raw HEIC/PNG stored
  directly after the fixed header) on the image surface, when present —
  respecting the `PRIVATE` flag, which suppresses the preview. A
  password-protected layered image shows this until it is unlocked;
- otherwise a **header summary** in the viewer's **note view**: dark like
  the rest of the viewer and laid out as the Details panel lays out its
  facts (a heading, then a table of the container's fields) - not on the
  white page a text file is shown on.

The info bar labels the file `UC DOCUMENT`, and the Details panel lists the
container fields: type descriptor (`UCDoc`, `UCForm`, `UCVector`,
`UCBitmap` …), version, body encoding, compression, encryption (always
XChaCha20-Poly1305; the key from a password or from SuperVault) and
thumbnail info. A `*.ucd` file without a valid v2 signature (e.g. a v1
document) gets a summary saying why. For a layered image it adds the size
and the layer count, and a *Shown* row says what the pane shows - the image
at full size, or the thumbnail and why not the image (password-protected,
damaged).

## Password-protected files

A file the viewer can open with its password - a layered `*.ucd` image
(UCD 2.2, see [`UltraCanvasUCDContainer.md`](UltraCanvasUCDContainer.md#password-protection))
or a PDF - first shows what it can without it: the `*.ucd`'s thumbnail
(its header summary when the document is private), or a note saying the PDF
is password-protected, both in the dark note view. An **Unlock** button comes up in the info bar (so a host
that hides the top bars, such as UltraFiler's preview pane, still offers it)
and as an icon in the toolbar; **Enter** does the same. Unlock
asks for the password over the viewer's window - again, saying so, when it
is not right - and then shows the file whole: the image at full size with
its layers composited, the PDF in its view with every page.

Nothing asks on its own: browsing a folder of protected files never pops up
a prompt. The info bar says *password-protected*, and the Details panel adds
an *Encryption* row and a *Shown* row that says what Unlock would show.

**Passwords are kept for the folder being browsed.** A password Unlock was
given opens, without asking again, the file it unlocked when it is browsed
back to, and is tried - with the others kept, the one that opened that file
first - on every other protected file shown in the same folder, which then
shows whole straight away. A file none of them opens is not tried again
until another password is given. They are held in `UltraCryptSecureBuffer`s
(zeroed when freed), at most `kMaxRememberedPasswords` (8), and only for that
folder: opening another folder, or a selection in another folder, forgets
them; `CloseFile` keeps them, so a preview pane that closes and reopens the
same folder does not ask again. `ForgetPasswords()` forgets them at once, and
`SetRememberPasswords(false)` keeps none - every protected file asks each
time it is shown.

**They do not outlive the user stepping away.** When the desktop session
locks ([UltraCanvasSessionLock](UltraCanvasSessionLock.md): the screen locked
by hand or by the screensaver), or the viewer's window has had no key or
mouse input for `GetForgetPasswordsAfterIdle()` - ten minutes unless set -
the viewer calls `ForgetPasswords()` itself. That forgets the passwords *and*
shows a file showing unlocked locked again: the PDF closes behind its note,
the image goes back to its thumbnail (or to nothing, when it is private). The
viewer watches for this only while it holds something - a kept password or
an unlocked file. Input means a key, a click, a wheel turn or the mouse moving
anywhere in the viewer's window, and a password given - typed into Unlock or
passed to `UnlockWithPassword` - but not a kept password opening a file as it
is shown, so an unattended slideshow through protected files still forgets.
`SetForgetPasswordsAfterIdle(std::chrono::seconds(0))` turns the idle half
off; the session lock still forgets. `ForgetPasswordsIfIdle()` is what the
idle timer runs.

**An unlocked file says so.** A protected file opened with its password -
typed, or kept - looks like any other once it is open, so the info bar puts
*unlocked* beside its kind (`name · PDF · unlocked · page 1 / 3 · …`,
`name · UC DOCUMENT · unlocked · …`) and the Details name its encryption.
`IsUnlocked()` reports the same.

```cpp
viewer->SetRememberPasswords(false);   // a kiosk: ask every time
viewer->ForgetPasswords();             // or: forget now - and lock the shown file again
viewer->SetForgetPasswordsAfterIdle(std::chrono::minutes(2));   // a shared desk: sooner
if (viewer->IsUnlocked()) { /* the shown file is protected, and open */ }
```

**A host's settings.** `SetSettingsAction(action)` puts a gear at the right
end of the info bar, beside Details, that runs `action`: UltraViewer opens
its settings there - *Forget passwords after*, which it passes on to
`SetForgetPasswordsAfterIdle`. The gear is hidden while no action is set, and
an empty one hides it again; the optional second argument is its tooltip.

```cpp
viewer->SetSettingsAction([this]() { ShowSettings(); });   // the gear appears
viewer->SetSettingsAction(nullptr);                        // ... and goes again
```

```cpp
if (viewer->IsLocked()) {
    viewer->Unlock();                      // asks, in a dialog
}
// ... or, with a password the host already has:
const std::string password = "tulip";
std::string why;
const MediaUnlockResult result = viewer->UnlockWithPassword(password, why);
if (result == MediaUnlockResult::WrongPassword) { /* still locked; `why` says so */ }
else if (result == MediaUnlockResult::Failed)   { /* right, but it would not open */ }
// Unlocked: shown whole. NotLocked: there was nothing to unlock.
```

What a protected PDF needs from the engine is a reason: `IPDFDocument` and
`UltraCanvasPDFView` report it with `GetLastOpenError()` - `NeedsPassword`
for a document opened without one, `WrongPassword` for one opened with the
wrong one, `Failed` for anything else - so the viewer offers Unlock rather
than calling the file damaged. A host that hides both bars still has Enter,
or calls `Unlock()` itself. Formats the framework can tell are encrypted but
not decrypt - an encrypted Excel workbook - say so in their view instead
(*"The workbook is password-protected"*), with no Unlock.

## Details panel

The **Details** button at the right end of the info bar (the `file-info`
icon) opens a scrollable panel over the
active view, for every media kind:
file name, path and size, the kind-specific facts (dimensions, channels,
colour space, dpi and loader for an image; title / author for an e-book; page
count for a PDF; container fields for a `*.ucd`) and, for images, the
file's own **metadata** read with `PixelFX::Header::ReadMetadata` — one
section per block (EXIF, IPTC, XMP, Colour / ICC, Other such as PNG text
chunks), or a line saying the file carries none.

The panel is an `UltraCanvasTextArea` in `MarkdownHybrid` mode, dark themed,
so each section is a heading over a two-column *Property / Value* table.
The mouse wheel, Up / Down and PageUp / PageDown scroll it; Escape or the
A picture whose header reads but whose pixels do not decode (a HEIC on a
build without an HEVC decoder, a truncated file) says so instead of leaving
the area empty: the display shows *Cannot decode this picture*, the info bar
reads `dice.heic · 800 x 600 · 25.4 KB · cannot decode - heif: Unsupported
feature: Unsupported codec`, and the Details panel adds a *Decoding* row with
the same reason. The surface learns it only when it draws (that is when the
pixels are decoded) and tells the viewer through
`UltraCanvasMediaSurface::onDecodeFailed`, once per shown image, after the
frame.

button again closes it. Left / Right keep browsing, and the panel follows
to the next file. The panel's text is built only while it is open (on
opening, then for each file loaded): browsing with it closed reads no
metadata.

```cpp
viewer->SetDetailsVisible(true);
viewer->ToggleDetails();
bool open = viewer->IsDetailsVisible();
```

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
pane). `ClassifyFile(path)` (static) says which view a path opens in -
`MediaKind::Image`, `Vector`, `Model`, `Video`, ... - from its name alone;
an unknown extension answers `Image`, so check `IsSupportedMedia` first.
UltraFiler uses it to preview pictures, vector drawings and 3D models from
FTP and cloud drives without downloading videos or documents. Keyboard: Left / Right (or left / right mouse click on the picture)
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
bool busy = viewer->IsPlayingMedia();   // a video or sound playing right now
```

`IsPlayingMedia()` is true while the shown file is a video or a sound that is
playing (the muted `PreviewClip` too), and false once it is paused, stopped or
has ended. A host that reopens a file which changed on disk asks it first:
reopening restarts playback from the beginning. UltraFiler's preview pane
reopens a changed video only once its file has stopped changing and it is not
playing.

`PreviewClip` is silent end to end: the mute is decided before the source is
opened (so the engine builds a muted session rather than muting one already
wired for sound), and the clip stays muted while it sits paused at the end of
the preview. The sound returns when the viewer resumes playback itself — press
play on the transport bar and the clip continues audibly.

## A file saved over

Images are read with `UCImage::GetFresh()`, which checks the shared image
cache against the file: a picture saved over since it was last shown opens as
it is now, not as the cache remembered it. The viewer does not watch the file
it shows; a host that wants it followed reopens it when the file changes, as
UltraFiler's preview pane does after a rescan finds the selected file changed.

## Embedding as a preview pane

```cpp
auto preview = CreateMediaViewer("Preview", 0, 0, 0, 0);
preview->SetTopBarsVisible(false);      // host provides the navigation
preview->SetGrabFocusOnAttach(false);   // don't steal the host's keyboard
preview->SetTransparentBackground(TransparentImageBackground::Checkered);
preview->SetPDFThumbnailWidth(56);      // a fixed-width PDF page inventory
preview->SetFitEnlarges(false);         // a small picture at its own size, not blown up
preview->SetInfoBarVisible(false);      // nothing but the picture (UltraClipboard's classic preview)
```

`SetFitEnlarges(false)` makes fit-to-window shrink only: a picture smaller
than the pane is shown at its own size, centred, instead of being enlarged to
fill it - what a hover preview wants, so an icon stays an icon rather than a
blur the size of the pane. The wheel still zooms in from there, and the info
bar reports the resulting zoom as always. The default (`true`) fills the pane.

### Taking the 3D view out of it

A model in the viewer is orbited with the mouse, and a host that embeds it as
a view *picker* needs to read that view back — or set it:

```cpp
ModelViewPose pose;
if (viewer->GetModelViewPose(pose)) { /* what the user framed */ }
viewer->SetModelViewPose(ModelViewPose::Default());          // "Reset view"
if (const Mesh3D* mesh = viewer->GetModelMesh()) { /* already loaded */ }
```

All three answer only while a model is shown (false / null otherwise). Handing
the mesh out is what lets a caller render the framed view without parsing the
file a second time — see
[`UltraCanvasModelRaster`](UltraCanvasModelRaster.md) for the rendering and
[`UltraCanvasModelViewDialog`](UltraCanvasModelViewDialog.md) for the dialog
built on exactly these three calls.

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

The adjustments panel (the `settings-sliders` toolbar toggle) drives `MediaAdjustments`
(gamma, brightness, per-channel multipliers, sharpen, auto-optimise) through
PixelFX/libvips; *Save as* bakes the current adjustments + geometry
(rotation, mirror) into a new file in any save-capable format.

*Save as* offers FileLoader's types - every bitmap type this build writes
(`UltraCanvasFileLoader::AddImageSaveTypes`: PNG first, then JPEG, WebP,
AVIF, HEIC, TIFF, GIF, QOI, ICO, … as available) - and tells the Save dialog
how big the picture is, so the **Settings** of a bitmap type offer
[another size](UltraCanvasFileDialog.md#the-pictures-size) for that save, and
writes with the type's save settings (JPEG quality, PNG compression ...) once
the user has stored any. An SVG has no pixel size of its own: it is offered as
[a drawing](UltraCanvasFileDialog.md#a-drawing), at its size on the page, and
written by drawing it again at the size chosen - sharp at any size, where the
picture shown would blur when stretched. The surface does it:

<!-- doc-check: std::shared_ptr<UltraCanvasMediaViewer> viewer; void ShowSettings(); -->
```cpp
UltraCanvasMediaSurface* surface = viewer->GetSurface();
const FileSavePicture picture = surface->GetSavePicture();   // turned as shown
FileSaveImageSize size;
size.width = picture.width * 2;
size.height = picture.height * 2;
std::string error;
surface->SaveProcessed("twice.png", size, error);   // a drawing drawn again at that size
```

Each slider is **continuous over its whole range** and carries its live value in
its caption (`Gamma  1.37`), so an edit can be read off and repeated. The
panel ends in two icon buttons: *Auto-optimise* (the `wand`), a toggle whose
pressed look says whether it is on, and *Reset* (the `reload` arrow). *Reset*
puts the controls and the values back in one pass — curves included.

### Curves (highlights, midtones, shadows)

The sliders move the whole tone range at once. To reach one part of it — lift
the shadows without blowing the highlights, drop a colour cast out of the
whites — the *Curves* toolbar button (the `curves` icon) opens
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
curves the viewer had. The button is there only for bitmap images (see
[Tools per kind of file](#tools-per-kind-of-file)); browsing on to a file the
curves do not apply to leaves an open dialog without a histogram rather than
with the previous picture's.
