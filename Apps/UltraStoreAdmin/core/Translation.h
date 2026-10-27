// Apps/UltraStoreAdmin/core/Translation.h
// The texts in every official language. The creator writes the summary, the
// description and the release notes in at least one language
// (`source_language`); the admin backend translates them into every language
// the store serves, with UltraAI (the local model on the publishing machine,
// or whichever provider its settings name), and lists what it translated
// under `machine_translated`, so the store can say so and offer the
// original. A text a person wrote in a language is never overwritten; a
// machine one is redone on request.
//
// The translator is an interface here, so a test hands in a fake and
// nothing in a test reaches a model; the UltraAI one is built when the
// UltraAI module is (ULTRASTOREADMIN_HAVE_ULTRAAI).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "UltraStoreTypes.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraStoreAdmin {

using UltraStore::Component;
using UltraStore::ErrorCode;
using UltraStore::Result;

class ITextTranslator {
public:
    virtual ~ITextTranslator() = default;
    // The texts, in order, in the target language ("de"), from the source
    // language ("en"; empty: detect).
    virtual Result<std::vector<std::string>> Translate(const std::vector<std::string>& texts,
                                                       const std::string& sourceLanguage,
                                                       const std::string& targetLanguage) = 0;
};

struct Language {
    std::string code;   // "de"
    std::string name;   // "German"
};

// The languages the store serves out of the box: the official languages of
// the EU's and the EFTA's countries, then those of the other markets the
// apps ship to. The setting `texts.languages` (codes separated by spaces)
// narrows or widens the list.
const std::vector<Language>& OfficialLanguages();
std::string LanguageName(const std::string& code);
std::vector<std::string> OfficialLanguageCodes();

struct TranslationReport {
    size_t texts = 0;                       // texts translated
    std::vector<std::string> languages;     // languages filled, in order
    std::vector<std::string> skipped;       // languages that already had every text
    std::vector<std::string> errors;        // "de: <why>", the component keeps what it had
};

// Fills the summary, the description and every release's notes in each
// target language the component lacks, from its source language (or the
// first language it has), and records them under machine_translated.
// redo: the machine translations are translated again, too.
Result<TranslationReport> TranslateComponent(Component& component, ITextTranslator& translator,
                                             const std::vector<std::string>& targetLanguages, bool redo = false);

// The languages a component is missing a text in, out of the targets.
std::vector<std::string> LanguagesToFill(const Component& component, const std::vector<std::string>& targetLanguages, bool redo = false);

// UltraAI: the provider the setting names ("qwen", "llama-cpp", "" for the
// routing policy's choice), its base URL and model when set. nullptr with
// the reason when this build has no UltraAI or the provider is unknown.
std::unique_ptr<ITextTranslator> CreateUltraAITranslator(const std::string& providerId, const std::string& baseUrl,
                                                         const std::string& model, std::string* error);
bool HaveUltraAI();

} // namespace UltraStoreAdmin
