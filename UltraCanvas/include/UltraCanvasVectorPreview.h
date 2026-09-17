// include/UltraCanvasVectorPreview.h
// The seam that lets core display a vector drawing it cannot itself read.
//
// Core owns the vector document model (DataFormats/UltraCanvasVectorStorage.h)
// and the renderer that draws one (DataFormats/UltraCanvasVectorRenderer.h),
// but not a single reader: SVG, XAR, EMF, WMF, DXF and the DWG family all
// live in the Vector plugin, which links *against* core. So the media viewer
// and the Filer could only ever show a vector file two ways - rasterized by
// libvips (svg/svgz, and eps/ps on a build with a PostScript loader) or as
// the preview bitmap some formats store inside themselves. A drawing that is
// neither, a DXF or a DWG, showed nothing at all, although the framework had
// read it perfectly well in the next process over.
//
// This inverts that without inverting the dependency, exactly as
// UltraCanvasModelPreview.h does for 3D formats: core declares what it wants -
// "turn this path into a VectorDocument", "is this one you read" - and
// RegisterVectorFormatsPlugin() installs an implementation on the way in. No
// provider means no answers, which is what a build with
// ULTRACANVAS_PLUGIN_VECTOR=OFF gets and what every caller got before.
//
// A provider hands back a VectorDocument rather than pixels: the document is
// resolution-independent, so one read serves a 32-pixel icon and a full-width
// detail pane, and the caller picks the size. RenderVectorPreviewPixmap()
// below is the drawing half, shared by every caller that wants an image.
//
// Version: 1.0.0
// Last Modified: 2026-09-16
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_VECTOR_PREVIEW_H
#define ULTRACANVAS_VECTOR_PREVIEW_H

#include "DataFormats/UltraCanvasVectorStorage.h"
#include "UltraCanvasImage.h"   // UCPixmap

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// What a provider must answer.
struct VectorPreviewProvider {
    // Every extension this provider reads, lowercase and without a dot. Used
    // to decide whether a file is a drawing at all, so it must not name
    // extensions the provider would then refuse.
    std::function<std::vector<std::string>()> Extensions;

    // Reads `path` into a document, or returns null. Never throws.
    std::function<std::shared_ptr<VectorStorage::VectorDocument>(
            const std::string& path)> Load;

    // Optional: claims a file the Extensions() list does not cover - a
    // suffix the provider reads without advertising it, or one only the
    // file's own header can settle. A DWG copied to .bak is a drawing and a
    // text editor's .bak is not, and nothing but the first six bytes tells
    // them apart, so that question cannot be answered from `Extensions`
    // alone. Asked only after Extensions() has said no, and expected to cost
    // a name comparison or a header read, never a full parse.
    std::function<bool(const std::string& path)> ClaimsFile;

    bool Valid() const { return Extensions && Load; }
};

// Installs the provider. Called by RegisterVectorFormatsPlugin(); an
// application that registers no plugins never calls it. Passing an invalid
// provider clears it.
void SetVectorPreviewProvider(VectorPreviewProvider provider);

// ===== WHAT CORE ASKS =====

// True when this build can turn the extension into a drawing. `extension` may
// carry a leading dot and any case, or be a whole path - and when it is a path
// that exists, a provider's ClaimsFile() gets a look at content-decided
// formats too.
bool CanPreviewVectorExtension(const std::string& extensionOrPath);

// Every extension CanPreviewVectorExtension() answers true for, lowercase and
// without a dot, sorted and deduplicated. Content-decided formats are not in
// it - by definition they have no extension to list.
std::vector<std::string> PreviewableVectorExtensions();

// Reads a drawing, or returns null when nothing in this build reads it.
std::shared_ptr<VectorStorage::VectorDocument> LoadVectorPreviewDocument(
        const std::string& path);

// Draws a document into a pixmap of `w` x `h` logical pixels (`scale` is the
// display scale, so the pixmap comes back `w * scale` wide). The drawing is
// fitted to the box and centred, keeping its aspect ratio; `background` is
// painted underneath - transparent by default, which is what a thumbnail tile
// and a preview pane both want. Null when the size is unusable or no render
// context could be made.
std::shared_ptr<UCPixmap> RenderVectorDocumentPixmap(
        const VectorStorage::VectorDocument& document, int w, int h,
        float scale = 1.0f, const Color& background = Color(0, 0, 0, 0));

// The two halves together: read `path` and draw it. Null when the file cannot
// be read or holds nothing to draw.
std::shared_ptr<UCPixmap> RenderVectorPreviewPixmap(const std::string& path,
                                                    int w, int h,
                                                    float scale = 1.0f);

} // namespace UltraCanvas

#endif // ULTRACANVAS_VECTOR_PREVIEW_H
