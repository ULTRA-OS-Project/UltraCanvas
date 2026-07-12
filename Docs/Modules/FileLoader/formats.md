## Supported File Formats

FileLoader delegates to one decoder/encoder per format family. Some families
are always built into the core library; others (image codecs, PDF, the vector
and 3D plugins, the audio/video backends) are optional and depend on how the
framework was compiled and which plugins the application registers.

> **The tables below list the formats the framework *implements*. What a given
> build can actually load and save depends on the installed libraries
> (libvips image codecs, MuPDF, the platform audio/video backend) and on the
> plugins registered at runtime.** For the definitive, runtime-accurate answer
> on the current system, query the supported-format API:
>
> ```cpp
> // Everything the running build can load/save, per category, with the
> // providing library or plugin named for each entry:
> for (const auto& f : UltraCanvasSupportedFormats::GetAll()) { ... }
>
> // Or one category, ready for a file dialog filter:
> auto exts = UltraCanvasFileLoader::GetSupportedLoadExtensions(
>                 MediaFormatCategory::Bitmap);
> ```

Legend: ✅ supported · — not supported · † depends on the installed libvips
build · ‡ requires the matching plugin to be compiled and registered ·
§ depends on the platform backend and installed codecs.

### Bitmaps — libvips †

| Format | Extension(s) | Load | Save |
|--------|--------------|:----:|:----:|
| PNG | `.png` | ✅ | ✅ |
| JPEG | `.jpg`, `.jpeg`, `.jfif` | ✅ | ✅ |
| WebP | `.webp` | ✅ | ✅ |
| AVIF | `.avif` | ✅ | ✅ † |
| HEIC / HEIF | `.heic`, `.heif` | ✅ † | ✅ † |
| GIF | `.gif` | ✅ | ✅ |
| BMP | `.bmp` | ✅ | ✅ |
| TIFF | `.tif`, `.tiff` | ✅ | ✅ |
| TGA | `.tga` | ✅ | — |
| Radiance HDR | `.hdr` | ✅ | ✅ |
| OpenEXR | `.exr` | ✅ † | — |
| JPEG XL | `.jxl` | ✅ † | ✅ † |
| JPEG 2000 | `.jp2`, `.j2k` | ✅ † | ✅ † |
| Netpbm | `.ppm`, `.pgm`, `.pbm`, `.pnm` | ✅ | ✅ |
| QOI | `.qoi` | ✅ | — |
| Photoshop | `.psd` | ✅ † | — |
| Windows icon | `.ico` | ✅ † | — |
| FITS | `.fits` | ✅ † | ✅ † |

HEIC/AVIF encoding needs a libvips built with libheif and the matching
encoder (x265 for HEIC, aom/rav1e for AVIF); several distro builds ship the
decoder only. Formats marked † that lack a dedicated libvips loader may still
load through the ImageMagick delegate when libvips was built with it.

### Vector graphics

| Format | Extension(s) | Load | Save | Provider |
|--------|--------------|:----:|:----:|----------|
| SVG | `.svg`, `.svgz` | ✅ | — | librsvg (via libvips) |
| CorelDRAW | `.cdr`, `.cmx` | ✅ ‡ | — | CDR plugin (libcdr) |
| Xara | `.xar`, `.web` | ✅ ‡ | — | XAR plugin |
| PostScript / EPS | `.eps`, `.ps` | ✅ † | — | libvips delegate |

There is no SVG *writer* in the image pipeline — SVG is load-only. EPS/PS load
only when libvips was built with a PostScript loader (Ghostscript delegate).

### 3D models

| Format | Extension(s) | Load | Save | Provider |
|--------|--------------|:----:|:----:|----------|
| STL | `.stl` | ✅ ‡ | ✅ ‡ | STL plugin (built-in, ASCII + binary) |

STL support is provided by the STL graphics plugin; register it with
`RegisterSTLPlugin()`. Other 3D formats (OBJ, glTF/GLB, FBX, 3DS, PLY,
COLLADA) are **not** implemented by the FileLoader path — the demo app has a
minimal OBJ reader for its GL viewer only.

### Documents

| Format | Extension(s) | Load | Save | Provider |
|--------|--------------|:----:|:----:|----------|
| PDF | `.pdf` | ✅ ‡ | ✅ ‡ | MuPDF |
| Word (OOXML) | `.docx` | ✅ | ✅ | built-in (miniz + tinyxml2) |
| OpenDocument Text | `.odt` | ✅ | ✅ | built-in (miniz + tinyxml2) |
| Word 97-2003 | `.doc` | ✅ | — | built-in OLE2/CFB parser (text import only) |
| Markdown | `.md`, `.markdown` | ✅ | ✅ | built-in Markdown engine |
| Plain text | `.txt` | ✅ | ✅ | built-in |
| HTML | `.html`, `.htm`, `.xhtml` | ✅ | ✅ | built-in HTMLReader / `ToHTML` |
| EPUB | `.epub` | ✅ | — | built-in eBook engine |
| FictionBook | `.fb2`, `.fb2.zip` | ✅ | — | built-in eBook engine |
| Mobipocket / Kindle | `.mobi`, `.prc`, `.azw`, `.azw3` | ✅ | — | built-in eBook engine |

E-book engines self-register via `RegisterBuiltinEBookEngines()`; only the
engines linked into the binary are reported. RTF is **not** supported.

### Spreadsheets — built-in

| Format | Extension(s) | Load | Save |
|--------|--------------|:----:|:----:|
| OpenDocument Spreadsheet | `.ods` | ✅ | ✅ |
| Excel workbook (OOXML) | `.xlsx` | ✅ | ✅ |
| Comma-separated values | `.csv` | ✅ | ✅ |
| Tab-separated values | `.tsv` | ✅ | ✅ |

### Audio — miniaudio

| Format | Extension(s) | Load | Save |
|--------|--------------|:----:|:----:|
| WAV | `.wav` | ✅ | ✅ |
| MP3 | `.mp3` | ✅ | — |
| FLAC | `.flac` | ✅ | — |

The bundled miniaudio backend decodes WAV/MP3/FLAC and encodes WAV only.
Ogg Vorbis, AAC/M4A, Opus and WMA are **not** decoded — no decoder for them is
wired into the backend.

### Video — platform backend §

| Format | Extension(s) | Load | Save § |
|--------|--------------|:----:|:----:|
| MP4 | `.mp4`, `.m4v` | ✅ | ✅ |
| QuickTime | `.mov` | ✅ | ✅ (Linux/macOS) |
| Matroska | `.mkv` | ✅ | ✅ (Linux) |
| WebM | `.webm` | ✅ | ✅ (Linux) |
| AVI | `.avi` | ✅ | ✅ (Linux) |

Video is handled by the platform backend — GStreamer (Linux), Media Foundation
(Windows) or AVFoundation (macOS). Playback codec support depends on the
plugins/codecs installed on the system; recording is limited to the container
set the active backend can mux (Windows and macOS record MP4/H.264+AAC). When
the framework is built without a video backend, no video formats are reported.
