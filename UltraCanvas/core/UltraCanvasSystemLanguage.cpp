// core/UltraCanvasSystemLanguage.cpp
// See UltraCanvasSystemLanguage.h.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#include "UltraCanvasSystemLanguage.h"

#include "UltraCanvasPathUtf8.h"   // GetEnvUtf8

#include <algorithm>
#include <cctype>

namespace UltraCanvas {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// "de_AT", "de-at", "zh-hant-tw" -> the canonical spelling: the language in
// lower case, a four-letter script in title case, a region in upper case.
std::string CanonicalTag(const std::string& raw) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : raw) {
        if (c == '-' || c == '_') { parts.push_back(current); current.clear(); }
        else current += c;
    }
    parts.push_back(current);
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        std::string p = parts[i];
        if (p.empty()) continue;
        if (i == 0) p = Lower(p);
        else if (p.size() == 4 && std::isalpha(static_cast<unsigned char>(p[0]))) {
            p = Lower(p);
            p[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(p[0])));
        } else if (p.size() == 2 || (p.size() == 3 && std::isdigit(static_cast<unsigned char>(p[0])))) {
            p = Upper(p);
        }
        if (!out.empty()) out += '-';
        out += p;
    }
    return out;
}

void AddOnce(std::vector<std::string>& tags, const std::string& tag) {
    if (tag.empty()) return;
    for (const std::string& have : tags) {
        if (LanguageTagsEqual(have, tag)) return;
    }
    tags.push_back(tag);
}

}  // namespace

std::string LanguageTagFromPosixLocale(const std::string& locale) {
    std::string value = locale;
    size_t cut = value.find('.');            // "de_DE.UTF-8" -> "de_DE"
    if (cut != std::string::npos) value.erase(cut);
    cut = value.find('@');                   // "sr_RS@latin" -> "sr_RS"
    if (cut != std::string::npos) value.erase(cut);
    const std::string lower = Lower(value);
    if (lower.empty() || lower == "c" || lower == "posix") return {};
    return CanonicalTag(value);
}

std::vector<std::string> LanguageTagsFromEnvironment(const std::string& language,
                                                     const std::string& lcAll,
                                                     const std::string& lcMessages,
                                                     const std::string& lang) {
    std::vector<std::string> tags;
    const std::string locale = !lcAll.empty() ? lcAll : !lcMessages.empty() ? lcMessages : lang;
    const std::string localeTag = LanguageTagFromPosixLocale(locale);
    // gettext ignores LANGUAGE under the C locale: a list of languages with
    // no locale behind it is a shell's leftover, not a choice.
    if (!localeTag.empty()) {
        std::string item;
        for (size_t i = 0; i <= language.size(); ++i) {
            if (i == language.size() || language[i] == ':') {
                AddOnce(tags, LanguageTagFromPosixLocale(item));
                item.clear();
            } else {
                item += language[i];
            }
        }
    }
    AddOnce(tags, localeTag);
    return tags;
}

std::string LanguageTagBase(const std::string& tag) {
    const std::string canonical = CanonicalTag(tag);
    return canonical.substr(0, canonical.find('-'));
}

bool LanguageTagsEqual(const std::string& a, const std::string& b) {
    return Lower(CanonicalTag(a)) == Lower(CanonicalTag(b));
}

std::vector<std::string> SystemLanguageTags() {
    std::vector<std::string> tags = LanguageTagsFromEnvironment(
        GetEnvUtf8("LANGUAGE"), GetEnvUtf8("LC_ALL"), GetEnvUtf8("LC_MESSAGES"), GetEnvUtf8("LANG"));
    for (const std::string& native : SystemLanguageTagsNative()) {
        AddOnce(tags, LanguageTagFromPosixLocale(native));
    }
    return tags;
}

std::string SystemLanguage() {
    const std::vector<std::string> tags = SystemLanguageTags();
    return tags.empty() ? std::string("en") : tags.front();
}

}  // namespace UltraCanvas
