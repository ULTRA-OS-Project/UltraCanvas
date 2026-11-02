// core/UltraLocalisation/UltraLocalisation.cpp
// See UltraLocalisation/UltraLocalisation.h. The catalogue files are read
// with the vendored yyjson engine directly, as UltraWin's QMP client does,
// so the archive stands without the core; the format is the one
// UltraCanvasLocalisationProposal.md §4 shows.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#include "UltraLocalisation/UltraLocalisation.h"

#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8 / GetEnvUtf8 / OpenFileUtf8

#include <yyjson.h>

#include <algorithm>
#include <cctype>
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
    const std::string base = Lower(FallbackWalk(language).empty() ? std::string("en") : FallbackWalk(language).back());
    const long long n = count < 0 ? -count : count;
    const long long m10 = n % 10, m100 = n % 100;
    auto in = [](long long v, long long lo, long long hi) { return v >= lo && v <= hi; };
    auto oneOf = [&](std::initializer_list<const char*> langs) {
        for (const char* l : langs) if (base == l) return true;
        return false;
    };
    if (oneOf({"ja", "zh", "ko", "th", "vi", "id", "ms", "my", "km", "lo", "yue", "jv", "ig", "yo"})) return "other";
    if (oneOf({"fr", "hy", "kab", "ff"})) return n <= 1 ? "one" : "other";
    if (oneOf({"pt"})) return n <= 1 ? "one" : "other";                         // pt (Brazil's rule); pt-PT in CLDR is one for 1 alone
    if (oneOf({"hi", "bn", "gu", "fa", "mr", "kn", "zu", "am", "as"})) return n <= 1 ? "one" : "other";
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
    if (oneOf({"he", "iw"})) return n == 1 ? "one" : n == 2 ? "two" : "other";
    if (oneOf({"cy"})) return n == 0 ? "zero" : n == 1 ? "one" : n == 2 ? "two" : n == 3 ? "few" : n == 6 ? "many" : "other";
    if (oneOf({"mt"})) return n == 1 ? "one" : (n == 0 || in(m100, 2, 10)) ? "few" : in(m100, 11, 19) ? "many" : "other";
    if (oneOf({"is", "mk"})) return (m10 == 1 && m100 != 11) ? "one" : "other";
    // en, de, nl, sv, da, nb, nn, fi, et, it, es, ca, el, hu, tr, bg, sq, eu, gl, ka, sw, ur, ... : one for 1
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
    // dot-decimal whatever LC_NUMERIC says: formatted in the classic locale,
    // six significant decimals, trailing zeros dropped
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.setf(std::ios::fixed);
    out.precision(6);
    out << value;
    std::string s = out.str();
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

}  // namespace LocalisationDetail

}  // namespace UltraCanvas
