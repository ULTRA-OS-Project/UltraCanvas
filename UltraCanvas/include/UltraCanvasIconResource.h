// include/UltraCanvasIconResource.h
// Windows icon resources read without Windows: ".ico" files and the icons a
// PE binary (".exe", ".dll", and the icon libraries that use the same
// format) carries in its resource section.
//
// Windows itself answers this through the shell, and the Windows backend
// does exactly that. Everywhere else - ULTRA OS, Linux and macOS looking at
// a mounted Windows disk or at a Wine prefix - there is no shell to ask, and
// a file display had nothing to draw for a program but a generic sheet. This
// reader is the portable answer: it walks the PE resource directory itself,
// picks the icon nearest the size that was asked for, and decodes it. The
// decoder handles both frame formats an icon can hold: the PNG frames of
// Vista-and-later 256px icons, and the classic DIB frames with their 1-bit
// transparency mask.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework
#pragma once
#include "UltraCanvasImage.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    // Cheap check (extension only, no file access): is this a file kind that
    // can hold icon resources? ".ico", ".exe", ".dll" and the icon-library
    // variants of the PE format (".icl", ".cpl", ".ocx", ".scr", ".mun").
    bool HasIconResourceExtension(const std::string& path);

    // The icon `index` of `path`, rasterized at the embedded size nearest
    // `desiredSize`. Index semantics are the ones Windows shortcuts use: a
    // non-negative index selects the n-th icon of the file in resource
    // order, a negative one names a resource id (-index). Null when the file
    // holds no icon, is not one of the formats above, or cannot be read.
    // Blocking file access - call it from a worker thread.
    std::shared_ptr<UCPixmap> LoadIconResource(const std::string& path,
                                               int index, int desiredSize);

    // Decode one icon file's bytes (an ".ico", or the equivalent blob built
    // from a PE resource). Exposed for callers that already hold the bytes -
    // an icon inside an archive, a resource served over the network.
    std::shared_ptr<UCPixmap> DecodeIconFileBytes(const std::vector<uint8_t>& bytes,
                                                  int desiredSize);

} // namespace UltraCanvas
