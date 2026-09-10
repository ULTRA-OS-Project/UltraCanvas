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
        // A font with no glyph for ANY of those characters (an icon or symbol
        // font) falls back to drawing its own first glyphs instead, so a
        // specimen is produced either way; a font that has some of them draws
        // the part it has, so a one-character text really does draw that one
        // character.
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

    // ===== ONE GLYPH OF A FACE =====
    // What a browser needs to address a glyph and to label it. `codepoint` is
    // the character that reaches the glyph through the face's charmap; it is 0
    // for a glyph enumerated by index because the face has no usable charmap,
    // and for the glyphs a font can only reach through substitution.
    struct FontGlyphEntry {
        uint32_t glyphIndex = 0;
        uint32_t codepoint = 0;
    };

    // ===== A CONTIGUOUS STRETCH OF THE COVERAGE =====
    // What a "jump to a range" picker lists. Ranges are cut from the coverage
    // itself - a run of consecutive codepoints the font actually has - and
    // then named after the Unicode block the run starts in, so a font that
    // covers three quarters of Cyrillic gets one "Cyrillic" entry rather than
    // the whole block whether it has it or not.
    struct FontCoverageRange {
        std::string name;        // "Basic Latin", "Cyrillic", "Glyphs 256-511"
        uint32_t first = 0;      // first / last codepoint, or glyph index when
        uint32_t last = 0;       // the face is enumerated by index
        size_t firstEntry = 0;   // where the run starts in Glyphs()
        size_t count = 0;        // how many entries it holds
    };

    // ===== HOW ONE GLYPH IS DRAWN INTO A CELL =====
    struct FontGlyphOptions {
        Color textColor = Color(26, 26, 28, 255);
        Color backgroundColor = Color(255, 255, 255, 255);
        int padding = 2;         // logical pixels kept clear inside the cell
        // Scale each glyph to fill its cell (true) or scale the face's em box
        // to the cell and let each glyph take the room it really occupies
        // (false, the default). A grid wants the latter: shared sizing and a
        // shared baseline are what make a row of glyphs comparable, and
        // per-glyph fitting instead blows a comma up to the size of a W.
        bool fitInkToCell = false;
    };

    // ===== A FONT FILE HELD OPEN =====
    // ReadFontFileInfo() and RenderFontSpecimenPixmap() each open the file,
    // work, and close it again. That is what makes them safe to run on
    // several threads at once, and it is the right shape for one thumbnail.
    //
    // It is the wrong shape for a browser. A grid showing hundreds of cells,
    // re-rasterized on every scroll and every size change, cannot re-open the
    // file per glyph. This is the session type for that: open once, enumerate
    // the coverage once, then rasterize individual glyphs from the face that
    // is already open.
    //
    // One instance is single-threaded - it owns a live FT_Face, which is not
    // re-entrant. Give each thread its own if more than one needs glyphs.
    // Move-only, and closing is automatic.
    class UltraCanvasFontFace {
    public:
        UltraCanvasFontFace();
        ~UltraCanvasFontFace();
        UltraCanvasFontFace(UltraCanvasFontFace&&) noexcept;
        UltraCanvasFontFace& operator=(UltraCanvasFontFace&&) noexcept;
        UltraCanvasFontFace(const UltraCanvasFontFace&) = delete;
        UltraCanvasFontFace& operator=(const UltraCanvasFontFace&) = delete;

        // Open one face of a font file and enumerate its coverage. Returns
        // false when the file cannot be read or holds no glyphs; a face that
        // was open before is closed either way.
        bool Open(const std::string& filePath, int faceIndex = 0);
        void Close();
        bool IsOpen() const;

        const std::string& Path() const;
        int FaceIndex() const;
        // The same record ReadFontFileInfo() reports, for this face alone.
        const FontFaceInfo& Info() const;

        // Every glyph the face offers, in codepoint order where it has a
        // usable charmap and in glyph-index order where it has not. Empty
        // while closed.
        const std::vector<FontGlyphEntry>& Glyphs() const;
        // False when the list above is index-ordered because no charmap could
        // be used - a browser then labels cells by glyph index, not character.
        bool GlyphsAreByCodepoint() const;
        // The runs of Glyphs(), for a range picker.
        const std::vector<FontCoverageRange>& Ranges() const;

        // The font's own name for a glyph ("A", "eacute", "uni20AC"), empty
        // when the format carries no glyph names. Queried on demand rather
        // than stored: a CJK face has tens of thousands of glyphs and a
        // browser only ever labels the handful under the pointer.
        std::string GlyphName(size_t entry) const;

        // Rasterize one glyph into a cell of width × height logical pixels
        // (scale > 1 renders that many device pixels, as everywhere else).
        // `entry` indexes Glyphs(). Returns null for an out-of-range entry, a
        // closed face, or a glyph that rasterizes to nothing - a blank such as
        // a space is a real answer, so it comes back as an empty cell rather
        // than null.
        // Not const: rasterizing sets the face's size and loads a glyph into
        // its one slot, so it really does move the face's state.
        std::shared_ptr<UCPixmap> RenderGlyph(size_t entry, int width, int height,
                                              float scale,
                                              const FontGlyphOptions& options = {});

        // Where a codepoint sits in Glyphs(), or Glyphs().size() when the face
        // does not cover it. Lets a browser jump to a character.
        size_t FindCodepoint(uint32_t codepoint) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

} // namespace UltraCanvas

#endif // ULTRACANVASFONTFILE_H
