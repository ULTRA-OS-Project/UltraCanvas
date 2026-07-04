# UltraCanvas Image Performance Test Documentation

## Overview

The **Image Performance Test** is a built-in DemoApp benchmark that compares every bitmap codec UltraCanvas can drive (PNG, JPG, JPEG2000, WebP, AVIF, HEIF, GIF, TIFF, QOI, and optionally BMP) on the **same source image**. For each codec it measures three numbers:

1. **Encoded file size** — bytes on disk after `UCImage::Save()`
2. **Encode time** — milliseconds spent in `UCImage::Save()`
3. **Decode time** — milliseconds spent in `UCImage::Load()` *forcing full pixel materialization*

Results are streamed live into a grouped bar chart (one group per codec, three bars per group) with thumbnails of the re-encoded result under each X-axis label.

The demo is registered in `UltraCanvasDemo.cpp` under three navigation variants — **Full Pipeline Test**, **Decompress + Draw Test**, and **Draw Only Test** — that all map to the same `CreateImagePerformanceTest()` factory. The variants name the *phases* of the image pipeline the benchmark exercises:

| Variant                     | What it measures                                                                      |
|-----------------------------|---------------------------------------------------------------------------------------|
| **Full Pipeline Test**      | End-to-end: source pixels → encode → write file → read file → decode → pixel buffer   |
| **Decompress + Draw Test**  | Decompress phase isolated (file → pixel buffer), plus image element draw cost         |
| **Draw Only Test**          | Cached `UCPixmap` reused — measures only the per-frame draw cost                      |

In the v2.x codebase all three variants present the same benchmark UI; the differing labels remain to expose the underlying conceptual breakdown of the image pipeline.

**Version:** 2.5.7
**Source file:** `Apps/DemoApp/UltraCanvasImagePerformanceTest.cpp`

## Header Includes

```cpp
#include "UltraCanvasDemo.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasBoxLayout.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasApplication.h"

// QOI third-party header — provides qoi_write / qoi_read; do NOT define
// QOI_IMPLEMENTATION here, it is defined in the standalone libspecific/Cairo/qoi.cpp.
#include "../../UltraCanvas/libspecific/Cairo/qoi.h"
```

## Class Reference

### UCImage (loading and saving the source image)

The benchmark uses the `UCImage` typedef (aliased to `UCImageRaster`):

```cpp
class UCImageRaster {
public:
    // Lazy load — parses headers, does NOT decompress pixels until needed.
    static std::shared_ptr<UCImageRaster> Load(const std::string& path,
                                               bool loadOnlyHeader = true);

    // Eager save through libvips (or built-in QOI/BMP path).
    std::string Save(const std::string& imagePath,
                     const UCImageSave::ImageExportOptions& options);

    int GetWidth() const;
    int GetHeight() const;
    bool IsValid();

#ifdef HAS_LIBVIPS
    vips::VImage GetVImage();   // Direct libvips handle for advanced codecs
#endif
};

typedef std::shared_ptr<UCImage> UCImagePtr;
```

### UCImageSave::ImageExportOptions

Used to drive `Save()` per codec. The benchmark builds one with `DefaultOptionsFor(format)`:

```cpp
struct ImageExportOptions {
    int  targetWidth = 0;
    int  targetHeight = 0;
    bool maintainAspectRatio  = true;
    bool preserveMetadata     = true;
    bool preserveTransparency = true;

    UCImageSaveFormat format = UCImageSaveFormat::PNG;

    PngExportOptions  png;
    JpegExportOptions jpeg;
    WebpExportOptions webp;
    AvifExportOptions avif;
    HeifExportOptions heif;
    GifExportOptions  gif;
    BmpExportOptions  bmp;
    TiffExportOptions tiff;
    QoiExportOptions  qoi;
    // ... plus JPEG2000, JXL, TGA, ICO, PCX, HDR, EXR, DPX, CIN, FITS, PSD, SGI, FARBFELD
};
```

### UltraCanvasImageElement (used for thumbnails)

```cpp
class UltraCanvasImageElement : public UltraCanvasUIElement {
public:
    UltraCanvasImageElement(const std::string& identifier = "ImageElement",
                            long x = 0, long y = 0, long w = 100, long h = 100);

    bool LoadFromFile(const std::string& filePath);
    bool LoadFromImage(std::shared_ptr<UCImage> img);

    void SetFitMode(ImageFitMode mode);   // Contain, Cover, Stretch, ...
    void SetOpacity(float alpha);
    void SetTintColor(const Color& color);
    void SetClickable(bool enable);

    std::function<void()> onClick;
    std::function<void()> onImageLoaded;
    std::function<void(const std::string&)> onImageLoadFailed;
};
```

### Codec table (file-local)

The benchmark drives this fixed codec list in display order. `EncoderBackend` selects which encode/decode path runs:

```cpp
enum class EncoderBackend { Libvips, QOI, BMP };

struct CodecEntry {
    UCImageSaveFormat format;     // ignored when backend != Libvips
    const char* name;
    const char* extension;
    EncoderBackend backend;
    bool isOptional;
};

const CodecEntry kAllCodecs[] = {
    { UCImageSaveFormat::PNG,      "PNG",  "png",  EncoderBackend::Libvips, false },
    { UCImageSaveFormat::PNG,      "QOI",  "qoi",  EncoderBackend::QOI,     false },
    { UCImageSaveFormat::JPEG,     "JPG",  "jpg",  EncoderBackend::Libvips, false },
    { UCImageSaveFormat::JPEG2000, "JP2K", "jp2",  EncoderBackend::Libvips, false },
    { UCImageSaveFormat::WEBP,     "WebP", "webp", EncoderBackend::Libvips, false },
    { UCImageSaveFormat::AVIF,     "AVIF", "avif", EncoderBackend::Libvips, false },
    { UCImageSaveFormat::HEIF,     "HEIF", "heif", EncoderBackend::Libvips, false },
    { UCImageSaveFormat::GIF,      "GIF",  "gif",  EncoderBackend::Libvips, false },
    { UCImageSaveFormat::TIFF,     "TIFF", "tiff", EncoderBackend::Libvips, false },
    { UCImageSaveFormat::BMP,      "BMP",  "bmp",  EncoderBackend::BMP,     true  },
};
```

### CodecResult (per-codec output row)

```cpp
struct CodecResult {
    UCImageSaveFormat format = UCImageSaveFormat::PNG;
    std::string name;               // "PNG", "JPG", "QOI", ...
    std::string extension;          // "png", "jpg", "qoi", ...
    std::string encodedFilePath;    // path of the temp file produced
    size_t      encodedSizeBytes = 0;
    double      encodeTimeMs = 0.0;
    double      decodeTimeMs = 0.0;
    bool        succeeded = false;
    std::string errorMessage;
};
```

## Events / Callbacks

The benchmark wires up the following user-facing actions:

| Control            | Callback              | Behavior                                                          |
|--------------------|----------------------|-------------------------------------------------------------------|
| `chooseBtn`        | `onClick`             | Opens native file dialog, calls `loadSourceImage`, auto-runs      |
| `includeBmpCheckbox` | `onCheckedChanged`  | Rebuilds active codec set, re-runs benchmark                      |
| Thumbnail per codec| `onClick`             | Opens `ShowFullSizeImageViewer(path)` at 1:1 zoom                 |
| `chartElem`        | `onGeometryChanged`   | Repositions thumbnails under their bar groups when bounds shift   |

Internally, the demo exposes two lambdas — `loadSourceImage` and `runBenchmark` — that callers can hook into if they want to drive the benchmark programmatically.

## Test Modes (Pipeline Phases)

The benchmark times distinct phases of the image pipeline. The pseudocode below mirrors the timed regions in `runBenchmark`.

### Full Pipeline (encode + write + read + decode)

```cpp
// Full pipeline: source pixels → encoded file → decoded pixels
t0 = NowMs();
std::string err = state->sourceImage->Save(outPath, opts);
t1 = NowMs();
r.encodeTimeMs = t1 - t0;

d0 = NowMs();
auto round = UCImage::Load(outPath, false);
#ifdef HAS_LIBVIPS
// Force eager decode: libvips is lazy and would otherwise only parse the
// header. copy_memory() walks the full pipeline and writes every decoded
// pixel into a contiguous RAM buffer.
vips::VImage v = round->GetVImage();
v = v.copy_memory();
#endif
d1 = NowMs();
r.decodeTimeMs = d1 - d0;
```

### Decompress + Draw

Decompression is timed via the same `Load() + copy_memory()` sequence above. The "+ draw" portion comes from the thumbnail update:

```cpp
auto thumb = state->thumbs[globalIdx];
thumb->LoadFromFile(outPath);   // decode + draw into the thumbnail element
thumb->SetVisible(true);
thumb->RequestRedraw();
```

### Draw Only

Once a `UCImage` has been cached by `UCImageRaster::Get(path)` (path-keyed cache), subsequent loads short-circuit the decoder. The benchmark explicitly works around this by regenerating `runStamp` per run so each codec writes to a unique temp path — but for "draw only" measurement you can keep a path stable and only measure repaint cost:

```cpp
auto img = UCImage::Get(stablePath);    // returns cached UCImage (no decode)
imageElement->LoadFromImage(img);
imageElement->RequestRedraw();          // measure this paint's frame time
```

## Usage Examples

### Driving the benchmark from a "Choose Image" handler

```cpp
auto loadSourceImage = [state, cleanupTempFiles, resetThumbnails,
                        runBenchmark](const std::string& path) -> bool {
    auto img = UCImage::Load(path, false);
    if (!img || !img->IsValid()) {
        state->statusLabel->SetText("Status: Failed to load " + path);
        state->infoLabel->SetText("No image loaded");
        state->infoLabel->RequestRedraw();
        return false;
    }
    state->sourceImage  = img;
    state->sourcePath   = path;
    state->sourceWidth  = img->GetWidth();
    state->sourceHeight = img->GetHeight();

    cleanupTempFiles();
    state->chartElem->SetResults(state->results);
    resetThumbnails();

    std::string fileName = std::filesystem::path(path).filename().string();
    std::ostringstream info;
    info << fileName << "  -  " << state->sourceWidth << " x " << state->sourceHeight << " px";
    state->infoLabel->SetText(info.str());
    state->infoLabel->RequestRedraw();

    runBenchmark();   // auto-run after a successful load
    return true;
};
```

### Encoding one codec and timing the round-trip

This is the inner loop of `runBenchmark` for a libvips-backed codec:

```cpp
auto opts = DefaultOptionsFor(entry.format);
double t0 = NowMs();
std::string err = state->sourceImage->Save(outPath, opts);
double t1 = NowMs();

if (err.empty()) {
    r.encodeTimeMs    = t1 - t0;
    r.encodedFilePath = outPath;
    r.encodedSizeBytes =
        static_cast<size_t>(std::filesystem::file_size(outPath));

    // Force eager decode so libvips doesn't lazy-defer the work.
    double d0 = NowMs();
    auto round = UCImage::Load(outPath, false);
    bool ok = false;
    if (round && round->IsValid()) {
#ifdef HAS_LIBVIPS
        vips::VImage v = round->GetVImage();
        v = v.copy_memory();
        (void)v;
#endif
        ok = true;
    }
    double d1 = NowMs();
    if (ok) { r.decodeTimeMs = d1 - d0; r.succeeded = true; }
    else    { r.errorMessage = "decode failed"; }
}
```

### Encoding with the bundled QOI path

QOI is decoded directly via the bundled `qoi.h` (no libvips round-trip on the decode side):

```cpp
vips::VImage vImg = state->sourceImage->GetVImage();
double t0 = NowMs();
std::string err = EncodeQOIFromVImage(vImg, outPath);
double t1 = NowMs();
r.encodeTimeMs = t1 - t0;

double d0 = NowMs();
bool ok = DecodeQOIFile(outPath);   // qoi_read + free
double d1 = NowMs();
r.decodeTimeMs = d1 - d0;
```

### Encoding with the bundled BMP path

libvips has no BMP writer, so the demo ships a standalone 24bpp BI_RGB encoder/decoder. The encode/decode timing pattern is symmetric with QOI:

```cpp
double t0 = NowMs();
std::string err = EncodeBMPFromVImage(vImg, outPath);
double t1 = NowMs();
r.encodeTimeMs = t1 - t0;

double d0 = NowMs();
bool ok = DecodeBMPFile(outPath);   // header parse + row walk + free
double d1 = NowMs();
r.decodeTimeMs = d1 - d0;
```

### Hooking thumbnail clicks to the 1:1 viewer

Each successfully-encoded codec gets a clickable thumbnail. Clicking opens the result at full resolution:

```cpp
if (r.succeeded && globalIdx < static_cast<int>(state->thumbs.size())) {
    auto thumb = state->thumbs[globalIdx];
    if (thumb) {
        thumb->LoadFromFile(outPath);
        thumb->SetVisible(true);
        std::string slotPath = outPath;
        thumb->onClick = [slotPath]() {
            ShowFullSizeImageViewer(slotPath);
        };
        thumb->RequestRedraw();
    }
}
```

### Live status updates per codec

The benchmark forces an immediate repaint after each codec so the bar chart grows interactively:

```cpp
std::ostringstream s;
s << "Status: " << r.name
  << (r.succeeded ? " OK" : " FAILED")
  << "  (" << (slot + 1) << "/" << activeCount << ")";
if (!r.succeeded && !r.errorMessage.empty()) s << " - " << r.errorMessage;
state->statusLabel->SetText(s.str());

state->chartElem->SetResults(state->results);
if (win && ctx) {
    win->UpdateAndRender();
}
```

## Why "Force Eager Decode" Matters

libvips uses lazy evaluation: `UCImage::Load()` typically only parses the file header and defers pixel decompression until pixels are actually read. Without `vImage.copy_memory()`, the timed region would only measure header parsing — drastically understating real-world decode cost for PNG, JPG, WebP, AVIF, HEIF, TIFF, and JP2K, while making them look unfairly fast next to QOI (which always fully decompresses in `qoi_read`).

Calling `copy_memory()` walks the full libvips pipeline (decompress, color convert, optional alpha premultiply) and writes every pixel into a contiguous RAM buffer. Decode times then reflect real file→RAM decompression work for every codec.

## Best Practices

1. **Always regenerate per-run temp paths.** UCImage's path-keyed cache will short-circuit identical paths and silently serve stale pixels into your thumbnails. The benchmark uses a nanosecond timestamp (`runStamp`) for this.
2. **Force eager decode for any libvips load you intend to time.** Bare `Load()` only parses the header.
3. **Disable BMP by default** when charting size — uncompressed RGB at 24bpp dominates the size axis and crushes the scale of every other codec.
4. **Re-encode through `UCImage::Save()` (not raw libvips)** so your benchmark numbers reflect what UltraCanvas apps will actually see.
5. **Use `UpdateAndRender()` per codec** for streaming-style chart updates — without it the whole benchmark batches into a single repaint at the end.

## Codec Support Matrix

| Codec | Backend  | Notes                                                                  |
|-------|----------|------------------------------------------------------------------------|
| PNG   | libvips  | Lossless; default reference                                            |
| QOI   | bundled  | Lossless; reference encoder/decoder from `qoi.h`                       |
| JPG   | libvips  | Lossy; quality 85 default                                              |
| JP2K  | libvips  | JPEG 2000; quality 75                                                  |
| WebP  | libvips  | Lossy by default; quality 80                                           |
| AVIF  | libvips  | Quality 65; speed 6                                                    |
| HEIF  | libvips  | Quality 50                                                             |
| GIF   | libvips  | 8-bit indexed                                                          |
| TIFF  | libvips  | LZW compression default                                                |
| BMP   | bundled  | Optional. Uncompressed 24bpp BI_RGB                                    |

## See Also

- [UltraCanvasImageElement](UltraCanvasImageElement.md) — Image display widget used for the thumbnails
- [UltraCanvasLabel](UltraCanvasLabelExamples.md) — Status/info text labels
- [UltraCanvasButton](UltraCanvasButtonExamples.md) — "Choose Image..." button
- [UltraCanvasCheckbox](UltraCanvasCheckbox.md) — "Include BMP" toggle
- [UltraCanvasContainer](UltraCanvasContainer.md) — Layout root for the benchmark
