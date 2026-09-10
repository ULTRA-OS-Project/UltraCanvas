// include/Plugins/LaTeX/UltraCanvasMathModel.h
// The data model of the native math engine: the styles a formula is set in,
// the atom tree the parser produces from LaTeX source, and the box tree the
// layout produces from atoms. Phase 1 of UltraCanvasLaTeXEngineProposal.md.
//
// Atoms follow The TeXbook, Appendix G: every atom has a class (Ord, Op, Bin,
// Rel, Open, Close, Punct, Inner) that drives inter-atom spacing, and a kind
// that says what it is (a character, a fraction, a radical, scripts, ...).
// Boxes are the typeset result: nested lists of glyphs, rules and kerns with
// TeX dimensions (width, height above the baseline, depth below), positioned
// relative to their parent's origin (left edge, baseline; y grows down).
//
// This header has no dependency on the UI framework so the parser and layout
// can be unit-tested and run on any thread; only the renderer touches
// IRenderContext.
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

class UltraCanvasMathFont;

// =============================================================================
// Styles
// =============================================================================

// TeX's four styles; each also has a cramped variant (used under radicals,
// in denominators and subscripts) that lowers superscripts.
enum class MathStyleKind : uint8_t { Display = 0, Text, Script, ScriptScript };

struct MathStyle {
    MathStyleKind kind = MathStyleKind::Display;
    bool cramped = false;

    static MathStyle Display() { return {MathStyleKind::Display, false}; }
    static MathStyle Text()    { return {MathStyleKind::Text, false}; }

    MathStyle Cramped() const { return {kind, true}; }
    // Style of a superscript (Appendix G rule 18a).
    MathStyle Sup() const {
        switch (kind) {
            case MathStyleKind::Display:
            case MathStyleKind::Text:   return {MathStyleKind::Script, cramped};
            default:                    return {MathStyleKind::ScriptScript, cramped};
        }
    }
    // Style of a subscript: like Sup() but always cramped.
    MathStyle Sub() const { return Sup().Cramped(); }
    // Numerator / denominator styles (rule 15).
    MathStyle Numerator() const {
        return kind == MathStyleKind::Display ? MathStyle{MathStyleKind::Text, cramped} : Sup();
    }
    MathStyle Denominator() const { return Numerator().Cramped(); }
    bool IsDisplay() const { return kind == MathStyleKind::Display; }
    bool IsScript()  const { return kind >= MathStyleKind::Script; }
    bool operator==(const MathStyle& o) const { return kind == o.kind && cramped == o.cramped; }
};

// Which alphabet a letter is taken from (unicode-math mathematical
// alphanumerics), resolved at parse time from \mathbf, \mathcal, ...
enum class MathFontFamily : uint8_t { Serif = 0, Sans, Mono, Script, Fraktur, DoubleStruck };
enum class MathFontShape  : uint8_t { Auto = 0, Upright, Italic };   // Auto = TeX default

struct MathFontStyle {
    MathFontFamily family = MathFontFamily::Serif;
    MathFontShape shape = MathFontShape::Auto;
    bool bold = false;
    bool operator==(const MathFontStyle& o) const {
        return family == o.family && shape == o.shape && bold == o.bold;
    }
};

// Packed 0xAARRGGBB, the same convention as the framework's Color::ToARGB.
using MathColor = uint32_t;
constexpr MathColor kMathColorBlack = 0xFF000000u;
constexpr MathColor kMathColorNone  = 0x00000000u;   // "no color" / inherit

// =============================================================================
// Atoms
// =============================================================================

enum class MathAtomClass : uint8_t { Ord = 0, Op, Bin, Rel, Open, Close, Punct, Inner, None };

enum class MathAtomKind : uint8_t {
    Char,        // one character: codepoint + font style
    Row,         // a list of atoms (a group); Inner when it came from \left..\right
    Space,       // explicit spacing (\, \quad \hspace ...)
    Fraction,    // \frac \genfrac \binom \over \atop \cfrac
    Radical,     // \sqrt with optional degree
    Scripts,     // nucleus with sub/superscript (also Op limits)
    Accent,      // \hat \vec \widetilde ... over or under a body
    OverUnder,   // \overset \underset \stackrel \overbrace \underbrace \xrightarrow \overline ...
    Fence,       // \left ... \middle ... \right
    BigDelim,    // \bigl( \Bigg| ...
    Style,       // \displaystyle ... (applies to the rest of the enclosing row)
    Color,       // \textcolor{c}{...} and \color{c} scopes
    Phantom,     // \phantom \hphantom \vphantom
    Lap,         // \rlap \llap \clap \mathrlap ...
    Box,         // \fbox \boxed \colorbox \fcolorbox \shadowbox \doublebox \ovalbox
    Cancel,      // \cancel \bcancel \xcancel
    Text,        // \text{...} run set in text mode
    Array,       // \begin{array} and the matrix / align / cases family
    Sideset,     // \sideset{pre}{post}{op}, \prescript
    Transform,   // \rotatebox \reflectbox \scalebox \resizebox
    Rule,        // \rule{w}{h}, strut-like boxes
    Not,         // \not applied to the next symbol
    LongDiv,     // \longdiv{dividend}{divisor}
    Smash,       // \smash
    Error        // an unknown command, rendered as red text
};

struct MathAtom;
using MathAtomPtr = std::shared_ptr<MathAtom>;
using MathList = std::vector<MathAtomPtr>;

enum class MathLimits : uint8_t { Auto = 0, Limits, NoLimits };
enum class MathAccentKind : uint8_t { Over = 0, Under };
enum class MathOverUnderKind : uint8_t {
    Overset, Underset,                 // small script-style material above/below
    Overbrace, Underbrace,             // stretchy brace, scripts become limits
    Overbracket, Underbracket, Overparen, Underparen,
    Overline, Underline,               // rules
    OverArrow, UnderArrow,             // stretchy arrows above/below the body
    XArrow                             // \xrightarrow[below]{above}: the arrow is the body, stretched to the scripts
};
enum class MathBoxKind : uint8_t { Frame, Boxed, ColorBox, FColorBox, Shadow, Double, Oval };
enum class MathCancelKind : uint8_t { Cancel, BCancel, XCancel };
enum class MathLapKind : uint8_t { Left, Right, Center };
enum class MathArrayKind : uint8_t {
    Array, Matrix, Align, Aligned, AlignAt, Gather, Multline, Cases, Split, Eqnarray, Lines
};
enum class MathColumnAlign : uint8_t { Left, Center, Right };

struct MathColumnSpec {
    MathColumnAlign align = MathColumnAlign::Center;
    int linesBefore = 0;               // count of '|' before the column
    int linesAfter = 0;                // '|' after the last column only
    bool customSepBefore = false;      // @{...} replaces the left \arraycolsep
    bool customSepAfter = false;       // @{...} after the last column
    MathList sepBefore, sepAfter;      // the @{...} content
    std::string prefixSource;          // >{...} inserted before every cell
};

struct MathCell {
    MathList content;
    int colSpan = 1;                   // \multicolumn
    bool hasOwnSpec = false;           // \multicolumn alignment
    MathColumnSpec spec;
    MathColor background = kMathColorNone;   // \cellcolor
    bool dotsFill = false;             // \hdotsfor: fill with \cdots
    bool intertext = false;            // \intertext: a text row spanning the table
};

struct MathRow {
    std::vector<MathCell> cells;
    int hlinesBefore = 0;              // \hline count before this row
    float extraSkip = 0.f;             // \\[len], in em of the base size
    MathColor background = kMathColorNone;   // \rowcolor
};

struct MathArrayData {
    MathArrayKind kind = MathArrayKind::Array;
    std::vector<MathColumnSpec> columns;
    std::vector<MathRow> rows;
    int hlinesAfter = 0;               // \hline after the last row
    uint32_t leftDelim = 0, rightDelim = 0;   // pmatrix etc. (0 = none)
    bool textStyleCells = true;        // matrices: cells in text style
    bool scriptStyleCells = false;     // smallmatrix, \substack: cells in script style
    bool displayStyleCells = false;    // dcases: cells in display style
    bool leftColSep = true;            // false: no \arraycolsep before the first column (cases)
    bool rightColSep = true;           // false: none after the last column
};

struct MathAtom {
    MathAtomKind kind = MathAtomKind::Row;
    MathAtomClass atomClass = MathAtomClass::Ord;
    int sourceStart = -1, sourceEnd = -1;      // byte span in the LaTeX source

    // LongDiv
    std::string divisor;
    // Char / BigDelim / Not
    char32_t codepoint = 0;
    MathFontStyle fontStyle;
    bool isTextChar = false;                   // set in \text: no math alphabet mapping
    int bigSize = 0;                           // BigDelim: 1..4 for \big .. \Bigg
    // Space (em of the current size unless mu is set) / Rule
    float spaceEm = 0.f;
    float spaceMu = 0.f;
    bool spaceIsMu = false;
    float ruleWidthEm = 0.f, ruleHeightEm = 0.f, ruleDepthEm = 0.f;
    // Row / Text / general body
    MathList body;
    std::string text;                          // Text, Error, LongDiv digits
    // Fraction
    MathList numerator, denominator;
    float ruleThicknessEm = -1.f;              // <0: font default; 0: \atop
    uint32_t fracLeftDelim = 0, fracRightDelim = 0;
    bool fracDisplayStyle = false;             // \dfrac \cfrac: force display style
    bool fracTextStyle = false;                // \tfrac
    // Radical
    MathList degree;
    // Scripts
    MathAtomPtr nucleus;
    MathList subscript, superscript;
    MathLimits limits = MathLimits::Auto;
    bool hasSub = false, hasSup = false;
    // Accent / OverUnder
    MathAccentKind accentKind = MathAccentKind::Over;
    bool accentStretchy = false;
    MathOverUnderKind overUnderKind = MathOverUnderKind::Overset;
    MathList over, under;                      // OverUnder material; XArrow: above/below scripts
    // Fence
    uint32_t leftDelim = 0, rightDelim = 0;    // 0 = null delimiter "."
    std::vector<size_t> middleIndices;         // positions in body of \middle delimiters
    // Style
    MathStyle style;
    bool hasStyle = false;
    float sizeScale = 0.f;                     // \small etc: >0 sets the scale for the rest of the row
    // Color / Box
    MathColor color = kMathColorNone;
    MathColor background = kMathColorNone;
    MathColor border = kMathColorNone;
    MathBoxKind boxKind = MathBoxKind::Frame;
    float cornerSize = 0.5f;                   // \cornersize for \ovalbox
    // Phantom / Smash
    bool phantomWidth = true, phantomHeight = true;
    // Lap
    MathLapKind lapKind = MathLapKind::Right;
    // Cancel
    MathCancelKind cancelKind = MathCancelKind::Cancel;
    // Array
    std::shared_ptr<MathArrayData> array;
    // Sideset
    MathList preSub, preSup, postSub, postSup;
    MathList preNucleus, postNucleus;          // non-script material given to \sideset
    // Transform
    float rotateDegrees = 0.f;
    float scaleX = 1.f, scaleY = 1.f;
    bool reflect = false;
    float resizeWidthEm = 0.f, resizeHeightEm = 0.f;   // \resizebox targets (0 = keep)
    // Op flags
    bool opIsLargeOperator = false;            // \sum-like: grows in display style
    bool opIsNamed = false;                    // \sin-like: upright text, no limits by default
    bool isCombiningAccentChar = false;

    static MathAtomPtr MakeChar(char32_t cp, MathAtomClass cls, const MathFontStyle& fs) {
        auto a = std::make_shared<MathAtom>();
        a->kind = MathAtomKind::Char;
        a->atomClass = cls;
        a->codepoint = cp;
        a->fontStyle = fs;
        return a;
    }
    static MathAtomPtr MakeRow(MathList list, MathAtomClass cls = MathAtomClass::Ord) {
        auto a = std::make_shared<MathAtom>();
        a->kind = MathAtomKind::Row;
        a->atomClass = cls;
        a->body = std::move(list);
        return a;
    }
    static MathAtomPtr Make(MathAtomKind kind, MathAtomClass cls = MathAtomClass::Ord) {
        auto a = std::make_shared<MathAtom>();
        a->kind = kind;
        a->atomClass = cls;
        return a;
    }
};

// =============================================================================
// Boxes
// =============================================================================

enum class MathBoxType : uint8_t {
    List,        // children at (dx, dy); the only container
    Glyph,       // one glyph of `font` at `fontSize`
    Rule,        // filled rectangle: width x (height + depth)
    Kern,        // empty space
    Text,        // a run drawn by the host's text fallback (characters the math font lacks)
    Color,       // children drawn in `color`
    Background,  // filled rectangle behind the children, then the children
    Frame,       // border(s) around the children (FrameStyle)
    Cancel,      // children plus diagonal strike lines
    Transform,   // children drawn through the 2x2 matrix (rotate / reflect / scale)
    Error        // red error text (message in `text`)
};

enum class MathFrameStyle : uint8_t { Single, Double, Shadow, Oval };

struct MathBox;
using MathBoxPtr = std::shared_ptr<MathBox>;

struct MathBoxChild {
    float dx = 0.f, dy = 0.f;                  // child origin relative to parent origin
    MathBoxPtr box;
};

struct MathBox {
    MathBoxType type = MathBoxType::List;
    float width = 0.f, height = 0.f, depth = 0.f;
    std::vector<MathBoxChild> children;
    int sourceStart = -1, sourceEnd = -1;

    // Glyph
    const UltraCanvasMathFont* font = nullptr;
    uint32_t glyph = 0;
    float fontSize = 0.f;                      // pixels per em
    float italicsCorrection = 0.f;             // of a glyph box, pixels
    float topAccentAttachment = 0.f;           // of a glyph box, pixels; <0 = none
    // Text / Error
    std::string text;
    MathFontStyle textStyle;
    // Color / Background / Frame / Cancel
    MathColor color = kMathColorNone;          // Color: foreground; Background: fill
    MathColor borderColor = kMathColorNone;
    float lineWidth = 0.f;                     // Frame border, Cancel stroke
    float cornerRadius = 0.f;
    float shadowOffset = 0.f;
    MathFrameStyle frameStyle = MathFrameStyle::Single;
    MathCancelKind cancelKind = MathCancelKind::Cancel;
    // Transform: [a b; c d] applied to child coordinates around the child origin
    float ma = 1.f, mb = 0.f, mc = 0.f, md = 1.f;
    float transformDx = 0.f, transformDy = 0.f;   // translation applied before drawing the children

    float TotalHeight() const { return height + depth; }

    static MathBoxPtr MakeList() { return std::make_shared<MathBox>(); }
    static MathBoxPtr MakeKern(float w) {
        auto b = std::make_shared<MathBox>();
        b->type = MathBoxType::Kern;
        b->width = w;
        return b;
    }
    static MathBoxPtr MakeRule(float w, float h, float d) {
        auto b = std::make_shared<MathBox>();
        b->type = MathBoxType::Rule;
        b->width = w; b->height = h; b->depth = d;
        return b;
    }
    void Add(MathBoxPtr child, float dx, float dy) { children.push_back({dx, dy, std::move(child)}); }
};

// Diagnostics collected while parsing or laying out.
struct MathDiagnostic {
    std::string message;
    int sourceStart = -1, sourceEnd = -1;
};

} // namespace UltraCanvas
