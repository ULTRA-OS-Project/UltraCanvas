// OS/Linux/UltraCanvasSpellCheckSupport.cpp
// Linux native spell check backend built on enchant-2
// Version: 1.0.1
// Last Modified: 2026-08-24
// Author: UltraCanvas Framework
//
// enchant-2 is the same broker GTK, LibreOffice and Firefox use on Linux, so
// this backend sees exactly the dictionaries the user already has installed
// through Hunspell, Aspell, Nuspell or Voikko.
//
// Build flag: ULTRACANVAS_HAS_ENCHANT
//   Undefined -> CreateNativeSpellCheckBackend() returns nullptr and
//                UltraCanvasSpellChecker falls back to Hunspell automatically.
// pkg-config: enchant-2     apt: libenchant-2-dev

#include "ISpellCheckBackend.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#ifdef ULTRACANVAS_HAS_ENCHANT
    #include <enchant.h>
#endif

namespace UltraCanvas {

#ifdef ULTRACANVAS_HAS_ENCHANT

namespace {

// enchant_broker_list_dicts hands each dictionary to a C callback.
void CollectDictionaryCallback(const char* languageTag,
                               const char* /*providerName*/,
                               const char* /*providerDescription*/,
                               const char* /*providerFile*/,
                               void* userData) {
    if (!languageTag || !userData) return;
    auto* codes = static_cast<std::vector<std::string>*>(userData);
    codes->emplace_back(languageTag);
}

} // namespace

class SpellCheckBackendEnchant : public ISpellCheckBackend {
public:
    ~SpellCheckBackendEnchant() override {
        Shutdown();
    }

    std::string GetBackendName() const override { return "enchant-2"; }

    bool Initialize() override {
        std::lock_guard<std::mutex> lock(mutex);
        if (broker) return true;
        broker = enchant_broker_init();
        return broker != nullptr;
    }

    void Shutdown() override {
        std::lock_guard<std::mutex> lock(mutex);
        ReleaseDictionaryLocked();
        if (broker) {
            enchant_broker_free(broker);
            broker = nullptr;
        }
        currentLanguage.clear();
    }

    std::vector<SpellLanguageInfo> EnumerateLanguages() override {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<SpellLanguageInfo> languages;
        if (!broker) return languages;

        std::vector<std::string> codes;
        enchant_broker_list_dicts(broker, CollectDictionaryCallback, &codes);

        languages.reserve(codes.size());
        for (const std::string& code : codes) {
            SpellLanguageInfo info = ResolveSpellLanguageNames(code);
            info.code = code;
            info.isAvailable = true;
            languages.push_back(std::move(info));
        }
        return languages;
    }

    bool SetLanguage(const std::string& languageCode) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (!broker) return false;

        EnchantDict* requested = enchant_broker_request_dict(broker, languageCode.c_str());
        if (!requested) return false;

        ReleaseDictionaryLocked();
        dictionary = requested;
        currentLanguage = languageCode;
        return true;
    }

    std::string GetLanguage() const override {
        std::lock_guard<std::mutex> lock(mutex);
        return currentLanguage;
    }

    bool IsWordCorrect(const std::string& word) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (!dictionary) return true;
        // enchant_dict_check returns 0 for a correct word, positive when the
        // word is unknown, negative on error. Errors must not flag the word.
        return enchant_dict_check(dictionary, word.c_str(), static_cast<ssize_t>(word.size())) <= 0;
    }

    std::vector<std::string> GetSuggestions(const std::string& word, int maxCount) override {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::string> suggestions;
        if (!dictionary) return suggestions;

        size_t count = 0;
        char** raw = enchant_dict_suggest(dictionary, word.c_str(),
                                          static_cast<ssize_t>(word.size()), &count);
        if (!raw) return suggestions;

        const size_t limit = (maxCount > 0)
                             ? std::min(count, static_cast<size_t>(maxCount))
                             : count;
        suggestions.reserve(limit);
        for (size_t i = 0; i < limit; ++i) {
            if (raw[i]) suggestions.emplace_back(raw[i]);
        }

        enchant_dict_free_string_list(dictionary, raw);
        return suggestions;
    }

    bool AddWordToBackendDictionary(const std::string& word) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (!dictionary) return false;
        // Writes to the shared personal word list under ~/.config/enchant,
        // so the word is also known to other enchant applications.
        enchant_dict_add(dictionary, word.c_str(), static_cast<ssize_t>(word.size()));
        return true;
    }

    bool IsThreadSafe() const override { return false; }

private:
    void ReleaseDictionaryLocked() {
        if (broker && dictionary) {
            enchant_broker_free_dict(broker, dictionary);
        }
        dictionary = nullptr;
    }

    mutable std::mutex mutex;
    EnchantBroker* broker = nullptr;
    EnchantDict* dictionary = nullptr;
    std::string currentLanguage;
};

std::unique_ptr<ISpellCheckBackend> CreateNativeSpellCheckBackend() {
    return std::make_unique<SpellCheckBackendEnchant>();
}

#else // ULTRACANVAS_HAS_ENCHANT

std::unique_ptr<ISpellCheckBackend> CreateNativeSpellCheckBackend() {
    return nullptr;   // Service falls back to the Hunspell backend
}

#endif // ULTRACANVAS_HAS_ENCHANT

} // namespace UltraCanvas
