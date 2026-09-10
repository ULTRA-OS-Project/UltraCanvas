// include/Plugins/LaTeX/UltraCanvasMathFont.h
// An OpenType math font read directly from its file: the MATH table
// (constants, italics correction, top-accent attachment, extended shapes,
// math kerning, size variants and glyph assemblies), the glyph metrics and
// the glyph outlines - everything a TeX-style math typesetter needs from a
// font, and nothing it does not.
//
// This is Phase 0 of UltraCanvasLaTeXEngineProposal.md: the font layer of a
// native math engine. It replaces the FontForge-generated `.clm2` container
// the vendored MicroTeX engine reads, so that any OpenType font with a MATH
// table - Latin Modern Math, STIX Two Math, Libertinus Math, Cambria Math -
// works unmodified and at runtime.
//
// Everything is FreeType, which is a hard dependency of the framework: the
// MATH table is loaded with FT_Load_Sfnt_Table and parsed here (it is a
// small, flat binary format; parsing it ourselves avoids depending on the
// HarfBuzz version a platform ships), outlines come from
// FT_Outline_Decompose. There is no fontconfig, no Pango and no render
// context involved, so a font can be opened and measured on any thread; one
// UltraCanvasMathFont object must not be used from two threads at once.
//
// Units: every length is in font units (see GetUnitsPerEm) with y pointing
// up, exactly as the font file stores them. The engine scales them once by
// fontSize / unitsPerEm. The three *PercentScaleDown constants are
// percentages, not lengths.
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// The 56 MathConstants of the OpenType MATH table, in table order.
enum class MathConstant : uint8_t {
    ScriptPercentScaleDown = 0,
    ScriptScriptPercentScaleDown,
    DelimitedSubFormulaMinHeight,
    DisplayOperatorMinHeight,
    MathLeading,
    AxisHeight,
    AccentBaseHeight,
    FlattenedAccentBaseHeight,
    SubscriptShiftDown,
    SubscriptTopMax,
    SubscriptBaselineDropMin,
    SuperscriptShiftUp,
    SuperscriptShiftUpCramped,
    SuperscriptBottomMin,
    SuperscriptBaselineDropMax,
    SubSuperscriptGapMin,
    SuperscriptBottomMaxWithSubscript,
    SpaceAfterScript,
    UpperLimitGapMin,
    UpperLimitBaselineRiseMin,
    LowerLimitGapMin,
    LowerLimitBaselineDropMin,
    StackTopShiftUp,
    StackTopDisplayStyleShiftUp,
    StackBottomShiftDown,
    StackBottomDisplayStyleShiftDown,
    StackGapMin,
    StackDisplayStyleGapMin,
    StretchStackTopShiftUp,
    StretchStackBottomShiftDown,
    StretchStackGapAboveMin,
    StretchStackGapBelowMin,
    FractionNumeratorShiftUp,
    FractionNumeratorDisplayStyleShiftUp,
    FractionDenominatorShiftDown,
    FractionDenominatorDisplayStyleShiftDown,
    FractionNumeratorGapMin,
    FractionNumDisplayStyleGapMin,
    FractionRuleThickness,
    FractionDenominatorGapMin,
    FractionDenomDisplayStyleGapMin,
    SkewedFractionHorizontalGap,
    SkewedFractionVerticalGap,
    OverbarVerticalGap,
    OverbarRuleThickness,
    OverbarExtraAscender,
    UnderbarVerticalGap,
    UnderbarRuleThickness,
    UnderbarExtraDescender,
    RadicalVerticalGap,
    RadicalDisplayStyleVerticalGap,
    RadicalRuleThickness,
    RadicalExtraAscender,
    RadicalKernBeforeDegree,
    RadicalKernAfterDegree,
    RadicalDegreeBottomRaisePercent,
    Count
};

// Human-readable name of a constant, as spelled in the OpenType specification.
const char* MathConstantName(MathConstant c);

// The corner a math-kern table applies to (sub/superscript placement).
enum class MathKernCorner : uint8_t { TopRight = 0, TopLeft, BottomRight, BottomLeft };

// The direction a glyph stretches in: delimiters and radicals vertically,
// accents, braces and arrows horizontally.
enum class MathStretchDirection : uint8_t { Vertical = 0, Horizontal };

// Metrics of one glyph. The bounding box is the exact outline box (not the
// control box), so height/depth derived from it match what a TeX engine
// expects. An empty glyph (a space) has an all-zero box.
struct MathGlyphMetrics {
    int advance = 0;
    int xMin = 0, yMin = 0, xMax = 0, yMax = 0;
    // TeX-style height (above the baseline) and depth (below), never negative.
    int Height() const { return yMax > 0 ? yMax : 0; }
    int Depth()  const { return yMin < 0 ? -yMin : 0; }
    int Width()  const { return xMax - xMin; }
};

// One entry of a glyph's size-variant list, smallest first, as the font
// lists them. `advance` is the variant's extent in the stretch direction.
struct MathGlyphVariant {
    uint32_t glyph = 0;
    int advance = 0;
};

// One part of a glyph assembly (a delimiter built from top, extender and
// bottom pieces). Lengths are in font units along the stretch direction.
struct MathGlyphPart {
    uint32_t glyph = 0;
    int startConnectorLength = 0;
    int endConnectorLength = 0;
    int fullAdvance = 0;
    bool isExtender = false;
};

struct MathGlyphAssembly {
    int italicsCorrection = 0;
    std::vector<MathGlyphPart> parts;
    bool IsEmpty() const { return parts.empty(); }
};

// A math-kern table for one corner of one glyph: n correction heights and
// n+1 kern values; kernValues[i] applies below correctionHeights[i], the
// last value above the highest height.
struct MathKernTable {
    std::vector<int> correctionHeights;
    std::vector<int> kernValues;
    bool IsEmpty() const { return kernValues.empty(); }
    // The kern value in effect at `height` (font units above the baseline).
    int KernAtHeight(int height) const;
};

// A glyph outline as path commands in font units, y up, ready to be scaled
// and fed to IRenderContext::MoveTo / LineTo / QuadraticCurveTo /
// BezierCurveTo / ClosePath.
enum class MathOutlineCommand : uint8_t { MoveTo, LineTo, QuadTo, CubicTo, Close };

struct MathOutlineSegment {
    MathOutlineCommand command = MathOutlineCommand::Close;
    // MoveTo/LineTo use (x1,y1); QuadTo uses control (x1,y1) and end (x2,y2);
    // CubicTo uses controls (x1,y1),(x2,y2) and end (x3,y3).
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0;
};

struct MathGlyphOutline {
    std::vector<MathOutlineSegment> segments;
    bool IsEmpty() const { return segments.empty(); }
};

class UltraCanvasMathFont {
public:
    UltraCanvasMathFont();
    ~UltraCanvasMathFont();
    UltraCanvasMathFont(const UltraCanvasMathFont&) = delete;
    UltraCanvasMathFont& operator=(const UltraCanvasMathFont&) = delete;

    // ----- loading -----
    // Opens face `faceIndex` of the font file. Returns false - with the reason
    // in GetLastError() - for a missing, unreadable or non-OpenType file. A
    // font *without* a MATH table still loads (HasMathTable() is false; every
    // MATH query returns its documented "absent" value) so that a text font
    // can serve as a \text{} face through the same object.
    bool Load(const std::string& path, int faceIndex = 0);
    // Same from a memory image; the bytes are copied and kept for the
    // lifetime of the object.
    bool LoadFromMemory(const uint8_t* data, size_t size, int faceIndex = 0);
    void Unload();

    bool IsLoaded() const;
    const std::string& GetLastError() const;
    const std::string& GetPath() const;

    // ----- face facts -----
    const std::string& GetFamilyName() const;
    const std::string& GetStyleName() const;
    int GetUnitsPerEm() const;
    int GetAscender() const;      // hhea ascender, font units
    int GetDescender() const;     // hhea descender, font units (negative)
    int GetXHeight() const;       // OS/2 sxHeight, or 0 when the font has none
    uint32_t GetGlyphCount() const;

    // ----- MATH table -----
    bool HasMathTable() const;
    // A constant in font units (percent for the *PercentScaleDown ones).
    // 0 when the font has no MATH table.
    int GetConstant(MathConstant c) const;
    // The same constant scaled to a font size in pixels (percent constants
    // are returned as a factor, e.g. 0.7).
    float GetConstantScaled(MathConstant c, float fontSizePx) const;

    // ----- glyphs -----
    // Glyph index of a code point; 0 (.notdef) when the font lacks it.
    uint32_t GetGlyphIndex(char32_t codepoint) const;
    // PostScript glyph name when the font carries one ("parenleft"), else "".
    std::string GetGlyphName(uint32_t glyph) const;
    bool GetGlyphMetrics(uint32_t glyph, MathGlyphMetrics& out) const;

    // Italics correction (0 when the font defines none for the glyph).
    int GetItalicsCorrection(uint32_t glyph) const;
    bool HasItalicsCorrection(uint32_t glyph) const;
    // Horizontal position an accent is centred on. When the font defines
    // none the specification's default - half the advance - is returned.
    int GetTopAccentAttachment(uint32_t glyph) const;
    bool HasTopAccentAttachment(uint32_t glyph) const;
    // Extended shapes are tall glyphs (integrals, big operators) whose
    // superscripts are placed by height rather than by the usual shift.
    bool IsExtendedShape(uint32_t glyph) const;

    // Math kerning for script placement; null when the glyph has none at
    // that corner.
    const MathKernTable* GetMathKern(uint32_t glyph, MathKernCorner corner) const;
    int GetMathKernValue(uint32_t glyph, MathKernCorner corner, int height) const;

    // ----- stretching -----
    int GetMinConnectorOverlap() const;
    // Pre-drawn size variants in the given direction, smallest first, as the
    // font lists them. Empty when the glyph does not stretch that way.
    std::vector<MathGlyphVariant> GetGlyphVariants(uint32_t glyph, MathStretchDirection direction) const;
    // The assembly recipe for arbitrary sizes. Returns false (and an empty
    // assembly) when the glyph has none in that direction.
    bool GetGlyphAssembly(uint32_t glyph, MathStretchDirection direction, MathGlyphAssembly& out) const;

    // ----- outlines -----
    // The glyph's outline in font units, cached per glyph after first use.
    // Returns false for an invalid glyph index; a blank glyph returns true
    // with an empty outline.
    bool GetGlyphOutline(uint32_t glyph, MathGlyphOutline& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace UltraCanvas
