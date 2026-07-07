# FileLoader - Universal File Handling for Modern Applications

## Simplify File Management Across All Formats

**FileLoader** is a comprehensive, cross-platform file handling system that eliminates the complexity of working with diverse file formats. Whether you're building desktop applications, content management systems, or media processing tools, FileLoader provides a single, unified API for loading, saving, and converting files across dozens of formats.

---

## Purpose

Stop wrestling with dozens of different libraries and APIs for each file format. FileLoader gives you:

- **One API for Everything** - Load images, audio, video, documents, and 3D models through a single, consistent interface
- **Effortless Format Conversion** - Convert between compatible formats with a single function call
- **Built-in Security** - Automatic malware and virus scanning protects your users
- **Cross-Platform Compatibility** - Same code works on Windows, Linux, and macOS

---

## Key Functionality

### Universal File Loading
Load any supported file format with one simple call. FileLoader automatically detects the format and applies the appropriate decoder:

```cpp
FileLoadResult result = FileLoader::LoadFile("document.pdf");
FileLoadResult image = FileLoader::LoadFile("photo.heic");
FileLoadResult model = FileLoader::LoadFile("asset.fbx");
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

### Seamless Format Conversion
Transform files between formats without manual processing. Perfect for web optimization, archival, or platform compatibility:

```cpp
// Convert iPhone photos to web-friendly format
FileLoader::ConvertFile("IMG_1234.HEIC", "photo.webp");

// Batch convert RAW images to JPEG XL
FileLoader::ConvertFile("DSC_5678.ARW", "photo.jxl");
```

### Built-in Security Scanning
Every loaded file is automatically scanned for threats, protecting your application and users:

```cpp
SecurityScanResult scan = FileLoader::ScanFileForThreats("uploaded_file.exe");
if (scan.isThreatDetected) {
    // Handle security threat
}
```

### Intelligent Caching
Automatic memory management ensures optimal performance without manual optimization:

```cpp
// Configure once, benefit everywhere
FileLoader::SetCacheSize(Size::Gigabytes(2));
FileLoader::LoadMultipleFiles(fileList); // Efficient batch processing
```

### Extensible Plugin Architecture
Add support for proprietary or specialized formats without modifying core code:

```cpp
// Register your custom format handler
auto customPlugin = std::make_shared<MyFormatPlugin>();
FileLoader::RegisterPlugin(customPlugin);
```

---

## Supported Formats

**Images:** PNG, JPEG, WebP, AVIF, HEIC, GIF, BMP, TIFF, SVG, PSD, HDR, TGA, RAW formats  
**Audio:** MP3, FLAC, WAV, OGG, AAC, M4A, OPUS, WMA  
**Video:** MP4, AVI, MKV, WebM, MOV, FLV, WMV  
**Documents:** PDF, DOCX, ODT, RTF, TXT, Markdown  
**3D Models:** OBJ, FBX, GLTF, STL, 3DS, PLY, COLLADA  
**Archives:** ZIP, RAR, 7Z, TAR, GZ, BZ2

*...and easily extensible for custom formats*

---

## Why FileLoader?

### For Developers
- **Reduce Development Time** - Stop integrating dozens of format-specific libraries
- **Single Source of Truth** - One module handles all file operations consistently
- **Bulletproof Error Handling** - Comprehensive error reporting and recovery
- **Memory Efficient** - Smart caching and resource management built-in

### For Applications
- **Enhanced Security** - Automatic threat detection protects users
- **Better Performance** - Optimized loading and intelligent caching
- **Future-Proof** - Easy to add new formats as standards evolve
- **Professional Quality** - Production-ready code with extensive testing

### For Users
- **Seamless Experience** - Open any file format without manual conversion
- **Protected** - Automatic security scanning prevents malware
- **Fast** - Optimized performance for smooth operation
- **Reliable** - Robust error handling ensures stability

---

## Perfect For

✅ **Media Applications** - Photo editors, video processors, audio workstations  
✅ **Content Management** - Document systems, digital asset managers  
✅ **3D Tools** - CAD software, game engines, visualization tools  
✅ **File Utilities** - Converters, organizers, batch processors  
✅ **Enterprise Software** - Business applications handling diverse file types

---

## Get Started

FileLoader integrates seamlessly into any C++ application:

```cpp
// Initialize once
FileLoader::InitializeModule();

// Use anywhere
auto result = FileLoader::LoadFile("any_file.format");
if (result.IsSuccess()) {
    // Process your data
    ProcessFile(result.GetData());
}

// Clean shutdown
FileLoader::ShutdownModule();
```

---

## The Bottom Line

**Stop managing file formats. Start building features.**

FileLoader handles the complexity of file operations so you can focus on what makes your application unique. With comprehensive format support, built-in security, and a simple API, FileLoader is the universal file handling solution for modern C++ applications.

**Universal. Secure. Simple.**

*FileLoader - Part of the UltraCanvas Framework*