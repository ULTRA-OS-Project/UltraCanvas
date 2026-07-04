// core/HTMLReader/HTMLDocument.cpp
// DOM helpers and entity decoding for the HTMLReader module.
// Version: 1.0.0
// Last Modified: 2026-07-02
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
    static const std::unordered_map<std::string, unsigned> table = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''},
        {"nbsp", 0x00A0}, {"shy", 0x00AD},
        {"copy", 0x00A9}, {"reg", 0x00AE}, {"trade", 0x2122},
        {"sect", 0x00A7}, {"para", 0x00B6}, {"middot", 0x00B7},
        {"laquo", 0x00AB}, {"raquo", 0x00BB},
        {"deg", 0x00B0}, {"plusmn", 0x00B1}, {"times", 0x00D7}, {"divide", 0x00F7},
        {"frac12", 0x00BD}, {"frac14", 0x00BC}, {"frac34", 0x00BE},
        {"ndash", 0x2013}, {"mdash", 0x2014},
        {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"sbquo", 0x201A},
        {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bdquo", 0x201E},
        {"dagger", 0x2020}, {"Dagger", 0x2021},
        {"bull", 0x2022}, {"hellip", 0x2026}, {"prime", 0x2032}, {"Prime", 0x2033},
        {"euro", 0x20AC}, {"pound", 0x00A3}, {"cent", 0x00A2}, {"yen", 0x00A5},
        {"agrave", 0x00E0}, {"aacute", 0x00E1}, {"acirc", 0x00E2}, {"auml", 0x00E4},
        {"egrave", 0x00E8}, {"eacute", 0x00E9}, {"ecirc", 0x00EA}, {"euml", 0x00EB},
        {"igrave", 0x00EC}, {"iacute", 0x00ED}, {"icirc", 0x00EE}, {"iuml", 0x00EF},
        {"ograve", 0x00F2}, {"oacute", 0x00F3}, {"ocirc", 0x00F4}, {"ouml", 0x00F6},
        {"ugrave", 0x00F9}, {"uacute", 0x00FA}, {"ucirc", 0x00FB}, {"uuml", 0x00FC},
        {"ntilde", 0x00F1}, {"ccedil", 0x00E7}, {"szlig", 0x00DF},
        {"Agrave", 0x00C0}, {"Aacute", 0x00C1}, {"Auml", 0x00C4},
        {"Eacute", 0x00C9}, {"Ouml", 0x00D6}, {"Uuml", 0x00DC},
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
