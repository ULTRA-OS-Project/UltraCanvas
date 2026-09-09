// core/SpellCheckLanguageNames.cpp
// Shared language code to display name resolution for all spell check backends
// Version: 1.0.0
// Last Modified: 2026-08-25
// Author: UltraCanvas Framework

#include "ISpellCheckBackend.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace UltraCanvas {

namespace {

struct LanguageNameEntry {
    const char* english;
    const char* native;
};

// Base language names. Region qualifiers are appended separately so a single
// table covers every dialect a dictionary provider might install.
const std::unordered_map<std::string, LanguageNameEntry>& BaseLanguageTable() {
    static const std::unordered_map<std::string, LanguageNameEntry> table = {
        { "af", { "Afrikaans",  "Afrikaans" } },
        { "ar", { "Arabic",     "\xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9" } },
        { "be", { "Belarusian", "\xD0\x91\xD0\xB5\xD0\xBB\xD0\xB0\xD1\x80\xD1\x83\xD1\x81\xD0\xBA\xD0\xB0\xD1\x8F" } },
        { "bg", { "Bulgarian",  "\xD0\x91\xD1\x8A\xD0\xBB\xD0\xB3\xD0\xB0\xD1\x80\xD1\x81\xD0\xBA\xD0\xB8" } },
        { "bn", { "Bengali",    "\xE0\xA6\xAC\xE0\xA6\xBE\xE0\xA6\x82\xE0\xA6\xB2\xE0\xA6\xBE" } },
        { "ca", { "Catalan",    "Catal\xC3\xA0" } },
        { "cs", { "Czech",      "\xC4\x8C" "e\xC5\xA1tina" } },   // \x8C must not absorb the following 'e'
        { "cy", { "Welsh",      "Cymraeg" } },
        { "da", { "Danish",     "Dansk" } },
        { "de", { "German",     "Deutsch" } },
        { "el", { "Greek",      "\xCE\x95\xCE\xBB\xCE\xBB\xCE\xB7\xCE\xBD\xCE\xB9\xCE\xBA\xCE\xAC" } },
        { "en", { "English",    "English" } },
        { "eo", { "Esperanto",  "Esperanto" } },
        { "es", { "Spanish",    "Espa\xC3\xB1ol" } },
        { "et", { "Estonian",   "Eesti" } },
        { "eu", { "Basque",     "Euskara" } },
        { "fa", { "Persian",    "\xD9\x81\xD8\xA7\xD8\xB1\xD8\xB3\xDB\x8C" } },
        { "fi", { "Finnish",    "Suomi" } },
        { "fo", { "Faroese",    "F\xC3\xB8royskt" } },
        { "fr", { "French",     "Fran\xC3\xA7"  "ais" } },
        { "ga", { "Irish",      "Gaeilge" } },
        { "gl", { "Galician",   "Galego" } },
        { "he", { "Hebrew",     "\xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA" } },
        { "hi", { "Hindi",      "\xE0\xA4\xB9\xE0\xA4\xBF\xE0\xA4\xA8\xE0\xA5\x8D\xE0\xA4\xA6\xE0\xA5\x80" } },
        { "hr", { "Croatian",   "Hrvatski" } },
        { "hu", { "Hungarian",  "Magyar" } },
        { "hy", { "Armenian",   "\xD5\x80\xD5\xA1\xD5\xB5\xD5\xA5\xD6\x80\xD5\xA5\xD5\xB6" } },
        { "id", { "Indonesian", "Bahasa Indonesia" } },
        { "is", { "Icelandic",  "\xC3\x8Dslenska" } },
        { "it", { "Italian",    "Italiano" } },
        { "ja", { "Japanese",   "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E" } },
        { "ka", { "Georgian",   "\xE1\x83\xA5\xE1\x83\x90\xE1\x83\xA0\xE1\x83\x97\xE1\x83\xA3\xE1\x83\x9A\xE1\x83\x98" } },
        { "kk", { "Kazakh",     "\xD2\x9A\xD0\xB0\xD0\xB7\xD0\xB0\xD2\x9B" } },
        { "ko", { "Korean",     "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4" } },
        { "lt", { "Lithuanian", "Lietuvi\xC5\xB3" } },
        { "lv", { "Latvian",    "Latvie\xC5\xA1u" } },
        { "mk", { "Macedonian", "\xD0\x9C\xD0\xB0\xD0\xBA\xD0\xB5\xD0\xB4\xD0\xBE\xD0\xBD\xD1\x81\xD0\xBA\xD0\xB8" } },
        { "ms", { "Malay",      "Bahasa Melayu" } },
        { "nb", { "Norwegian Bokmal", "Norsk bokm\xC3\xA5l" } },
        { "ne", { "Nepali",     "\xE0\xA4\xA8\xE0\xA5\x87\xE0\xA4\xAA\xE0\xA4\xBE\xE0\xA4\xB2\xE0\xA5\x80" } },
        { "nl", { "Dutch",      "Nederlands" } },
        { "nn", { "Norwegian Nynorsk", "Norsk nynorsk" } },
        { "no", { "Norwegian",  "Norsk" } },
        { "pl", { "Polish",     "Polski" } },
        { "pt", { "Portuguese", "Portugu\xC3\xAAs" } },
        { "ro", { "Romanian",   "Rom\xC3\xA2n\xC4\x83" } },
        { "ru", { "Russian",    "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9" } },
        { "sk", { "Slovak",     "Sloven\xC4\x8Dina" } },
        { "sl", { "Slovenian",  "Sloven\xC5\xA1\xC4\x8Dina" } },
        { "sq", { "Albanian",   "Shqip" } },
        { "sr", { "Serbian",    "\xD0\xA1\xD1\x80\xD0\xBF\xD1\x81\xD0\xBA\xD0\xB8" } },
        { "sv", { "Swedish",    "Svenska" } },
        { "sw", { "Swahili",    "Kiswahili" } },
        { "ta", { "Tamil",      "\xE0\xAE\xA4\xE0\xAE\xAE\xE0\xAE\xBF\xE0\xAE\xB4\xE0\xAF\x8D" } },
        { "th", { "Thai",       "\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2" } },
        { "tr", { "Turkish",    "T\xC3\xBCrk\xC3\xA7"  "e" } },
        { "uk", { "Ukrainian",  "\xD0\xA3\xD0\xBA\xD1\x80\xD0\xB0\xD1\x97\xD0\xBD\xD1\x81\xD1\x8C\xD0\xBA\xD0\xB0" } },
        { "ur", { "Urdu",       "\xD8\xA7\xD8\xB1\xD8\xAF\xD9\x88" } },
        { "vi", { "Vietnamese", "Ti\xE1\xBA\xBFng Vi\xE1\xBB\x87t" } },
        { "zh", { "Chinese",    "\xE4\xB8\xAD\xE6\x96\x87" } },
    };
    return table;
}

// Region names shown in parentheses. Only the regions that commonly ship
// dictionaries are listed; anything else falls back to the raw region code.
const std::unordered_map<std::string, const char*>& RegionTable() {
    static const std::unordered_map<std::string, const char*> table = {
        { "AR", "Argentina" },     { "AT", "Austria" },       { "AU", "Australia" },
        { "BE", "Belgium" },       { "BR", "Brazil" },        { "CA", "Canada" },
        { "CH", "Switzerland" },   { "CL", "Chile" },         { "CN", "China" },
        { "CO", "Colombia" },      { "CZ", "Czechia" },       { "DE", "Germany" },
        { "DK", "Denmark" },       { "ES", "Spain" },         { "FI", "Finland" },
        { "FR", "France" },        { "GB", "United Kingdom" },{ "GR", "Greece" },
        { "HK", "Hong Kong" },     { "IE", "Ireland" },       { "IL", "Israel" },
        { "IN", "India" },         { "IT", "Italy" },         { "JP", "Japan" },
        { "KR", "Korea" },         { "MX", "Mexico" },        { "NL", "Netherlands" },
        { "NO", "Norway" },        { "NZ", "New Zealand" },   { "PL", "Poland" },
        { "PT", "Portugal" },      { "RU", "Russia" },        { "SE", "Sweden" },
        { "TR", "Turkey" },        { "TW", "Taiwan" },        { "UA", "Ukraine" },
        { "US", "United States" }, { "ZA", "South Africa" },
    };
    return table;
}

} // namespace

SpellLanguageInfo ResolveSpellLanguageNames(const std::string& languageCode) {
    SpellLanguageInfo info;
    info.code = languageCode;
    info.displayName = languageCode;
    info.nativeName = languageCode;
    // isAvailable is deliberately left at its default here: only the backend
    // that found the dictionary knows whether it can actually be loaded.

    if (languageCode.empty()) return info;

    // Normalise "de-DE" and "de_DE.UTF-8" to base "de" and region "DE".
    std::string working = languageCode;
    size_t cut = working.find('.');
    if (cut != std::string::npos) working.erase(cut);
    cut = working.find('@');
    if (cut != std::string::npos) working.erase(cut);
    std::replace(working.begin(), working.end(), '-', '_');

    std::string base = working;
    std::string region;
    const size_t separator = working.find('_');
    if (separator != std::string::npos) {
        base = working.substr(0, separator);
        region = working.substr(separator + 1);
    }

    std::transform(base.begin(), base.end(), base.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(region.begin(), region.end(), region.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    const auto& languages = BaseLanguageTable();
    auto languageIt = languages.find(base);
    if (languageIt == languages.end()) return info;

    info.displayName = languageIt->second.english;
    info.nativeName = languageIt->second.native;

    if (region.empty()) return info;

    std::string regionName = region;
    const auto& regions = RegionTable();
    auto regionIt = regions.find(region);
    if (regionIt != regions.end()) regionName = regionIt->second;

    info.displayName += " (" + regionName + ")";
    info.nativeName += " (" + regionName + ")";
    return info;
}

} // namespace UltraCanvas
