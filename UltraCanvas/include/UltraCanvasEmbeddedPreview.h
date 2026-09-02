// include/UltraCanvasEmbeddedPreview.h
// The preview bitmap a vector document carries inside itself.
//
// Most vector formats have no renderer that works without a window, so a
// file manager cannot rasterize them on a background thread. Two families
// do not need one: Xara (.xar/.web/.wix) stores a GIF/JPEG/PNG preview as
// one of the first records of the file head, and the ZIP-based CorelDRAW
// documents (X4 and newer .cdr/.cdt) keep one as "previews/thumbnail.png".
// Both are ordinary images once extracted, so they decode through the image
// pipeline like any other bitmap.
//
// The extraction is pure file parsing - no graphics plugin, no render
// context - so it is safe to call from a worker thread.
// Version: 1.0.0
// Last Modified: 2026-09-02
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

    // True when files of this format can carry an embedded preview bitmap.
    // Takes a path or a bare extension (with or without the leading dot);
    // it answers for the format, not for the individual file - a file of a
    // listed format may still turn out to have no preview stored in it.
    bool FormatCarriesEmbeddedPreview(const std::string& pathOrExtension);

    // The bytes of the preview bitmap stored inside `filePath` (a complete
    // GIF, JPEG or PNG file), or an empty vector when the format carries
    // none, the file has none, or it cannot be read. Never throws for a
    // malformed file: a truncated or corrupt document reads as "no preview".
    std::vector<uint8_t> ExtractEmbeddedPreviewBytes(const std::string& filePath);

} // namespace UltraCanvas
