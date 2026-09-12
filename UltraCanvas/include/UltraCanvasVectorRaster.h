// include/UltraCanvasVectorRaster.h
// Turning vector artwork into pixels: inspect a vector file for its natural
// size and page count, then rasterize it into an editable UCRasterLayer at a
// chosen pixel size.
//
// Two rasterizers sit behind one call, picked per file:
//
//   ImagePipeline  — libvips: svgload (librsvg) for SVG/SVGZ, pdfload for
//                    PDF/AI, and whatever PostScript delegate the libvips
//                    build has for EPS/PS. Rendering happens *at* the
//                    requested size, so the result is resolution-independent
//                    rather than an upscaled thumbnail.
//   GraphicsPlugin — any extension a registered IGraphicsPlugin claims
//                    (the Vector plugin's DXF / DWG / EMF / WMF / XAR, the
//                    CDR and XAR viewer plugins, ...). The plugin's element
//                    is rendered into an offscreen render context and the
//                    pixels are read back.
//
// Which one applies to a given file — and whether this build can rasterize it
// at all — is what InspectVectorFile() reports; nothing here throws.
// Version: 1.0.0
// Last Modified: 2026-09-12
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRasterLayer.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== WHICH RASTERIZER HANDLES A FILE =====
enum class VectorRasterSource {
    // Named Unsupported rather than None: X11 defines None as a macro.
    Unsupported,     // not a vector file, or no rasterizer in this build
    ImagePipeline,   // libvips: svgload / pdfload / PostScript delegate
    GraphicsPlugin   // a registered IGraphicsPlugin element, rendered offscreen
};

const char* VectorRasterSourceName(VectorRasterSource source);

// ===== WHAT A VECTOR FILE SAYS ABOUT ITSELF =====
// `naturalWidth` / `naturalHeight` are the size the artwork asks to be drawn
// at, in pixels (SVG user units; PDF points at 72 dpi), and are the sensible
// default raster size. They are 0 when the source cannot be measured without
// rendering it, in which case a caller must choose a size itself.
struct VectorSourceInfo {
    bool ok = false;                                     // this build can rasterize it
    VectorRasterSource source = VectorRasterSource::Unsupported;
    int naturalWidth = 0;
    int naturalHeight = 0;
    int pageCount = 1;                                   // > 1 only for paged sources (PDF)
    std::string provider;                                // human-readable, for status lines
    std::string error;                                   // why `ok` is false
};

// ===== HOW TO RASTERIZE IT =====
// `width` / `height` are the wanted pixel size; either or both may be 0, in
// which case the missing one follows the natural aspect ratio (both 0 means
// the natural size). `background` is painted underneath — the default is
// transparent, so an SVG with no backdrop keeps its alpha.
struct VectorRasterOptions {
    int width = 0;
    int height = 0;
    int page = 0;                                    // paged sources; 0-based
    RasterPixel background = RasterPixel(0, 0, 0, 0);
    // Refuses anything larger, so a stray "×100" cannot ask for a 40 GB
    // buffer. Counted on the target size, not the file.
    size_t maxPixels = 256u * 1024u * 1024u;
};

// True for an extension this build can rasterize — the cheap check a file
// dialog or a drop handler makes before doing any work. It answers for the
// *extension*, so an unreadable or corrupt file still comes back true; only
// InspectVectorFile() opens the file.
bool IsVectorGraphicsPath(const std::string& path);

// Every extension IsVectorGraphicsPath() accepts in this build (lowercase, no
// dot), in no particular order. Depends on the libvips build and on which
// graphics plugins the application has registered, so it is a runtime answer.
std::vector<std::string> GetVectorRasterExtensions();

// Opens the file far enough to report its natural size and page count.
VectorSourceInfo InspectVectorFile(const std::string& path);

// Rasterizes `path` into a fresh layer, or returns null and fills `error`.
// The layer is named after the file (page number appended for a paged source)
// so it reads sensibly in a layer list.
std::shared_ptr<UCRasterLayer> RasterizeVectorFile(const std::string& path,
                                                   const VectorRasterOptions& options,
                                                   std::string& error);

} // namespace UltraCanvas
