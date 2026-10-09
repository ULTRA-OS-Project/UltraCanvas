// Apps/EmailCleaner/engine/EmailCleanerText.cpp
// Obfuscation folding and the shared normalisation pipeline.
// Version: 0.2.0 - HTML is read through the HTMLReader module (HTML::ExtractPlainText):
//                  every entity decoded; dashes and the ellipsis fold to ASCII
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerText.h"

#include "HTMLReader/HTMLDocument.h"   // HTML::ExtractPlainText

#include <cctype>
#include <initializer_list>
#include <string>
#include <utility>

namespace EmailCleaner {

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
    // 1. Markup out of the way first, so "<b>vi</b>agra" joins back up (an
    //    inline element leaves no space; a block does), entities decoded.
    std::string s = (text.find('<') != std::string::npos) ? UltraCanvas::HTML::ExtractPlainText(text)
                                                          : text;
    // Typographic dashes and the ellipsis read as their ASCII spelling, so a
    // term written "-" or "..." matches "&mdash;" and "&hellip;" too.
    for (const auto& [from, to] : { std::pair<const char*, const char*>{"\xE2\x80\x93", "-"},   // en dash
                                    {"\xE2\x80\x94", "-"},                                      // em dash
                                    {"\xE2\x80\xA6", "..."} }) {                              // ellipsis
        for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at)) {
            s.replace(at, 3, to);
            at += std::char_traits<char>::length(to);
        }
    }

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
