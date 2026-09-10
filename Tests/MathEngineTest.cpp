// Tests/MathEngineTest.cpp
// The native math engine (Phase 1 of UltraCanvasLaTeXEngineProposal.md):
// parser, layout and the facade, checked three ways.
//
// 1. Parser: the atom tree a piece of LaTeX produces - scripts, fractions,
//    fences, environments, macros, text mode, errors with diagnostics.
// 2. Layout: box metrics against the font's own numbers - a glyph box is
//    its advance and bounding box, a fraction rule sits on the axis, scripts
//    obey the MATH constants, inter-atom spacing is TeX's table, display
//    operators grow, delimiters reach their target.
// 3. Oracle: the vendored MicroTeX typesets the same corpus (the shipped
//    media/LaTex documents plus a list of formulas) and the two engines'
//    widths and heights must agree within a tolerance - not because MicroTeX
//    is the truth, but because a large drift is a regression in one of them.
//    MicroTeX is driven with a stub platform (no drawing, text runs
//    estimated), so formulas that lean on \text are compared loosely.
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "Plugins/LaTeX/UltraCanvasMathEngine.h"
#include "Plugins/LaTeX/UltraCanvasMathParser.h"
#include "Plugins/LaTeX/UltraCanvasMathSymbols.h"

// MicroTeX oracle
#include "microtex.h"
#include "graphic/graphic.h"
#include "graphic/graphic_basic.h"
#include "render/render.h"
#include "unimath/font_src.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

std::string Fmt(double v) { char b[32]; std::snprintf(b, sizeof(b), "%.1f", v); return b; }

fs::path MediaFile(const std::string& relative) { return fs::path(UC_MEDIA_DIR) / relative; }

// ----- atom tree helpers -----

MathAtomPtr ParseOne(const std::string& src, std::vector<MathDiagnostic>* diags = nullptr) {
    UltraCanvasMathParser parser;
    std::vector<MathDiagnostic> d;
    MathAtomPtr root = parser.Parse(src, d);
    if (diags) *diags = d;
    return root;
}

const MathAtom* Nth(const MathAtomPtr& root, size_t i) {
    if (!root || root->kind != MathAtomKind::Row || i >= root->body.size()) return nullptr;
    return root->body[i].get();
}

// Finds the first box of a type in a tree (depth first).
const MathBox* FindBox(const MathBox* b, MathBoxType type) {
    if (!b) return nullptr;
    if (b->type == type) return b;
    for (const auto& c : b->children) if (const MathBox* f = FindBox(c.box.get(), type)) return f;
    return nullptr;
}

int CountBoxes(const MathBox* b, MathBoxType type) {
    if (!b) return 0;
    int n = b->type == type ? 1 : 0;
    for (const auto& c : b->children) n += CountBoxes(c.box.get(), type);
    return n;
}

// The absolute position (relative to the root origin) of the first glyph box
// whose glyph id matches; returns false when absent.
bool FindGlyphPosition(const MathBox* b, uint32_t glyph, float x, float y, float& outX, float& outY, float& outSize) {
    if (!b) return false;
    if (b->type == MathBoxType::Glyph && b->glyph == glyph) { outX = x; outY = y; outSize = b->fontSize; return true; }
    for (const auto& c : b->children) if (FindGlyphPosition(c.box.get(), glyph, x + c.dx, y + c.dy, outX, outY, outSize)) return true;
    return false;
}

// ----- parser -----

void TestParser() {
    std::cout << "\nParser\n";
    std::vector<MathDiagnostic> diags;

    MathAtomPtr r = ParseOne("x^2_i", &diags);
    const MathAtom* s = Nth(r, 0);
    Check(r && r->body.size() == 1 && s && s->kind == MathAtomKind::Scripts && s->hasSup && s->hasSub &&
          s->nucleus && s->nucleus->kind == MathAtomKind::Char && s->nucleus->codepoint == 'x',
          "x^2_i is one Scripts atom on x with both scripts");
    Check(diags.empty(), "...without diagnostics");

    r = ParseOne("\\frac{a+b}{2}");
    s = Nth(r, 0);
    Check(s && s->kind == MathAtomKind::Fraction && s->atomClass == MathAtomClass::Inner &&
          s->numerator.size() == 3 && s->denominator.size() == 1, "\\frac is an Inner fraction with 3-atom numerator");

    r = ParseOne("a + b = c");
    Check(r && r->body.size() == 5 && Nth(r, 1)->atomClass == MathAtomClass::Bin && Nth(r, 3)->atomClass == MathAtomClass::Rel,
          "+ is Bin and = is Rel; spaces are ignored");
    Check(Nth(r, 0)->fontStyle.shape == MathFontShape::Auto && !Nth(r, 0)->fontStyle.bold, "letters carry the default font style");

    r = ParseOne("\\left( x \\middle| y \\right]");
    s = Nth(r, 0);
    Check(s && s->kind == MathAtomKind::Fence && s->leftDelim == '(' && s->rightDelim == ']' && s->middleIndices.size() == 1,
          "\\left \\middle \\right make a Fence with one middle");

    r = ParseOne("\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}");
    s = Nth(r, 0);
    Check(s && s->kind == MathAtomKind::Array && s->array && s->array->rows.size() == 2 && s->array->rows[1].cells.size() == 2 &&
          s->array->leftDelim == '(' && s->array->columns.size() == 2, "pmatrix is a 2x2 array with parentheses");

    r = ParseOne("\\begin{array}{|c|l||r|} \\hline 1 & 2 & 3 \\\\ \\hline \\end{array}");
    s = Nth(r, 0);
    Check(s && s->array && s->array->columns.size() == 3 && s->array->columns[0].linesBefore == 1 &&
          s->array->columns[2].linesBefore == 2 && s->array->columns[2].linesAfter == 1 &&
          s->array->columns[1].align == MathColumnAlign::Left && s->array->rows.size() == 1 &&
          s->array->rows[0].hlinesBefore == 1 && s->array->hlinesAfter == 1,
          "array column spec: alignments, single and double rules, trailing \\hline");

    r = ParseOne("\\newcommand{\\R}{\\mathbb{R}} x \\in \\R");
    s = Nth(r, 2);
    Check(s && s->kind == MathAtomKind::Char && s->codepoint == 'R' && s->fontStyle.family == MathFontFamily::DoubleStruck,
          "\\newcommand expands; \\mathbb sets the double-struck family");
    r = ParseOne("\\newcommand{\\pow}[2][2]{#2^{#1}} \\pow{x} \\pow[3]{y}");
    Check(r && r->body.size() == 2 && Nth(r, 0)->kind == MathAtomKind::Scripts && Nth(r, 1)->kind == MathAtomKind::Scripts &&
          Nth(r, 1)->superscript.size() == 1 && Nth(r, 1)->superscript[0]->codepoint == '3',
          "optional macro arguments with a default");

    r = ParseOne("\\foo{x}", &diags);
    Check(r && Nth(r, 0) && Nth(r, 0)->kind == MathAtomKind::Error && Nth(r, 0)->text == "\\foo" &&
          diags.size() == 1 && diags[0].message.find("unknown command") != std::string::npos && diags[0].sourceStart == 0,
          "an unknown command is an Error atom with a diagnostic at its position");
    Check(r->body.size() == 2, "...and parsing continues after it");

    r = ParseOne("\\text{if } x \\ge 0");
    s = Nth(r, 0);
    Check(s && s->kind == MathAtomKind::Text && s->body.size() == 3 && s->body[0]->isTextChar &&
          s->body[2]->codepoint == ' ', "\\text keeps its characters and the trailing space");

    r = ParseOne("\\mathbf{x} \\boldsymbol{y} \\mathit{Z}");
    Check(Nth(r, 0)->fontStyle.bold && Nth(r, 0)->fontStyle.shape == MathFontShape::Upright &&
          Nth(r, 1)->fontStyle.bold && Nth(r, 1)->fontStyle.shape == MathFontShape::Auto &&
          !Nth(r, 2)->fontStyle.bold && Nth(r, 2)->fontStyle.shape == MathFontShape::Italic,
          "\\mathbf is bold upright, \\boldsymbol bold in the default shape, \\mathit italic");

    r = ParseOne("{\\color{red} x + y} z");
    s = Nth(r, 0);
    Check(s && s->kind == MathAtomKind::Row && s->body.size() == 1 && s->body[0]->kind == MathAtomKind::Color &&
          s->body[0]->color == 0xFFFF0000u && s->body[0]->body.size() == 3 && r->body.size() == 2,
          "\\color scopes the rest of its group");

    r = ParseOne("f'' + g'^2");
    Check(Nth(r, 0)->kind == MathAtomKind::Scripts && Nth(r, 0)->superscript.size() == 1 && Nth(r, 0)->superscript[0]->codepoint == 0x2033 &&
          Nth(r, 2)->superscript.size() == 2, "primes become superscripts and merge with an explicit ^");

    r = ParseOne("\\sqrt[3]{x}");
    Check(Nth(r, 0)->kind == MathAtomKind::Radical && Nth(r, 0)->degree.size() == 1 && Nth(r, 0)->body.size() == 1, "\\sqrt with a degree");

    r = ParseOne("\\operatorname{sgn}(x) \\sin x \\lim_{n\\to\\infty}");
    Check(r->body.size() == 7 && Nth(r, 0)->kind == MathAtomKind::Scripts && Nth(r, 0)->atomClass == MathAtomClass::Op &&
          Nth(r, 0)->limits == MathLimits::NoLimits && Nth(r, 4)->limits == MathLimits::NoLimits &&
          Nth(r, 6)->limits == MathLimits::Auto && Nth(r, 6)->hasSub,
          "named operators are Op atoms; \\sin has no limits, \\lim takes them in display style");

    r = ParseOne("\\begin{foo} x \\end{foo}", &diags);
    Check(!diags.empty() && diags[0].message.find("unknown environment") != std::string::npos, "an unknown environment is diagnosed");
    r = ParseOne("{x", &diags);
    Check(!diags.empty() && diags[0].message.find("missing '}'") != std::string::npos, "a missing brace is diagnosed");
    r = ParseOne("a \\\\ b");
    Check(r && r->body.size() == 1 && Nth(r, 0)->kind == MathAtomKind::Array && Nth(r, 0)->array->kind == MathArrayKind::Lines &&
          Nth(r, 0)->array->rows.size() == 2, "a top-level \\\\ makes a two-line formula");

    r = ParseOne("\\definecolor{c}{rgb}{0,0.5,1} \\textcolor{c}{x} \\textcolor{#00bde5}{y}");
    Check(Nth(r, 0)->kind == MathAtomKind::Color && Nth(r, 0)->color == 0xFF0080FFu && Nth(r, 1)->color == 0xFF00BDE5u,
          "\\definecolor rgb and #hex colours");

    r = ParseOne("\\not= \\not\\in \\not\\subset");
    Check(Nth(r, 0)->codepoint == 0x2260 && Nth(r, 1)->codepoint == 0x2209 && Nth(r, 2)->codepoint == 0x2284, "\\not composes negated relations");

    r = ParseOne("\\sideset{_a^b}{_c^d}{\\sum}");
    Check(Nth(r, 0)->kind == MathAtomKind::Sideset && Nth(r, 0)->preSub.size() == 1 && Nth(r, 0)->preSup.size() == 1 &&
          Nth(r, 0)->postSub.size() == 1 && Nth(r, 0)->postSup.size() == 1, "\\sideset splits pre- and post-scripts");

    r = ParseOne("\\int_0^1 f \\, dx \\quad \\hspace{2em}");
    Check(Nth(r, 0)->limits == MathLimits::Auto && Nth(r, 2)->kind == MathAtomKind::Space && Nth(r, 2)->spaceIsMu && Nth(r, 2)->spaceMu == 3.f &&
          Nth(r, 5)->spaceEm == 1.f && std::fabs(Nth(r, 6)->spaceEm - 2.f) < 1e-4f, "spacing commands: \\, \\quad \\hspace");
    float em = 0.f;
    Check(ParseMathLength("10pt", em) && std::fabs(em - 1.f) < 1e-4f && ParseMathLength("1.5em", em) && std::fabs(em - 1.5f) < 1e-4f &&
          ParseMathLength("18mu", em) && std::fabs(em - 1.f) < 1e-4f && !ParseMathLength("abc", em), "ParseMathLength units");

    // Alphabets
    MathFontStyle st;
    Check(MapMathAlphanumeric('x', st) == 0x1D465 && MapMathAlphanumeric('1', st) == '1' && MapMathAlphanumeric(0x03B1, st) == 0x1D6FC &&
          MapMathAlphanumeric(0x0393, st) == 0x0393 && MapMathAlphanumeric('h', st) == 0x210E,
          "TeX defaults: italic letters (h is the Planck hole), upright digits and capital Greek");
    st.bold = true; st.shape = MathFontShape::Upright;
    Check(MapMathAlphanumeric('A', st) == 0x1D400 && MapMathAlphanumeric('0', st) == 0x1D7CE, "bold upright alphabet");
    st = MathFontStyle{}; st.family = MathFontFamily::Script; st.shape = MathFontShape::Upright;
    Check(MapMathAlphanumeric('B', st) == 0x212C && MapMathAlphanumeric('A', st) == 0x1D49C, "script alphabet with letterlike holes");
    st.family = MathFontFamily::DoubleStruck;
    Check(MapMathAlphanumeric('R', st) == 0x211D && MapMathAlphanumeric('a', st) == 0x1D552, "double-struck alphabet");
    MathColor col;
    Check(LookupNamedColor("Tan", col) && col == 0xFFDA9D76u && LookupNamedColor("blue", col) && !LookupNamedColor("nosuchcolour", col), "named colours");
}

// ----- layout -----

void TestLayout(const UltraCanvasMathEngine& engine) {
    std::cout << "\nLayout against the font's numbers\n";
    const UltraCanvasMathFont& font = engine.GetFont();
    const float size = 20.f;
    const float upem = static_cast<float>(font.GetUnitsPerEm());
    auto C = [&](MathConstant c) { return font.GetConstant(c) * size / upem; };
    MathTypesetOptions opt; opt.fontSize = size;
    auto typeset = [&](const std::string& tex) { return engine.Typeset(tex, opt); };

    // A single glyph
    {
        MathTypesetResult r = typeset("x");
        const uint32_t gid = font.GetGlyphIndex(0x1D465);
        MathGlyphMetrics m; font.GetGlyphMetrics(gid, m);
        const float ic = font.GetItalicsCorrection(gid) * size / upem;
        Check(r.root && r.diagnostics.empty(), "'x' typesets");
        Check(std::fabs(r.width - (m.advance * size / upem + ic)) < 0.01f, "width = advance of math italic x + its italic correction");
        Check(std::fabs(r.height - m.Height() * size / upem) < 0.01f && std::fabs(r.depth - m.Depth() * size / upem) < 0.01f,
              "height/depth = the glyph's bounding box");
        Check(CountBoxes(r.root.get(), MathBoxType::Glyph) == 1, "one glyph box");
    }
    // Spacing
    {
        MathTypesetResult ab = typeset("a=b"), a = typeset("a"), eq = typeset("="), b = typeset("b");
        const float thick = 2.f * 5.f * size / 18.f;   // two \thickmuskip
        Check(std::fabs(ab.width - (a.width + eq.width + b.width + thick)) < 0.05f, "a=b: thick space on both sides of a relation");
        MathTypesetResult plus = typeset("a+b"), p = typeset("+");
        const float med = 2.f * 4.f * size / 18.f;
        Check(std::fabs(plus.width - (a.width + p.width + b.width + med)) < 0.05f, "a+b: medium space around a binary operator");
        MathTypesetResult unary = typeset("-b");
        MathTypesetResult minus = typeset("-");
        Check(std::fabs(unary.width - (minus.width + b.width)) < 0.05f, "-b: a leading minus is Ord, no space");
        MathTypesetResult sub = typeset("x^{a+b}");
        MathTypesetResult subPlain = typeset("x^{ab}");
        Check(std::fabs(sub.width - subPlain.width - typeset("+").width * 0.7f) < 0.3f, "no binary spacing in script style");
        MathTypesetResult text = typeset("\\text{a=b}");
        MathTypesetResult atext = typeset("\\text{a}");
        Check(text.width < ab.width - thick * 0.5f && text.width > atext.width * 2.f, "\\text{a=b}: no math spacing in a text run");
    }
    // Scripts
    {
        MathTypesetResult r = typeset("x^2");
        float gx, gy, gs;
        Check(FindGlyphPosition(r.root.get(), font.GetGlyphIndex('2'), 0, 0, gx, gy, gs), "x^2: the 2 is a glyph");
        Check(-gy >= C(MathConstant::SuperscriptShiftUp) - 0.01f, "superscript raised at least SuperscriptShiftUp (" + Fmt(-gy) + " >= " + Fmt(C(MathConstant::SuperscriptShiftUp)) + ")");
        Check(std::fabs(gs - size * font.GetConstant(MathConstant::ScriptPercentScaleDown) / 100.f) < 0.01f, "superscript at ScriptPercentScaleDown size");
        MathTypesetResult s = typeset("x_i");
        FindGlyphPosition(s.root.get(), font.GetGlyphIndex(0x1D456), 0, 0, gx, gy, gs);
        Check(gy >= C(MathConstant::SubscriptShiftDown) - 0.01f, "subscript lowered at least SubscriptShiftDown");
        MathTypesetResult both = typeset("x_i^2");
        float sx, sy, ss, tx, ty, ts;
        FindGlyphPosition(both.root.get(), font.GetGlyphIndex('2'), 0, 0, tx, ty, ts);
        FindGlyphPosition(both.root.get(), font.GetGlyphIndex(0x1D456), 0, 0, sx, sy, ss);
        MathGlyphMetrics m2, mi;
        font.GetGlyphMetrics(font.GetGlyphIndex('2'), m2); font.GetGlyphMetrics(font.GetGlyphIndex(0x1D456), mi);
        const float gap = (-ty - m2.Depth() * ts / upem) - (mi.Height() * ss / upem - sy);
        Check(gap >= C(MathConstant::SubSuperscriptGapMin) - 0.05f, "sub/superscript gap at least SubSuperscriptGapMin (" + Fmt(gap) + ")");
        MathTypesetResult nested = typeset("x^{y^z}");
        float zx, zy, zs;
        FindGlyphPosition(nested.root.get(), font.GetGlyphIndex(0x1D467), 0, 0, zx, zy, zs);
        Check(std::fabs(zs - size * font.GetConstant(MathConstant::ScriptScriptPercentScaleDown) / 100.f) < 0.01f, "a nested superscript is script-script size");
    }
    // Fractions
    {
        MathTypesetResult r = typeset("\\frac{a}{b}");
        const MathBox* rule = FindBox(r.root.get(), MathBoxType::Rule);
        Check(rule != nullptr, "\\frac has a rule");
        // Find the rule's absolute baseline offset
        float rx = 0, ry = 0;
        std::function<bool(const MathBox*, float, float)> find = [&](const MathBox* b, float x, float y) {
            if (b == rule) { rx = x; ry = y; return true; }
            for (const auto& c : b->children) if (find(c.box.get(), x + c.dx, y + c.dy)) return true;
            return false;
        };
        find(r.root.get(), 0, 0);
        Check(std::fabs(-ry - C(MathConstant::AxisHeight)) < 0.05f, "the fraction rule is centred on the axis");
        Check(std::fabs(rule->height + rule->depth - C(MathConstant::FractionRuleThickness)) < 0.01f, "...with FractionRuleThickness");
        MathTypesetResult d = typeset("\\dfrac{a}{b}"), t = typeset("\\tfrac{a}{b}");
        Check(d.TotalHeight() > t.TotalHeight(), "\\dfrac is taller than \\tfrac");
        MathTypesetResult bin = typeset("\\binom{n}{k}");
        Check(CountBoxes(bin.root.get(), MathBoxType::Rule) == 0 && bin.width > d.width, "\\binom has no rule and parentheses");
    }
    // Radicals and delimiters
    {
        MathTypesetResult x = typeset("x"), sq = typeset("\\sqrt{x}");
        Check(sq.width > x.width && sq.height > x.height && CountBoxes(sq.root.get(), MathBoxType::Rule) == 1, "\\sqrt adds the sign and the overbar");
        MathTypesetResult big = typeset("\\sqrt{\\frac{a}{b}}");
        Check(big.TotalHeight() >= typeset("\\frac{a}{b}").TotalHeight() + C(MathConstant::RadicalRuleThickness), "the radical grows with its body");
        MathTypesetResult fence = typeset("\\left( \\frac{a}{b} \\right)");
        MathTypesetResult paren = typeset("(");
        const MathBox* leftParen = FindBox(fence.root.get(), MathBoxType::Glyph);
        Check(leftParen && leftParen->TotalHeight() > paren.TotalHeight() * 1.5f &&
              leftParen->TotalHeight() >= fence.TotalHeight() * 0.85f, "\\left( grows to the body (TeX's 90% rule)");
        MathTypesetResult tall = typeset("\\left( \\rule{0.1em}{4em} \\right)");
        Check(CountBoxes(tall.root.get(), MathBoxType::Glyph) >= 6 && tall.TotalHeight() > 20.f * 6.f,
              "a delimiter taller than every variant is assembled from parts");
        MathTypesetResult bigs = typeset("\\bigl( \\Bigl( \\biggl( \\Biggl(");
        std::vector<float> heights;
        for (const auto& c : bigs.root->children) if (c.box->type == MathBoxType::List || c.box->type == MathBoxType::Glyph) heights.push_back(c.box->TotalHeight());
        Check(heights.size() == 4 && heights[0] < heights[1] && heights[1] < heights[2] && heights[2] < heights[3], "\\big < \\Big < \\bigg < \\Bigg");
    }
    // Operators
    {
        MathTypesetOptions textOpt = opt; textOpt.style = MathStyle::Text();
        MathTypesetResult disp = typeset("\\sum"), text = engine.Typeset("\\sum", textOpt);
        Check(disp.TotalHeight() > text.TotalHeight() * 1.2f, "\\sum is larger in display style");
        MathTypesetResult lim = typeset("\\sum_{n=1}^{N}"), nolim = engine.Typeset("\\sum_{n=1}^{N}", textOpt);
        Check(lim.width < nolim.width && lim.TotalHeight() > nolim.TotalHeight(), "limits stack above and below in display style, beside in text style");
        MathTypesetResult integral = typeset("\\int_0^1");
        Check(integral.width > typeset("\\int").width + 2.f, "\\int keeps side scripts in display style");
        MathTypesetResult forced = typeset("\\int\\limits_0^1");
        Check(forced.width < integral.width, "\\limits forces stacked limits");
    }
    // Arrays and colour
    {
        MathTypesetResult m = typeset("\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}");
        MathTypesetResult a = typeset("a");
        Check(m.TotalHeight() > 2.f * a.TotalHeight() && m.width > 2.f * a.width, "a 2x2 matrix is at least two rows and columns");
        Check(std::fabs((m.height - m.depth) / 2.f - C(MathConstant::AxisHeight)) < 0.05f, "the matrix is centred on the axis");
        MathTypesetResult c = typeset("\\textcolor{red}{x}");
        const MathBox* cb = FindBox(c.root.get(), MathBoxType::Color);
        Check(cb && cb->color == 0xFFFF0000u, "\\textcolor produces a Color box");
        MathTypesetResult f = typeset("\\fbox{x}");
        Check(FindBox(f.root.get(), MathBoxType::Frame) != nullptr && f.width > typeset("x").width + 2.f, "\\fbox frames with padding");
        MathTypesetResult err = typeset("x + \\nosuch");
        Check(err.root && err.HasErrors() && FindBox(err.root.get(), MathBoxType::Error) != nullptr && err.width > typeset("x +").width,
              "an unknown command still renders (as an error box) and reports");
    }
    // Text runs and fallback
    {
        MathTypesetResult t = typeset("\\text{if}");
        Check(CountBoxes(t.root.get(), MathBoxType::Glyph) == 2 && CountBoxes(t.root.get(), MathBoxType::Text) == 0,
              "\\text uses the math font's upright glyphs");
        MathTypesetResult cyr = typeset("\\text{привет}");
        Check(cyr.root && cyr.width > 0.f, "characters the font lacks still take space (placeholders without a host)");
    }
    // Line breaking and empty input
    {
        MathTypesetOptions narrow = opt; narrow.maxWidth = 120.f;
        MathTypesetResult one = typeset("a+b+c+d+e+f+g+h+i+j+k+l+m+n");
        MathTypesetResult broken = engine.Typeset("a+b+c+d+e+f+g+h+i+j+k+l+m+n", narrow);
        Check(broken.width <= 120.f + 1.f && broken.TotalHeight() > one.TotalHeight() * 1.8f, "maxWidth breaks a long row into lines");
        MathTypesetResult empty = typeset("");
        Check(empty.root && empty.width == 0.f && empty.diagnostics.empty(), "empty input is an empty box");
    }
}

// ----- MicroTeX oracle -----

class StubFont : public microtex::Font {
public:
    bool operator==(const microtex::Font&) const override { return true; }
};

class StubTextLayout : public microtex::TextLayout {
public:
    StubTextLayout(std::string s, float size) : text_(std::move(s)), size_(size) {}
    void getBounds(microtex::Rect& b) override {
        size_t n = 0;
        for (size_t p = 0; p < text_.size();) { DecodeUtf8(text_, p); ++n; }
        b.x = 0; b.w = 0.5f * size_ * static_cast<float>(n); b.y = -0.7f * size_; b.h = 0.9f * size_;
    }
    void draw(microtex::Graphics2D&, float, float) override {}
private:
    std::string text_;
    float size_;
};

class StubFactory : public microtex::PlatformFactory {
public:
    microtex::sptr<microtex::Font> createFont(const std::string&) override { return std::make_shared<StubFont>(); }
    microtex::sptr<microtex::TextLayout> createTextLayout(const std::string& src, microtex::FontStyle, float size) override {
        return std::make_shared<StubTextLayout>(src, size);
    }
};

std::string ExtractBody(std::string s) {
    size_t b = s.find("\\begin{document}");
    if (b != std::string::npos) { s = s.substr(b + 16); size_t e = s.find("\\end{document}"); if (e != std::string::npos) s = s.substr(0, e); }
    std::string o; bool comment = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) { o += s[i]; o += s[i + 1]; ++i; continue; }
        if (s[i] == '%') comment = true;
        if (s[i] == '\n') comment = false;
        if (!comment) o += s[i];
    }
    auto strip = [&](const std::string& a, const std::string& z) {
        size_t p = o.find(a);
        if (p != std::string::npos) { size_t q = o.rfind(z); if (q != std::string::npos && q > p) o = o.substr(p + a.size(), q - p - a.size()); }
    };
    strip("\\[", "\\]"); strip("$$", "$$");
    return o;
}

const char* kOracleFormulas[] = {
    "x", "x+y", "a=b", "x^2", "x_i", "x_i^2", "\\frac{a}{b}", "\\frac{a+b}{c+d}", "\\sqrt{x}", "\\sqrt{x^2+y^2}",
    "\\sum_{n=1}^{\\infty} \\frac{1}{n^2}", "\\int_0^1 f(x)\\,dx", "\\left( \\frac{a}{b} \\right)", "\\alpha + \\beta = \\gamma",
    "e^{i\\pi} + 1 = 0", "\\binom{n}{k}", "\\hat{x} + \\vec{v} + \\bar{y}", "\\overline{AB} + \\underline{CD}",
    "\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}", "\\lim_{x \\to 0} \\frac{\\sin x}{x} = 1", "\\prod_{i=1}^{n} a_i",
    "\\mathbf{v} \\cdot \\mathbf{w}", "\\mathbb{R}^n \\subset \\mathbb{C}^n", "f'(x) = \\lim_{h\\to 0} \\frac{f(x+h)-f(x)}{h}",
    "\\nabla \\times \\mathbf{E} = -\\frac{\\partial \\mathbf{B}}{\\partial t}", "x = \\frac{-b \\pm \\sqrt{b^2-4ac}}{2a}",
    "\\sqrt[3]{x}", "\\overbrace{a+b}^{n} + \\underbrace{c+d}_{m}", "\\xrightarrow{f}", "\\left\\{ x \\middle| x > 0 \\right\\}",
    "A \\Longrightarrow B \\iff C", "\\cfrac{1}{1+\\cfrac{1}{1+x}}", "\\Bigl( \\bigl( x \\bigr) \\Bigr)", "\\overset{\\circ}{\\to}",
    "\\dot{x} \\ddot{x} \\widetilde{abc} \\widehat{abc}", "\\displaystyle \\sum_{k} x_k", "a \\bmod b", "\\forall \\varepsilon > 0\\ \\exists \\delta > 0",
    "\\begin{cases} 1 & x>0 \\\\ 0 & x \\le 0 \\end{cases}", "\\begin{aligned} a &= b \\\\ c &= d \\end{aligned}",
};

void TestOracle(const UltraCanvasMathEngine& engine, const fs::path& clm, const fs::path& otf) {
    std::cout << "\nMicroTeX oracle: widths and heights of the same formulas\n";
    microtex::PlatformFactory::registerFactory("stub", std::make_unique<StubFactory>());
    microtex::PlatformFactory::activate("stub");
    try {
        microtex::MicroTeX::init(microtex::FontSrcFile(clm.string(), otf.string()));
    } catch (const std::exception& e) {
        Check(false, std::string("MicroTeX initialises: ") + e.what());
        return;
    }
    Check(microtex::MicroTeX::isInited(), "MicroTeX initialised with the same Latin Modern Math");

    struct Case { std::string name, tex; bool textHeavy; };
    std::vector<Case> cases;
    for (const char* f : kOracleFormulas) cases.push_back({f, f, false});
    const fs::path dir = MediaFile("LaTex");
    if (fs::is_directory(dir)) {
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir)) if (e.path().extension() == ".tex") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const auto& p : files) {
            std::ifstream in(p); std::stringstream ss; ss << in.rdbuf();
            const std::string body = ExtractBody(ss.str());
            const bool textHeavy = body.find("\\text") != std::string::npos || body.find("\\mbox") != std::string::npos ||
                                   body.find("\\rotatebox") != std::string::npos || body.find("\\longdiv") != std::string::npos;
            cases.push_back({p.filename().string(), body, textHeavy});
        }
    }

    const float size = 24.f;
    MathTypesetOptions opt; opt.fontSize = size;
    int compared = 0, withinTight = 0, withinLoose = 0, ours0 = 0, theirs0 = 0;
    double sumAbsW = 0, sumAbsH = 0;
    std::cout << "         " << std::left;
    std::printf("         %-32s %9s %9s %7s   %9s %9s %7s\n", "formula", "ourW", "mtW", "dW%", "ourH", "mtH", "dH%");
    for (const auto& c : cases) {
        MathTypesetResult ours = engine.Typeset(c.tex, opt);
        std::unique_ptr<microtex::Render> theirs;
        try {
            theirs.reset(microtex::MicroTeX::parse(c.tex, 0, size, size / 3.f, 0xff000000, true,
                                                   microtex::OverrideTeXStyle{true, microtex::TexStyle::display}));
        } catch (const std::exception& e) {
            std::printf("         %-32s MicroTeX refused: %s\n", c.name.c_str(), e.what());
            continue;
        } catch (...) { continue; }
        if (!ours.root || !theirs) { if (!ours.root) ++ours0; if (!theirs) ++theirs0; continue; }
        const double ow = ours.width, oh = ours.TotalHeight();
        const double mw = theirs->getWidth(), mh = theirs->getHeight();
        if (mw <= 0 || mh <= 0) continue;
        const double dw = (ow - mw) / mw * 100.0, dh = (oh - mh) / mh * 100.0;
        ++compared;
        sumAbsW += std::fabs(dw); sumAbsH += std::fabs(dh);
        // Small formulas are judged in absolute terms too: TeX's
        // \nulldelimiterspace around a fraction or a fixed-size \Big are a
        // large percentage of a tiny box but a fraction of an em.
        const double absTol = 0.75 * size;
        const bool tight = std::fabs(dw) <= 15.0 && std::fabs(dh) <= 15.0;
        const bool loose = (std::fabs(dw) <= 35.0 || std::fabs(ow - mw) <= absTol) &&
                           (std::fabs(dh) <= 35.0 || std::fabs(oh - mh) <= absTol);
        if (tight) ++withinTight;
        if (loose || c.textHeavy) ++withinLoose;
        std::printf("         %-32s %9.1f %9.1f %+6.1f%%   %9.1f %9.1f %+6.1f%%%s\n", c.name.substr(0, 32).c_str(), ow, mw, dw, oh, mh, dh,
                    tight ? "" : (loose ? "  (loose)" : "  <-- far"));
    }
    std::cout << std::right;
    Check(compared >= 50, "compared " + std::to_string(compared) + " formulas");
    Check(ours0 == 0, "the native engine typeset every formula MicroTeX did");
    const double meanW = compared ? sumAbsW / compared : 0, meanH = compared ? sumAbsH / compared : 0;
    Check(meanW < 8.0 && meanH < 12.0, "mean absolute width/height deviation " + Fmt(meanW) + "% / " + Fmt(meanH) + "%");
    Check(withinTight >= compared * 3 / 4, std::to_string(withinTight) + " of " + std::to_string(compared) + " within 15% on both axes");
    Check(withinLoose == compared, "every formula within 35% or 0.75em on both axes (text-heavy ones excepted)");
}

} // namespace

int main() {
    std::cout << "UltraCanvasMathEngine test\n";
    TestParser();

    const fs::path otf = MediaFile("microtex/latinmodern-math.otf");
    const fs::path clm = MediaFile("microtex/latinmodern-math.clm2");
    UltraCanvasMathEngine engine;
    std::cout << "\nEngine\n";
    Check(engine.LoadFont(otf.string()), "loads latinmodern-math.otf: " + engine.GetLastError());
    Check(engine.IsReady(), "IsReady");
    {
        UltraCanvasMathEngine none;
        MathTypesetResult r = none.Typeset("x", MathTypesetOptions{});
        Check(!r.root && r.HasErrors(), "an engine without a font reports instead of crashing");
        Check(!none.LoadFont(MediaFile("fonts/Ubuntu-R.ttf").string()) && none.GetLastError().find("MATH") != std::string::npos,
              "a font without a MATH table is refused: " + none.GetLastError());
    }
    if (engine.IsReady()) {
        TestLayout(engine);
        if (fs::exists(clm)) TestOracle(engine, clm, otf);
        else std::cout << "  (no .clm2 - oracle skipped)\n";
    }

    std::cout << "\n" << (g_failures == 0 ? "ALL PASSED" : std::to_string(g_failures) + " FAILURE(S)") << "\n";
    return g_failures == 0 ? 0 : 1;
}
