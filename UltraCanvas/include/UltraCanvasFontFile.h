// include/UltraCanvasFontFile.h
// Reading and previewing font definition files (TTF, OTF, WOFF, Type 1, ...).
//
// A font file is the one document class the framework consumed but could not
// show: fonts went into the text pipeline by family name and never came back
// out as something a file manager could display. This module closes that gap
// by treating a font file as a readable document - its name records are
// metadata, and a line of its own glyphs is its thumbnail.
//
// Everything here is pure FreeType against the file on disk: no fontconfig,
// no Pango, no render context, and no need for the font to be installed or
// registered first. Each call owns its own FT_Library, so the functions are
// safe to run concurrently on background threads - which is what lets the
// filer thumbnail a folder of fonts the same way it thumbnails photos.
//
// To make a font file usable for actual text rendering (a FontStyle naming
// its family), register it with the application instead:
// UltraCanvasApplicationBase::RegisterFontFile().
// Version: 1.0.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASFONTFILE_H
#define ULTRACANVASFONTFILE_H

#include "UltraCanvasImage.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    // ===== FONT FILE FORMATS =====
    // What the container is, as FreeType reports it after opening the file -
    // not what the extension claimed. A file named .ttf that really holds CFF
    // outlines reads as OpenType here.
    enum class FontFileFormat {
        Unknown,
        TrueType,             // ttf / ttc - glyf outlines
        OpenType,             // otf / otc - CFF or CFF2 outlines
        Type1,                // pfa / pfb (+ the CID-keyed variant)
        WOFF,                 // web font, TrueType or CFF inside
        WOFF2,                // web font, Brotli-compressed
        BitmapFont            // bdf / pcf / fon / fnt - no outlines, fixed strikes
    };

    // ===== ONE FACE OF A FONT FILE =====
    // A plain .ttf holds one face; a collection (.ttc/.otc) holds several,
    // and Type 1 and bitmap files may hold more than one size or style. The
    // strings come from the font's own name table and are already UTF-8;
    // any of them is empty when the font does not carry that record.
    struct FontFaceInfo {
        int index = 0;                  // face index inside the file
        std::string family;             // "Ubuntu"
        std::string subfamily;          // "Bold Italic"
        std::string fullName;           // "Ubuntu Bold Italic"
        std::string postScriptName;     // "Ubuntu-BoldItalic"
        std::string version;            // "Version 0.83"
        std::string copyright;
        std::string trademark;
        std::string manufacturer;
        std::string designer;
        std::string license;            // the license text the font carries
        std::string licenseURL;
        std::string sampleText;         // the font's own suggested specimen

        long glyphCount = 0;
        int unitsPerEM = 0;             // 0 for a non-scalable (bitmap) face
        bool scalable = false;
        bool fixedWidth = false;        // monospaced
        bool hasKerning = false;
        bool bold = false;
        bool italic = false;
        bool hasUnicodeCharmap = false; // false for symbol-only faces

        // Strike heights in pixels, for a face that only exists at fixed
        // sizes. Empty for a scalable face.
        std::vector<int> fixedSizes;
    };

    struct FontFileInfo {
        std::string path;
        FontFileFormat format = FontFileFormat::Unknown;
        std::string formatName;         // FreeType's own name for the format
        uint64_t fileSize = 0;
        int faceCount = 0;
        std::vector<FontFaceInfo> faces;   // faces[0] is the one previews use
    };

    // ===== RECOGNITION =====
    // True for an extension the reader below can open. Takes a path or a bare
    // extension, with or without the leading dot, in any case. It answers for
    // the extension only: whether THIS build's FreeType can actually decode
    // the file (WOFF2 needs Brotli, for one) is decided by ReadFontFileInfo.
    bool IsFontFileExtension(const std::string& pathOrExtension);

    // The format an extension names, before the file is opened.
    FontFileFormat FontFormatForExtension(const std::string& pathOrExtension);

    // Human-readable name of a format ("TrueType", "OpenType", ...).
    const char* FontFormatName(FontFileFormat format);

    // ===== METADATA =====
    // Read the file's format, face count and the name records of every face.
    // Returns false when the file cannot be opened or FreeType does not
    // recognize it; never throws on a malformed file. Safe on any thread.
    bool ReadFontFileInfo(const std::string& filePath, FontFileInfo& out);

    // ===== SPECIMEN =====
    // How to draw the specimen. The defaults produce a white card with a
    // line of dark glyphs on it, sized to fill the box - the "sheet of
    // paper" idiom the document and PDF previews already use, so a font sits
    // among them in a thumbnail grid without standing out as a different
    // kind of thing.
    struct FontSpecimenOptions {
        // Text to draw. Empty picks a default that follows the shape of the
        // box - "AaBbCc" where it is at least twice as wide as it is tall,
        // "Ag" otherwise, because a six-glyph line in a square tile is fitted
        // by its width and comes out too small to read the letterforms off.
        // A font with no glyph for those characters (an icon or symbol font)
        // falls back to drawing its own first glyphs instead, so a specimen
        // is produced either way.
        std::string text;
        Color textColor = Color(26, 26, 28, 255);
        Color backgroundColor = Color(255, 255, 255, 255);
        int faceIndex = 0;
        int padding = 3;              // logical pixels of margin around the ink
    };

    // Rasterize a line of the font's own glyphs into a ready-to-draw pixmap
    // of width × height logical pixels (scale > 1 renders that many device
    // pixels for HiDPI, like every other preview producer). Returns null when
    // the file cannot be opened, holds no glyphs, or nothing legible fits.
    // Pure FreeType rasterization - safe to call from a worker thread.
    std::shared_ptr<UCPixmap> RenderFontSpecimenPixmap(
            const std::string& filePath, int width, int height, float scale,
            const FontSpecimenOptions& options = {});

} // namespace UltraCanvas

#endif // ULTRACANVASFONTFILE_H
