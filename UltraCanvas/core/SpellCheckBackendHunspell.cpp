// core/SpellCheckBackendHunspell.cpp
// Portable Hunspell spell check backend with dictionary discovery
// Version: 1.0.1
// Last Modified: 2026-08-24
// Author: UltraCanvas Framework
//
// This backend is always compiled. It is the fallback when a native OS spell
// service is unavailable, and the only backend on WASM and UltraOS.
//
// Build flag: ULTRACANVAS_HAS_HUNSPELL
//   Defined  -> real Hunspell is linked
//   Undefined-> a null backend compiles cleanly and reports zero dictionaries,
//               so the framework never fails to build because of a missing
//               optional dependency.
//
// ENCODING: Hunspell expects words in the encoding declared by the dictionary's
// SET directive, not in UTF-8. The encoding is read from the .aff during
// discovery so a dictionary whose encoding cannot be represented is reported as
// unavailable rather than silently flagging every accented word as wrong.

#include "ISpellCheckBackend.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#ifdef ULTRACANVAS_HAS_HUNSPELL
    #include <hunspell/hunspell.hxx>
#endif

namespace UltraCanvas {

namespace {

namespace fs = std::filesystem;

// ===== DICTIONARY TEXT ENCODING =====
// Hunspell dictionaries are either UTF-8 or one of the single-byte Latin sets.
// Anything else (KOI8-R, CP1251, ISO8859-2 and friends) would need a full
// conversion table; those dictionaries are reported unavailable instead of
// being loaded and then producing nonsense results.
enum class DictionaryEncoding {
    Utf8,
    Latin1,        // ISO-8859-1
    Latin9,        // ISO-8859-15
    Unsupported
};

// ISO-8859-15 differs from ISO-8859-1 in exactly eight positions.
struct Latin9Difference {
    unsigned char byte;
    unsigned int codepoint;
};
const Latin9Difference latin9Differences[] = {
    { 0xA4, 0x20AC }, { 0xA6, 0x0160 }, { 0xA8, 0x0161 }, { 0xB4, 0x017D },
    { 0xB8, 0x017E }, { 0xBC, 0x0152 }, { 0xBD, 0x0153 }, { 0xBE, 0x0178 },
};

DictionaryEncoding ParseEncodingName(const std::string& rawName) {
    std::string name;
    name.reserve(rawName.size());
    for (unsigned char c : rawName) {
        if (c == '-' || c == '_' || c == ' ') continue;
        name += static_cast<char>(std::toupper(c));
    }
    if (name == "UTF8") return DictionaryEncoding::Utf8;
    if (name == "ISO88591" || name == "LATIN1") return DictionaryEncoding::Latin1;
    if (name == "ISO885915" || name == "LATIN9") return DictionaryEncoding::Latin9;
    return DictionaryEncoding::Unsupported;
}

// Reads the SET directive from a .aff file. Hunspell's documented default when
// the directive is absent is ISO-8859-1.
DictionaryEncoding ReadAffixEncoding(const std::string& affixPath) {
    std::ifstream input(affixPath);
    if (!input) return DictionaryEncoding::Unsupported;

    std::string line;
    int scanned = 0;
    while (std::getline(input, line) && scanned < 64) {
        ++scanned;
        if (line.rfind("SET", 0) != 0) continue;
        if (line.size() < 4 || !std::isspace(static_cast<unsigned char>(line[3]))) continue;

        size_t start = line.find_first_not_of(" \t", 3);
        if (start == std::string::npos) break;
        size_t end = line.find_last_not_of(" \t\r\n");
        return ParseEncodingName(line.substr(start, end - start + 1));
    }
    return DictionaryEncoding::Latin1;
}

// ===== UTF-8 <-> SINGLE BYTE CONVERSION =====
// Only used for Latin1/Latin9 dictionaries; UTF-8 dictionaries pass through
// untouched, which is every dictionary shipped by a current distribution.

void AppendUtf8(std::string& out, unsigned int codepoint) {
    if (codepoint < 0x80) {
        out += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

// Returns false when the text contains a character the target encoding cannot
// represent, so the caller can decline rather than substitute a wrong byte.
bool Utf8ToSingleByte(const std::string& text, DictionaryEncoding encoding, std::string& out) {
    out.clear();
    out.reserve(text.size());

    for (size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        unsigned int codepoint = 0;
        size_t length = 1;

        if (lead < 0x80) {
            codepoint = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            length = 2;
            if (i + 1 >= text.size()) return false;
            codepoint = ((lead & 0x1Fu) << 6) |
                        (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            if (i + 2 >= text.size()) return false;
            codepoint = ((lead & 0x0Fu) << 12) |
                        ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6) |
                        (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
        } else {
            return false;   // 4-byte codepoints never fit a single-byte set
        }
        i += length;

        if (encoding == DictionaryEncoding::Latin9) {
            bool matched = false;
            for (const Latin9Difference& difference : latin9Differences) {
                if (difference.codepoint == codepoint) {
                    out += static_cast<char>(difference.byte);
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
            // The eight Latin-1 characters those positions replaced are absent
            // from Latin-9, so reject them rather than emit the wrong glyph.
            for (const Latin9Difference& difference : latin9Differences) {
                if (difference.byte == codepoint) return false;
            }
        }

        if (codepoint > 0xFF) return false;
        out += static_cast<char>(codepoint);
    }
    return true;
}

std::string SingleByteToUtf8(const std::string& text, DictionaryEncoding encoding) {
    std::string out;
    out.reserve(text.size());

    for (unsigned char byte : text) {
        unsigned int codepoint = byte;
        if (encoding == DictionaryEncoding::Latin9) {
            for (const Latin9Difference& difference : latin9Differences) {
                if (difference.byte == byte) {
                    codepoint = difference.codepoint;
                    break;
                }
            }
        }
        AppendUtf8(out, codepoint);
    }
    return out;
}

// ===== DICTIONARY DISCOVERY =====

struct DictionaryEntry {
    std::string code;
    std::string basePath;                                        // without extension
    DictionaryEncoding encoding = DictionaryEncoding::Unsupported;
};

// Directories searched for <code>.aff / <code>.dic pairs, in priority order.
// ULTRACANVAS_DICT_PATH (colon or semicolon separated) always wins so an
// application can ship its own dictionaries next to the executable.
std::vector<std::string> BuildDictionarySearchPaths() {
    std::vector<std::string> paths;

    if (const char* custom = std::getenv("ULTRACANVAS_DICT_PATH")) {
#if defined(_WIN32)
        const char separator = ';';
#else
        const char separator = ':';
#endif
        std::string value(custom);
        size_t start = 0;
        while (start <= value.size()) {
            size_t end = value.find(separator, start);
            if (end == std::string::npos) end = value.size();
            std::string entry = value.substr(start, end - start);
            if (!entry.empty()) paths.push_back(entry);
            start = end + 1;
        }
    }

    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        paths.push_back(std::string(xdg) + "/UltraCanvas/dictionaries");
    }
    if (const char* home = std::getenv("HOME")) {
        paths.push_back(std::string(home) + "/.local/share/UltraCanvas/dictionaries");
        paths.push_back(std::string(home) + "/.local/share/hunspell");
        paths.push_back(std::string(home) + "/Library/Spelling");
    }
    if (const char* appData = std::getenv("APPDATA")) {
        paths.push_back(std::string(appData) + "\\UltraCanvas\\dictionaries");
    }

    paths.push_back("dictionaries");
    paths.push_back("Resources/dictionaries");

#if !defined(_WIN32)
    paths.push_back("/usr/share/hunspell");
    paths.push_back("/usr/share/myspell");
    paths.push_back("/usr/share/myspell/dicts");
    paths.push_back("/usr/local/share/hunspell");
    paths.push_back("/Library/Spelling");
    paths.push_back("/System/Library/Spelling");
#endif

    return paths;
}

// Collects every .aff that has a matching .dic. Keyed by code so the
// highest-priority directory wins.
//
// Every filesystem query takes its own std::error_code: sharing one across the
// loop lets a single failed probe latch and abort the rest of the directory.
void CollectDictionaries(std::vector<DictionaryEntry>& outFound) {
    std::set<std::string> seen;

    for (const std::string& directory : BuildDictionarySearchPaths()) {
        std::error_code directoryError;
        if (!fs::is_directory(directory, directoryError) || directoryError) continue;

        std::error_code iterationError;
        fs::directory_iterator it(directory, iterationError);
        if (iterationError) continue;

        for (const fs::directory_entry& entry : it) {
            std::error_code fileError;
            if (!entry.is_regular_file(fileError) || fileError) continue;
            if (entry.path().extension() != ".aff") continue;

            const std::string code = entry.path().stem().string();
            if (code.empty() || seen.count(code) != 0) continue;

            fs::path dictionaryPath = entry.path();
            dictionaryPath.replace_extension(".dic");

            std::error_code existsError;
            if (!fs::exists(dictionaryPath, existsError) || existsError) continue;

            fs::path base = entry.path();
            base.replace_extension();

            DictionaryEntry found;
            found.code = code;
            found.basePath = base.string();
            found.encoding = ReadAffixEncoding(entry.path().string());

            seen.insert(code);
            outFound.push_back(std::move(found));
        }
    }

    std::sort(outFound.begin(), outFound.end(),
              [](const DictionaryEntry& a, const DictionaryEntry& b) { return a.code < b.code; });
}

} // namespace

// ===================================================================
// HUNSPELL BACKEND
// ===================================================================
class SpellCheckBackendHunspell : public ISpellCheckBackend {
public:
    ~SpellCheckBackendHunspell() override {
        Shutdown();
    }

    std::string GetBackendName() const override {
#ifdef ULTRACANVAS_HAS_HUNSPELL
        return "Hunspell";
#else
        return "Hunspell (not compiled in)";
#endif
    }

    bool Initialize() override {
        std::lock_guard<std::mutex> lock(mutex);
        discovered.clear();
        CollectDictionaries(discovered);
        return true;   // Zero dictionaries is a valid, reportable state
    }

    void Shutdown() override {
        std::lock_guard<std::mutex> lock(mutex);
        ReleaseHandleLocked();
        discovered.clear();
        currentLanguage.clear();
    }

    std::vector<SpellLanguageInfo> EnumerateLanguages() override {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<SpellLanguageInfo> languages;
        languages.reserve(discovered.size());

        for (const DictionaryEntry& entry : discovered) {
            SpellLanguageInfo info = ResolveSpellLanguageNames(entry.code);
            info.code = entry.code;
#ifdef ULTRACANVAS_HAS_HUNSPELL
            // An encoding we cannot round-trip would flag every accented word,
            // so such a dictionary is listed but never offered as usable.
            info.isAvailable = (entry.encoding != DictionaryEncoding::Unsupported);
#else
            info.isAvailable = false;   // Files exist but nothing can read them
#endif
            languages.push_back(std::move(info));
        }
        return languages;
    }

    bool SetLanguage(const std::string& languageCode) override {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = std::find_if(discovered.begin(), discovered.end(),
                               [&languageCode](const DictionaryEntry& entry) {
                                   return entry.code == languageCode;
                               });
        if (it == discovered.end()) return false;

#ifdef ULTRACANVAS_HAS_HUNSPELL
        if (it->encoding == DictionaryEncoding::Unsupported) return false;

        const std::string affixPath = it->basePath + ".aff";
        const std::string dictionaryPath = it->basePath + ".dic";

        Hunspell* opened = nullptr;
        try {
            opened = new Hunspell(affixPath.c_str(), dictionaryPath.c_str());
        } catch (...) {
            // A malformed or unreadable .aff throws out of the constructor.
            return false;
        }
        if (!opened) return false;

        ReleaseHandleLocked();
        handle = opened;
        encoding = it->encoding;
        currentLanguage = languageCode;
        return true;
#else
        (void)languageCode;
        return false;   // Nothing can be checked without Hunspell linked in
#endif
    }

    std::string GetLanguage() const override {
        std::lock_guard<std::mutex> lock(mutex);
        return currentLanguage;
    }

    bool IsWordCorrect(const std::string& word) override {
#ifdef ULTRACANVAS_HAS_HUNSPELL
        std::lock_guard<std::mutex> lock(mutex);
        if (!handle) return true;   // No dictionary loaded: flag nothing

        std::string encoded;
        if (!EncodeForDictionaryLocked(word, encoded)) return true;
        return handle->spell(encoded);
#else
        (void)word;
        return true;
#endif
    }

    std::vector<std::string> GetSuggestions(const std::string& word, int maxCount) override {
#ifdef ULTRACANVAS_HAS_HUNSPELL
        std::lock_guard<std::mutex> lock(mutex);
        if (!handle) return {};

        std::string encoded;
        if (!EncodeForDictionaryLocked(word, encoded)) return {};

        std::vector<std::string> suggestions = handle->suggest(encoded);
        if (maxCount > 0 && suggestions.size() > static_cast<size_t>(maxCount)) {
            suggestions.resize(static_cast<size_t>(maxCount));
        }
        if (encoding != DictionaryEncoding::Utf8) {
            for (std::string& suggestion : suggestions) {
                suggestion = SingleByteToUtf8(suggestion, encoding);
            }
        }
        return suggestions;
#else
        (void)word;
        (void)maxCount;
        return {};
#endif
    }

    bool AddWordToBackendDictionary(const std::string& word) override {
#ifdef ULTRACANVAS_HAS_HUNSPELL
        std::lock_guard<std::mutex> lock(mutex);
        if (!handle) return false;

        std::string encoded;
        if (!EncodeForDictionaryLocked(word, encoded)) return false;
        handle->add(encoded);
        // Hunspell's runtime addition is not persisted; the service keeps its
        // own on-disk user dictionary, so report false to make that explicit.
        return false;
#else
        (void)word;
        return false;
#endif
    }

    bool IsThreadSafe() const override { return false; }

private:
#ifdef ULTRACANVAS_HAS_HUNSPELL
    // Converts a UTF-8 word into the loaded dictionary's encoding. Returns
    // false when the word cannot be represented there, which the callers treat
    // as "not checkable" rather than "misspelled".
    bool EncodeForDictionaryLocked(const std::string& word, std::string& out) const {
        if (encoding == DictionaryEncoding::Utf8) {
            out = word;
            return true;
        }
        return Utf8ToSingleByte(word, encoding, out);
    }
#endif

    void ReleaseHandleLocked() {
#ifdef ULTRACANVAS_HAS_HUNSPELL
        if (handle) {
            delete handle;
            handle = nullptr;
        }
#endif
    }

    mutable std::mutex mutex;
    std::vector<DictionaryEntry> discovered;
    std::string currentLanguage;
    DictionaryEncoding encoding = DictionaryEncoding::Utf8;

#ifdef ULTRACANVAS_HAS_HUNSPELL
    Hunspell* handle = nullptr;
#endif
};

std::unique_ptr<ISpellCheckBackend> CreateHunspellSpellCheckBackend() {
    return std::make_unique<SpellCheckBackendHunspell>();
}

} // namespace UltraCanvas
