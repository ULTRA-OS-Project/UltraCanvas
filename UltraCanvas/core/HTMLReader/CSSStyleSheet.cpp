// core/HTMLReader/CSSStyleSheet.cpp
// CSS-subset parser: values, selectors, rules.
// Version: 1.4.0 - attribute selectors: [a], [a=v], ~= ^= $= *= |=, quoted
//                  values, the i flag (Mailchimp: td[class=mcnTextContent])
// Version: 1.3.0 - @media blocks apply when their query matches the media width;
//                  <!-- --> around a style sheet is skipped.
// Version: 1.2.0 - a:link / :any-link selectors match links (mail styles its
//                  links that way); other pseudo-classes still drop the rule.
// Version: 1.1.2 - ParseFloatClassic moved to UltraCanvasTextUtils, so the SVG
//                  reader and the other format parsers share one copy of it
// Last Modified: 2026-10-02
// Author: UltraCanvas Framework

#include "HTMLReader/CSSStyleSheet.h"
#include "UltraCanvasTextUtils.h"   // ParseFloatClassic - see there for why not std::stof/from_chars
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace UltraCanvas {
namespace HTML {

// ============================================================================
// STRING HELPERS
// ============================================================================

std::string TrimLower(const std::string& text) {
    std::string result = Trim(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

namespace {

std::string StripComments(const std::string& css) {
    std::string out;
    out.reserve(css.size());
    size_t i = 0;
    while (i < css.size()) {
        if (i + 1 < css.size() && css[i] == '/' && css[i + 1] == '*') {
            size_t end = css.find("*/", i + 2);
            i = (end == std::string::npos) ? css.size() : end + 2;
            out += ' ';
            continue;
        }
        out += css[i];
        ++i;
    }
    return out;
}

// Advance past a balanced {...} block; `i` points at the '{'.
size_t SkipBlock(const std::string& text, size_t i) {
    int depth = 0;
    while (i < text.size()) {
        if (text[i] == '{') ++depth;
        else if (text[i] == '}') {
            --depth;
            if (depth == 0) return i + 1;
        }
        ++i;
    }
    return i;
}

} // namespace

// ============================================================================
// COLOR
// ============================================================================

namespace {

const std::unordered_map<std::string, uint32_t>& NamedColors() {
    // 0xRRGGBB
    static const std::unordered_map<std::string, uint32_t> table = {
        {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000},
        {"green", 0x008000}, {"blue", 0x0000FF}, {"yellow", 0xFFFF00},
        {"cyan", 0x00FFFF}, {"aqua", 0x00FFFF}, {"magenta", 0xFF00FF},
        {"fuchsia", 0xFF00FF}, {"gray", 0x808080}, {"grey", 0x808080},
        {"silver", 0xC0C0C0}, {"maroon", 0x800000}, {"olive", 0x808000},
        {"lime", 0x00FF00}, {"teal", 0x008080}, {"navy", 0x000080},
        {"purple", 0x800080}, {"orange", 0xFFA500}, {"brown", 0xA52A2A},
        {"pink", 0xFFC0CB}, {"gold", 0xFFD700}, {"beige", 0xF5F5DC},
        {"ivory", 0xFFFFF0}, {"khaki", 0xF0E68C}, {"lavender", 0xE6E6FA},
        {"salmon", 0xFA8072}, {"tan", 0xD2B48C}, {"violet", 0xEE82EE},
        {"indigo", 0x4B0082}, {"crimson", 0xDC143C}, {"coral", 0xFF7F50},
        {"chocolate", 0xD2691E}, {"darkred", 0x8B0000}, {"darkblue", 0x00008B},
        {"darkgreen", 0x006400}, {"darkgray", 0xA9A9A9}, {"darkgrey", 0xA9A9A9},
        {"lightgray", 0xD3D3D3}, {"lightgrey", 0xD3D3D3},
        {"lightblue", 0xADD8E6}, {"lightgreen", 0x90EE90},
        {"lightyellow", 0xFFFFE0}, {"whitesmoke", 0xF5F5F5},
        {"dimgray", 0x696969}, {"slategray", 0x708090},
        {"midnightblue", 0x191970}, {"royalblue", 0x4169E1},
        {"steelblue", 0x4682B4}, {"skyblue", 0x87CEEB},
        {"seagreen", 0x2E8B57}, {"forestgreen", 0x228B22},
        {"goldenrod", 0xDAA520}, {"firebrick", 0xB22222},
        {"tomato", 0xFF6347}, {"orangered", 0xFF4500},
        {"saddlebrown", 0x8B4513}, {"sienna", 0xA0522D},
        {"wheat", 0xF5DEB3}, {"linen", 0xFAF0E6}, {"snow", 0xFFFAFA},
        {"honeydew", 0xF0FFF0}, {"azure", 0xF0FFFF}, {"gainsboro", 0xDCDCDC},
    };
    return table;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

std::optional<CssColor> CssColor::Parse(const std::string& text) {
    std::string value = TrimLower(text);
    if (value.empty()) return std::nullopt;

    if (value == "transparent") {
        return CssColor{0, 0, 0, 0};
    }

    if (value[0] == '#') {
        std::string hex = value.substr(1);
        auto valid = [&]() {
            for (char c : hex) {
                if (HexDigit(c) < 0) return false;
            }
            return true;
        };
        if (!valid()) return std::nullopt;

        CssColor color;
        if (hex.size() == 3) {
            color.r = static_cast<uint8_t>(HexDigit(hex[0]) * 17);
            color.g = static_cast<uint8_t>(HexDigit(hex[1]) * 17);
            color.b = static_cast<uint8_t>(HexDigit(hex[2]) * 17);
            return color;
        }
        if (hex.size() == 6 || hex.size() == 8) {
            color.r = static_cast<uint8_t>(HexDigit(hex[0]) * 16 + HexDigit(hex[1]));
            color.g = static_cast<uint8_t>(HexDigit(hex[2]) * 16 + HexDigit(hex[3]));
            color.b = static_cast<uint8_t>(HexDigit(hex[4]) * 16 + HexDigit(hex[5]));
            if (hex.size() == 8) {
                color.a = static_cast<uint8_t>(HexDigit(hex[6]) * 16 + HexDigit(hex[7]));
            }
            return color;
        }
        return std::nullopt;
    }

    if (value.rfind("rgb", 0) == 0) {
        size_t open = value.find('(');
        size_t close = value.find(')');
        if (open == std::string::npos || close == std::string::npos || close < open) {
            return std::nullopt;
        }
        std::string args = value.substr(open + 1, close - open - 1);

        float components[4] = {0, 0, 0, 255};
        int count = 0;
        size_t start = 0;
        while (start <= args.size() && count < 4) {
            size_t comma = args.find(',', start);
            std::string part = Trim(comma == std::string::npos
                                        ? args.substr(start)
                                        : args.substr(start, comma - start));
            if (!part.empty()) {
                bool percent = part.back() == '%';
                if (percent) part.pop_back();
                float v = 0.f;   // locale-independent (unlike strtof)
                ParseFloatClassic(part.data(), part.data() + part.size(), v);
                if (percent) v = v * 255.f / 100.f;
                if (count == 3) v = v * 255.f;   // alpha given as 0..1
                components[count] = v;
                ++count;
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        if (count < 3) return std::nullopt;

        auto clamp255 = [](float v) {
            return static_cast<uint8_t>(std::max(0.f, std::min(255.f, v)));
        };
        return CssColor{clamp255(components[0]), clamp255(components[1]),
                        clamp255(components[2]), clamp255(components[3])};
    }

    auto it = NamedColors().find(value);
    if (it != NamedColors().end()) {
        uint32_t rgb = it->second;
        return CssColor{static_cast<uint8_t>((rgb >> 16) & 0xFF),
                        static_cast<uint8_t>((rgb >> 8) & 0xFF),
                        static_cast<uint8_t>(rgb & 0xFF), 255};
    }

    return std::nullopt;
}

// ============================================================================
// LENGTH
// ============================================================================

std::optional<CssLength> CssLength::Parse(const std::string& text) {
    std::string value = TrimLower(text);
    if (value.empty()) return std::nullopt;

    if (value == "auto") {
        return CssLength{0.f, CssUnit::Auto};
    }
    if (value == "0") {
        return CssLength{0.f, CssUnit::Px};
    }

    const char* begin = value.c_str();
    const char* bufEnd = begin + value.size();
    float number = 0.f;   // locale-independent (unlike strtof)
    const char* numberEnd = ParseFloatClassic(begin, bufEnd, number);
    if (numberEnd == begin) return std::nullopt;

    std::string unit = Trim(std::string(numberEnd));
    if (unit.empty()) return CssLength{number, CssUnit::Number};
    if (unit == "px") return CssLength{number, CssUnit::Px};
    if (unit == "em") return CssLength{number, CssUnit::Em};
    if (unit == "rem") return CssLength{number, CssUnit::Rem};
    if (unit == "%") return CssLength{number, CssUnit::Percent};
    if (unit == "pt") return CssLength{number, CssUnit::Pt};
    // Rarely used units mapped approximately at 96 dpi.
    if (unit == "in") return CssLength{number * 96.f, CssUnit::Px};
    if (unit == "cm") return CssLength{number * 37.795f, CssUnit::Px};
    if (unit == "mm") return CssLength{number * 3.7795f, CssUnit::Px};
    if (unit == "ex") return CssLength{number * 0.5f, CssUnit::Em};
    if (unit == "ch") return CssLength{number * 0.5f, CssUnit::Em};

    return std::nullopt;
}

float CssLength::ToPx(float emPx, float remPx, float percentBase) const {
    switch (unit) {
        case CssUnit::Px:      return value;
        case CssUnit::Em:      return value * emPx;
        case CssUnit::Rem:     return value * remPx;
        case CssUnit::Percent: return value * percentBase / 100.f;
        case CssUnit::Pt:      return value * 96.f / 72.f;
        case CssUnit::Number:  return value;
        case CssUnit::Auto:    return 0.f;
    }
    return 0.f;
}

// ============================================================================
// SELECTORS
// ============================================================================

int Selector::Specificity() const {
    int ids = 0, classes = 0, tags = 0;
    for (const auto& part : path) {
        if (!part.id.empty()) ++ids;
        classes += static_cast<int>(part.classes.size() + part.attributes.size()) +
                   (part.link ? 1 : 0);
        if (!part.tag.empty() && part.tag != "*") ++tags;
    }
    return ids * 10000 + classes * 100 + tags;
}

namespace {

// Returns nullopt when the selector uses unsupported syntax.
std::optional<SimpleSelector> ParseCompound(const std::string& text) {
    SimpleSelector result;
    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (c == ':' && i + 1 < text.size() && text[i + 1] != ':') {
            // :link / :any-link match every link; any other pseudo-class
            // (:hover, :visited, :nth-child(...)) drops the selector.
            std::string name;
            ++i;
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) ||
                                       text[i] == '-')) {
                name += static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
                ++i;
            }
            if (name == "link" || name == "any-link") {
                result.link = true;
                continue;
            }
            return std::nullopt;
        }
        if (c == '[') {
            // [name], [name=value], [name~=value] ... with an optional
            // quoted value and an " i" flag; anything else drops the rule.
            size_t close = i + 1;
            char quote = 0;
            for (; close < text.size(); ++close) {
                if (quote) { if (text[close] == quote) quote = 0; continue; }
                if (text[close] == '"' || text[close] == '\'') quote = text[close];
                else if (text[close] == ']') break;
            }
            if (close >= text.size()) return std::nullopt;
            std::string inner = Trim(text.substr(i + 1, close - i - 1));
            i = close + 1;
            AttributeSelector attr;
            size_t n = 0;
            while (n < inner.size() && (std::isalnum(static_cast<unsigned char>(inner[n])) ||
                                        inner[n] == '-' || inner[n] == '_' || inner[n] == ':')) {
                attr.name += static_cast<char>(std::tolower(static_cast<unsigned char>(inner[n])));
                ++n;
            }
            if (attr.name.empty()) return std::nullopt;
            std::string rest = Trim(inner.substr(n));
            if (!rest.empty()) {
                size_t eq = 0;
                if (rest[0] == '=') { attr.op = '='; eq = 1; }
                else if (rest.size() > 1 && rest[1] == '=' &&
                         std::string("~^$*|").find(rest[0]) != std::string::npos) {
                    attr.op = rest[0]; eq = 2;
                } else {
                    return std::nullopt;
                }
                std::string value = Trim(rest.substr(eq));
                if (!value.empty() && (value[0] == '"' || value[0] == '\'')) {
                    const size_t end = value.find(value[0], 1);
                    if (end == std::string::npos) return std::nullopt;
                    attr.value = value.substr(1, end - 1);
                    value = Trim(value.substr(end + 1));
                } else {
                    const size_t space = value.find_first_of(" \t");
                    attr.value = value.substr(0, space);
                    value = space == std::string::npos ? std::string() : Trim(value.substr(space));
                }
                if (value == "i" || value == "I") attr.ignoreCase = true;
                else if (!value.empty() && value != "s" && value != "S") return std::nullopt;
            }
            result.attributes.push_back(std::move(attr));
            continue;
        }
        if (c == ':' || c == '(') {
            return std::nullopt;   // pseudo-element / functional
        }
        if (c == '*') {
            ++i;
            continue;
        }
        if (c == '.' || c == '#') {
            char kind = c;
            ++i;
            std::string name;
            while (i < text.size() && text[i] != '.' && text[i] != '#' &&
                   text[i] != ':' && text[i] != '[') {
                name += text[i];
                ++i;
            }
            if (name.empty()) return std::nullopt;
            if (kind == '.') result.classes.push_back(name);
            else result.id = name;
            continue;
        }
        // Tag name (strip namespace prefix).
        std::string tag;
        while (i < text.size() && text[i] != '.' && text[i] != '#' &&
               text[i] != ':' && text[i] != '[') {
            tag += static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
            ++i;
        }
        size_t bar = tag.find('|');
        if (bar != std::string::npos) tag = tag.substr(bar + 1);
        result.tag = tag;
    }
    return result;
}

std::optional<Selector> ParseSelector(const std::string& text) {
    Selector selector;
    std::string token;
    size_t i = 0;

    auto flush = [&]() -> bool {
        if (token.empty()) return true;
        auto compound = ParseCompound(token);
        token.clear();
        if (!compound) return false;
        selector.path.push_back(*compound);
        return true;
    };

    while (i <= text.size()) {
        char c = (i < text.size()) ? text[i] : ' ';
        // An attribute selector is one piece: its spaces, '~' (~=) and
        // quoted text are not combinators.
        if (c == '[') {
            char quote = 0;
            size_t j = i;
            for (; j < text.size(); ++j) {
                if (quote) { if (text[j] == quote) quote = 0; continue; }
                if (text[j] == '"' || text[j] == '\'') quote = text[j];
                else if (text[j] == ']') break;
            }
            if (j >= text.size()) return std::nullopt;
            token += text.substr(i, j - i + 1);
            i = j + 1;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!flush()) return std::nullopt;
            ++i;
            continue;
        }
        if (c == '>') {
            // Child combinator: treated as descendant (documented approximation).
            if (!flush()) return std::nullopt;
            ++i;
            continue;
        }
        if (c == '+' || c == '~') {
            return std::nullopt;   // sibling combinators unsupported
        }
        token += c;
        ++i;
    }
    if (!flush()) return std::nullopt;
    if (selector.path.empty()) return std::nullopt;
    return selector;
}

} // namespace

// ============================================================================
// DECLARATIONS
// ============================================================================

std::vector<Declaration> StyleSheet::ParseDeclarationList(const std::string& text) {
    std::vector<Declaration> result;

    size_t start = 0;
    while (start < text.size()) {
        size_t semi = text.find(';', start);
        std::string chunk = (semi == std::string::npos)
                                ? text.substr(start)
                                : text.substr(start, semi - start);

        size_t colon = chunk.find(':');
        if (colon != std::string::npos) {
            Declaration decl;
            decl.property = TrimLower(chunk.substr(0, colon));
            std::string value = Trim(chunk.substr(colon + 1));

            // !important
            size_t bang = value.rfind('!');
            if (bang != std::string::npos &&
                TrimLower(value.substr(bang + 1)) == "important") {
                decl.important = true;
                value = Trim(value.substr(0, bang));
            }
            decl.value = value;

            if (!decl.property.empty() && !decl.value.empty()) {
                result.push_back(std::move(decl));
            }
        }

        if (semi == std::string::npos) break;
        start = semi + 1;
    }

    return result;
}

// ============================================================================
// STYLESHEET
// ============================================================================

bool StyleSheet::MediaMatches(const std::string& query, float widthPx) {
    // A comma-separated list matches when any of its queries does.
    std::string list = TrimLower(query);
    if (list.empty()) return true;
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(',', start);
        std::string one = Trim(list.substr(start, comma == std::string::npos
                                                      ? std::string::npos : comma - start));
        start = (comma == std::string::npos) ? list.size() + 1 : comma + 1;
        if (one.empty()) continue;

        bool negate = false, ok = true;
        size_t i = 0;
        while (i < one.size() && ok) {
            while (i < one.size() && std::isspace(static_cast<unsigned char>(one[i]))) ++i;
            if (i >= one.size()) break;
            if (one[i] == '(') {
                size_t close = one.find(')', i);
                if (close == std::string::npos) { ok = false; break; }
                std::string feature = one.substr(i + 1, close - i - 1);
                i = close + 1;
                size_t colon = feature.find(':');
                std::string name = Trim(feature.substr(0, colon));
                std::string value = colon == std::string::npos ? "" : Trim(feature.substr(colon + 1));
                if (name == "min-width" || name == "max-width" || name == "width") {
                    auto len = CssLength::Parse(value);
                    if (!len || len->unit == CssUnit::Percent) { ok = false; break; }
                    const float px = len->ToPx(16.f, 16.f);   // em in a query: the initial 16px
                    if (name == "min-width") ok = widthPx >= px;
                    else if (name == "max-width") ok = widthPx <= px;
                    else ok = std::fabs(widthPx - px) < 0.5f;
                } else if (name == "orientation") {
                    ok = value == "landscape";
                } else if (name == "prefers-color-scheme") {
                    ok = value == "light";
                } else if (name == "color" || name == "hover" || name == "pointer") {
                    ok = true;
                } else {
                    ok = false;   // unknown feature: no match, as in a browser
                }
                continue;
            }
            size_t e = i;
            while (e < one.size() && !std::isspace(static_cast<unsigned char>(one[e])) && one[e] != '(') ++e;
            std::string word = one.substr(i, e - i);
            i = e;
            if (word == "not") negate = true;
            else if (word == "only" || word == "and" || word == "screen" || word == "all") continue;
            else ok = false;   // print, speech, tv, ...
        }
        if (ok != negate) return true;
    }
    return false;
}

void StyleSheet::ParseAppend(const std::string& css) {
    std::string text = StripComments(css);

    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i >= text.size()) break;

        // The HTML comment markers old mail wraps its style sheet in.
        if (text.compare(i, 4, "<!--") == 0) { i += 4; continue; }
        if (text.compare(i, 3, "-->") == 0)  { i += 3; continue; }

        if (text[i] == '@') {
            // @import/@charset end at ';'; block at-rules skip the block,
            // except a matching @media, whose rules apply.
            size_t semi = text.find(';', i);
            size_t brace = text.find('{', i);
            if (brace != std::string::npos &&
                (semi == std::string::npos || brace < semi)) {
                const size_t end = SkipBlock(text, brace);
                std::string keyword;
                for (size_t k = i + 1; k < brace && (std::isalpha(static_cast<unsigned char>(text[k])) ||
                                                     text[k] == '-'); ++k)
                    keyword += static_cast<char>(std::tolower(static_cast<unsigned char>(text[k])));
                if (keyword == "media" &&
                    MediaMatches(text.substr(i + 6, brace - i - 6), mediaWidth)) {
                    const size_t innerEnd = (end > brace + 1 && text[end - 1] == '}') ? end - 1 : end;
                    ParseAppend(text.substr(brace + 1, innerEnd - brace - 1));
                }
                i = end;
            } else if (semi != std::string::npos) {
                i = semi + 1;
            } else {
                break;
            }
            continue;
        }

        if (text[i] == '}') {   // stray close brace
            ++i;
            continue;
        }

        size_t brace = text.find('{', i);
        if (brace == std::string::npos) break;
        size_t close = text.find('}', brace);
        if (close == std::string::npos) close = text.size();

        std::string selectorText = text.substr(i, brace - i);
        std::string body = text.substr(brace + 1, close - brace - 1);

        Rule rule;
        rule.sourceOrder = nextOrder++;
        rule.declarations = ParseDeclarationList(body);

        size_t start = 0;
        while (start <= selectorText.size()) {
            size_t comma = selectorText.find(',', start);
            std::string one = Trim(comma == std::string::npos
                                       ? selectorText.substr(start)
                                       : selectorText.substr(start, comma - start));
            if (!one.empty()) {
                if (auto selector = ParseSelector(one)) {
                    rule.selectors.push_back(*selector);
                }
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }

        if (!rule.selectors.empty() && !rule.declarations.empty()) {
            rules.push_back(std::move(rule));
        }

        i = (close < text.size()) ? close + 1 : text.size();
    }
}

} // namespace HTML
} // namespace UltraCanvas
