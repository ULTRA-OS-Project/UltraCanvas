// Plugins/Documents/Word/UltraCanvasMathToLatex.cpp
// Converts embedded formula markup to LaTeX: the MathML subset that
// LibreOffice/OpenOffice writes into ODF documents, and the OMML subset that
// Word writes into DOCX (WordprocessingML). Output carries no surrounding $.
// Version: 1.0.0
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework
#include "UltraCanvasMathToLatex.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace UltraCanvas {
namespace WordMath {
namespace {

using tinyxml2::XMLElement;
using tinyxml2::XMLNode;
using tinyxml2::XMLText;

// ---------------------------------------------------------------------------
// Generic helpers
// ---------------------------------------------------------------------------

// tinyxml2 does no namespace processing: "math:mrow" and "mrow" are distinct
// tag names, and documents in the wild use either. Compare local names only.
std::string LocalName(const char* qualifiedName) {
    if (!qualifiedName) return {};
    std::string name(qualifiedName);
    size_t colon = name.rfind(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

// Byte length of the UTF-8 sequence starting with `lead`. Invalid lead bytes
// count as 1 so a malformed input still terminates.
size_t Utf8SeqLen(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

std::string Trim(const std::string& s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
}

// All text content of an element, descending through nested elements so
// wrappers around character data do not lose it.
std::string GatherText(const XMLElement* e) {
    std::string out;
    for (const XMLNode* n = e->FirstChild(); n; n = n->NextSibling()) {
        if (const XMLText* t = n->ToText()) {
            if (t->Value()) out += t->Value();
        } else if (const XMLElement* c = n->ToElement()) {
            out += GatherText(c);
        }
    }
    return out;
}

// Appends `piece` to `out`, inserting a space only when `out` ends in a
// backslash command and `piece` starts with a letter — otherwise the letter
// would be swallowed into the command name (e.g. "\pm" + "x" -> "\pm x",
// but "\pm" + "\sqrt" stays "\pm\sqrt").
void AppendLatex(std::string& out, const std::string& piece) {
    if (piece.empty()) return;
    if (!out.empty() && std::isalpha(static_cast<unsigned char>(piece[0]))) {
        size_t i = out.size();
        while (i > 0 && std::isalpha(static_cast<unsigned char>(out[i - 1]))) --i;
        if (i > 0 && i < out.size() && out[i - 1] == '\\') out += ' ';
    }
    out += piece;
}

// Collapses whitespace runs to a single space and trims the ends. Single
// spaces separating a command from a following letter are preserved.
std::string TidyLatex(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool pendingSpace = false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out += ' ';
            pendingSpace = false;
        }
        out += c;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Character mapping and escaping
// ---------------------------------------------------------------------------

// Escaping applies to literal characters only; generated commands are
// appended verbatim and must never pass through here.
std::string EscapeLatexChar(char c) {
    switch (c) {
        case '#': case '$': case '%': case '&': case '_': case '{': case '}':
            return std::string("\\") + c;
        case '~': return "\\textasciitilde{}";
        case '^': return "\\textasciicircum{}";
        case '\\': return "\\backslash";
        default: return std::string(1, c);
    }
}

// Text-mode variant used inside \text{...}, where \backslash is unavailable.
std::string EscapeTextChar(char c) {
    if (c == '\\') return "\\textbackslash{}";
    return EscapeLatexChar(c);
}

// Operators, relation symbols and Greek letters keyed by their UTF-8 bytes.
// An empty value means the codepoint is dropped (invisible operators).
const std::unordered_map<std::string, std::string>& SymbolMap() {
    static const std::unordered_map<std::string, std::string> map = {
        // Operators / relations
        {"±", "\\pm"},     {"∓", "\\mp"},
        {"×", "\\times"},  {"÷", "\\div"},
        {"⋅", "\\cdot"},   {"·", "\\cdot"},   {"∘", "\\circ"},
        {"≤", "\\le"},     {"≥", "\\ge"},     {"≠", "\\ne"},
        {"≈", "\\approx"}, {"≡", "\\equiv"},  {"∼", "\\sim"},
        {"∝", "\\propto"}, {"≅", "\\cong"},
        {"→", "\\to"},     {"←", "\\leftarrow"},
        {"↔", "\\leftrightarrow"},
        {"⇒", "\\Rightarrow"}, {"⇐", "\\Leftarrow"},
        {"⇔", "\\Leftrightarrow"}, {"↦", "\\mapsto"},
        {"∞", "\\infty"},  {"∂", "\\partial"}, {"∇", "\\nabla"},
        {"∑", "\\sum"},    {"∏", "\\prod"},   {"∐", "\\coprod"},
        {"∫", "\\int"},    {"∬", "\\iint"},   {"∭", "\\iiint"},
        {"∮", "\\oint"},   {"√", "\\sqrt"},
        {"∈", "\\in"},     {"∉", "\\notin"},  {"∋", "\\ni"},
        {"⊂", "\\subset"}, {"⊆", "\\subseteq"},
        {"⊃", "\\supset"}, {"⊇", "\\supseteq"},
        {"∪", "\\cup"},    {"∩", "\\cap"},    {"∅", "\\emptyset"},
        {"∀", "\\forall"}, {"∃", "\\exists"}, {"¬", "\\neg"},
        {"∧", "\\wedge"},  {"∨", "\\vee"},    {"⊕", "\\oplus"},
        {"⊗", "\\otimes"}, {"⊥", "\\perp"},   {"∥", "\\parallel"},
        {"⋯", "\\cdots"},  {"…", "\\ldots"},  {"⋮", "\\vdots"},
        {"′", "'"},        {"″", "''"},       {"‴", "'''"},
        {"°", "^{\\circ}"},
        {"−", "-"},        // U+2212 MINUS SIGN -> ASCII hyphen-minus
        {"⁢", ""},         // invisible times
        {"⁡", ""},         // apply function
        {"⁣", ""},         // invisible separator
        {"⁤", ""},         // invisible plus
        {" ", " "},        // no-break space
        {"∣", "\\mid"},    {"ℓ", "\\ell"},    {"ℏ", "\\hbar"},
        {"ℵ", "\\aleph"},  {"∠", "\\angle"},
        // Greek lowercase
        {"α", "\\alpha"},   {"β", "\\beta"},    {"γ", "\\gamma"},
        {"δ", "\\delta"},   {"ε", "\\varepsilon"}, {"ϵ", "\\epsilon"},
        {"ζ", "\\zeta"},    {"η", "\\eta"},     {"θ", "\\theta"},
        {"ϑ", "\\vartheta"},{"ι", "\\iota"},    {"κ", "\\kappa"},
        {"λ", "\\lambda"},  {"μ", "\\mu"},      {"ν", "\\nu"},
        {"ξ", "\\xi"},      {"ο", "o"},         {"π", "\\pi"},
        {"ϖ", "\\varpi"},   {"ρ", "\\rho"},     {"ϱ", "\\varrho"},
        {"ς", "\\varsigma"},{"σ", "\\sigma"},   {"τ", "\\tau"},
        {"υ", "\\upsilon"}, {"φ", "\\varphi"},  {"ϕ", "\\phi"},
        {"χ", "\\chi"},     {"ψ", "\\psi"},     {"ω", "\\omega"},
        // Greek uppercase (letters that look Latin map to Latin, as LaTeX has
        // no commands for them)
        {"Α", "A"},         {"Β", "B"},         {"Γ", "\\Gamma"},
        {"Δ", "\\Delta"},   {"Ε", "E"},         {"Ζ", "Z"},
        {"Η", "H"},         {"Θ", "\\Theta"},   {"Ι", "I"},
        {"Κ", "K"},         {"Λ", "\\Lambda"},  {"Μ", "M"},
        {"Ν", "N"},         {"Ξ", "\\Xi"},      {"Ο", "O"},
        {"Π", "\\Pi"},      {"Ρ", "P"},         {"Σ", "\\Sigma"},
        {"Τ", "T"},         {"Υ", "\\Upsilon"}, {"Φ", "\\Phi"},
        {"Χ", "X"},         {"Ψ", "\\Psi"},     {"Ω", "\\Omega"},
    };
    return map;
}

// Maps literal formula text to LaTeX codepoint-by-codepoint: known symbols
// become commands, everything else passes through with specials escaped.
std::string MapMathText(const std::string& text) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        size_t len = Utf8SeqLen(static_cast<unsigned char>(text[i]));
        if (i + len > text.size()) len = 1;
        std::string cp = text.substr(i, len);
        i += len;
        auto it = SymbolMap().find(cp);
        if (it != SymbolMap().end()) {
            AppendLatex(out, it->second);
        } else if (cp.size() == 1) {
            AppendLatex(out, EscapeLatexChar(cp[0]));
        } else {
            AppendLatex(out, cp);   // unknown multibyte char passes through
        }
    }
    return out;
}

std::string EscapeTextMode(const std::string& text) {
    std::string out;
    for (char c : text) out += EscapeTextChar(c);
    return out;
}

// Function names that have a LaTeX operator command of the same spelling.
const std::unordered_set<std::string>& KnownFunctions() {
    static const std::unordered_set<std::string> funcs = {
        "sin", "cos", "tan", "cot", "sec", "csc",
        "arcsin", "arccos", "arctan",
        "sinh", "cosh", "tanh", "coth",
        "log", "ln", "lg", "exp",
        "lim", "limsup", "liminf", "max", "min", "sup", "inf",
        "det", "dim", "gcd", "deg", "arg", "hom", "ker", "Pr",
    };
    return funcs;
}

// Bases that conventionally carry sub/superscripts as limits.
bool IsLimitsBase(const std::string& tex) {
    static const std::unordered_set<std::string> ops = {
        "\\sum", "\\prod", "\\coprod", "\\int", "\\iint", "\\iiint", "\\oint",
        "\\bigcup", "\\bigcap", "\\bigvee", "\\bigwedge",
        "\\bigoplus", "\\bigotimes", "\\bigodot", "\\biguplus",
        "\\lim", "\\limsup", "\\liminf", "\\max", "\\min", "\\sup", "\\inf",
        "\\det", "\\gcd", "\\Pr",
    };
    return ops.count(tex) != 0;
}

// True when `s` already parses as one TeX atom (single char, one command,
// or one balanced brace group) and needs no extra braces as a script base.
bool IsSingleToken(const std::string& s) {
    if (s.empty()) return true;
    if (s.size() == 1) return true;
    if (s[0] == '\\' &&
        std::all_of(s.begin() + 1, s.end(),
                    [](unsigned char c) { return std::isalpha(c) != 0; })) {
        return true;
    }
    if (s.front() == '{' && s.back() == '}') {
        int depth = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\') { ++i; continue; }   // skip escaped brace
            if (s[i] == '{') ++depth;
            else if (s[i] == '}') {
                --depth;
                if (depth == 0 && i + 1 < s.size()) return false;
            }
        }
        return true;
    }
    if (static_cast<unsigned char>(s[0]) >= 0x80 &&
        Utf8SeqLen(static_cast<unsigned char>(s[0])) == s.size()) {
        return true;    // one multibyte character
    }
    return false;
}

std::string BraceIfNeeded(const std::string& s) {
    return IsSingleToken(s) ? s : "{" + s + "}";
}

// LaTeX accent command for a combining/spacing accent character; "" if the
// character is not a recognized accent.
std::string AccentCommand(const std::string& ch) {
    if (ch == "^" || ch == "̂" || ch == "ˆ") return "\\hat";
    if (ch == "~" || ch == "̃" || ch == "˜") return "\\tilde";
    if (ch == "ˉ" || ch == "¯" || ch == "̄") return "\\bar";
    if (ch == "‾" || ch == "̅") return "\\overline";
    if (ch == "→" || ch == "⃗") return "\\vec";
    if (ch == "˙" || ch == "̇") return "\\dot";
    if (ch == "¨" || ch == "̈") return "\\ddot";
    if (ch == "ˇ" || ch == "̌") return "\\check";
    if (ch == "˘" || ch == "̆") return "\\breve";
    if (ch == "˚" || ch == "̊") return "\\mathring";
    if (ch == "´" || ch == "́") return "\\acute";
    if (ch == "`" || ch == "̀") return "\\grave";
    return {};
}

// Delimiter spelling valid after \left or \right; "." when unusable.
std::string FenceDelim(const std::string& ch) {
    if (ch.empty()) return ".";
    if (ch == "(" || ch == ")" || ch == "[" || ch == "]" || ch == "|" ||
        ch == "/") return ch;
    if (ch == "{") return "\\{";
    if (ch == "}") return "\\}";
    if (ch == "⟨" || ch == "〈" || ch == "〈") return "\\langle";
    if (ch == "⟩" || ch == "〉" || ch == "〉") return "\\rangle";
    if (ch == "⌈") return "\\lceil";
    if (ch == "⌉") return "\\rceil";
    if (ch == "⌊") return "\\lfloor";
    if (ch == "⌋") return "\\rfloor";
    if (ch == "‖" || ch == "∥") return "\\|";
    if (ch == "∣") return "|";
    return ".";
}

// ---------------------------------------------------------------------------
// MathML (ODF embedded formulas)
// ---------------------------------------------------------------------------

std::string ConvertMathML(const XMLElement* e);

std::string ConvertMathMLChildren(const XMLElement* e) {
    std::string out;
    for (const XMLElement* c = e->FirstChildElement(); c;
         c = c->NextSiblingElement()) {
        AppendLatex(out, ConvertMathML(c));
    }
    return out;
}

const XMLElement* NthChildElement(const XMLElement* e, int index) {
    const XMLElement* c = e->FirstChildElement();
    while (c && index > 0) {
        c = c->NextSiblingElement();
        --index;
    }
    return c;
}

// MathML attributes are normally unprefixed even when elements carry a
// prefix, but some producers prefix both.
const char* MathMLAttr(const XMLElement* e, const char* name) {
    if (const char* v = e->Attribute(name)) return v;
    std::string prefixed = std::string("math:") + name;
    return e->Attribute(prefixed.c_str());
}

std::string ConvertMathMLScript(const XMLElement* e, bool sub, bool sup) {
    const XMLElement* base = NthChildElement(e, 0);
    if (!base) return {};
    std::string out = BraceIfNeeded(ConvertMathML(base));
    int index = 1;
    if (sub) {
        if (const XMLElement* c = NthChildElement(e, index++))
            out += "_{" + ConvertMathML(c) + "}";
    }
    if (sup) {
        if (const XMLElement* c = NthChildElement(e, index))
            out += "^{" + ConvertMathML(c) + "}";
    }
    return out;
}

std::string ConvertMathMLFenced(const XMLElement* e) {
    const char* openAttr = MathMLAttr(e, "open");
    const char* closeAttr = MathMLAttr(e, "close");
    const char* sepAttr = MathMLAttr(e, "separators");
    std::string open = openAttr ? openAttr : "(";
    std::string close = closeAttr ? closeAttr : ")";
    std::string seps = sepAttr ? sepAttr : ",";

    // The separators attribute lists one character per gap; the last one
    // repeats. Whitespace inside the list is not a separator.
    std::vector<std::string> sepList;
    for (size_t i = 0; i < seps.size();) {
        size_t len = Utf8SeqLen(static_cast<unsigned char>(seps[i]));
        if (i + len > seps.size()) len = 1;
        std::string cp = seps.substr(i, len);
        i += len;
        if (cp.size() == 1 && std::isspace(static_cast<unsigned char>(cp[0])))
            continue;
        sepList.push_back(MapMathText(cp));
    }

    std::string inner;
    size_t childIndex = 0;
    for (const XMLElement* c = e->FirstChildElement(); c;
         c = c->NextSiblingElement(), ++childIndex) {
        if (childIndex > 0 && !sepList.empty()) {
            size_t sepIndex = std::min(childIndex - 1, sepList.size() - 1);
            inner += sepList[sepIndex];
        }
        AppendLatex(inner, ConvertMathML(c));
    }
    return "\\left" + FenceDelim(Trim(open)) + inner +
           "\\right" + FenceDelim(Trim(close));
}

std::string ConvertMathMLTable(const XMLElement* e) {
    std::string rows;
    for (const XMLElement* tr = e->FirstChildElement(); tr;
         tr = tr->NextSiblingElement()) {
        if (LocalName(tr->Name()) != "mtr" &&
            LocalName(tr->Name()) != "mlabeledtr") continue;
        std::string row;
        bool firstCell = true;
        for (const XMLElement* td = tr->FirstChildElement(); td;
             td = td->NextSiblingElement()) {
            if (!firstCell) row += " & ";
            firstCell = false;
            row += LocalName(td->Name()) == "mtd" ? ConvertMathMLChildren(td)
                                                  : ConvertMathML(td);
        }
        if (!rows.empty()) rows += " \\\\ ";
        rows += row;
    }
    return "\\begin{matrix}" + rows + "\\end{matrix}";
}

// mover/munder/munderover. `underAccent`/`overAccent` scripts that are bare
// accent characters collapse into accent commands; big-operator bases take
// the scripts as limits; everything else uses \overset/\underset.
std::string ConvertMathMLUnderOver(const XMLElement* e, bool under, bool over) {
    const XMLElement* base = NthChildElement(e, 0);
    if (!base) return {};
    std::string baseTex = ConvertMathML(base);
    const XMLElement* underEl = under ? NthChildElement(e, 1) : nullptr;
    const XMLElement* overEl = over ? NthChildElement(e, under ? 2 : 1) : nullptr;

    if (over && !under && overEl) {
        std::string accent = AccentCommand(Trim(GatherText(overEl)));
        if (!accent.empty()) return accent + "{" + baseTex + "}";
    }
    if (under && !over && underEl) {
        std::string ch = Trim(GatherText(underEl));
        if (ch == "_" || ch == "̲" || ch == "_")
            return "\\underline{" + baseTex + "}";
        if (ch == "⏟") return "\\underbrace{" + baseTex + "}";
    }

    std::string underTex = underEl ? ConvertMathML(underEl) : std::string();
    std::string overTex = overEl ? ConvertMathML(overEl) : std::string();
    if (IsLimitsBase(baseTex)) {
        std::string out = baseTex;
        if (underEl) out += "_{" + underTex + "}";
        if (overEl) out += "^{" + overTex + "}";
        return out;
    }
    std::string out = baseTex;
    if (underEl) out = "\\underset{" + underTex + "}{" + out + "}";
    if (overEl) out = "\\overset{" + overTex + "}{" + out + "}";
    return out;
}

std::string ConvertMathML(const XMLElement* e) {
    const std::string name = LocalName(e->Name());

    if (name == "mi") {
        std::string text = Trim(GatherText(e));
        if (text.empty()) return {};
        if (KnownFunctions().count(text)) return "\\" + text;
        size_t firstLen = Utf8SeqLen(static_cast<unsigned char>(text[0]));
        if (firstLen >= text.size()) return MapMathText(text);
        return "\\mathrm{" + MapMathText(text) + "}";   // multi-char identifier
    }
    if (name == "mn" || name == "mo") return MapMathText(Trim(GatherText(e)));
    if (name == "mtext") {
        std::string text = GatherText(e);
        return text.empty() ? std::string() : "\\text{" + EscapeTextMode(text) + "}";
    }
    if (name == "mspace") return "\\,";
    if (name == "mrow" || name == "math" || name == "mstyle" ||
        name == "mpadded" || name == "merror" || name == "menclose") {
        return ConvertMathMLChildren(e);
    }
    if (name == "mphantom") return "\\phantom{" + ConvertMathMLChildren(e) + "}";
    if (name == "semantics") {
        // Content is the first non-annotation child; annotations carry
        // StarMath/other source text and must not leak into the output.
        for (const XMLElement* c = e->FirstChildElement(); c;
             c = c->NextSiblingElement()) {
            std::string childName = LocalName(c->Name());
            if (childName == "annotation" || childName == "annotation-xml")
                continue;
            return ConvertMathML(c);
        }
        return {};
    }
    if (name == "annotation" || name == "annotation-xml") return {};
    if (name == "msup") return ConvertMathMLScript(e, false, true);
    if (name == "msub") return ConvertMathMLScript(e, true, false);
    if (name == "msubsup") return ConvertMathMLScript(e, true, true);
    if (name == "mfrac") {
        const XMLElement* num = NthChildElement(e, 0);
        const XMLElement* den = NthChildElement(e, 1);
        if (!num || !den) return ConvertMathMLChildren(e);
        return "\\frac{" + ConvertMathML(num) + "}{" + ConvertMathML(den) + "}";
    }
    if (name == "msqrt") return "\\sqrt{" + ConvertMathMLChildren(e) + "}";
    if (name == "mroot") {
        const XMLElement* base = NthChildElement(e, 0);
        const XMLElement* index = NthChildElement(e, 1);
        if (!base) return {};
        if (!index) return "\\sqrt{" + ConvertMathML(base) + "}";
        return "\\sqrt[" + ConvertMathML(index) + "]{" + ConvertMathML(base) + "}";
    }
    if (name == "mfenced") return ConvertMathMLFenced(e);
    if (name == "mover") return ConvertMathMLUnderOver(e, false, true);
    if (name == "munder") return ConvertMathMLUnderOver(e, true, false);
    if (name == "munderover") return ConvertMathMLUnderOver(e, true, true);
    if (name == "mtable") return ConvertMathMLTable(e);
    if (name == "mtr" || name == "mtd") return ConvertMathMLChildren(e);

    // Unknown container: salvage whatever renders inside it.
    return ConvertMathMLChildren(e);
}

// ---------------------------------------------------------------------------
// OMML (OOXML / WordprocessingML embedded math, all tags prefixed "m:")
// ---------------------------------------------------------------------------

std::string ConvertOmml(const XMLElement* e);

std::string ConvertOmmlChildren(const XMLElement* e) {
    std::string out;
    for (const XMLElement* c = e->FirstChildElement(); c;
         c = c->NextSiblingElement()) {
        AppendLatex(out, ConvertOmml(c));
    }
    return out;
}

const XMLElement* OmmlChild(const XMLElement* e, const char* localName) {
    for (const XMLElement* c = e->FirstChildElement(); c;
         c = c->NextSiblingElement()) {
        if (LocalName(c->Name()) == localName) return c;
    }
    return nullptr;
}

std::string OmmlVal(const XMLElement* e) {
    if (!e) return {};
    if (const char* v = e->Attribute("m:val")) return v;
    if (const char* v = e->Attribute("val")) return v;
    return {};
}

// Value of <parent><prName><fieldName m:val="..."/></prName></parent>.
std::string OmmlPropVal(const XMLElement* e, const char* prName,
                        const char* fieldName) {
    const XMLElement* pr = OmmlChild(e, prName);
    return pr ? OmmlVal(OmmlChild(pr, fieldName)) : std::string();
}

// ST_OnOff: the element being present with no m:val means "on".
bool OmmlPropFlag(const XMLElement* e, const char* prName,
                  const char* fieldName) {
    const XMLElement* pr = OmmlChild(e, prName);
    if (!pr) return false;
    const XMLElement* field = OmmlChild(pr, fieldName);
    if (!field) return false;
    std::string val = OmmlVal(field);
    return val != "0" && val != "off" && val != "false";
}

std::string ConvertOmmlChildNamed(const XMLElement* e, const char* localName) {
    const XMLElement* c = OmmlChild(e, localName);
    return c ? ConvertOmmlChildren(c) : std::string();
}

// n-ary operator character to LaTeX command; falls back to the symbol map
// for characters without a big-operator form.
std::string NaryCommand(const std::string& chr) {
    static const std::unordered_map<std::string, std::string> ops = {
        {"∫", "\\int"},    {"∬", "\\iint"},  {"∭", "\\iiint"},
        {"∮", "\\oint"},   {"∑", "\\sum"},   {"∏", "\\prod"},
        {"∐", "\\coprod"}, {"⋃", "\\bigcup"},{"⋂", "\\bigcap"},
        {"⋁", "\\bigvee"}, {"⋀", "\\bigwedge"},
        {"⨁", "\\bigoplus"}, {"⨂", "\\bigotimes"},
        {"⨀", "\\bigodot"},  {"⨄", "\\biguplus"},
    };
    auto it = ops.find(chr);
    if (it != ops.end()) return it->second;
    return MapMathText(chr);
}

std::string ConvertOmmlNary(const XMLElement* e) {
    std::string chr = OmmlPropVal(e, "naryPr", "chr");
    std::string out = NaryCommand(chr.empty() ? "∫" : chr);
    if (!OmmlPropFlag(e, "naryPr", "subHide")) {
        std::string sub = ConvertOmmlChildNamed(e, "sub");
        if (!sub.empty()) out += "_{" + sub + "}";
    }
    if (!OmmlPropFlag(e, "naryPr", "supHide")) {
        std::string sup = ConvertOmmlChildNamed(e, "sup");
        if (!sup.empty()) out += "^{" + sup + "}";
    }
    AppendLatex(out, ConvertOmmlChildNamed(e, "e"));
    return out;
}

std::string ConvertOmmlDelimiter(const XMLElement* e) {
    const XMLElement* pr = OmmlChild(e, "dPr");
    // Explicit empty m:val means "no delimiter on this side".
    const XMLElement* begEl = pr ? OmmlChild(pr, "begChr") : nullptr;
    const XMLElement* endEl = pr ? OmmlChild(pr, "endChr") : nullptr;
    const XMLElement* sepEl = pr ? OmmlChild(pr, "sepChr") : nullptr;
    std::string beg = begEl ? OmmlVal(begEl) : "(";
    std::string end = endEl ? OmmlVal(endEl) : ")";
    std::string sep = sepEl ? OmmlVal(sepEl) : "|";

    std::string inner;
    bool first = true;
    for (const XMLElement* c = e->FirstChildElement(); c;
         c = c->NextSiblingElement()) {
        if (LocalName(c->Name()) != "e") continue;
        if (!first && !sep.empty()) inner += MapMathText(sep);
        first = false;
        AppendLatex(inner, ConvertOmmlChildren(c));
    }
    return "\\left" + FenceDelim(beg) + inner + "\\right" + FenceDelim(end);
}

std::string ConvertOmmlMatrix(const XMLElement* e) {
    std::string rows;
    for (const XMLElement* mr = e->FirstChildElement(); mr;
         mr = mr->NextSiblingElement()) {
        if (LocalName(mr->Name()) != "mr") continue;
        std::string row;
        bool firstCell = true;
        for (const XMLElement* cell = mr->FirstChildElement(); cell;
             cell = cell->NextSiblingElement()) {
            if (LocalName(cell->Name()) != "e") continue;
            if (!firstCell) row += " & ";
            firstCell = false;
            row += ConvertOmmlChildren(cell);
        }
        if (!rows.empty()) rows += " \\\\ ";
        rows += row;
    }
    return "\\begin{matrix}" + rows + "\\end{matrix}";
}

std::string ConvertOmml(const XMLElement* e) {
    const std::string name = LocalName(e->Name());

    // Property containers hold layout hints, never renderable content.
    if (name.size() > 2 && name.ends_with("Pr")) return {};
    if (name == "ctrlPr") return {};

    if (name == "t") return MapMathText(GatherText(e));
    if (name == "f") {
        std::string num = ConvertOmmlChildNamed(e, "num");
        std::string den = ConvertOmmlChildNamed(e, "den");
        std::string type = OmmlPropVal(e, "fPr", "type");
        if (type == "lin")
            return BraceIfNeeded(num) + "/" + BraceIfNeeded(den);
        if (type == "noBar")
            return "\\binom{" + num + "}{" + den + "}";
        return "\\frac{" + num + "}{" + den + "}";
    }
    if (name == "sSup") {
        return BraceIfNeeded(ConvertOmmlChildNamed(e, "e")) +
               "^{" + ConvertOmmlChildNamed(e, "sup") + "}";
    }
    if (name == "sSub") {
        return BraceIfNeeded(ConvertOmmlChildNamed(e, "e")) +
               "_{" + ConvertOmmlChildNamed(e, "sub") + "}";
    }
    if (name == "sSubSup") {
        return BraceIfNeeded(ConvertOmmlChildNamed(e, "e")) +
               "_{" + ConvertOmmlChildNamed(e, "sub") + "}" +
               "^{" + ConvertOmmlChildNamed(e, "sup") + "}";
    }
    if (name == "sPre") {
        return "{}_{" + ConvertOmmlChildNamed(e, "sub") + "}" +
               "^{" + ConvertOmmlChildNamed(e, "sup") + "}" +
               BraceIfNeeded(ConvertOmmlChildNamed(e, "e"));
    }
    if (name == "rad") {
        std::string body = ConvertOmmlChildNamed(e, "e");
        std::string degree = ConvertOmmlChildNamed(e, "deg");
        if (OmmlPropFlag(e, "radPr", "degHide") || degree.empty())
            return "\\sqrt{" + body + "}";
        return "\\sqrt[" + degree + "]{" + body + "}";
    }
    if (name == "d") return ConvertOmmlDelimiter(e);
    if (name == "nary") return ConvertOmmlNary(e);
    if (name == "func") {
        std::string fn = ConvertOmmlChildNamed(e, "fName");
        if (KnownFunctions().count(fn)) fn = "\\" + fn;
        AppendLatex(fn, ConvertOmmlChildNamed(e, "e"));
        return fn;
    }
    if (name == "limLow" || name == "limUpp") {
        std::string base = ConvertOmmlChildNamed(e, "e");
        if (KnownFunctions().count(base)) base = "\\" + base;
        std::string lim = ConvertOmmlChildNamed(e, "lim");
        return BraceIfNeeded(base) +
               (name == "limLow" ? "_{" : "^{") + lim + "}";
    }
    if (name == "m") return ConvertOmmlMatrix(e);
    if (name == "acc") {
        std::string chr = OmmlPropVal(e, "accPr", "chr");
        std::string cmd = AccentCommand(chr);
        if (cmd.empty() || cmd == "\\overline") cmd = "\\hat";  // spec default
        return cmd + "{" + ConvertOmmlChildNamed(e, "e") + "}";
    }
    if (name == "bar") {
        std::string pos = OmmlPropVal(e, "barPr", "pos");
        std::string cmd = (pos == "top") ? "\\overline" : "\\underline";
        return cmd + "{" + ConvertOmmlChildNamed(e, "e") + "}";
    }
    if (name == "groupChr") {
        std::string chr = OmmlPropVal(e, "groupChrPr", "chr");
        std::string pos = OmmlPropVal(e, "groupChrPr", "pos");
        bool onTop = (chr == "⏞") || (pos == "top");
        return (onTop ? "\\overbrace{" : "\\underbrace{") +
               ConvertOmmlChildNamed(e, "e") + "}";
    }
    if (name == "eqArr") {
        std::string rows;
        for (const XMLElement* c = e->FirstChildElement(); c;
             c = c->NextSiblingElement()) {
            if (LocalName(c->Name()) != "e") continue;
            if (!rows.empty()) rows += " \\\\ ";
            rows += ConvertOmmlChildren(c);
        }
        return rows;
    }
    if (name == "phant") return ConvertOmmlChildNamed(e, "e");
    if (name == "box" || name == "borderBox") return ConvertOmmlChildNamed(e, "e");

    // oMath, oMathPara, r, e, num, den, sub, sup, deg, lim, fName and any
    // unknown container all reduce to their converted children.
    return ConvertOmmlChildren(e);
}

} // namespace

std::string MathMLToLatex(const XMLElement* mathElement) {
    if (!mathElement) return {};
    return TidyLatex(ConvertMathML(mathElement));
}

std::string OmmlToLatex(const XMLElement* oMathElement) {
    if (!oMathElement) return {};
    return TidyLatex(ConvertOmml(oMathElement));
}

} // namespace WordMath
} // namespace UltraCanvas
