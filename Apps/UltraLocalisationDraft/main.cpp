// Apps/UltraLocalisationDraft/main.cpp
// ultralocalisation-draft - drafts a translation catalogue with UltraAI.
//
// Reads an English source catalogue (media/locale/en/Standard.json, a base
// set, an application's set), translates every entry the target catalogue
// lacks through UltraAI's translator - any text-LLM provider the person
// has configured, or a translation provider - and writes the target with
// "@translated-by" marked as a draft until a person reviews it. The
// drafting button of the localisation editor (phase L4 of
// Docs/UltraCanvas/UltraCanvasLocalisationProposal.md) is this same step;
// scripts/translate_catalogue.py runs it over every set of a language.
//
//   ultralocalisation-draft --language de \
//       --source media/locale/en/Standard.json --target media/locale/de/Standard.json \
//       [--provider anthropic] [--model claude-sonnet-5-5] [--api-key-env ANTHROPIC_API_KEY] \
//       [--vault-ref ai.anthropic.api_key] [--batch 40] [--context "a video editor"] \
//       [--redo] [--dry-run]
//
// A context key ("button|Open") sends its text alone; a plural entry sends
// its singular for the "one" form and its plural for every other form the
// language's rule needs, so a reviewer has every form to correct. A
// translation that lost a placeholder the source has is not written: the
// entry stays untranslated and is reported, which the catalogue check
// would otherwise refuse.
//
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#include "UltraLocalisation/UltraLocalisation.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraAITranslator.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

struct Options {
    std::string language, source, target, provider, model, apiKeyEnv, vaultRef, context;
    int batch = 40;
    bool redo = false;
    bool dryRun = false;
};

void Usage() {
    std::fprintf(stderr,
        "usage: ultralocalisation-draft --language <tag> --source <en catalogue> --target <catalogue>\n"
        "       [--provider <id>] [--model <name>] [--api-key-env <VAR>] [--vault-ref <key>]\n"
        "       [--batch <n>] [--context <what the program is>] [--redo] [--dry-run]\n");
}

bool ParseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&](std::string& into) { if (i + 1 >= argc) return false; into = argv[++i]; return true; };
        if (a == "--language") { if (!value(o.language)) return false; }
        else if (a == "--source") { if (!value(o.source)) return false; }
        else if (a == "--target") { if (!value(o.target)) return false; }
        else if (a == "--provider") { if (!value(o.provider)) return false; }
        else if (a == "--model") { if (!value(o.model)) return false; }
        else if (a == "--api-key-env") { if (!value(o.apiKeyEnv)) return false; }
        else if (a == "--vault-ref") { if (!value(o.vaultRef)) return false; }
        else if (a == "--context") { if (!value(o.context)) return false; }
        else if (a == "--batch") { std::string s; if (!value(s)) return false; o.batch = std::atoi(s.c_str()); if (o.batch < 1) o.batch = 1; }
        else if (a == "--redo") o.redo = true;
        else if (a == "--dry-run") o.dryRun = true;
        else return false;
    }
    return !o.language.empty() && !o.source.empty() && !o.target.empty();
}

// The plural categories the language's rule produces, in CLDR order.
std::vector<std::string> CategoriesOf(const std::string& language) {
    std::set<std::string> seen;
    for (long long n = 0; n <= 200; ++n) seen.insert(UltraLocalisation::PluralCategory(language, n));
    seen.insert(UltraLocalisation::PluralCategory(language, 1000000));
    std::vector<std::string> ordered;
    for (const char* c : {"zero", "one", "two", "few", "many", "other"}) if (seen.count(c)) ordered.push_back(c);
    return ordered;
}

std::set<std::string> Placeholders(const std::string& text) {
    std::set<std::string> out;
    for (size_t i = 0; i + 2 < text.size() + 1; ++i) {
        if (text[i] != '{') continue;
        const size_t close = text.find('}', i);
        if (close == std::string::npos) break;
        const std::string inner = text.substr(i + 1, close - i - 1);
        if (inner == "n" || (!inner.empty() && std::all_of(inner.begin(), inner.end(), [](char c) { return c >= '0' && c <= '9'; })))
            out.insert(inner);
    }
    return out;
}

std::string TextOfKey(const std::string& key) {
    const size_t bar = key.find('|');
    return bar == std::string::npos ? key : key.substr(bar + 1);
}

std::string Today() {
    const std::time_t now = std::time(nullptr);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", std::localtime(&now));
    return buf;
}

const std::map<std::string, std::string>& NativeNames() {
    static const std::map<std::string, std::string> names = {
        {"de", "Deutsch"}, {"fr", "Français"}, {"es", "Español"}, {"it", "Italiano"}, {"nl", "Nederlands"},
        {"pt", "Português"}, {"pt-BR", "Português (Brasil)"}, {"pt-PT", "Português (Portugal)"}, {"ja", "日本語"},
        {"zh-Hans", "简体中文"}, {"zh-Hant", "繁體中文"}, {"ko", "한국어"}, {"ru", "Русский"}, {"uk", "Українська"},
        {"pl", "Polski"}, {"cs", "Čeština"}, {"sk", "Slovenčina"}, {"hu", "Magyar"}, {"ro", "Română"},
        {"bg", "Български"}, {"el", "Ελληνικά"}, {"tr", "Türkçe"}, {"sv", "Svenska"}, {"da", "Dansk"},
        {"nb", "Norsk bokmål"}, {"fi", "Suomi"}, {"et", "Eesti"}, {"lv", "Latviešu"}, {"lt", "Lietuvių"},
        {"hr", "Hrvatski"}, {"sr", "Српски"}, {"sl", "Slovenščina"}, {"ar", "العربية"}, {"he", "עברית"},
        {"hi", "हिन्दी"}, {"th", "ไทย"}, {"vi", "Tiếng Việt"}, {"id", "Bahasa Indonesia"}, {"ms", "Bahasa Melayu"},
        {"ca", "Català"}, {"eu", "Euskara"}, {"gl", "Galego"}, {"ga", "Gaeilge"}, {"cy", "Cymraeg"}, {"en", "English"}};
    return names;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!ParseArgs(argc, argv, o)) { Usage(); return 2; }

    LocalisationCatalogue source, target;
    std::string error;
    if (!UltraLocalisation::ReadCatalogue(o.source, source, error)) {
        std::fprintf(stderr, "ultralocalisation-draft: cannot read %s: %s\n", o.source.c_str(), error.c_str());
        return 1;
    }
    if (std::filesystem::exists(PathFromUtf8(o.target)) && !UltraLocalisation::ReadCatalogue(o.target, target, error)) {
        std::fprintf(stderr, "ultralocalisation-draft: cannot read %s: %s\n", o.target.c_str(), error.c_str());
        return 1;
    }
    target.language = o.language;
    if (target.nativeName.empty()) {
        auto it = NativeNames().find(o.language);
        if (it != NativeNames().end() && PathFromUtf8(o.source).stem() == "Standard") target.nativeName = it->second;
    }
    if (target.sourceLanguage.empty() && !source.sourceLanguage.empty()) target.sourceLanguage = source.sourceLanguage;
    target.bases = source.bases;

    // What to send: one line per text, with where it goes back to
    struct Slot { std::string key; std::string category; std::string text; };
    std::vector<Slot> slots;
    for (const auto& [key, text] : source.entries) {
        if (!o.redo && target.entries.count(key)) continue;
        slots.push_back({key, "", TextOfKey(text)});
    }
    const std::vector<std::string> categories = CategoriesOf(o.language);
    for (const auto& [key, forms] : source.plurals) {
        if (!o.redo && target.plurals.count(key)) continue;
        const std::string one = forms.count("one") ? forms.at("one") : key;
        const std::string other = forms.count("other") ? forms.at("other") : one;
        for (const std::string& category : categories)
            slots.push_back({key, category, category == "one" ? one : other});
    }
    if (slots.empty()) {
        std::printf("ultralocalisation-draft: %s has every entry of %s\n", o.target.c_str(), o.source.c_str());
        return 0;
    }
    if (o.dryRun) {
        for (const Slot& s : slots) std::printf("%s%s%s\t%s\n", s.key.c_str(), s.category.empty() ? "" : " [", s.category.c_str(), s.text.c_str());
        std::printf("ultralocalisation-draft: %zu text(s) would be sent for %s\n", slots.size(), o.language.c_str());
        return 0;
    }

    UltraAI::TranslatorConfig cfg;
    cfg.providerId = o.provider;
    cfg.defaultModel = o.model;
    cfg.apiKeyVaultRef = o.vaultRef;
    if (!o.apiKeyEnv.empty()) cfg.apiKey = GetEnvUtf8(o.apiKeyEnv.c_str());
    UltraAI::Error createError;
    std::unique_ptr<UltraAI::ITranslator> translator = UltraAI::CreateTranslator(cfg, &createError);
    if (!translator) {
        std::fprintf(stderr, "ultralocalisation-draft: no translator: %s\n", createError.message.c_str());
        return 1;
    }
    const std::string provider = translator->GetCapabilities().providerId;

    size_t written = 0, dropped = 0;
    for (size_t start = 0; start < slots.size(); start += static_cast<size_t>(o.batch)) {
        const size_t end = std::min(slots.size(), start + static_cast<size_t>(o.batch));
        UltraAI::TranslateRequest request;
        request.model = o.model;
        request.sourceLanguage = source.sourceLanguage.empty() ? "en" : source.sourceLanguage;
        request.targetLanguage = o.language;
        request.domain = o.context.empty() ? "the user interface of a desktop application: menus, buttons, dialogs"
                                           : "the user interface of " + o.context + ": menus, buttons, dialogs";
        request.preserveFormatting = true;
        for (size_t i = start; i < end; ++i) request.texts.push_back(slots[i].text);
        const UltraAI::TranslateResponse response = translator->Translate(request);
        if (!response.error.message.empty() || response.results.size() != request.texts.size()) {
            std::fprintf(stderr, "ultralocalisation-draft: batch %zu-%zu failed: %s\n", start, end,
                         response.error.message.empty() ? "wrong number of results" : response.error.message.c_str());
            return 1;
        }
        for (size_t i = start; i < end; ++i) {
            const Slot& s = slots[i];
            const std::string& out = response.results[i - start].text;
            if (out.empty() || Placeholders(s.text) != Placeholders(out)) {
                std::fprintf(stderr, "ultralocalisation-draft: %s%s%s: the draft lost a placeholder or came back empty, left untranslated\n",
                             s.key.c_str(), s.category.empty() ? "" : " [", s.category.c_str());
                ++dropped;
                continue;
            }
            if (s.category.empty()) target.entries[s.key] = out;
            else target.plurals[s.key][s.category] = out;
            ++written;
        }
        std::fprintf(stderr, "ultralocalisation-draft: %zu of %zu\n", end, slots.size());
    }
    target.translatedBy = "UltraAI draft " + Today() + " (" + provider + (o.model.empty() ? "" : "/" + o.model) + "), reviewed by -";

    std::error_code ec;
    std::filesystem::create_directories(PathFromUtf8(o.target).parent_path(), ec);
    std::ofstream out(PathFromUtf8(o.target), std::ios::binary | std::ios::trunc);
    if (!out) { std::fprintf(stderr, "ultralocalisation-draft: cannot write %s\n", o.target.c_str()); return 1; }
    out << target.Serialize();
    std::printf("ultralocalisation-draft: %zu draft(s) written to %s, %zu left untranslated\n", written, o.target.c_str(), dropped);
    return 0;
}
