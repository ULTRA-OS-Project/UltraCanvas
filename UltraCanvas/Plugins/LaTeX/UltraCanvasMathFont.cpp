// Plugins/LaTeX/UltraCanvasMathFont.cpp
// OpenType math font reader: MATH table, metrics and outlines via FreeType.
// See UltraCanvasMathFont.h for the design overview.
//
// The MATH table layout implemented here is the one in the OpenType
// specification (Microsoft Typography, "MATH - The mathematical typesetting
// table"): a header with three subtable offsets, MathConstants,
// MathGlyphInfo (italics correction, top accent attachment, extended shape
// coverage, math kern info) and MathVariants (minimum connector overlap,
// per-glyph constructions with size variants and an optional assembly).
// Every read is bounds-checked; a malformed table is reported as "no MATH
// table" with the reason in GetLastError(), never as a crash.
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "Plugins/LaTeX/UltraCanvasMathFont.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H
#include FT_OUTLINE_H
#include FT_BBOX_H

#include <array>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace UltraCanvas {

// =============================================================================
// Constant names
// =============================================================================

namespace {

constexpr int kConstantCount = static_cast<int>(MathConstant::Count);

const char* const kConstantNames[kConstantCount] = {
    "ScriptPercentScaleDown", "ScriptScriptPercentScaleDown",
    "DelimitedSubFormulaMinHeight", "DisplayOperatorMinHeight", "MathLeading",
    "AxisHeight", "AccentBaseHeight", "FlattenedAccentBaseHeight",
    "SubscriptShiftDown", "SubscriptTopMax", "SubscriptBaselineDropMin",
    "SuperscriptShiftUp", "SuperscriptShiftUpCramped", "SuperscriptBottomMin",
    "SuperscriptBaselineDropMax", "SubSuperscriptGapMin",
    "SuperscriptBottomMaxWithSubscript", "SpaceAfterScript", "UpperLimitGapMin",
    "UpperLimitBaselineRiseMin", "LowerLimitGapMin", "LowerLimitBaselineDropMin",
    "StackTopShiftUp", "StackTopDisplayStyleShiftUp", "StackBottomShiftDown",
    "StackBottomDisplayStyleShiftDown", "StackGapMin", "StackDisplayStyleGapMin",
    "StretchStackTopShiftUp", "StretchStackBottomShiftDown",
    "StretchStackGapAboveMin", "StretchStackGapBelowMin",
    "FractionNumeratorShiftUp", "FractionNumeratorDisplayStyleShiftUp",
    "FractionDenominatorShiftDown", "FractionDenominatorDisplayStyleShiftDown",
    "FractionNumeratorGapMin", "FractionNumDisplayStyleGapMin",
    "FractionRuleThickness", "FractionDenominatorGapMin",
    "FractionDenomDisplayStyleGapMin", "SkewedFractionHorizontalGap",
    "SkewedFractionVerticalGap", "OverbarVerticalGap", "OverbarRuleThickness",
    "OverbarExtraAscender", "UnderbarVerticalGap", "UnderbarRuleThickness",
    "UnderbarExtraDescender", "RadicalVerticalGap",
    "RadicalDisplayStyleVerticalGap", "RadicalRuleThickness",
    "RadicalExtraAscender", "RadicalKernBeforeDegree", "RadicalKernAfterDegree",
    "RadicalDegreeBottomRaisePercent",
};

// A bounds-checked big-endian reader over the MATH table bytes. Any read
// past the end flips `ok` and returns 0, so a parser can run to completion
// and check `ok` once instead of testing every field.
struct TableReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    bool ok = true;

    uint16_t U16(size_t off) {
        if (off + 2 > size) { ok = false; return 0; }
        return static_cast<uint16_t>((data[off] << 8) | data[off + 1]);
    }
    int16_t I16(size_t off) { return static_cast<int16_t>(U16(off)); }
    // A MathValueRecord is an int16 value followed by an Offset16 to a device
    // table; device tables only matter for hinted bitmap rendering and are
    // ignored, as every vector math renderer does.
    int16_t MathValue(size_t off) { return I16(off); }
};

// Reads a coverage table into the list of glyphs it covers, in coverage
// index order. Returns false for an unknown format or a malformed table.
bool ReadCoverage(TableReader& r, size_t off, std::vector<uint32_t>& out) {
    out.clear();
    const uint16_t format = r.U16(off);
    if (format == 1) {
        const uint16_t count = r.U16(off + 2);
        out.reserve(count);
        for (uint16_t i = 0; i < count; ++i) out.push_back(r.U16(off + 4 + 2 * i));
        return r.ok;
    }
    if (format == 2) {
        const uint16_t rangeCount = r.U16(off + 2);
        for (uint16_t i = 0; i < rangeCount; ++i) {
            const size_t rec = off + 4 + 6 * static_cast<size_t>(i);
            const uint16_t start = r.U16(rec);
            const uint16_t end = r.U16(rec + 2);
            const uint16_t startIndex = r.U16(rec + 4);
            if (!r.ok || end < start) return false;
            const size_t needed = static_cast<size_t>(startIndex) + (end - start) + 1;
            if (needed > 65536) return false;
            if (out.size() < needed) out.resize(needed, 0);
            for (uint32_t g = start; g <= end; ++g) out[startIndex + (g - start)] = g;
        }
        return r.ok;
    }
    return false;
}

constexpr int kAbsentValue = 0;

} // namespace

const char* MathConstantName(MathConstant c) {
    const int i = static_cast<int>(c);
    return (i >= 0 && i < kConstantCount) ? kConstantNames[i] : "";
}

int MathKernTable::KernAtHeight(int height) const {
    if (kernValues.empty()) return 0;
    for (size_t i = 0; i < correctionHeights.size() && i < kernValues.size(); ++i) {
        if (height < correctionHeights[i]) return kernValues[i];
    }
    return kernValues.back();
}

// =============================================================================
// Impl
// =============================================================================

struct UltraCanvasMathFont::Impl {
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    std::vector<uint8_t> memoryImage;   // backing store for LoadFromMemory
    std::string path;
    std::string lastError;
    std::string familyName, styleName;
    int unitsPerEm = 0, ascender = 0, descender = 0, xHeight = 0;

    // ----- MATH table, parsed -----
    bool hasMath = false;
    std::array<int, kConstantCount> constants{};
    int minConnectorOverlap = 0;
    std::unordered_map<uint32_t, int> italics;
    std::unordered_map<uint32_t, int> topAccent;
    std::unordered_set<uint32_t> extendedShapes;
    std::unordered_map<uint32_t, std::array<MathKernTable, 4>> kerns;

    struct Construction {
        std::vector<MathGlyphVariant> variants;
        MathGlyphAssembly assembly;
    };
    std::unordered_map<uint32_t, Construction> vertical, horizontal;

    // ----- caches -----
    mutable std::unordered_map<uint32_t, MathGlyphOutline> outlineCache;
    mutable std::unordered_map<uint32_t, MathGlyphMetrics> metricsCache;

    ~Impl() { Close(); }

    void Close() {
        if (face) { FT_Done_Face(face); face = nullptr; }
        if (library) { FT_Done_FreeType(library); library = nullptr; }
        memoryImage.clear();
        memoryImage.shrink_to_fit();
        path.clear();
        familyName.clear();
        styleName.clear();
        unitsPerEm = ascender = descender = xHeight = 0;
        hasMath = false;
        constants.fill(0);
        minConnectorOverlap = 0;
        italics.clear();
        topAccent.clear();
        extendedShapes.clear();
        kerns.clear();
        vertical.clear();
        horizontal.clear();
        outlineCache.clear();
        metricsCache.clear();
    }

    bool Fail(const std::string& why) {
        lastError = why;
        Close();
        return false;
    }

    // Everything after the face is open: face facts and the MATH table.
    bool Finish(const std::string& sourceName) {
        path = sourceName;
        familyName = face->family_name ? face->family_name : "";
        styleName = face->style_name ? face->style_name : "";
        unitsPerEm = face->units_per_EM;
        ascender = face->ascender;
        descender = face->descender;
        if (unitsPerEm <= 0) return Fail("not a scalable font: " + sourceName);
        if (auto* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2))) {
            if (os2->version >= 2) xHeight = os2->sxHeight;
        }
        lastError.clear();
        ParseMathTable();   // absence is not an error; a broken table is reported
        return true;
    }

    // ----- MATH -----

    void ParseMathTable() {
        hasMath = false;
        const FT_ULong tag = FT_MAKE_TAG('M', 'A', 'T', 'H');
        FT_ULong length = 0;
        if (FT_Load_Sfnt_Table(face, tag, 0, nullptr, &length) != 0 || length < 10) return;
        std::vector<uint8_t> bytes(length);
        if (FT_Load_Sfnt_Table(face, tag, 0, bytes.data(), &length) != 0) return;

        TableReader r{bytes.data(), bytes.size(), true};
        const uint16_t major = r.U16(0);
        if (major != 1) { lastError = "unsupported MATH table version"; return; }
        const size_t constantsOff = r.U16(4);
        const size_t glyphInfoOff = r.U16(6);
        const size_t variantsOff = r.U16(8);
        if (!r.ok) { lastError = "malformed MATH table header"; return; }

        bool good = true;
        if (constantsOff) good = ParseConstants(r, constantsOff) && good;
        if (glyphInfoOff) good = ParseGlyphInfo(r, glyphInfoOff) && good;
        if (variantsOff)  good = ParseVariants(r, variantsOff) && good;
        if (!good || !r.ok) {
            lastError = "malformed MATH table";
            constants.fill(0);
            italics.clear(); topAccent.clear(); extendedShapes.clear(); kerns.clear();
            vertical.clear(); horizontal.clear();
            return;
        }
        hasMath = true;
    }

    bool ParseConstants(TableReader& r, size_t off) {
        constants[0] = r.I16(off);       // ScriptPercentScaleDown
        constants[1] = r.I16(off + 2);   // ScriptScriptPercentScaleDown
        constants[2] = r.U16(off + 4);   // DelimitedSubFormulaMinHeight
        constants[3] = r.U16(off + 6);   // DisplayOperatorMinHeight
        // 51 MathValueRecords, MathLeading .. RadicalKernAfterDegree
        size_t p = off + 8;
        for (int i = 4; i <= 54; ++i, p += 4) constants[i] = r.MathValue(p);
        constants[55] = r.I16(p);        // RadicalDegreeBottomRaisePercent
        return r.ok;
    }

    bool ParseGlyphInfo(TableReader& r, size_t off) {
        const size_t italicsOff = r.U16(off);
        const size_t accentOff = r.U16(off + 2);
        const size_t extendedOff = r.U16(off + 4);
        const size_t kernInfoOff = r.U16(off + 6);
        if (!r.ok) return false;

        std::vector<uint32_t> coverage;
        if (italicsOff) {
            const size_t base = off + italicsOff;
            if (!ReadCoverage(r, base + r.U16(base), coverage)) return false;
            const uint16_t count = r.U16(base + 2);
            for (uint16_t i = 0; i < count && i < coverage.size(); ++i) {
                italics[coverage[i]] = r.MathValue(base + 4 + 4 * static_cast<size_t>(i));
            }
        }
        if (accentOff) {
            const size_t base = off + accentOff;
            if (!ReadCoverage(r, base + r.U16(base), coverage)) return false;
            const uint16_t count = r.U16(base + 2);
            for (uint16_t i = 0; i < count && i < coverage.size(); ++i) {
                topAccent[coverage[i]] = r.MathValue(base + 4 + 4 * static_cast<size_t>(i));
            }
        }
        if (extendedOff) {
            if (!ReadCoverage(r, off + extendedOff, coverage)) return false;
            extendedShapes.insert(coverage.begin(), coverage.end());
        }
        if (kernInfoOff) {
            const size_t base = off + kernInfoOff;
            if (!ReadCoverage(r, base + r.U16(base), coverage)) return false;
            const uint16_t count = r.U16(base + 2);
            for (uint16_t i = 0; i < count && i < coverage.size(); ++i) {
                const size_t rec = base + 4 + 8 * static_cast<size_t>(i);
                std::array<MathKernTable, 4> corners;
                bool any = false;
                for (int c = 0; c < 4; ++c) {
                    const size_t kernOff = r.U16(rec + 2 * c);
                    if (!kernOff) continue;
                    if (!ReadKern(r, base + kernOff, corners[c])) return false;
                    any = true;
                }
                if (any) kerns[coverage[i]] = std::move(corners);
            }
        }
        return r.ok;
    }

    bool ReadKern(TableReader& r, size_t off, MathKernTable& out) {
        const uint16_t heightCount = r.U16(off);
        if (!r.ok) return false;
        out.correctionHeights.reserve(heightCount);
        out.kernValues.reserve(heightCount + 1);
        size_t p = off + 2;
        for (uint16_t i = 0; i < heightCount; ++i, p += 4) out.correctionHeights.push_back(r.MathValue(p));
        for (uint16_t i = 0; i <= heightCount; ++i, p += 4) out.kernValues.push_back(r.MathValue(p));
        return r.ok;
    }

    bool ParseVariants(TableReader& r, size_t off) {
        minConnectorOverlap = r.U16(off);
        const size_t vertCovOff = r.U16(off + 2);
        const size_t horizCovOff = r.U16(off + 4);
        const uint16_t vertCount = r.U16(off + 6);
        const uint16_t horizCount = r.U16(off + 8);
        if (!r.ok) return false;

        std::vector<uint32_t> coverage;
        if (vertCovOff && vertCount) {
            if (!ReadCoverage(r, off + vertCovOff, coverage)) return false;
            for (uint16_t i = 0; i < vertCount && i < coverage.size(); ++i) {
                const size_t cOff = r.U16(off + 10 + 2 * static_cast<size_t>(i));
                if (!cOff) continue;
                if (!ReadConstruction(r, off + cOff, vertical[coverage[i]])) return false;
            }
        }
        if (horizCovOff && horizCount) {
            if (!ReadCoverage(r, off + horizCovOff, coverage)) return false;
            const size_t table = off + 10 + 2 * static_cast<size_t>(vertCount);
            for (uint16_t i = 0; i < horizCount && i < coverage.size(); ++i) {
                const size_t cOff = r.U16(table + 2 * static_cast<size_t>(i));
                if (!cOff) continue;
                if (!ReadConstruction(r, off + cOff, horizontal[coverage[i]])) return false;
            }
        }
        return r.ok;
    }

    bool ReadConstruction(TableReader& r, size_t off, Construction& out) {
        const size_t assemblyOff = r.U16(off);
        const uint16_t variantCount = r.U16(off + 2);
        if (!r.ok) return false;
        out.variants.reserve(variantCount);
        for (uint16_t i = 0; i < variantCount; ++i) {
            const size_t rec = off + 4 + 4 * static_cast<size_t>(i);
            MathGlyphVariant v;
            v.glyph = r.U16(rec);
            v.advance = r.U16(rec + 2);
            out.variants.push_back(v);
        }
        if (assemblyOff) {
            const size_t a = off + assemblyOff;
            out.assembly.italicsCorrection = r.MathValue(a);
            const uint16_t partCount = r.U16(a + 4);
            out.assembly.parts.reserve(partCount);
            for (uint16_t i = 0; i < partCount; ++i) {
                const size_t rec = a + 6 + 10 * static_cast<size_t>(i);
                MathGlyphPart part;
                part.glyph = r.U16(rec);
                part.startConnectorLength = r.U16(rec + 2);
                part.endConnectorLength = r.U16(rec + 4);
                part.fullAdvance = r.U16(rec + 6);
                part.isExtender = (r.U16(rec + 8) & 0x0001) != 0;
                out.assembly.parts.push_back(part);
            }
        }
        return r.ok;
    }

    // ----- glyph loading -----

    bool LoadGlyph(uint32_t glyph) const {
        if (!face || glyph >= static_cast<uint32_t>(face->num_glyphs)) return false;
        const FT_Int32 flags = FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING |
                               FT_LOAD_NO_BITMAP | FT_LOAD_IGNORE_TRANSFORM;
        return FT_Load_Glyph(face, glyph, flags) == 0 &&
               face->glyph->format == FT_GLYPH_FORMAT_OUTLINE;
    }

    static int MoveToCb(const FT_Vector* to, void* user) {
        auto* out = static_cast<MathGlyphOutline*>(user);
        MathOutlineSegment s;
        s.command = MathOutlineCommand::MoveTo;
        s.x1 = static_cast<int>(to->x); s.y1 = static_cast<int>(to->y);
        out->segments.push_back(s);
        return 0;
    }
    static int LineToCb(const FT_Vector* to, void* user) {
        auto* out = static_cast<MathGlyphOutline*>(user);
        MathOutlineSegment s;
        s.command = MathOutlineCommand::LineTo;
        s.x1 = static_cast<int>(to->x); s.y1 = static_cast<int>(to->y);
        out->segments.push_back(s);
        return 0;
    }
    static int ConicToCb(const FT_Vector* c, const FT_Vector* to, void* user) {
        auto* out = static_cast<MathGlyphOutline*>(user);
        MathOutlineSegment s;
        s.command = MathOutlineCommand::QuadTo;
        s.x1 = static_cast<int>(c->x);  s.y1 = static_cast<int>(c->y);
        s.x2 = static_cast<int>(to->x); s.y2 = static_cast<int>(to->y);
        out->segments.push_back(s);
        return 0;
    }
    static int CubicToCb(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user) {
        auto* out = static_cast<MathGlyphOutline*>(user);
        MathOutlineSegment s;
        s.command = MathOutlineCommand::CubicTo;
        s.x1 = static_cast<int>(c1->x); s.y1 = static_cast<int>(c1->y);
        s.x2 = static_cast<int>(c2->x); s.y2 = static_cast<int>(c2->y);
        s.x3 = static_cast<int>(to->x); s.y3 = static_cast<int>(to->y);
        out->segments.push_back(s);
        return 0;
    }
};

// =============================================================================
// UltraCanvasMathFont
// =============================================================================

UltraCanvasMathFont::UltraCanvasMathFont() : impl_(std::make_unique<Impl>()) {}
UltraCanvasMathFont::~UltraCanvasMathFont() = default;

bool UltraCanvasMathFont::Load(const std::string& path, int faceIndex) {
    impl_->Close();
    if (path.empty()) return impl_->Fail("empty font path");
    if (FT_Init_FreeType(&impl_->library) != 0) return impl_->Fail("FreeType initialisation failed");
    const FT_Error err = FT_New_Face(impl_->library, path.c_str(), faceIndex, &impl_->face);
    if (err != 0) {
        impl_->face = nullptr;
        return impl_->Fail("cannot open font face: " + path);
    }
    return impl_->Finish(path);
}

bool UltraCanvasMathFont::LoadFromMemory(const uint8_t* data, size_t size, int faceIndex) {
    impl_->Close();
    if (!data || size == 0) return impl_->Fail("empty font image");
    if (FT_Init_FreeType(&impl_->library) != 0) return impl_->Fail("FreeType initialisation failed");
    impl_->memoryImage.assign(data, data + size);
    const FT_Error err = FT_New_Memory_Face(impl_->library, impl_->memoryImage.data(),
                                            static_cast<FT_Long>(size), faceIndex, &impl_->face);
    if (err != 0) {
        impl_->face = nullptr;
        return impl_->Fail("cannot open font face from memory");
    }
    return impl_->Finish("<memory>");
}

void UltraCanvasMathFont::Unload() { impl_->Close(); }

bool UltraCanvasMathFont::IsLoaded() const { return impl_->face != nullptr; }
const std::string& UltraCanvasMathFont::GetLastError() const { return impl_->lastError; }
const std::string& UltraCanvasMathFont::GetPath() const { return impl_->path; }
const std::string& UltraCanvasMathFont::GetFamilyName() const { return impl_->familyName; }
const std::string& UltraCanvasMathFont::GetStyleName() const { return impl_->styleName; }
int UltraCanvasMathFont::GetUnitsPerEm() const { return impl_->unitsPerEm; }
int UltraCanvasMathFont::GetAscender() const { return impl_->ascender; }
int UltraCanvasMathFont::GetDescender() const { return impl_->descender; }
int UltraCanvasMathFont::GetXHeight() const { return impl_->xHeight; }
uint32_t UltraCanvasMathFont::GetGlyphCount() const {
    return impl_->face ? static_cast<uint32_t>(impl_->face->num_glyphs) : 0;
}

bool UltraCanvasMathFont::HasMathTable() const { return impl_->hasMath; }

int UltraCanvasMathFont::GetConstant(MathConstant c) const {
    const int i = static_cast<int>(c);
    if (!impl_->hasMath || i < 0 || i >= kConstantCount) return kAbsentValue;
    return impl_->constants[i];
}

float UltraCanvasMathFont::GetConstantScaled(MathConstant c, float fontSizePx) const {
    const int v = GetConstant(c);
    switch (c) {
        case MathConstant::ScriptPercentScaleDown:
        case MathConstant::ScriptScriptPercentScaleDown:
        case MathConstant::RadicalDegreeBottomRaisePercent:
            return static_cast<float>(v) / 100.f;
        default:
            break;
    }
    if (impl_->unitsPerEm <= 0) return 0.f;
    return static_cast<float>(v) * fontSizePx / static_cast<float>(impl_->unitsPerEm);
}

uint32_t UltraCanvasMathFont::GetGlyphIndex(char32_t codepoint) const {
    if (!impl_->face) return 0;
    return FT_Get_Char_Index(impl_->face, static_cast<FT_ULong>(codepoint));
}

std::string UltraCanvasMathFont::GetGlyphName(uint32_t glyph) const {
    if (!impl_->face || !FT_HAS_GLYPH_NAMES(impl_->face)) return {};
    if (glyph >= static_cast<uint32_t>(impl_->face->num_glyphs)) return {};
    char buffer[128];
    if (FT_Get_Glyph_Name(impl_->face, glyph, buffer, sizeof(buffer)) != 0) return {};
    return buffer;
}

bool UltraCanvasMathFont::GetGlyphMetrics(uint32_t glyph, MathGlyphMetrics& out) const {
    out = MathGlyphMetrics{};
    if (auto it = impl_->metricsCache.find(glyph); it != impl_->metricsCache.end()) {
        out = it->second;
        return true;
    }
    if (!impl_->LoadGlyph(glyph)) return false;
    const FT_GlyphSlot slot = impl_->face->glyph;
    out.advance = static_cast<int>(slot->metrics.horiAdvance);
    if (slot->outline.n_points > 0) {
        FT_BBox box{};
        FT_Outline_Get_BBox(&slot->outline, &box);
        out.xMin = static_cast<int>(box.xMin);
        out.yMin = static_cast<int>(box.yMin);
        out.xMax = static_cast<int>(box.xMax);
        out.yMax = static_cast<int>(box.yMax);
    }
    impl_->metricsCache[glyph] = out;
    return true;
}

int UltraCanvasMathFont::GetItalicsCorrection(uint32_t glyph) const {
    auto it = impl_->italics.find(glyph);
    return it == impl_->italics.end() ? 0 : it->second;
}

bool UltraCanvasMathFont::HasItalicsCorrection(uint32_t glyph) const {
    return impl_->italics.count(glyph) != 0;
}

int UltraCanvasMathFont::GetTopAccentAttachment(uint32_t glyph) const {
    if (auto it = impl_->topAccent.find(glyph); it != impl_->topAccent.end()) return it->second;
    MathGlyphMetrics m;
    if (!GetGlyphMetrics(glyph, m)) return 0;
    return m.advance / 2;
}

bool UltraCanvasMathFont::HasTopAccentAttachment(uint32_t glyph) const {
    return impl_->topAccent.count(glyph) != 0;
}

bool UltraCanvasMathFont::IsExtendedShape(uint32_t glyph) const {
    return impl_->extendedShapes.count(glyph) != 0;
}

const MathKernTable* UltraCanvasMathFont::GetMathKern(uint32_t glyph, MathKernCorner corner) const {
    auto it = impl_->kerns.find(glyph);
    if (it == impl_->kerns.end()) return nullptr;
    const MathKernTable& table = it->second[static_cast<int>(corner)];
    return table.IsEmpty() ? nullptr : &table;
}

int UltraCanvasMathFont::GetMathKernValue(uint32_t glyph, MathKernCorner corner, int height) const {
    const MathKernTable* table = GetMathKern(glyph, corner);
    return table ? table->KernAtHeight(height) : 0;
}

int UltraCanvasMathFont::GetMinConnectorOverlap() const {
    return impl_->hasMath ? impl_->minConnectorOverlap : 0;
}

std::vector<MathGlyphVariant>
UltraCanvasMathFont::GetGlyphVariants(uint32_t glyph, MathStretchDirection direction) const {
    const auto& table = direction == MathStretchDirection::Vertical ? impl_->vertical : impl_->horizontal;
    auto it = table.find(glyph);
    if (it == table.end()) return {};
    return it->second.variants;
}

bool UltraCanvasMathFont::GetGlyphAssembly(uint32_t glyph, MathStretchDirection direction,
                                           MathGlyphAssembly& out) const {
    out = MathGlyphAssembly{};
    const auto& table = direction == MathStretchDirection::Vertical ? impl_->vertical : impl_->horizontal;
    auto it = table.find(glyph);
    if (it == table.end() || it->second.assembly.IsEmpty()) return false;
    out = it->second.assembly;
    return true;
}

bool UltraCanvasMathFont::GetGlyphOutline(uint32_t glyph, MathGlyphOutline& out) const {
    out = MathGlyphOutline{};
    if (auto it = impl_->outlineCache.find(glyph); it != impl_->outlineCache.end()) {
        out = it->second;
        return true;
    }
    if (!impl_->LoadGlyph(glyph)) return false;
    FT_Outline_Funcs funcs{};
    funcs.move_to = &Impl::MoveToCb;
    funcs.line_to = &Impl::LineToCb;
    funcs.conic_to = &Impl::ConicToCb;
    funcs.cubic_to = &Impl::CubicToCb;
    funcs.shift = 0;
    funcs.delta = 0;
    MathGlyphOutline outline;
    if (FT_Outline_Decompose(&impl_->face->glyph->outline, &funcs, &outline) != 0) return false;
    // FreeType closes contours implicitly; make it explicit for path consumers.
    if (!outline.segments.empty()) {
        MathGlyphOutline closed;
        closed.segments.reserve(outline.segments.size() + 8);
        bool open = false;
        for (const auto& s : outline.segments) {
            if (s.command == MathOutlineCommand::MoveTo && open) {
                closed.segments.push_back(MathOutlineSegment{});   // Close
            }
            closed.segments.push_back(s);
            open = true;
        }
        if (open) closed.segments.push_back(MathOutlineSegment{});
        outline = std::move(closed);
    }
    impl_->outlineCache[glyph] = outline;
    out = outline;
    return true;
}

} // namespace UltraCanvas
