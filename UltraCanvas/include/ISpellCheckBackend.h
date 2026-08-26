// include/ISpellCheckBackend.h
// Abstract spell-check backend interface bridging native OS spell services and Hunspell
// Version: 1.0.0
// Last Modified: 2026-08-25
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>
#include <memory>

namespace UltraCanvas {

// ===== LANGUAGE DESCRIPTOR =====
// Describes one dictionary that the active backend can load.
// Populated at runtime by EnumerateLanguages() - never hard-coded, so the
// host application menu always reflects the dictionaries actually installed.
struct SpellLanguageInfo {
    std::string code;          // BCP-47 / POSIX style: "en_US", "de_DE", "fr_FR"
    std::string displayName;   // English name:  "German (Germany)"
    std::string nativeName;    // Endonym:       "Deutsch (Deutschland)"
    bool isAvailable = false;  // Dictionary files present and loadable

    SpellLanguageInfo() = default;
    SpellLanguageInfo(const std::string& languageCode,
                      const std::string& display,
                      const std::string& native,
                      bool available = true)
        : code(languageCode), displayName(display), nativeName(native), isAvailable(available) {}
};

// ===== BACKEND INTERFACE =====
// One instance is owned by UltraCanvasSpellChecker. Implementations live in:
//   OS/Linux/UltraCanvasSpellCheckSupport.cpp      -> enchant-2
//   OS/MSWindows/UltraCanvasSpellCheckSupport.cpp  -> ISpellChecker (Windows 8+)
//   OS/MacOS/UltraCanvasSpellCheckSupport.mm       -> NSSpellChecker
//   OS/UltraOS/UltraCanvasSpellCheckSupport.cpp    -> delegates to Hunspell
//   core/SpellCheckBackendHunspell.cpp             -> portable fallback (also WASM)
//
// THREADING CONTRACT: implementations are assumed NOT thread safe unless
// IsThreadSafe() returns true. UltraCanvasSpellChecker serialises every call
// with a per-word mutex, so a background check never blocks the render thread
// for longer than a single word lookup.
class ISpellCheckBackend {
public:
    virtual ~ISpellCheckBackend() = default;

    // ===== LIFECYCLE =====
    virtual std::string GetBackendName() const = 0;
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    // ===== LANGUAGE MANAGEMENT =====
    virtual std::vector<SpellLanguageInfo> EnumerateLanguages() = 0;
    virtual bool SetLanguage(const std::string& languageCode) = 0;
    virtual std::string GetLanguage() const = 0;

    // ===== CHECKING =====
    virtual bool IsWordCorrect(const std::string& word) = 0;
    virtual std::vector<std::string> GetSuggestions(const std::string& word, int maxCount) = 0;

    // ===== OPTIONAL CAPABILITIES =====
    // Backends that own a persistent user dictionary (macOS, Windows, enchant)
    // override this. Returning false makes UltraCanvasSpellChecker fall back to
    // its own on-disk user dictionary, so behaviour is identical either way.
    virtual bool AddWordToBackendDictionary(const std::string& word) {
        (void)word;
        return false;
    }

    virtual bool IsThreadSafe() const { return false; }
};

// ===== SHARED LANGUAGE NAME RESOLUTION =====
// Maps a dictionary code to English and native display names so every backend
// produces identical menu labels. Unknown codes return the code itself as both
// names, with isAvailable left untouched by this call.
// Implemented once in core/SpellCheckLanguageNames.cpp.
SpellLanguageInfo ResolveSpellLanguageNames(const std::string& languageCode);

// ===== BACKEND FACTORIES =====
// Exactly one definition of CreateNativeSpellCheckBackend() is compiled,
// selected by the OS/<Platform> source file included in the build.
std::unique_ptr<ISpellCheckBackend> CreateNativeSpellCheckBackend();

// Always available from core/. Used as the fallback when the native backend
// cannot initialise, and as the only backend on WASM and UltraOS.
std::unique_ptr<ISpellCheckBackend> CreateHunspellSpellCheckBackend();

} // namespace UltraCanvas
