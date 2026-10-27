// Apps/UltraStoreAdmin/core/Translation.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Translation.h"

#ifdef ULTRASTOREADMIN_HAVE_ULTRAAI
#include "UltraAITranslator.h"
#endif

#include <algorithm>

namespace UltraStoreAdmin {

const std::vector<Language>& OfficialLanguages() {
    static const std::vector<Language> languages = {
        // The EU's 24 official languages
        {"bg", "Bulgarian"}, {"cs", "Czech"}, {"da", "Danish"}, {"de", "German"}, {"el", "Greek"},
        {"en", "English"}, {"es", "Spanish"}, {"et", "Estonian"}, {"fi", "Finnish"}, {"fr", "French"},
        {"ga", "Irish"}, {"hr", "Croatian"}, {"hu", "Hungarian"}, {"it", "Italian"}, {"lt", "Lithuanian"},
        {"lv", "Latvian"}, {"mt", "Maltese"}, {"nl", "Dutch"}, {"pl", "Polish"}, {"pt", "Portuguese"},
        {"ro", "Romanian"}, {"sk", "Slovak"}, {"sl", "Slovenian"}, {"sv", "Swedish"},
        // EFTA and the UK
        {"is", "Icelandic"}, {"nb", "Norwegian"}, {"rm", "Romansh"},
        // The other markets the apps ship to
        {"uk", "Ukrainian"}, {"tr", "Turkish"}, {"sr", "Serbian"}, {"ru", "Russian"},
        {"ar", "Arabic"}, {"he", "Hebrew"}, {"hi", "Hindi"}, {"id", "Indonesian"}, {"ja", "Japanese"},
        {"ko", "Korean"}, {"th", "Thai"}, {"vi", "Vietnamese"}, {"zh", "Chinese (Simplified)"},
        {"zh-TW", "Chinese (Traditional)"}, {"pt-BR", "Portuguese (Brazil)"},
    };
    return languages;
}

std::string LanguageName(const std::string& code) {
    for (const Language& l : OfficialLanguages()) if (l.code == code) return l.name;
    return code;
}

std::vector<std::string> OfficialLanguageCodes() {
    std::vector<std::string> out;
    for (const Language& l : OfficialLanguages()) out.push_back(l.code);
    return out;
}

namespace {

bool IsMachine(const Component& c, const std::string& language) {
    return std::find(c.machineTranslated.begin(), c.machineTranslated.end(), language) != c.machineTranslated.end();
}

// The texts of a component: the summary, the description, each release's notes.
std::vector<UltraStore::LocalizedText*> Texts(Component& c) {
    std::vector<UltraStore::LocalizedText*> out = {&c.summary, &c.description};
    for (UltraStore::Release& r : c.releases) out.push_back(&r.notes);
    return out;
}

std::string SourceOf(const Component& c) {
    if (!c.sourceLanguage.empty()) return c.sourceLanguage;
    if (c.summary.byLanguage.count("en") || c.description.byLanguage.count("en")) return "en";
    if (!c.summary.byLanguage.empty()) return c.summary.byLanguage.begin()->first;
    if (!c.description.byLanguage.empty()) return c.description.byLanguage.begin()->first;
    return {};
}

} // namespace

std::vector<std::string> LanguagesToFill(const Component& component, const std::vector<std::string>& targetLanguages, bool redo) {
    std::vector<std::string> out;
    const std::string source = SourceOf(component);
    Component& mutableRef = const_cast<Component&>(component);   // Texts() reads only
    for (const std::string& language : targetLanguages) {
        if (language == source) continue;
        bool missing = false;
        for (const UltraStore::LocalizedText* t : Texts(mutableRef)) {
            if (t->byLanguage.count(source) && !t->byLanguage.count(language)) missing = true;
        }
        if (missing || (redo && IsMachine(component, language))) out.push_back(language);
    }
    return out;
}

Result<TranslationReport> TranslateComponent(Component& component, ITextTranslator& translator,
                                             const std::vector<std::string>& targetLanguages, bool redo) {
    using R = Result<TranslationReport>;
    TranslationReport report;
    const std::string source = SourceOf(component);
    if (source.empty()) return R::Failure(ErrorCode::InvalidArgument, component.id + " has no text to translate from");
    if (component.sourceLanguage.empty()) component.sourceLanguage = source;
    const std::vector<std::string> fill = LanguagesToFill(component, targetLanguages, redo);
    for (const std::string& language : targetLanguages) {
        if (language != source && std::find(fill.begin(), fill.end(), language) == fill.end()) report.skipped.push_back(language);
    }
    for (const std::string& language : fill) {
        // Only what is missing (or machine-made, when redoing) in this language.
        std::vector<UltraStore::LocalizedText*> targets;
        std::vector<std::string> inputs;
        for (UltraStore::LocalizedText* t : Texts(component)) {
            const auto src = t->byLanguage.find(source);
            if (src == t->byLanguage.end() || src->second.empty()) continue;
            const bool have = t->byLanguage.count(language) > 0;
            if (have && !(redo && IsMachine(component, language))) continue;
            targets.push_back(t);
            inputs.push_back(src->second);
        }
        if (inputs.empty()) continue;
        const auto translated = translator.Translate(inputs, source, language);
        if (!translated) {
            report.errors.push_back(language + ": " + translated.error.message);
            continue;
        }
        if (translated.Value().size() != inputs.size()) {
            report.errors.push_back(language + ": the translator returned " + std::to_string(translated.Value().size()) +
                                    " texts for " + std::to_string(inputs.size()));
            continue;
        }
        for (size_t i = 0; i < targets.size(); ++i) targets[i]->byLanguage[language] = translated.Value()[i];
        report.texts += inputs.size();
        report.languages.push_back(language);
        if (!IsMachine(component, language)) component.machineTranslated.push_back(language);
    }
    std::sort(component.machineTranslated.begin(), component.machineTranslated.end());
    return R::Succeed(std::move(report));
}

// ===== UltraAI =====
#ifdef ULTRASTOREADMIN_HAVE_ULTRAAI

namespace {

class UltraAITextTranslator final : public ITextTranslator {
public:
    explicit UltraAITextTranslator(std::unique_ptr<UltraAI::ITranslator> inner, std::string model)
        : inner_(std::move(inner)), model_(std::move(model)) {}

    Result<std::vector<std::string>> Translate(const std::vector<std::string>& texts, const std::string& sourceLanguage,
                                               const std::string& targetLanguage) override {
        using R = Result<std::vector<std::string>>;
        UltraAI::TranslateRequest request;
        request.model = model_;
        request.texts = texts;
        request.sourceLanguage = sourceLanguage;
        request.targetLanguage = targetLanguage;
        request.domain = "software";
        request.preserveFormatting = true;
        const UltraAI::TranslateResponse response = inner_->Translate(request);
        if (!response.error) return R::Failure(ErrorCode::NetworkError, response.error.message, response.error.providerCode);
        std::vector<std::string> out;
        for (const UltraAI::TranslateResult& r : response.results) out.push_back(r.text);
        return R::Succeed(std::move(out));
    }

private:
    std::unique_ptr<UltraAI::ITranslator> inner_;
    std::string model_;
};

} // namespace

bool HaveUltraAI() { return true; }

std::unique_ptr<ITextTranslator> CreateUltraAITranslator(const std::string& providerId, const std::string& baseUrl,
                                                         const std::string& model, std::string* error) {
    UltraAI::TranslatorConfig config;
    config.providerId = providerId;
    config.baseUrl = baseUrl;
    config.defaultModel = model;
    UltraAI::Error aiError;
    std::unique_ptr<UltraAI::ITranslator> inner = UltraAI::CreateTranslator(config, &aiError);
    if (!inner) {
        if (error) *error = aiError.message.empty() ? "UltraAI has no translator for \"" + providerId + "\"" : aiError.message;
        return nullptr;
    }
    return std::make_unique<UltraAITextTranslator>(std::move(inner), model);
}

#else

bool HaveUltraAI() { return false; }

std::unique_ptr<ITextTranslator> CreateUltraAITranslator(const std::string&, const std::string&, const std::string&, std::string* error) {
    if (error) *error = "This build has no UltraAI module (BUILD_ULTRAAI_MODULE)";
    return nullptr;
}

#endif

} // namespace UltraStoreAdmin
