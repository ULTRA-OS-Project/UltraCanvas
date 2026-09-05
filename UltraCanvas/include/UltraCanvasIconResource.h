// include/UltraCanvasIconResource.h
// The icon files of the other two desktops, read without either of them:
// Windows ".ico" files and the icons a PE binary (".exe", ".dll", and the
// icon libraries that use the same format) carries in its resource section,
// and Apple ".icns" files - the icon of every macOS application bundle.
//
// Windows itself answers this through the shell, and the Windows backend
// does exactly that. Everywhere else - ULTRA OS, Linux and macOS looking at
// a mounted Windows disk or at a Wine prefix - there is no shell to ask, and
// a file display had nothing to draw for a program but a generic sheet. This
// reader is the portable answer: it walks the PE resource directory (or the
// icns element list) itself, picks the rendition nearest the size that was
// asked for, and decodes it. Both eras of each format are handled: the PNG
// renditions of modern icons, the classic Windows DIB frames with their
// 1-bit transparency mask, and the run-length encoded RGB and ARGB
// renditions of an icns with the mask element that belongs to them.
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
    // can hold icon resources? ".ico", ".icns", ".exe", ".dll" and the
    // icon-library variants of the PE format (".icl", ".cpl", ".ocx",
    // ".scr", ".mun").
    bool HasIconResourceExtension(const std::string& path);

    // The icon `index` of `path`, rasterized at the embedded size nearest
    // `desiredSize`. Index semantics are the ones Windows shortcuts use: a
    // non-negative index selects the n-th icon of the file in resource
    // order, a negative one names a resource id (-index). Null when the file
    // holds no icon, is not one of the formats above, or cannot be read.
    // Blocking file access - call it from a worker thread.
    std::shared_ptr<UCPixmap> LoadIconResource(const std::string& path,
                                               int index, int desiredSize);

    // Decode one icon file's bytes - an ".ico" or an ".icns", told apart by
    // their first bytes rather than by any name. Exposed for callers that
    // already hold them: an icon inside an archive, inside an application
    // bundle, or served over the network.
    std::shared_ptr<UCPixmap> DecodeIconFileBytes(const std::vector<uint8_t>& bytes,
                                                  int desiredSize);

} // namespace UltraCanvas
