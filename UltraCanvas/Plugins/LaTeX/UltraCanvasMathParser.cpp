// Plugins/LaTeX/UltraCanvasMathParser.cpp
// LaTeX math parser of the native math engine. See UltraCanvasMathParser.h.
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "Plugins/LaTeX/UltraCanvasMathParser.h"
#include "Plugins/LaTeX/UltraCanvasMathSymbols.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace UltraCanvas {

// =============================================================================
// Lengths
// =============================================================================

bool ParseMathLength(const std::string& text, float& em) {
    size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    size_t start = i;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) ++i;
    bool digits = false;
    while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.' || text[i] == ',')) {
        ++i; digits = true;
    }
    if (!digits) return false;
    std::string num = text.substr(start, i - start);
    for (auto& c : num) if (c == ',') c = '.';
    const float value = std::strtof(num.c_str(), nullptr);
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    std::string unit;
    while (i < text.size() && std::isalpha(static_cast<unsigned char>(text[i]))) unit.push_back(text[i++]);
    // Points are relative to a 10pt design size: 1pt = 0.1em, the convention
    // MicroTeX uses too, so \hspace{5pt} scales with the formula.
    float factor;
    if (unit.empty() || unit == "pt") factor = 0.1f;
    else if (unit == "em") factor = 1.f;
    else if (unit == "ex") factor = 0.43f;
    else if (unit == "mu") factor = 1.f / 18.f;
    else if (unit == "px") factor = 0.1f;
    else if (unit == "bp") factor = 0.100375f;
    else if (unit == "cm") factor = 2.845f;
    else if (unit == "mm") factor = 0.2845f;
    else if (unit == "in") factor = 7.227f;
    else if (unit == "pc") factor = 1.2f;
    else if (unit == "dd") factor = 0.107f;
    else if (unit == "sp") factor = 0.1f / 65536.f;
    else return false;
    em = value * factor;
    return true;
}

// =============================================================================
// Impl
// =============================================================================

namespace {

struct Macro {
    int argCount = 0;
    bool hasOptional = false;
    std::string optionalDefault;
    std::string body;
};

struct Environment {
    int argCount = 0;
    std::string begin, end;
};

struct Context {
    MathFontStyle font;
    bool textMode = false;
};

struct Stops {
    bool cell = false;      // stop at & and at a row break
    bool fence = false;     // stop at \right and \middle
    bool dollar = false;    // stop at $ (math inside text)
    bool lines = false;     // stop at top-level \\ (multi-line formula)
};

const char* kSizeCommands[] = {"tiny", "scriptsize", "footnotesize", "small", "normalsize",
                               "large", "Large", "LARGE", "huge", "Huge"};
const float kSizeScales[] = {0.5f, 0.7f, 0.8f, 0.9f, 1.f, 1.2f, 1.44f, 1.728f, 2.074f, 2.488f};

bool IsLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

char32_t NegatedRelation(char32_t cp) {
    switch (cp) {
        case '=': return 0x2260; case 0x2208: return 0x2209; case 0x220B: return 0x220C;
        case 0x2282: return 0x2284; case 0x2283: return 0x2285; case 0x2286: return 0x2288;
        case 0x2287: return 0x2289; case 0x2261: return 0x2262; case '<': return 0x226E;
        case '>': return 0x226F; case 0x2264: return 0x2270; case 0x2265: return 0x2271;
        case 0x223C: return 0x2241; case 0x2243: return 0x2244; case 0x2248: return 0x2249;
        case 0x2245: return 0x2247; case 0x2223: return 0x2224; case 0x2225: return 0x2226;
        case 0x22A2: return 0x22AC; case 0x22A8: return 0x22AD; case 0x2192: return 0x219B;
        case 0x2190: return 0x219A; case 0x21D2: return 0x21CF; case 0x21D0: return 0x21CD;
        case 0x21D4: return 0x21CE; case 0x2194: return 0x21AE; case 0x224D: return 0x226D;
        case 0x227A: return 0x2280; case 0x227B: return 0x2281; case 0x2291: return 0x22E2;
        case 0x2292: return 0x22E3; case 0x2203: return 0x2204;
        default: return 0;
    }
}

// Text-mode accents (\'e) composed to the precomposed letter where one exists.
char32_t ComposeTextAccent(char accent, char32_t base) {
    struct E { char a; char32_t b; char32_t r; };
    static const E table[] = {
        {'\'', 'a', 0xE1}, {'\'', 'e', 0xE9}, {'\'', 'i', 0xED}, {'\'', 'o', 0xF3}, {'\'', 'u', 0xFA},
        {'\'', 'y', 0xFD}, {'\'', 'A', 0xC1}, {'\'', 'E', 0xC9}, {'\'', 'I', 0xCD}, {'\'', 'O', 0xD3},
        {'\'', 'U', 0xDA}, {'\'', 'c', 0x107}, {'\'', 'n', 0x144}, {'\'', 's', 0x15B}, {'\'', 'z', 0x17A},
        {'`', 'a', 0xE0}, {'`', 'e', 0xE8}, {'`', 'i', 0xEC}, {'`', 'o', 0xF2}, {'`', 'u', 0xF9},
        {'`', 'A', 0xC0}, {'`', 'E', 0xC8}, {'`', 'I', 0xCC}, {'`', 'O', 0xD2}, {'`', 'U', 0xD9},
        {'^', 'a', 0xE2}, {'^', 'e', 0xEA}, {'^', 'i', 0xEE}, {'^', 'o', 0xF4}, {'^', 'u', 0xFB},
        {'^', 'A', 0xC2}, {'^', 'E', 0xCA}, {'^', 'I', 0xCE}, {'^', 'O', 0xD4}, {'^', 'U', 0xDB},
        {'"', 'a', 0xE4}, {'"', 'e', 0xEB}, {'"', 'i', 0xEF}, {'"', 'o', 0xF6}, {'"', 'u', 0xFC},
        {'"', 'y', 0xFF}, {'"', 'A', 0xC4}, {'"', 'E', 0xCB}, {'"', 'I', 0xCF}, {'"', 'O', 0xD6},
        {'"', 'U', 0xDC}, {'~', 'a', 0xE3}, {'~', 'n', 0xF1}, {'~', 'o', 0xF5}, {'~', 'A', 0xC3},
        {'~', 'N', 0xD1}, {'~', 'O', 0xD5}, {'c', 'c', 0xE7}, {'c', 'C', 0xC7}, {'v', 'c', 0x10D},
        {'v', 's', 0x161}, {'v', 'z', 0x17E}, {'v', 'C', 0x10C}, {'v', 'S', 0x160}, {'v', 'Z', 0x17D},
        {'v', 'e', 0x11B}, {'v', 'r', 0x159}, {'=', 'a', 0x101}, {'=', 'e', 0x113}, {'=', 'o', 0x14D},
        {'.', 'z', 0x17C}, {'.', 'e', 0x117}, {'u', 'a', 0x103}, {'u', 'g', 0x11F}, {'H', 'o', 0x151},
        {'H', 'u', 0x171}, {'r', 'a', 0xE5}, {'r', 'A', 0xC5}, {'k', 'a', 0x105}, {'k', 'e', 0x119},
    };
    for (const auto& e : table) if (e.a == accent && e.b == base) return e.r;
    return 0;
}

char32_t CombiningForTextAccent(char accent) {
    switch (accent) {
        case '\'': return 0x0301; case '`': return 0x0300; case '^': return 0x0302; case '"': return 0x0308;
        case '~': return 0x0303; case '=': return 0x0304; case '.': return 0x0307; case 'u': return 0x0306;
        case 'v': return 0x030C; case 'H': return 0x030B; case 'r': return 0x030A; case 'c': return 0x0327;
        case 'k': return 0x0328; case 'd': return 0x0323; case 'b': return 0x0331; case 't': return 0x0361;
        default: return 0;
    }
}

} // namespace

struct UltraCanvasMathParser::Impl {
    std::string src;
    size_t pos = 0;
    std::vector<MathDiagnostic>* diags = nullptr;
    std::map<std::string, Macro> macros;
    std::map<std::string, Environment> environments;
    std::map<std::string, MathColor> colors;
    std::map<std::string, std::string> columnTypes;
    std::map<std::string, std::string> operators;   // \DeclareMathOperator
    std::set<std::string> starredOperators;
    float cornerSize = 0.5f;
    int expansions = 0;
    int depth = 0;
    Context ctx;
    MathColor pendingCellColor = kMathColorNone;    // \cellcolor seen inside a cell
    MathColor pendingRowColor = kMathColorNone;

    // ----- diagnostics -----
    void Report(const std::string& message, size_t start, size_t end) {
        if (diags) diags->push_back({message, static_cast<int>(start), static_cast<int>(end)});
    }

    MathAtomPtr MakeError(const std::string& text, size_t start, size_t end, const std::string& message) {
        Report(message, start, end);
        auto a = MathAtom::Make(MathAtomKind::Error);
        a->text = text;
        a->sourceStart = static_cast<int>(start);
        a->sourceEnd = static_cast<int>(end);
        return a;
    }

    // ----- scanning -----
    bool AtEnd() const { return pos >= src.size(); }
    char Peek(size_t ahead = 0) const { return pos + ahead < src.size() ? src[pos + ahead] : '\0'; }

    void SkipComment() {
        while (!AtEnd() && src[pos] != '\n') ++pos;
    }
    void SkipSpaces() {
        while (!AtEnd()) {
            const char c = src[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++pos;
            else if (c == '%') SkipComment();
            else break;
        }
    }
    // After a control word TeX swallows following whitespace.
    void SkipSpacesAfterControlWord() {
        while (!AtEnd() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\r' || src[pos] == '\n')) ++pos;
    }

    // Reads a control sequence name at `pos` (pointing at the backslash).
    // Returns "" when no backslash is there.
    std::string ReadCommand() {
        if (Peek() != '\\') return {};
        ++pos;
        if (AtEnd()) return " ";
        if (IsLetter(src[pos])) {
            size_t s = pos;
            while (pos < src.size() && IsLetter(src[pos])) ++pos;
            std::string name = src.substr(s, pos - s);
            // \operatorname* and friends
            if (Peek() == '*' && (name == "operatorname" || name == "DeclareMathOperator" ||
                                  name == "hspace" || name == "newcommand" || name == "renewcommand" ||
                                  name == "providecommand" || name == "newenvironment")) {
                ++pos; name += "*";
            }
            SkipSpacesAfterControlWord();
            return name;
        }
        // Single non-letter control symbol (\, \; \\ \{ \| ...), one UTF-8 char.
        size_t p = pos;
        DecodeUtf8(src, p);
        std::string name = src.substr(pos, p - pos);
        pos = p;
        return name;
    }
    std::string PeekCommand(size_t* endPos = nullptr) {
        const size_t saved = pos;
        std::string name = ReadCommand();
        if (endPos) *endPos = pos;
        pos = saved;
        return name;
    }

    bool ReadGroupRaw(std::string& out) {
        SkipSpaces();
        if (Peek() != '{') return false;
        ++pos;
        int level = 1;
        size_t s = pos;
        while (!AtEnd()) {
            const char c = src[pos];
            if (c == '\\') { pos += 2; continue; }
            if (c == '{') ++level;
            else if (c == '}') { if (--level == 0) break; }
            else if (c == '%') { SkipComment(); continue; }
            ++pos;
        }
        out = src.substr(s, pos - s);
        if (!AtEnd()) ++pos;   // the closing brace
        return true;
    }
    bool ReadOptionalRaw(std::string& out) {
        const size_t saved = pos;
        SkipSpaces();
        if (Peek() != '[') { pos = saved; return false; }
        ++pos;
        int level = 0;
        size_t s = pos;
        while (!AtEnd()) {
            const char c = src[pos];
            if (c == '\\') { pos += 2; continue; }
            if (c == '{') ++level;
            else if (c == '}') --level;
            else if (c == ']' && level <= 0) break;
            ++pos;
        }
        out = src.substr(s, pos - s);
        if (!AtEnd()) ++pos;
        return true;
    }
    // A required argument as raw text: a group, or a single token.
    std::string ReadArgRaw() {
        SkipSpaces();
        std::string out;
        if (ReadGroupRaw(out)) return out;
        if (AtEnd()) return {};
        if (Peek() == '\\') {
            const size_t s = pos;
            ReadCommand();
            return src.substr(s, pos - s);
        }
        size_t p = pos;
        DecodeUtf8(src, p);
        out = src.substr(pos, p - pos);
        pos = p;
        return out;
    }
    // A bare TeX dimension after \kern etc: "1.5em", "-3mu".
    std::string ReadBareLength() {
        SkipSpaces();
        size_t s = pos;
        if (Peek() == '-' || Peek() == '+') ++pos;
        while (!AtEnd() && (std::isdigit(static_cast<unsigned char>(src[pos])) || src[pos] == '.')) ++pos;
        while (!AtEnd() && src[pos] == ' ') ++pos;
        size_t u = pos;
        while (!AtEnd() && IsLetter(src[pos]) && pos - u < 2) ++pos;
        // "plus"/"minus" glue parts are ignored
        std::string len = src.substr(s, pos - s);
        for (;;) {
            const size_t save = pos;
            SkipSpaces();
            if (src.compare(pos, 4, "plus") == 0 || src.compare(pos, 5, "minus") == 0) {
                pos += src.compare(pos, 4, "plus") == 0 ? 4 : 5;
                ReadBareLength();
            } else { pos = save; break; }
        }
        return len;
    }

    // ----- macros -----
    bool ExpandMacro(const std::string& name, size_t cmdStart) {
        auto it = macros.find(name);
        if (it == macros.end()) return false;
        if (++expansions > 20000) {
            Report("macro expansion limit reached", cmdStart, pos);
            return false;
        }
        const Macro& m = it->second;
        std::vector<std::string> args;
        if (m.hasOptional) {
            std::string opt;
            args.push_back(ReadOptionalRaw(opt) ? opt : m.optionalDefault);
        }
        for (int i = static_cast<int>(args.size()); i < m.argCount; ++i) args.push_back(ReadArgRaw());
        std::string expanded;
        for (size_t i = 0; i < m.body.size(); ++i) {
            if (m.body[i] == '#' && i + 1 < m.body.size()) {
                if (std::isdigit(static_cast<unsigned char>(m.body[i + 1]))) {
                    const int n = m.body[i + 1] - '0';
                    if (n >= 1 && n <= static_cast<int>(args.size())) expanded += args[n - 1];
                    ++i;
                    continue;
                }
                if (m.body[i + 1] == '#') { expanded.push_back('#'); ++i; continue; }
            }
            expanded.push_back(m.body[i]);
        }
        // Splice: replace [cmdStart, pos) with the expansion.
        src = src.substr(0, cmdStart) + expanded + " " + src.substr(pos);
        pos = cmdStart;
        return true;
    }

    void ParseNewCommand(bool renew, bool provide) {
        SkipSpaces();
        std::string name;
        if (Peek() == '{') {
            std::string g; ReadGroupRaw(g);
            size_t i = 0; while (i < g.size() && std::isspace(static_cast<unsigned char>(g[i]))) ++i;
            if (i < g.size() && g[i] == '\\') ++i;
            name = g.substr(i);
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
        } else if (Peek() == '\\') {
            name = ReadCommand();
        }
        std::string opt;
        Macro m;
        if (ReadOptionalRaw(opt)) m.argCount = std::atoi(opt.c_str());
        if (ReadOptionalRaw(opt)) { m.hasOptional = true; m.optionalDefault = opt; }
        std::string body;
        if (!ReadGroupRaw(body)) body = ReadArgRaw();
        m.body = body;
        if (name.empty()) return;
        if (provide && macros.count(name)) return;
        (void)renew;   // redefinitions are accepted silently (\fatalIfCmdConflict{false} semantics)
        macros[name] = m;
    }

    void ParseDef() {
        // \def\name#1#2{body}
        SkipSpaces();
        std::string name = ReadCommand();
        Macro m;
        while (!AtEnd() && Peek() != '{') {
            if (Peek() == '#') { ++pos; if (std::isdigit(static_cast<unsigned char>(Peek()))) { ++pos; ++m.argCount; } }
            else ++pos;
        }
        ReadGroupRaw(m.body);
        if (!name.empty()) macros[name] = m;
    }

    void ParseNewEnvironment() {
        std::string name, opt, begin, end;
        ReadGroupRaw(name);
        Environment e;
        if (ReadOptionalRaw(opt)) e.argCount = std::atoi(opt.c_str());
        ReadGroupRaw(begin);
        ReadGroupRaw(end);
        e.begin = begin; e.end = end;
        environments[name] = e;
    }

    // ----- colours -----
    bool ParseColorSpec(std::string spec, MathColor& out) {
        while (!spec.empty() && std::isspace(static_cast<unsigned char>(spec.front()))) spec.erase(spec.begin());
        while (!spec.empty() && std::isspace(static_cast<unsigned char>(spec.back()))) spec.pop_back();
        if (spec.empty()) return false;
        if (auto it = colors.find(spec); it != colors.end()) { out = it->second; return true; }
        std::string hex = spec;
        if (hex[0] == '#') hex.erase(0, 1);
        if ((hex.size() == 6 || hex.size() == 8) &&
            hex.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
            const uint32_t v = static_cast<uint32_t>(std::strtoul(hex.c_str(), nullptr, 16));
            out = hex.size() == 6 ? (0xFF000000u | v) : v;
            return true;
        }
        if (LookupNamedColor(spec, out)) return true;
        // "gray!30" style mixes: take the base colour
        if (const size_t bang = spec.find('!'); bang != std::string::npos) return ParseColorSpec(spec.substr(0, bang), out);
        return false;
    }

    void ParseDefineColor() {
        std::string name, model, spec;
        ReadGroupRaw(name); ReadGroupRaw(model); ReadGroupRaw(spec);
        MathColor c = kMathColorBlack;
        float v[4] = {0, 0, 0, 0};
        int n = 0;
        {
            std::string cur;
            for (char ch : spec + ",") {
                if (ch == ',') { if (n < 4) v[n++] = std::strtof(cur.c_str(), nullptr); cur.clear(); }
                else cur.push_back(ch);
            }
        }
        auto clamp = [](float x) { return static_cast<uint32_t>(std::max(0.f, std::min(255.f, x + 0.5f))); };
        if (model == "rgb") c = 0xFF000000u | (clamp(v[0] * 255) << 16) | (clamp(v[1] * 255) << 8) | clamp(v[2] * 255);
        else if (model == "RGB") c = 0xFF000000u | (clamp(v[0]) << 16) | (clamp(v[1]) << 8) | clamp(v[2]);
        else if (model == "gray" || model == "Gray") {
            const uint32_t g = clamp(v[0] * (model == "gray" ? 255.f : 255.f / 15.f));
            c = 0xFF000000u | (g << 16) | (g << 8) | g;
        } else if (model == "cmyk") {
            const float k = v[3];
            c = 0xFF000000u | (clamp(255 * (1 - v[0]) * (1 - k)) << 16) | (clamp(255 * (1 - v[1]) * (1 - k)) << 8) |
                clamp(255 * (1 - v[2]) * (1 - k));
        } else if (model == "HTML" || model == "html") {
            ParseColorSpec("#" + spec, c);
        } else if (model == "named") {
            ParseColorSpec(spec, c);
        }
        colors[name] = c;
    }

    // ----- font style helpers -----
    MathFontStyle FontFor(MathFontFamily fam, MathFontShape shape, bool bold) const {
        MathFontStyle f = ctx.font;
        f.family = fam; f.shape = shape; f.bold = bold;
        return f;
    }

    // ----- atoms -----
    MathAtomPtr CharAtom(char32_t cp, MathAtomClass cls, size_t start) {
        auto a = MathAtom::MakeChar(cp, cls, ctx.font);
        a->isTextChar = ctx.textMode;
        a->sourceStart = static_cast<int>(start);
        a->sourceEnd = static_cast<int>(pos);
        return a;
    }
    MathAtomPtr SpaceAtom(float em, bool mu = false) {
        auto a = MathAtom::Make(MathAtomKind::Space, MathAtomClass::None);
        if (mu) { a->spaceMu = em; a->spaceIsMu = true; } else a->spaceEm = em;
        return a;
    }
    MathAtomPtr StyleAtom(MathStyle st) {
        auto a = MathAtom::Make(MathAtomKind::Style, MathAtomClass::None);
        a->style = st; a->hasStyle = true;
        return a;
    }

    // Attach a script to the last atom of `out`.
    void AttachScript(MathList& out, bool sup, MathList arg, size_t start) {
        MathAtomPtr target;
        if (!out.empty() && out.back()->kind == MathAtomKind::Scripts &&
            !(sup ? out.back()->hasSup : out.back()->hasSub)) {
            target = out.back();
        } else if (!out.empty() && out.back()->kind == MathAtomKind::Scripts) {
            // Double script: be lenient and append to the existing one (x'^2).
            target = out.back();
            auto& list = sup ? target->superscript : target->subscript;
            list.insert(list.end(), arg.begin(), arg.end());
            return;
        } else {
            target = MathAtom::Make(MathAtomKind::Scripts);
            if (!out.empty() && out.back()->kind != MathAtomKind::Space &&
                out.back()->kind != MathAtomKind::Style) {
                target->nucleus = out.back();
                target->atomClass = out.back()->atomClass;
                out.pop_back();
            } else {
                target->nucleus = MathAtom::MakeRow({});
            }
            // Braces take their scripts as limits.
            if (target->nucleus->kind == MathAtomKind::OverUnder) {
                const auto k = target->nucleus->overUnderKind;
                if (k == MathOverUnderKind::Overbrace || k == MathOverUnderKind::Underbrace ||
                    k == MathOverUnderKind::Overbracket || k == MathOverUnderKind::Underbracket ||
                    k == MathOverUnderKind::Overparen || k == MathOverUnderKind::Underparen) {
                    target->limits = MathLimits::Limits;
                    target->atomClass = MathAtomClass::Op;
                }
            }
            target->sourceStart = static_cast<int>(start);
            out.push_back(target);
        }
        if (sup) { target->superscript = std::move(arg); target->hasSup = true; }
        else     { target->subscript = std::move(arg); target->hasSub = true; }
        target->sourceEnd = static_cast<int>(pos);
    }

    // ----- lists -----
    MathList ParseGroup() {
        // At '{'
        ++pos;
        Context saved = ctx;
        ++depth;
        MathList list = ParseList(Stops{});
        --depth;
        ctx = saved;
        SkipSpaces();
        if (Peek() == '}') ++pos;
        else Report("missing '}'", pos, pos);
        return list;
    }

    MathList ParseArg() {
        SkipSpaces();
        if (Peek() == '{') return ParseGroup();
        MathList out;
        if (AtEnd()) return out;
        ParseItem(out, Stops{});
        return out;
    }

    // Parse an argument as text (the content of \text{...}).
    MathList ParseTextArg() {
        SkipSpaces();
        Context saved = ctx;
        ctx.textMode = true;
        if (ctx.font.shape == MathFontShape::Auto) ctx.font.shape = MathFontShape::Upright;
        MathList out;
        if (Peek() == '{') {
            ++pos;
            out = ParseText();
            SkipSpaces();
            if (Peek() == '}') ++pos;
        } else {
            ParseTextItem(out);
        }
        ctx = saved;
        return out;
    }

    MathAtomPtr TextAtom(MathList body, size_t start) {
        auto a = MathAtom::Make(MathAtomKind::Text);
        a->body = std::move(body);
        a->sourceStart = static_cast<int>(start);
        a->sourceEnd = static_cast<int>(pos);
        return a;
    }

    bool AtStop(const Stops& stops) {
        if (AtEnd()) return true;
        const char c = Peek();
        if (c == '}') return true;
        if (stops.dollar && c == '$') return true;
        if ((stops.cell || stops.lines) && c == '&') return stops.cell;
        if (c == '\\') {
            const std::string cmd = PeekCommand();
            if (cmd == "end") return true;
            if ((stops.cell || stops.lines) && (cmd == "\\" || cmd == "cr")) return true;
            if (stops.fence && (cmd == "right" || cmd == "middle")) return true;
        }
        return false;
    }

    MathList ParseList(const Stops& stops) {
        MathList out;
        for (;;) {
            SkipSpaces();
            if (AtStop(stops)) break;
            ParseItem(out, stops);
        }
        return out;
    }

    void ParseItem(MathList& out, const Stops& stops) {
        const size_t start = pos;
        const char c = Peek();
        if (c == '{') {
            MathList inner = ParseGroup();
            auto row = MathAtom::MakeRow(std::move(inner));
            row->sourceStart = static_cast<int>(start);
            row->sourceEnd = static_cast<int>(pos);
            out.push_back(row);
            return;
        }
        if (c == '}') { ++pos; Report("unexpected '}'", start, pos); return; }
        if (c == '^' || c == '_') {
            ++pos;
            MathList arg = ParseArg();
            AttachScript(out, c == '^', std::move(arg), start);
            return;
        }
        if (c == '\'') {
            int count = 0;
            while (Peek() == '\'') { ++pos; ++count; }
            static const char32_t primes[] = {0x2032, 0x2033, 0x2034, 0x2057};
            MathList sup;
            while (count > 0) {
                const int n = std::min(count, 4);
                sup.push_back(CharAtom(primes[n - 1], MathAtomClass::Ord, start));
                count -= n;
            }
            AttachScript(out, true, std::move(sup), start);
            return;
        }
        if (c == '&') {
            ++pos;
            if (!stops.cell) Report("'&' outside an alignment", start, pos);
            return;
        }
        if (c == '$') {
            // Math inside math: treat as a group toggle (rare); skip.
            ++pos;
            return;
        }
        if (c == '~') { ++pos; out.push_back(SpaceAtom(0.333f)); return; }
        if (c == '#') { ++pos; out.push_back(CharAtom('#', MathAtomClass::Ord, start)); return; }
        if (c == '\\') {
            const std::string cmd = ReadCommand();
            ParseCommand(cmd, out, stops, start);
            return;
        }
        // A plain character.
        char32_t cp = DecodeUtf8(src, pos);
        MathAtomClass cls = ClassOfCharacter(cp);
        if (cp == '-') cp = 0x2212;
        else if (cp == '*') cp = 0x2217;
        else if (cp == '\'') cp = 0x2032;
        else if (cp == '`') cp = 0x2035;
        else if (cp == '"') cp = 0x2033;
        auto atom = CharAtom(cp, cls, start);
        out.push_back(atom);
    }

    // ----- text mode -----
    MathList ParseText() {
        MathList out;
        for (;;) {
            if (AtEnd() || Peek() == '}') break;
            if (Peek() == '\\' && PeekCommand() == "end") break;
            ParseTextItem(out);
        }
        return out;
    }

    void ParseTextItem(MathList& out) {
        const size_t start = pos;
        const char c = Peek();
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            while (!AtEnd() && (Peek() == ' ' || Peek() == '\t' || Peek() == '\n' || Peek() == '\r')) ++pos;
            out.push_back(CharAtom(' ', MathAtomClass::Ord, start));
            return;
        }
        if (c == '%') { SkipComment(); return; }
        if (c == '{') {
            ++pos;
            Context saved = ctx;
            MathList inner = ParseText();
            ctx = saved;
            if (Peek() == '}') ++pos;
            out.insert(out.end(), inner.begin(), inner.end());
            return;
        }
        if (c == '$') {
            ++pos;
            Context saved = ctx;
            ctx.textMode = false;
            ctx.font = MathFontStyle{};
            MathList math = ParseList(Stops{false, false, true, false});
            ctx = saved;
            if (Peek() == '$') ++pos; else Report("missing closing '$'", start, pos);
            auto row = MathAtom::MakeRow(std::move(math));
            row->sourceStart = static_cast<int>(start);
            row->sourceEnd = static_cast<int>(pos);
            out.push_back(row);
            return;
        }
        if (c == '~') { ++pos; out.push_back(CharAtom(0x00A0, MathAtomClass::Ord, start)); return; }
        if (c == '\\') {
            const std::string cmd = ReadCommand();
            if (cmd == "(") {
                Context saved = ctx;
                ctx.textMode = false; ctx.font = MathFontStyle{};
                MathList math;
                for (;;) {
                    SkipSpaces();
                    if (AtEnd() || Peek() == '}') break;
                    if (Peek() == '\\' && PeekCommand() == ")") { ReadCommand(); break; }
                    ParseItem(math, Stops{});
                }
                ctx = saved;
                out.push_back(MathAtom::MakeRow(std::move(math)));
                return;
            }
            if (cmd == "\\" || cmd == "cr" || cmd == "newline") { std::string o; ReadOptionalRaw(o); return; }
            if (cmd == "textbf" || cmd == "bold" || cmd == "textit" || cmd == "emph" || cmd == "textsf" ||
                cmd == "texttt" || cmd == "textrm" || cmd == "textnormal" || cmd == "textup" ||
                cmd == "textmd" || cmd == "textsl" || cmd == "textsc") {
                Context saved = ctx;
                if (cmd == "textbf" || cmd == "bold") ctx.font.bold = true;
                else if (cmd == "textit" || cmd == "emph" || cmd == "textsl") ctx.font.shape = MathFontShape::Italic;
                else if (cmd == "textsf") ctx.font.family = MathFontFamily::Sans;
                else if (cmd == "texttt") ctx.font.family = MathFontFamily::Mono;
                else if (cmd == "textrm" || cmd == "textnormal") { ctx.font.family = MathFontFamily::Serif; ctx.font.shape = MathFontShape::Upright; }
                else if (cmd == "textup") ctx.font.shape = MathFontShape::Upright;
                MathList inner = ParseTextArg();
                ctx = saved;
                out.insert(out.end(), inner.begin(), inner.end());
                return;
            }
            if (cmd == "text" || cmd == "mbox" || cmd == "hbox") {
                MathList inner = ParseTextArg();
                out.insert(out.end(), inner.begin(), inner.end());
                return;
            }
            if (cmd == "textcolor") {
                std::string spec; ReadGroupRaw(spec);
                MathColor col = kMathColorBlack;
                if (!ParseColorSpec(spec, col)) Report("unknown colour '" + spec + "'", start, pos);
                auto a = MathAtom::Make(MathAtomKind::Color, MathAtomClass::None);
                a->color = col;
                a->body = ParseTextArg();
                out.push_back(a);
                return;
            }
            if (cmd == "color") {
                std::string spec; ReadGroupRaw(spec);
                MathColor col = kMathColorBlack;
                if (!ParseColorSpec(spec, col)) Report("unknown colour '" + spec + "'", start, pos);
                auto a = MathAtom::Make(MathAtomKind::Color, MathAtomClass::None);
                a->color = col;
                a->body = ParseText();
                out.push_back(a);
                return;
            }
            for (size_t i = 0; i < 10; ++i) {
                if (cmd == kSizeCommands[i]) {
                    auto a = MathAtom::Make(MathAtomKind::Style, MathAtomClass::None);
                    a->sizeScale = kSizeScales[i];
                    a->body = ParseText();
                    out.push_back(a);
                    return;
                }
            }
            if (cmd == " " || cmd == ",") { out.push_back(CharAtom(' ', MathAtomClass::Ord, start)); return; }
            if (cmd == ";" || cmd == ":") { out.push_back(SpaceAtom(0.25f)); return; }
            if (cmd == "quad") { out.push_back(SpaceAtom(1.f)); return; }
            if (cmd == "qquad") { out.push_back(SpaceAtom(2.f)); return; }
            if (cmd == "hspace" || cmd == "hspace*") {
                std::string len; ReadGroupRaw(len); float em = 0;
                ParseMathLength(len, em); out.push_back(SpaceAtom(em)); return;
            }
            if (cmd == "{" || cmd == "}" || cmd == "$" || cmd == "&" || cmd == "#" || cmd == "%" || cmd == "_") {
                out.push_back(CharAtom(static_cast<unsigned char>(cmd[0]), MathAtomClass::Ord, start));
                return;
            }
            if (cmd == "LaTeX" || cmd == "TeX") {
                for (char ch : std::string(cmd)) out.push_back(CharAtom(static_cast<unsigned char>(ch), MathAtomClass::Ord, start));
                return;
            }
            if (cmd.size() == 1 && CombiningForTextAccent(cmd[0])) {
                // \'e, \"o, \c{c} ...
                std::string arg = ReadArgRaw();
                size_t p = 0;
                const char32_t base = arg.empty() ? ' ' : DecodeUtf8(arg, p);
                if (const char32_t composed = ComposeTextAccent(cmd[0], base)) {
                    out.push_back(CharAtom(composed, MathAtomClass::Ord, start));
                } else {
                    out.push_back(CharAtom(base, MathAtomClass::Ord, start));
                    out.push_back(CharAtom(CombiningForTextAccent(cmd[0]), MathAtomClass::Ord, start));
                }
                return;
            }
            if (const MathSymbolInfo* sym = LookupMathSymbol(cmd)) {
                out.push_back(CharAtom(sym->codepoint, MathAtomClass::Ord, start));
                return;
            }
            if (ExpandMacro(cmd, start)) return;
            // Anything else: parse as a math command inside the text run.
            Context saved = ctx;
            MathList tmp;
            ParseCommand(cmd, tmp, Stops{}, start);
            ctx = saved;
            out.insert(out.end(), tmp.begin(), tmp.end());
            return;
        }
        const char32_t cp = DecodeUtf8(src, pos);
        out.push_back(CharAtom(cp, MathAtomClass::Ord, start));
    }

    // ----- delimiters -----
    // Reads a delimiter after \left, \right, \big...: returns the code point,
    // 0 for the null delimiter '.'. `ok` is false when nothing usable follows.
    char32_t ReadDelimiter(bool& ok) {
        ok = true;
        SkipSpaces();
        if (AtEnd()) { ok = false; return 0; }
        if (Peek() == '\\') {
            const size_t s = pos;
            const std::string cmd = ReadCommand();
            if (cmd == "{") return '{';
            if (cmd == "}") return '}';
            if (cmd == "|") return 0x2016;
            if (cmd == "\\") { pos = s; ok = false; return 0; }
            if (const MathSymbolInfo* sym = LookupMathSymbol(cmd)) return sym->codepoint;
            if (ExpandMacro(cmd, s)) return ReadDelimiter(ok);
            Report("unknown delimiter \\" + cmd, s, pos);
            ok = false;
            return 0;
        }
        const size_t s = pos;
        const char32_t cp = DecodeUtf8(src, pos);
        switch (cp) {
            case '.': return 0;
            case '<': return 0x27E8;
            case '>': return 0x27E9;
            case '(': case ')': case '[': case ']': case '|': case '/':
            case 0x27E8: case 0x27E9: case 0x2016: case 0x230A: case 0x230B: case 0x2308: case 0x2309:
            case 0x2191: case 0x2193: case 0x2195: case 0x21D1: case 0x21D3: case 0x21D5: case '{': case '}':
                return cp;
            default:
                pos = s; ok = false; return 0;
        }
    }

    // ----- environments -----
    void ParseColumnSpec(const std::string& spec, MathArrayData& data) {
        size_t i = 0;
        MathColumnSpec pending;   // accumulates | and @{} before the next column
        bool havePending = false;
        auto flush = [&](MathColumnAlign align) {
            pending.align = align;
            data.columns.push_back(pending);
            pending = MathColumnSpec{};
            havePending = false;
        };
        while (i < spec.size()) {
            const char c = spec[i];
            if (c == ' ' || c == '\n' || c == '\t') { ++i; continue; }
            if (c == '|') { ++pending.linesBefore; havePending = true; ++i; continue; }
            if (c == '@' || c == '!') {
                ++i;
                std::string inner;
                if (i < spec.size() && spec[i] == '{') {
                    int level = 0; size_t s = i + 1;
                    for (; i < spec.size(); ++i) {
                        if (spec[i] == '{') ++level;
                        else if (spec[i] == '}') { if (--level == 0) break; }
                    }
                    inner = spec.substr(s, i - s);
                    ++i;
                }
                if (c == '@') {
                    pending.customSepBefore = true;
                    // Parse the separator content as math.
                    UltraCanvasMathParser::Impl sub;
                    sub.diags = diags; sub.src = inner; sub.ctx = ctx; sub.macros = macros; sub.colors = colors;
                    pending.sepBefore = sub.ParseList(Stops{});
                }
                havePending = true;
                continue;
            }
            if (c == '>') {
                ++i;
                if (i < spec.size() && spec[i] == '{') {
                    int level = 0; size_t s = i + 1;
                    for (; i < spec.size(); ++i) {
                        if (spec[i] == '{') ++level;
                        else if (spec[i] == '}') { if (--level == 0) break; }
                    }
                    pending.prefixSource += spec.substr(s, i - s);
                    ++i;
                }
                havePending = true;
                continue;
            }
            if (c == '<') {   // <{...} suffix: ignored
                ++i;
                if (i < spec.size() && spec[i] == '{') {
                    int level = 0;
                    for (; i < spec.size(); ++i) {
                        if (spec[i] == '{') ++level;
                        else if (spec[i] == '}') { if (--level == 0) break; }
                    }
                    ++i;
                }
                continue;
            }
            if (c == '*') {
                // *{n}{spec}
                ++i;
                std::string n, inner;
                if (i < spec.size() && spec[i] == '{') {
                    size_t s = ++i; while (i < spec.size() && spec[i] != '}') ++i; n = spec.substr(s, i - s); ++i;
                }
                if (i < spec.size() && spec[i] == '{') {
                    int level = 0; size_t s = i + 1;
                    for (; i < spec.size(); ++i) {
                        if (spec[i] == '{') ++level;
                        else if (spec[i] == '}') { if (--level == 0) break; }
                    }
                    inner = spec.substr(s, i - s); ++i;
                }
                std::string repeated;
                for (int k = 0; k < std::atoi(n.c_str()); ++k) repeated += inner;
                MathArrayData tmp;
                ParseColumnSpec(repeated, tmp);
                for (auto& col : tmp.columns) data.columns.push_back(col);
                continue;
            }
            if (c == 'l') { flush(MathColumnAlign::Left); ++i; continue; }
            if (c == 'c') { flush(MathColumnAlign::Center); ++i; continue; }
            if (c == 'r') { flush(MathColumnAlign::Right); ++i; continue; }
            if (c == 'p' || c == 'm' || c == 'b') {
                ++i;
                if (i < spec.size() && spec[i] == '{') { while (i < spec.size() && spec[i] != '}') ++i; ++i; }
                flush(MathColumnAlign::Left);
                continue;
            }
            if (auto it = columnTypes.find(std::string(1, c)); it != columnTypes.end()) {
                MathArrayData tmp;
                ParseColumnSpec(it->second, tmp);
                for (auto& col : tmp.columns) {
                    if (havePending && &col == &tmp.columns.front()) {
                        col.linesBefore += pending.linesBefore;
                        if (!col.customSepBefore && pending.customSepBefore) { col.customSepBefore = true; col.sepBefore = pending.sepBefore; }
                        col.prefixSource = pending.prefixSource + col.prefixSource;
                        pending = MathColumnSpec{}; havePending = false;
                    }
                    data.columns.push_back(col);
                }
                ++i;
                continue;
            }
            ++i;   // unknown spec letters are skipped
        }
        if (havePending) {
            if (!data.columns.empty()) {
                data.columns.back().linesAfter = pending.linesBefore;
                data.columns.back().customSepAfter = pending.customSepBefore;
                data.columns.back().sepAfter = pending.sepBefore;
            }
        }
    }

    MathAtomPtr ParseEnvironment(size_t start) {
        std::string name;
        if (!ReadGroupRaw(name)) { return MakeError("\\begin", start, pos, "\\begin without a name"); }
        // Trim
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());

        // User-defined environment: splice begin body, parse to \end, splice end body.
        if (auto it = environments.find(name); it != environments.end()) {
            const Environment env = it->second;
            std::vector<std::string> args;
            for (int i = 0; i < env.argCount; ++i) args.push_back(ReadArgRaw());
            auto substitute = [&](const std::string& body) {
                std::string outText;
                for (size_t i = 0; i < body.size(); ++i) {
                    if (body[i] == '#' && i + 1 < body.size() && std::isdigit(static_cast<unsigned char>(body[i + 1]))) {
                        const int n = body[i + 1] - '0';
                        if (n >= 1 && n <= static_cast<int>(args.size())) outText += args[n - 1];
                        ++i;
                    } else outText.push_back(body[i]);
                }
                return outText;
            };
            // Find the matching \end{name}
            const size_t bodyStart = pos;
            int level = 0;
            size_t p = pos, endStart = std::string::npos, endStop = std::string::npos;
            while (p < src.size()) {
                if (src.compare(p, 7, "\\begin{") == 0) { ++level; p += 7; continue; }
                if (src.compare(p, 5, "\\end{") == 0) {
                    size_t q = p + 5; size_t e = src.find('}', q);
                    if (e == std::string::npos) break;
                    if (level == 0 && src.substr(q, e - q) == name) { endStart = p; endStop = e + 1; break; }
                    if (level > 0) --level;
                    p = e + 1; continue;
                }
                ++p;
            }
            if (endStart == std::string::npos) return MakeError("\\begin{" + name + "}", start, pos, "missing \\end{" + name + "}");
            const std::string inner = src.substr(bodyStart, endStart - bodyStart);
            src = src.substr(0, bodyStart) + "{" + substitute(env.begin) + inner + substitute(env.end) + "}" + src.substr(endStop);
            pos = bodyStart;
            MathList inside = ParseGroup();
            return MathAtom::MakeRow(std::move(inside));
        }

        auto data = std::make_shared<MathArrayData>();
        Context saved = ctx;
        bool isMatrix = false;
        std::string opt;
        if (name == "array" || name == "tabular" || name == "subarray" || name == "darray") {
            ReadOptionalRaw(opt);   // [t] [b]
            std::string spec; ReadGroupRaw(spec);
            ParseColumnSpec(spec, *data);
            data->kind = MathArrayKind::Array;
            if (name == "subarray") { data->scriptStyleCells = true; data->leftColSep = false; data->rightColSep = false; }
            if (name == "darray") data->displayStyleCells = true;
        } else if (name.size() >= 6 && name.compare(name.size() - 6, 6, "matrix") == 0) {
            isMatrix = true;
            data->kind = MathArrayKind::Matrix;
            data->leftColSep = false;    // amsmath: \hskip-\arraycolsep at both ends
            data->rightColSep = false;
            std::string base = name;
            bool star = false;
            if (!base.empty() && base.back() == '*') { base.pop_back(); star = true; }
            if (star) { ReadOptionalRaw(opt); }
            if (base == "pmatrix") { data->leftDelim = '('; data->rightDelim = ')'; }
            else if (base == "bmatrix") { data->leftDelim = '['; data->rightDelim = ']'; }
            else if (base == "Bmatrix") { data->leftDelim = '{'; data->rightDelim = '}'; }
            else if (base == "vmatrix") { data->leftDelim = '|'; data->rightDelim = '|'; }
            else if (base == "Vmatrix") { data->leftDelim = 0x2016; data->rightDelim = 0x2016; }
            else if (base == "smallmatrix") data->scriptStyleCells = true;
            MathColumnAlign align = MathColumnAlign::Center;
            if (star && !opt.empty()) align = opt[0] == 'l' ? MathColumnAlign::Left : opt[0] == 'r' ? MathColumnAlign::Right : MathColumnAlign::Center;
            MathColumnSpec col; col.align = align;
            data->columns.assign(1, col);   // widened to the row length below
        } else if (name == "cases" || name == "dcases" || name == "rcases" || name == "drcases") {
            data->kind = MathArrayKind::Cases;
            MathColumnSpec l; l.align = MathColumnAlign::Left;
            data->columns = {l, l};
            data->leftColSep = false;
            if (name[0] == 'r' || name == "drcases") { data->rightDelim = '}'; } else { data->leftDelim = '{'; }
            if (name[0] == 'd') data->displayStyleCells = true;
        } else if (name == "align" || name == "align*" || name == "aligned" || name == "flalign" || name == "flalign*" ||
                   name == "alignat" || name == "alignat*" || name == "alignedat" || name == "split" ||
                   name == "eqnarray" || name == "eqnarray*") {
            data->kind = name == "split" ? MathArrayKind::Split : (name == "aligned" || name == "alignedat") ? MathArrayKind::Aligned : MathArrayKind::Align;
            if (name.rfind("alignat", 0) == 0 || name == "alignedat") { std::string n; ReadGroupRaw(n); data->kind = MathArrayKind::AlignAt; }
            if (name.rfind("eqnarray", 0) == 0) {
                data->kind = MathArrayKind::Eqnarray;
                MathColumnSpec r; r.align = MathColumnAlign::Right;
                MathColumnSpec c; c.align = MathColumnAlign::Center;
                MathColumnSpec l; l.align = MathColumnAlign::Left;
                data->columns = {r, c, l};
            } else {
                MathColumnSpec r; r.align = MathColumnAlign::Right;
                MathColumnSpec l; l.align = MathColumnAlign::Left;
                data->columns = {r, l};   // extended in pairs below
            }
            data->textStyleCells = false;
        } else if (name == "gather" || name == "gather*" || name == "gathered") {
            data->kind = MathArrayKind::Gather;
            MathColumnSpec c; data->columns = {c};
            data->textStyleCells = false;
        } else if (name == "multline" || name == "multline*") {
            data->kind = MathArrayKind::Multline;
            MathColumnSpec c; data->columns = {c};
            data->textStyleCells = false;
        } else if (name == "equation" || name == "equation*" || name == "math" || name == "displaymath" ||
                   name == "center" || name == "minipage" || name == "figure" || name == "document") {
            if (name == "minipage") { ReadOptionalRaw(opt); std::string w; ReadGroupRaw(w); }
            MathList inside;
            for (;;) {
                SkipSpaces();
                if (AtEnd()) break;
                if (Peek() == '\\' && PeekCommand() == "end") break;
                ParseItem(inside, Stops{});
            }
            ConsumeEnd(name, start);
            return MathAtom::MakeRow(std::move(inside));
        } else {
            return MakeError("\\begin{" + name + "}", start, pos, "unknown environment '" + name + "'");
        }

        // ----- rows and cells -----
        MathRow row;
        MathCell cell;
        int pendingHlines = 0;
        bool rowHasContent = false;
        const bool alignKind = data->kind == MathArrayKind::Align || data->kind == MathArrayKind::Aligned ||
                               data->kind == MathArrayKind::AlignAt || data->kind == MathArrayKind::Split;
        auto finishCell = [&]() {
            cell.background = pendingCellColor; pendingCellColor = kMathColorNone;
            row.cells.push_back(std::move(cell));
            cell = MathCell{};
        };
        auto finishRow = [&]() {
            finishCell();
            row.hlinesBefore = pendingHlines; pendingHlines = 0;
            row.background = pendingRowColor; pendingRowColor = kMathColorNone;
            data->rows.push_back(std::move(row));
            row = MathRow{};
            rowHasContent = false;
        };
        for (;;) {
            SkipSpaces();
            if (AtEnd()) { Report("missing \\end{" + name + "}", start, pos); break; }
            // Row-start commands
            if (Peek() == '\\') {
                const size_t cmdStart = pos;
                const std::string cmd = PeekCommand();
                if (cmd == "hline" || cmd == "toprule" || cmd == "midrule" || cmd == "bottomrule" || cmd == "cline") {
                    ReadCommand();
                    if (cmd == "cline") { std::string r; ReadGroupRaw(r); }
                    ++pendingHlines;
                    continue;
                }
                if (cmd == "rowcolor") {
                    ReadCommand();
                    std::string spec; ReadGroupRaw(spec);
                    MathColor col = kMathColorNone;
                    if (!ParseColorSpec(spec, col)) Report("unknown colour '" + spec + "'", cmdStart, pos);
                    pendingRowColor = col;
                    continue;
                }
                if (cmd == "intertext" && row.cells.empty() && cell.content.empty()) {
                    ReadCommand();
                    MathCell tc;
                    tc.intertext = true;
                    tc.content = ParseTextArg();
                    row.cells.push_back(std::move(tc));
                    row.hlinesBefore = pendingHlines; pendingHlines = 0;
                    data->rows.push_back(std::move(row));
                    row = MathRow{};
                    SkipSpaces();
                    // an \\ after \intertext is optional
                    if (Peek() == '\\' && PeekCommand() == "\\") { ReadCommand(); }
                    continue;
                }
                if (cmd == "multicolumn" && cell.content.empty()) {
                    ReadCommand();
                    std::string n, spec; ReadGroupRaw(n); ReadGroupRaw(spec);
                    cell.colSpan = std::max(1, std::atoi(n.c_str()));
                    MathArrayData tmp; ParseColumnSpec(spec, tmp);
                    if (!tmp.columns.empty()) { cell.hasOwnSpec = true; cell.spec = tmp.columns.front(); }
                    MathList inner = ParseArg();
                    cell.content.insert(cell.content.end(), inner.begin(), inner.end());
                    rowHasContent = true;
                    continue;
                }
                if (cmd == "hdotsfor" && cell.content.empty()) {
                    ReadCommand();
                    std::string o; ReadOptionalRaw(o);
                    std::string n; ReadGroupRaw(n);
                    cell.colSpan = std::max(1, std::atoi(n.c_str()));
                    cell.dotsFill = true;
                    rowHasContent = true;
                    continue;
                }
                if (cmd == "end") {
                    ReadCommand();
                    std::string endName; ReadGroupRaw(endName);
                    while (!endName.empty() && std::isspace(static_cast<unsigned char>(endName.back()))) endName.pop_back();
                    if (endName != name) Report("\\end{" + endName + "} does not match \\begin{" + name + "}", cmdStart, pos);
                    // Drop an empty trailing row (the usual "\\ \hline" ending)
                    if (!rowHasContent && row.cells.empty() && cell.content.empty()) {
                        data->hlinesAfter = pendingHlines;
                    } else {
                        finishRow();
                    }
                    break;
                }
                if (cmd == "\\" || cmd == "cr") {
                    ReadCommand();
                    if (Peek() == '*') ++pos;
                    std::string len;
                    if (ReadOptionalRaw(len)) { float em = 0; ParseMathLength(len, em); row.extraSkip = em; }
                    finishRow();
                    continue;
                }
            }
            if (Peek() == '&') {
                ++pos;
                finishCell();
                rowHasContent = true;
                continue;
            }
            // Cell content: column prefix (>{...}) then the material up to & or \\.
            {
                const size_t colIndex = row.cells.size();
                if (colIndex < data->columns.size() && !data->columns[colIndex].prefixSource.empty() && cell.content.empty()) {
                    // Splice the prefix into the source before the cell.
                    src = src.substr(0, pos) + data->columns[colIndex].prefixSource + " " + src.substr(pos);
                }
            }
            MathList content = ParseList(Stops{true, false, false, false});
            if (!content.empty()) rowHasContent = true;
            cell.content.insert(cell.content.end(), content.begin(), content.end());
            if (AtEnd()) { Report("missing \\end{" + name + "}", start, pos); finishRow(); break; }
        }
        ctx = saved;

        // Widen implicit column lists (matrices, align pairs) to the longest row.
        size_t maxCells = 0;
        for (const auto& r : data->rows) {
            size_t n = 0;
            for (const auto& c : r.cells) n += static_cast<size_t>(c.colSpan);
            maxCells = std::max(maxCells, n);
        }
        if (isMatrix || data->kind == MathArrayKind::Gather || data->kind == MathArrayKind::Multline) {
            MathColumnSpec col = data->columns.empty() ? MathColumnSpec{} : data->columns.front();
            data->columns.assign(std::max<size_t>(1, maxCells), col);
        } else if (alignKind) {
            MathColumnSpec r; r.align = MathColumnAlign::Right;
            MathColumnSpec l; l.align = MathColumnAlign::Left;
            data->columns.clear();
            for (size_t i = 0; i < std::max<size_t>(2, maxCells); ++i) data->columns.push_back(i % 2 == 0 ? r : l);
        } else if (data->kind == MathArrayKind::Cases) {
            MathColumnSpec l; l.align = MathColumnAlign::Left;
            data->columns.assign(std::max<size_t>(2, maxCells), l);
        }
        auto atom = MathAtom::Make(MathAtomKind::Array, isMatrix ? MathAtomClass::Ord : MathAtomClass::Ord);
        if (data->leftDelim || data->rightDelim) atom->atomClass = MathAtomClass::Inner;
        atom->array = data;
        atom->sourceStart = static_cast<int>(start);
        atom->sourceEnd = static_cast<int>(pos);
        return atom;
    }

    void ConsumeEnd(const std::string& name, size_t start) {
        SkipSpaces();
        if (Peek() == '\\' && PeekCommand() == "end") {
            ReadCommand();
            std::string endName; ReadGroupRaw(endName);
            while (!endName.empty() && std::isspace(static_cast<unsigned char>(endName.back()))) endName.pop_back();
            if (endName != name) Report("\\end{" + endName + "} does not match \\begin{" + name + "}", start, pos);
        } else {
            Report("missing \\end{" + name + "}", start, pos);
        }
    }

    // ----- commands -----
    void ParseCommand(const std::string& cmd, MathList& out, const Stops& stops, size_t start) {
        // Font-family switches with an argument.
        struct FontCmd { const char* name; MathFontFamily fam; MathFontShape shape; int bold; };   // bold: -1 keep, 0 no, 1 yes
        static const FontCmd fontCmds[] = {
            {"mathrm", MathFontFamily::Serif, MathFontShape::Upright, 0}, {"mathit", MathFontFamily::Serif, MathFontShape::Italic, -1},
            {"mathbf", MathFontFamily::Serif, MathFontShape::Upright, 1}, {"mathbfit", MathFontFamily::Serif, MathFontShape::Italic, 1},
            {"boldsymbol", MathFontFamily::Serif, MathFontShape::Auto, 1}, {"bm", MathFontFamily::Serif, MathFontShape::Auto, 1},
            {"pmb", MathFontFamily::Serif, MathFontShape::Auto, 1}, {"bold", MathFontFamily::Serif, MathFontShape::Upright, 1},
            {"mathsf", MathFontFamily::Sans, MathFontShape::Upright, 0}, {"mathsfit", MathFontFamily::Sans, MathFontShape::Italic, 0},
            {"mathsfbf", MathFontFamily::Sans, MathFontShape::Upright, 1}, {"mathbfsf", MathFontFamily::Sans, MathFontShape::Upright, 1},
            {"mathtt", MathFontFamily::Mono, MathFontShape::Upright, 0}, {"mathcal", MathFontFamily::Script, MathFontShape::Upright, 0},
            {"mathscr", MathFontFamily::Script, MathFontShape::Upright, 0}, {"mathbfcal", MathFontFamily::Script, MathFontShape::Upright, 1},
            {"mathfrak", MathFontFamily::Fraktur, MathFontShape::Upright, 0}, {"mathbffrak", MathFontFamily::Fraktur, MathFontShape::Upright, 1},
            {"mathbb", MathFontFamily::DoubleStruck, MathFontShape::Upright, 0}, {"mathds", MathFontFamily::DoubleStruck, MathFontShape::Upright, 0},
            {"Bbb", MathFontFamily::DoubleStruck, MathFontShape::Upright, 0}, {"mathnormal", MathFontFamily::Serif, MathFontShape::Auto, 0},
        };
        for (const auto& fc : fontCmds) {
            if (cmd == fc.name) {
                Context saved = ctx;
                ctx.font.family = fc.fam;
                if (!(fc.shape == MathFontShape::Auto && fc.bold == 1)) ctx.font.shape = fc.shape;   // \boldsymbol keeps the shape
                if (fc.bold >= 0) ctx.font.bold = fc.bold == 1;
                MathList inner = ParseArg();
                ctx = saved;
                auto row = MathAtom::MakeRow(std::move(inner));
                if (row->body.size() == 1) { out.push_back(row->body.front()); return; }
                out.push_back(row);
                return;
            }
        }
        // Font switches for the rest of the group (\rm \bf ...).
        struct SwitchCmd { const char* name; MathFontFamily fam; MathFontShape shape; int bold; };
        static const SwitchCmd switchCmds[] = {
            {"rm", MathFontFamily::Serif, MathFontShape::Upright, 0}, {"it", MathFontFamily::Serif, MathFontShape::Italic, -1},
            {"bf", MathFontFamily::Serif, MathFontShape::Upright, 1}, {"sf", MathFontFamily::Sans, MathFontShape::Upright, 0},
            {"tt", MathFontFamily::Mono, MathFontShape::Upright, 0}, {"cal", MathFontFamily::Script, MathFontShape::Upright, 0},
            {"frak", MathFontFamily::Fraktur, MathFontShape::Upright, 0}, {"bfseries", MathFontFamily::Serif, MathFontShape::Auto, 1},
            {"itshape", MathFontFamily::Serif, MathFontShape::Italic, -1}, {"rmfamily", MathFontFamily::Serif, MathFontShape::Auto, -1},
            {"sffamily", MathFontFamily::Sans, MathFontShape::Auto, -1}, {"ttfamily", MathFontFamily::Mono, MathFontShape::Auto, -1},
            {"upshape", MathFontFamily::Serif, MathFontShape::Upright, -1}, {"mdseries", MathFontFamily::Serif, MathFontShape::Auto, 0},
        };
        for (const auto& sc : switchCmds) {
            if (cmd == sc.name) {
                ctx.font.family = sc.fam;
                if (sc.shape != MathFontShape::Auto || sc.bold != 1) ctx.font.shape = sc.shape == MathFontShape::Auto ? ctx.font.shape : sc.shape;
                if (sc.bold >= 0) ctx.font.bold = sc.bold == 1;
                return;
            }
        }
        // Text
        if (cmd == "text" || cmd == "mbox" || cmd == "hbox" || cmd == "textrm" || cmd == "textnormal" ||
            cmd == "textup" || cmd == "textmd" || cmd == "textbf" || cmd == "textit" || cmd == "textsf" ||
            cmd == "texttt" || cmd == "emph" || cmd == "textsl" || cmd == "textsc" || cmd == "textrm") {
            Context saved = ctx;
            ctx.font = MathFontStyle{};
            ctx.font.shape = MathFontShape::Upright;
            if (cmd == "textbf") ctx.font.bold = true;
            if (cmd == "textit" || cmd == "emph" || cmd == "textsl") ctx.font.shape = MathFontShape::Italic;
            if (cmd == "textsf") ctx.font.family = MathFontFamily::Sans;
            if (cmd == "texttt") ctx.font.family = MathFontFamily::Mono;
            if (cmd == "mbox" || cmd == "hbox") {
                // \mbox inherits the surrounding math font family (\mathbf{\mbox{..}} is bold)
                ctx.font.bold = saved.font.bold;
                ctx.font.family = saved.font.family;
                if (saved.font.shape == MathFontShape::Italic) ctx.font.shape = MathFontShape::Italic;
            }
            MathList body = ParseTextArg();
            ctx = saved;
            out.push_back(TextAtom(std::move(body), start));
            return;
        }
        if (cmd == "LaTeX" || cmd == "TeX") {
            Context saved = ctx;
            ctx.font = MathFontStyle{}; ctx.font.shape = MathFontShape::Upright; ctx.textMode = true;
            MathList body;
            for (char ch : cmd) body.push_back(CharAtom(static_cast<unsigned char>(ch), MathAtomClass::Ord, start));
            ctx = saved;
            out.push_back(TextAtom(std::move(body), start));
            return;
        }
        // Styles
        if (cmd == "displaystyle" || cmd == "textstyle" || cmd == "scriptstyle" || cmd == "scriptscriptstyle") {
            auto a = StyleAtom(cmd == "displaystyle" ? MathStyle::Display() : cmd == "textstyle" ? MathStyle::Text()
                               : cmd == "scriptstyle" ? MathStyle{MathStyleKind::Script, false} : MathStyle{MathStyleKind::ScriptScript, false});
            a->body = ParseList(stops);
            out.push_back(a);
            return;
        }
        for (size_t i = 0; i < 10; ++i) {
            if (cmd == kSizeCommands[i]) {
                auto a = MathAtom::Make(MathAtomKind::Style, MathAtomClass::None);
                a->sizeScale = kSizeScales[i];
                a->body = ParseList(stops);
                out.push_back(a);
                return;
            }
        }
        // Spacing
        if (cmd == ",") { out.push_back(SpaceAtom(3, true)); return; }
        if (cmd == ":" || cmd == ">") { out.push_back(SpaceAtom(4, true)); return; }
        if (cmd == ";") { out.push_back(SpaceAtom(5, true)); return; }
        if (cmd == "!") { out.push_back(SpaceAtom(-3, true)); return; }
        if (cmd == " " || cmd == "space" || cmd == "nobreakspace" || cmd == "nbsp") { out.push_back(SpaceAtom(0.333f)); return; }
        if (cmd == "thinspace") { out.push_back(SpaceAtom(3, true)); return; }
        if (cmd == "medspace") { out.push_back(SpaceAtom(4, true)); return; }
        if (cmd == "thickspace") { out.push_back(SpaceAtom(5, true)); return; }
        if (cmd == "negthinspace") { out.push_back(SpaceAtom(-3, true)); return; }
        if (cmd == "negmedspace") { out.push_back(SpaceAtom(-4, true)); return; }
        if (cmd == "negthickspace") { out.push_back(SpaceAtom(-5, true)); return; }
        if (cmd == "enspace" || cmd == "enskip") { out.push_back(SpaceAtom(0.5f)); return; }
        if (cmd == "quad") { out.push_back(SpaceAtom(1.f)); return; }
        if (cmd == "qquad") { out.push_back(SpaceAtom(2.f)); return; }
        if (cmd == "hspace" || cmd == "hspace*" || cmd == "vspace" || cmd == "vspace*") {
            std::string len; if (!ReadGroupRaw(len)) len = ReadBareLength();
            float em = 0; if (!ParseMathLength(len, em)) Report("bad length '" + len + "'", start, pos);
            if (cmd[0] == 'h') out.push_back(SpaceAtom(em));
            return;
        }
        if (cmd == "kern" || cmd == "hskip" || cmd == "mkern" || cmd == "mskip") {
            std::string len = ReadBareLength();
            float em = 0; if (!ParseMathLength(len, em)) Report("bad length '" + len + "'", start, pos);
            out.push_back(SpaceAtom(em));
            return;
        }
        if (cmd == "phantom" || cmd == "hphantom" || cmd == "vphantom") {
            auto a = MathAtom::Make(MathAtomKind::Phantom);
            a->phantomWidth = cmd != "vphantom";
            a->phantomHeight = cmd != "hphantom";
            a->body = ParseArg();
            out.push_back(a);
            return;
        }
        if (cmd == "smash") {
            auto a = MathAtom::Make(MathAtomKind::Smash);
            std::string o; ReadOptionalRaw(o);
            a->phantomHeight = o.find('t') != std::string::npos || o.empty();
            a->phantomWidth = o.find('b') != std::string::npos || o.empty();   // reused: width=bottom
            a->body = ParseArg();
            out.push_back(a);
            return;
        }
        if (cmd == "rule") {
            std::string raise, w, h;
            ReadOptionalRaw(raise); ReadGroupRaw(w); ReadGroupRaw(h);
            float r = 0, we = 0, he = 0;
            ParseMathLength(raise, r); ParseMathLength(w, we); ParseMathLength(h, he);
            auto a = MathAtom::Make(MathAtomKind::Rule);
            a->ruleWidthEm = we; a->ruleHeightEm = he + r; a->ruleDepthEm = -r;
            out.push_back(a);
            return;
        }
        if (cmd == "strut" || cmd == "mathstrut") {
            auto a = MathAtom::Make(MathAtomKind::Rule);
            a->ruleWidthEm = 0; a->ruleHeightEm = cmd == "strut" ? 0.7f : 0.75f; a->ruleDepthEm = cmd == "strut" ? 0.3f : 0.25f;
            out.push_back(a);
            return;
        }
        // Fractions
        if (cmd == "frac" || cmd == "dfrac" || cmd == "tfrac" || cmd == "cfrac" || cmd == "sfrac" || cmd == "nicefrac") {
            auto a = MathAtom::Make(MathAtomKind::Fraction, MathAtomClass::Inner);
            std::string o;
            if (cmd == "cfrac") ReadOptionalRaw(o);
            a->numerator = ParseArg();
            a->denominator = ParseArg();
            a->fracDisplayStyle = cmd == "dfrac" || cmd == "cfrac";
            a->fracTextStyle = cmd == "tfrac";
            if (cmd == "cfrac" && !o.empty()) {
                // \cfrac[l] / [r]: alignment of the numerator; kept centred (rare)
            }
            a->sourceStart = static_cast<int>(start); a->sourceEnd = static_cast<int>(pos);
            out.push_back(a);
            return;
        }
        if (cmd == "binom" || cmd == "dbinom" || cmd == "tbinom") {
            auto a = MathAtom::Make(MathAtomKind::Fraction, MathAtomClass::Inner);
            a->numerator = ParseArg();
            a->denominator = ParseArg();
            a->ruleThicknessEm = 0.f;
            a->fracLeftDelim = '('; a->fracRightDelim = ')';
            a->fracDisplayStyle = cmd == "dbinom";
            a->fracTextStyle = cmd == "tbinom";
            out.push_back(a);
            return;
        }
        if (cmd == "genfrac") {
            std::string l, r, thick, style;
            ReadGroupRaw(l); ReadGroupRaw(r); ReadGroupRaw(thick); ReadGroupRaw(style);
            auto a = MathAtom::Make(MathAtomKind::Fraction, MathAtomClass::Inner);
            auto delimOf = [&](std::string s) -> uint32_t {
                while (!s.empty() && s.front() == ' ') s.erase(s.begin());
                if (s.empty()) return 0;
                if (s[0] == '\\') { if (const auto* sym = LookupMathSymbol(s.substr(1))) return sym->codepoint; if (s == "\\{") return '{'; if (s == "\\}") return '}'; if (s == "\\|") return 0x2016; return 0; }
                size_t p = 0; const char32_t cp = DecodeUtf8(s, p);
                return cp == '.' ? 0 : cp;
            };
            a->fracLeftDelim = delimOf(l); a->fracRightDelim = delimOf(r);
            float t = -1; if (!thick.empty() && ParseMathLength(thick, t)) a->ruleThicknessEm = t;
            if (style == "0") a->fracDisplayStyle = true; else if (style == "1") a->fracTextStyle = true;
            a->numerator = ParseArg();
            a->denominator = ParseArg();
            out.push_back(a);
            return;
        }
        if (cmd == "over" || cmd == "atop" || cmd == "choose" || cmd == "brace" || cmd == "brack" || cmd == "above") {
            // The material before becomes the numerator, the rest of the group the denominator.
            auto a = MathAtom::Make(MathAtomKind::Fraction, MathAtomClass::Inner);
            a->numerator = out;
            out.clear();
            if (cmd == "above") { std::string len = ReadBareLength(); float t = 0; ParseMathLength(len, t); a->ruleThicknessEm = t; }
            if (cmd != "over" && cmd != "above") a->ruleThicknessEm = 0.f;
            if (cmd == "choose") { a->fracLeftDelim = '('; a->fracRightDelim = ')'; }
            if (cmd == "brace") { a->fracLeftDelim = '{'; a->fracRightDelim = '}'; }
            if (cmd == "brack") { a->fracLeftDelim = '['; a->fracRightDelim = ']'; }
            a->denominator = ParseList(stops);
            out.push_back(a);
            return;
        }
        if (cmd == "sqrt") {
            auto a = MathAtom::Make(MathAtomKind::Radical);
            std::string deg;
            if (ReadOptionalRaw(deg)) {
                UltraCanvasMathParser::Impl sub;
                sub.diags = diags; sub.src = deg; sub.ctx = ctx; sub.macros = macros; sub.colors = colors;
                a->degree = sub.ParseList(Stops{});
            }
            a->body = ParseArg();
            a->sourceStart = static_cast<int>(start); a->sourceEnd = static_cast<int>(pos);
            out.push_back(a);
            return;
        }
        // Delimiters
        if (cmd == "left") {
            bool ok;
            const uint32_t l = ReadDelimiter(ok);
            if (!ok) Report("\\left needs a delimiter", start, pos);
            auto a = MathAtom::Make(MathAtomKind::Fence, MathAtomClass::Inner);
            a->leftDelim = l;
            Stops inner = stops; inner.fence = true; inner.cell = false; inner.lines = false;
            Context saved = ctx;
            for (;;) {
                MathList part = ParseList(inner);
                a->body.insert(a->body.end(), part.begin(), part.end());
                SkipSpaces();
                if (Peek() == '\\' && PeekCommand() == "middle") {
                    ReadCommand();
                    bool mok; const uint32_t m = ReadDelimiter(mok);
                    a->middleIndices.push_back(a->body.size());
                    a->body.push_back(CharAtom(m, MathAtomClass::Ord, pos));
                    continue;
                }
                break;
            }
            ctx = saved;
            SkipSpaces();
            if (Peek() == '\\' && PeekCommand() == "right") {
                ReadCommand();
                bool rok; a->rightDelim = ReadDelimiter(rok);
                if (!rok) Report("\\right needs a delimiter", start, pos);
            } else {
                Report("missing \\right", start, pos);
            }
            a->sourceStart = static_cast<int>(start); a->sourceEnd = static_cast<int>(pos);
            out.push_back(a);
            return;
        }
        if (cmd == "right" || cmd == "middle") {
            Report("\\" + cmd + " without \\left", start, pos);
            bool ok; ReadDelimiter(ok);
            return;
        }
        {
            static const char* bigNames[] = {"big", "Big", "bigg", "Bigg"};
            for (int i = 0; i < 4; ++i) {
                const std::string base = bigNames[i];
                if (cmd == base || cmd == base + "l" || cmd == base + "r" || cmd == base + "m") {
                    bool ok; const uint32_t d = ReadDelimiter(ok);
                    if (!ok) { Report("\\" + cmd + " needs a delimiter", start, pos); return; }
                    auto a = MathAtom::Make(MathAtomKind::BigDelim);
                    a->codepoint = d; a->bigSize = i + 1;
                    a->atomClass = cmd == base ? MathAtomClass::Ord : cmd.back() == 'l' ? MathAtomClass::Open
                                   : cmd.back() == 'r' ? MathAtomClass::Close : MathAtomClass::Rel;
                    out.push_back(a);
                    return;
                }
            }
        }
        // Scripts modifiers
        if (cmd == "limits" || cmd == "nolimits" || cmd == "displaylimits") {
            const MathLimits lim = cmd == "limits" ? MathLimits::Limits : cmd == "nolimits" ? MathLimits::NoLimits : MathLimits::Auto;
            if (!out.empty()) {
                if (out.back()->kind == MathAtomKind::Scripts) out.back()->limits = lim;
                else {
                    auto s = MathAtom::Make(MathAtomKind::Scripts, out.back()->atomClass);
                    s->nucleus = out.back(); s->limits = lim;
                    out.back() = s;
                }
            }
            return;
        }
        // Atom class wrappers
        {
            struct ClassCmd { const char* name; MathAtomClass cls; };
            static const ClassCmd classCmds[] = {
                {"mathop", MathAtomClass::Op}, {"mathbin", MathAtomClass::Bin}, {"mathrel", MathAtomClass::Rel},
                {"mathord", MathAtomClass::Ord}, {"mathopen", MathAtomClass::Open}, {"mathclose", MathAtomClass::Close},
                {"mathpunct", MathAtomClass::Punct}, {"mathinner", MathAtomClass::Inner},
            };
            for (const auto& cc : classCmds) {
                if (cmd == cc.name) {
                    MathList inner = ParseArg();
                    auto row = MathAtom::MakeRow(std::move(inner), cc.cls);
                    out.push_back(row);
                    return;
                }
            }
        }
        if (cmd == "operatorname" || cmd == "operatorname*" || cmd == "DeclareMathOperator" || cmd == "DeclareMathOperator*") {
            if (cmd.rfind("DeclareMathOperator", 0) == 0) {
                std::string n, body; ReadGroupRaw(n); ReadGroupRaw(body);
                if (!n.empty() && n[0] == '\\') n.erase(0, 1);
                operators[n] = body;
                if (cmd.back() == '*') starredOperators.insert(n);
                return;
            }
            Context saved = ctx;
            ctx.font = MathFontStyle{}; ctx.font.shape = MathFontShape::Upright;
            MathList body = ParseTextArg();
            ctx = saved;
            auto t = TextAtom(std::move(body), start);
            auto row = MathAtom::MakeRow({t}, MathAtomClass::Op);
            row->opIsNamed = true;
            auto s = MathAtom::Make(MathAtomKind::Scripts, MathAtomClass::Op);
            s->nucleus = row;
            s->limits = cmd.back() == '*' ? MathLimits::Limits : MathLimits::NoLimits;
            out.push_back(s);
            return;
        }
        if (auto it = operators.find(cmd); it != operators.end()) {
            Context saved = ctx;
            ctx.font = MathFontStyle{}; ctx.font.shape = MathFontShape::Upright; ctx.textMode = true;
            UltraCanvasMathParser::Impl sub;
            sub.diags = diags; sub.src = it->second; sub.ctx = ctx; sub.macros = macros; sub.colors = colors;
            MathList body = sub.ParseText();
            ctx = saved;
            auto row = MathAtom::MakeRow({TextAtom(std::move(body), start)}, MathAtomClass::Op);
            row->opIsNamed = true;
            auto s = MathAtom::Make(MathAtomKind::Scripts, MathAtomClass::Op);
            s->nucleus = row;
            s->limits = starredOperators.count(cmd) ? MathLimits::Limits : MathLimits::NoLimits;
            out.push_back(s);
            return;
        }
        {
            bool limits = false;
            if (LookupNamedOperator(cmd, limits)) {
                Context saved = ctx;
                ctx.font = MathFontStyle{}; ctx.font.shape = MathFontShape::Upright; ctx.textMode = true;
                MathList body;
                for (char ch : cmd) body.push_back(CharAtom(static_cast<unsigned char>(ch), MathAtomClass::Ord, start));
                ctx = saved;
                auto row = MathAtom::MakeRow({TextAtom(std::move(body), start)}, MathAtomClass::Op);
                row->opIsNamed = true;
                auto s = MathAtom::Make(MathAtomKind::Scripts, MathAtomClass::Op);
                s->nucleus = row;
                s->limits = limits ? MathLimits::Auto : MathLimits::NoLimits;
                out.push_back(s);
                return;
            }
        }
        if (cmd == "pmod" || cmd == "pod" || cmd == "mod") {
            MathList inner = ParseArg();
            MathList row;
            row.push_back(SpaceAtom(cmd == "mod" ? 1.f : 0.8f));
            if (cmd != "pod") {
                MathList m;
                Context saved = ctx; ctx.font = MathFontStyle{}; ctx.font.shape = MathFontShape::Upright; ctx.textMode = true;
                for (char ch : std::string("mod")) m.push_back(CharAtom(static_cast<unsigned char>(ch), MathAtomClass::Ord, start));
                ctx = saved;
                row.push_back(TextAtom(std::move(m), start));
                row.push_back(SpaceAtom(cmd == "mod" ? 0.333f : 0.333f));
            }
            if (cmd != "mod") row.push_back(CharAtom('(', MathAtomClass::Open, start));
            row.insert(row.end(), inner.begin(), inner.end());
            if (cmd != "mod") row.push_back(CharAtom(')', MathAtomClass::Close, start));
            out.push_back(MathAtom::MakeRow(std::move(row)));
            return;
        }
        if (cmd == "sideset") {
            auto a = MathAtom::Make(MathAtomKind::Sideset, MathAtomClass::Op);
            MathList pre = ParseArg();
            MathList post = ParseArg();
            auto pull = [&](MathList& list, MathList& sub, MathList& sup, MathList& nucleus) {
                for (auto& atom : list) {
                    if (atom->kind == MathAtomKind::Scripts) {
                        if (atom->hasSub) sub.insert(sub.end(), atom->subscript.begin(), atom->subscript.end());
                        if (atom->hasSup) sup.insert(sup.end(), atom->superscript.begin(), atom->superscript.end());
                        if (atom->nucleus && !(atom->nucleus->kind == MathAtomKind::Row && atom->nucleus->body.empty()))
                            nucleus.push_back(atom->nucleus);
                    } else {
                        nucleus.push_back(atom);
                    }
                }
            };
            pull(pre, a->preSub, a->preSup, a->preNucleus);
            pull(post, a->postSub, a->postSup, a->postNucleus);
            a->body = ParseArg();
            out.push_back(a);
            return;
        }
        if (cmd == "prescript") {
            auto a = MathAtom::Make(MathAtomKind::Sideset, MathAtomClass::Ord);
            a->preSup = ParseArg();
            a->preSub = ParseArg();
            a->body = ParseArg();
            out.push_back(a);
            return;
        }
        if (cmd == "substack") {
            // Rows separated by \\ in script style, centred.
            SkipSpaces();
            std::string raw; ReadGroupRaw(raw);
            src = src.substr(0, pos) + "\\begin{subarray}{c}" + raw + "\\end{subarray}" + src.substr(pos);
            return;
        }
        // Accents
        if (const MathSymbolInfo* sym = LookupMathSymbol(cmd); sym && sym->combiningAccent) {
            auto a = MathAtom::Make(MathAtomKind::Accent);
            a->codepoint = sym->codepoint;
            a->accentStretchy = sym->stretchyAccent;
            a->body = ParseArg();
            a->sourceStart = static_cast<int>(start); a->sourceEnd = static_cast<int>(pos);
            out.push_back(a);
            return;
        }
        {
            struct OU { const char* name; MathOverUnderKind kind; char32_t cp; MathAtomClass cls; };
            static const OU ous[] = {
                {"overrightarrow", MathOverUnderKind::OverArrow, 0x2192, MathAtomClass::Ord},
                {"overleftarrow", MathOverUnderKind::OverArrow, 0x2190, MathAtomClass::Ord},
                {"overleftrightarrow", MathOverUnderKind::OverArrow, 0x2194, MathAtomClass::Ord},
                {"underrightarrow", MathOverUnderKind::UnderArrow, 0x2192, MathAtomClass::Ord},
                {"underleftarrow", MathOverUnderKind::UnderArrow, 0x2190, MathAtomClass::Ord},
                {"underleftrightarrow", MathOverUnderKind::UnderArrow, 0x2194, MathAtomClass::Ord},
                {"overbrace", MathOverUnderKind::Overbrace, 0x23DE, MathAtomClass::Op},
                {"underbrace", MathOverUnderKind::Underbrace, 0x23DF, MathAtomClass::Op},
                {"overbracket", MathOverUnderKind::Overbracket, 0x23B4, MathAtomClass::Op},
                {"underbracket", MathOverUnderKind::Underbracket, 0x23B5, MathAtomClass::Op},
                {"overparen", MathOverUnderKind::Overparen, 0x23DC, MathAtomClass::Op},
                {"underparen", MathOverUnderKind::Underparen, 0x23DD, MathAtomClass::Op},
                {"overline", MathOverUnderKind::Overline, 0, MathAtomClass::Ord},
                {"underline", MathOverUnderKind::Underline, 0, MathAtomClass::Ord},
                {"widebar", MathOverUnderKind::Overline, 0, MathAtomClass::Ord},
            };
            for (const auto& ou : ous) {
                if (cmd == ou.name) {
                    auto a = MathAtom::Make(MathAtomKind::OverUnder, ou.cls);
                    a->overUnderKind = ou.kind;
                    a->codepoint = ou.cp;
                    a->body = ParseArg();
                    a->sourceStart = static_cast<int>(start); a->sourceEnd = static_cast<int>(pos);
                    out.push_back(a);
                    return;
                }
            }
        }
        if (cmd == "overset" || cmd == "underset" || cmd == "stackrel" || cmd == "stackbin" || cmd == "overunderset") {
            auto a = MathAtom::Make(MathAtomKind::OverUnder, cmd == "stackrel" ? MathAtomClass::Rel : cmd == "stackbin" ? MathAtomClass::Bin : MathAtomClass::Ord);
            a->overUnderKind = cmd == "underset" ? MathOverUnderKind::Underset : MathOverUnderKind::Overset;
            if (cmd == "overunderset") { a->over = ParseArg(); a->under = ParseArg(); a->body = ParseArg(); }
            else {
                MathList first = ParseArg();
                if (cmd == "underset") a->under = std::move(first); else a->over = std::move(first);
                a->body = ParseArg();
                if (a->atomClass == MathAtomClass::Ord && !a->body.empty()) a->atomClass = a->body.front()->atomClass;
            }
            out.push_back(a);
            return;
        }
        {
            struct XA { const char* name; char32_t cp; };
            static const XA xas[] = {
                {"xrightarrow", 0x2192}, {"xleftarrow", 0x2190}, {"xleftrightarrow", 0x2194},
                {"xRightarrow", 0x21D2}, {"xLeftarrow", 0x21D0}, {"xLeftrightarrow", 0x21D4},
                {"xmapsto", 0x21A6}, {"xlongequal", '='}, {"xhookrightarrow", 0x21AA}, {"xhookleftarrow", 0x21A9},
                {"xrightharpoonup", 0x21C0}, {"xleftharpoonup", 0x21BC}, {"xrightharpoondown", 0x21C1},
                {"xleftharpoondown", 0x21BD}, {"xtofrom", 0x21C4}, {"xrightleftharpoons", 0x21CC},
                {"xleftrightharpoons", 0x21CB},
            };
            for (const auto& xa : xas) {
                if (cmd == xa.name) {
                    auto a = MathAtom::Make(MathAtomKind::OverUnder, MathAtomClass::Rel);
                    a->overUnderKind = MathOverUnderKind::XArrow;
                    a->codepoint = xa.cp;
                    std::string below;
                    if (ReadOptionalRaw(below)) {
                        UltraCanvasMathParser::Impl sub;
                        sub.diags = diags; sub.src = below; sub.ctx = ctx; sub.macros = macros; sub.colors = colors;
                        a->under = sub.ParseList(Stops{});
                    }
                    a->over = ParseArg();
                    out.push_back(a);
                    return;
                }
            }
        }
        if (cmd == "not") {
            MathList next;
            SkipSpaces();
            if (!AtEnd()) ParseItem(next, stops);
            if (next.size() == 1 && next.front()->kind == MathAtomKind::Char) {
                if (const char32_t neg = NegatedRelation(next.front()->codepoint)) {
                    next.front()->codepoint = neg;
                    out.push_back(next.front());
                    return;
                }
            }
            auto a = MathAtom::Make(MathAtomKind::Not, next.empty() ? MathAtomClass::Rel : next.front()->atomClass);
            a->body = std::move(next);
            out.push_back(a);
            return;
        }
        // Colour
        if (cmd == "color" || cmd == "textcolor" || cmd == "fgcolor") {
            std::string spec; ReadGroupRaw(spec);
            MathColor col = kMathColorBlack;
            if (!ParseColorSpec(spec, col)) Report("unknown colour '" + spec + "'", start, pos);
            auto a = MathAtom::Make(MathAtomKind::Color, MathAtomClass::None);
            a->color = col;
            if (cmd == "color") a->body = ParseList(stops);
            else a->body = ParseArg();
            out.push_back(a);
            return;
        }
        if (cmd == "colorbox" || cmd == "fcolorbox" || cmd == "bgcolor") {
            std::string s1, s2;
            ReadGroupRaw(s1);
            auto a = MathAtom::Make(MathAtomKind::Box);
            if (cmd == "fcolorbox") { ReadGroupRaw(s2); ParseColorSpec(s1, a->border); ParseColorSpec(s2, a->background); a->boxKind = MathBoxKind::FColorBox; }
            else { ParseColorSpec(s1, a->background); a->boxKind = MathBoxKind::ColorBox; }
            a->body = cmd == "bgcolor" ? ParseArg() : ParseTextArgOrMath();
            out.push_back(a);
            return;
        }
        if (cmd == "definecolor") { ParseDefineColor(); return; }
        if (cmd == "cellcolor") {
            std::string spec; ReadGroupRaw(spec);
            MathColor col = kMathColorNone;
            if (!ParseColorSpec(spec, col)) Report("unknown colour '" + spec + "'", start, pos);
            pendingCellColor = col;
            return;
        }
        if (cmd == "rowcolor" || cmd == "columncolor" || cmd == "arrayrulecolor") {
            std::string spec; ReadGroupRaw(spec);
            if (cmd == "rowcolor") ParseColorSpec(spec, pendingRowColor);
            return;
        }
        // Boxes
        if (cmd == "fbox" || cmd == "boxed" || cmd == "framebox" || cmd == "shadowbox" || cmd == "doublebox" ||
            cmd == "ovalbox" || cmd == "Ovalbox") {
            auto a = MathAtom::Make(MathAtomKind::Box);
            a->boxKind = cmd == "boxed" ? MathBoxKind::Boxed : cmd == "shadowbox" ? MathBoxKind::Shadow
                         : cmd == "doublebox" ? MathBoxKind::Double : (cmd == "ovalbox" || cmd == "Ovalbox") ? MathBoxKind::Oval : MathBoxKind::Frame;
            a->cornerSize = cornerSize;
            if (cmd == "framebox") { std::string o; ReadOptionalRaw(o); ReadOptionalRaw(o); }
            a->body = cmd == "boxed" ? ParseArg() : ParseTextArgOrMath();
            out.push_back(a);
            return;
        }
        if (cmd == "cornersize") { std::string v; ReadGroupRaw(v); cornerSize = std::strtof(v.c_str(), nullptr); return; }
        if (cmd == "cancel" || cmd == "bcancel" || cmd == "xcancel" || cmd == "cancelto") {
            auto a = MathAtom::Make(MathAtomKind::Cancel);
            a->cancelKind = cmd == "bcancel" ? MathCancelKind::BCancel : cmd == "xcancel" ? MathCancelKind::XCancel : MathCancelKind::Cancel;
            if (cmd == "cancelto") ParseArg();   // the "to" value is dropped
            a->body = ParseArg();
            out.push_back(a);
            return;
        }
        if (cmd == "rlap" || cmd == "llap" || cmd == "clap" || cmd == "mathrlap" || cmd == "mathllap" || cmd == "mathclap") {
            auto a = MathAtom::Make(MathAtomKind::Lap);
            a->lapKind = cmd.find("rlap") != std::string::npos ? MathLapKind::Right : cmd.find("llap") != std::string::npos ? MathLapKind::Left : MathLapKind::Center;
            a->body = cmd[0] == 'm' ? ParseArg() : ParseTextArgOrMath();
            out.push_back(a);
            return;
        }
        if (cmd == "rotatebox" || cmd == "reflectbox" || cmd == "scalebox" || cmd == "resizebox") {
            auto a = MathAtom::Make(MathAtomKind::Transform);
            if (cmd == "rotatebox") { std::string o; ReadOptionalRaw(o); std::string d; ReadGroupRaw(d); a->rotateDegrees = std::strtof(d.c_str(), nullptr); }
            else if (cmd == "reflectbox") a->reflect = true;
            else if (cmd == "scalebox") {
                std::string sx, sy; ReadGroupRaw(sx); a->scaleX = std::strtof(sx.c_str(), nullptr); a->scaleY = a->scaleX;
                if (ReadOptionalRaw(sy)) a->scaleY = std::strtof(sy.c_str(), nullptr);
            } else {
                std::string w, h; ReadGroupRaw(w); ReadGroupRaw(h);
                float we = 0, he = 0;
                if (w != "!") ParseMathLength(w, we);
                if (h != "!") ParseMathLength(h, he);
                a->resizeWidthEm = we; a->resizeHeightEm = he;
            }
            a->body = ParseTextArgOrMath();
            out.push_back(a);
            return;
        }
        if (cmd == "longdiv") {
            auto a = MathAtom::Make(MathAtomKind::LongDiv, MathAtomClass::Ord);
            ReadGroupRaw(a->text); ReadGroupRaw(a->divisor);
            out.push_back(a);
            return;
        }
        // Environments
        if (cmd == "begin") { out.push_back(ParseEnvironment(start)); return; }
        if (cmd == "end") { Report("\\end without \\begin", start, pos); std::string n; ReadGroupRaw(n); return; }
        if (cmd == "\\" || cmd == "cr" || cmd == "newline") {
            if (!stops.cell && !stops.lines) Report("\\\\ outside an alignment", start, pos);
            std::string o; ReadOptionalRaw(o);
            return;
        }
        if (cmd == "hline" || cmd == "vline") { return; }
        if (cmd == "multicolumn" || cmd == "hdotsfor" || cmd == "intertext") {
            Report("\\" + cmd + " outside a table cell", start, pos);
            std::string g; ReadGroupRaw(g); ReadGroupRaw(g); if (cmd == "multicolumn") ReadGroupRaw(g);
            return;
        }
        // Definitions
        if (cmd == "newcommand" || cmd == "renewcommand" || cmd == "providecommand" || cmd == "newcommand*" ||
            cmd == "renewcommand*" || cmd == "providecommand*" || cmd == "DeclareRobustCommand") {
            ParseNewCommand(cmd.rfind("renew", 0) == 0, cmd.rfind("provide", 0) == 0);
            return;
        }
        if (cmd == "def" || cmd == "gdef" || cmd == "edef") { ParseDef(); return; }
        if (cmd == "let") { ReadCommand(); SkipSpaces(); if (Peek() == '=') ++pos; ReadCommand(); return; }
        if (cmd == "newenvironment" || cmd == "renewenvironment" || cmd == "newenvironment*") { ParseNewEnvironment(); return; }
        if (cmd == "newcolumntype") {
            std::string n, o, spec; ReadGroupRaw(n); ReadOptionalRaw(o); ReadGroupRaw(spec);
            columnTypes[n] = spec;
            return;
        }
        // Ignored / no-op commands
        if (cmd == "fatalIfCmdConflict" || cmd == "breakEverywhere" || cmd == "label" || cmd == "tag" ||
            cmd == "tag*" || cmd == "ref" || cmd == "eqref" || cmd == "usepackage" || cmd == "documentclass" ||
            cmd == "setlength" || cmd == "setcounter" || cmd == "DeclareMathSizes" || cmd == "magnification" ||
            cmd == "arraystretch" || cmd == "renewcommand\\arraystretch") {
            std::string g; ReadOptionalRaw(g); ReadGroupRaw(g);
            if (cmd == "setlength" || cmd == "setcounter") ReadGroupRaw(g);
            return;
        }
        if (cmd == "notag" || cmd == "nonumber" || cmd == "makeatletter" || cmd == "makeatother" || cmd == "relax" ||
            cmd == "displaybreak" || cmd == "allowbreak" || cmd == "nolinebreak" || cmd == "linebreak" ||
            cmd == "noindent" || cmd == "centering" || cmd == "left." || cmd == "unskip" || cmd == "nokern") {
            return;
        }
        if (cmd == "ensuremath" || cmd == "textnormal" || cmd == "mathstyle") {
            MathList inner = ParseArg();
            out.push_back(MathAtom::MakeRow(std::move(inner)));
            return;
        }
        if (cmd == "char") {
            SkipSpaces();
            std::string num;
            if (Peek() == '"') { ++pos; while (std::isxdigit(static_cast<unsigned char>(Peek()))) num.push_back(src[pos++]); out.push_back(CharAtom(static_cast<char32_t>(std::strtoul(num.c_str(), nullptr, 16)), MathAtomClass::Ord, start)); }
            else { while (std::isdigit(static_cast<unsigned char>(Peek()))) num.push_back(src[pos++]); out.push_back(CharAtom(static_cast<char32_t>(std::atoi(num.c_str())), MathAtomClass::Ord, start)); }
            return;
        }
        // Escaped characters
        if (cmd == "{" || cmd == "}" || cmd == "$" || cmd == "&" || cmd == "#" || cmd == "%" || cmd == "_" || cmd == "|") {
            const char32_t cp = cmd == "|" ? 0x2016 : static_cast<unsigned char>(cmd[0]);
            const MathAtomClass cls = cmd == "{" ? MathAtomClass::Open : cmd == "}" ? MathAtomClass::Close : MathAtomClass::Ord;
            out.push_back(CharAtom(cp, cls, start));
            return;
        }
        // Symbols
        if (const MathSymbolInfo* sym = LookupMathSymbol(cmd)) {
            auto a = CharAtom(sym->codepoint, sym->atomClass, start);
            a->opIsLargeOperator = sym->largeOperator;
            out.push_back(a);
            return;
        }
        // User macros
        if (ExpandMacro(cmd, start)) return;
        // Unknown
        out.push_back(MakeError("\\" + cmd, start, pos, "unknown command \\" + cmd));
    }

    // \fbox{...} takes text; in math mode a group argument is still parsed as
    // text unless it obviously is math ($...$ inside handles the mix).
    MathList ParseTextArgOrMath() {
        // \fbox, \rlap, \rotatebox ... take horizontal material; inside a
        // formula that material is math (MicroTeX and most users agree), and
        // \text{} inside it switches to text where wanted.
        if (ctx.textMode) return ParseTextArg();
        return ParseArg();
    }
};

// =============================================================================
// Public
// =============================================================================

UltraCanvasMathParser::UltraCanvasMathParser() : impl_(new Impl) {}
UltraCanvasMathParser::~UltraCanvasMathParser() { delete impl_; }

void UltraCanvasMathParser::DefineMacro(const std::string& name, int argCount, const std::string& optionalDefault,
                                        bool hasOptional, const std::string& body) {
    Macro m;
    m.argCount = argCount; m.hasOptional = hasOptional; m.optionalDefault = optionalDefault; m.body = body;
    impl_->macros[name] = m;
}

void UltraCanvasMathParser::DefineColor(const std::string& name, MathColor color) { impl_->colors[name] = color; }

MathAtomPtr UltraCanvasMathParser::Parse(const std::string& source, std::vector<MathDiagnostic>& diagnostics) {
    impl_->src = source;
    impl_->pos = 0;
    impl_->diags = &diagnostics;
    impl_->expansions = 0;
    impl_->ctx = Context{};
    impl_->pendingCellColor = kMathColorNone;
    impl_->pendingRowColor = kMathColorNone;

    std::vector<MathList> lines;
    for (;;) {
        MathList line = impl_->ParseList(Stops{false, false, false, true});
        lines.push_back(std::move(line));
        impl_->SkipSpaces();
        if (impl_->AtEnd()) break;
        if (impl_->Peek() == '}') { impl_->Report("unexpected '}'", impl_->pos, impl_->pos + 1); ++impl_->pos; continue; }
        if (impl_->Peek() == '\\') {
            const std::string cmd = impl_->PeekCommand();
            if (cmd == "\\" || cmd == "cr") {
                impl_->ReadCommand();
                std::string o; impl_->ReadOptionalRaw(o);
                continue;
            }
            if (cmd == "end") {
                const size_t s = impl_->pos;
                impl_->ReadCommand();
                std::string n; impl_->ReadGroupRaw(n);
                impl_->Report("\\end{" + n + "} without \\begin", s, impl_->pos);
                continue;
            }
        }
        if (impl_->Peek() == '&') { ++impl_->pos; continue; }
        // Anything else that stopped the list: skip one character to make progress.
        ++impl_->pos;
    }
    // Merge continuation lines: a trailing empty line (source ends with \\) is dropped.
    while (lines.size() > 1 && lines.back().empty()) lines.pop_back();
    if (lines.size() == 1) return MathAtom::MakeRow(std::move(lines.front()));

    auto data = std::make_shared<MathArrayData>();
    data->kind = MathArrayKind::Lines;
    data->columns.assign(1, MathColumnSpec{});
    data->textStyleCells = false;
    for (auto& l : lines) {
        MathRow row;
        MathCell cell;
        cell.content = std::move(l);
        row.cells.push_back(std::move(cell));
        data->rows.push_back(std::move(row));
    }
    auto atom = MathAtom::Make(MathAtomKind::Array);
    atom->array = data;
    return MathAtom::MakeRow({atom});
}

} // namespace UltraCanvas
