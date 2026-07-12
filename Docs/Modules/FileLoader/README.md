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
UltraCanvas object through the native file dialog:

```cpp
// Decode an image into a UCImage
UltraCanvasFileLoader::OpenImage(opts, [](const FileLoadResult& r, std::shared_ptr<UCImage> img) { ... });

// Decode audio into a UCAudio PCM buffer (WAV/MP3/FLAC)
UltraCanvasFileLoader::OpenAudio(opts, [](const FileLoadResult& r, std::shared_ptr<UCAudio> audio) { ... });

// Parse a word-processing document into a UCRichDocument (ODT/DOCX/Markdown/TXT)
UltraCanvasFileLoader::OpenTextDocument(opts, [](const FileLoadResult& r, std::shared_ptr<UCRichDocument> doc) { ... });
```

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

### Extensible Plugin Architecture
Add support for proprietary or specialized graphics formats without modifying
core code. Register an `IGraphicsPlugin` with the graphics plugin registry and
its extensions immediately show up in the format inventory:

```cpp
// Register a graphics plugin (e.g. the built-in STL model plugin)
RegisterSTLPlugin();

// ...or your own
UltraCanvasGraphicsPluginRegistry::RegisterPlugin(std::make_shared<MyFormatPlugin>());
```

---

## Supported Formats

The lists below are what the framework implements; the *actual* set for a given
build is reported by `UltraCanvasSupportedFormats::GetAll()`. See
[formats.md](formats.md) for the full per-format load/save matrix.

**Images** (libvips, build-dependent): PNG, JPEG, WebP, AVIF, HEIC/HEIF, GIF, BMP, TIFF, TGA, HDR, EXR, JPEG XL, JPEG 2000, Netpbm, QOI, PSD, ICO, FITS
**Vector:** SVG/SVGZ (load); CorelDRAW, Xara (via plugins)
**3D Models:** STL (load + save, via plugin)
**Audio** (miniaudio): WAV (load + save); MP3, FLAC (load)
**Video** (platform backend): MP4, MOV, MKV, WebM, AVI — playback codec- and recording backend-dependent
**Documents:** PDF (load + save), DOCX/ODT (load + save), DOC (load), Markdown, TXT, HTML, EPUB, FB2, MOBI/PRC/AZW/AZW3
**Spreadsheets:** ODS, XLSX, CSV, TSV (load + save)
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
