// Tests/MathFontTest.cpp
// The OpenType math font reader (UltraCanvasMathFont) - Phase 0 of the
// native LaTeX engine (Docs/UltraCanvas/UltraCanvasLaTeXEngineProposal.md).
//
// The bundled Latin Modern Math ships twice: as the OpenType file and as the
// `.clm2` container the vendored MicroTeX engine reads, which FontForge
// produced from that same OpenType file. That makes the .clm2 an oracle: every
// MATH constant, glyph metric, italics correction, top-accent attachment,
// size-variant list, assembly recipe and math-kern table our reader takes
// from the .otf must equal what FontForge wrote into the .clm2. The test
// compares them, glyph by glyph, over a sample of code points a math engine
// actually uses, then checks the outline extraction, a font without a MATH
// table, a missing file, and - when one is installed - a second math font.
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "Plugins/LaTeX/UltraCanvasMathFont.h"

#include "otf/otf.h"        // MicroTeX: the .clm2 oracle
#include "otf/glyph.h"
#include "otf/math_consts.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

fs::path MediaFile(const std::string& relative) {
    return fs::path(UC_MEDIA_DIR) / relative;
}

std::string Hex(char32_t cp) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "U+%04X", static_cast<unsigned>(cp));
    return buf;
}

// The code points a math engine reaches for first: ASCII, Greek, the math
// italic/bold alphabets, big operators, delimiters, stretchy accents and
// brace/arrow families. Every one exists in Latin Modern Math.
const std::vector<char32_t> kSampleCodePoints = {
    'a', 'f', 'x', 'y', 'A', 'V', 'W', '0', '1', '+', '=', '(', ')', '[', ']',
    '{', '}', '|', '/',
    0x03B1, 0x03B2, 0x03C0, 0x0393, 0x03A9,            // α β π Γ Ω
    0x1D44E, 0x1D453, 0x1D465, 0x1D449, 0x1D434,       // 𝑎 𝑓 𝑥 𝑉 𝐴 (math italic)
    0x1D41A, 0x1D400, 0x1D49C, 0x1D504, 0x1D538,       // 𝐚 𝐀 𝒜 𝔄 𝔸
    0x2211, 0x220F, 0x222B, 0x222C, 0x222E, 0x221A,    // ∑ ∏ ∫ ∬ ∮ √
    0x2190, 0x2192, 0x2194, 0x21D2,                    // ← → ↔ ⇒
    0x23DE, 0x23DF, 0x23B4, 0x23B5,                    // ⏞ ⏟ ⎴ ⎵
    0x0302, 0x0303, 0x0304, 0x20D7,                    // combining hat tilde bar vec
    0x27E8, 0x27E9, 0x2016, 0x2308, 0x230B,            // ⟨ ⟩ ‖ ⌈ ⌋
    0x2202, 0x221E, 0x2207,                            // ∂ ∞ ∇
};

// The 57 accessors of MicroTeX's MathConsts in MATH-table order, so the two
// readers can be compared field by field.
using ConstAccessor = microtex::i16 (microtex::MathConsts::*)() const;
const std::vector<std::pair<MathConstant, ConstAccessor>> kConstantMap = {
    {MathConstant::ScriptPercentScaleDown, &microtex::MathConsts::scriptPercentScaleDown},
    {MathConstant::ScriptScriptPercentScaleDown, &microtex::MathConsts::scriptScriptPercentScaleDown},
    {MathConstant::DelimitedSubFormulaMinHeight, &microtex::MathConsts::delimitedSubFormulaMinHeight},
    {MathConstant::DisplayOperatorMinHeight, &microtex::MathConsts::displayOperatorMinHeight},
    {MathConstant::MathLeading, &microtex::MathConsts::mathLeading},
    {MathConstant::AxisHeight, &microtex::MathConsts::axisHeight},
    {MathConstant::AccentBaseHeight, &microtex::MathConsts::accentBaseHeight},
    {MathConstant::FlattenedAccentBaseHeight, &microtex::MathConsts::flattenedAccentBaseHeight},
    {MathConstant::SubscriptShiftDown, &microtex::MathConsts::subscriptShiftDown},
    {MathConstant::SubscriptTopMax, &microtex::MathConsts::subscriptTopMax},
    {MathConstant::SubscriptBaselineDropMin, &microtex::MathConsts::subscriptBaselineDropMin},
    {MathConstant::SuperscriptShiftUp, &microtex::MathConsts::superscriptShiftUp},
    {MathConstant::SuperscriptShiftUpCramped, &microtex::MathConsts::superscriptShiftUpCramped},
    {MathConstant::SuperscriptBottomMin, &microtex::MathConsts::superscriptBottomMin},
    {MathConstant::SuperscriptBaselineDropMax, &microtex::MathConsts::superscriptBaselineDropMax},
    {MathConstant::SubSuperscriptGapMin, &microtex::MathConsts::subSuperscriptGapMin},
    {MathConstant::SuperscriptBottomMaxWithSubscript, &microtex::MathConsts::superscriptBottomMaxWithSubscript},
    {MathConstant::SpaceAfterScript, &microtex::MathConsts::spaceAfterScript},
    {MathConstant::UpperLimitGapMin, &microtex::MathConsts::upperLimitGapMin},
    {MathConstant::UpperLimitBaselineRiseMin, &microtex::MathConsts::upperLimitBaselineRiseMin},
    {MathConstant::LowerLimitGapMin, &microtex::MathConsts::lowerLimitGapMin},
    {MathConstant::LowerLimitBaselineDropMin, &microtex::MathConsts::lowerLimitBaselineDropMin},
    {MathConstant::StackTopShiftUp, &microtex::MathConsts::stackTopShiftUp},
    {MathConstant::StackTopDisplayStyleShiftUp, &microtex::MathConsts::stackTopDisplayStyleShiftUp},
    {MathConstant::StackBottomShiftDown, &microtex::MathConsts::stackBottomShiftDown},
    {MathConstant::StackBottomDisplayStyleShiftDown, &microtex::MathConsts::stackBottomDisplayStyleShiftDown},
    {MathConstant::StackGapMin, &microtex::MathConsts::stackGapMin},
    {MathConstant::StackDisplayStyleGapMin, &microtex::MathConsts::stackDisplayStyleGapMin},
    {MathConstant::StretchStackTopShiftUp, &microtex::MathConsts::stretchStackTopShiftUp},
    {MathConstant::StretchStackBottomShiftDown, &microtex::MathConsts::stretchStackBottomShiftDown},
    {MathConstant::StretchStackGapAboveMin, &microtex::MathConsts::stretchStackGapAboveMin},
    {MathConstant::StretchStackGapBelowMin, &microtex::MathConsts::stretchStackGapBelowMin},
    {MathConstant::FractionNumeratorShiftUp, &microtex::MathConsts::fractionNumeratorShiftUp},
    {MathConstant::FractionNumeratorDisplayStyleShiftUp, &microtex::MathConsts::fractionNumeratorDisplayStyleShiftUp},
    {MathConstant::FractionDenominatorShiftDown, &microtex::MathConsts::fractionDenominatorShiftDown},
    {MathConstant::FractionDenominatorDisplayStyleShiftDown, &microtex::MathConsts::fractionDenominatorDisplayStyleShiftDown},
    {MathConstant::FractionNumeratorGapMin, &microtex::MathConsts::fractionNumeratorGapMin},
    {MathConstant::FractionNumDisplayStyleGapMin, &microtex::MathConsts::fractionNumeratorDisplayStyleGapMin},
    {MathConstant::FractionRuleThickness, &microtex::MathConsts::fractionRuleThickness},
    {MathConstant::FractionDenominatorGapMin, &microtex::MathConsts::fractionDenominatorGapMin},
    {MathConstant::FractionDenomDisplayStyleGapMin, &microtex::MathConsts::fractionDenominatorDisplayStyleGapMin},
    {MathConstant::SkewedFractionHorizontalGap, &microtex::MathConsts::skewedFractionHorizontalGap},
    {MathConstant::SkewedFractionVerticalGap, &microtex::MathConsts::skewedFractionVerticalGap},
    {MathConstant::OverbarVerticalGap, &microtex::MathConsts::overbarVerticalGap},
    {MathConstant::OverbarRuleThickness, &microtex::MathConsts::overbarRuleThickness},
    {MathConstant::OverbarExtraAscender, &microtex::MathConsts::overbarExtraAscender},
    {MathConstant::UnderbarVerticalGap, &microtex::MathConsts::underbarVerticalGap},
    {MathConstant::UnderbarRuleThickness, &microtex::MathConsts::underbarRuleThickness},
    {MathConstant::UnderbarExtraDescender, &microtex::MathConsts::underbarExtraDescender},
    {MathConstant::RadicalVerticalGap, &microtex::MathConsts::radicalVerticalGap},
    {MathConstant::RadicalDisplayStyleVerticalGap, &microtex::MathConsts::radicalDisplayStyleVerticalGap},
    {MathConstant::RadicalRuleThickness, &microtex::MathConsts::radicalRuleThickness},
    {MathConstant::RadicalExtraAscender, &microtex::MathConsts::radicalExtraAscender},
    {MathConstant::RadicalKernBeforeDegree, &microtex::MathConsts::radicalKernBeforeDegree},
    {MathConstant::RadicalKernAfterDegree, &microtex::MathConsts::radicalKernAfterDegree},
    {MathConstant::RadicalDegreeBottomRaisePercent, &microtex::MathConsts::radicalDegreeBottomRaisePercent},
};

bool SameVariants(const std::vector<MathGlyphVariant>& ours, const microtex::Variants& theirs) {
    if (ours.size() != theirs.count()) return false;
    for (size_t i = 0; i < ours.size(); ++i) {
        if (ours[i].glyph != theirs[static_cast<microtex::u32>(i)]) return false;
    }
    return true;
}

bool SameAssembly(const MathGlyphAssembly& ours, const microtex::GlyphAssembly& theirs) {
    if (ours.parts.size() != theirs.partCount()) return false;
    if (ours.italicsCorrection != theirs.italicsCorrection()) return false;
    for (size_t i = 0; i < ours.parts.size(); ++i) {
        const auto& a = ours.parts[i];
        const auto& b = theirs[static_cast<microtex::u16>(i)];
        if (a.glyph != b.glyph() || a.isExtender != b.isExtender() ||
            a.startConnectorLength != b.startConnectorLength() ||
            a.endConnectorLength != b.endConnectorLength() ||
            a.fullAdvance != b.fullAdvance()) return false;
    }
    return true;
}

// FontForge stores the (height, kern) pairs it read; MicroTeX keeps them
// as-is. The OpenType table has n heights and n+1 values, and FontForge's
// last pair carries the top value with an out-of-range height, so the two
// agree on every value and on every real height.
bool SameKern(const MathKernTable* ours, const microtex::MathKern& theirs) {
    if (!ours) return theirs.count() == 0;
    const size_t theirCount = theirs.count();
    if (ours->kernValues.size() != theirCount && ours->kernValues.size() != theirCount + 1) return false;
    const size_t n = std::min(ours->kernValues.size(), static_cast<size_t>(theirs.count()));
    for (size_t i = 0; i < n; ++i) {
        if (ours->kernValues[i] != theirs.value(static_cast<microtex::u16>(i))) return false;
        if (i < ours->correctionHeights.size() &&
            ours->correctionHeights[i] != theirs.correctionHeight(static_cast<microtex::u16>(i))) return false;
    }
    return true;
}

// ----- the oracle comparison -----

void TestAgainstClm(UltraCanvasMathFont& font, const microtex::Otf& clm) {
    std::cout << "\nFace facts vs .clm2\n";
    Check(font.GetUnitsPerEm() == clm.em(), "units per em " + std::to_string(font.GetUnitsPerEm()));
    Check(font.GetGlyphCount() == clm.glyphsCount(),
          "glyph count " + std::to_string(font.GetGlyphCount()) + " == " + std::to_string(clm.glyphsCount()));
    Check(font.GetXHeight() == clm.xHeight(), "x-height " + std::to_string(font.GetXHeight()));

    std::cout << "\nMATH constants vs .clm2 (" << kConstantMap.size() << " fields)\n";
    const microtex::MathConsts* mc = clm.mathConsts();
    Check(mc != nullptr, ".clm2 carries MathConsts");
    if (mc) {
        int mismatches = 0;
        for (const auto& [ours, theirs] : kConstantMap) {
            const int a = font.GetConstant(ours);
            const int b = (mc->*theirs)();
            if (a != b) {
                ++mismatches;
                std::cout << "         " << MathConstantName(ours) << ": otf " << a << " clm " << b << "\n";
            }
        }
        Check(mismatches == 0, "all constants equal");
        Check(font.GetMinConnectorOverlap() == mc->minConnectorOverlap(),
              "MinConnectorOverlap " + std::to_string(font.GetMinConnectorOverlap()));
        Check(font.GetConstant(MathConstant::AxisHeight) > 0 &&
              font.GetConstant(MathConstant::FractionRuleThickness) > 0,
              "axis height and rule thickness are positive lengths");
    }

    std::cout << "\nCode point mapping vs .clm2 over " << kSampleCodePoints.size() << " code points\n";
    int mapped = 0;
    for (char32_t cp : kSampleCodePoints) {
        const uint32_t gid = font.GetGlyphIndex(cp);
        const microtex::i32 theirGid = clm.glyphId(cp);
        if (gid != 0 && theirGid >= 0 && static_cast<microtex::i32>(gid) == theirGid) { ++mapped; continue; }
        std::cout << "         " << Hex(cp) << " otf gid " << gid << ", clm gid " << theirGid << "\n";
    }
    Check(mapped == static_cast<int>(kSampleCodePoints.size()),
          "every code point maps to the same glyph id (" + std::to_string(mapped) + ")");

    std::cout << "\nPer-glyph data vs .clm2 over all " << clm.glyphsCount() << " glyphs\n";
    int metricsOk = 0, italicsOk = 0, accentOk = 0, variantsOk = 0, assemblyOk = 0, kernOk = 0;
    int italicsSeen = 0, accentSeen = 0, variantsSeen = 0, assemblySeen = 0, kernSeen = 0, reported = 0;
    auto report = [&](uint32_t gid, const std::string& what) {
        if (reported++ < 20) std::cout << "         glyph " << gid << " (" << font.GetGlyphName(gid) << ") " << what << "\n";
    };
    const uint32_t total = std::min<uint32_t>(font.GetGlyphCount(), clm.glyphsCount());
    for (uint32_t gid = 0; gid < total; ++gid) {
        const microtex::Glyph* g = clm.glyph(static_cast<microtex::i32>(gid));
        if (!g) continue;

        MathGlyphMetrics m;
        const bool haveMetrics = font.GetGlyphMetrics(gid, m);
        const auto& tm = g->metrics();
        if (haveMetrics && m.advance == tm.width() &&
            std::abs(m.Height() - tm.height()) <= 1 && std::abs(m.Depth() - tm.depth()) <= 1) {
            ++metricsOk;
        } else {
            report(gid, "metrics otf (" + std::to_string(m.advance) + "," + std::to_string(m.Height()) + "," +
                        std::to_string(m.Depth()) + ") clm (" + std::to_string(tm.width()) + "," +
                        std::to_string(tm.height()) + "," + std::to_string(tm.depth()) + ")");
        }

        const auto& math = g->math();
        if (math.italicsCorrection() != 0 && math.italicsCorrection() != microtex::Otf::undefinedMathValue) {
            ++italicsSeen;
            if (font.GetItalicsCorrection(gid) == math.italicsCorrection()) ++italicsOk;
            else report(gid, "italics otf " + std::to_string(font.GetItalicsCorrection(gid)) + " clm " +
                             std::to_string(math.italicsCorrection()));
        }
        if (math.topAccentAttachment() != microtex::Otf::undefinedMathValue) {
            ++accentSeen;
            if (font.HasTopAccentAttachment(gid) && font.GetTopAccentAttachment(gid) == math.topAccentAttachment()) ++accentOk;
            else report(gid, "top accent otf " + std::to_string(font.GetTopAccentAttachment(gid)) + " clm " +
                             std::to_string(math.topAccentAttachment()));
        }
        for (auto dir : {MathStretchDirection::Vertical, MathStretchDirection::Horizontal}) {
            const bool vertical = dir == MathStretchDirection::Vertical;
            const char* name = vertical ? "vertical" : "horizontal";
            const auto& theirVariants = vertical ? math.verticalVariants() : math.horizontalVariants();
            const auto& theirAssembly = vertical ? math.verticalAssembly() : math.horizontalAssembly();
            const auto ourVariants = font.GetGlyphVariants(gid, dir);
            if (!theirVariants.isEmpty() || !ourVariants.empty()) {
                ++variantsSeen;
                if (SameVariants(ourVariants, theirVariants)) ++variantsOk;
                else report(gid, std::string(name) + " variants differ (otf " + std::to_string(ourVariants.size()) +
                                 ", clm " + std::to_string(theirVariants.count()) + ")");
            }
            MathGlyphAssembly ourAssembly;
            const bool haveAssembly = font.GetGlyphAssembly(gid, dir, ourAssembly);
            if (!theirAssembly.isEmpty() || haveAssembly) {
                ++assemblySeen;
                if (SameAssembly(ourAssembly, theirAssembly)) ++assemblyOk;
                else report(gid, std::string(name) + " assembly differs (otf " + std::to_string(ourAssembly.parts.size()) +
                                 " parts, clm " + std::to_string(theirAssembly.partCount()) + ")");
            }
        }
        const auto& kr = math.kernRecord();
        const std::pair<MathKernCorner, const microtex::MathKern*> corners[] = {
            {MathKernCorner::TopRight, &kr.topRight()}, {MathKernCorner::TopLeft, &kr.topLeft()},
            {MathKernCorner::BottomRight, &kr.bottomRight()}, {MathKernCorner::BottomLeft, &kr.bottomLeft()},
        };
        for (const auto& [corner, theirs] : corners) {
            const MathKernTable* ours = font.GetMathKern(gid, corner);
            if (theirs->count() == 0 && !ours) continue;
            ++kernSeen;
            if (SameKern(ours, *theirs)) ++kernOk;
            else report(gid, "math kern corner " + std::to_string(static_cast<int>(corner)) + " differs (otf " +
                             std::to_string(ours ? ours->kernValues.size() : 0) + " values, clm " +
                             std::to_string(theirs->count()) + ")");
        }
    }
    auto ratio = [](int ok, int seen) { return " (" + std::to_string(ok) + "/" + std::to_string(seen) + ")"; };
    Check(metricsOk == static_cast<int>(total), "advance/height/depth equal for every glyph" + ratio(metricsOk, total));
    Check(italicsSeen > 0 && italicsOk == italicsSeen, "italics corrections equal" + ratio(italicsOk, italicsSeen));
    Check(accentSeen > 0 && accentOk == accentSeen, "top-accent attachments equal" + ratio(accentOk, accentSeen));
    Check(variantsSeen > 0 && variantsOk == variantsSeen, "size-variant lists equal" + ratio(variantsOk, variantsSeen));
    Check(assemblySeen > 0 && assemblyOk == assemblySeen, "glyph assemblies equal" + ratio(assemblyOk, assemblySeen));
    // Latin Modern Math carries no MathKernInfo; the check still runs so a
    // font that has one is compared, and both readers must agree on absence.
    Check(kernOk == kernSeen, "math-kern tables equal" + ratio(kernOk, kernSeen));
}

// ----- the reader on its own -----

void TestStretchSemantics(UltraCanvasMathFont& font) {
    std::cout << "\nStretch semantics\n";
    const uint32_t brace = font.GetGlyphIndex('{');
    const auto variants = font.GetGlyphVariants(brace, MathStretchDirection::Vertical);
    Check(variants.size() >= 3, "left brace has a ladder of vertical variants (" + std::to_string(variants.size()) + ")");
    bool ascending = true;
    for (size_t i = 1; i < variants.size(); ++i) ascending = ascending && variants[i].advance >= variants[i - 1].advance;
    Check(ascending, "variants are listed smallest first");
    MathGlyphAssembly assembly;
    Check(font.GetGlyphAssembly(brace, MathStretchDirection::Vertical, assembly), "left brace has a vertical assembly");
    bool hasExtender = false, partsExist = true;
    for (const auto& p : assembly.parts) {
        hasExtender = hasExtender || p.isExtender;
        partsExist = partsExist && p.glyph != 0 && p.glyph < font.GetGlyphCount();
    }
    Check(hasExtender, "assembly has an extender part");
    Check(partsExist, "every assembly part is a real glyph");
    Check(font.GetGlyphVariants(brace, MathStretchDirection::Horizontal).empty(),
          "left brace does not stretch horizontally");

    const uint32_t overbrace = font.GetGlyphIndex(0x23DE);
    Check(!font.GetGlyphVariants(overbrace, MathStretchDirection::Horizontal).empty() ||
          font.GetGlyphAssembly(overbrace, MathStretchDirection::Horizontal, assembly),
          "top brace U+23DE stretches horizontally");

    const uint32_t integral = font.GetGlyphIndex(0x222B);
    const uint32_t letter = font.GetGlyphIndex('x');
    std::cout << "         integral extended shape: " << (font.IsExtendedShape(integral) ? "yes" : "no")
              << ", 'x': " << (font.IsExtendedShape(letter) ? "yes" : "no") << "\n";
    Check(!font.IsExtendedShape(letter), "a letter is not an extended shape");
}

void TestKernSemantics(UltraCanvasMathFont& font) {
    std::cout << "\nMath-kern lookup\n";
    MathKernTable t;
    t.correctionHeights = {100, 300};
    t.kernValues = {-10, -20, -30};
    Check(t.KernAtHeight(50) == -10 && t.KernAtHeight(200) == -20 && t.KernAtHeight(900) == -30,
          "KernAtHeight picks the band below each correction height and the last above");
    Check(MathKernTable{}.KernAtHeight(100) == 0, "an empty table kerns by zero");
    Check(font.GetMathKernValue(font.GetGlyphIndex('+'), MathKernCorner::TopRight, 500) == 0,
          "a glyph without kern info kerns by zero");
}

void TestOutlines(UltraCanvasMathFont& font) {
    std::cout << "\nOutlines\n";
    const uint32_t x = font.GetGlyphIndex('x');
    MathGlyphOutline outline;
    Check(font.GetGlyphOutline(x, outline) && !outline.IsEmpty(), "'x' has an outline");
    int moves = 0, closes = 0;
    bool onCurveInsideBox = true, curves = false;
    MathGlyphMetrics m;
    font.GetGlyphMetrics(x, m);
    auto inside = [&](int px, int py) { return px >= m.xMin && px <= m.xMax && py >= m.yMin && py <= m.yMax; };
    for (const auto& s : outline.segments) {
        switch (s.command) {
            case MathOutlineCommand::MoveTo: ++moves; onCurveInsideBox = onCurveInsideBox && inside(s.x1, s.y1); break;
            case MathOutlineCommand::LineTo: onCurveInsideBox = onCurveInsideBox && inside(s.x1, s.y1); break;
            case MathOutlineCommand::QuadTo: curves = true; onCurveInsideBox = onCurveInsideBox && inside(s.x2, s.y2); break;
            case MathOutlineCommand::CubicTo: curves = true; onCurveInsideBox = onCurveInsideBox && inside(s.x3, s.y3); break;
            case MathOutlineCommand::Close: ++closes; break;
        }
    }
    Check(moves >= 1 && moves == closes, "every contour is opened once and closed once (" + std::to_string(moves) + ")");
    Check(curves, "the outline has curve segments (a CFF font is all cubics)");
    Check(onCurveInsideBox, "every on-curve point lies inside the reported bounding box");
    Check(outline.segments.back().command == MathOutlineCommand::Close, "the outline ends with Close");

    MathGlyphOutline again;
    font.GetGlyphOutline(x, again);
    Check(again.segments.size() == outline.segments.size(), "cached outline is the same outline");

    const uint32_t space = font.GetGlyphIndex(' ');
    MathGlyphOutline blank;
    Check(font.GetGlyphOutline(space, blank) && blank.IsEmpty(), "a space has an empty outline, not a failure");
    Check(!font.GetGlyphOutline(font.GetGlyphCount() + 10, blank), "an out-of-range glyph fails");

    Check(!font.GetGlyphName(font.GetGlyphIndex('(')).empty(), "glyph names are readable (" + font.GetGlyphName(font.GetGlyphIndex('(')) + ")");
}

void TestScaling(UltraCanvasMathFont& font) {
    std::cout << "\nScaling\n";
    const int axis = font.GetConstant(MathConstant::AxisHeight);
    const float scaled = font.GetConstantScaled(MathConstant::AxisHeight, static_cast<float>(font.GetUnitsPerEm()));
    Check(static_cast<int>(scaled + 0.5f) == axis, "a constant scaled to one em is itself");
    const float pct = font.GetConstantScaled(MathConstant::ScriptPercentScaleDown, 20.f);
    Check(pct > 0.3f && pct < 1.0f, "ScriptPercentScaleDown scales to a factor (" + std::to_string(pct) + ")");
    Check(font.GetTopAccentAttachment(font.GetGlyphIndex('+')) > 0, "top accent defaults to half the advance when absent");
}

void TestNonMathAndMissing() {
    std::cout << "\nA text font, and a missing file\n";
    UltraCanvasMathFont text;
    const fs::path ubuntu = MediaFile("fonts/Ubuntu-R.ttf");
    if (fs::exists(ubuntu)) {
        Check(text.Load(ubuntu.string()), "Ubuntu-R.ttf loads");
        Check(!text.HasMathTable(), "...without a MATH table");
        Check(text.GetConstant(MathConstant::AxisHeight) == 0, "...constants read as 0");
        Check(text.GetGlyphVariants(text.GetGlyphIndex('('), MathStretchDirection::Vertical).empty(),
              "...no variants");
        Check(text.GetMinConnectorOverlap() == 0, "...no connector overlap");
        MathGlyphMetrics m;
        const bool haveMetrics = text.GetGlyphMetrics(text.GetGlyphIndex('A'), m);
        Check(haveMetrics && m.advance > 0 && m.Height() > 0,
              "...but metrics still work (advance " + std::to_string(m.advance) + ")");
        MathGlyphOutline o;
        Check(text.GetGlyphOutline(text.GetGlyphIndex('A'), o) && !o.IsEmpty(), "...and outlines (TrueType quads)");
    } else {
        std::cout << "         (bundled Ubuntu-R.ttf not found, skipped)\n";
    }
    UltraCanvasMathFont missing;
    Check(!missing.Load(MediaFile("microtex/does-not-exist.otf").string()), "a missing file fails to load");
    Check(!missing.GetLastError().empty(), "...with a reason: " + missing.GetLastError());
    Check(!missing.IsLoaded() && missing.GetGlyphCount() == 0, "...and stays unloaded");
    Check(!missing.Load(""), "an empty path fails");
}

void TestSecondMathFont() {
    std::cout << "\nA second OpenType math font (optional)\n";
    const char* candidates[] = {
        "/usr/share/fonts/opentype/stix/STIXMath-Regular.otf",
        "/usr/share/fonts/opentype/stix-word/STIXMath-Regular.otf",
        "/usr/share/fonts/truetype/stix/STIXMath-Regular.otf",
        "/usr/share/fonts/opentype/stix2/STIXTwoMath-Regular.otf",
        "/usr/share/fonts/opentype/libertinus/libertinusmath-regular.otf",
        "/usr/share/fonts/opentype/texgyre/texgyretermes-math.otf",
        "/usr/share/texmf/fonts/opentype/public/tex-gyre-math/texgyretermes-math.otf",
        "/usr/share/texmf/fonts/opentype/public/stix/STIXMath-Regular.otf",
    };
    int found = 0;
    for (const char* path : candidates) {
        if (!fs::exists(path)) continue;
        ++found;
        UltraCanvasMathFont font;
        Check(font.Load(path), std::string("loads ") + path);
        Check(font.HasMathTable(), font.GetFamilyName() + " has a MATH table");
        Check(font.GetConstant(MathConstant::AxisHeight) > 0, "...with an axis height");
        MathGlyphAssembly a;
        const bool haveAssembly = font.GetGlyphAssembly(font.GetGlyphIndex('{'), MathStretchDirection::Vertical, a);
        Check(haveAssembly, "...and a brace assembly of " + std::to_string(a.parts.size()) + " parts");
        MathGlyphOutline o;
        Check(font.GetGlyphOutline(font.GetGlyphIndex(0x222B), o) && !o.IsEmpty(), "...and an integral outline");
    }
    if (!found) std::cout << "         (none installed, skipped)\n";
}

// ----- a synthetic MATH table -----
// None of the math fonts at hand (Latin Modern, STIX, the TeX Gyre family)
// carries MathKernInfo, so the kern parser cannot be checked against a real
// file. Instead a text font gets a hand-assembled MATH table appended to its
// sfnt directory and is loaded from memory: one glyph with a top-right kern
// table of two heights and a bottom-left table of one value, plus a few
// constants, so the parser's offsets, counts and the n/n+1 rule are exercised
// end to end through the same code path a real font takes.

void Put16(std::vector<uint8_t>& v, size_t off, uint16_t x) { v[off] = uint8_t(x >> 8); v[off + 1] = uint8_t(x); }
void Put32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    v[off] = uint8_t(x >> 24); v[off + 1] = uint8_t(x >> 16); v[off + 2] = uint8_t(x >> 8); v[off + 3] = uint8_t(x);
}
uint16_t Get16(const std::vector<uint8_t>& v, size_t off) { return uint16_t((v[off] << 8) | v[off + 1]); }
uint32_t Get32(const std::vector<uint8_t>& v, size_t off) {
    return (uint32_t(v[off]) << 24) | (uint32_t(v[off + 1]) << 16) | (uint32_t(v[off + 2]) << 8) | v[off + 3];
}

std::vector<uint8_t> BuildMathTable(uint16_t glyph) {
    std::vector<uint8_t> t(10 + 214 + 8 + 46, 0);
    // header: version 1.0, constants at 10, glyph info at 224, no variants
    Put16(t, 0, 1); Put16(t, 2, 0); Put16(t, 4, 10); Put16(t, 6, 224); Put16(t, 8, 0);
    // constants
    const size_t c = 10;
    Put16(t, c + 0, 70);      // ScriptPercentScaleDown
    Put16(t, c + 2, 50);      // ScriptScriptPercentScaleDown
    Put16(t, c + 4, 1300);    // DelimitedSubFormulaMinHeight
    Put16(t, c + 6, 1450);    // DisplayOperatorMinHeight
    Put16(t, c + 8 + 4 * (5 - 4), 250);                     // AxisHeight (record index 1)
    Put16(t, c + 8 + 4 * (38 - 4), 40);                     // FractionRuleThickness
    Put16(t, c + 8 + 4 * 51, uint16_t(int16_t(-60)));       // RadicalDegreeBottomRaisePercent (negative on purpose)
    // glyph info at 224: only a MathKernInfo, at +8
    const size_t gi = 224;
    Put16(t, gi + 6, 8);
    const size_t ki = gi + 8;                                // MathKernInfo
    Put16(t, ki + 0, 12);                                    // coverage at +12
    Put16(t, ki + 2, 1);                                     // one record
    Put16(t, ki + 4, 18); Put16(t, ki + 6, 0);               // TopRight at +18, TopLeft none
    Put16(t, ki + 8, 0);  Put16(t, ki + 10, 40);             // BottomRight none, BottomLeft at +40
    Put16(t, ki + 12, 1); Put16(t, ki + 14, 1); Put16(t, ki + 16, glyph);   // coverage format 1, one glyph
    const size_t tr = ki + 18;                               // heights 100, 300; values -10, -20, -30
    Put16(t, tr, 2);
    Put16(t, tr + 2, 100); Put16(t, tr + 6, 300);
    Put16(t, tr + 10, uint16_t(int16_t(-10))); Put16(t, tr + 14, uint16_t(int16_t(-20))); Put16(t, tr + 18, uint16_t(int16_t(-30)));
    const size_t bl = ki + 40;                               // no heights; one value 15
    Put16(t, bl, 0); Put16(t, bl + 2, 15);
    return t;
}

// Appends `table` as a new sfnt table `tag` to a font image.
std::vector<uint8_t> AppendSfntTable(const std::vector<uint8_t>& font, const char* tag, const std::vector<uint8_t>& table) {
    const uint16_t numTables = Get16(font, 4);
    const size_t dirEnd = 12 + 16 * size_t(numTables);
    std::vector<uint8_t> out(font.begin(), font.begin() + 12);
    Put16(out, 4, numTables + 1);
    for (uint16_t i = 0; i < numTables; ++i) {
        const size_t rec = 12 + 16 * size_t(i);
        out.insert(out.end(), font.begin() + rec, font.begin() + rec + 16);
        Put32(out, out.size() - 8, Get32(font, rec + 8) + 16);   // data moves down by one record
    }
    size_t mathOffset = font.size() + 16;
    while (mathOffset % 4) ++mathOffset;
    out.resize(out.size() + 16);
    std::memcpy(&out[out.size() - 16], tag, 4);
    Put32(out, out.size() - 8, uint32_t(mathOffset));
    Put32(out, out.size() - 4, uint32_t(table.size()));
    out.insert(out.end(), font.begin() + dirEnd, font.end());
    out.resize(mathOffset, 0);
    out.insert(out.end(), table.begin(), table.end());
    return out;
}

void TestSyntheticKernTable() {
    std::cout << "\nMath kern parser on a synthetic MATH table\n";
    const fs::path ubuntu = MediaFile("fonts/Ubuntu-R.ttf");
    if (!fs::exists(ubuntu)) { std::cout << "         (bundled Ubuntu-R.ttf not found, skipped)\n"; return; }
    std::ifstream in(ubuntu, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    UltraCanvasMathFont plain;
    Check(plain.LoadFromMemory(bytes.data(), bytes.size()), "the text font loads from memory");
    const uint32_t glyphA = plain.GetGlyphIndex('A');
    Check(glyphA != 0 && !plain.HasMathTable(), "...has an 'A' and no MATH table");

    const std::vector<uint8_t> image = AppendSfntTable(bytes, "MATH", BuildMathTable(uint16_t(glyphA)));
    UltraCanvasMathFont synth;
    Check(synth.LoadFromMemory(image.data(), image.size()), "the font with the appended MATH table loads: " + synth.GetLastError());
    Check(synth.HasMathTable(), "...and now has a MATH table");
    Check(synth.GetGlyphIndex('A') == glyphA && synth.GetGlyphCount() == plain.GetGlyphCount(),
          "...with its other tables intact");
    Check(synth.GetConstant(MathConstant::ScriptPercentScaleDown) == 70 &&
          synth.GetConstant(MathConstant::ScriptScriptPercentScaleDown) == 50 &&
          synth.GetConstant(MathConstant::DelimitedSubFormulaMinHeight) == 1300 &&
          synth.GetConstant(MathConstant::DisplayOperatorMinHeight) == 1450 &&
          synth.GetConstant(MathConstant::AxisHeight) == 250 &&
          synth.GetConstant(MathConstant::FractionRuleThickness) == 40 &&
          synth.GetConstant(MathConstant::RadicalDegreeBottomRaisePercent) == -60 &&
          synth.GetConstant(MathConstant::MathLeading) == 0,
          "constants land in the right slots, signed");
    Check(synth.GetConstantScaled(MathConstant::RadicalDegreeBottomRaisePercent, 10.f) < 0, "a negative percent scales negative");
    const MathKernTable* tr = synth.GetMathKern(glyphA, MathKernCorner::TopRight);
    Check(tr != nullptr, "'A' has a top-right kern table");
    if (tr) {
        Check(tr->correctionHeights == std::vector<int>{100, 300}, "...with heights 100, 300");
        Check(tr->kernValues == std::vector<int>{-10, -20, -30}, "...and n+1 values -10, -20, -30");
        Check(synth.GetMathKernValue(glyphA, MathKernCorner::TopRight, 50) == -10 &&
              synth.GetMathKernValue(glyphA, MathKernCorner::TopRight, 250) == -20 &&
              synth.GetMathKernValue(glyphA, MathKernCorner::TopRight, 1000) == -30,
              "...looked up by height");
    }
    const MathKernTable* bl = synth.GetMathKern(glyphA, MathKernCorner::BottomLeft);
    Check(bl && bl->correctionHeights.empty() && bl->kernValues == std::vector<int>{15},
          "'A' has a bottom-left kern table with no heights and one value");
    Check(synth.GetMathKern(glyphA, MathKernCorner::TopLeft) == nullptr &&
          synth.GetMathKern(glyphA, MathKernCorner::BottomRight) == nullptr,
          "the corners the table omits are absent");
    Check(synth.GetMathKern(synth.GetGlyphIndex('B'), MathKernCorner::TopRight) == nullptr,
          "a glyph outside the coverage has no kern table");
    Check(synth.GetGlyphVariants(glyphA, MathStretchDirection::Vertical).empty() &&
          synth.GetMinConnectorOverlap() == 0, "no variants table means no variants and no overlap");

    // A truncated table must be rejected, not crash, and leaves the face usable.
    std::vector<uint8_t> broken = BuildMathTable(uint16_t(glyphA));
    broken.resize(230);
    UltraCanvasMathFont bad;
    const std::vector<uint8_t> badImage = AppendSfntTable(bytes, "MATH", broken);
    Check(bad.LoadFromMemory(badImage.data(), badImage.size()), "a font with a truncated MATH table still loads");
    Check(!bad.HasMathTable() && bad.GetLastError().find("malformed") != std::string::npos,
          "...reports the table as malformed: " + bad.GetLastError());
    Check(bad.GetGlyphIndex('A') == glyphA, "...and its glyphs still resolve");
}

} // namespace

int main() {
    std::cout << "UltraCanvasMathFont test\n";

    const fs::path otf = MediaFile("microtex/latinmodern-math.otf");
    const fs::path clm = MediaFile("microtex/latinmodern-math.clm2");
    if (!fs::exists(otf) || !fs::exists(clm)) {
        std::cout << "  [FAIL] bundled Latin Modern Math not found under " << UC_MEDIA_DIR << "/microtex\n";
        return 1;
    }

    UltraCanvasMathFont font;
    std::cout << "\nLoading\n";
    Check(font.Load(otf.string()), "latinmodern-math.otf loads: " + font.GetLastError());
    Check(font.IsLoaded(), "IsLoaded");
    Check(font.HasMathTable(), "has a MATH table");
    Check(font.GetFamilyName().find("Latin Modern Math") != std::string::npos,
          "family name: " + font.GetFamilyName());
    Check(font.GetUnitsPerEm() == 1000, "units per em 1000");
    Check(font.GetAscender() > 0 && font.GetDescender() < 0, "ascender/descender have the right signs");

    std::unique_ptr<microtex::Otf> oracle(microtex::Otf::fromFile(clm.string().c_str()));
    Check(oracle != nullptr && oracle->isMathFont(), ".clm2 oracle loads");
    if (oracle && font.IsLoaded()) TestAgainstClm(font, *oracle);

    if (font.IsLoaded()) {
        TestStretchSemantics(font);
        TestKernSemantics(font);
        TestOutlines(font);
        TestScaling(font);
    }
    TestNonMathAndMissing();
    TestSyntheticKernTable();
    TestSecondMathFont();

    std::cout << "\n" << (g_failures == 0 ? "ALL PASSED" : std::to_string(g_failures) + " FAILURE(S)") << "\n";
    return g_failures == 0 ? 0 : 1;
}
