// Plugins/Documents/LaTeX/UltraCanvasLaTeXDocumentReader.cpp
// LaTeX document-subset importer → UCRichDocument. A direct-source scanner
// with macro splicing (the same technique as the math parser): commands and
// environments of the article subset are mapped onto blocks and runs, user
// macros are expanded in place, math is kept as LaTeX source for the engine.
// See the header for the scope statement.
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "Plugins/Documents/LaTeX/UltraCanvasLaTeXDocumentReader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace UltraCanvas {

namespace {

// ===== SMALL STRING HELPERS =====

bool IsLetter(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
bool IsSpaceChar(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string TrimCopy(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && IsSpaceChar(s[b])) ++b;
    while (e > b && IsSpaceChar(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::string LowerCopy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void AppendUtf8Codepoint(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string Utf8(uint32_t cp) { std::string s; AppendUtf8Codepoint(s, cp); return s; }

// Reads one UTF-8 code point starting at s[i]; advances i.
uint32_t DecodeUtf8At(const std::string& s, size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp; int extra;
    if (c < 0x80) { cp = c; extra = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else { cp = c & 0x07; extra = 3; }
    ++i;
    for (int k = 0; k < extra && i < s.size(); ++k, ++i) {
        cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
    }
    return cp;
}

// ===== ACCENTS =====
// \'e → é: the accent command's combining mark, then the precomposed letter
// where Latin-1 / Latin Extended-A has one (text renderers draw a combining
// sequence just as well, but searches and tests want the composed form).

uint32_t CombiningMarkFor(const std::string& accent) {
    static const std::unordered_map<std::string, uint32_t> marks = {
        {"`", 0x0300}, {"'", 0x0301}, {"^", 0x0302}, {"~", 0x0303}, {"=", 0x0304},
        {"u", 0x0306}, {".", 0x0307}, {"\"", 0x0308}, {"r", 0x030A}, {"H", 0x030B},
        {"v", 0x030C}, {"c", 0x0327}, {"k", 0x0328}, {"d", 0x0323}, {"b", 0x0331},
    };
    auto it = marks.find(accent);
    return it == marks.end() ? 0 : it->second;
}

uint32_t Precomposed(uint32_t base, uint32_t mark) {
    struct Entry { uint32_t base, mark, composed; };
    // Latin-1 Supplement.
    static const Entry table[] = {
        {'A',0x300,0xC0},{'A',0x301,0xC1},{'A',0x302,0xC2},{'A',0x303,0xC3},{'A',0x308,0xC4},{'A',0x30A,0xC5},
        {'C',0x327,0xC7},{'E',0x300,0xC8},{'E',0x301,0xC9},{'E',0x302,0xCA},{'E',0x308,0xCB},
        {'I',0x300,0xCC},{'I',0x301,0xCD},{'I',0x302,0xCE},{'I',0x308,0xCF},{'N',0x303,0xD1},
        {'O',0x300,0xD2},{'O',0x301,0xD3},{'O',0x302,0xD4},{'O',0x303,0xD5},{'O',0x308,0xD6},
        {'U',0x300,0xD9},{'U',0x301,0xDA},{'U',0x302,0xDB},{'U',0x308,0xDC},{'Y',0x301,0xDD},
        {'a',0x300,0xE0},{'a',0x301,0xE1},{'a',0x302,0xE2},{'a',0x303,0xE3},{'a',0x308,0xE4},{'a',0x30A,0xE5},
        {'c',0x327,0xE7},{'e',0x300,0xE8},{'e',0x301,0xE9},{'e',0x302,0xEA},{'e',0x308,0xEB},
        {'i',0x300,0xEC},{'i',0x301,0xED},{'i',0x302,0xEE},{'i',0x308,0xEF},{'n',0x303,0xF1},
        {'o',0x300,0xF2},{'o',0x301,0xF3},{'o',0x302,0xF4},{'o',0x303,0xF5},{'o',0x308,0xF6},
        {'u',0x300,0xF9},{'u',0x301,0xFA},{'u',0x302,0xFB},{'u',0x308,0xFC},{'y',0x301,0xFD},{'y',0x308,0xFF},
        // Latin Extended-A.
        {'A',0x304,0x100},{'a',0x304,0x101},{'A',0x306,0x102},{'a',0x306,0x103},{'A',0x328,0x104},{'a',0x328,0x105},
        {'C',0x301,0x106},{'c',0x301,0x107},{'C',0x302,0x108},{'c',0x302,0x109},{'C',0x307,0x10A},{'c',0x307,0x10B},
        {'C',0x30C,0x10C},{'c',0x30C,0x10D},{'D',0x30C,0x10E},{'d',0x30C,0x10F},
        {'E',0x304,0x112},{'e',0x304,0x113},{'E',0x306,0x114},{'e',0x306,0x115},{'E',0x307,0x116},{'e',0x307,0x117},
        {'E',0x328,0x118},{'e',0x328,0x119},{'E',0x30C,0x11A},{'e',0x30C,0x11B},
        {'G',0x302,0x11C},{'g',0x302,0x11D},{'G',0x306,0x11E},{'g',0x306,0x11F},{'G',0x307,0x120},{'g',0x307,0x121},
        {'G',0x327,0x122},{'g',0x327,0x123},{'H',0x302,0x124},{'h',0x302,0x125},
        {'I',0x303,0x128},{'i',0x303,0x129},{'I',0x304,0x12A},{'i',0x304,0x12B},{'I',0x306,0x12C},{'i',0x306,0x12D},
        {'I',0x328,0x12E},{'i',0x328,0x12F},{'I',0x307,0x130},{'J',0x302,0x134},{'j',0x302,0x135},
        {'K',0x327,0x136},{'k',0x327,0x137},{'L',0x301,0x139},{'l',0x301,0x13A},{'L',0x327,0x13B},{'l',0x327,0x13C},
        {'L',0x30C,0x13D},{'l',0x30C,0x13E},{'N',0x301,0x143},{'n',0x301,0x144},{'N',0x327,0x145},{'n',0x327,0x146},
        {'N',0x30C,0x147},{'n',0x30C,0x148},{'O',0x304,0x14C},{'o',0x304,0x14D},{'O',0x306,0x14E},{'o',0x306,0x14F},
        {'O',0x30B,0x150},{'o',0x30B,0x151},{'R',0x301,0x154},{'r',0x301,0x155},{'R',0x327,0x156},{'r',0x327,0x157},
        {'R',0x30C,0x158},{'r',0x30C,0x159},{'S',0x301,0x15A},{'s',0x301,0x15B},{'S',0x302,0x15C},{'s',0x302,0x15D},
        {'S',0x327,0x15E},{'s',0x327,0x15F},{'S',0x30C,0x160},{'s',0x30C,0x161},{'T',0x327,0x162},{'t',0x327,0x163},
        {'T',0x30C,0x164},{'t',0x30C,0x165},{'U',0x303,0x168},{'u',0x303,0x169},{'U',0x304,0x16A},{'u',0x304,0x16B},
        {'U',0x306,0x16C},{'u',0x306,0x16D},{'U',0x30A,0x16E},{'u',0x30A,0x16F},{'U',0x30B,0x170},{'u',0x30B,0x171},
        {'U',0x328,0x172},{'u',0x328,0x173},{'W',0x302,0x174},{'w',0x302,0x175},{'Y',0x302,0x176},{'y',0x302,0x177},
        {'Y',0x308,0x178},{'Z',0x301,0x179},{'z',0x301,0x17A},{'Z',0x307,0x17B},{'z',0x307,0x17C},{'Z',0x30C,0x17D},{'z',0x30C,0x17E},
    };
    for (const Entry& e : table) {
        if (e.base == base && e.mark == mark) return e.composed;
    }
    return 0;
}

std::string ApplyAccent(const std::string& accent, const std::string& baseText) {
    const uint32_t mark = CombiningMarkFor(accent);
    if (mark == 0 || baseText.empty()) return baseText;
    size_t i = 0;
    uint32_t base = DecodeUtf8At(baseText, i);
    // \i and \j (dotless) take the accent on the plain letter.
    if (base == 0x131) base = 'i';
    if (base == 0x237) base = 'j';
    std::string out;
    if (uint32_t composed = Precomposed(base, mark)) {
        AppendUtf8Codepoint(out, composed);
    } else {
        AppendUtf8Codepoint(out, base);
        AppendUtf8Codepoint(out, mark);
    }
    out += baseText.substr(i);
    return out;
}

// ===== SYMBOL COMMANDS (text mode) =====

const char* TextSymbol(const std::string& name) {
    static const std::unordered_map<std::string, const char*> symbols = {
        {"ss", "ß"}, {"SS", "SS"}, {"ae", "æ"}, {"AE", "Æ"}, {"oe", "œ"}, {"OE", "Œ"},
        {"o", "ø"}, {"O", "Ø"}, {"aa", "å"}, {"AA", "Å"}, {"l", "ł"}, {"L", "Ł"},
        {"i", "ı"}, {"j", "ȷ"}, {"dh", "ð"}, {"DH", "Ð"}, {"th", "þ"}, {"TH", "Þ"},
        {"ng", "ŋ"}, {"NG", "Ŋ"},
        {"S", "§"}, {"P", "¶"}, {"pounds", "£"}, {"textsterling", "£"}, {"copyright", "©"},
        {"textcopyright", "©"}, {"textregistered", "®"}, {"texttrademark", "™"},
        {"dag", "†"}, {"ddag", "‡"}, {"textdagger", "†"}, {"textdaggerdbl", "‡"},
        {"textbullet", "•"}, {"textellipsis", "…"}, {"ldots", "…"}, {"dots", "…"},
        {"textendash", "–"}, {"textemdash", "—"}, {"textquoteleft", "‘"}, {"textquoteright", "’"},
        {"textquotedblleft", "“"}, {"textquotedblright", "”"}, {"textquotedbl", "\""},
        {"textdegree", "°"}, {"degree", "°"}, {"textbackslash", "\\"}, {"textasciitilde", "~"},
        {"textasciicircum", "^"}, {"textbar", "|"}, {"textless", "<"}, {"textgreater", ">"},
        {"textbraceleft", "{"}, {"textbraceright", "}"}, {"textunderscore", "_"},
        {"euro", "€"}, {"texteuro", "€"}, {"textyen", "¥"}, {"textcent", "¢"},
        {"textperiodcentered", "·"}, {"textmu", "µ"}, {"textpm", "±"}, {"texttimes", "×"},
        {"textdiv", "÷"}, {"textonehalf", "½"}, {"textonequarter", "¼"}, {"textthreequarters", "¾"},
        {"textexclamdown", "¡"}, {"textquestiondown", "¿"}, {"guillemotleft", "«"},
        {"guillemotright", "»"}, {"textrightarrow", "→"}, {"textleftarrow", "←"},
        {"checkmark", "✓"}, {"slash", "/"}, {"LaTeX", "LaTeX"}, {"LaTeXe", "LaTeX2e"},
        {"TeX", "TeX"}, {"BibTeX", "BibTeX"}, {"XeLaTeX", "XeLaTeX"}, {"LuaLaTeX", "LuaLaTeX"},
        {"quad", " "}, {"qquad", "  "}, {"enspace", " "}, {"enskip", " "},
        {"thinspace", " "}, {",", " "}, {";", " "}, {":", " "}, {" ", " "},
        {"space", " "}, {"textvisiblespace", "␣"}, {"_", "_"}, {"&", "&"}, {"%", "%"},
        {"$", "$"}, {"#", "#"}, {"{", "{"}, {"}", "}"}, {"lbrace", "{"}, {"rbrace", "}"},
        {"lq", "‘"}, {"rq", "’"}, {"ldq", "“"}, {"rdq", "”"},
    };
    auto it = symbols.find(name);
    return it == symbols.end() ? nullptr : it->second;
}

// ===== COLOURS =====

std::string HexColor(int r, int g, int b) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                  std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
    return buf;
}

const std::unordered_map<std::string, std::string>& BaseColors() {
    static const std::unordered_map<std::string, std::string> colors = {
        {"red", "#FF0000"}, {"green", "#00FF00"}, {"blue", "#0000FF"}, {"cyan", "#00FFFF"},
        {"magenta", "#FF00FF"}, {"yellow", "#FFFF00"}, {"black", "#000000"}, {"white", "#FFFFFF"},
        {"gray", "#808080"}, {"grey", "#808080"}, {"darkgray", "#404040"}, {"darkgrey", "#404040"},
        {"lightgray", "#BFBFBF"}, {"lightgrey", "#BFBFBF"}, {"brown", "#BF8040"}, {"lime", "#BFFF00"},
        {"olive", "#808000"}, {"orange", "#FF8000"}, {"pink", "#FFBFBF"}, {"purple", "#BF0040"},
        {"teal", "#008080"}, {"violet", "#800080"},
    };
    return colors;
}

// Article-class font size commands in points (10pt base).
float SizeCommandPt(const std::string& name) {
    static const std::unordered_map<std::string, float> sizes = {
        {"tiny", 5.0f}, {"scriptsize", 7.0f}, {"footnotesize", 8.0f}, {"small", 9.0f},
        {"normalsize", 0.0f}, {"large", 12.0f}, {"Large", 14.4f}, {"LARGE", 17.28f},
        {"huge", 20.74f}, {"Huge", 24.88f},
    };
    auto it = sizes.find(name);
    return it == sizes.end() ? -1.0f : it->second;
}

// TeX lengths → points. "\textwidth"-relative values use article's 345pt.
constexpr float kTextWidthPt = 345.0f;

float ParseTeXLengthPt(std::string value) {
    value = TrimCopy(value);
    if (value.empty()) return 0.0f;
    char* end = nullptr;
    float number = std::strtof(value.c_str(), &end);
    if (end == value.c_str()) {
        // "\textwidth" alone.
        number = 1.0f;
        end = const_cast<char*>(value.c_str());
    }
    std::string unit = TrimCopy(std::string(end));
    if (unit.empty() || unit == "pt") return number;
    if (unit == "in") return number * 72.0f;
    if (unit == "cm") return number * 72.0f / 2.54f;
    if (unit == "mm") return number * 72.0f / 25.4f;
    if (unit == "px") return number * 72.0f / 96.0f;
    if (unit == "bp") return number;
    if (unit == "pc") return number * 12.0f;
    if (unit == "em") return number * 10.0f;
    if (unit == "ex") return number * 4.3f;
    if (unit == "\\textwidth" || unit == "\\linewidth" || unit == "\\columnwidth" ||
        unit == "\\hsize" || unit == "\\textheight" || unit == "\\paperwidth") {
        return number * kTextWidthPt;
    }
    return number;
}

// Splits "a, b ,c" on commas, trimming.
std::vector<std::string> SplitCommaList(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '{') ++depth;
        if (c == '}') --depth;
        if (c == ',' && depth == 0) { out.push_back(TrimCopy(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    if (!TrimCopy(cur).empty()) out.push_back(TrimCopy(cur));
    return out;
}

// Reads a balanced {...} group from raw text starting at i (which must point
// at '{'); returns the inner text and advances i past the closing brace.
bool ReadRawGroupAt(const std::string& s, size_t& i, std::string& inner) {
    if (i >= s.size() || s[i] != '{') return false;
    int depth = 0;
    size_t start = i + 1;
    for (size_t k = i; k < s.size(); ++k) {
        char c = s[k];
        if (c == '\\' && k + 1 < s.size()) { ++k; continue; }
        if (c == '{') ++depth;
        else if (c == '}') {
            if (--depth == 0) {
                inner = s.substr(start, k - start);
                i = k + 1;
                return true;
            }
        }
    }
    return false;
}

// Environments whose body is copied verbatim (code).
bool IsVerbatimEnvironment(const std::string& name) {
    static const std::unordered_set<std::string> names = {
        "verbatim", "verbatim*", "Verbatim", "BVerbatim", "LVerbatim", "lstlisting",
        "minted", "alltt", "boxedverbatim", "code", "spverbatim",
    };
    return names.count(name) != 0;
}

// Display-math environments; the value says whether the environment itself
// must be kept around the source for the math engine (equation-like bodies
// are plain math, the alignment ones carry structure).
bool IsMathEnvironment(const std::string& name, bool& keepEnvironment, bool& numbered) {
    static const std::unordered_map<std::string, std::pair<bool, bool>> envs = {
        {"equation", {false, true}}, {"equation*", {false, false}}, {"displaymath", {false, false}},
        {"math", {false, false}}, {"align", {true, true}}, {"align*", {true, false}},
        {"gather", {true, true}}, {"gather*", {true, false}}, {"multline", {true, true}},
        {"multline*", {true, false}}, {"eqnarray", {true, true}}, {"eqnarray*", {true, false}},
        {"flalign", {true, true}}, {"flalign*", {true, false}}, {"alignat", {true, true}},
        {"alignat*", {true, false}}, {"dmath", {false, true}}, {"dmath*", {false, false}},
    };
    auto it = envs.find(name);
    if (it == envs.end()) return false;
    keepEnvironment = it->second.first;
    numbered = it->second.second;
    return true;
}

bool IsTikZEnvironment(const std::string& name) {
    return name == "tikzpicture" || name == "axis" || name == "pgfpicture" ||
           name == "picture" || name == "circuitikz" || name == "forest" || name == "tikzcd";
}

// Marker bytes for cross-references resolved after the whole source is read
// (a \ref may precede its \label). Never appear in LaTeX text.
constexpr char kRefOpen = '\x01';
constexpr char kRefClose = '\x02';

} // namespace

// ===== THE READER =====

namespace {

struct TextStyle {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    bool code = false;
    bool superscript = false;
    bool subscript = false;
    std::string color;
    float fontSizePt = 0.0f;
    std::string linkTarget;

    bool operator==(const TextStyle& o) const {
        return bold == o.bold && italic == o.italic && underline == o.underline &&
               strikethrough == o.strikethrough && code == o.code &&
               superscript == o.superscript && subscript == o.subscript &&
               color == o.color && fontSizePt == o.fontSizePt && linkTarget == o.linkTarget;
    }
};

struct Macro {
    int argCount = 0;
    bool hasOptional = false;
    std::string optionalDefault;
    std::string body;
    std::string rawDefinition;   // as written, for the math prelude
};

struct EnvironmentMacro {
    int argCount = 0;
    bool hasOptional = false;
    std::string optionalDefault;
    std::string beginBody;
    std::string endBody;
};

struct TheoremKind {
    std::string label;        // "Theorem"
    std::string counter;      // shared counter name
    bool numbered = true;
};

enum class EnvKind {
    Generic, Document, List, Quote, Align, Float, Tabular, Theorem, Abstract,
    Minipage, UserDefined, Verse, Titlepage
};

struct EnvFrame {
    std::string name;
    EnvKind kind = EnvKind::Generic;
    size_t scopeDepth = 0;        // style scope index to pop back to
    // List
    bool ordered = false;
    bool description = false;
    int itemCounter = 0;
    bool itemOpen = false;
    // Float
    std::string floatKind;        // "Figure" / "Table"
    // Theorem
    bool proof = false;
    // User environment
    std::string endBody;
};

struct Scope {
    TextStyle style;
    RichTextAlign align = RichTextAlign::Default;
};

enum class Stop { End, CloseBrace, Cell };
enum class StopReason { End, CloseBrace, Cell, Row, EndEnvironment };

class Reader {
public:
    Reader(std::string source, UCRichDocument& doc,
           std::vector<LaTeXDocumentDiagnostic>* diags,
           const LaTeXDocumentReadOptions& options)
        : src_(std::move(source)), doc_(doc), diags_(diags), options_(options) {
        scopes_.push_back(Scope{});
        // amsthm's proof environment is always defined.
        TheoremKind proof;
        proof.label = "Proof";
        proof.counter = "proof";
        proof.numbered = false;
        theorems_["proof"] = proof;
    }

    bool Run();

private:
    // ----- input -----
    std::string src_;
    size_t pos_ = 0;
    UCRichDocument& doc_;
    std::vector<LaTeXDocumentDiagnostic>* diags_;
    LaTeXDocumentReadOptions options_;
    int expansions_ = 0;
    int inputFiles_ = 0;
    std::set<std::string> reportedUnknown_;

    // ----- state -----
    std::vector<Scope> scopes_;
    std::vector<EnvFrame> envs_;
    bool inDocument_ = false;
    bool hasDocumentEnvironment_ = false;
    bool finished_ = false;

    RichDocBlock cur_;
    bool blockActive_ = false;
    bool pendingLineBreak_ = false;
    bool pendingSpace_ = false;
    RichTextRun pendingSpaceRun_;                   // the style the space was seen in
    std::vector<RichTextRun>* capture_ = nullptr;   // table cells, footnotes
    int quoteDepth_ = 0;

    std::unordered_map<std::string, Macro> macros_;
    std::unordered_map<std::string, EnvironmentMacro> userEnvs_;
    std::unordered_map<std::string, TheoremKind> theorems_;
    std::unordered_map<std::string, int> theoremCounters_;
    std::vector<std::string> mathPrelude_;          // raw macro definitions, in order
    std::unordered_map<std::string, std::string> colors_;
    std::vector<std::string> graphicsPaths_;

    // counters and cross references
    int counters_[4] = {0, 0, 0, 0};                // chapter, section, subsection, subsubsection
    bool hasChapters_ = false;
    bool appendix_ = false;
    int figureCounter_ = 0;
    int tableCounter_ = 0;
    int equationCounter_ = 0;
    int footnoteCounter_ = 0;
    std::string lastRefValue_;
    std::unordered_map<std::string, std::string> labels_;
    std::unordered_map<std::string, int> citations_;
    int bibitemCounter_ = 0;
    std::vector<std::vector<RichTextRun>> footnotes_;

    std::string titleRaw_, authorRaw_, dateRaw_;
    bool dateGiven_ = false;

    // ----- scanning primitives -----
    bool AtEnd() const { return pos_ >= src_.size(); }
    char Peek(size_t ahead = 0) const { return pos_ + ahead < src_.size() ? src_[pos_ + ahead] : '\0'; }
    bool StartsWith(const char* s) const { return src_.compare(pos_, std::strlen(s), s) == 0; }
    int CurrentLine() const {
        const size_t at = std::min(pos_, src_.size());
        int line = 1 + static_cast<int>(std::count(src_.begin(), src_.begin() + static_cast<long>(at), '\n'));
        for (const auto& e : spliceLines_) if (e.first <= at) line -= e.second;
        return std::max(1, line);
    }
    void Diag(const std::string& message) {
        if (diags_) diags_->push_back({CurrentLine(), message});
    }
    void SkipComment();                 // at '%'
    void SkipSpacesAndComments();       // spaces, tabs, newlines, comments
    void SkipInlineSpaces();            // spaces/tabs only
    std::string ReadCommandName();      // after the backslash
    bool ReadStar();
    bool ReadGroup(std::string& inner);            // {...} raw, comments kept
    bool ReadOptional(std::string& inner);         // [...] raw
    std::string ReadArgument();                    // {...} or single token
    std::string ReadUntil(const std::string& terminator, bool& found);
    std::string PeekEnvironmentName(size_t at) const;

    // ----- splicing -----
    // Spliced text (macro bodies, \input files) shifts the newline count;
    // the shift is remembered per position so diagnostics keep reporting
    // lines of the original source. Positions are kept in order; a later
    // splice before an earlier one moves the earlier entries along.
    std::vector<std::pair<size_t, int>> spliceLines_;
    void Splice(const std::string& text) {
        const int newlines = static_cast<int>(std::count(text.begin(), text.end(), '\n'));
        if (newlines > 0) {
            for (auto& e : spliceLines_) if (e.first >= pos_) e.first += text.size();
            spliceLines_.push_back({pos_ + text.size(), newlines});
        } else {
            for (auto& e : spliceLines_) if (e.first > pos_) e.first += text.size();
        }
        src_.insert(pos_, text);
    }

    // ----- output -----
    Scope& scope() { return scopes_.back(); }
    void PushScope() { scopes_.push_back(scopes_.back()); }
    void PopScope() { if (scopes_.size() > 1) scopes_.pop_back(); }
    RichTextAlign CurrentAlign() const { return scopes_.back().align; }

    void EnsureBlock();
    void BeginBlock(RichBlockType type, int headingLevel = 0);
    void FlushParagraph();
    void EmitText(const std::string& text);
    void EmitRun(RichTextRun run);
    RichTextRun StyledRun(const std::string& text) const;
    void EmitSpace() {
        if (!blockActive_) return;
        if (!pendingSpace_) pendingSpaceRun_ = StyledRun(" ");
        pendingSpace_ = true;
    }
    void EmitLineBreak();
    void EmitMathRun(const std::string& source);
    void EmitMathBlock(const std::string& source);
    void EmitStandaloneBlock(RichDocBlock block);

    // ----- parsing -----
    StopReason ParseContent(Stop stop);
    bool HandleCommand(const std::string& name, Stop stop, StopReason& reason);
    bool HandleEnvironmentBegin(const std::string& name);
    bool HandleEnvironmentEnd(const std::string& name, Stop stop, StopReason& reason);
    void ParseTabular(const std::string& envName);
    void ParseVerbatimEnvironment(const std::string& name);
    void ParseMathEnvironment(const std::string& name, bool keepEnvironment, bool numbered);
    void ParseInlineMath(const std::string& closer);
    void ParseDisplayMath(const std::string& closer);
    void ParseSection(const std::string& name, int level, bool starred);
    void ParseCaption();
    void ParseIncludeGraphics();
    void ParseFootnote();
    void ParseItem();
    void ParseMacroDefinition(const std::string& name);
    void ParseEnvironmentDefinition();
    void ParseNewTheorem();
    void ParseDefineColor();
    bool ExpandMacro(const std::string& name);
    void ParseInput(const std::string& name);
    void ParseMakeTitle();
    void ParseVerb(bool star);
    void ParseAccent(const std::string& accent);
    void ParseTextCommandWithStyle(const std::function<void(TextStyle&)>& apply);
    void SkipUnknownArguments();
    void ParseLabel();
    void EmitRefMarker(char kind, const std::string& key);

    std::string ResolveColor(std::string spec);
    std::string SectionNumber(int level);
    std::string MathPreludeFor(const std::string& mathSource) const;
    std::string TodayString() const;
    void FinishDocument();
    void ResolveReferences();
};

// ===== SCANNING PRIMITIVES =====

void Reader::SkipComment() {
    // '%' to end of line; TeX also eats the newline and the next line's
    // leading spaces, so a comment never produces a paragraph break by itself.
    while (!AtEnd() && Peek() != '\n') ++pos_;
    if (!AtEnd()) ++pos_;
    while (!AtEnd() && (Peek() == ' ' || Peek() == '\t')) ++pos_;
}

void Reader::SkipSpacesAndComments() {
    for (;;) {
        if (AtEnd()) return;
        char c = Peek();
        if (IsSpaceChar(c)) { ++pos_; continue; }
        if (c == '%') { SkipComment(); continue; }
        return;
    }
}

void Reader::SkipInlineSpaces() {
    while (!AtEnd() && (Peek() == ' ' || Peek() == '\t')) ++pos_;
}

std::string Reader::ReadCommandName() {
    // pos_ is just past the backslash.
    if (AtEnd()) return std::string();
    std::string name;
    if (IsLetter(Peek())) {
        while (!AtEnd() && IsLetter(Peek())) name.push_back(src_[pos_++]);
        // A control word swallows the spaces after it (but not a newline
        // pair, which stays a paragraph break — handled by the caller).
        size_t p = pos_;
        while (p < src_.size() && (src_[p] == ' ' || src_[p] == '\t')) ++p;
        if (p < src_.size() && src_[p] == '\n') {
            size_t q = p + 1;
            while (q < src_.size() && (src_[q] == ' ' || src_[q] == '\t')) ++q;
            if (q < src_.size() && src_[q] == '\n') return name;   // keep the blank line
        }
        pos_ = p;
        if (!AtEnd() && Peek() == '\n') {
            ++pos_;
            EmitSpace();
        }
    } else {
        name.push_back(src_[pos_++]);
    }
    return name;
}

bool Reader::ReadStar() {
    size_t p = pos_;
    while (p < src_.size() && (src_[p] == ' ' || src_[p] == '\t')) ++p;
    if (p < src_.size() && src_[p] == '*') { pos_ = p + 1; return true; }
    return false;
}

bool Reader::ReadGroup(std::string& inner) {
    size_t p = pos_;
    while (p < src_.size() && IsSpaceChar(src_[p])) ++p;
    if (p < src_.size() && src_[p] == '%') {
        // A comment between a command and its argument is allowed.
        size_t save = pos_;
        pos_ = p;
        SkipSpacesAndComments();
        p = pos_;
        pos_ = save;
    }
    if (p >= src_.size() || src_[p] != '{') return false;
    size_t i = p;
    if (!ReadRawGroupAt(src_, i, inner)) {
        Diag("unbalanced braces");
        inner = src_.substr(p + 1);
        pos_ = src_.size();
        return true;
    }
    pos_ = i;
    return true;
}

bool Reader::ReadOptional(std::string& inner) {
    size_t p = pos_;
    while (p < src_.size() && (src_[p] == ' ' || src_[p] == '\t')) ++p;
    if (p >= src_.size() || src_[p] != '[') return false;
    int depth = 0;
    for (size_t k = p + 1; k < src_.size(); ++k) {
        char c = src_[k];
        if (c == '\\' && k + 1 < src_.size()) { ++k; continue; }
        if (c == '{') ++depth;
        else if (c == '}') --depth;
        else if (c == ']' && depth <= 0) {
            inner = src_.substr(p + 1, k - p - 1);
            pos_ = k + 1;
            return true;
        }
    }
    return false;
}

std::string Reader::ReadArgument() {
    std::string inner;
    if (ReadGroup(inner)) return inner;
    SkipInlineSpaces();
    if (AtEnd()) return std::string();
    if (Peek() == '\\') {
        ++pos_;
        return "\\" + ReadCommandName();
    }
    size_t i = pos_;
    DecodeUtf8At(src_, i);
    std::string token = src_.substr(pos_, i - pos_);
    pos_ = i;
    return token;
}

std::string Reader::ReadUntil(const std::string& terminator, bool& found) {
    size_t at = src_.find(terminator, pos_);
    found = at != std::string::npos;
    std::string body = src_.substr(pos_, found ? at - pos_ : std::string::npos);
    pos_ = found ? at + terminator.size() : src_.size();
    return body;
}

std::string Reader::PeekEnvironmentName(size_t at) const {
    // at points just past "\begin" / "\end".
    while (at < src_.size() && (src_[at] == ' ' || src_[at] == '\t')) ++at;
    if (at >= src_.size() || src_[at] != '{') return std::string();
    size_t close = src_.find('}', at);
    if (close == std::string::npos) return std::string();
    return TrimCopy(src_.substr(at + 1, close - at - 1));
}

// ===== OUTPUT =====

void Reader::EnsureBlock() {
    if (blockActive_) return;
    RichBlockType type = RichBlockType::Paragraph;
    int level = 0;
    if (!envs_.empty()) {
        for (auto it = envs_.rbegin(); it != envs_.rend(); ++it) {
            if (it->kind == EnvKind::List) {
                if (it->itemOpen) {
                    type = RichBlockType::ListItem;
                    cur_ = RichDocBlock{};
                    cur_.type = type;
                    cur_.orderedList = it->ordered;
                    int depth = 0;
                    for (const EnvFrame& f : envs_) if (f.kind == EnvKind::List) ++depth;
                    cur_.listLevel = std::max(0, depth - 1);
                    cur_.align = CurrentAlign();
                    blockActive_ = true;
                    pendingSpace_ = false;
                    pendingLineBreak_ = false;
                    return;
                }
                break;
            }
        }
    }
    if (quoteDepth_ > 0 && !capture_) type = RichBlockType::BlockQuote;
    BeginBlock(type, level);
}

void Reader::BeginBlock(RichBlockType type, int headingLevel) {
    if (blockActive_) FlushParagraph();
    cur_ = RichDocBlock{};
    cur_.type = type;
    cur_.headingLevel = headingLevel;
    cur_.align = CurrentAlign();
    blockActive_ = true;
    pendingSpace_ = false;
    pendingLineBreak_ = false;
}

void Reader::FlushParagraph() {
    if (!blockActive_) return;
    blockActive_ = false;
    pendingSpace_ = false;
    pendingLineBreak_ = false;
    // Trim trailing whitespace of the last run and drop empty runs.
    while (!cur_.runs.empty()) {
        RichTextRun& last = cur_.runs.back();
        if (last.math) break;
        size_t e = last.text.find_last_not_of(" \t");
        last.text = e == std::string::npos ? std::string() : last.text.substr(0, e + 1);
        if (!last.text.empty()) break;
        cur_.runs.pop_back();
    }
    bool hasContent = false;
    for (const RichTextRun& r : cur_.runs) {
        if (r.math || !TrimCopy(r.text).empty()) { hasContent = true; break; }
    }
    if (!hasContent) return;
    if (capture_) {
        if (!capture_->empty()) cur_.runs.front().lineBreakBefore = true;
        capture_->insert(capture_->end(), cur_.runs.begin(), cur_.runs.end());
        return;
    }
    doc_.blocks.push_back(std::move(cur_));
    cur_ = RichDocBlock{};
}

void Reader::EmitRun(RichTextRun run) {
    if (!inDocument_) return;
    EnsureBlock();
    if (pendingLineBreak_) {
        run.lineBreakBefore = true;
        pendingLineBreak_ = false;
        pendingSpace_ = false;
    } else if (pendingSpace_) {
        pendingSpace_ = false;
        if (!cur_.runs.empty()) {
            RichTextRun& last = cur_.runs.back();
            const bool lastEndsWithSpace = !last.math && !last.text.empty() && IsSpaceChar(last.text.back());
            if (!lastEndsWithSpace) {
                // The space belongs to the run whose style it was seen in:
                // "a \textbf{b}" ends the plain run, "{\bfseries b} c" starts
                // the plain one; between two styled words (or formulas) it
                // is a plain run of its own.
                if (!last.math && last.HasSameFormatting(pendingSpaceRun_)) {
                    last.text.push_back(' ');
                } else if (!run.math && run.HasSameFormatting(pendingSpaceRun_)) {
                    run.text.insert(run.text.begin(), ' ');
                } else {
                    cur_.runs.push_back(pendingSpaceRun_);
                }
            }
        }
    }
    if (!cur_.runs.empty() && !run.lineBreakBefore && !run.math &&
        cur_.runs.back().HasSameFormatting(run) && !cur_.runs.back().math) {
        cur_.runs.back().text += run.text;
        return;
    }
    cur_.runs.push_back(std::move(run));
}

void Reader::EmitText(const std::string& text) {
    if (text.empty() || !inDocument_) return;
    // Leading whitespace at the start of a block is dropped, as TeX does.
    if (!blockActive_ && TrimCopy(text).empty()) return;
    EmitRun(StyledRun(text));
}

RichTextRun Reader::StyledRun(const std::string& text) const {
    const TextStyle& st = scopes_.back().style;
    RichTextRun run;
    run.text = text;
    run.bold = st.bold;
    run.italic = st.italic;
    run.underline = st.underline;
    run.strikethrough = st.strikethrough;
    run.code = st.code;
    run.superscript = st.superscript;
    run.subscript = st.subscript;
    run.color = st.color;
    run.fontSizePt = st.fontSizePt;
    run.linkTarget = st.linkTarget;
    return run;
}

void Reader::EmitLineBreak() {
    if (!blockActive_) return;
    // A break right after another break yields an empty line: keep it as a
    // run holding a single space so the paragraph shows the gap.
    if (pendingLineBreak_) {
        RichTextRun gap;
        gap.text = " ";
        gap.lineBreakBefore = true;
        cur_.runs.push_back(gap);
    }
    pendingLineBreak_ = true;
    pendingSpace_ = false;
}

void Reader::EmitMathRun(const std::string& source) {
    std::string flat;
    for (char c : source) flat.push_back(c == '\n' || c == '\r' || c == '\t' ? ' ' : c);
    flat = TrimCopy(flat);
    if (flat.empty()) return;
    RichTextRun run;
    run.math = true;
    run.text = MathPreludeFor(flat) + flat;
    run.color = scope().style.color;
    run.fontSizePt = scope().style.fontSizePt;
    EmitRun(std::move(run));
}

void Reader::EmitMathBlock(const std::string& source) {
    // \label{...} has been recorded; the engine ignores it, the source reads
    // cleaner without it.
    std::string stripped;
    for (size_t i = 0; i < source.size(); ++i) {
        if (source.compare(i, 6, "\\label") == 0) {
            size_t k = i + 6;
            while (k < source.size() && IsSpaceChar(source[k])) ++k;
            std::string key;
            if (ReadRawGroupAt(source, k, key)) { i = k - 1; continue; }
        }
        stripped.push_back(source[i]);
    }
    std::string body = TrimCopy(stripped);
    if (body.empty()) return;
    if (!inDocument_) return;
    if (capture_) {
        // Inside a table cell / footnote a display formula becomes an inline
        // display-style run.
        EmitMathRun(body);
        return;
    }
    FlushParagraph();
    RichDocBlock block;
    block.type = RichBlockType::MathBlock;
    block.align = RichTextAlign::Center;
    std::string text = MathPreludeFor(body) + body;
    // One run per source line (the CodeBlock convention).
    size_t start = 0;
    bool first = true;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        RichTextRun run;
        run.text = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        run.lineBreakBefore = !first;
        first = false;
        block.runs.push_back(run);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    doc_.blocks.push_back(std::move(block));
}

void Reader::EmitStandaloneBlock(RichDocBlock block) {
    if (!inDocument_) return;
    if (capture_) {
        // Block content in a cell: keep its text.
        for (RichTextRun& r : block.runs) EmitRun(r);
        return;
    }
    FlushParagraph();
    doc_.blocks.push_back(std::move(block));
}

void Reader::EmitRefMarker(char kind, const std::string& key) {
    EmitText(std::string(1, kRefOpen) + kind + key + kRefClose);
}

std::string Reader::SectionNumber(int level) {
    // level: 0 chapter, 1 section, 2 subsection, 3 subsubsection
    std::string out;
    int top = hasChapters_ ? 0 : 1;
    for (int l = top; l <= level; ++l) {
        if (!out.empty()) out.push_back('.');
        if (l == top && appendix_) {
            out.push_back(static_cast<char>('A' + std::clamp(counters_[l] - 1, 0, 25)));
        } else {
            out += std::to_string(counters_[l]);
        }
    }
    return out;
}

std::string Reader::TodayString() const {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%B %d, %Y", &tmv);
    return buf;
}

std::string Reader::ResolveColor(std::string spec) {
    spec = TrimCopy(spec);
    // xcolor mixes ("blue!30!white") take the base colour.
    size_t bang = spec.find('!');
    if (bang != std::string::npos) spec = spec.substr(0, bang);
    if (!spec.empty() && spec[0] == '#') return spec;
    auto it = colors_.find(spec);
    if (it != colors_.end()) return it->second;
    auto base = BaseColors().find(spec);
    if (base != BaseColors().end()) return base->second;
    return std::string();
}

// Definitions the formula needs, prepended to the math source so the engine
// can expand the document's own macros (\newcommand{\R}{\mathbb{R}}).
std::string Reader::MathPreludeFor(const std::string& mathSource) const {
    if (mathPrelude_.empty()) return std::string();
    std::vector<bool> used(mathPrelude_.size(), false);
    auto namesIn = [](const std::string& def) {
        // The defined name is the first control sequence after the command.
        size_t bs = def.find('\\', 1);
        if (bs == std::string::npos) return std::string();
        size_t e = bs + 1;
        while (e < def.size() && (IsLetter(def[e]) || def[e] == '@')) ++e;
        if (e == bs + 1 && e < def.size()) ++e;
        return def.substr(bs, e - bs);
    };
    auto mentions = [](const std::string& text, const std::string& name) {
        size_t at = 0;
        while ((at = text.find(name, at)) != std::string::npos) {
            size_t after = at + name.size();
            if (after >= text.size() || !IsLetter(text[after]) || !IsLetter(name.back())) return true;
            at = after;
        }
        return false;
    };
    bool changed = true;
    std::string probe = mathSource;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < mathPrelude_.size(); ++i) {
            if (used[i]) continue;
            std::string name = namesIn(mathPrelude_[i]);
            if (name.empty()) continue;
            if (mentions(probe, name)) {
                used[i] = true;
                changed = true;
                probe += mathPrelude_[i];
            }
        }
    }
    std::string out;
    for (size_t i = 0; i < mathPrelude_.size(); ++i) {
        if (used[i]) out += mathPrelude_[i];
    }
    return out;
}

// ===== CONTENT LOOP =====

StopReason Reader::ParseContent(Stop stop) {
    while (!AtEnd() && !finished_) {
        char c = Peek();
        if (c == '%') { SkipComment(); continue; }
        if (c == '{') {
            ++pos_;
            PushScope();
            StopReason r = ParseContent(Stop::CloseBrace);
            PopScope();
            if (r != StopReason::CloseBrace) return r;   // input ended inside the group
            continue;
        }
        if (c == '}') {
            ++pos_;
            if (stop == Stop::CloseBrace) return StopReason::CloseBrace;
            Diag("unmatched '}' ignored");
            continue;
        }
        if (c == '\\') {
            size_t cmdStart = pos_;
            ++pos_;
            std::string name = ReadCommandName();
            if (name == "\\") {
                // Line break; optional [length] and * are swallowed.
                ReadStar();
                std::string opt;
                ReadOptional(opt);
                if (stop == Stop::Cell) return StopReason::Row;
                EmitLineBreak();
                continue;
            }
            if (name == "begin") {
                std::string env = TrimCopy(ReadArgument());
                if (!HandleEnvironmentBegin(env)) {
                    Diag("unknown environment \\begin{" + env + "} (content kept)");
                    EnvFrame f; f.name = env; f.kind = EnvKind::Generic;
                    f.scopeDepth = scopes_.size();
                    PushScope();
                    envs_.push_back(f);
                }
                continue;
            }
            if (name == "end") {
                std::string env = TrimCopy(ReadArgument());
                StopReason reason;
                if (HandleEnvironmentEnd(env, stop, reason)) {
                    if (reason == StopReason::EndEnvironment) {
                        // The caller (tabular loop) consumes the \end itself.
                        pos_ = cmdStart;
                        return reason;
                    }
                    if (finished_) return StopReason::End;
                }
                continue;
            }
            StopReason reason = StopReason::End;
            if (HandleCommand(name, stop, reason)) {
                if (reason == StopReason::Cell || reason == StopReason::Row) return reason;
                continue;
            }
            continue;
        }
        if (c == '$') {
            if (Peek(1) == '$') { pos_ += 2; ParseDisplayMath("$$"); }
            else { ++pos_; ParseInlineMath("$"); }
            continue;
        }
        if (c == '&') {
            ++pos_;
            if (stop == Stop::Cell) return StopReason::Cell;
            Diag("'&' outside a table kept as text");
            EmitText("&");
            continue;
        }
        if (c == '~') { ++pos_; EmitText(Utf8(0xA0)); continue; }
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            // Whitespace: a run containing two newlines is a paragraph break.
            int newlines = 0;
            while (!AtEnd() && IsSpaceChar(Peek())) {
                if (Peek() == '\n') ++newlines;
                ++pos_;
            }
            if (newlines >= 2) {
                if (stop == Stop::Cell) { EmitSpace(); continue; }
                FlushParagraph();
            } else {
                EmitSpace();
            }
            continue;
        }
        if (c == '-' && Peek(1) == '-') {
            if (Peek(2) == '-') { pos_ += 3; EmitText("—"); }
            else { pos_ += 2; EmitText("–"); }
            continue;
        }
        if (c == '`') {
            if (Peek(1) == '`') { pos_ += 2; EmitText("“"); }
            else { ++pos_; EmitText("‘"); }
            continue;
        }
        if (c == '\'') {
            if (Peek(1) == '\'') { pos_ += 2; EmitText("”"); }
            else { ++pos_; EmitText("’"); }
            continue;
        }
        if (c == '!' && Peek(1) == '`') { pos_ += 2; EmitText("¡"); continue; }
        if (c == '?' && Peek(1) == '`') { pos_ += 2; EmitText("¿"); continue; }
        // Ordinary text up to the next special character.
        size_t start = pos_;
        while (!AtEnd()) {
            char d = Peek();
            if (d == '\\' || d == '{' || d == '}' || d == '$' || d == '&' || d == '~' ||
                d == '%' || d == '`' || d == '\'' || IsSpaceChar(d) ||
                (d == '-' && Peek(1) == '-') || ((d == '!' || d == '?') && Peek(1) == '`')) {
                break;
            }
            ++pos_;
        }
        if (pos_ == start) { ++pos_; continue; }   // defensive: never stall
        EmitText(src_.substr(start, pos_ - start));
    }
    return StopReason::End;
}

// ===== ENVIRONMENTS =====

bool Reader::HandleEnvironmentBegin(const std::string& name) {
    auto push = [&](EnvKind kind) -> EnvFrame& {
        EnvFrame f;
        f.name = name;
        f.kind = kind;
        f.scopeDepth = scopes_.size();
        PushScope();
        envs_.push_back(f);
        return envs_.back();
    };

    if (name == "document") {
        FlushParagraph();
        inDocument_ = true;
        push(EnvKind::Document);
        return true;
    }

    // User environments: splice the begin body, remember the end body.
    auto ue = userEnvs_.find(name);
    if (ue != userEnvs_.end()) {
        const EnvironmentMacro& m = ue->second;
        std::vector<std::string> args;
        if (m.hasOptional) {
            std::string opt;
            args.push_back(ReadOptional(opt) ? opt : m.optionalDefault);
        }
        for (int i = static_cast<int>(args.size()); i < m.argCount; ++i) args.push_back(ReadArgument());
        auto substitute = [&](const std::string& body) {
            std::string out;
            for (size_t i = 0; i < body.size(); ++i) {
                if (body[i] == '#' && i + 1 < body.size() && std::isdigit(static_cast<unsigned char>(body[i + 1]))) {
                    int k = body[i + 1] - '1';
                    if (k >= 0 && k < static_cast<int>(args.size())) out += args[k];
                    ++i;
                } else {
                    out.push_back(body[i]);
                }
            }
            return out;
        };
        EnvFrame& f = push(EnvKind::UserDefined);
        f.endBody = substitute(m.endBody);
        if (++expansions_ < 20000) Splice(substitute(m.beginBody));
        return true;
    }

    if (name == "itemize" || name == "enumerate" || name == "description" ||
        name == "compactitem" || name == "compactenum" || name == "compactdesc" ||
        name == "itemize*" || name == "enumerate*" || name == "tasks" || name == "outline") {
        std::string opt;
        ReadOptional(opt);   // enumitem options
        FlushParagraph();
        EnvFrame& f = push(EnvKind::List);
        f.ordered = name.find("enum") != std::string::npos;
        f.description = name.find("desc") != std::string::npos;
        return true;
    }
    if (name == "quote" || name == "quotation" || name == "displayquote" || name == "verse") {
        FlushParagraph();
        push(name == "verse" ? EnvKind::Verse : EnvKind::Quote);
        ++quoteDepth_;
        return true;
    }
    if (name == "abstract") {
        FlushParagraph();
        push(EnvKind::Abstract);
        RichDocBlock heading;
        heading.type = RichBlockType::Paragraph;
        heading.align = RichTextAlign::Center;
        RichTextRun run; run.text = "Abstract"; run.bold = true;
        heading.runs.push_back(run);
        EmitStandaloneBlock(std::move(heading));
        return true;
    }
    if (name == "center" || name == "flushleft" || name == "flushright" || name == "centering") {
        FlushParagraph();
        push(EnvKind::Align);
        scope().align = name == "center" || name == "centering" ? RichTextAlign::Center
                      : name == "flushleft" ? RichTextAlign::Left : RichTextAlign::Right;
        return true;
    }
    if (name == "figure" || name == "figure*" || name == "table" || name == "table*" ||
        name == "wrapfigure" || name == "wraptable" || name == "SCfigure" || name == "SCtable" ||
        name == "subfigure" || name == "subtable" || name == "sidewaysfigure" || name == "sidewaystable" ||
        name == "algorithm" || name == "listing" || name == "float") {
        std::string opt;
        ReadOptional(opt);                         // placement
        if (name.rfind("wrap", 0) == 0) { std::string w; ReadGroup(w); ReadGroup(w); }
        if (name == "subfigure" || name == "subtable") { std::string w; ReadGroup(w); }
        FlushParagraph();
        EnvFrame& f = push(EnvKind::Float);
        f.floatKind = name.find("fig") != std::string::npos ? "Figure"
                    : name.find("tab") != std::string::npos ? "Table"
                    : name == "algorithm" ? "Algorithm" : name == "listing" ? "Listing" : "Figure";
        return true;
    }
    if (name == "tabular" || name == "tabular*" || name == "tabularx" || name == "longtable" ||
        name == "tabulary" || name == "supertabular" || name == "xltabular" || name == "tblr" ||
        name == "longtabu" || name == "tabu") {
        ParseTabular(name);
        return true;
    }
    if (IsVerbatimEnvironment(name)) {
        ParseVerbatimEnvironment(name);
        return true;
    }
    bool keepEnv = false, numbered = false;
    if (IsMathEnvironment(name, keepEnv, numbered)) {
        ParseMathEnvironment(name, keepEnv, numbered);
        return true;
    }
    if (name == "subequations" || name == "sloppypar" || name == "samepage" || name == "group" ||
        name == "flushbottom" || name == "spacing" || name == "singlespace" || name == "onehalfspace" ||
        name == "doublespace" || name == "multicols" || name == "multicols*" || name == "landscape" ||
        name == "frame" || name == "block" || name == "columns" || name == "column" ||
        name == "adjustbox" || name == "small" || name == "footnotesize" || name == "scriptsize" ||
        name == "tiny" || name == "large" || name == "Large" || name == "LARGE" || name == "huge" ||
        name == "Huge" || name == "normalsize" || name == "em" || name == "bfseries" ||
        name == "itshape" || name == "ttfamily" || name == "otherlanguage" || name == "hyphenrules" ||
        name == "framed" || name == "shaded" || name == "mdframed" || name == "tcolorbox" ||
        name == "tabbing" || name == "flalign" || name == "raggedright" || name == "raggedleft" ||
        name == "appendices" || name == "refsection" || name == "sffamily" || name == "rmfamily") {
        std::string arg;
        if (name == "spacing" || name == "multicols" || name == "multicols*" || name == "column" ||
            name == "otherlanguage" || name == "hyphenrules") {
            ReadGroup(arg);
        }
        if (name == "block" || name == "frame") {
            std::string opt; ReadOptional(opt);
            if (name == "block") ReadGroup(arg);
        }
        if (name == "tcolorbox" || name == "mdframed" || name == "adjustbox") {
            std::string opt; ReadOptional(opt);
            if (name == "adjustbox") ReadGroup(arg);
        }
        if (name == "raggedright" || name == "raggedleft" || name == "framed" || name == "shaded" ||
            name == "mdframed" || name == "tcolorbox") {
            FlushParagraph();
        }
        push(EnvKind::Generic);
        float pt = SizeCommandPt(name);
        if (pt >= 0.0f) scope().style.fontSizePt = pt;
        if (name == "em" || name == "itshape") scope().style.italic = !scope().style.italic;
        if (name == "bfseries") scope().style.bold = true;
        if (name == "ttfamily") scope().style.code = true;
        if (name == "raggedright") scope().align = RichTextAlign::Left;
        if (name == "raggedleft") scope().align = RichTextAlign::Right;
        if (name == "appendices") { appendix_ = true; counters_[hasChapters_ ? 0 : 1] = 0; }
        return true;
    }
    if (name == "minipage" || name == "parbox") {
        std::string opt, width;
        ReadOptional(opt);
        ReadOptional(opt);
        ReadOptional(opt);
        ReadGroup(width);
        push(EnvKind::Minipage);
        return true;
    }
    if (name == "titlepage") {
        FlushParagraph();
        push(EnvKind::Titlepage);
        return true;
    }
    if (name == "thebibliography") {
        std::string widest;
        ReadGroup(widest);
        FlushParagraph();
        RichDocBlock heading;
        heading.type = RichBlockType::Heading;
        heading.headingLevel = hasChapters_ ? 1 : 2;
        RichTextRun run; run.text = "References";
        heading.runs.push_back(run);
        EmitStandaloneBlock(std::move(heading));
        EnvFrame& f = push(EnvKind::List);
        f.ordered = true;
        return true;
    }
    auto th = theorems_.find(name);
    if (th != theorems_.end()) {
        std::string note;
        ReadOptional(note);
        FlushParagraph();
        EnvFrame& f = push(EnvKind::Theorem);
        const TheoremKind& kind = th->second;
        f.proof = LowerCopy(kind.label) == "proof";
        BeginBlock(RichBlockType::Paragraph);
        std::string head = kind.label;
        if (kind.numbered && !f.proof) {
            int n = ++theoremCounters_[kind.counter];
            std::string prefix = hasChapters_ ? (counters_[0] > 0 ? std::to_string(counters_[0]) + "." : "")
                                              : std::string();
            head += " " + prefix + std::to_string(n);
            lastRefValue_ = prefix + std::to_string(n);
        }
        RichTextRun run;
        run.text = head;
        run.bold = !f.proof;
        run.italic = f.proof;
        EmitRun(run);
        if (!note.empty()) {
            RichTextRun gap; gap.text = " "; gap.bold = !f.proof; gap.italic = f.proof;
            EmitRun(gap);
            Splice(std::string(f.proof ? "{\\itshape (" : "{\\bfseries (") + note + ")}\\UCTheoremDot{}\\UCTheoremBody{}");
        } else {
            RichTextRun dot; dot.text = ". "; dot.bold = !f.proof; dot.italic = f.proof;
            EmitRun(dot);
            if (!f.proof) scope().style.italic = true;
        }
        return true;
    }
    if (IsTikZEnvironment(name)) {
        // Phase 4 territory: the picture is reported and its place marked.
        bool found = false;
        std::string opt;
        ReadOptional(opt);
        ReadUntil("\\end{" + name + "}", found);
        Diag("\\begin{" + name + "} pictures are not imported (TikZ / pgfplots are out of the document subset)");
        RichDocBlock placeholder;
        placeholder.type = RichBlockType::Paragraph;
        placeholder.align = RichTextAlign::Center;
        RichTextRun run; run.text = "[" + name + " picture]"; run.italic = true; run.color = "#A05020";
        placeholder.runs.push_back(run);
        EmitStandaloneBlock(std::move(placeholder));
        return true;
    }
    if (name == "comment") {
        bool found = false;
        ReadUntil("\\end{comment}", found);
        return true;
    }
    if (name == "filecontents" || name == "filecontents*") {
        std::string file; ReadGroup(file);
        bool found = false;
        ReadUntil("\\end{" + name + "}", found);
        return true;
    }
    return false;
}

bool Reader::HandleEnvironmentEnd(const std::string& name, Stop stop, StopReason& reason) {
    reason = StopReason::End;
    if (name == "document") {
        FlushParagraph();
        finished_ = true;
        reason = StopReason::End;
        return true;
    }
    if (envs_.empty()) {
        Diag("\\end{" + name + "} without a matching \\begin");
        return false;
    }
    EnvFrame& top = envs_.back();
    if (top.kind == EnvKind::Tabular) {
        // The tabular loop owns this \end.
        reason = StopReason::EndEnvironment;
        return true;
    }
    if (top.name != name) {
        Diag("\\end{" + name + "} closes \\begin{" + top.name + "}");
    }
    switch (top.kind) {
        case EnvKind::List:
            FlushParagraph();
            break;
        case EnvKind::Quote:
        case EnvKind::Verse:
            FlushParagraph();
            --quoteDepth_;
            break;
        case EnvKind::Abstract:
        case EnvKind::Align:
        case EnvKind::Float:
        case EnvKind::Titlepage:
            FlushParagraph();
            break;
        case EnvKind::Theorem:
            if (top.proof && blockActive_) {
                pendingSpace_ = false;
                if (!cur_.runs.empty() && !cur_.runs.back().math) {
                    std::string& t = cur_.runs.back().text;
                    while (!t.empty() && IsSpaceChar(t.back())) t.pop_back();
                }
                EmitText(" ∎");
            }
            FlushParagraph();
            break;
        case EnvKind::UserDefined:
            if (!top.endBody.empty()) {
                std::string body = top.endBody;
                // Pop the frame first so \end inside the end body does not recurse.
                size_t depth = top.scopeDepth;
                envs_.pop_back();
                while (scopes_.size() > depth) PopScope();
                Splice(body);
                (void)stop;
                return true;
            }
            break;
        default:
            break;
    }
    size_t depth = top.scopeDepth;
    envs_.pop_back();
    while (scopes_.size() > depth) PopScope();
    return true;
}

// ===== TABULAR =====

void Reader::ParseTabular(const std::string& envName) {
    std::string opt, spec;
    ReadOptional(opt);                                   // [pos]
    if (envName == "tabular*" || envName == "tabularx" || envName == "tabulary" ||
        envName == "xltabular" || envName == "longtabu" || envName == "tabu") {
        std::string width;
        if (envName != "tabu" && envName != "longtabu") ReadGroup(width);
    }
    ReadGroup(spec);                                     // column specification
    ReadOptional(opt);

    FlushParagraph();
    RichDocBlock table;
    table.type = RichBlockType::Table;
    table.align = CurrentAlign();

    EnvFrame frame;
    frame.name = envName;
    frame.kind = EnvKind::Tabular;
    frame.scopeDepth = scopes_.size();
    PushScope();
    scope().align = RichTextAlign::Default;
    envs_.push_back(frame);

    std::vector<RichTextRun>* outerCapture = capture_;
    RichDocBlock outerBlock = cur_;
    bool outerActive = blockActive_;
    bool outerPendingBreak = pendingLineBreak_;
    bool outerPendingSpace = pendingSpace_;
    blockActive_ = false;
    pendingLineBreak_ = false;
    pendingSpace_ = false;

    RichTableRow row;
    bool ruleAfterFirstRow = false;
    auto skipRowCommands = [&]() -> bool {
        // Rules and spacing between rows; true when the row is closed by \end.
        for (;;) {
            SkipSpacesAndComments();
            if (AtEnd()) return true;
            if (Peek() != '\\') return false;
            size_t save = pos_;
            ++pos_;
            std::string name = ReadCommandName();
            if (name == "hline" || name == "toprule" || name == "midrule" || name == "bottomrule" ||
                name == "cline" || name == "cmidrule" || name == "specialrule" || name == "addlinespace" ||
                name == "noalign" || name == "endhead" || name == "endfirsthead" || name == "endfoot" ||
                name == "endlastfoot" || name == "hdashline" || name == "cdashline" || name == "morecmidrules") {
                if (table.tableRows.size() == 1 && row.cells.empty()) ruleAfterFirstRow = true;
                std::string arg;
                if (name == "cline" || name == "cmidrule" || name == "cdashline") {
                    ReadOptional(arg); ReadOptional(arg); ReadGroup(arg);
                } else if (name == "specialrule") {
                    ReadGroup(arg); ReadGroup(arg); ReadGroup(arg);
                } else if (name == "addlinespace") {
                    ReadOptional(arg);
                } else if (name == "noalign") {
                    ReadGroup(arg);
                }
                continue;
            }
            if (name == "end") {
                std::string env = PeekEnvironmentName(pos_);
                if (env == envName) {
                    ReadArgument();
                    return true;
                }
            }
            pos_ = save;
            return false;
        }
    };

    for (;;) {
        if (skipRowCommands()) break;
        RichTableCell cell;
        std::vector<RichTextRun> cellRuns;
        capture_ = &cellRuns;

        // \multicolumn / \multirow prefixes.
        for (;;) {
            SkipSpacesAndComments();
            if (Peek() != '\\') break;
            size_t save = pos_;
            ++pos_;
            std::string name = ReadCommandName();
            if (name == "multicolumn") {
                std::string n, colSpec;
                ReadGroup(n); ReadGroup(colSpec);
                cell.columnSpan = std::max(1, std::atoi(TrimCopy(n).c_str()));
                std::string cs = colSpec;
                continue;
            }
            if (name == "multirow") {
                std::string n, w, o;
                ReadOptional(o); ReadGroup(n); ReadOptional(o); ReadGroup(w); ReadOptional(o);
                cell.rowSpan = std::max(1, std::atoi(TrimCopy(n).c_str()));
                continue;
            }
            pos_ = save;
            break;
        }

        StopReason reason = ParseContent(Stop::Cell);
        FlushParagraph();
        capture_ = nullptr;
        cell.runs = std::move(cellRuns);
        // Trim leading whitespace of the first run.
        if (!cell.runs.empty() && !cell.runs.front().math) {
            std::string& t = cell.runs.front().text;
            size_t b = t.find_first_not_of(" \t");
            t = b == std::string::npos ? std::string() : t.substr(b);
        }

        const bool cellHasContent = !cell.runs.empty();
        if (reason == StopReason::Cell) {
            row.cells.push_back(std::move(cell));
            continue;
        }
        if (reason == StopReason::Row) {
            row.cells.push_back(std::move(cell));
            table.tableRows.push_back(std::move(row));
            row = RichTableRow{};
            continue;
        }
        // End of the environment (or of the input).
        if (cellHasContent || !row.cells.empty()) {
            row.cells.push_back(std::move(cell));
            table.tableRows.push_back(std::move(row));
            row = RichTableRow{};
        }
        if (reason == StopReason::EndEnvironment) {
            ++pos_;                      // backslash
            ReadCommandName();           // "end"
            ReadArgument();              // {tabular}
        } else if (reason == StopReason::End && !finished_) {
            Diag("\\begin{" + envName + "} is never closed");
        }
        break;
    }

    // Pop the frame and scope.
    while (!envs_.empty() && envs_.back().kind == EnvKind::Tabular) {
        size_t depth = envs_.back().scopeDepth;
        envs_.pop_back();
        while (scopes_.size() > depth) PopScope();
    }
    capture_ = outerCapture;
    cur_ = outerBlock;
    blockActive_ = outerActive;
    pendingLineBreak_ = outerPendingBreak;
    pendingSpace_ = outerPendingSpace;

    if (table.tableRows.empty()) return;
    // Pad ragged rows so every row has the column count of the widest one.
    size_t columns = 0;
    for (const RichTableRow& r : table.tableRows) {
        size_t n = 0;
        for (const RichTableCell& c : r.cells) n += static_cast<size_t>(std::max(1, c.columnSpan));
        columns = std::max(columns, n);
    }
    for (RichTableRow& r : table.tableRows) {
        size_t n = 0;
        for (const RichTableCell& c : r.cells) n += static_cast<size_t>(std::max(1, c.columnSpan));
        while (n++ < columns) r.cells.push_back(RichTableCell{});
    }
    table.tableRows.front().header = ruleAfterFirstRow || table.tableRows.size() > 1;
    if (!inDocument_) return;
    if (capture_) {
        // A table inside a cell or footnote: rows become lines.
        for (const RichTableRow& r : table.tableRows) {
            bool firstCell = true;
            EmitLineBreak();
            for (const RichTableCell& c : r.cells) {
                if (!firstCell) EmitText(" | ");
                firstCell = false;
                for (const RichTextRun& run : c.runs) EmitRun(run);
            }
        }
        return;
    }
    FlushParagraph();
    doc_.blocks.push_back(std::move(table));
}

// ===== VERBATIM =====

void Reader::ParseVerbatimEnvironment(const std::string& name) {
    std::string opt, language;
    if (name == "minted") { ReadOptional(opt); ReadGroup(language); }
    else if (name == "lstlisting") {
        if (ReadOptional(opt)) {
            for (const std::string& kv : SplitCommaList(opt)) {
                if (kv.rfind("language=", 0) == 0) language = TrimCopy(kv.substr(9));
            }
        }
    } else { ReadOptional(opt); }
    // The body starts after the newline that follows \begin{...}.
    if (!AtEnd() && Peek() == '\n') ++pos_;
    bool found = false;
    std::string body = ReadUntil("\\end{" + name + "}", found);
    if (!found) Diag("\\begin{" + name + "} is never closed");
    // Drop the trailing newline before \end.
    while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) body.pop_back();
    if (!inDocument_) return;
    RichDocBlock block;
    block.type = RichBlockType::CodeBlock;
    block.codeLanguage = LowerCopy(language);
    size_t start = 0;
    bool first = true;
    while (start <= body.size()) {
        size_t nl = body.find('\n', start);
        RichTextRun run;
        run.text = body.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!run.text.empty() && run.text.back() == '\r') run.text.pop_back();
        run.code = true;
        run.lineBreakBefore = !first;
        first = false;
        block.runs.push_back(run);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    EmitStandaloneBlock(std::move(block));
}

// ===== MATH =====

void Reader::ParseMathEnvironment(const std::string& name, bool keepEnvironment, bool numbered) {
    std::string arg;
    if (name.rfind("alignat", 0) == 0) ReadGroup(arg);     // column count stays in the source
    bool found = false;
    std::string body = ReadUntil("\\end{" + name + "}", found);
    if (!found) Diag("\\begin{" + name + "} is never closed");
    // Equation numbers: one per row of a numbered environment, minus \notag rows.
    if (numbered) {
        int rows = 1;
        for (size_t i = 0; (i = body.find("\\\\", i)) != std::string::npos; i += 2) ++rows;
        for (size_t i = 0; (i = body.find("\\notag", i)) != std::string::npos; i += 6) --rows;
        for (size_t i = 0; (i = body.find("\\nonumber", i)) != std::string::npos; i += 9) --rows;
        int first = equationCounter_ + 1;
        equationCounter_ += std::max(1, rows);
        // Each \label takes the number of the row it sits in.
        int row = 0;
        for (size_t i = 0; i < body.size(); ++i) {
            if (body.compare(i, 2, "\\\\") == 0) { ++row; ++i; continue; }
            if (body.compare(i, 6, "\\label") == 0) {
                size_t k = i + 6;
                while (k < body.size() && IsSpaceChar(body[k])) ++k;
                std::string key;
                if (ReadRawGroupAt(body, k, key)) {
                    labels_[TrimCopy(key)] = std::to_string(std::min(first + row, equationCounter_));
                    i = k - 1;
                }
            }
        }
        lastRefValue_ = std::to_string(first);
    }
    if (name == "math") { EmitMathRun(body); return; }
    if (keepEnvironment) {
        std::string envName = name;
        std::string open = "\\begin{" + envName + "}" + (arg.empty() ? "" : "{" + arg + "}");
        EmitMathBlock(open + body + "\\end{" + envName + "}");
    } else {
        EmitMathBlock(body);
    }
}

void Reader::ParseInlineMath(const std::string& closer) {
    std::string body;
    for (;;) {
        if (AtEnd()) { Diag("inline math is never closed"); break; }
        if (src_.compare(pos_, closer.size(), closer) == 0) { pos_ += closer.size(); break; }
        if (Peek() == '\\' && pos_ + 1 < src_.size()) {
            body.push_back(src_[pos_]);
            body.push_back(src_[pos_ + 1]);
            pos_ += 2;
            continue;
        }
        if (Peek() == '%') { SkipComment(); continue; }
        body.push_back(src_[pos_++]);
    }
    EmitMathRun(body);
}

void Reader::ParseDisplayMath(const std::string& closer) {
    bool found = false;
    std::string body = ReadUntil(closer, found);
    if (!found) Diag("display math is never closed");
    EmitMathBlock(body);
}

// ===== SECTIONING, CAPTIONS, IMAGES, FOOTNOTES, ITEMS =====

void Reader::ParseSection(const std::string& name, int level, bool starred) {
    (void)name;
    std::string shortTitle;
    ReadOptional(shortTitle);
    if (!inDocument_) { ReadArgument(); return; }
    std::string number;
    if (!starred && options_.numberSections && level >= 1 && level <= 4) {
        int idx = level - 1;                     // chapter 0 … subsubsection 3
        if (idx == 0) hasChapters_ = true;
        ++counters_[idx];
        for (int k = idx + 1; k < 4; ++k) counters_[k] = 0;
        if (idx == 0) { figureCounter_ = 0; tableCounter_ = 0; theoremCounters_.clear(); }
        number = SectionNumber(idx);
        lastRefValue_ = number;
    }
    // Heading levels: part/chapter 1, section 2, subsection 3, subsubsection 4,
    // paragraph 5, subparagraph 6.
    int headingLevel = std::clamp(level == 0 ? 1 : level, 1, 6);
    FlushParagraph();
    if (capture_) {
        // A heading inside a cell or note: a bold line.
        EmitLineBreak();
        if (!number.empty()) EmitText(number + " ");
        std::string title;
        if (ReadGroup(title)) Splice("{\\bfseries " + title + "}");
        return;
    }
    BeginBlock(RichBlockType::Heading, headingLevel);
    if (!number.empty()) EmitText(number + " ");
    std::string title;
    if (ReadGroup(title)) {
        Splice("{" + title + "}\\UCEndBlock{}");
    } else {
        Splice("\\UCEndBlock{}");
    }
}

void Reader::ParseCaption() {
    std::string shortCaption;
    ReadOptional(shortCaption);
    std::string kind = "Figure";
    for (auto it = envs_.rbegin(); it != envs_.rend(); ++it) {
        if (it->kind == EnvKind::Float) { kind = it->floatKind; break; }
    }
    int number = kind == "Table" ? ++tableCounter_ : ++figureCounter_;
    std::string numberText = (hasChapters_ && counters_[0] > 0 ? std::to_string(counters_[0]) + "." : "") +
                             std::to_string(number);
    lastRefValue_ = numberText;
    FlushParagraph();
    if (capture_) { EmitLineBreak(); }
    else {
        BeginBlock(RichBlockType::Paragraph);
        cur_.align = RichTextAlign::Center;
    }
    RichTextRun label;
    label.text = kind + " " + numberText + ": ";
    label.bold = true;
    EmitRun(label);
    std::string text;
    ReadGroup(text);
    Splice("{" + text + "}" + (capture_ ? "" : "\\UCEndBlock{}"));
}

void Reader::ParseIncludeGraphics() {
    std::string opt, file;
    ReadOptional(opt);
    ReadGroup(file);
    ReadOptional(opt);   // \includegraphics{file}[opts] (obsolete order) — tolerated
    file = TrimCopy(file);
    if (!inDocument_) return;

    RichDocBlock block;
    block.type = RichBlockType::Image;
    block.align = CurrentAlign();
    block.imageAltText = std::filesystem::path(file).filename().string();

    // Resolve the file against the graphics paths and the base directory.
    std::vector<std::string> dirs = graphicsPaths_;
    dirs.insert(dirs.begin(), options_.baseDirectory);
    static const char* kExtensions[] = {"", ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".svg", ".webp", ".pdf", ".eps"};
    std::filesystem::path found;
    for (const std::string& dir : dirs) {
        for (const char* ext : kExtensions) {
            std::filesystem::path candidate = std::filesystem::path(file + ext);
            if (candidate.is_relative()) candidate = std::filesystem::path(dir) / candidate;
            std::error_code ec;
            if (std::filesystem::is_regular_file(candidate, ec)) { found = candidate; break; }
        }
        if (!found.empty()) break;
    }
    float widthPt = 0.0f, heightPt = 0.0f, scale = 0.0f;
    for (const std::string& kv : SplitCommaList(opt)) {
        size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        std::string key = TrimCopy(kv.substr(0, eq)), value = TrimCopy(kv.substr(eq + 1));
        if (key == "width") widthPt = ParseTeXLengthPt(value);
        else if (key == "height") heightPt = ParseTeXLengthPt(value);
        else if (key == "scale") scale = std::strtof(value.c_str(), nullptr);
    }
    if (!found.empty()) {
        block.imageAltText = found.filename().string();
        std::ifstream in(found, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!data.empty()) {
            block.mediaIndex = doc_.AddMedia(found.filename().string(),
                                             UCRichDocument::MimeTypeForImageName(found.filename().string()),
                                             std::move(data));
            int px = 0, py = 0;
            if (UCRichDocument::SniffImagePixelSize(doc_.media[static_cast<size_t>(block.mediaIndex)].data, px, py) && px > 0 && py > 0) {
                const float aspect = static_cast<float>(py) / static_cast<float>(px);
                if (widthPt > 0 && heightPt <= 0) heightPt = widthPt * aspect;
                else if (heightPt > 0 && widthPt <= 0) widthPt = heightPt / aspect;
                else if (widthPt <= 0 && heightPt <= 0) {
                    const float s = scale > 0 ? scale : 1.0f;
                    widthPt = static_cast<float>(px) * 72.0f / 96.0f * s;
                    heightPt = static_cast<float>(py) * 72.0f / 96.0f * s;
                }
            }
        }
    } else {
        Diag("image \"" + file + "\" not found (kept as a placeholder)");
    }
    block.imageWidthPt = widthPt;
    block.imageHeightPt = heightPt;
    if (capture_) {
        EmitText("[" + block.imageAltText + "]");
        return;
    }
    FlushParagraph();
    doc_.blocks.push_back(std::move(block));
}

void Reader::ParseFootnote() {
    std::string opt;
    ReadOptional(opt);
    std::string text;
    if (!ReadGroup(text)) return;
    if (!inDocument_) return;
    const int number = ++footnoteCounter_;
    RichTextRun mark;
    mark.text = std::to_string(number);
    mark.superscript = true;
    EmitRun(mark);
    // The note's own text is parsed in a capture, then stored for the end.
    std::vector<RichTextRun>* outerCapture = capture_;
    RichDocBlock outerBlock = cur_;
    bool outerActive = blockActive_, outerBreak = pendingLineBreak_, outerSpace = pendingSpace_;
    std::vector<RichTextRun> note;
    capture_ = &note;
    blockActive_ = false; pendingLineBreak_ = false; pendingSpace_ = false;
    PushScope();
    scope().style = TextStyle{};
    Splice(text + "}");
    ParseContent(Stop::CloseBrace);
    FlushParagraph();
    PopScope();
    capture_ = outerCapture;
    cur_ = outerBlock;
    blockActive_ = outerActive; pendingLineBreak_ = outerBreak; pendingSpace_ = outerSpace;
    footnotes_.push_back(std::move(note));
}

void Reader::ParseItem() {
    std::string label;
    bool hasLabel = ReadOptional(label);
    FlushParagraph();
    EnvFrame* list = nullptr;
    for (auto it = envs_.rbegin(); it != envs_.rend(); ++it) {
        if (it->kind == EnvKind::List) { list = &*it; break; }
    }
    if (!list) {
        Diag("\\item outside a list");
        EnsureBlock();
        if (hasLabel) Splice(label + " ");
        return;
    }
    list->itemOpen = true;
    ++list->itemCounter;
    if (list->ordered) lastRefValue_ = std::to_string(list->itemCounter);
    EnsureBlock();
    if (list->description) {
        if (hasLabel) Splice("{\\bfseries " + label + "} ");
    } else if (hasLabel && !label.empty()) {
        Splice(label + " ");
    }
}

// ===== MACROS, INPUT, TITLE, VERB, ACCENTS, LABELS =====

void Reader::ParseMacroDefinition(const std::string& name) {
    // \newcommand{\foo}[n][default]{body}, \newcommand\foo{...}, \def\foo#1{...}
    const size_t defStart = pos_ - name.size() - 1;
    ReadStar();
    Macro m;
    std::string macroName;
    SkipInlineSpaces();
    if (Peek() == '{') {
        std::string g; ReadGroup(g);
        macroName = TrimCopy(g);
    } else if (Peek() == '\\') {
        ++pos_;
        macroName = "\\" + ReadCommandName();
    } else {
        Diag("\\" + name + " without a command name");
        return;
    }
    if (!macroName.empty() && macroName[0] == '\\') macroName = macroName.substr(1);
    if (name == "def" || name == "gdef" || name == "edef" || name == "xdef" || name == "let") {
        if (name == "let") {
            // \let\a=\b or \let\a\b: alias.
            SkipInlineSpaces();
            if (Peek() == '=') ++pos_;
            SkipInlineSpaces();
            if (Peek() == '\\') {
                ++pos_;
                std::string target = ReadCommandName();
                auto it = macros_.find(target);
                if (it != macros_.end()) macros_[macroName] = it->second;
                else { m.body = "\\" + target; macros_[macroName] = m; }
            }
            return;
        }
        // Parameter text: #1#2 … up to the brace.
        int params = 0;
        while (!AtEnd() && Peek() != '{') {
            if (Peek() == '#' && std::isdigit(static_cast<unsigned char>(Peek(1)))) { ++params; pos_ += 2; }
            else ++pos_;
        }
        m.argCount = params;
        ReadGroup(m.body);
    } else {
        std::string n;
        if (ReadOptional(n)) m.argCount = std::atoi(TrimCopy(n).c_str());
        std::string def;
        if (ReadOptional(def)) { m.hasOptional = true; m.optionalDefault = def; }
        if (!ReadGroup(m.body)) {
            // \newcommand\foo\bar form
            m.body = ReadArgument();
        }
    }
    if (name == "providecommand" && macros_.count(macroName)) return;
    m.rawDefinition = src_.substr(defStart, pos_ - defStart) + "\n";
    macros_[macroName] = m;
    // Only definitions a formula could need go to the math prelude; the
    // engine has the macro splicer for \newcommand and \def.
    if (name != "let") mathPrelude_.push_back(m.rawDefinition);
}

void Reader::ParseEnvironmentDefinition() {
    ReadStar();
    std::string envName;
    ReadGroup(envName);
    envName = TrimCopy(envName);
    EnvironmentMacro m;
    std::string n;
    if (ReadOptional(n)) m.argCount = std::atoi(TrimCopy(n).c_str());
    std::string def;
    if (ReadOptional(def)) { m.hasOptional = true; m.optionalDefault = def; }
    ReadGroup(m.beginBody);
    ReadGroup(m.endBody);
    if (!envName.empty()) userEnvs_[envName] = m;
}

void Reader::ParseNewTheorem() {
    ReadStar();
    std::string envName, sharedCounter, label, within;
    ReadGroup(envName);
    bool shared = ReadOptional(sharedCounter);
    ReadGroup(label);
    ReadOptional(within);
    TheoremKind kind;
    kind.label = TrimCopy(label);
    kind.counter = shared ? TrimCopy(sharedCounter) : TrimCopy(envName);
    theorems_[TrimCopy(envName)] = kind;
}

void Reader::ParseDefineColor() {
    std::string name, model, spec;
    ReadGroup(name); ReadGroup(model); ReadGroup(spec);
    name = TrimCopy(name); model = LowerCopy(TrimCopy(model)); spec = TrimCopy(spec);
    std::vector<std::string> parts = SplitCommaList(spec);
    std::string hex;
    if (model == "rgb" && parts.size() == 3) {
        hex = HexColor(static_cast<int>(std::strtof(parts[0].c_str(), nullptr) * 255.0f + 0.5f),
                       static_cast<int>(std::strtof(parts[1].c_str(), nullptr) * 255.0f + 0.5f),
                       static_cast<int>(std::strtof(parts[2].c_str(), nullptr) * 255.0f + 0.5f));
    } else if (model == "rgb255" || (model == "RGB" || model == "rgb") ) {
        if (parts.size() == 3) hex = HexColor(std::atoi(parts[0].c_str()), std::atoi(parts[1].c_str()), std::atoi(parts[2].c_str()));
    } else if (model == "html" && spec.size() == 6) {
        hex = "#" + spec;
    } else if (model == "gray" && parts.size() == 1) {
        int g = static_cast<int>(std::strtof(parts[0].c_str(), nullptr) * 255.0f + 0.5f);
        hex = HexColor(g, g, g);
    } else if (model == "cmyk" && parts.size() == 4) {
        float c = std::strtof(parts[0].c_str(), nullptr), mm = std::strtof(parts[1].c_str(), nullptr);
        float y = std::strtof(parts[2].c_str(), nullptr), k = std::strtof(parts[3].c_str(), nullptr);
        hex = HexColor(static_cast<int>(255.0f * (1 - c) * (1 - k) + 0.5f),
                       static_cast<int>(255.0f * (1 - mm) * (1 - k) + 0.5f),
                       static_cast<int>(255.0f * (1 - y) * (1 - k) + 0.5f));
    } else if (model == "named") {
        hex = ResolveColor(spec);
    }
    if (!hex.empty()) colors_[name] = hex;
}

bool Reader::ExpandMacro(const std::string& name) {
    auto it = macros_.find(name);
    if (it == macros_.end()) return false;
    if (++expansions_ > 20000) {
        Diag("macro expansion limit reached at \\" + name);
        return true;
    }
    const Macro& m = it->second;
    std::vector<std::string> args;
    if (m.hasOptional) {
        std::string opt;
        args.push_back(ReadOptional(opt) ? opt : m.optionalDefault);
    }
    for (int i = static_cast<int>(args.size()); i < m.argCount; ++i) args.push_back(ReadArgument());
    std::string out;
    for (size_t i = 0; i < m.body.size(); ++i) {
        if (m.body[i] == '#' && i + 1 < m.body.size()) {
            if (std::isdigit(static_cast<unsigned char>(m.body[i + 1]))) {
                int k = m.body[i + 1] - '1';
                if (k >= 0 && k < static_cast<int>(args.size())) out += args[k];
                ++i;
                continue;
            }
            if (m.body[i + 1] == '#') { out.push_back('#'); ++i; continue; }
        }
        out.push_back(m.body[i]);
    }
    Splice(out);
    return true;
}

void Reader::ParseInput(const std::string& name) {
    std::string file;
    if (!ReadGroup(file)) {
        // \input file  (space terminated)
        SkipInlineSpaces();
        while (!AtEnd() && !IsSpaceChar(Peek()) && Peek() != '\\' && Peek() != '{') file.push_back(src_[pos_++]);
    }
    file = TrimCopy(file);
    if (file.empty()) return;
    if (name == "include") FlushParagraph();
    if (++inputFiles_ > options_.maxInputFiles) {
        Diag("\\" + name + " limit reached; \"" + file + "\" skipped");
        return;
    }
    std::filesystem::path path(file);
    if (path.is_relative()) path = std::filesystem::path(options_.baseDirectory) / path;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) && path.extension().empty()) path += ".tex";
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        Diag("\\" + name + " file \"" + file + "\" not found");
        return;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (char& c : content) if (c == '\r') c = ' ';
    Splice(content);
}

void Reader::ParseMakeTitle() {
    if (!inDocument_) return;
    FlushParagraph();
    std::string spliced;
    if (!titleRaw_.empty()) spliced += "\\UCTitle{" + titleRaw_ + "}";
    if (!authorRaw_.empty()) spliced += "\\UCAuthor{" + authorRaw_ + "}";
    if (dateGiven_ ? !TrimCopy(dateRaw_).empty() : true) {
        spliced += "\\UCDate{" + (dateGiven_ ? dateRaw_ : std::string("\\today")) + "}";
    }
    Splice(spliced);
}

void Reader::ParseVerb(bool star) {
    if (AtEnd()) return;
    const char delim = src_[pos_++];
    std::string text;
    while (!AtEnd() && Peek() != delim && Peek() != '\n') text.push_back(src_[pos_++]);
    if (!AtEnd() && Peek() == delim) ++pos_;
    if (star) {
        std::string shown;
        for (char c : text) { if (c == ' ') shown += "␣"; else shown.push_back(c); }
        text = shown;
    }
    PushScope();
    scope().style.code = true;
    EmitText(text);
    PopScope();
}

void Reader::ParseAccent(const std::string& accent) {
    std::string arg = ReadArgument();
    // The argument may itself be a symbol command (\'\i).
    if (!arg.empty() && arg[0] == '\\') {
        const char* sym = TextSymbol(arg.substr(1));
        arg = sym ? sym : std::string();
    }
    // Strip braces the argument reader already removed; an empty base means
    // the accent stands alone (rare) — keep it as its combining mark.
    if (arg.empty()) { EmitText(Utf8(CombiningMarkFor(accent))); return; }
    EmitText(ApplyAccent(accent, arg));
}

void Reader::ParseTextCommandWithStyle(const std::function<void(TextStyle&)>& apply) {
    // \textbf{...}: parse the argument as a group with the style applied.
    std::string inner;
    if (!ReadGroup(inner)) {
        std::string tok = ReadArgument();
        inner = tok;
    }
    PushScope();
    apply(scope().style);
    Splice(inner + "\\UCPopScope{}");
}

void Reader::SkipUnknownArguments() {
    ReadStar();
    std::string opt;
    while (ReadOptional(opt)) {}
}

void Reader::ParseLabel() {
    std::string key;
    ReadGroup(key);
    key = TrimCopy(key);
    if (!key.empty() && !lastRefValue_.empty()) labels_[key] = lastRefValue_;
}

// ===== COMMAND DISPATCH =====

bool Reader::HandleCommand(const std::string& name, Stop stop, StopReason& reason) {
    reason = StopReason::End;
    (void)stop;

    // ----- internal helpers spliced by the reader itself -----
    // Internal markers are spliced as "\UCName{}" so the control word does
    // not swallow the source's own whitespace; the empty group is consumed.
    if (name.rfind("UC", 0) == 0 && Peek() == '{' && Peek(1) == '}') pos_ += 2;
    if (name == "UCEndBlock") { FlushParagraph(); return true; }
    if (name == "UCPopScope") { PopScope(); return true; }
    if (name == "UCTheoremDot") {
        bool proof = !envs_.empty() && envs_.back().proof;
        RichTextRun dot; dot.text = ". "; dot.bold = !proof; dot.italic = proof;
        EmitRun(dot);
        return true;
    }
    if (name == "UCTheoremBody") {
        if (!envs_.empty() && envs_.back().kind == EnvKind::Theorem && !envs_.back().proof) scope().style.italic = true;
        return true;
    }
    if (name == "UCTitle") {
        std::string t; ReadGroup(t);
        BeginBlock(RichBlockType::Heading, 1);
        cur_.align = RichTextAlign::Center;
        if (doc_.metadata.title.empty()) doc_.metadata.title = TrimCopy(t);
        Splice("{" + t + "}\\UCEndBlock{}");
        return true;
    }
    if (name == "UCAuthor") {
        std::string a; ReadGroup(a);
        BeginBlock(RichBlockType::Paragraph);
        cur_.align = RichTextAlign::Center;
        std::string authors;
        size_t at = 0, prev = 0;
        while ((at = a.find("\\and", prev)) != std::string::npos) {
            authors += TrimCopy(a.substr(prev, at - prev)) + ", ";
            prev = at + 4;
        }
        authors += TrimCopy(a.substr(prev));
        if (doc_.metadata.author.empty()) doc_.metadata.author = TrimCopy(authors);
        Splice("{" + authors + "}\\UCEndBlock{}");
        return true;
    }
    if (name == "UCDate") {
        std::string d; ReadGroup(d);
        BeginBlock(RichBlockType::Paragraph);
        cur_.align = RichTextAlign::Center;
        Splice("{" + d + "}\\UCEndBlock{}");
        return true;
    }

    // ----- user macros first: a document may redefine anything -----
    if (ExpandMacro(name)) return true;

    // ----- symbols -----
    if (const char* sym = TextSymbol(name)) {
        if (name == "i" || name == "j" || name == "o" || name == "O" || name == "l" || name == "L" ||
            name == "aa" || name == "AA" || name == "ss" || name == "ae" || name == "AE" ||
            name == "oe" || name == "OE" || name == "dh" || name == "DH" || name == "th" || name == "TH") {
            // \ss{} style empty group after a letter command is common.
            size_t p = pos_;
            if (p + 1 < src_.size() && src_[p] == '{' && src_[p + 1] == '}') pos_ = p + 2;
        }
        EmitText(sym);
        return true;
    }
    if (name == "today") { EmitText(TodayString()); return true; }
    if (name == "\\") { EmitLineBreak(); return true; }
    if (name == "newline" || name == "linebreak") { std::string o; ReadOptional(o); EmitLineBreak(); return true; }
    if (name == "-" || name == "/" || name == "@" || name == "relax" || name == "protect" ||
        name == "ignorespaces" || name == "nolinebreak" || name == "nopagebreak" || name == "sloppy" ||
        name == "fussy" || name == "frenchspacing" || name == "nonfrenchspacing" || name == "noindent" ||
        name == "indent" || name == "allowbreak" || name == "hfill" || name == "hfil" || name == "vfill" ||
        name == "hss" || name == "strut" || name == "mathstrut" || name == "null" || name == "leavevmode" ||
        name == "unskip" || name == "centering" || name == "raggedright" || name == "raggedleft" ||
        name == "justifying" || name == "onecolumn" || name == "twocolumn" || name == "samepage" ||
        name == "appendix" || name == "mainmatter" || name == "frontmatter" || name == "backmatter" ||
        name == "clearpage" || name == "cleardoublepage" || name == "newpage" || name == "pagebreak" ||
        name == "par" || name == "medskip" || name == "smallskip" || name == "bigskip" ||
        name == "tableofcontents" || name == "listoffigures" || name == "listoftables" ||
        name == "printbibliography" || name == "printindex" || name == "printglossaries" ||
        name == "makeindex" || name == "makeglossaries" || name == "flushbottom" || name == "raggedbottom" ||
        name == "maketitle" || name == "hrule" || name == "hrulefill" || name == "dotfill" ||
        name == "nocite" || name == "makeatletter" || name == "makeatother" || name == "endinput" ||
        name == "normalfont" || name == "normalsize" || name == "textnormal" || name == "displaystyle") {
        if (name == "hfill") { EmitSpace(); return true; }
        if (name == "centering") { scope().align = RichTextAlign::Center; if (blockActive_) cur_.align = RichTextAlign::Center; return true; }
        if (name == "raggedright") { scope().align = RichTextAlign::Left; return true; }
        if (name == "raggedleft") { scope().align = RichTextAlign::Right; return true; }
        if (name == "appendix") { appendix_ = true; counters_[hasChapters_ ? 0 : 1] = 0; return true; }
        if (name == "par" || name == "medskip" || name == "smallskip" || name == "bigskip") { FlushParagraph(); return true; }
        if (name == "newpage" || name == "clearpage" || name == "cleardoublepage" || name == "pagebreak") {
            std::string o; ReadOptional(o);
            if (name != "pagebreak") { RichDocBlock b; b.type = RichBlockType::PageBreak; EmitStandaloneBlock(std::move(b)); }
            return true;
        }
        if (name == "hrule" || name == "hrulefill") {
            if (!blockActive_) { RichDocBlock b; b.type = RichBlockType::HorizontalRule; EmitStandaloneBlock(std::move(b)); }
            return true;
        }
        if (name == "maketitle") { ParseMakeTitle(); return true; }
        if (name == "nocite") { std::string k; ReadGroup(k); return true; }
        if (name == "normalfont") { scope().style.bold = false; scope().style.italic = false; scope().style.code = false; return true; }
        if (name == "normalsize") { scope().style.fontSizePt = 0.0f; return true; }
        if (name == "textnormal") { ParseTextCommandWithStyle([](TextStyle& s) { s.bold = false; s.italic = false; s.code = false; }); return true; }
        if (name == "endinput") { finished_ = true; return true; }
        return true;
    }

    // ----- preamble and settings (arguments swallowed) -----
    if (name == "documentclass" || name == "usepackage" || name == "RequirePackage" ||
        name == "usetikzlibrary" || name == "pgfplotsset" || name == "lstset" || name == "captionsetup" ||
        name == "hypersetup" || name == "geometry" || name == "setlength" || name == "addtolength" ||
        name == "setcounter" || name == "addtocounter" || name == "stepcounter" || name == "refstepcounter" ||
        name == "pagestyle" || name == "thispagestyle" || name == "pagenumbering" || name == "bibliographystyle" ||
        name == "bibliography" || name == "addbibresource" || name == "theoremstyle" || name == "numberwithin" ||
        name == "setmainfont" || name == "setsansfont" || name == "setmonofont" || name == "setmathfont" ||
        name == "newcolumntype" || name == "selectlanguage" || name == "linespread" || name == "hyphenation" ||
        name == "includeonly" || name == "listfiles" || name == "enlargethispage" || name == "vspace" ||
        name == "hspace" || name == "addvspace" || name == "setstretch" || name == "onehalfspacing" ||
        name == "doublespacing" || name == "singlespacing" || name == "index" || name == "glossary" ||
        name == "addcontentsline" || name == "markboth" || name == "markright" || name == "lhead" ||
        name == "rhead" || name == "chead" || name == "lfoot" || name == "rfoot" || name == "cfoot" ||
        name == "fancyhf" || name == "fancyhead" || name == "fancyfoot" || name == "renewenvironment" ||
        name == "DeclareGraphicsExtensions" || name == "graphicspath" || name == "newlength" ||
        name == "newcounter" || name == "newsavebox" || name == "savebox" || name == "usebox" ||
        name == "vskip" || name == "hskip" || name == "kern" || name == "penalty" || name == "parindent" ||
        name == "parskip" || name == "baselineskip" || name == "columnsep" || name == "columnseprule" ||
        name == "phantom" || name == "vphantom" || name == "hphantom" || name == "label" || name == "PassOptionsToPackage" ||
        name == "title" || name == "author" || name == "date" || name == "institute" || name == "subtitle" ||
        name == "DeclareMathOperator" || name == "newtheorem" || name == "definecolor" || name == "colorlet" ||
        name == "newcommand" || name == "renewcommand" || name == "providecommand" || name == "def" ||
        name == "gdef" || name == "edef" || name == "xdef" || name == "let" || name == "newenvironment" ||
        name == "DeclareRobustCommand" || name == "DeclareTextCommand" || name == "input" || name == "include" ||
        name == "subfile" || name == "documentstyle" || name == "affil" || name == "affiliation" ||
        name == "email" || name == "keywords" || name == "thanks" || name == "footnotemark" ||
        name == "footnotetext" || name == "MakeUppercase" || name == "MakeLowercase" || name == "uppercase" ||
        name == "lowercase" || name == "setlist" || name == "setitemize" || name == "setenumerate" ||
        name == "bibitem" || name == "newblock" || name == "titleformat" || name == "titlespacing" ||
        name == "setcounter" || name == "floatname" || name == "restylefloat" || name == "newfloat" ||
        name == "DeclareCaptionFormat" || name == "AtBeginDocument" || name == "AtEndDocument" ||
        name == "renewcommand*" || name == "newcommand*" || name == "counterwithin" || name == "counterwithout") {
        if (name == "newcommand" || name == "renewcommand" || name == "providecommand" ||
            name == "DeclareRobustCommand" || name == "DeclareTextCommand" ||
            name == "def" || name == "gdef" || name == "edef" || name == "xdef" || name == "let") {
            ParseMacroDefinition(name);
            return true;
        }
        if (name == "newenvironment" || name == "renewenvironment") { ParseEnvironmentDefinition(); return true; }
        if (name == "newtheorem") { ParseNewTheorem(); return true; }
        if (name == "definecolor") { ParseDefineColor(); return true; }
        if (name == "colorlet") {
            std::string n, spec; ReadGroup(n); ReadOptional(spec); ReadGroup(spec);
            std::string hex = ResolveColor(spec);
            if (!hex.empty()) colors_[TrimCopy(n)] = hex;
            return true;
        }
        if (name == "DeclareMathOperator") {
            size_t defStart = pos_ - name.size() - 1;
            ReadStar();
            std::string a, b; ReadGroup(a); ReadGroup(b);
            mathPrelude_.push_back(src_.substr(defStart, pos_ - defStart) + "\n");
            return true;
        }
        if (name == "input" || name == "include" || name == "subfile") { ParseInput(name); return true; }
        if (name == "title") { ReadOptional(titleRaw_); ReadGroup(titleRaw_); return true; }
        if (name == "author") { ReadOptional(authorRaw_); ReadGroup(authorRaw_); return true; }
        if (name == "date") { ReadGroup(dateRaw_); dateGiven_ = true; return true; }
        if (name == "thanks") { ParseFootnote(); return true; }
        if (name == "footnotemark") {
            std::string o; ReadOptional(o);
            RichTextRun mark; mark.text = std::to_string(footnoteCounter_ + 1); mark.superscript = true;
            EmitRun(mark);
            return true;
        }
        if (name == "footnotetext") {
            // Numbered by the preceding \footnotemark.
            std::string o; ReadOptional(o);
            ParseFootnote();
            return true;
        }
        if (name == "label") { ParseLabel(); return true; }
        if (name == "graphicspath") {
            std::string paths; ReadGroup(paths);
            size_t i = 0;
            while (i < paths.size()) {
                std::string dir;
                if (paths[i] == '{' && ReadRawGroupAt(paths, i, dir)) {
                    std::filesystem::path p(TrimCopy(dir));
                    if (p.is_relative()) p = std::filesystem::path(options_.baseDirectory) / p;
                    graphicsPaths_.push_back(p.string());
                } else {
                    ++i;
                }
            }
            return true;
        }
        if (name == "MakeUppercase" || name == "uppercase" || name == "MakeLowercase" || name == "lowercase") {
            std::string g; ReadGroup(g);
            const bool upper = name == "MakeUppercase" || name == "uppercase";
            std::string out;
            for (char c : g) out.push_back(static_cast<char>(upper ? std::toupper(static_cast<unsigned char>(c))
                                                                   : std::tolower(static_cast<unsigned char>(c))));
            Splice(out);
            return true;
        }
        if (name == "bibitem") {
            std::string label, key;
            bool hasLabel = ReadOptional(label);
            ReadGroup(key);
            key = TrimCopy(key);
            int n = ++bibitemCounter_;
            citations_[key] = n;
            FlushParagraph();
            EnvFrame* list = nullptr;
            for (auto it = envs_.rbegin(); it != envs_.rend(); ++it) if (it->kind == EnvKind::List) { list = &*it; break; }
            if (list) { list->itemOpen = true; ++list->itemCounter; }
            EnsureBlock();
            if (hasLabel) Splice("[" + label + "] ");
            return true;
        }
        if (name == "newblock") { EmitSpace(); return true; }
        if (name == "vspace" || name == "hspace" || name == "addvspace" || name == "vskip" || name == "hskip" || name == "kern") {
            ReadStar();
            std::string len;
            if (!ReadGroup(len)) { while (!AtEnd() && !IsSpaceChar(Peek()) && Peek() != '\\' && Peek() != '{' && Peek() != '}') ++pos_; }
            if (name == "hspace" || name == "hskip") EmitSpace();
            return true;
        }
        if (name == "phantom" || name == "hphantom") { std::string g; ReadGroup(g); EmitSpace(); return true; }
        if (name == "vphantom") { std::string g; ReadGroup(g); return true; }
        if (name == "AtBeginDocument" || name == "AtEndDocument") { std::string g; ReadGroup(g); return true; }
        // Everything else: star, optionals and up to three braced arguments.
        ReadStar();
        std::string a;
        while (ReadOptional(a)) {}
        for (int i = 0; i < 3 && ReadGroup(a); ++i) { ReadOptional(a); }
        return true;
    }

    // ----- sectioning -----
    if (name == "part") { bool st = ReadStar(); ParseSection(name, 0, st); return true; }
    if (name == "chapter") { bool st = ReadStar(); ParseSection(name, 1, st); return true; }
    if (name == "section") { bool st = ReadStar(); ParseSection(name, 2, st); return true; }
    if (name == "subsection") { bool st = ReadStar(); ParseSection(name, 3, st); return true; }
    if (name == "subsubsection") { bool st = ReadStar(); ParseSection(name, 4, st); return true; }
    if (name == "paragraph") { ReadStar(); ParseSection(name, 5, true); return true; }
    if (name == "subparagraph") { ReadStar(); ParseSection(name, 6, true); return true; }

    // ----- blocks -----
    if (name == "item") { ParseItem(); return true; }
    if (name == "caption" || name == "captionof" || name == "subcaption") {
        ReadStar();
        if (name == "captionof") { std::string kind; ReadGroup(kind); }
        ParseCaption();
        return true;
    }
    if (name == "includegraphics" || name == "includesvg" || name == "includepdf") {
        ReadStar();
        ParseIncludeGraphics();
        return true;
    }
    if (name == "footnote") { ParseFootnote(); return true; }
    if (name == "verb") { ParseVerb(ReadStar()); return true; }
    if (name == "lstinline" || name == "mintinline" || name == "cppinline" || name == "pyinline") {
        std::string o; ReadOptional(o);
        if (name == "mintinline") { std::string l; ReadGroup(l); }
        if (Peek() == '{') {
            std::string g; ReadGroup(g);
            PushScope(); scope().style.code = true; EmitText(g); PopScope();
        } else {
            ParseVerb(false);
        }
        return true;
    }
    if (name == "rule") {
        ReadStar();
        std::string o, w, h; ReadOptional(o); ReadGroup(w); ReadGroup(h);
        if (!blockActive_ && ParseTeXLengthPt(w) >= kTextWidthPt * 0.5f) {
            RichDocBlock b; b.type = RichBlockType::HorizontalRule; EmitStandaloneBlock(std::move(b));
        }
        return true;
    }
    if (name == "ensuremath") { std::string g; ReadGroup(g); EmitMathRun(g); return true; }
    if (name == "(") { ParseInlineMath("\\)"); return true; }
    if (name == "[") { ParseDisplayMath("\\]"); return true; }
    if (name == "begin" || name == "end") return true;   // handled by the loop

    // ----- accents -----
    if (name == "'" || name == "`" || name == "^" || name == "\"" || name == "~" || name == "=" ||
        name == "." || name == "u" || name == "v" || name == "H" || name == "c" || name == "k" ||
        name == "r" || name == "d" || name == "b" || name == "t") {
        if (name == "t") { EmitText(ReadArgument()); return true; }
        ParseAccent(name);
        return true;
    }

    // ----- inline formatting with an argument -----
    if (name == "textbf") { ParseTextCommandWithStyle([](TextStyle& s) { s.bold = true; }); return true; }
    if (name == "textit" || name == "textsl") { ParseTextCommandWithStyle([](TextStyle& s) { s.italic = true; }); return true; }
    if (name == "emph") { ParseTextCommandWithStyle([](TextStyle& s) { s.italic = !s.italic; }); return true; }
    if (name == "texttt") { ParseTextCommandWithStyle([](TextStyle& s) { s.code = true; }); return true; }
    if (name == "textsc" || name == "textrm" || name == "textsf" || name == "textmd" || name == "textup" ||
        name == "mbox" || name == "hbox" || name == "text" || name == "textsuperscript" || name == "textsubscript" ||
        name == "makebox" || name == "framebox" || name == "fbox" || name == "parbox" || name == "raisebox" ||
        name == "scalebox" || name == "rotatebox" || name == "resizebox" || name == "colorbox" ||
        name == "fcolorbox" || name == "hyperref" || name == "nolinkurl" || name == "path" ||
        name == "textcolor" || name == "underline" || name == "uline" || name == "uuline" || name == "sout" ||
        name == "st" || name == "hl" || name == "highlight" || name == "textbox" || name == "marginpar" ||
        name == "smash" || name == "intertext" || name == "shortstack" || name == "adjustbox" ||
        name == "textsubscript") {
        std::string opt;
        if (name == "makebox" || name == "framebox" || name == "parbox" || name == "raisebox" || name == "marginpar" || name == "adjustbox") {
            while (ReadOptional(opt)) {}
        }
        if (name == "parbox") { std::string w; ReadGroup(w); while (ReadOptional(opt)) {} }
        if (name == "raisebox" || name == "scalebox" || name == "rotatebox") { std::string a; ReadOptional(a); ReadGroup(a); ReadOptional(a); }
        if (name == "resizebox") { std::string a; ReadStar(); ReadGroup(a); ReadGroup(a); }
        if (name == "adjustbox") { std::string a; ReadGroup(a); }
        if (name == "hyperref") { std::string l; ReadOptional(l); }
        std::string color;
        if (name == "textcolor" || name == "colorbox" || name == "fcolorbox") {
            std::string model, spec;
            ReadOptional(model);
            ReadGroup(spec);
            if (name == "fcolorbox") { ReadOptional(model); ReadGroup(spec); }   // frame colour, then fill
            color = ResolveColor(spec);
            if (name != "textcolor") color.clear();   // box fills do not change the text colour
        }
        if (name == "marginpar") {
            std::string g; ReadGroup(g);
            Splice(" (" + g + ") ");
            return true;
        }
        if (name == "intertext") { std::string g; ReadGroup(g); Splice(g); return true; }
        auto apply = [name, color](TextStyle& s) {
            if (name == "textsuperscript") s.superscript = true;
            else if (name == "textsubscript") s.subscript = true;
            else if (name == "underline" || name == "uline" || name == "uuline") s.underline = true;
            else if (name == "sout" || name == "st") s.strikethrough = true;
            else if (name == "textcolor" && !color.empty()) s.color = color;
            else if (name == "nolinkurl" || name == "path") s.code = true;
        };
        ParseTextCommandWithStyle(apply);
        return true;
    }
    if (name == "href") {
        std::string o, url, text;
        ReadOptional(o); ReadGroup(url); ReadGroup(text);
        PushScope();
        scope().style.linkTarget = TrimCopy(url);
        Splice(text + "\\UCPopScope{}");
        return true;
    }
    if (name == "url") {
        std::string o, url;
        ReadOptional(o);
        if (!ReadGroup(url)) { ParseVerb(false); return true; }
        url = TrimCopy(url);
        RichTextRun run; run.text = url; run.linkTarget = url; run.code = true;
        EmitRun(run);
        return true;
    }

    // ----- declarations (scope-wide) -----
    if (name == "bfseries" || name == "bf") { scope().style.bold = true; return true; }
    if (name == "mdseries") { scope().style.bold = false; return true; }
    if (name == "itshape" || name == "it" || name == "slshape" || name == "sl") { scope().style.italic = true; return true; }
    if (name == "em") { scope().style.italic = !scope().style.italic; return true; }
    if (name == "upshape") { scope().style.italic = false; return true; }
    if (name == "ttfamily" || name == "tt") { scope().style.code = true; return true; }
    if (name == "rmfamily" || name == "rm" || name == "sffamily" || name == "sf" || name == "scshape" ||
        name == "sc" || name == "cal" || name == "selectfont" || name == "fontfamily" || name == "fontseries" ||
        name == "fontshape" || name == "fontsize") {
        if (name == "fontfamily" || name == "fontseries" || name == "fontshape") { std::string g; ReadGroup(g); }
        if (name == "fontsize") { std::string a, b; ReadGroup(a); ReadGroup(b); scope().style.fontSizePt = ParseTeXLengthPt(a); }
        if (name == "rmfamily" || name == "rm" || name == "sffamily" || name == "sf") scope().style.code = false;
        return true;
    }
    if (name == "color") {
        std::string model, spec;
        ReadOptional(model);
        ReadGroup(spec);
        scope().style.color = ResolveColor(spec);
        return true;
    }
    if (name == "pagecolor" || name == "nopagecolor") { std::string s; ReadOptional(s); ReadGroup(s); return true; }
    if (float pt = SizeCommandPt(name); pt >= 0.0f) { scope().style.fontSizePt = pt; return true; }

    // ----- cross references and citations -----
    if (name == "ref" || name == "pageref" || name == "autoref" || name == "cref" || name == "Cref" ||
        name == "vref" || name == "nameref" || name == "eqref" || name == "labelcref") {
        ReadStar();
        std::string key; ReadGroup(key);
        for (const std::string& k : SplitCommaList(key)) {
            if (name == "eqref") EmitRefMarker('E', k);
            else if (name == "pageref") EmitText("?");
            else EmitRefMarker('R', k);
        }
        return true;
    }
    if (name == "cite" || name == "citep" || name == "citet" || name == "citeauthor" || name == "citeyear" ||
        name == "parencite" || name == "textcite" || name == "autocite" || name == "footcite" ||
        name == "citealp" || name == "citealt" || name == "Cite" || name == "citeyearpar" || name == "fullcite" ||
        name == "citetitle" || name == "supercite" || name == "smartcite") {
        ReadStar();
        std::string o;
        while (ReadOptional(o)) {}
        std::string keys; ReadGroup(keys);
        std::string joined;
        for (const std::string& k : SplitCommaList(keys)) {
            if (!joined.empty()) joined += ", ";
            joined += std::string(1, kRefOpen) + 'C' + k + kRefClose;
        }
        EmitText("[" + joined + "]");
        return true;
    }
    if (name == "and") { EmitText(", "); return true; }

    // ----- unknown: report once, keep the arguments' text -----
    if (!reportedUnknown_.count(name)) {
        reportedUnknown_.insert(name);
        Diag("unknown command \\" + name + " (arguments kept as text)");
    }
    SkipUnknownArguments();
    return true;
}

// ===== DRIVER =====

bool Reader::Run() {
    doc_ = UCRichDocument{};
    hasDocumentEnvironment_ = src_.find("\\begin{document}") != std::string::npos;
    // A fragment without \begin{document} is all body.
    inDocument_ = !hasDocumentEnvironment_;
    ParseContent(Stop::End);
    FlushParagraph();
    // Close whatever is still open (a missing \end{itemize} must not lose text).
    while (!envs_.empty()) {
        if (envs_.back().kind != EnvKind::Document) Diag("\\begin{" + envs_.back().name + "} is never closed");
        envs_.pop_back();
    }
    FinishDocument();
    ResolveReferences();
    return !doc_.blocks.empty();
}

void Reader::FinishDocument() {
    if (footnotes_.empty()) return;
    RichDocBlock rule;
    rule.type = RichBlockType::HorizontalRule;
    doc_.blocks.push_back(rule);
    for (size_t i = 0; i < footnotes_.size(); ++i) {
        RichDocBlock note;
        note.type = RichBlockType::Paragraph;
        RichTextRun mark;
        mark.text = std::to_string(i + 1);
        mark.superscript = true;
        note.runs.push_back(mark);
        RichTextRun space; space.text = " ";
        note.runs.push_back(space);
        for (RichTextRun r : footnotes_[i]) {
            r.lineBreakBefore = false;
            note.runs.push_back(std::move(r));
        }
        doc_.blocks.push_back(std::move(note));
    }
}

void Reader::ResolveReferences() {
    auto resolveText = [&](std::string& text) {
        size_t at = 0;
        while ((at = text.find(kRefOpen, at)) != std::string::npos) {
            size_t close = text.find(kRefClose, at);
            if (close == std::string::npos || close < at + 2) { text.erase(at, 1); continue; }
            const char kind = text[at + 1];
            const std::string key = text.substr(at + 2, close - at - 2);
            std::string value;
            if (kind == 'C') {
                auto it = citations_.find(key);
                value = it != citations_.end() ? std::to_string(it->second) : key;
            } else {
                auto it = labels_.find(key);
                if (it == labels_.end()) {
                    value = "??";
                    if (diags_) diags_->push_back({0, "reference to undefined label \"" + key + "\""});
                } else {
                    value = it->second;
                }
                if (kind == 'E') value = "(" + value + ")";
            }
            text.replace(at, close - at + 1, value);
            at += value.size();
        }
    };
    auto resolveRuns = [&](std::vector<RichTextRun>& runs) {
        for (RichTextRun& r : runs) if (!r.math) resolveText(r.text);
    };
    for (RichDocBlock& block : doc_.blocks) {
        resolveRuns(block.runs);
        for (RichTableRow& row : block.tableRows) {
            for (RichTableCell& cell : row.cells) resolveRuns(cell.runs);
        }
        resolveText(block.imageAltText);
    }
    resolveText(doc_.metadata.title);
    resolveText(doc_.metadata.author);
}

} // namespace

// ===== PUBLIC API =====

bool UltraCanvasLaTeXDocumentReader::Parse(const std::string& source, UCRichDocument& outDocument,
                                           std::vector<LaTeXDocumentDiagnostic>* outDiagnostics,
                                           const LaTeXDocumentReadOptions& options) {
    if (outDiagnostics) outDiagnostics->clear();
    // Normalise line endings and drop a UTF-8 BOM.
    std::string text;
    text.reserve(source.size());
    size_t start = (source.size() >= 3 && source.compare(0, 3, "\xEF\xBB\xBF") == 0) ? 3 : 0;
    for (size_t i = start; i < source.size(); ++i) {
        if (source[i] != '\r') text.push_back(source[i]);
    }
    Reader reader(std::move(text), outDocument, outDiagnostics, options);
    return reader.Run();
}

bool UltraCanvasLaTeXDocumentReader::Load(const std::string& filePath, UCRichDocument& outDocument,
                                          std::string& outError,
                                          std::vector<LaTeXDocumentDiagnostic>* outDiagnostics,
                                          LaTeXDocumentReadOptions options) {
    outError.clear();
    outDocument = UCRichDocument{};
    std::ifstream in(std::filesystem::path(filePath), std::ios::binary);
    if (!in.is_open()) {
        outError = "Cannot open LaTeX document: " + filePath;
        return false;
    }
    std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (options.baseDirectory.empty()) {
        options.baseDirectory = std::filesystem::path(filePath).parent_path().string();
    }
    if (!Parse(source, outDocument, outDiagnostics, options)) {
        outError = "The LaTeX file holds no document content: " + filePath;
        return false;
    }
    if (outDocument.metadata.title.empty()) {
        outDocument.metadata.title = std::filesystem::path(filePath).stem().string();
    }
    return true;
}

bool UltraCanvasLaTeXDocumentReader::LooksLikeLaTeXDocument(const std::string& text) {
    // Skip comments and blank lines; the first real line decides.
    size_t pos = 0;
    int linesChecked = 0;
    while (pos < text.size() && linesChecked < 40) {
        size_t nl = text.find('\n', pos);
        std::string line = TrimCopy(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (line.empty() || line[0] == '%') continue;
        ++linesChecked;
        if (line.rfind("\\documentclass", 0) == 0 || line.rfind("\\begin{document}", 0) == 0 ||
            line.rfind("\\usepackage", 0) == 0 || line.rfind("\\section", 0) == 0 ||
            line.rfind("\\chapter", 0) == 0 || line.rfind("\\documentstyle", 0) == 0 ||
            line.rfind("\\input", 0) == 0 || line.rfind("\\title", 0) == 0) {
            return true;
        }
        if (line[0] != '\\') return false;
    }
    return false;
}

std::string UltraCanvasLaTeXDocumentReader::FormatDiagnostics(
        const std::vector<LaTeXDocumentDiagnostic>& diagnostics) {
    std::string out;
    for (const LaTeXDocumentDiagnostic& d : diagnostics) {
        if (!out.empty()) out.push_back('\n');
        if (d.line > 0) out += "line " + std::to_string(d.line) + ": ";
        out += d.message;
    }
    return out;
}

} // namespace UltraCanvas
