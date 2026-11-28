// core/UltraLocalisation/UltraLocalisation.cpp
// See UltraLocalisation/UltraLocalisation.h. The catalogue files are read
// with the vendored yyjson engine directly, as UltraWin's QMP client does,
// so the archive stands without the core; the format is the one
// UltraCanvasLocalisationProposal.md §4 shows.
// Version: 1.2.0 - DecimalSeparator, GroupSeparator, FormatNumber, ParseNumber (CLDR symbols); TrF's double in them
// Version: 1.1.0 - the plural rules of Breton, Scottish Gaelic, Manx, Cornish, Tachelhit, Tamazight, Filipino, Colognian, the Sami languages, Hebrew's many, pt-PT
// Version: 1.0.0
// Last Modified: 2026-11-28
// Author: UltraCanvas Framework
#include "UltraLocalisation/UltraLocalisation.h"

#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8 / GetEnvUtf8 / OpenFileUtf8

#include <yyjson.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

std::string UltraLocalisation::ChooseLanguage(const std::vector<std::string>& systemLanguages) {
    State& st = S();
    std::string override, settings;
    {
        std::shared_lock lock(st.mutex);
        override = st.languageOverride;
        settings = st.settings;
    }
    if (!override.empty()) return override;
    const std::string env = GetEnvUtf8("ULTRACANVAS_LANG");
    if (!env.empty()) return CanonicalTag(env);
    const LanguageSettings conf = LanguageSettings::Load(settings);
    if (!conf.language.empty() && conf.language != "system") return conf.language;
    const std::vector<std::string> available = AvailableLanguages();
    for (const std::string& wanted : systemLanguages) {
        for (const std::string& tag : FallbackWalk(wanted)) {
            if (std::find(available.begin(), available.end(), tag) != available.end()) return tag;
        }
    }
    return "en";
}

bool UltraLocalisation::Initialize(const std::string& application, const std::vector<std::string>& systemLanguages) {
    return Load(ChooseLanguage(systemLanguages), application);
}

bool UltraLocalisation::Load(const std::string& language, const std::string& application) {
    State& st = S();
    std::unique_lock lock(st.mutex);
    const std::string tag = CanonicalTag(language.empty() ? std::string("en") : language);
    st.language = tag;
    st.application = application;
    st.layers.clear();
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
    std::string countText = LocalisationDetail::ArgText(count);
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
std::string ArgText(long long value) { return std::to_string(value); }
std::string ArgText(unsigned long long value) { return std::to_string(value); }
std::string ArgText(double value) {
    // the language's decimal separator, up to six places, not grouped
    return UltraLocalisation::FormatNumber(value, -1, false);
}

}  // namespace LocalisationDetail

// ===== NUMBERS IN THE LANGUAGE =====

namespace {

constexpr const char* kNoBreakSpace = "\xC2\xA0";         // U+00A0
constexpr const char* kNarrowNoBreakSpace = "\xE2\x80\xAF";  // U+202F
constexpr const char* kApostrophe = "\xE2\x80\x99";      // U+2019

struct NumberSymbols {
    const char* decimal = ".";
    const char* group = ",";
    int minimumGrouping = 1;   // CLDR minimumGroupingDigits: 2 leaves 1234 ungrouped
    bool indian = false;       // 12,34,567: threes, then twos
};

std::string NormalisedTag(std::string_view language) {
    std::string tag = language.empty() ? UltraLocalisation::CurrentLanguage() : std::string(language);
    for (char& c : tag) {
        if (c == '_') c = '-';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const size_t dot = tag.find_first_of(".@");   // "de_DE.UTF-8"
    if (dot != std::string::npos) tag.resize(dot);
    return tag;
}

// CLDR's latin-digit symbols for the languages UltraCanvas ships and their
// neighbours; a language not listed writes as English does.
NumberSymbols SymbolsFor(std::string_view language) {
    const std::string tag = NormalisedTag(language);
    const std::string base = tag.substr(0, tag.find('-'));
    auto region = [&](std::initializer_list<const char*> tags) {
        for (const char* t : tags) if (tag == t || tag.rfind(std::string(t) + "-", 0) == 0) return true;
        return false;
    };
    if (region({"de-ch", "de-li", "it-ch"})) return {".", kApostrophe, 1, false};
    if (region({"es-mx", "es-us", "es-419"})) return {".", ",", 1, false};
    if (region({"pt-pt", "pt-ao", "pt-mz"})) return {",", kNoBreakSpace, 2, false};
    if (region({"en-in"})) return {".", ",", 1, true};
    static const std::unordered_map<std::string, NumberSymbols> table = {
        // decimal comma, grouping dot
        {"de", {",", ".", 1, false}}, {"nl", {",", ".", 1, false}}, {"it", {",", ".", 1, false}},
        {"es", {",", ".", 2, false}}, {"pt", {",", ".", 1, false}}, {"id", {",", ".", 1, false}},
        {"da", {",", ".", 1, false}}, {"tr", {",", ".", 1, false}}, {"el", {",", ".", 1, false}},
        {"ro", {",", ".", 1, false}}, {"vi", {",", ".", 1, false}}, {"ca", {",", ".", 1, false}},
        {"sl", {",", ".", 2, false}}, {"hr", {",", ".", 1, false}}, {"sr", {",", ".", 1, false}},
        {"is", {",", ".", 1, false}}, {"gl", {",", ".", 1, false}}, {"eu", {",", ".", 1, false}},
        // decimal comma, grouping space
        {"fr", {",", kNarrowNoBreakSpace, 1, false}},
        {"ru", {",", kNoBreakSpace, 1, false}}, {"uk", {",", kNoBreakSpace, 1, false}},
        {"cs", {",", kNoBreakSpace, 1, false}}, {"sk", {",", kNoBreakSpace, 1, false}},
        {"pl", {",", kNoBreakSpace, 2, false}}, {"fi", {",", kNoBreakSpace, 1, false}},
        {"nb", {",", kNoBreakSpace, 1, false}}, {"nn", {",", kNoBreakSpace, 1, false}},
        {"no", {",", kNoBreakSpace, 1, false}}, {"sv", {",", kNoBreakSpace, 1, false}},
        {"hu", {",", kNoBreakSpace, 1, false}}, {"bg", {",", kNoBreakSpace, 2, false}},
        {"lt", {",", kNoBreakSpace, 1, false}}, {"lv", {",", kNoBreakSpace, 1, false}},
        {"et", {",", kNoBreakSpace, 2, false}},
        // grouping in lakhs
        {"hi", {".", ",", 1, true}}, {"bn", {".", ",", 1, true}}, {"mr", {".", ",", 1, true}},
    };
    const auto it = table.find(base);
    return it != table.end() ? it->second : NumberSymbols{};
}

std::string GroupDigits(const std::string& digits, const NumberSymbols& symbols) {
    const size_t n = digits.size();
    if (n < static_cast<size_t>(3 + symbols.minimumGrouping)) return digits;
    std::vector<size_t> cuts;   // where a separator goes, counted from the left
    size_t at = n;
    size_t width = 3;
    while (at > width) {
        at -= width;
        cuts.push_back(at);
        if (symbols.indian) width = 2;
    }
    std::string out;
    size_t from = 0;
    for (auto cut = cuts.rbegin(); cut != cuts.rend(); ++cut) {
        out.append(digits, from, *cut - from);
        out += symbols.group;
        from = *cut;
    }
    out.append(digits, from, std::string::npos);
    return out;
}

}  // namespace

std::string UltraLocalisation::DecimalSeparator(std::string_view language) { return SymbolsFor(language).decimal; }
std::string UltraLocalisation::GroupSeparator(std::string_view language) { return SymbolsFor(language).group; }

std::string UltraLocalisation::FormatNumber(double value, int decimals, bool grouped, std::string_view language) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value < 0 ? "-\xE2\x88\x9E" : "\xE2\x88\x9E";   // -∞, ∞
    // The digits in the classic locale, whatever LC_NUMERIC says, then the
    // language's symbols put in.
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text.setf(std::ios::fixed);
    text.precision(decimals < 0 ? 6 : std::min(decimals, 17));
    text << std::fabs(value);
    std::string digits = text.str();
    std::string fraction;
    const size_t point = digits.find('.');
    if (point != std::string::npos) {
        fraction = digits.substr(point + 1);
        digits.resize(point);
        if (decimals < 0)
            while (!fraction.empty() && fraction.back() == '0') fraction.pop_back();
    }
    const NumberSymbols symbols = SymbolsFor(language);
    std::string out = grouped ? GroupDigits(digits, symbols) : digits;
    if (!fraction.empty()) out += symbols.decimal + fraction;
    const bool zero = std::all_of(digits.begin(), digits.end(), [](char c) { return c == '0'; }) &&
                      std::all_of(fraction.begin(), fraction.end(), [](char c) { return c == '0'; });
    if (value < 0 && !zero) out.insert(0, "-");   // never "-0"
    return out;
}

bool UltraLocalisation::ParseNumber(std::string_view text, double& out, std::string_view language) {
    const NumberSymbols symbols = SymbolsFor(language);
    const char decimal = symbols.decimal[0];
    const char groupChar = symbols.group[1] == '\0' ? symbols.group[0] : '\0';   // "." or "," when it is one
    // Spaces and apostrophes of grouping out, U+2212 to "-".
    std::string s;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == ' ' || c == '\t' || c == '\'') { ++i; continue; }
        if (text.compare(i, 2, kNoBreakSpace) == 0) { i += 2; continue; }
        if (text.compare(i, 3, kNarrowNoBreakSpace) == 0 || text.compare(i, 3, kApostrophe) == 0 ||
            text.compare(i, 3, "\xE2\x80\x89") == 0) { i += 3; continue; }   // and U+2009 thin space
        if (text.compare(i, 3, "\xE2\x88\x92") == 0) { s += '-'; i += 3; continue; }
        s += static_cast<char>(c);
        ++i;
    }
    size_t start = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
    if (start == s.size()) return false;
    for (size_t i = start; i < s.size(); ++i)
        if (!(std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == ',')) return false;
    // Which of "." and "," is the decimal point.
    const size_t dots = static_cast<size_t>(std::count(s.begin(), s.end(), '.'));
    const size_t commas = static_cast<size_t>(std::count(s.begin(), s.end(), ','));
    char point = '\0';
    if (dots && commas) {
        point = s.find_last_of(".,") == s.rfind('.') ? '.' : ',';
        if ((point == '.' ? dots : commas) > 1) return false;
    } else if (dots + commas == 1) {
        const char only = dots ? '.' : ',';
        const size_t at = s.find(only);
        const size_t after = s.size() - at - 1;
        // "1.234" in German is a thousand and more; "1.5" is one and a half.
        const bool grouping = only != decimal && only == groupChar && after == 3 && at > start;
        point = grouping ? '\0' : only;
    }
    std::string plain;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '.' || s[i] == ',') { if (s[i] == point) plain += '.'; continue; }
        plain += s[i];
    }
    if (plain.empty() || plain == "-" || plain == "+" || plain == ".") return false;
    std::istringstream in(plain);
    in.imbue(std::locale::classic());
    double v = 0.0;
    in >> v;
    if (in.fail() || in.peek() != std::char_traits<char>::eof()) return false;
    out = v;
    return true;
}

}  // namespace UltraCanvas
