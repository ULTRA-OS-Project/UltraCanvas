# FileLoader - Universal File Handling for Modern Applications

## Simplify File Management Across All Formats

**FileLoader** is a cross-platform file handling facade that reduces the
complexity of working with diverse file formats. Whether you're building
desktop applications, content management systems, or media processing tools,
FileLoader provides a single, consistent API for opening and saving files, and
a runtime inventory of exactly which formats the current build supports.

---

## Purpose

Stop wrestling with dozens of different libraries and APIs for each file format. FileLoader gives you:

- **One API for Everything** - Open images, audio, video, documents, spreadsheets and 3D models through a single, consistent interface
- **Runtime Format Inventory** - Ask the framework which formats it can actually load and save on this build, and which library/plugin provides each
- **Transparent Decompression** - gzip/zlib/Zstandard/LZ4 streams are decoded automatically, by content, regardless of extension
- **Cross-Platform Compatibility** - Same code works on Windows, Linux, and macOS

---

## Key Functionality

### Generic File Loading

Read a local path or a remote URL into memory with one call. The result is a
byte buffer plus metadata (source URI, HTTP status/content-type for URLs, and
the compression method the payload was transparently decompressed from):

```cpp
FileBytesResult pdf   = UltraCanvasFileLoader::LoadFile("document.pdf");
FileBytesResult photo = UltraCanvasFileLoader::LoadFile("photo.heic");
FileBytesResult remote = UltraCanvasFileLoader::LoadFile("https://example.com/data.json");
if (photo.success) {
    // photo.bytes holds the decoded file contents
}
```

### Typed Object Loaders

Beyond raw bytes, FileLoader can open a file straight into the matching
UltraCanvas object through a file dialog. Every FileLoader dialog (open,
open multiple, save, select folder) follows
`UltraCanvasDialogManager::SetUseNativeDialogs`: the platform's picker when
it is on, the framework's `UltraCanvasFileDialog` when it is off (the
default). A native picker blocks and runs the callback before the call
returns; the framework's runs it when the dialog closes. The framework's
dialog is built from elements: a path field with an *up* button, a folder
tree (Home, Desktop, Documents, Downloads and the mounted drives) beside the
listing of the current folder (an `UltraCanvasFilerWidget`, the UltraFiler
display), and the file-name field and file-type dropdown
below them.

<!-- doc-check: FileDialogOptions opts; void ShowPicture(std::shared_ptr<UCImage> image); void Play(std::shared_ptr<UCAudio> audio); void ShowDocument(std::shared_ptr<UCRichDocument> document); void ShowError(const std::string& message); -->
```cpp
// Decode an image into a UCImage
UltraCanvasFileLoader::OpenImage(opts, [](const FileLoadResult& r, std::shared_ptr<UCImage> img) {
    if (r.IsSuccess()) ShowPicture(img);
    else if (!r.loadError.empty()) ShowError(r.loadError);   // empty: cancelled
});

// Decode audio into a UCAudio PCM buffer (WAV/MP3/FLAC always; OGG/Opus with codec libs)
UltraCanvasFileLoader::OpenAudio(opts, [](const FileLoadResult& r, std::shared_ptr<UCAudio> audio) {
    if (r.IsSuccess()) Play(audio);
});

// Parse a word-processing document into a UCRichDocument (ODT/DOCX/DOC/LaTeX/Markdown/TXT)
UltraCanvasFileLoader::OpenTextDocument(opts, [](const FileLoadResult& r, std::shared_ptr<UCRichDocument> doc) {
    if (r.IsSuccess()) ShowDocument(doc);
});
```

### Editable Vector Documents

Vector drawings load into — and save from — the shared
`VectorStorage::VectorDocument` model (layers, groups, paths, text, gradients),
the one ArtCreator edits and the vector renderer draws. The readers and
writers are the Vector plugin's converters (SVG, XAR, EMF, WMF, DXF, DWG, AI,
EPS, CDR, PDF), installed by `RegisterVectorFormatsPlugin()`:

```cpp
#include "UltraCanvasVectorFormatsPlugin.h"          // the Vector plugin

RegisterVectorFormatsPlugin();                       // once, at start-up

std::string error;
std::vector<std::string> notes;                      // what the reader could not carry over
auto doc = UltraCanvasFileLoader::LoadVectorDocument("drawing.svg", error, &notes);
if (!doc) ShowError(error);

UltraCanvasFileLoader::SaveVectorDocument(*doc, "drawing.xar", error);   // format from the extension

opts.AddFilter("Drawings", UltraCanvasFileLoader::GetVectorLoadExtensions());
```

`UCImage` also opens `.svg` (through librsvg), but as pixels — right for a
viewer, useless to an editor. Use `LoadVectorDocument` whenever the drawing
is to be changed or written back.

### Save Settings
How a format is written - JPEG's quality, PNG's compression, TIFF's codec - is
described per format (`UltraCanvasFileSaveSettings.h`), so one dialog can offer
the settings of any of them, and the values the user chose are kept for every
application, between runs. The framework's Save dialog has a **Settings**
button on its type row that edits the chosen type's settings, and the
writers read them back - `UCRasterDocument::SaveToFile` does so on its own:

```cpp
if (UltraCanvasFileLoader::HasSaveSettings("photo.jpg"))
    UltraCanvasFileLoader::ShowSaveSettingsDialog("photo.jpg", window.get());

FileSaveSettings jpeg = UltraCanvasFileLoader::GetSaveSettings("jpg");
int quality = jpeg.GetInt("quality", 85);

UCImageSave::ImageExportOptions options;                 // the image writer's, ready made
UltraCanvasFileLoader::GetImageExportOptions("out.webp", options);
```

The bitmap formats the image writer takes options for are built in; a plugin
or an application adds its own with `RegisterSaveFormat`. A caller that passes
the picture's size (`FileDialogOptions::SetImageSize`) also gets a Size row in
a bitmap type's settings, for that save only: the size comes back through the
`SaveFileDialog` overload whose callback takes a `FileSaveImageSize`, and
`GetImageExportOptions(path, size, options)` writes at it. A drawing (SVG,
a page of shapes) has no pixel size of its own: `SetDrawingSize` gives the
size it is drawn at, offers the same row, and the caller draws it again at the
size chosen. See *Save settings*
in [the file dialog's documentation](../../UltraCanvas/UltraCanvasFileDialog.md#save-settings).

### A Drawing Saved as a Bitmap
Every file a user loads or saves in an UltraCanvas application goes through
FileLoader - only a program's own internal files (settings, caches, logs) are
read and written directly. That is what puts every format this build reads
and writes in front of the user, and the conversions between them. A
drawing's Export is two calls: the Save dialog's types for it - every vector
writer and every bitmap type this build writes, with the page's size as a
drawing so a bitmap type's Settings offer any size - and the save, which
draws the page at the size chosen (a drawing has no pixel size of its own,
so it is sharp at any) and writes it with the format's save settings:

<!-- doc-check: std::shared_ptr<VectorStorage::VectorDocument> drawing; void ShowError(const std::string&); -->
```cpp
FileDialogOptions opts;
opts.SetTitle("Export drawing");
UltraCanvasFileLoader::AddVectorDocumentSaveTypes(opts, *drawing);
UltraCanvasFileLoader::SaveFileDialog(opts, [drawing](DialogResult r, const std::string& path,
                                                      const FileSaveImageSize& size) {
    if (r != DialogResult::OK) return;
    std::string error;
    if (!UltraCanvasFileLoader::SaveVectorDocument(*drawing, path, size, error))
        ShowError(error);
});
```

`GetVectorDocumentPixelSize(document)` is the size a page starts at (96 DPI;
the model is in points), and `CanSaveImage(extensionOrPath)` says whether
this build writes a bitmap type. A picture's Save offers
`AddImageSaveTypes(opts)` - every bitmap type this build writes, PNG first -
and its Open `AddLoadTypes(opts, "Pictures", {MediaFormatCategory::Bitmap})`
(see *A Dialog's File Types* below), as UltraPaint and the media viewer do. Windows icons and QOI are read and written
by the framework's own libvips plugins, and an AVIF or HEIC type is offered
only where libheif can encode it.

### Transparent Decompression
Apps never need to care about what compression format they are dealing
with. Compressed content (gzip, zlib, Zstandard, LZ4) is detected by
magic bytes — independent of the file extension — and decompressed
automatically via the VirtualFS compression API, for local files and
URL loads alike:

```cpp
// All of these deliver the plain payload; result.decompressedFrom
// records the on-disk compression ("GZIP", "Zstandard", "LZ4", ...)
auto a = UltraCanvasFileLoader::LoadFile("styles.css.gz");
auto b = UltraCanvasFileLoader::LoadFile("dataset.json.zst");
auto c = UltraCanvasFileLoader::LoadFile("https://example.com/report.lz4");

// Opt out when the raw compressed bytes are wanted (e.g. verbatim copy)
auto raw = UltraCanvasFileLoader::LoadFile("styles.css.gz", false);
```

Archive containers (ZIP, 7z, TAR, ...) are not unpacked here — browse
those as folders through VirtualFS. Content that only looks compressed
but fails to decode is returned unmodified.

### Runtime Format Inventory
Format availability varies by build: image codecs come from libvips, PDF from
MuPDF, audio/video from the platform backend, and vector/3D formats from
plugins the application registers. Rather than guessing, ask the framework
what it can actually do right now — each entry names the providing library or
plugin and its load/save capability:

```cpp
// Every supported format, across all media categories
for (const auto& f : UltraCanvasSupportedFormats::GetAll()) {
    // f.extension, f.description, f.category, f.canLoad, f.canSave, f.provider
}

// Or just the extensions for one category — ready for a file-dialog filter
auto imgLoad = UltraCanvasFileLoader::GetSupportedLoadExtensions(MediaFormatCategory::Bitmap);
auto imgSave = UltraCanvasFileLoader::GetSupportedSaveExtensions(MediaFormatCategory::Bitmap);

// Alias-aware lookup: ".JPEG", "jpg" and "jpeg" all resolve to the same entry
auto info = UltraCanvasSupportedFormats::FindByExtension(".heic");
```

### A Dialog's File Types
An application's Open and Save dialogs offer the inventory, never a list of
their own (the rule in `AGENTS.md`), so every type this build reads or
writes is on offer and nothing it cannot:

<!-- doc-check: std::shared_ptr<UCRichDocument> letter; void ShowError(const std::string& message); -->
```cpp
// Open: one filter holding every type this build reads in the categories
FileDialogOptions open;
open.SetTitle("Import media");
UltraCanvasFileLoader::AddLoadTypes(open, "Video, pictures and sound",
    {MediaFormatCategory::Video, MediaFormatCategory::Bitmap, MediaFormatCategory::Audio});
open.AddFilter("All files", "*");

// Save: one filter per type it writes, under its name - the type the
// proposed name ends in first, since the dialog starts on the first
FileDialogOptions save;
save.SetDefaultFileName("table.ods");
UltraCanvasFileLoader::AddSaveTypes(save, MediaFormatCategory::Spreadsheet);

// A text document: what LoadTextDocument reads (ODT, DOCX, DOC, LaTeX,
// Markdown, text - not the PDFs and e-books the Document category also
// holds) and what SaveTextDocument writes (ODT, DOCX)
FileDialogOptions saveLetter;
saveLetter.SetDefaultFileName("letter.odt");
UltraCanvasFileLoader::AddTextDocumentSaveTypes(saveLetter);
UltraCanvasFileLoader::SaveFileDialog(saveLetter, [document = letter](DialogResult r, const std::string& path) {
    std::string error;
    if (r == DialogResult::OK && !UltraCanvasFileLoader::SaveTextDocument(*document, path, error))
        ShowError(error);
});
```

`AddTextDocumentLoadTypes(opts)` is the Open side: one "Documents" filter,
then each type under its name. A codec an application brings itself joins
the lists once it is registered: UltraVideo registers what its FFmpeg engine
reads and writes (`VideoFX_GetFileFormats`) with `RegisterMediaCodec`, and
FileLoader's Video and Audio lists then hold MKV, WebM and AVI on a macOS
whose platform backend reads only MP4, MOV and 3GP.

### Extensible Plugin Architecture
Add support for proprietary or specialized graphics formats without modifying
core code. Register an `IGraphicsPlugin` with the graphics plugin registry and
its extensions immediately show up in the format inventory:

<!-- doc-check: std::shared_ptr<IGraphicsPlugin> myFormatPlugin; -->
```cpp
#include "Models/STL/UltraCanvasSTLPlugin.h"

// Register a graphics plugin (e.g. the built-in STL model plugin)
RegisterSTLPlugin();

// ...or your own: an IGraphicsPlugin implementation
UltraCanvasGraphicsPluginRegistry::RegisterPlugin(myFormatPlugin);
```

---

## Supported Formats

The lists below are what the framework implements; the *actual* set for a given
build is reported by `UltraCanvasSupportedFormats::GetAll()`. See
[formats.md](formats.md) for the full per-format load/save matrix.

**Images** (libvips, build-dependent): PNG, JPEG, WebP, AVIF, HEIC/HEIF, GIF, BMP, TIFF, TGA, HDR, EXR, JPEG XL, JPEG 2000, Netpbm, QOI, PSD, ICO, FITS
**Vector:** SVG/SVGZ (load); CorelDRAW, Xara (via plugins)
**3D Models:** STL (load + save, via plugin)
**Audio** (miniaudio + optional codec libs): WAV (load + save); MP3, FLAC (load always, save with LAME / libFLAC); OGG Vorbis, Opus (load + save with libvorbis / opusfile + libopusenc)
**Video** (platform backend): MP4, MOV, MKV, WebM, AVI — playback codec- and recording backend-dependent
**Documents:** PDF (load + save), DOCX/ODT (load + save), DOC (load), Markdown, TXT, HTML, EPUB, FB2, MOBI/PRC/AZW/AZW3
**Spreadsheets:** ODS, XLSX, CSV, TSV (load + save), XLS — Excel 97-2003 and 5.0/95 (load)
**Transparent decompression:** GZIP, zlib, Zstandard, LZ4 (streams, by content — not archive containers; use VirtualFS for ZIP/7z/TAR)

*...and easily extensible for custom graphics formats via the plugin registry*

---

## Why FileLoader?

### For Developers
- **Reduce Development Time** - Stop integrating dozens of format-specific libraries
- **Single Source of Truth** - One module handles file open/save consistently
- **Know Your Capabilities** - Query the exact format support of the running build at any time
- **Memory Efficient** - Decoded images are shared through an internal cache

### For Applications
- **Consistent Behaviour** - The same open/save flow across every format
- **Accurate File Dialogs** - Populate filters from the real supported-format lists, never advertising formats the build can't handle
- **Future-Proof** - Register a plugin and its formats appear everywhere automatically
- **Portable** - One code path on Windows, Linux, and macOS

### For Users
- **Seamless Experience** - Open supported files without manual conversion
- **Fast** - Cached decoding for smooth operation
- **Reliable** - Clear error reporting through result structures

---

## Perfect For

✅ **Media Applications** - Photo editors, media viewers, audio tools
✅ **Content Management** - Document systems, digital asset managers
✅ **File Utilities** - Viewers, organizers, batch processors
✅ **Enterprise Software** - Business applications handling diverse file types

---

## Get Started

FileLoader is part of the core library — no separate initialization is needed
beyond creating the application. Load a file and check the result:

<!-- doc-check: void ProcessBytes(const std::vector<uint8_t>& bytes); -->
```cpp
auto result = UltraCanvasFileLoader::LoadFile("any_file.ext");
if (result.success) {
    // result.bytes holds the (decompressed) payload
    ProcessBytes(result.bytes);
} else {
    // result.error explains what went wrong
}
```

---

## The Bottom Line

**Stop managing file formats. Start building features.**

FileLoader handles the mechanics of opening and saving files so you can focus
on what makes your application unique — with a consistent API, transparent
decompression, and an honest, runtime-accurate view of exactly which formats
your build supports.

**Universal. Consistent. Honest.**

*FileLoader - Part of the UltraCanvas Framework*
