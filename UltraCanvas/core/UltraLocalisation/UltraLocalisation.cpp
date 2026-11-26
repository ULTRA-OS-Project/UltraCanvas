// core/UltraLocalisation/UltraLocalisation.cpp
// See UltraLocalisation/UltraLocalisation.h. The catalogue files are read
// with the vendored yyjson engine directly, as UltraWin's QMP client does,
// so the archive stands without the core; the format is the one
// UltraCanvasLocalisationProposal.md §4 shows.
// Version: 1.2.0 - phase L6: numbers, dates and times in the person's conventions; TrF's numbers in the language's digits and decimal separator, TrN's count grouped
// Version: 1.1.0 - the plural rules of Breton, Scottish Gaelic, Manx, Cornish, Tachelhit, Tamazight, Filipino, Colognian, the Sami languages, Hebrew's many, pt-PT
// Version: 1.0.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework
#include "UltraLocalisation/UltraLocalisation.h"

#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8 / GetEnvUtf8 / OpenFileUtf8

#include <yyjson.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <locale>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <unordered_map>

namespace UltraCanvas {

namespace {

namespace fs = std::filesystem;

// ===== TAGS =====

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "de_AT" and "de-at" -> "de-AT"; a four-letter script in title case.
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
        std::string p = Lower(parts[i]);
        if (p.empty()) continue;
        if (i > 0 && p.size() == 4 && std::isalpha(static_cast<unsigned char>(p[0])))
            p[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(p[0])));
        else if (i > 0 && (p.size() == 2 || (p.size() == 3 && std::isdigit(static_cast<unsigned char>(p[0])))))
            for (char& c : p) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (!out.empty()) out += '-';
        out += p;
    }
    return out;
}

// "zh-Hant-TW" -> {"zh-Hant-TW", "zh-Hant", "zh"}
std::vector<std::string> FallbackWalk(const std::string& tag) {
    std::vector<std::string> walk;
    std::string t = CanonicalTag(tag);
    while (!t.empty()) {
        walk.push_back(t);
        const size_t dash = t.rfind('-');
        if (dash == std::string::npos) break;
        t.erase(dash);
    }
    return walk;
}

std::string TrimSlash(std::string s) {
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}

std::string ReadWholeFile(const fs::path& path, bool& found) {
    found = false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    found = true;
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// ===== THE STATE =====

struct Layer {
    LocalisationOrigin origin = LocalisationOrigin::Source;
    std::string name;                                                        // "UltraMail", "Video", "Standard"
    std::unordered_map<std::string, std::string> entries;
    std::unordered_map<std::string, std::map<std::string, std::string>> plurals;
};

struct State {
    std::shared_mutex mutex;
    std::string shipped;          // media/locale, no trailing separator
    std::string settings;         // the UltraCanvas settings folder
    std::string languageOverride;
    std::string language = "en";
    std::string application;
    std::vector<std::string> basesSet;        // UseBaseSets
    bool basesGiven = false;
    std::vector<std::string> libraries;       // AddSet
    std::vector<Layer> layers;                // lookup order
    LocaleFormats regional;                   // SetRegionalFormats: the platform's
    bool regionalGiven = false;
    std::string formatsTag = "en";            // the language's regional tag: "en-GB" while the catalogue is "en"
    bool languageFromDesktop = false;         // chosen from the desktop's languages, not by --lang or a setting
    LocaleFormats formats;                    // what the Format... calls use, worked out when any of the above changes
    struct Handler { int id; UltraLocalisation::ChangeHandler fn; };
    std::vector<Handler> handlers;
    int nextHandler = 1;
};

State& S() {
    static State state;
    return state;
}

// Every file of a set for a language, most specific first, the settings
// folder before the shipped tree for each tag.
std::vector<std::string> SetFiles(const State& st, const std::string& language, const std::string& set, bool base) {
    std::vector<std::string> files;
    for (const std::string& tag : FallbackWalk(language)) {
        for (const std::string& root : {st.settings.empty() ? std::string() : st.settings + "/locale", st.shipped}) {
            if (root.empty()) continue;
            std::string path = root + "/" + tag + "/";
            if (base) path += "base/";
            path += set + ".json";
            files.push_back(path);
        }
    }
    return files;
}

// Adds a catalogue's entries to a layer where the layer has none yet, so
// the more specific file, loaded first, wins.
void Merge(Layer& layer, const LocalisationCatalogue& catalogue) {
    for (const auto& [key, text] : catalogue.entries) layer.entries.emplace(key, text);
    for (const auto& [key, forms] : catalogue.plurals) layer.plurals.emplace(key, forms);
}

bool LoadLayer(const State& st, const std::string& language, const std::string& set, bool base,
               LocalisationOrigin origin, Layer& layer, std::vector<std::string>* basesOut) {
    layer.origin = origin;
    layer.name = set;
    bool any = false;
    for (const std::string& path : SetFiles(st, language, set, base)) {
        LocalisationCatalogue catalogue;
        std::string error;
        if (!UltraLocalisation::ReadCatalogue(path, catalogue, error)) continue;
        any = true;
        Merge(layer, catalogue);
        if (basesOut && basesOut->empty() && !catalogue.bases.empty()) *basesOut = catalogue.bases;
    }
    return any;
}

// The "@bases" of the application's catalogue, from the source catalogue
// (en) when the language's own has none.
std::vector<std::string> BasesFromCatalogue(const State& st, const std::string& language, const std::string& application) {
    std::vector<std::string> bases;
    for (const std::string& lang : {language, std::string("en")}) {
        for (const std::string& path : SetFiles(st, lang, application, false)) {
            LocalisationCatalogue catalogue;
            std::string error;
            if (UltraLocalisation::ReadCatalogue(path, catalogue, error) && !catalogue.bases.empty()) return catalogue.bases;
        }
    }
    return bases;
}

const Layer* FindLayer(const State& st, std::string_view key, const std::string** text) {
    for (const Layer& layer : st.layers) {
        auto it = layer.entries.find(std::string(key));
        if (it != layer.entries.end()) { if (text) *text = &it->second; return &layer; }
    }
    return nullptr;
}

std::string ReplaceAll(std::string text, std::string_view what, std::string_view with) {
    size_t pos = 0;
    while ((pos = text.find(what, pos)) != std::string::npos) {
        text.replace(pos, what.size(), with);
        pos += with.size();
    }
    return text;
}

// ===== JSON =====

std::string JsonString(yyjson_val* v) {
    return yyjson_is_str(v) ? std::string(yyjson_get_str(v), yyjson_get_len(v)) : std::string();
}

void JsonEscape(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
                else out += static_cast<char>(c);
        }
    }
    out += '"';
}

}  // namespace

// ===== THE CATALOGUE =====

bool LocalisationCatalogue::Parse(const std::string& text, LocalisationCatalogue& out, std::string& error) {
    yyjson_read_err err;
    yyjson_doc* doc = yyjson_read_opts(const_cast<char*>(text.data()), text.size(), 0, nullptr, &err);
    if (!doc) { error = err.msg ? err.msg : "not JSON"; return false; }
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) { yyjson_doc_free(doc); error = "a catalogue is a JSON object"; return false; }
    LocalisationCatalogue result;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(root, &iter);
    yyjson_val* keyVal;
    while ((keyVal = yyjson_obj_iter_next(&iter))) {
        const std::string key = JsonString(keyVal);
        yyjson_val* val = yyjson_obj_iter_get_val(keyVal);
        if (!key.empty() && key[0] == '@') {
            if (key == "@language") result.language = JsonString(val);
            else if (key == "@name") result.nativeName = JsonString(val);
            else if (key == "@source-language") result.sourceLanguage = JsonString(val);
            else if (key == "@translated-by") result.translatedBy = JsonString(val);
            else if ((key == "@bases" || key == "@inherited") && yyjson_is_arr(val)) {
                std::vector<std::string>& list = key == "@bases" ? result.bases : result.inherited;
                yyjson_arr_iter ai;
                yyjson_arr_iter_init(val, &ai);
                yyjson_val* item;
                while ((item = yyjson_arr_iter_next(&ai))) {
                    const std::string s = JsonString(item);
                    if (!s.empty()) list.push_back(s);
                }
            }
            continue;
        }
        if (yyjson_is_str(val)) {
            result.entries[key] = JsonString(val);
        } else if (yyjson_is_obj(val)) {
            std::map<std::string, std::string> forms;
            yyjson_obj_iter fi;
            yyjson_obj_iter_init(val, &fi);
            yyjson_val* formKey;
            while ((formKey = yyjson_obj_iter_next(&fi))) {
                yyjson_val* formVal = yyjson_obj_iter_get_val(formKey);
                if (yyjson_is_str(formVal)) forms[JsonString(formKey)] = JsonString(formVal);
            }
            if (!forms.empty()) result.plurals[key] = std::move(forms);
        }
    }
    yyjson_doc_free(doc);
    out = std::move(result);
    error.clear();
    return true;
}

std::string LocalisationCatalogue::Serialize() const {
    std::string out = "{\n";
    bool first = true;
    auto line = [&](const std::string& key, const std::string& valueJson) {
        if (!first) out += ",\n";
        first = false;
        out += "  ";
        JsonEscape(out, key);
        out += ": ";
        out += valueJson;
    };
    auto quoted = [](const std::string& s) { std::string q; JsonEscape(q, s); return q; };
    auto list = [&](const std::vector<std::string>& items) {
        std::string a = "[";
        for (size_t i = 0; i < items.size(); ++i) { if (i) a += ", "; a += quoted(items[i]); }
        return a + "]";
    };
    if (!language.empty()) line("@language", quoted(language));
    if (!nativeName.empty()) line("@name", quoted(nativeName));
    if (!sourceLanguage.empty()) line("@source-language", quoted(sourceLanguage));
    if (!translatedBy.empty()) line("@translated-by", quoted(translatedBy));
    if (!bases.empty()) line("@bases", list(bases));
    if (!inherited.empty()) line("@inherited", list(inherited));
    // Entries and plurals in one key order
    std::map<std::string, int> keys;
    for (const auto& e : entries) keys[e.first] = 0;
    for (const auto& p : plurals) keys[p.first] = 1;
    for (const auto& [key, kind] : keys) {
        if (kind == 0) {
            line(key, quoted(entries.at(key)));
        } else {
            std::string obj = "{ ";
            bool firstForm = true;
            for (const auto& [category, form] : plurals.at(key)) {
                if (!firstForm) obj += ", ";
                firstForm = false;
                obj += quoted(category) + ": " + quoted(form);
            }
            line(key, obj + " }");
        }
    }
    out += "\n}\n";
    return out;
}

// ===== LANGUAGE.CONF =====

LanguageSettings LanguageSettings::Parse(const std::string& text) {
    LanguageSettings s;
    std::string line;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size() && text[i] != '\n') { line += text[i]; continue; }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq != std::string::npos) {
            const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
            if (key == "language" && !value.empty()) s.language = value == "system" ? value : CanonicalTag(value);
            else if (key == "translator") s.translator = value;
        }
        line.clear();
    }
    return s;
}

std::string LanguageSettings::Serialize() const {
    std::string out = "language=" + (language.empty() ? std::string("system") : language) + "\n";
    if (!translator.empty()) out += "translator=" + translator + "\n";
    return out;
}

std::string LanguageSettings::FilePath(const std::string& settingsFolder) {
    if (settingsFolder.empty()) return {};
    return TrimSlash(settingsFolder) + "/Language.conf";
}

LanguageSettings LanguageSettings::Load(const std::string& settingsFolder) {
    const std::string path = FilePath(settingsFolder);
    if (path.empty()) return {};
    bool found = false;
    const std::string text = ReadWholeFile(PathFromUtf8(path), found);
    return found ? Parse(text) : LanguageSettings{};
}

bool LanguageSettings::Save(const std::string& settingsFolder) const {
    const std::string path = FilePath(settingsFolder);
    if (path.empty()) return false;
    std::error_code ec;
    fs::create_directories(PathFromUtf8(settingsFolder), ec);
    const std::string temp = path + ".tmp";
    {
        std::ofstream out(PathFromUtf8(temp), std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << Serialize();
        if (!out) return false;
    }
    fs::rename(PathFromUtf8(temp), PathFromUtf8(path), ec);
    if (ec) { fs::remove(PathFromUtf8(temp), ec); return false; }
    return true;
}

// ===== THE MODULE =====

void UltraLocalisation::SetFolders(const std::string& shippedLocaleDir, const std::string& settingsFolder) {
    State& st = S();
    std::unique_lock lock(st.mutex);
    st.shipped = TrimSlash(shippedLocaleDir);
    st.settings = TrimSlash(settingsFolder);
}

std::string UltraLocalisation::ShippedFolder() { State& st = S(); std::shared_lock lock(st.mutex); return st.shipped; }
std::string UltraLocalisation::SettingsFolder() { State& st = S(); std::shared_lock lock(st.mutex); return st.settings; }

void UltraLocalisation::SetLanguageOverride(const std::string& language) {
    State& st = S();
    std::unique_lock lock(st.mutex);
    st.languageOverride = language.empty() ? std::string() : CanonicalTag(language);
}

std::vector<std::string> UltraLocalisation::AvailableLanguages() {
    State& st = S();
    std::vector<std::string> roots;
    {
        std::shared_lock lock(st.mutex);
        if (!st.settings.empty()) roots.push_back(st.settings + "/locale");
        if (!st.shipped.empty()) roots.push_back(st.shipped);
    }
    std::vector<std::string> languages = {"en"};
    for (const std::string& root : roots) {
        std::error_code ec;
        for (const fs::directory_entry& entry : fs::directory_iterator(PathFromUtf8(root), ec)) {
            if (!entry.is_directory(ec)) continue;
            bool any = false;
            for (const fs::directory_entry& file : fs::recursive_directory_iterator(entry.path(), ec)) {
                if (file.is_regular_file(ec) && file.path().extension() == ".json") { any = true; break; }
            }
            if (!any) continue;
            const std::string tag = CanonicalTag(PathToUtf8(entry.path().filename()));
            if (!tag.empty() && std::find(languages.begin(), languages.end(), tag) == languages.end()) languages.push_back(tag);
        }
    }
    std::sort(languages.begin(), languages.end());
    return languages;
}

std::string UltraLocalisation::NativeName(const std::string& language) {
    State& st = S();
    std::vector<std::string> files;
    {
        std::shared_lock lock(st.mutex);
        files = SetFiles(st, language, "Standard", false);
    }
    for (const std::string& path : files) {
        LocalisationCatalogue catalogue;
        std::string error;
        if (ReadCatalogue(path, catalogue, error) && !catalogue.nativeName.empty()) return catalogue.nativeName;
    }
    return CanonicalTag(language);
}

namespace {

// ChooseLanguage, with where the answer came from: `formatsTag` is the tag
// the numbers and dates are written for (the desktop's "en-GB" that found
// the "en" catalogue), `fromDesktop` whether the person chose nothing here.
std::string ChooseLanguageFrom(const std::vector<std::string>& systemLanguages, std::string& formatsTag, bool& fromDesktop) {
    State& st = S();
    std::string override, settings;
    {
        std::shared_lock lock(st.mutex);
        override = st.languageOverride;
        settings = st.settings;
    }
    fromDesktop = false;
    if (!override.empty()) { formatsTag = override; return override; }
    const std::string env = GetEnvUtf8("ULTRACANVAS_LANG");
    if (!env.empty()) { formatsTag = CanonicalTag(env); return formatsTag; }
    const LanguageSettings conf = LanguageSettings::Load(settings);
    if (!conf.language.empty() && conf.language != "system") { formatsTag = conf.language; return conf.language; }
    fromDesktop = true;
    const std::vector<std::string> available = UltraLocalisation::AvailableLanguages();
    for (const std::string& wanted : systemLanguages) {
        for (const std::string& tag : FallbackWalk(wanted)) {
            if (std::find(available.begin(), available.end(), tag) != available.end()) {
                formatsTag = CanonicalTag(wanted);
                return tag;
            }
        }
    }
    formatsTag = systemLanguages.empty() ? std::string("en") : CanonicalTag(systemLanguages.front());
    return "en";
}

std::string BaseOf(const std::string& tag) {
    const std::vector<std::string> walk = FallbackWalk(tag);
    return Lower(walk.empty() ? std::string() : walk.back());
}

// The formats the calls use, from the state; the caller holds the lock.
void UpdateFormats(State& st) {
    if (st.regionalGiven && !st.regional.language.empty() &&
        (st.languageFromDesktop || BaseOf(st.regional.language) == BaseOf(st.language))) {
        st.formats = st.regional;
        return;
    }
    st.formats = UltraLocalisation::FormatsFor(st.formatsTag.empty() ? st.language : st.formatsTag);
}

}  // namespace

std::string UltraLocalisation::ChooseLanguage(const std::vector<std::string>& systemLanguages) {
    std::string formatsTag;
    bool fromDesktop = false;
    return ChooseLanguageFrom(systemLanguages, formatsTag, fromDesktop);
}

bool UltraLocalisation::Initialize(const std::string& application, const std::vector<std::string>& systemLanguages) {
    std::string formatsTag;
    bool fromDesktop = false;
    const std::string language = ChooseLanguageFrom(systemLanguages, formatsTag, fromDesktop);
    const bool ok = Load(language, application);
    State& st = S();
    std::unique_lock lock(st.mutex);
    st.formatsTag = formatsTag;
    st.languageFromDesktop = fromDesktop;
    UpdateFormats(st);
    return ok;
}

bool UltraLocalisation::Load(const std::string& language, const std::string& application) {
    State& st = S();
    std::unique_lock lock(st.mutex);
    const std::string tag = CanonicalTag(language.empty() ? std::string("en") : language);
    st.language = tag;
    st.application = application;
    st.layers.clear();
    st.formatsTag = tag;
    st.languageFromDesktop = false;
    UpdateFormats(st);
    const bool english = Lower(tag) == "en";
    bool any = false;
    std::vector<std::string> bases = st.basesGiven ? st.basesSet : BasesFromCatalogue(st, tag, application);
    if (english) {
        // The source is the text; only the bases are worth knowing
        st.basesSet = bases;
        return true;
    }
    if (!application.empty()) {
        Layer layer;
        if (LoadLayer(st, tag, application, false, LocalisationOrigin::Application, layer, nullptr)) any = true;
        st.layers.push_back(std::move(layer));
    }
    for (const std::string& library : st.libraries) {
        Layer layer;
        if (LoadLayer(st, tag, library, false, LocalisationOrigin::Library, layer, nullptr)) any = true;
        st.layers.push_back(std::move(layer));
    }
    for (const std::string& base : bases) {
        Layer layer;
        if (LoadLayer(st, tag, base, true, LocalisationOrigin::Base, layer, nullptr)) any = true;
        st.layers.push_back(std::move(layer));
    }
    {
        Layer layer;
        if (LoadLayer(st, tag, "Standard", false, LocalisationOrigin::Standard, layer, nullptr)) any = true;
        st.layers.push_back(std::move(layer));
    }
    if (!st.basesGiven) st.basesSet = bases;
    return any;
}

bool UltraLocalisation::SetLanguage(const std::string& language) {
    State& st = S();
    std::string application;
    { std::shared_lock lock(st.mutex); application = st.application; }
    const bool ok = Load(language, application);
    std::vector<State::Handler> handlers;
    std::string current;
    { std::shared_lock lock(st.mutex); handlers = st.handlers; current = st.language; }
    for (const State::Handler& h : handlers) if (h.fn) h.fn(current);
    return ok;
}

std::string UltraLocalisation::CurrentLanguage() { State& st = S(); std::shared_lock lock(st.mutex); return st.language; }
std::string UltraLocalisation::CurrentApplication() { State& st = S(); std::shared_lock lock(st.mutex); return st.application; }

int UltraLocalisation::AddChangeHandler(ChangeHandler handler) {
    State& st = S();
    std::unique_lock lock(st.mutex);
    const int id = st.nextHandler++;
    st.handlers.push_back({id, std::move(handler)});
    return id;
}

void UltraLocalisation::RemoveChangeHandler(int id) {
    State& st = S();
    std::unique_lock lock(st.mutex);
    for (auto it = st.handlers.begin(); it != st.handlers.end(); ++it) {
        if (it->id == id) { st.handlers.erase(it); return; }
    }
}

void UltraLocalisation::UseBaseSets(const std::vector<std::string>& bases) {
    State& st = S();
    std::unique_lock lock(st.mutex);
    st.basesSet = bases;
    st.basesGiven = true;
}

std::vector<std::string> UltraLocalisation::BaseSets() { State& st = S(); std::shared_lock lock(st.mutex); return st.basesSet; }

void UltraLocalisation::AddSet(const std::string& name) {
    State& st = S();
    std::unique_lock lock(st.mutex);
    if (!name.empty() && std::find(st.libraries.begin(), st.libraries.end(), name) == st.libraries.end())
        st.libraries.push_back(name);
}

std::vector<std::string> UltraLocalisation::LibrarySets() { State& st = S(); std::shared_lock lock(st.mutex); return st.libraries; }

LocalisationOrigin UltraLocalisation::OriginOf(std::string_view source) {
    State& st = S();
    std::shared_lock lock(st.mutex);
    const Layer* layer = FindLayer(st, source, nullptr);
    if (!layer) {
        for (const Layer& l : st.layers) if (l.plurals.count(std::string(source))) return l.origin;
        return LocalisationOrigin::Source;
    }
    return layer->origin;
}

std::vector<std::string> UltraLocalisation::StandardKeys() {
    State& st = S();
    std::shared_lock lock(st.mutex);
    std::vector<std::string> keys;
    for (const Layer& layer : st.layers) {
        if (layer.origin != LocalisationOrigin::Standard) continue;
        for (const auto& e : layer.entries) keys.push_back(e.first);
        for (const auto& p : layer.plurals) keys.push_back(p.first);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

size_t UltraLocalisation::EntryCount(LocalisationOrigin origin) {
    State& st = S();
    std::shared_lock lock(st.mutex);
    size_t count = 0;
    for (const Layer& layer : st.layers) if (layer.origin == origin) count += layer.entries.size() + layer.plurals.size();
    return count;
}

bool UltraLocalisation::ReadCatalogue(const std::string& path, LocalisationCatalogue& out, std::string& error) {
    bool found = false;
    const std::string text = ReadWholeFile(PathFromUtf8(path), found);
    if (!found) { error = "not found"; return false; }
    return LocalisationCatalogue::Parse(text, out, error);
}

void UltraLocalisation::Reset() {
    State& st = S();
    std::unique_lock lock(st.mutex);
    st.shipped.clear();
    st.settings.clear();
    st.languageOverride.clear();
    st.language = "en";
    st.application.clear();
    st.basesSet.clear();
    st.basesGiven = false;
    st.libraries.clear();
    st.layers.clear();
    st.handlers.clear();
    st.regional = LocaleFormats{};
    st.regionalGiven = false;
    st.formatsTag = "en";
    st.languageFromDesktop = false;
    st.formats = LocaleFormats{};
}

// ===== PLURALS =====

std::string UltraLocalisation::PluralCategory(const std::string& language, long long count) {
    // The CLDR plural rules for integers (cardinal), by base language, with
    // the two regional exceptions that have a rule of their own.
    const std::vector<std::string> walk = FallbackWalk(language);
    const std::string base = Lower(walk.empty() ? std::string("en") : walk.back());
    const std::string full = Lower(walk.empty() ? std::string("en") : walk.front());
    const long long n = count < 0 ? -count : count;
    const long long m10 = n % 10, m100 = n % 100, m1000 = n % 1000, m100000 = n % 100000, m1000000 = n % 1000000;
    auto in = [](long long v, long long lo, long long hi) { return v >= lo && v <= hi; };
    auto oneOf = [&](std::initializer_list<const char*> langs) {
        for (const char* l : langs) if (base == l) return true;
        return false;
    };
    // One form: the East and South-East Asian languages and a few more
    if (oneOf({"ja", "zh", "yue", "ko", "th", "vi", "id", "ms", "my", "km", "lo", "jv", "ig", "yo", "bo", "dz", "ii",
               "kde", "kea", "ses", "sg", "to", "wo", "jbo", "nqo", "sah", "su", "osa", "hnj", "tpi"})) return "other";
    // 0 and 1 are singular
    if (oneOf({"fr", "hy", "kab", "ff", "pt"}) && full != "pt-pt") return n <= 1 ? "one" : "other";
    if (oneOf({"hi", "bn", "gu", "fa", "mr", "kn", "zu", "am", "as", "si", "ln", "mg", "nso", "ti", "wa", "ak", "pa", "pcm", "doi"}))
        return n <= 1 ? "one" : "other";
    // Filipino: one unless the last digit is 4, 6 or 9
    if (oneOf({"fil", "tl"})) return (m10 != 4 && m10 != 6 && m10 != 9) ? "one" : "other";
    // Tachelhit: 0 and 1 singular, 2 to 10 few
    if (oneOf({"shi"})) return n <= 1 ? "one" : in(n, 2, 10) ? "few" : "other";
    // Central Atlas Tamazight: 0, 1 and 11 to 99 singular
    if (oneOf({"tzm"})) return (n <= 1 || in(n, 11, 99)) ? "one" : "other";
    // Colognian and Langi: a zero form
    if (oneOf({"ksh", "lag"})) return n == 0 ? "zero" : n == 1 ? "one" : "other";
    // One, two, other: the Sami languages, Inuktitut, Nama, Northern Sami
    if (oneOf({"iu", "naq", "se", "sma", "smi", "smj", "smn", "sms"})) return n == 1 ? "one" : n == 2 ? "two" : "other";
    // Scottish Gaelic: 1 and 11, 2 and 12, 3 to 10 and 13 to 19
    if (oneOf({"gd"})) {
        if (n == 1 || n == 11) return "one";
        if (n == 2 || n == 12) return "two";
        if (in(n, 3, 10) || in(n, 13, 19)) return "few";
        return "other";
    }
    // Breton: by the last digit, with the teens and the seventies and nineties excepted; many for the millions
    if (oneOf({"br"})) {
        if (m10 == 1 && m100 != 11 && m100 != 71 && m100 != 91) return "one";
        if (m10 == 2 && m100 != 12 && m100 != 72 && m100 != 92) return "two";
        if ((in(m10, 3, 4) || m10 == 9) && !in(m100, 10, 19) && !in(m100, 70, 79) && !in(m100, 90, 99)) return "few";
        if (n != 0 && m1000000 == 0) return "many";
        return "other";
    }
    // Manx: by the last digit, few for the round twenties
    if (oneOf({"gv"})) {
        if (m10 == 1) return "one";
        if (m10 == 2) return "two";
        if (m100 == 0 || m100 == 20 || m100 == 40 || m100 == 60 || m100 == 80) return "few";
        return "other";
    }
    // Cornish: the full CLDR rule
    if (oneOf({"kw"})) {
        if (n == 0) return "zero";
        if (n == 1) return "one";
        if (m100 == 2 || m100 == 22 || m100 == 42 || m100 == 62 || m100 == 82) return "two";
        if (m1000 == 0 && (in(m100000, 1000, 20000) || m100000 == 40000 || m100000 == 60000 || m100000 == 80000)) return "two";
        if (n != 0 && m1000000 == 100000) return "two";
        if (m100 == 3 || m100 == 23 || m100 == 43 || m100 == 63 || m100 == 83) return "few";
        if (n != 1 && (m100 == 1 || m100 == 21 || m100 == 41 || m100 == 61 || m100 == 81)) return "many";
        return "other";
    }
    if (oneOf({"ru", "uk", "be"})) {
        if (m10 == 1 && m100 != 11) return "one";
        if (in(m10, 2, 4) && !in(m100, 12, 14)) return "few";
        return "many";
    }
    if (oneOf({"pl"})) {
        if (n == 1) return "one";
        if (in(m10, 2, 4) && !in(m100, 12, 14)) return "few";
        return "many";
    }
    if (oneOf({"cs", "sk"})) return n == 1 ? "one" : in(n, 2, 4) ? "few" : "other";
    if (oneOf({"ro"})) return n == 1 ? "one" : (n == 0 || in(m100, 2, 19)) ? "few" : "other";
    if (oneOf({"lt"})) {
        if (m10 == 1 && !in(m100, 11, 19)) return "one";
        if (in(m10, 2, 9) && !in(m100, 11, 19)) return "few";
        return "other";
    }
    if (oneOf({"lv"})) {
        if (m10 == 0 || in(m100, 11, 19)) return "zero";
        if (m10 == 1 && m100 != 11) return "one";
        return "other";
    }
    if (oneOf({"hr", "sr", "bs", "sh"})) {
        if (m10 == 1 && m100 != 11) return "one";
        if (in(m10, 2, 4) && !in(m100, 12, 14)) return "few";
        return "other";
    }
    if (oneOf({"sl"})) return m100 == 1 ? "one" : m100 == 2 ? "two" : in(m100, 3, 4) ? "few" : "other";
    if (oneOf({"ga"})) return n == 1 ? "one" : n == 2 ? "two" : in(n, 3, 6) ? "few" : in(n, 7, 10) ? "many" : "other";
    if (oneOf({"ar"})) {
        if (n == 0) return "zero";
        if (n == 1) return "one";
        if (n == 2) return "two";
        if (in(m100, 3, 10)) return "few";
        if (in(m100, 11, 99)) return "many";
        return "other";
    }
    if (oneOf({"he", "iw"})) return n == 1 ? "one" : n == 2 ? "two" : (m10 == 0 && n > 10) ? "many" : "other";
    if (oneOf({"cy"})) return n == 0 ? "zero" : n == 1 ? "one" : n == 2 ? "two" : n == 3 ? "few" : n == 6 ? "many" : "other";
    if (oneOf({"mt"})) return n == 1 ? "one" : (n == 0 || in(m100, 2, 10)) ? "few" : in(m100, 11, 19) ? "many" : "other";
    if (oneOf({"is", "mk"})) return (m10 == 1 && m100 != 11) ? "one" : "other";
    // Everything else - en, de, nl, sv, da, nb, nn, fi, et, it, es, ca, el,
    // hu, tr, bg, sq, eu, gl, ka, sw, ur, pt-PT and the rest of the
    // one-for-1 languages, which are most of them: one for 1
    return n == 1 ? "one" : "other";
}

// ===== NUMBERS, DATES AND TIMES =====

namespace {

// The separators CLDR writes, spelled as bytes so the table reads the same
// whatever the compiler's source charset.
#define UC_NBSP "\xC2\xA0"          // U+00A0 no-break space
#define UC_NNBSP "\xE2\x80\xAF"     // U+202F narrow no-break space
#define UC_RSQUO "\xE2\x80\x99"     // U+2019, the Swiss group separator
#define UC_RLM "\xE2\x80\x8F"       // U+200F right-to-left mark

const char* const kArabicIndicDigits = "\xD9\xA0\xD9\xA1\xD9\xA2\xD9\xA3\xD9\xA4\xD9\xA5\xD9\xA6\xD9\xA7\xD9\xA8\xD9\xA9";

// One region's conventions, from CLDR: the decimal and group separators,
// the group sizes and the fewest digits worth grouping, the percent
// pattern, the three date patterns, the two time patterns, how a date and a
// time are joined, the digits.
struct FormatRow {
    const char* tag;
    const char* decimal;
    const char* group;
    int groupSize, secondaryGroupSize, minimumGroupingDigits;
    const char* percent;
    const char* shortDate;
    const char* longDate;
    const char* fullDate;
    const char* shortTime;
    const char* mediumTime;
    const char* join;
    const char* digits;
};

const FormatRow kFormatRows[] = {
    {"en", ".", ",", 3, 3, 1, "{0}%", "M/d/y", "MMMM d, y", "EEEE, MMMM d, y", "h:mm a", "h:mm:ss a", "{1}, {0}", ""},
    {"en-GB", ".", ",", 3, 3, 1, "{0}%", "dd/MM/y", "d MMMM y", "EEEE d MMMM y", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"en-IE", ".", ",", 3, 3, 1, "{0}%", "dd/MM/y", "d MMMM y", "EEEE d MMMM y", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"en-AU", ".", ",", 3, 3, 1, "{0}%", "dd/MM/y", "d MMMM y", "EEEE d MMMM y", "h:mm a", "h:mm:ss a", "{1}, {0}", ""},
    {"en-NZ", ".", ",", 3, 3, 1, "{0}%", "d/MM/y", "d MMMM y", "EEEE, d MMMM y", "h:mm a", "h:mm:ss a", "{1}, {0}", ""},
    {"en-CA", ".", ",", 3, 3, 1, "{0}%", "y-MM-dd", "MMMM d, y", "EEEE, MMMM d, y", "h:mm a", "h:mm:ss a", "{1}, {0}", ""},
    {"en-IN", ".", ",", 3, 2, 1, "{0}%", "dd/MM/y", "d MMMM y", "EEEE, d MMMM y", "h:mm a", "h:mm:ss a", "{1}, {0}", ""},
    {"en-ZA", ",", UC_NBSP, 3, 3, 1, "{0}%", "y/MM/dd", "d MMMM y", "EEEE, d MMMM y", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"de", ",", ".", 3, 3, 1, "{0}" UC_NBSP "%", "dd.MM.y", "d. MMMM y", "EEEE, d. MMMM y", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"de-AT", ",", UC_NBSP, 3, 3, 1, "{0}" UC_NBSP "%", "dd.MM.y", "d. MMMM y", "EEEE, d. MMMM y", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"de-CH", ".", UC_RSQUO, 3, 3, 1, "{0}%", "dd.MM.y", "d. MMMM y", "EEEE, d. MMMM y", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"fr", ",", UC_NNBSP, 3, 3, 1, "{0}" UC_NNBSP "%", "dd/MM/y", "d MMMM y", "EEEE d MMMM y", "HH:mm", "HH:mm:ss", "{1} {0}", ""},
    {"fr-CA", ",", UC_NBSP, 3, 3, 1, "{0}" UC_NBSP "%", "y-MM-dd", "d MMMM y", "EEEE d MMMM y", "HH 'h' mm", "HH 'h' mm 'min' ss 's'", "{1} {0}", ""},
    {"fr-CH", ",", UC_NNBSP, 3, 3, 1, "{0}%", "dd.MM.y", "d MMMM y", "EEEE, d MMMM y", "HH:mm", "HH:mm:ss", "{1} {0}", ""},
    {"es", ",", ".", 3, 3, 2, "{0}" UC_NBSP "%", "d/M/y", "d 'de' MMMM 'de' y", "EEEE, d 'de' MMMM 'de' y", "H:mm", "H:mm:ss", "{1}, {0}", ""},
    {"es-MX", ".", ",", 3, 3, 1, "{0}" UC_NBSP "%", "dd/MM/y", "d 'de' MMMM 'de' y", "EEEE, d 'de' MMMM 'de' y", "H:mm", "H:mm:ss", "{1}, {0}", ""},
    {"es-US", ".", ",", 3, 3, 1, "{0}" UC_NBSP "%", "d/M/y", "d 'de' MMMM 'de' y", "EEEE, d 'de' MMMM 'de' y", "h:mm a", "h:mm:ss a", "{1}, {0}", ""},
    {"es-419", ".", ",", 3, 3, 1, "{0}" UC_NBSP "%", "d/M/y", "d 'de' MMMM 'de' y", "EEEE, d 'de' MMMM 'de' y", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"it", ",", ".", 3, 3, 1, "{0}%", "dd/MM/y", "d MMMM y", "EEEE d MMMM y", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"it-CH", ".", UC_RSQUO, 3, 3, 1, "{0}%", "dd.MM.y", "d MMMM y", "EEEE, d MMMM y", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"nl", ",", ".", 3, 3, 1, "{0}%", "dd-MM-y", "d MMMM y", "EEEE d MMMM y", "HH:mm", "HH:mm:ss", "{1} {0}", ""},
    {"nl-BE", ",", ".", 3, 3, 1, "{0}%", "d/MM/y", "d MMMM y", "EEEE d MMMM y", "HH:mm", "HH:mm:ss", "{1} {0}", ""},
    {"pt", ",", UC_NBSP, 3, 3, 2, "{0}%", "dd/MM/y", "d 'de' MMMM 'de' y", "EEEE, d 'de' MMMM 'de' y", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"pt-BR", ",", ".", 3, 3, 1, "{0}%", "dd/MM/y", "d 'de' MMMM 'de' y", "EEEE, d 'de' MMMM 'de' y", "HH:mm", "HH:mm:ss", "{1} {0}", ""},
    {"ja", ".", ",", 3, 3, 1, "{0}%", "y/MM/dd", "y年M月d日", "y年M月d日EEEE", "H:mm", "H:mm:ss", "{1} {0}", ""},
    {"zh", ".", ",", 3, 3, 1, "{0}%", "y/M/d", "y年M月d日", "y年M月d日EEEE", "HH:mm", "HH:mm:ss", "{1} {0}", ""},
    {"zh-Hant", ".", ",", 3, 3, 1, "{0}%", "y/M/d", "y年M月d日", "y年M月d日 EEEE", "ah:mm", "ah:mm:ss", "{1} {0}", ""},
    {"zh-TW", ".", ",", 3, 3, 1, "{0}%", "y/M/d", "y年M月d日", "y年M月d日 EEEE", "ah:mm", "ah:mm:ss", "{1} {0}", ""},
    {"zh-Hant-HK", ".", ",", 3, 3, 1, "{0}%", "d/M/y", "y年M月d日", "y年M月d日EEEE", "ah:mm", "ah:mm:ss", "{1} {0}", ""},
    {"zh-HK", ".", ",", 3, 3, 1, "{0}%", "d/M/y", "y年M月d日", "y年M月d日EEEE", "ah:mm", "ah:mm:ss", "{1} {0}", ""},
    {"ko", ".", ",", 3, 3, 1, "{0}%", "y. M. d.", "y년 M월 d일", "y년 M월 d일 EEEE", "a h:mm", "a h:mm:ss", "{1} {0}", ""},
    {"ru", ",", UC_NBSP, 3, 3, 1, "{0}" UC_NBSP "%", "dd.MM.y", "d MMMM y 'г'.", "EEEE, d MMMM y 'г'.", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"uk", ",", UC_NBSP, 3, 3, 1, "{0}%", "dd.MM.y", "d MMMM y 'р'.", "EEEE, d MMMM y 'р'.", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"pl", ",", UC_NBSP, 3, 3, 2, "{0}%", "dd.MM.y", "d MMMM y", "EEEE, d MMMM y", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"cs", ",", UC_NBSP, 3, 3, 1, "{0}" UC_NBSP "%", "d. M. y", "d. MMMM y", "EEEE d. MMMM y", "H:mm", "H:mm:ss", "{1} {0}", ""},
    {"sk", ",", UC_NBSP, 3, 3, 1, "{0}" UC_NBSP "%", "d. M. y", "d. MMMM y", "EEEE d. MMMM y", "H:mm", "H:mm:ss", "{1} {0}", ""},
    {"sv", ",", UC_NBSP, 3, 3, 1, "{0}" UC_NBSP "%", "y-MM-dd", "d MMMM y", "EEEE d MMMM y", "HH:mm", "HH:mm:ss", "{1} {0}", ""},
    {"da", ",", ".", 3, 3, 1, "{0}" UC_NBSP "%", "dd.MM.y", "d. MMMM y", "EEEE 'den' d. MMMM y", "HH.mm", "HH.mm.ss", "{1} {0}", ""},
    {"nb", ",", UC_NBSP, 3, 3, 1, "{0}" UC_NBSP "%", "dd.MM.y", "d. MMMM y", "EEEE d. MMMM y", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"no", ",", UC_NBSP, 3, 3, 1, "{0}" UC_NBSP "%", "dd.MM.y", "d. MMMM y", "EEEE d. MMMM y", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"nn", ",", UC_NBSP, 3, 3, 1, "{0}" UC_NBSP "%", "dd.MM.y", "d. MMMM y", "EEEE d. MMMM y", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"fi", ",", UC_NBSP, 3, 3, 1, "{0}" UC_NBSP "%", "d.M.y", "d. MMMM y", "EEEE d. MMMM y", "H.mm", "H.mm.ss", "{1} {0}", ""},
    {"tr", ",", ".", 3, 3, 1, "%{0}", "dd.MM.y", "d MMMM y", "d MMMM y EEEE", "HH:mm", "HH:mm:ss", "{1} {0}", ""},
    {"el", ",", ".", 3, 3, 1, "{0}%", "d/M/y", "d MMMM y", "EEEE d MMMM y", "h:mm a", "h:mm:ss a", "{1}, {0}", ""},
    {"ar", "\xD9\xAB", "\xD9\xAC", 3, 3, 1, "{0}\xD9\xAA\xD8\x9C", "d" UC_RLM "/M" UC_RLM "/y", "d MMMM y", "EEEE\xD8\x8C d MMMM y",
     "h:mm a", "h:mm:ss a", "{1} {0}", kArabicIndicDigits},
    {"ar-MA", ",", ".", 3, 3, 1, "{0}%", "d" UC_RLM "/M" UC_RLM "/y", "d MMMM y", "EEEE\xD8\x8C d MMMM y", "HH:mm", "HH:mm:ss", "{1} {0}", ""},
    {"ar-DZ", ",", ".", 3, 3, 1, "{0}%", "d" UC_RLM "/M" UC_RLM "/y", "d MMMM y", "EEEE\xD8\x8C d MMMM y", "h:mm a", "h:mm:ss a", "{1} {0}", ""},
    {"ar-TN", ",", ".", 3, 3, 1, "{0}%", "d" UC_RLM "/M" UC_RLM "/y", "d MMMM y", "EEEE\xD8\x8C d MMMM y", "h:mm a", "h:mm:ss a", "{1} {0}", ""},
    {"he", ".", ",", 3, 3, 1, "{0}%", "d.M.y", "d בMMMM y", "EEEE, d בMMMM y", "H:mm", "H:mm:ss", "{1}, {0}", ""},
    {"hu", ",", UC_NBSP, 3, 3, 1, "{0}%", "y. MM. dd.", "y. MMMM d.", "y. MMMM d., EEEE", "H:mm", "H:mm:ss", "{1} {0}", ""},
    {"ro", ",", ".", 3, 3, 1, "{0}" UC_NBSP "%", "dd.MM.y", "d MMMM y", "EEEE, d MMMM y", "HH:mm", "HH:mm:ss", "{1}, {0}", ""},
    {"hi", ".", ",", 3, 2, 1, "{0}%", "d/M/y", "d MMMM y", "EEEE, d MMMM y", "h:mm a", "h:mm:ss a", "{1}, {0}", ""},
    {"id", ",", ".", 3, 3, 1, "{0}%", "dd/MM/y", "d MMMM y", "EEEE, d MMMM y", "HH.mm", "HH.mm.ss", "{1} {0}", ""},
    {"vi", ",", ".", 3, 3, 1, "{0}%", "dd/MM/y", "d MMMM, y", "EEEE, d MMMM, y", "HH:mm", "HH:mm:ss", "{0} {1}", ""},
    {"th", ".", ",", 3, 3, 1, "{0}%", "d/M/y", "d MMMM y", "EEEEที่ d MMMM y", "HH:mm", "HH:mm:ss", "{1} {0}", ""},
    {"ca", ",", ".", 3, 3, 1, "{0}" UC_NBSP "%", "d/M/y", "d MMMM 'de' y", "EEEE, d MMMM 'de' y", "H:mm", "H:mm:ss", "{1}, {0}", ""},
};

LocaleFormats FormatsOfRow(const FormatRow& r) {
    LocaleFormats f;
    f.decimalSeparator = r.decimal;
    f.groupSeparator = r.group;
    f.groupSize = r.groupSize;
    f.secondaryGroupSize = r.secondaryGroupSize;
    f.minimumGroupingDigits = r.minimumGroupingDigits;
    f.digits = r.digits;
    f.percentPattern = r.percent;
    f.shortDate = r.shortDate;
    f.longDate = r.longDate;
    f.fullDate = r.fullDate;
    f.shortTime = r.shortTime;
    f.mediumTime = r.mediumTime;
    f.dateTimePattern = r.join;
    return f;
}

LocaleFormats CurrentFormats() {
    State& st = S();
    std::shared_lock lock(st.mutex);
    return st.formats;
}

// The ten digits of a LocaleFormats, each a UTF-8 sequence; empty for the
// ASCII ones (or a `digits` that does not hold ten).
std::vector<std::string> DigitsOf(const std::string& digits) {
    std::vector<std::string> out;
    for (size_t i = 0; i < digits.size();) {
        const unsigned char c = static_cast<unsigned char>(digits[i]);
        const size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        out.push_back(digits.substr(i, len));
        i += len;
    }
    if (out.size() != 10) out.clear();
    return out;
}

// ASCII digits in `text` as the language writes them.
std::string LocaliseDigits(const std::string& text, const LocaleFormats& f) {
    if (f.digits.empty()) return text;
    const std::vector<std::string> digits = DigitsOf(f.digits);
    if (digits.empty()) return text;
    std::string out;
    for (char c : text) {
        if (c >= '0' && c <= '9') out += digits[static_cast<size_t>(c - '0')];
        else out += c;
    }
    return out;
}

// "1234567" grouped: "1.234.567", "12,34,567" in India; whole while it has
// fewer digits than a group and the minimum.
std::string GroupDigits(const std::string& digits, const LocaleFormats& f) {
    const size_t first = static_cast<size_t>(std::max(1, f.groupSize));
    const size_t rest = static_cast<size_t>(std::max(1, f.secondaryGroupSize));
    const size_t minimum = static_cast<size_t>(std::max(1, f.minimumGroupingDigits));
    if (f.groupSeparator.empty() || f.groupSize <= 0 || digits.size() < first + minimum) return digits;
    std::vector<std::string> groups;
    size_t end = digits.size();
    size_t size = first;
    while (end > 0) {
        const size_t start = end > size ? end - size : 0;
        groups.push_back(digits.substr(start, end - start));
        end = start;
        size = rest;
    }
    std::string out;
    for (size_t i = groups.size(); i-- > 0;) {
        out += groups[i];
        if (i > 0) out += f.groupSeparator;
    }
    return out;
}

std::string FormatIntegerWith(long long value, const LocaleFormats& f) {
    const bool negative = value < 0;
    const unsigned long long magnitude = negative ? 0ULL - static_cast<unsigned long long>(value) : static_cast<unsigned long long>(value);
    return LocaliseDigits((negative ? "-" : "") + GroupDigits(std::to_string(magnitude), f), f);
}

// Exactly `decimals` digits after the separator; grouped when `group`.
std::string FormatNumberWith(double value, int decimals, const LocaleFormats& f, bool group) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value < 0 ? "-\xE2\x88\x9E" : "\xE2\x88\x9E";
    decimals = std::clamp(decimals, 0, 15);
    // The digits in the classic locale, whatever LC_NUMERIC and the global
    // C++ locale say; the separators are put in below.
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream.setf(std::ios::fixed);
    stream.precision(decimals);
    stream << value;
    std::string text = stream.str();
    bool negative = false;
    if (!text.empty() && text[0] == '-') { negative = true; text.erase(0, 1); }
    const size_t dot = text.find('.');
    std::string whole = dot == std::string::npos ? text : text.substr(0, dot);
    const std::string fraction = dot == std::string::npos ? std::string() : text.substr(dot + 1);
    if (negative && whole.find_first_not_of('0') == std::string::npos && fraction.find_first_not_of('0') == std::string::npos)
        negative = false;   // -0,0 is 0,0
    std::string out = negative ? "-" : "";
    out += group ? GroupDigits(whole, f) : whole;
    if (!fraction.empty()) out += f.decimalSeparator + fraction;
    return LocaliseDigits(out, f);
}

std::string Padded(int value, int width) {
    std::string s = std::to_string(value < 0 ? -value : value);
    while (static_cast<int>(s.size()) < width) s.insert(s.begin(), '0');
    return value < 0 ? "-" + s : s;
}

// 0 for Sunday, Sakamoto's method.
int WeekdayOf(int year, int month, int day) {
    static const int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 1 || month > 12) return 0;
    if (month < 3) --year;
    const int w = (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
    return w < 0 ? w + 7 : w;
}

const char* const kMonthNames[] = {"January", "February", "March", "April", "May", "June", "July",
                                   "August", "September", "October", "November", "December"};
const char* const kMonthShort[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kWeekdayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const kWeekdayShort[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};

bool IsPatternLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// Where a formatted date takes its words: the loaded language (TrC), or
// one language's standard set read for a preview (ExamplesFor).
using WordLookup = std::function<std::string(const char* context, const char* key)>;

std::string LoadedWord(const char* context, const char* key) { return TrC(context, key); }

// A CLDR pattern written out with the formats' digits and the standard
// set's words.
std::string FormatPatternWith(const CalendarTime& when, const std::string& pattern, const LocaleFormats& f,
                              const WordLookup& word = LoadedWord) {
    const int month = std::clamp(when.month, 1, 12);
    const int weekday = when.weekday >= 0 && when.weekday <= 6 ? when.weekday : WeekdayOf(when.year, when.month, when.day);
    std::string out;
    for (size_t i = 0; i < pattern.size();) {
        const char c = pattern[i];
        if (c == '\'') {
            if (i + 1 < pattern.size() && pattern[i + 1] == '\'') { out += '\''; i += 2; continue; }
            ++i;
            while (i < pattern.size()) {
                if (pattern[i] == '\'') {
                    if (i + 1 < pattern.size() && pattern[i + 1] == '\'') { out += '\''; i += 2; continue; }
                    ++i;
                    break;
                }
                out += pattern[i++];
            }
            continue;
        }
        if (!IsPatternLetter(c)) { out += c; ++i; continue; }
        size_t n = 1;
        while (i + n < pattern.size() && pattern[i + n] == c) ++n;
        i += n;
        auto number = [&](int value, size_t width) { out += LocaliseDigits(Padded(value, static_cast<int>(width)), f); };
        switch (c) {
            case 'y': case 'u':
                if (n == 2) number(((when.year % 100) + 100) % 100, 2);
                else number(when.year, n);
                break;
            case 'M': case 'L':
                if (n <= 2) number(month, n);
                else if (n == 3) out += word("month-short", kMonthShort[month - 1]);
                else out += word(c == 'M' ? "date-month" : "month", kMonthNames[month - 1]);
                break;
            case 'd': number(when.day, std::min<size_t>(n, 2)); break;
            case 'E': case 'c': case 'e':
                if (n <= 2 && c != 'E') break;   // the numeric weekday: not written
                if (n <= 3) out += word("weekday", kWeekdayShort[weekday]);
                else out += word("date-weekday", kWeekdayNames[weekday]);
                break;
            case 'H': number(when.hour, std::min<size_t>(n, 2)); break;
            case 'k': number(when.hour == 0 ? 24 : when.hour, std::min<size_t>(n, 2)); break;
            case 'h': number(when.hour % 12 == 0 ? 12 : when.hour % 12, std::min<size_t>(n, 2)); break;
            case 'K': number(when.hour % 12, std::min<size_t>(n, 2)); break;
            case 'm': number(when.minute, std::min<size_t>(n, 2)); break;
            case 's': number(when.second, std::min<size_t>(n, 2)); break;
            case 'a': case 'b': case 'B': out += word("time", when.hour < 12 ? "AM" : "PM"); break;
            default: break;   // the era, the time zone, the quarter, fractions: not written
        }
    }
    // A field left out (an era, a zone) leaves its spaces behind
    std::string tidy;
    for (char ch : out) {
        if (ch == ' ' && (tidy.empty() || tidy.back() == ' ')) continue;
        tidy += ch;
    }
    while (!tidy.empty() && tidy.back() == ' ') tidy.pop_back();
    return tidy;
}

std::string JoinDateTime(const std::string& date, const std::string& time, const LocaleFormats& f) {
    const std::string pattern = f.dateTimePattern.empty() ? std::string("{1} {0}") : f.dateTimePattern;
    return LocalisationDetail::Substitute(pattern, {time, date});
}

// A literal piece of a converted pattern: its runs of letters quoted, a
// quote doubled.
void AppendLiteral(std::string& out, const std::string& literal) {
    bool open = false;
    for (char c : literal) {
        if (IsPatternLetter(c) != open) { out += '\''; open = !open; }
        if (c == '\'') out += "''";
        else out += c;
    }
    if (open) out += '\'';
}

}  // namespace

LocaleFormats UltraLocalisation::FormatsFor(const std::string& language) {
    const std::string canonical = CanonicalTag(language.empty() ? std::string("en") : language);
    for (const std::string& tag : FallbackWalk(canonical)) {
        for (const FormatRow& row : kFormatRows) {
            if (Lower(row.tag) == Lower(tag)) {
                LocaleFormats f = FormatsOfRow(row);
                f.language = canonical;
                return f;
            }
        }
    }
    LocaleFormats f;
    f.language = canonical;
    return f;
}

LocaleFormats UltraLocalisation::Formats() { return CurrentFormats(); }

void UltraLocalisation::SetRegionalFormats(const LocaleFormats& formats) {
    State& st = S();
    std::unique_lock lock(st.mutex);
    st.regional = formats;
    if (!st.regional.language.empty()) st.regional.language = CanonicalTag(st.regional.language);
    st.regionalGiven = !formats.language.empty();
    UpdateFormats(st);
}

LocaleFormats UltraLocalisation::RegionalFormats() {
    State& st = S();
    std::shared_lock lock(st.mutex);
    if (st.regionalGiven) return st.regional;
    LocaleFormats none;
    none.language.clear();
    return none;
}

std::string UltraLocalisation::FormatNumber(double value, int decimals) {
    return FormatNumberWith(value, decimals, CurrentFormats(), true);
}

std::string UltraLocalisation::FormatInteger(long long value) { return FormatIntegerWith(value, CurrentFormats()); }

std::string UltraLocalisation::FormatPercent(double fraction, int decimals) {
    const LocaleFormats f = CurrentFormats();
    const std::string number = FormatNumberWith(fraction * 100.0, decimals, f, true);
    return LocalisationDetail::Substitute(f.percentPattern.empty() ? std::string("{0}%") : f.percentPattern, {number});
}

std::string UltraLocalisation::FormatFileSize(unsigned long long bytes) {
    if (bytes < 1024) return TrN("{n} byte", "{n} bytes", static_cast<long long>(bytes));
    static const char* const units[] = {"KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes) / 1024.0;
    size_t unit = 0;
    // 1023,96 KB rounds to "1024,0 KB": the next unit says it better
    while (unit + 1 < sizeof units / sizeof units[0] && value >= 1023.95) { value /= 1024.0; ++unit; }
    return FormatNumberWith(value, 1, CurrentFormats(), true) + " " + Tr(units[unit]);
}

CalendarTime UltraLocalisation::LocalTime(std::time_t when) {
    std::tm parts{};
#if defined(_WIN32)
    localtime_s(&parts, &when);
#else
    localtime_r(&when, &parts);
#endif
    CalendarTime t;
    t.year = parts.tm_year + 1900;
    t.month = parts.tm_mon + 1;
    t.day = parts.tm_mday;
    t.weekday = parts.tm_wday;
    t.hour = parts.tm_hour;
    t.minute = parts.tm_min;
    t.second = parts.tm_sec;
    return t;
}

std::string UltraLocalisation::FormatPattern(const CalendarTime& when, const std::string& pattern) {
    return FormatPatternWith(when, pattern, CurrentFormats());
}

std::string UltraLocalisation::FormatDate(const CalendarTime& when, DateStyle style) {
    const LocaleFormats f = CurrentFormats();
    const std::string& pattern = style == DateStyle::Short ? f.shortDate : style == DateStyle::Long ? f.longDate : f.fullDate;
    return FormatPatternWith(when, pattern, f);
}

std::string UltraLocalisation::FormatTime(const CalendarTime& when, TimeStyle style) {
    const LocaleFormats f = CurrentFormats();
    return FormatPatternWith(when, style == TimeStyle::Short ? f.shortTime : f.mediumTime, f);
}

std::string UltraLocalisation::FormatDateTime(const CalendarTime& when, DateStyle date, TimeStyle time) {
    const LocaleFormats f = CurrentFormats();
    return JoinDateTime(FormatDate(when, date), FormatTime(when, time), f);
}

std::string UltraLocalisation::FormatDate(std::time_t when, DateStyle style) { return FormatDate(LocalTime(when), style); }
std::string UltraLocalisation::FormatTime(std::time_t when, TimeStyle style) { return FormatTime(LocalTime(when), style); }
std::string UltraLocalisation::FormatDateTime(std::time_t when, DateStyle date, TimeStyle time) {
    return FormatDateTime(LocalTime(when), date, time);
}

std::string UltraLocalisation::DecimalSeparator() { return CurrentFormats().decimalSeparator; }
std::string UltraLocalisation::GroupSeparator() { return CurrentFormats().groupSeparator; }

std::string UltraLocalisation::PatternFromStrftime(const std::string& format) {
    std::string out, literal;
    auto field = [&](const char* ldml) { AppendLiteral(out, literal); literal.clear(); out += ldml; };
    for (size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%' || i + 1 >= format.size()) { literal += format[i]; continue; }
        size_t j = i + 1;
        bool unpadded = false;
        // glibc's flags and the E / O modifiers
        while (j < format.size() && std::strchr("-_0^#EO", format[j])) { unpadded = unpadded || format[j] == '-'; ++j; }
        if (j >= format.size()) break;
        i = j;
        switch (format[j]) {
            case 'd': field(unpadded ? "d" : "dd"); break;
            case 'e': field("d"); break;
            case 'm': field(unpadded ? "M" : "MM"); break;
            case 'Y': case 'G': field("y"); break;
            case 'y': case 'g': field("yy"); break;
            case 'b': case 'h': field("MMM"); break;
            case 'B': field("MMMM"); break;
            case 'a': field("EEE"); break;
            case 'A': field("EEEE"); break;
            case 'H': field(unpadded ? "H" : "HH"); break;
            case 'k': field("H"); break;
            case 'I': field(unpadded ? "h" : "hh"); break;
            case 'l': field("h"); break;
            case 'M': field(unpadded ? "m" : "mm"); break;
            case 'S': field(unpadded ? "s" : "ss"); break;
            case 'p': case 'P': field("a"); break;
            case 'r': field("hh:mm:ss a"); break;
            case 'R': field("HH:mm"); break;
            case 'T': field("HH:mm:ss"); break;
            case 'D': field("MM/dd/yy"); break;
            case 'F': field("y-MM-dd"); break;
            case 'n': case 't': literal += ' '; break;
            case '%': literal += '%'; break;
            default: break;   // %Z, %z, %C, %j, %U ...: nothing a date of ours writes
        }
    }
    AppendLiteral(out, literal);
    return out;
}

std::string UltraLocalisation::PatternFromWindows(const std::string& picture) {
    std::string out;
    for (size_t i = 0; i < picture.size();) {
        const char c = picture[i];
        if (c == '\'') {
            // A quoted piece is the same in both: copy it through its closing quote
            out += c;
            ++i;
            while (i < picture.size()) {
                out += picture[i];
                if (picture[i] == '\'') {
                    if (i + 1 < picture.size() && picture[i + 1] == '\'') { out += '\''; i += 2; continue; }
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }
        if (!IsPatternLetter(c)) { out += c; ++i; continue; }
        size_t n = 1;
        while (i + n < picture.size() && picture[i + n] == c) ++n;
        i += n;
        switch (c) {
            case 'd': out += n >= 4 ? "EEEE" : n == 3 ? "EEE" : n == 2 ? "dd" : "d"; break;
            case 'M': out += std::string(std::min<size_t>(n, 4), 'M'); break;
            case 'y': out += n >= 3 ? "y" : "yy"; break;
            case 'h': case 'H': case 'm': case 's': out += std::string(std::min<size_t>(n, 2), c); break;
            case 't': out += 'a'; break;
            case 'g': break;   // the era
            default: AppendLiteral(out, std::string(n, c)); break;
        }
    }
    return out;
}

std::string UltraLocalisation::PatternWithoutSeconds(const std::string& pattern) {
    std::string out;
    bool quoted = false;
    for (size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if (c == '\'') { quoted = !quoted; out += c; continue; }
        if (!quoted && c == 's') {
            while (i + 1 < pattern.size() && pattern[i + 1] == 's') ++i;
            // the separator before the seconds goes with them
            while (!out.empty() && (out.back() == ':' || out.back() == '.' || out.back() == ',')) out.pop_back();
            continue;
        }
        out += c;
    }
    return out;
}

std::string UltraLocalisation::PatternWithFullYear(const std::string& pattern) {
    std::string out;
    bool quoted = false;
    for (size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if (c == '\'') quoted = !quoted;
        if (!quoted && c == 'y') {
            size_t n = 1;
            while (i + n < pattern.size() && pattern[i + n] == 'y') ++n;
            out += n == 2 ? std::string("y") : std::string(n, 'y');
            i += n - 1;
            continue;
        }
        out += c;
    }
    return out;
}

LocaleFormatExamples UltraLocalisation::ExamplesFor(const std::string& language, std::time_t when, bool fromDesktop) {
    const std::string tag = CanonicalTag(language.empty() ? std::string("en") : language);
    LocaleFormatExamples examples;
    LocaleFormats f;
    std::vector<std::string> files;
    {
        State& st = S();
        std::shared_lock lock(st.mutex);
        examples.fromPlatform = st.regionalGiven && !st.regional.language.empty() &&
                                (fromDesktop || BaseOf(st.regional.language) == BaseOf(tag));
        f = examples.fromPlatform ? st.regional : FormatsFor(tag);
        files = SetFiles(st, tag, "Standard", false);
    }
    examples.formats = f.language;

    // The language's own standard set, the more specific file first, as Load reads it
    std::unordered_map<std::string, std::string> words;
    for (const std::string& path : files) {
        LocalisationCatalogue catalogue;
        std::string error;
        if (!ReadCatalogue(path, catalogue, error)) continue;
        for (const auto& [key, text] : catalogue.entries) words.emplace(key, text);
    }
    const WordLookup word = [&words](const char* context, const char* key) {
        auto it = words.find(std::string(context) + "|" + key);
        return it != words.end() ? it->second : std::string(key);
    };
    auto plain = [&words](const char* key) {
        auto it = words.find(key);
        return it != words.end() ? it->second : std::string(key);
    };

    const CalendarTime local = LocalTime(when);
    examples.number = FormatNumberWith(1234567.891, 2, f, true);
    examples.percent = LocalisationDetail::Substitute(f.percentPattern.empty() ? std::string("{0}%") : f.percentPattern,
                                                      {FormatNumberWith(12.5, 1, f, true)});
    examples.fileSize = FormatNumberWith(1258291.0 / (1024.0 * 1024.0), 1, f, true) + " " + plain("MB");
    examples.shortDate = FormatPatternWith(local, f.shortDate, f, word);
    examples.fullDate = FormatPatternWith(local, f.fullDate, f, word);
    examples.shortTime = FormatPatternWith(local, f.shortTime, f, word);
    return examples;
}

#undef UC_NBSP
#undef UC_NNBSP
#undef UC_RSQUO
#undef UC_RLM

// ===== THE CALLS =====

std::string Tr(std::string_view source) {
    State& st = S();
    std::shared_lock lock(st.mutex);
    const std::string* text = nullptr;
    if (FindLayer(st, source, &text) && text) return *text;
    return std::string(source);
}

std::string TrC(std::string_view context, std::string_view source) {
    std::string key(context);
    key += '|';
    key += source;
    State& st = S();
    std::shared_lock lock(st.mutex);
    const std::string* text = nullptr;
    if (FindLayer(st, key, &text) && text) return *text;
    return std::string(source);
}

std::string TrN(std::string_view singular, std::string_view plural, long long count) {
    State& st = S();
    const std::string countText = UltraLocalisation::FormatInteger(count);   // a count is a quantity: grouped
    std::shared_lock lock(st.mutex);
    const std::string key(singular);
    for (const Layer& layer : st.layers) {
        auto p = layer.plurals.find(key);
        if (p != layer.plurals.end()) {
            const std::string category = UltraLocalisation::PluralCategory(st.language, count);
            auto form = p->second.find(category);
            if (form == p->second.end()) form = p->second.find("other");
            if (form == p->second.end()) form = p->second.begin();
            return ReplaceAll(form->second, "{n}", countText);
        }
        auto e = layer.entries.find(key);
        if (e != layer.entries.end()) return ReplaceAll(e->second, "{n}", countText);   // a language with one form
    }
    return ReplaceAll(std::string(count == 1 || count == -1 ? singular : plural), "{n}", countText);
}

namespace LocalisationDetail {

std::string Substitute(std::string_view text, const std::vector<std::string>& args) {
    std::string out;
    out.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '{') {
            size_t j = i + 1;
            size_t index = 0;
            bool digits = false;
            while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) { index = index * 10 + (text[j] - '0'); ++j; digits = true; }
            if (digits && j < text.size() && text[j] == '}' && index < args.size()) {
                out += args[index];
                i = j;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

std::string ArgText(std::string_view value) { return std::string(value); }
// A number argument in the language's digits and decimal separator,
// ungrouped: it is as often a year, a port or a line number as a quantity,
// and "Port 8.080" would be wrong where "1.234 Dateien" is right (TrN and
// FormatInteger group).
std::string ArgText(long long value) { return LocaliseDigits(std::to_string(value), CurrentFormats()); }
std::string ArgText(unsigned long long value) { return LocaliseDigits(std::to_string(value), CurrentFormats()); }
std::string ArgText(double value) {
    // six decimals at most, the trailing zeros dropped, then the language's
    // separator and digits
    if (std::isnan(value) || std::isinf(value)) return FormatNumberWith(value, 0, CurrentFormats(), false);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.setf(std::ios::fixed);
    out.precision(6);
    out << value;
    std::string s = out.str();
    const size_t dot = s.find('.');
    if (dot != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    const LocaleFormats f = CurrentFormats();
    const size_t point = s.find('.');
    if (point != std::string::npos) s.replace(point, 1, f.decimalSeparator);
    return LocaliseDigits(s, f);
}

}  // namespace LocalisationDetail

}  // namespace UltraCanvas
