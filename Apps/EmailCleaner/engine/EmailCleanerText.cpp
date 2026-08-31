// Apps/EmailCleaner/engine/EmailCleanerText.cpp
// HTML stripping, obfuscation folding and the shared normalisation pipeline.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerText.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace EmailCleaner {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The handful of HTML entities that actually change a keyword match.
const std::map<std::string, std::string>& Entities() {
    static const std::map<std::string, std::string> table = {
        { "nbsp", " " }, { "amp", "&" }, { "lt", "<" }, { "gt", ">" },
        { "quot", "\"" }, { "apos", "'" }, { "#39", "'" }, { "#160", " " },
        { "hellip", "..." }, { "mdash", "-" }, { "ndash", "-" }
    };
    return table;
}

// Elements that format text *inside* a word. Removing one must not introduce
// a space, or "<b>via</b>gra" would read as two words and slip past the list.
bool IsInlineElement(const std::string& name) {
    static const char* kInline[] = {
        "a", "b", "i", "u", "s", "em", "strong", "span", "font", "small",
        "big", "sub", "sup", "mark", "abbr", "cite", "code", "tt", "var",
        "wbr", "ins", "del", "bdo", "bdi", "q", "label"
    };
    for (const char* n : kInline) {
        if (name == n) return true;
    }
    return false;
}

// Element name of a tag starting at `open` ("</b >" -> "b"; "" when it is a
// comment, a doctype or a processing instruction).
std::string TagName(const std::string& html, size_t open) {
    size_t i = open + 1;
    if (i < html.size() && html[i] == '/') ++i;
    std::string name;
    while (i < html.size() && (std::isalnum(static_cast<unsigned char>(html[i])) != 0)) {
        name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(html[i]))));
        ++i;
    }
    return name;
}

} // namespace

std::string StripHtml(const std::string& html) {
    std::string out;
    out.reserve(html.size());

    size_t i = 0;
    while (i < html.size()) {
        const char c = html[i];

        if (c == '<') {
            const std::string name = TagName(html, i);

            // Drop the contents of script and style elements entirely.
            if (name == "script" || name == "style") {
                const std::string closing = "</" + name;
                size_t close = html.find(closing, i + 1);
                if (close == std::string::npos) break;
                close = html.find('>', close);
                i = (close == std::string::npos) ? html.size() : close + 1;
                out.push_back(' ');
                continue;
            }

            size_t close = html.find('>', i);
            if (close == std::string::npos) break;   // unterminated tag: drop the rest
            // A block-level tag is a real word boundary; an inline one is not.
            if (!name.empty() && !IsInlineElement(name)) out.push_back(' ');
            else if (name.empty()) out.push_back(' ');   // comment / doctype
            i = close + 1;
            continue;
        }

        if (c == '&') {
            const size_t semi = html.find(';', i);
            if (semi != std::string::npos && semi - i <= 8) {
                const std::string name = Lower(html.substr(i + 1, semi - i - 1));
                auto it = Entities().find(name);
                if (it != Entities().end()) {
                    out += it->second;
                    i = semi + 1;
                    continue;
                }
            }
        }

        out.push_back(c);
        ++i;
    }
    return out;
}

std::string CollapseObfuscation(const std::string& text) {
    std::string out;
    out.reserve(text.size());

    auto isSeparator = [](char c) {
        return c == '.' || c == '-' || c == '_' || c == '*' || c == '|' ||
               c == '+' || c == ' ';
    };

    size_t i = 0;
    while (i < text.size()) {
        // A run looks like L s L s L ... where s is one repeated separator.
        if (std::isalpha(static_cast<unsigned char>(text[i])) &&
            i + 2 < text.size() && isSeparator(text[i + 1]) &&
            std::isalpha(static_cast<unsigned char>(text[i + 2]))) {

            const char separator = text[i + 1];
            std::string letters(1, text[i]);
            size_t j = i;
            while (j + 2 < text.size() && text[j + 1] == separator &&
                   std::isalpha(static_cast<unsigned char>(text[j + 2]))) {
                letters.push_back(text[j + 2]);
                j += 2;
            }
            // Spaces need a longer run to count: "a b c" is ordinary prose,
            // "v i a g r a" is not.
            const size_t minRun = (separator == ' ') ? 5 : 3;
            if (letters.size() >= minRun) {
                out += letters;
                i = j + 1;
                continue;
            }
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

std::string NormalizeForMatching(const std::string& text) {
    // 1. Markup out of the way first, so "<b>vi</b>agra" joins back up.
    std::string s = (text.find('<') != std::string::npos) ? StripHtml(text) : text;

    // 2. Lowercase, and fold the leet substitutions spam relies on. This runs
    //    over rule terms too, so both sides of a match agree on '@' -> 'a'.
    std::string folded;
    folded.reserve(s.size());
    for (char raw : s) {
        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(raw)));
        switch (c) {
            case '0': c = 'o'; break;
            case '1': c = 'i'; break;
            case '3': c = 'e'; break;
            case '4': c = 'a'; break;
            case '5': c = 's'; break;
            case '7': c = 't'; break;
            case '$': c = 's'; break;
            case '@': c = 'a'; break;
            default: break;
        }
        folded.push_back(c);
    }

    // 3. Undo letter-separator obfuscation.
    folded = CollapseObfuscation(folded);

    // 4. Collapse whitespace so multi-word terms match across line breaks.
    std::string out;
    out.reserve(folded.size());
    bool lastWasSpace = false;
    for (char c : folded) {
        const bool space = std::isspace(static_cast<unsigned char>(c)) != 0;
        if (space) {
            if (!lastWasSpace && !out.empty()) out.push_back(' ');
            lastWasSpace = true;
        } else {
            out.push_back(c);
            lastWasSpace = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

} // namespace EmailCleaner
