# VirtualFS

**Unified virtual file system layer for ULTRA OS.**
Sibling of `UltraCanvas` (UI), `UltraNet` (networking), and `UltraAI` (AI capabilities).

VirtualFS gives every ULTRA OS app a single, transparent interface
for accessing files inside archives — ZIP, 7z, TAR, RAR, ISO, CAB,
and 40+ other formats — as if they were regular folders, with a
plugin architecture for format extensions.

> Status: Public API specified in the master registry. Implementation
> complete for core providers; this overview reflects what apps and
> other ULTRA OS modules can rely on.

---

## Why it exists

Archive handling is exactly the kind of concern an OS owns once:
format detection, decompression, entry caching, password handling,
nested archive traversal, progress reporting, streaming extraction.
Without a shared module, every UltraCanvas app and every file manager
would re-implement the same plumbing — and inevitably get the edge
cases wrong.

VirtualFS centralises that plumbing behind a stable C-style API
(`VirtualFS_*` free functions + opaque handles) backed by
**libarchive** for the core, with optional **libmspack** (CHM/LIT),
**wimlib** (WIM), and direct **zlib/zstd/lz4** for raw compression.

---

## Formats at a glance

### Core (Tier 1, via libarchive)

| Category | Formats | Extensions |
|---|---|---|
| General | ZIP, 7-Zip | `.zip`, `.7z`, `.cbz`, `.cbr` |
| TAR family | TAR, GZIP, BZIP2, XZ, LZMA, Zstd | `.tar`, `.tgz`, `.txz`, `.tar.zst` |
| Microsoft | CAB (LZX, MS-ZIP, Quantum) | `.cab` |
| Disc images | ISO 9660, UDF | `.iso`, `.udf` |
| Unix/Linux | CPIO, AR, DEB, RPM | `.cpio`, `.deb`, `.rpm` |
| App bundles | JAR, APK, IPA, EPUB, Office XML | `.jar`, `.apk`, `.epub`, `.docx` |
| RAR | RAR v4/v5 (read-only) | `.rar`, `.cbr` |

### Plugin-supplied (Tier 2/3)

`CHM`, `LIT`, `WIM`, `NSIS`, `InnoSetup`, `SquashFS`, `CramFS`,
`ZPAQ`, `ACE`. Each is added through the `IVirtualFSProvider`
interface in `VirtualFS/VirtualFSProvider.h`.

---

## Compression algorithms

| Algorithm | Type | Speed | Ratio | Use case |
|---|---|---|---|---|
| Deflate | `VirtualFSCompressionMethod::Deflate` | Fast | Good | ZIP default |
| GZIP | `UCVFSCompressionType::GZIP` (bridge) | Fast | Good | Web, HTTP |
| BZip2 | `VirtualFSCompressionMethod::BZip2` | Slow | Better | Legacy TAR |
| LZMA/LZMA2 | `VirtualFSCompressionMethod::LZMA2` | Slow | Best | 7-Zip, XZ |
| **Zstandard** | `VirtualFSCompressionMethod::Zstd` | Very fast | Excellent | **Recommended** |
| LZ4 | `VirtualFSCompressionMethod::LZ4` | Fastest | Moderate | Real-time |
| LZX | `UCVFSCompressionType::LZX` (bridge) | Moderate | Good | CAB, CHM, WIM |
| Brotli | `VirtualFSCompressionMethod::Brotli` | Moderate | Excellent | Web content |

### Raw buffer compression (no archive container)

`VirtualFSCompression.h` exposes the compression codecs directly for
modules that define their own binary formats and only need a
compress/decompress primitive — e.g. the UltraWeb bundler compressing
`.ucpkg` section payloads:

```cpp
#include <VirtualFS/VirtualFSCompression.h>
using namespace VirtualFS;

std::vector<uint8_t> compressed;
VirtualFS_CompressBuffer(input, compressed, VirtualFSCompressionMethod::LZ4);

std::vector<uint8_t> restored;
VirtualFS_DecompressBuffer(compressed, restored);   // method auto-detected
```

Supported methods: `Store`, `Deflate` (zlib/gzip), `Zstd`, `LZ4`
(standard LZ4 frame format, interoperable with any LZ4F decoder), and
`Brotli` — each available only when the matching `VIRTUALFS_USE_*`
option is enabled; query with `VirtualFS_IsCompressionMethodAvailable()`.
`VirtualFS_DetectCompressionMethod()` identifies a stream by its magic
bytes (Brotli excepted — it has none).

---

## Architecture

```
App / Module (UltraCanvas, FileDialog, Texter, UltraFiler, ...)
  │
  ├─► VirtualFS_ListDirectory / ReadFile / ExtractAll / CreateArchive
  │     │
  │     ├─► VirtualFSManager                    (singleton, caching)
  │     │     │
  │     │     ├─► VirtualFSPath                 (path parsing, archive detection)
  │     │     │
  │     │     └─► IVirtualFSProvider            (format handlers)
  │     │           ├─► VirtualFSLibArchiveProvider  (ZIP, 7z, TAR, CAB, ...)
  │     │           ├─► VirtualFSCHMProvider         (CHM via libmspack)
  │     │           └─► VirtualFSWIMProvider         (WIM via wimlib)
  │     │
  │     └─► Platform glue                       (Linux / Win / Mac / UltraOS)
  │
  └─► UltraCanvasVirtualFSBridge                (UltraCanvas integration)
```

* Single C-style entrypoint surface (`VirtualFS_*`).
* `VirtualFSResult` for every operation — explicit error codes with
  human-readable messages.
* Transparent nested archive support (`/backup.zip/docs/report.7z/data.csv`).
* Password callbacks for encrypted archives.
* Progress callbacks for extraction/creation with cancellation.
* Entry caching for performance on repeated access.

---

## Quick examples

```cpp
#include <VirtualFS/VirtualFS.h>
using namespace VirtualFS;

// Initialize
VirtualFS_Initialize();

// List contents of a ZIP as if it were a folder
auto entries = VirtualFS_ListDirectory("/home/user/archive.zip");
for (const auto& entry : entries) {
    std::cout << entry.name << " (" << entry.size << " bytes)\n";
}

// Read file from inside archive — decompression is automatic
std::vector<uint8_t> data;
VirtualFS_ReadFile("/home/user/archive.zip/docs/readme.txt", data);

// Nested archives work transparently
VirtualFS_ReadFile("/backup.tar.gz/project.zip/src/main.cpp", data);
```

```cpp
// Extract entire archive with progress
VirtualFS_ExtractAll("/home/user/backup.7z", "/tmp/extracted",
    VirtualFSExtractOptions::Default(),
    [](const VirtualFSProgress& p) {
        std::cout << p.percentComplete << "% " << p.currentFile << "\n";
        return true;  // continue (return false to cancel)
    });
```

```cpp
// Create new archive
VirtualFS_CreateArchive("/home/user/new.zip",
    {"/home/user/docs", "/home/user/images"},
    VirtualFSOpenOptions::Default());
```

```cpp
// UltraCanvas integration — drop-in compression
#include <UltraCanvasVirtualFSBridge.h>
using namespace UltraCanvas;

// Compress data with Zstandard (recommended)
std::vector<uint8_t> compressed;
UCVFSBridge::ZstdCompress(rawData, compressed, 3);

// Decompress
std::vector<uint8_t> decompressed;
UCVFSBridge::ZstdDecompress(compressed, decompressed);

// Auto-detect format and decompress
UCVFSBridge::DecompressAuto(unknownData, decompressed);
```

---

## Module layout

```
VirtualFS/
├── include/
│   ├── VirtualFS.h              (main public header)
│   ├── VirtualFSTypes.h         (enums, structs, callbacks)
│   ├── VirtualFSPath.h          (path parsing, archive detection)
│   ├── VirtualFSProvider.h      (IVirtualFSProvider interface)
│   └── VirtualFSManager.h       (singleton manager)
├── core/
│   └── VirtualFSManager.cpp     (manager implementation)
├── providers/
│   ├── VirtualFSLibArchiveProvider.h/.cpp   (40+ formats)
│   ├── VirtualFSCHMProvider.h/.cpp          (CHM via libmspack)
│   └── VirtualFSWIMProvider.h/.cpp          (WIM via wimlib)
├── integration/
│   ├── UltraCanvasVirtualFSBridge.h         (UltraCanvas API)
│   └── UltraCanvasVirtualFSBridge.cpp
├── OS/<Platform>/
│   └── VirtualFSPlatform.cpp
└── CMakeLists.txt
```

CMake target: `VirtualFS`. Header include style: `<VirtualFS/VirtualFS.h>`.

---

## Integration with sibling modules

| Caller | Uses VirtualFS for |
|---|---|
| **UltraCanvas FileDialog** | Browsing into archives as folders, preview extraction. |
| **UltraFiler** | Full archive management — browse, extract, create, modify. |
| **Texter** | Opening documents from inside archives directly. |
| **ULTRA Store** | Package extraction (`.ucpkg` bundles use Zstd). |
| **IODeviceManager** | Firmware archives, driver packages. |
| **UltraAI** | Model weight archives (`.safetensors.zst`, `.gguf` bundles). |
| **Package/update tooling** | Delta updates, compressed asset bundles. |

VirtualFS integrates with **UltraCanvas** through the bridge module,
providing `UCVFSBridge::Compress/Decompress` functions that replace
direct zlib calls throughout the codebase.

---

## Conventions

* **Naming:** `VirtualFS_<Action>()` (e.g. `VirtualFS_ListDirectory`,
  `VirtualFS_ReadFile`). Types use `VirtualFS<Type>`. Provider
  interfaces use `IVirtualFSProvider`. Callbacks use `on<Event>`
  (base verb form per Code Guidelines V8).
* **Errors:** every call returns `VirtualFSResult`. Operator `bool`
  for quick success checks; `VirtualFSResultToString()` for messages.
* **Paths:** forward slashes only, normalized automatically. Archive
  boundaries detected by extension matching against 40+ known formats.
* **Passwords:** set via `VirtualFS_SetPasswordCallback()` — the
  callback is invoked when an encrypted archive is encountered.
* **Threading:** extraction/creation callbacks run on the calling
  thread. For async operations, wrap in `std::async` or UltraCanvas
  task system.
* **Reserved:** never write `ZipFile`, `TarArchive`, `Uncompress()`
  at module level — always go through `VirtualFS_*` or
  `UCVFSBridge::*` API.

---

## Dependencies

| Library | Purpose | Required |
|---|---|---|
| **libarchive** | Core format support (40+ formats) | Yes¹ |
| **zlib** | Deflate/GZIP compression | Yes¹ |
| **libzstd** | Zstandard compression | Recommended — `-DVIRTUALFS_USE_ZSTD=ON` (default OFF) |
| **liblz4** | LZ4 fast compression | Optional — `-DVIRTUALFS_USE_LZ4=ON` (default OFF) |
| **libmspack** | CHM, LIT, CAB (extended) | Planned — CHM provider not yet wired |
| **wimlib** | WIM format | Planned — WIM provider not yet wired |
| **libbrotli** | Brotli compression | Planned — detected by CMake, not yet used |

¹ The build degrades gracefully: configure succeeds without libarchive or
zlib; the affected features are compiled out with a warning.

---

## Status

| Component | State |
|---|---|
| Public API (master registry) | Locked at v1.0.0 |
| VirtualFSManager | ✅ Complete |
| VirtualFSPath | ✅ Complete |
| LibArchive provider | ✅ Complete |
| UltraCanvas bridge | ✅ Complete |
| CHM provider (libmspack) | Planned |
| WIM provider (wimlib) | Planned |
| Linux / macOS / Windows | ✅ Supported |
| ULTRA OS native | Planned |

---

## Reference

* **Master registry** — `VirtualFS_Master_Registry_V1.md`: full
  function list, types, callbacks, provider interfaces, supported
  formats, and integration patterns.
* **UltraCanvas integration** — `UltraCanvasVirtualFSBridge.h`:
  compression API, archive operations, legacy compatibility.
* **Code Guidelines** — `UltraCanvas_Code_Guideline_V8.md`: naming
  conventions, callback patterns, error handling.

---

*Part of ULTRA OS · MIT license · Cloverleaf UG*
