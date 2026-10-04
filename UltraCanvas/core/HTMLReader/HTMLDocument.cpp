// core/HTMLReader/HTMLDocument.cpp
// DOM helpers and entity decoding for the HTMLReader module.
// Version: 1.1.0 - every HTML 4 named entity (&acute; &eth; &alpha; ...)
// Last Modified: 2026-09-30
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLDocument.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace UltraCanvas {
namespace HTML {

// ============================================================================
// NODE
// ============================================================================

bool Node::HasAttribute(const std::string& name) const {
    for (const auto& attr : attributes) {
        if (attr.first == name) return true;
    }
    return false;
}

std::string Node::GetAttribute(const std::string& name) const {
    for (const auto& attr : attributes) {
        if (attr.first == name) return attr.second;
    }
    return "";
}

void Node::SetAttribute(const std::string& name, const std::string& value) {
    for (auto& attr : attributes) {
        if (attr.first == name) {
            attr.second = value;
            return;
        }
    }
    attributes.emplace_back(name, value);
}

std::vector<std::string> Node::ClassList() const {
    std::vector<std::string> result;
    std::string classAttr = GetAttribute("class");
    size_t start = 0;
    while (start < classAttr.size()) {
        while (start < classAttr.size() &&
               std::isspace(static_cast<unsigned char>(classAttr[start]))) {
            ++start;
        }
        size_t end = start;
        while (end < classAttr.size() &&
               !std::isspace(static_cast<unsigned char>(classAttr[end]))) {
            ++end;
        }
        if (end > start) result.push_back(classAttr.substr(start, end - start));
        start = end;
    }
    return result;
}

bool Node::HasClass(const std::string& className) const {
    for (const auto& c : ClassList()) {
        if (c == className) return true;
    }
    return false;
}

std::string Node::TextContent() const {
    if (type == NodeType::Text) return text;
    if (type == NodeType::Comment) return "";
    std::string result;
    for (const auto& child : children) {
        result += child->TextContent();
    }
    return result;
}

Node* Node::FindFirst(const std::string& tagName) {
    for (const auto& child : children) {
        if (child->IsElement(tagName)) return child.get();
        if (Node* found = child->FindFirst(tagName)) return found;
    }
    return nullptr;
}

void Node::ForEachElement(const std::function<bool(Node&)>& fn) {
    if (IsElement()) {
        if (!fn(*this)) return;
    }
    for (const auto& child : children) {
        child->ForEachElement(fn);
    }
}

// ============================================================================
// DOCUMENT
// ============================================================================

Node* Document::Head() const {
    if (!root) return nullptr;
    for (const auto& child : root->children) {
        if (child->IsElement("head")) return child.get();
    }
    return nullptr;
}

Node* Document::Body() const {
    if (!root) return nullptr;
    for (const auto& child : root->children) {
        if (child->IsElement("body")) return child.get();
    }
    return nullptr;
}

Node* Document::GetElementById(const std::string& id) const {
    if (!root || id.empty()) return nullptr;
    Node* result = nullptr;
    root->ForEachElement([&](Node& element) {
        if (element.GetId() == id) {
            result = &element;
            return false;
        }
        return true;
    });
    return result;
}

int Document::CountElements() const {
    if (!root) return 0;
    int count = 0;
    root->ForEachElement([&](Node&) { ++count; return true; });
    return count;
}

// ============================================================================
// ENTITY DECODING
// ============================================================================

namespace {

void AppendUtf8(std::string& out, unsigned code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x110000) {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

const std::unordered_map<std::string, unsigned>& NamedEntities() {
    // The HTML 4 set (252 names: Latin-1, symbols, Greek, punctuation) plus
    // &apos;. Mail and older pages spell accents and marks this way - an
    // unknown name is shown literally, so a gap here shows up as "&acute;".
    static const std::unordered_map<std::string, unsigned> table = {
        {"quot", 0x0022}, {"amp", 0x0026}, {"apos", 0x0027}, {"lt", 0x003C},
        {"gt", 0x003E}, {"nbsp", 0x00A0}, {"iexcl", 0x00A1}, {"cent", 0x00A2},
        {"pound", 0x00A3}, {"curren", 0x00A4}, {"yen", 0x00A5}, {"brvbar", 0x00A6},
        {"sect", 0x00A7}, {"uml", 0x00A8}, {"copy", 0x00A9}, {"ordf", 0x00AA},
        {"laquo", 0x00AB}, {"not", 0x00AC}, {"shy", 0x00AD}, {"reg", 0x00AE},
        {"macr", 0x00AF}, {"deg", 0x00B0}, {"plusmn", 0x00B1}, {"sup2", 0x00B2},
        {"sup3", 0x00B3}, {"acute", 0x00B4}, {"micro", 0x00B5}, {"para", 0x00B6},
        {"middot", 0x00B7}, {"cedil", 0x00B8}, {"sup1", 0x00B9}, {"ordm", 0x00BA},
        {"raquo", 0x00BB}, {"frac14", 0x00BC}, {"frac12", 0x00BD}, {"frac34", 0x00BE},
        {"iquest", 0x00BF}, {"Agrave", 0x00C0}, {"Aacute", 0x00C1}, {"Acirc", 0x00C2},
        {"Atilde", 0x00C3}, {"Auml", 0x00C4}, {"Aring", 0x00C5}, {"AElig", 0x00C6},
        {"Ccedil", 0x00C7}, {"Egrave", 0x00C8}, {"Eacute", 0x00C9}, {"Ecirc", 0x00CA},
        {"Euml", 0x00CB}, {"Igrave", 0x00CC}, {"Iacute", 0x00CD}, {"Icirc", 0x00CE},
        {"Iuml", 0x00CF}, {"ETH", 0x00D0}, {"Ntilde", 0x00D1}, {"Ograve", 0x00D2},
        {"Oacute", 0x00D3}, {"Ocirc", 0x00D4}, {"Otilde", 0x00D5}, {"Ouml", 0x00D6},
        {"times", 0x00D7}, {"Oslash", 0x00D8}, {"Ugrave", 0x00D9}, {"Uacute", 0x00DA},
        {"Ucirc", 0x00DB}, {"Uuml", 0x00DC}, {"Yacute", 0x00DD}, {"THORN", 0x00DE},
        {"szlig", 0x00DF}, {"agrave", 0x00E0}, {"aacute", 0x00E1}, {"acirc", 0x00E2},
        {"atilde", 0x00E3}, {"auml", 0x00E4}, {"aring", 0x00E5}, {"aelig", 0x00E6},
        {"ccedil", 0x00E7}, {"egrave", 0x00E8}, {"eacute", 0x00E9}, {"ecirc", 0x00EA},
        {"euml", 0x00EB}, {"igrave", 0x00EC}, {"iacute", 0x00ED}, {"icirc", 0x00EE},
        {"iuml", 0x00EF}, {"eth", 0x00F0}, {"ntilde", 0x00F1}, {"ograve", 0x00F2},
        {"oacute", 0x00F3}, {"ocirc", 0x00F4}, {"otilde", 0x00F5}, {"ouml", 0x00F6},
        {"divide", 0x00F7}, {"oslash", 0x00F8}, {"ugrave", 0x00F9}, {"uacute", 0x00FA},
        {"ucirc", 0x00FB}, {"uuml", 0x00FC}, {"yacute", 0x00FD}, {"thorn", 0x00FE},
        {"yuml", 0x00FF}, {"OElig", 0x0152}, {"oelig", 0x0153}, {"Scaron", 0x0160},
        {"scaron", 0x0161}, {"Yuml", 0x0178}, {"fnof", 0x0192}, {"circ", 0x02C6},
        {"tilde", 0x02DC}, {"Alpha", 0x0391}, {"Beta", 0x0392}, {"Gamma", 0x0393},
        {"Delta", 0x0394}, {"Epsilon", 0x0395}, {"Zeta", 0x0396}, {"Eta", 0x0397},
        {"Theta", 0x0398}, {"Iota", 0x0399}, {"Kappa", 0x039A}, {"Lambda", 0x039B},
        {"Mu", 0x039C}, {"Nu", 0x039D}, {"Xi", 0x039E}, {"Omicron", 0x039F},
        {"Pi", 0x03A0}, {"Rho", 0x03A1}, {"Sigma", 0x03A3}, {"Tau", 0x03A4},
        {"Upsilon", 0x03A5}, {"Phi", 0x03A6}, {"Chi", 0x03A7}, {"Psi", 0x03A8},
        {"Omega", 0x03A9}, {"alpha", 0x03B1}, {"beta", 0x03B2}, {"gamma", 0x03B3},
        {"delta", 0x03B4}, {"epsilon", 0x03B5}, {"zeta", 0x03B6}, {"eta", 0x03B7},
        {"theta", 0x03B8}, {"iota", 0x03B9}, {"kappa", 0x03BA}, {"lambda", 0x03BB},
        {"mu", 0x03BC}, {"nu", 0x03BD}, {"xi", 0x03BE}, {"omicron", 0x03BF},
        {"pi", 0x03C0}, {"rho", 0x03C1}, {"sigmaf", 0x03C2}, {"sigma", 0x03C3},
        {"tau", 0x03C4}, {"upsilon", 0x03C5}, {"phi", 0x03C6}, {"chi", 0x03C7},
        {"psi", 0x03C8}, {"omega", 0x03C9}, {"thetasym", 0x03D1}, {"upsih", 0x03D2},
        {"piv", 0x03D6}, {"ensp", 0x2002}, {"emsp", 0x2003}, {"thinsp", 0x2009},
        {"zwnj", 0x200C}, {"zwj", 0x200D}, {"lrm", 0x200E}, {"rlm", 0x200F},
        {"ndash", 0x2013}, {"mdash", 0x2014}, {"lsquo", 0x2018}, {"rsquo", 0x2019},
        {"sbquo", 0x201A}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bdquo", 0x201E},
        {"dagger", 0x2020}, {"Dagger", 0x2021}, {"bull", 0x2022}, {"hellip", 0x2026},
        {"permil", 0x2030}, {"prime", 0x2032}, {"Prime", 0x2033}, {"lsaquo", 0x2039},
        {"rsaquo", 0x203A}, {"oline", 0x203E}, {"frasl", 0x2044}, {"euro", 0x20AC},
        {"image", 0x2111}, {"weierp", 0x2118}, {"real", 0x211C}, {"trade", 0x2122},
        {"alefsym", 0x2135}, {"larr", 0x2190}, {"uarr", 0x2191}, {"rarr", 0x2192},
        {"darr", 0x2193}, {"harr", 0x2194}, {"crarr", 0x21B5}, {"lArr", 0x21D0},
        {"uArr", 0x21D1}, {"rArr", 0x21D2}, {"dArr", 0x21D3}, {"hArr", 0x21D4},
        {"forall", 0x2200}, {"part", 0x2202}, {"exist", 0x2203}, {"empty", 0x2205},
        {"nabla", 0x2207}, {"isin", 0x2208}, {"notin", 0x2209}, {"ni", 0x220B},
        {"prod", 0x220F}, {"sum", 0x2211}, {"minus", 0x2212}, {"lowast", 0x2217},
        {"radic", 0x221A}, {"prop", 0x221D}, {"infin", 0x221E}, {"ang", 0x2220},
        {"and", 0x2227}, {"or", 0x2228}, {"cap", 0x2229}, {"cup", 0x222A}, {"int", 0x222B},
        {"there4", 0x2234}, {"sim", 0x223C}, {"cong", 0x2245}, {"asymp", 0x2248},
        {"ne", 0x2260}, {"equiv", 0x2261}, {"le", 0x2264}, {"ge", 0x2265}, {"sub", 0x2282},
        {"sup", 0x2283}, {"nsub", 0x2284}, {"sube", 0x2286}, {"supe", 0x2287},
        {"oplus", 0x2295}, {"otimes", 0x2297}, {"perp", 0x22A5}, {"sdot", 0x22C5},
        {"lceil", 0x2308}, {"rceil", 0x2309}, {"lfloor", 0x230A}, {"rfloor", 0x230B},
        {"lang", 0x2329}, {"rang", 0x232A}, {"loz", 0x25CA}, {"spades", 0x2660},
        {"clubs", 0x2663}, {"hearts", 0x2665}, {"diams", 0x2666}
    };
    return table;
}

} // namespace

std::string DecodeEntities(const std::string& text) {
    std::string out;
    out.reserve(text.size());

    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (c != '&') {
            out += c;
            ++i;
            continue;
        }

        // Find the terminating ';' within a sane distance.
        size_t semi = text.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 10) {
            out += c;
            ++i;
            continue;
        }

        std::string entity = text.substr(i + 1, semi - i - 1);
        if (!entity.empty() && entity[0] == '#') {
            unsigned code = 0;
            bool ok = false;
            try {
                if (entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')) {
                    code = static_cast<unsigned>(std::stoul(entity.substr(2), nullptr, 16));
                } else {
                    code = static_cast<unsigned>(std::stoul(entity.substr(1)));
                }
                ok = true;
            } catch (...) {
                ok = false;
            }
            if (ok && code > 0 && code < 0x110000) {
                AppendUtf8(out, code);
                i = semi + 1;
                continue;
            }
        } else {
            auto it = NamedEntities().find(entity);
            if (it != NamedEntities().end()) {
                AppendUtf8(out, it->second);
                i = semi + 1;
                continue;
            }
        }

        // Unknown entity: keep it verbatim.
        out += c;
        ++i;
    }

    return out;
}

std::string ExtractPlainText(const std::string& html) {
    std::string stripped;
    stripped.reserve(html.size());

    bool inTag = false;
    size_t i = 0;
    while (i < html.size()) {
        char c = html[i];
        if (inTag) {
            if (c == '>') inTag = false;
            ++i;
            continue;
        }
        if (c == '<') {
            // Skip <script>/<style> bodies entirely.
            auto matchesWord = [&](const char* word) {
                size_t len = std::char_traits<char>::length(word);
                if (i + 1 + len > html.size()) return false;
                for (size_t k = 0; k < len; ++k) {
                    if (std::tolower(static_cast<unsigned char>(html[i + 1 + k])) != word[k]) {
                        return false;
                    }
                }
                return true;
            };
            const char* rawText = matchesWord("script") ? "</script"
                                : matchesWord("style") ? "</style" : nullptr;
            if (rawText) {
                size_t end = i + 1;
                while (end < html.size()) {
                    if (html[end] == '<') {
                        size_t k = 0;
                        bool match = true;
                        while (rawText[k]) {
                            if (end + k >= html.size() ||
                                std::tolower(static_cast<unsigned char>(html[end + k])) != rawText[k]) {
                                match = false;
                                break;
                            }
                            ++k;
                        }
                        if (match) break;
                    }
                    ++end;
                }
                i = end;
                inTag = true;   // consume the closing tag
                continue;
            }
            // Block-level separation keeps words from running together.
            stripped += ' ';
            inTag = true;
            ++i;
            continue;
        }
        stripped += c;
        ++i;
    }

    std::string decoded = DecodeEntities(stripped);

    // Collapse whitespace runs.
    std::string result;
    result.reserve(decoded.size());
    bool lastWasSpace = true;
    for (char c : decoded) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!lastWasSpace) {
                result += ' ';
                lastWasSpace = true;
            }
        } else {
            result += c;
            lastWasSpace = false;
        }
    }
    while (!result.empty() && result.back() == ' ') result.pop_back();

    return result;
}

} // namespace HTML
} // namespace UltraCanvas
