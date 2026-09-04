# UltraCanvasFontFile

Reads a **font definition file** — `.ttf`, `.otf`, `.ttc`, `.woff`, Type 1,
the bitmap formats — as a *document*: its name records are metadata you can
show, and a line of its own glyphs is a thumbnail you can draw. Together with
`UltraCanvasApplicationBase::RegisterFontFile()` it also makes a font file
usable for real text rendering without installing it.

Header: `include/UltraCanvasFontFile.h`. Implementation:
`core/UltraCanvasFontFile.cpp`. Backed by FreeType, which is a hard
dependency of the framework, so every build has this.

## Why it is separate from the text stack

The framework's text pipeline takes a **family name** (`FontStyle::fontFamily`)
and resolves it through fontconfig/Pango. That is the right shape for drawing
text and the wrong shape for two other jobs:

- **Showing a font to a person.** A file manager must thumbnail a folder of
  downloaded fonts *before* any of them is installed, on background threads,
  without a render context.
- **Learning what is in a file.** "Which families does this `.ttc` contain?"
  has to be answerable before you can ask for one by name.

So everything here goes straight at the file with FreeType: no fontconfig, no
Pango, no window, no installation. Every call creates and destroys its own
`FT_Library`, which is what makes concurrent calls from several worker threads
safe.

## Reading a font file

```cpp
#include "UltraCanvasFontFile.h"

UltraCanvas::FontFileInfo info;
if (UltraCanvas::ReadFontFileInfo("/home/me/Downloads/Inter.ttf", info)) {
    for (const auto& face : info.faces) {
        std::cout << face.family << " / " << face.subfamily
                  << "  (" << face.glyphCount << " glyphs)\n";
    }
}
```

`ReadFontFileInfo` returns false for a file that is missing, unreadable, or
not a font this FreeType understands. It never throws on a malformed file.

`FontFileInfo` carries the container: `format` (a `FontFileFormat`),
`formatName` (FreeType's own name for it), `fileSize`, `faceCount`, and one
`FontFaceInfo` per face — a plain `.ttf` has one, a `.ttc` collection has
several.

`FontFaceInfo` carries the name-table records, already decoded to UTF-8 and
empty when the font does not have them: `family`, `subfamily`, `fullName`,
`postScriptName`, `version`, `copyright`, `trademark`, `manufacturer`,
`designer`, `license`, `licenseURL`, `sampleText` — plus the technical
facts: `glyphCount`, `unitsPerEM`, `scalable`, `fixedWidth`, `hasKerning`,
`bold`, `italic`, `hasUnicodeCharmap`, and `fixedSizes` (the strike heights of
a bitmap-only face).

The name table stores the same string several times over, once per platform,
encoding and language the font was built for. The reader scores the records
and keeps the best one per name id, preferring Windows Unicode US-English, so
a font that also carries its family name in MacRoman Japanese still reads as
English. Where the font has the typographic names (ids 16/17) they win over
the legacy ones (1/2), so a split-weight family reads as `Ubuntu` / `Light`
rather than `Ubuntu Light` / `Regular`.

## Drawing a specimen

```cpp
auto pixmap = UltraCanvas::RenderFontSpecimenPixmap(path, 96, 96, deviceScale);
if (pixmap) ctx->DrawPixmap(*pixmap, rect, ImageFitMode::Contain);
```

The result is a white card with a line of the font's own glyphs on it, sized
to fill the box — the same "sheet of paper" idiom the document and PDF
previews use, so a font sits among them in a thumbnail grid without looking
like a different kind of thing. Returns null when the file cannot be opened,
holds no glyphs, or nothing legible fits.

`width`/`height` are logical pixels and `scale` is the device scale, exactly
as for every other preview producer: `scale = 2` rasterizes 2× the pixels.

```cpp
UltraCanvas::FontSpecimenOptions options;
options.text = "Hamburgefonstiv";          // default: see below
options.textColor = Color(26, 26, 28, 255);
options.backgroundColor = Colors::White;
options.faceIndex = 1;                      // a face inside a .ttc
options.padding = 3;
auto pixmap = RenderFontSpecimenPixmap(path, 240, 60, 1.0f, options);
```

The size is chosen by fitting the run's **ink** box to the padded area, so a
narrow box shrinks the glyphs to fit the width and a tall one fills the
height. A face with fixed strikes only (BDF/PCF/FON) gets the largest strike
that fits.

Because of that fit, the default sample follows the shape of the box:
`"AaBbCc"` where it is at least twice as wide as it is tall, `"Ag"` otherwise.
A six-glyph line in a square tile is fitted by its *width* and ends up a small
fraction of the tile tall — too small to read the letterforms off, which is
the whole point of a specimen. Two glyphs fill the same tile. Set
`options.text` for a fixed sample regardless of shape.

Two limits are worth knowing:

- **No shaping.** Glyphs are looked up per character and advanced with
  kerning; there is no HarfBuzz pass, no bidi and no fallback face. That is
  enough for the Latin specimen text and is the only thing possible here —
  shaping through Pango would need the font registered and a context, which
  is exactly what this module exists to avoid. Pass ASCII-ish specimen text.
- **Symbol and icon fonts** have no glyph for the sample characters at all.
  Rather than produce an empty card, the specimen then falls back to drawing
  the face's own first few glyphs, so a Font Awesome or a dingbat file
  previews as its own icons.

## Recognition

- `IsFontFileExtension(pathOrExtension)` — true for an extension this module
  can open. Takes a path or a bare extension, with or without the dot, in any
  case. It answers for the *extension*: whether this build's FreeType can
  really decode the file (WOFF2 needs Brotli) is decided by
  `ReadFontFileInfo`.
- `FontFormatForExtension(pathOrExtension)` — the format an extension names,
  before the file is opened.
- `FontFormatName(format)` — `"TrueType"`, `"OpenType"`, `"Type 1"`,
  `"WOFF"`, `"WOFF2"`, `"Bitmap font"`.

Fonts are also in the framework-wide format inventory
(`UltraCanvasSupportedFormats`) under `MediaFormatCategory::Font`, so a file
dialog can build a font filter with
`GetLoadExtensions(MediaFormatCategory::Font)`. They are listed as loadable
there and are **never** loadable by the image pipeline —
`CanImagePipelineLoad("ttf")` stays false, so a font is never handed to a
raster decoder by mistake.

## Registering a font for text rendering

Reading and previewing need no registration. Actually *drawing text* with a
font file does — the text pipeline resolves families by name:

```cpp
auto* app = UltraCanvasApplication::GetInstance();

FontFileInfo info;
if (ReadFontFileInfo(path, info) && app->RegisterFontFile(path)) {
    FontStyle style;
    style.fontFamily = info.faces[0].family;   // now resolvable
    style.fontSize = 18;
    ctx->SetFontStyle(style);
}
```

`RegisterFontFile()` is **process-private**: nothing is installed for the user
or for other applications, and it lasts until the process exits. Use it for
fonts an application ships or downloads rather than requires to be installed —
a document that embeds its fonts, a theme that comes with one, a font manager
previewing a candidate before it is installed.

| | |
|---|---|
| `bool RegisterFontFile(path)` | Registers the file. Registering the same file twice is a no-op that returns true; returns false when the file does not exist or the platform's font system rejects it. |
| `bool IsFontFileRegistered(path)` | Whether this path was registered already. |
| `std::vector<std::string> GetRegisteredFontFiles()` | Every file registered so far, in registration order. |

Per platform: FontConfig (`FcConfigAppFontAddFile`) on Linux, Android and
WASM; GDI `AddFontResourceExW(FR_PRIVATE)` **plus** FontConfig on Windows,
because Pango is pinned to its FontConfig backend there; CoreText
(`CTFontManagerRegisterFontsForURL`, process scope) on macOS.

Two caveats, both structural rather than incidental:

- **There is no unregister.** Neither FontConfig nor the framework can
  withdraw one file's faces from a running text stack without discarding
  every application font, so a registration is for the life of the process.
- **Call it from the UI thread.** It changes global font state that layout
  reads.

After a successful registration the framework calls
`RefreshFontConfiguration()` for you, which rebuilds the FontConfig FontSet
and signals Pango's default font map (`pango_fc_font_map_config_changed`) so
the new family is resolvable in the very next layout — not only in windows
created afterwards. On macOS, and on any build without PangoFT2, there is no
such signal: the default font map is dropped instead, and contexts that
already exist pick the font up when their surface is next recreated.

## In the filer

`UltraCanvasFilerWidget` classifies font files as `FilerFileCategory::Font`
and previews them as `FilerPreviewType::Fonts` — one of the kinds the
Display ▸ Thumbnails submenu toggles, on by default like all the others, and
addressable per format through the same list of files as everything else. As
with PDF pages and 3D models, the specimen is only drawn where it would be
legible: below 40 px the entry keeps its type glyph, so the icon column of a
Details row is not a smear of ink. Rendering happens on the same background
thumbnail workers as photos and video poster frames.

```cpp
filer->SetThumbnailKind(FilerPreviewType::Fonts, false);        // glyphs only
filer->SetThumbnailFormatEnabled("woff2", false);               // just this one
```

Fonts are the mirror image of the Audio kind: Audio has a detail view (a
host's player) and no thumbnail, while Fonts has a thumbnail and, so far, no
viewer — so `GetPreviewableFormats()` reports `thumbnailSupported` true for
every font format except `woff` and `woff2`, which depend on the zlib / Brotli
support the installed FreeType was built with.

## Supported formats

| Extension | Format | Notes |
|---|---|---|
| `ttf`, `ttc` | TrueType | `ttc` is a collection: `faceCount` > 1 |
| `otf`, `otc` | OpenType | CFF or CFF2 outlines |
| `woff` | Web font | needs a FreeType built with zlib |
| `woff2` | Web font 2 | needs a FreeType built with Brotli |
| `pfa`, `pfb` | PostScript Type 1 | no name table; family comes from the font program |
| `bdf`, `pcf`, `fon`, `fnt` | Bitmap fonts | fixed strikes only, `scalable` is false |

`format` in `FontFileInfo` reports what the **container really is** after the
file is opened, not what the extension claimed: an `.otf` holding `glyf`
outlines reads as `TrueType`.

## Threading

`ReadFontFileInfo` and `RenderFontSpecimenPixmap` are safe to call from any
thread, concurrently, and may block on I/O — call them off the UI thread for
anything more than a single file. `RegisterFontFile` is the opposite: UI
thread only.

## Tests

`Tests/FontFileTest.cpp` (ctest target `FontFileTest`) covers the extension
gate, the name records of the framework's own bundled Ubuntu faces, the
specimen rasterizer across sizes and scales, graceful refusal of a PNG and of
a truncated font, and the filer/inventory classification. It needs no
installed font.
