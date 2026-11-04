// core/UltraCanvasLocalisationEditor.cpp
// See UltraCanvasLocalisationEditor.h.
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework
#include "UltraCanvasLocalisationEditor.h"
#include "UltraCanvasConfig.h"           // GetResourcesDir
#include "UltraCanvasFileLoader.h"       // SaveFileDialog
#include "UltraCanvasModalDialog.h"      // DialogResult, FileFilter
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"
#include "UltraCanvasWindow.h"
#include "CSSLayout/CSSLayout.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace UltraCanvas {

namespace {

namespace fs = std::filesystem;

const char* kStandard = "Standard";

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ContainsText(const std::string& haystack, const std::string& needle) {
    return Lower(haystack).find(Lower(needle)) != std::string::npos;
}

std::string TrimSlash(std::string s) {
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}

// The extraction's key order (extract_ui_strings.py): case-insensitively,
// the exact spelling deciding between two that only differ in case.
bool KeyLess(const std::string& a, const std::string& b) {
    const std::string la = Lower(a), lb = Lower(b);
    return la != lb ? la < lb : a < b;
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
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
                else out += static_cast<char>(c);
        }
    }
    out += '"';
}

std::string Quoted(const std::string& s) { std::string q; JsonEscape(q, s); return q; }

// The file as extract_ui_strings.py writes it (json.dumps, indent=2,
// ensure_ascii=False): metadata first, then the keys in the extraction's
// order, a plural's forms and a list's items one per line. A save in this
// form is a clean diff against the shipped file.
std::string FormatCatalogue(const LocalisationCatalogue& c) {
    std::string out = "{\n";
    bool first = true;
    auto line = [&](const std::string& key, const std::string& valueJson) {
        if (!first) out += ",\n";
        first = false;
        out += "  " + Quoted(key) + ": " + valueJson;
    };
    auto list = [&](const std::vector<std::string>& items) {
        if (items.empty()) return std::string("[]");
        std::string a = "[\n";
        for (size_t i = 0; i < items.size(); ++i) a += "    " + Quoted(items[i]) + (i + 1 < items.size() ? ",\n" : "\n");
        return a + "  ]";
    };
    if (!c.language.empty()) line("@language", Quoted(c.language));
    if (!c.nativeName.empty()) line("@name", Quoted(c.nativeName));
    if (!c.sourceLanguage.empty()) line("@source-language", Quoted(c.sourceLanguage));
    if (!c.translatedBy.empty()) line("@translated-by", Quoted(c.translatedBy));
    if (!c.bases.empty()) line("@bases", list(c.bases));
    if (!c.inherited.empty()) line("@inherited", list(c.inherited));
    std::vector<std::string> keys;
    for (const auto& e : c.entries) keys.push_back(e.first);
    for (const auto& p : c.plurals) if (!c.entries.count(p.first)) keys.push_back(p.first);
    std::sort(keys.begin(), keys.end(), KeyLess);
    static const char* kOrder[] = {"zero", "one", "two", "few", "many", "other"};
    for (const std::string& key : keys) {
        auto p = c.plurals.find(key);
        if (p == c.plurals.end()) { line(key, Quoted(c.entries.at(key))); continue; }
        std::string obj = "{\n";
        std::vector<std::pair<std::string, std::string>> forms;
        for (const char* cat : kOrder) { auto f = p->second.find(cat); if (f != p->second.end()) forms.emplace_back(cat, f->second); }
        for (const auto& f : p->second) if (std::find_if(forms.begin(), forms.end(), [&](auto& x) { return x.first == f.first; }) == forms.end()) forms.push_back(f);
        for (size_t i = 0; i < forms.size(); ++i) obj += "    " + Quoted(forms[i].first) + ": " + Quoted(forms[i].second) + (i + 1 < forms.size() ? ",\n" : "\n");
        line(key, obj + "  }");
    }
    out += "\n}\n";
    return out;
}

std::string ContextOf(const std::string& key) {
    const size_t bar = key.find('|');
    return bar == std::string::npos ? std::string() : key.substr(0, bar);
}

std::string SourceOf(const std::string& key) {
    const size_t bar = key.find('|');
    return bar == std::string::npos ? key : key.substr(bar + 1);
}

std::string Today() {
    const std::time_t now = std::time(nullptr);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", std::localtime(&now));
    return buf;
}

std::vector<std::string> JsonNamesIn(const fs::path& folder) {
    std::vector<std::string> names;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return names;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const fs::path p = entry.path();
        if (PathToUtf8(p.extension()) != ".json") continue;
        names.push_back(PathToUtf8(p.stem()));
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string TextOf(const LocalisationCatalogue& c, const std::string& key, const std::string& category) {
    auto e = c.entries.find(key);
    if (e != c.entries.end() && category.empty()) return e->second;
    auto p = c.plurals.find(key);
    if (p != c.plurals.end()) {
        const std::string cat = category.empty() ? "other" : category;
        auto f = p->second.find(cat);
        if (f != p->second.end()) return f->second;
        if (category.empty() && !p->second.empty()) return p->second.begin()->second;
    }
    return e != c.entries.end() ? e->second : std::string();
}

bool HasKey(const LocalisationCatalogue& c, const std::string& key) {
    return c.entries.count(key) > 0 || c.plurals.count(key) > 0;
}

void EraseKey(LocalisationCatalogue& c, const std::string& key) {
    c.entries.erase(key);
    c.plurals.erase(key);
}

void CopyKey(const LocalisationCatalogue& from, LocalisationCatalogue& to, const std::string& key) {
    auto e = from.entries.find(key);
    if (e != from.entries.end()) to.entries[key] = e->second;
    auto p = from.plurals.find(key);
    if (p != from.plurals.end()) to.plurals[key] = p->second;
}

bool SameKey(const LocalisationCatalogue& a, const LocalisationCatalogue& b, const std::string& key) {
    auto ea = a.entries.find(key), eb = b.entries.find(key);
    if ((ea != a.entries.end()) != (eb != b.entries.end())) return false;
    if (ea != a.entries.end() && ea->second != eb->second) return false;
    auto pa = a.plurals.find(key), pb = b.plurals.find(key);
    if ((pa != a.plurals.end()) != (pb != b.plurals.end())) return false;
    return pa == a.plurals.end() || pa->second == pb->second;
}

void AddUnique(std::vector<std::string>& list, const std::string& item) {
    if (std::find(list.begin(), list.end(), item) == list.end()) {
        list.push_back(item);
        std::sort(list.begin(), list.end(), KeyLess);
    }
}

void Remove(std::vector<std::string>& list, const std::string& item) {
    list.erase(std::remove(list.begin(), list.end(), item), list.end());
}

}  // namespace

// ===== THE WORKSPACE: WHAT THERE IS =====

void LocalisationWorkspace::SetFolders(const std::string& shippedLocaleDir, const std::string& settingsFolder) {
    shipped = TrimSlash(shippedLocaleDir);
    settings = TrimSlash(settingsFolder);
}

std::vector<std::string> LocalisationWorkspace::Applications() const {
    std::vector<std::string> apps;
    if (shipped.empty()) return apps;
    for (const std::string& name : JsonNamesIn(PathFromUtf8(shipped) / "en")) if (name != kStandard) apps.push_back(name);
    if (!settings.empty()) {
        for (const std::string& name : JsonNamesIn(PathFromUtf8(settings) / "locale" / "en"))
            if (name != kStandard) AddUnique(apps, name);
    }
    std::sort(apps.begin(), apps.end(), KeyLess);
    return apps;
}

std::vector<std::string> LocalisationWorkspace::BaseSetNames() const {
    std::vector<std::string> names;
    if (!shipped.empty()) names = JsonNamesIn(PathFromUtf8(shipped) / "en" / "base");
    if (!settings.empty()) for (const std::string& n : JsonNamesIn(PathFromUtf8(settings) / "locale" / "en" / "base")) AddUnique(names, n);
    // The merged working set may hold a base not on disk yet (MergeBaseSets).
    for (const auto& [k, f] : files) if (f.base && f.lang == "en") AddUnique(names, f.set);
    std::sort(names.begin(), names.end(), KeyLess);
    return names;
}

std::vector<std::string> LocalisationWorkspace::LanguagesOnDisk() const {
    std::vector<std::string> tags;
    std::error_code ec;
    for (const std::string& root : {shipped, settings.empty() ? std::string() : settings + "/locale"}) {
        if (root.empty()) continue;
        for (const auto& entry : fs::directory_iterator(PathFromUtf8(root), ec)) {
            if (!entry.is_directory(ec)) continue;
            const std::string tag = PathToUtf8(entry.path().filename());
            if (!tag.empty() && tag[0] != '.') AddUnique(tags, tag);
        }
    }
    for (const auto& [k, f] : files) AddUnique(tags, f.lang);
    return tags;
}

std::vector<std::string> LocalisationWorkspace::Languages() const {
    std::vector<std::string> tags;
    for (const std::string& tag : LanguagesOnDisk()) {
        std::error_code ec;
        const bool standard = fs::exists(PathFromUtf8(ShippedPath(tag, kStandard, false)), ec)
                           || (!settings.empty() && fs::exists(PathFromUtf8(OverridePath(tag, kStandard, false)), ec));
        if (standard || tag == "en") tags.push_back(tag);
    }
    std::sort(tags.begin(), tags.end(), [](const std::string& a, const std::string& b) {
        if (a == "en") return b != "en";
        if (b == "en") return false;
        return a < b;
    });
    return tags;
}

std::string LocalisationWorkspace::ShippedPath(const std::string& lang, const std::string& set, bool base) const {
    if (shipped.empty()) return {};
    return shipped + "/" + lang + "/" + (base ? "base/" : "") + set + ".json";
}

std::string LocalisationWorkspace::OverridePath(const std::string& lang, const std::string& set, bool base) const {
    if (settings.empty()) return {};
    return settings + "/locale/" + lang + "/" + (base ? "base/" : "") + set + ".json";
}

std::vector<std::string> LocalisationWorkspace::Categories() const {
    std::set<std::string> seen;
    for (long long n = 0; n <= 200; ++n) seen.insert(UltraLocalisation::PluralCategory(language, n));
    seen.insert(UltraLocalisation::PluralCategory(language, 1000000));
    std::vector<std::string> ordered;
    for (const char* c : {"zero", "one", "two", "few", "many", "other"}) if (seen.count(c)) ordered.push_back(c);
    return ordered;
}

// ===== THE FILES =====

std::string LocalisationWorkspace::Key(const std::string& lang, const std::string& set, bool base) {
    return lang + "|" + (base ? "base/" : "") + set;
}

LocalisationWorkspace::File& LocalisationWorkspace::FileFor(const std::string& lang, const std::string& set, bool base) {
    const std::string key = Key(lang, set, base);
    auto it = files.find(key);
    if (it != files.end()) return it->second;
    File f;
    f.lang = lang;
    f.set = set;
    f.base = base;
    std::string error;
    LocalisationCatalogue shippedCatalogue, overrideCatalogue;
    const std::string sp = ShippedPath(lang, set, base), op = OverridePath(lang, set, base);
    if (!sp.empty() && UltraLocalisation::ReadCatalogue(sp, shippedCatalogue, error)) { f.shipped = shippedCatalogue; f.exists = true; }
    f.working = f.shipped;
    if (!op.empty() && UltraLocalisation::ReadCatalogue(op, overrideCatalogue, error)) {
        f.exists = true;
        for (const auto& [k, v] : overrideCatalogue.entries) { f.working.entries[k] = v; f.working.plurals.erase(k); }
        for (const auto& [k, v] : overrideCatalogue.plurals) { f.working.plurals[k] = v; f.working.entries.erase(k); }
        if (f.working.language.empty()) f.working.language = overrideCatalogue.language;
        if (f.working.nativeName.empty()) f.working.nativeName = overrideCatalogue.nativeName;
        if (f.working.bases.empty()) f.working.bases = overrideCatalogue.bases;
    }
    if (f.working.language.empty()) f.working.language = lang;
    f.persisted = f.working;
    return files.emplace(key, std::move(f)).first->second;
}

const LocalisationWorkspace::File* LocalisationWorkspace::FindFile(const std::string& lang, const std::string& set, bool base) const {
    auto it = files.find(Key(lang, set, base));
    return it == files.end() ? nullptr : &it->second;
}

bool LocalisationWorkspace::IsBase(const std::string& set) const {
    return set != kStandard && set != application && std::find(bases.begin(), bases.end(), set) != bases.end();
}

bool LocalisationWorkspace::TakesSet(const std::string& set) const {
    return set == application || set == kStandard || IsBase(set);
}

void LocalisationWorkspace::PushUndo() {
    undo.push_back(Snapshot{files, bases});
    if (undo.size() > 100) undo.erase(undo.begin());
}

bool LocalisationWorkspace::File::Dirty() const {
    return working.entries != persisted.entries || working.plurals != persisted.plurals
        || working.bases != persisted.bases || working.inherited != persisted.inherited
        || working.translatedBy != persisted.translatedBy;
}

bool LocalisationWorkspace::IsDraft(const LocalisationCatalogue& catalogue) {
    const std::string by = Lower(catalogue.translatedBy);
    return by.find("draft") != std::string::npos && by.find("reviewed by -") != std::string::npos;
}

// ===== OPEN =====

bool LocalisationWorkspace::Open(const std::string& app, const std::string& lang) {
    files.clear();
    undo.clear();
    bases.clear();
    application = app;
    language = lang.empty() ? "en" : lang;
    if (application.empty()) return false;
    // The application's bases: its catalogue in the language, else the English one.
    for (const std::string& l : {language, std::string("en")}) {
        const File& f = FileFor(l, application, false);
        if (!f.working.bases.empty()) { bases = f.working.bases; break; }
    }
    for (const std::string& b : bases) { FileFor("en", b, true); FileFor(language, b, true); }
    FileFor("en", kStandard, false);
    FileFor(language, kStandard, false);
    FileFor("en", application, false);
    FileFor(language, application, false);
    return true;
}

std::vector<LocalisationSetRef> LocalisationWorkspace::Sets() const {
    std::vector<LocalisationSetRef> sets;
    LocalisationSetRef app; app.name = application; app.isApplication = true; sets.push_back(app);
    for (const std::string& b : bases) { LocalisationSetRef r; r.name = b; r.isBase = true; sets.push_back(r); }
    LocalisationSetRef std_; std_.name = kStandard; std_.isStandard = true; sets.push_back(std_);
    return sets;
}

void LocalisationWorkspace::SetCompareLanguage(const std::string& lang) {
    compareLanguage = lang.empty() ? "en" : lang;
}

// ===== THE ROWS =====

std::string LocalisationWorkspace::Text(const std::string& set, const std::string& key, const std::string& category) const {
    const File* f = FindFile(language, set, IsBase(set));
    return f ? TextOf(f->working, key, category) : std::string();
}

std::string LocalisationWorkspace::EnglishText(const std::string& key, const std::string& category) const {
    for (const auto& s : Sets()) {
        const File* en = FindFile("en", s.name, s.isBase);
        if (!en || !HasKey(en->working, key)) continue;
        auto p = en->working.plurals.find(key);
        if (p != en->working.plurals.end()) {
            auto f = p->second.find(category.empty() ? "other" : category);
            if (f != p->second.end()) return f->second;
            auto o = p->second.find("other");
            if (o != p->second.end()) return o->second;
        }
        auto e = en->working.entries.find(key);
        if (e != en->working.entries.end()) return e->second;
    }
    return SourceOf(key);
}

bool LocalisationWorkspace::Has(const std::string& set, const std::string& key) const {
    const File* f = FindFile(language, set, IsBase(set));
    return f && HasKey(f->working, key);
}

std::vector<LocalisationRow> LocalisationWorkspace::Rows(bool wholeSets) const {
    std::vector<LocalisationRow> rows;
    if (application.empty()) return rows;
    const std::vector<LocalisationSetRef> sets = Sets();
    const std::vector<std::string> categories = Categories();
    auto fileOf = [&](const std::string& lang, const LocalisationSetRef& s) { return FindFile(lang, s.name, s.isBase); };

    // The keys: the application's own, the inherited ones its extraction
    // lists, what its file in the language has; with `wholeSets`, everything
    // the shared sets hold.
    std::vector<std::string> keys;
    std::set<std::string> seen, applicationKeys;
    auto add = [&](const std::string& k, bool ofApplication) {
        if (ofApplication) applicationKeys.insert(k);
        if (seen.insert(k).second) keys.push_back(k);
    };
    if (const File* en = fileOf("en", sets.front())) {
        for (const auto& e : en->working.entries) add(e.first, true);
        for (const auto& p : en->working.plurals) add(p.first, true);
        for (const std::string& k : en->working.inherited) add(k, true);
    }
    if (const File* own = fileOf(language, sets.front())) {
        for (const auto& e : own->working.entries) add(e.first, true);
        for (const auto& p : own->working.plurals) add(p.first, true);
    }
    if (wholeSets) {
        for (size_t i = 1; i < sets.size(); ++i) {
            if (const File* en = fileOf("en", sets[i])) {
                for (const auto& e : en->working.entries) add(e.first, false);
                for (const auto& p : en->working.plurals) add(p.first, false);
            }
        }
    }
    std::sort(keys.begin(), keys.end(), KeyLess);

    for (const std::string& key : keys) {
        LocalisationRow row;
        row.key = key;
        row.context = ContextOf(key);
        row.source = SourceOf(key);
        row.inApplication = applicationKeys.count(key) > 0;
        row.unusedSharedEntry = !row.inApplication;
        // A plural anywhere in English makes the row a plural.
        for (const auto& s : sets) {
            const File* en = fileOf("en", s);
            if (en && en->working.plurals.count(key)) { row.plural = true; break; }
        }
        if (row.plural) row.categories = categories;
        // The texts of every set in the language, the first answering wins.
        std::string firstBelow;    // the first set below the application that answers
        std::vector<std::string> baseAnswers;
        for (size_t i = 0; i < sets.size(); ++i) {
            const File* f = fileOf(language, sets[i]);
            if (!f || !HasKey(f->working, key)) continue;
            if (language == "en" && sets[i].isApplication) {
                // In English the application's own entry is its source: only
                // an entry that differs from the key is an override.
                if (TextOf(f->working, key, "") == row.source && !f->working.plurals.count(key)) continue;
            }
            row.texts[sets[i].name] = TextOf(f->working, key, "");
            auto p = f->working.plurals.find(key);
            if (p != f->working.plurals.end()) row.forms[sets[i].name] = p->second;
            if (row.shownFrom.empty()) {
                row.shownFrom = sets[i].name;
                row.shown = row.texts[sets[i].name];
                row.draft = IsDraft(f->working);
                row.changed = !SameKey(f->shipped, f->working, key);
            } else if (firstBelow.empty() && !sets[i].isApplication) {
                firstBelow = sets[i].name;
            }
            if (sets[i].isBase) baseAnswers.push_back(sets[i].name);
        }
        if (row.shownFrom.empty()) {
            row.shown = row.source;
            row.status = LocalisationRowStatus::Untranslated;
            if (const File* f = fileOf(language, sets.front())) row.changed = !SameKey(f->shipped, f->working, key);
        } else if (row.shownFrom == application) {
            if (firstBelow.empty()) row.status = LocalisationRowStatus::Application;
            else row.status = row.texts[firstBelow] == row.shown ? LocalisationRowStatus::Duplicate
                                                                 : LocalisationRowStatus::Override;
        } else if (row.shownFrom == kStandard) {
            row.status = LocalisationRowStatus::Standard;
        } else {
            row.status = LocalisationRowStatus::Base;
            for (const std::string& b : baseAnswers) {
                if (b != row.shownFrom && row.texts[b] != row.shown) {
                    row.status = LocalisationRowStatus::Collision;
                    row.collidesWith = b;
                    break;
                }
            }
        }
        // The comparison language, same lookup order.
        for (const auto& s : sets) {
            const File* f = fileOf(compareLanguage, s);
            if (f && HasKey(f->working, key)) { row.compare = TextOf(f->working, key, ""); break; }
        }
        if (row.compare.empty() && compareLanguage == "en") row.compare = row.source;
        rows.push_back(std::move(row));
    }
    return rows;
}

// ===== EDITING =====

bool LocalisationWorkspace::SetText(const std::string& set, const std::string& key, const std::string& text,
                                    const std::string& category) {
    if (application.empty() || key.empty() || !TakesSet(set)) return false;
    PushUndo();
    File& f = FileFor(language, set, IsBase(set));
    if (text.empty() && category.empty()) {
        if (!HasKey(f.working, key)) { undo.pop_back(); return false; }
        EraseKey(f.working, key);
    } else if (category.empty()) {
        f.working.plurals.erase(key);
        f.working.entries[key] = text;
    } else {
        f.working.entries.erase(key);
        if (text.empty()) {
            auto p = f.working.plurals.find(key);
            if (p != f.working.plurals.end()) { p->second.erase(category); if (p->second.empty()) f.working.plurals.erase(p); }
        } else {
            f.working.plurals[key][category] = text;
        }
    }
    if (set == application) {
        // A key new to the application is its own from now on.
        File& en = FileFor("en", application, false);
        if (!HasKey(en.working, key) && std::find(en.working.inherited.begin(), en.working.inherited.end(), key) == en.working.inherited.end()) {
            bool inherited = false;
            for (const auto& s : Sets()) {
                if (s.isApplication) continue;
                const File* sf = FindFile("en", s.name, s.isBase);
                if (sf && HasKey(sf->working, key)) { inherited = true; break; }
            }
            if (inherited) AddUnique(en.working.inherited, key);
            else if (language != "en") en.working.entries[key] = SourceOf(key);
        }
    }
    return true;
}

bool LocalisationWorkspace::Clear(const std::string& set, const std::string& key) {
    return SetText(set, key, "", "");
}

bool LocalisationWorkspace::AddKey(const std::string& key, const std::string& english) {
    if (application.empty() || key.empty()) return false;
    File& en = FileFor("en", application, false);
    if (HasKey(en.working, key)) return false;
    PushUndo();
    en.working.entries[key] = english.empty() ? SourceOf(key) : english;
    return true;
}

// ===== MERGING =====

bool LocalisationWorkspace::TakeBaseSet(const std::string& base) {
    if (application.empty() || base.empty() || base == kStandard || base == application) return false;
    if (std::find(bases.begin(), bases.end(), base) != bases.end()) return false;
    const std::vector<std::string> names = BaseSetNames();
    if (std::find(names.begin(), names.end(), base) == names.end()) return false;
    PushUndo();
    bases.push_back(base);
    FileFor("en", base, true);
    FileFor(language, base, true);
    return ReorderBases(bases);
}

bool LocalisationWorkspace::DropBaseSet(const std::string& base) {
    if (std::find(bases.begin(), bases.end(), base) == bases.end()) return false;
    PushUndo();
    std::vector<std::string> order = bases;
    Remove(order, base);
    bases = order;
    return ReorderBases(bases);
}

bool LocalisationWorkspace::ReorderBases(const std::vector<std::string>& order) {
    if (application.empty()) return false;
    std::vector<std::string> a = order, b = bases;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    if (a != b) return false;
    bases = order;
    // Every file of the application names the bases, the English first.
    for (const std::string& lang : LanguagesOnDisk()) {
        File& f = FileFor(lang, application, false);
        if (lang != "en" && lang != language && !f.exists) { files.erase(Key(lang, application, false)); continue; }
        if (f.working.bases != bases) { f.working.bases = bases; }
    }
    return true;
}

bool LocalisationWorkspace::PromoteToBase(const std::string& key, const std::string& base) {
    if (!IsBase(base) || key.empty()) return false;
    PushUndo();
    bool moved = false;
    for (const std::string& lang : LanguagesOnDisk()) {
        File& app = FileFor(lang, application, false);
        if (!HasKey(app.working, key)) { if (!app.exists && lang != language) files.erase(Key(lang, application, false)); continue; }
        File& target = FileFor(lang, base, true);
        if (!HasKey(target.working, key)) { CopyKey(app.working, target.working, key); }
        if (lang == "en") AddUnique(app.working.inherited, key);
        EraseKey(app.working, key);
        moved = true;
    }
    if (!moved) { undo.pop_back(); return false; }
    File& en = FileFor("en", application, false);
    AddUnique(en.working.inherited, key);
    return true;
}

bool LocalisationWorkspace::DemoteToApplication(const std::string& key, const std::string& base) {
    if (!IsBase(base) || key.empty()) return false;
    PushUndo();
    bool moved = false;
    for (const std::string& lang : LanguagesOnDisk()) {
        File& from = FileFor(lang, base, true);
        if (!HasKey(from.working, key)) { if (!from.exists) files.erase(Key(lang, base, true)); continue; }
        File& app = FileFor(lang, application, false);
        if (!HasKey(app.working, key)) { CopyKey(from.working, app.working, key); }
        EraseKey(from.working, key);
        moved = true;
    }
    if (!moved) { undo.pop_back(); return false; }
    File& en = FileFor("en", application, false);
    Remove(en.working.inherited, key);
    return true;
}

std::vector<std::string> LocalisationWorkspace::MergeBaseSets(const std::string& a, const std::string& b, const std::string& into) {
    std::vector<std::string> collisions;
    if (a.empty() || b.empty() || into.empty() || a == b || into == kStandard) return collisions;
    const std::vector<std::string> names = BaseSetNames();
    if (std::find(names.begin(), names.end(), a) == names.end() || std::find(names.begin(), names.end(), b) == names.end()) return collisions;
    PushUndo();
    for (const std::string& lang : LanguagesOnDisk()) {
        File& fa = FileFor(lang, a, true);
        File& fb = FileFor(lang, b, true);
        if (!fa.exists && !fb.exists && lang != "en") { files.erase(Key(lang, a, true)); files.erase(Key(lang, b, true)); continue; }
        const LocalisationCatalogue ca = fa.working, cb = fb.working;
        File& out = FileFor(lang, into, true);
        auto take = [&](const LocalisationCatalogue& from, const std::string& key) {
            if (HasKey(out.working, key)) {
                if (!SameKey(out.working, from, key) && lang == "en") AddUnique(collisions, key);
                return;
            }
            CopyKey(from, out.working, key);
        };
        if (into != a) { for (const auto& e : ca.entries) take(ca, e.first); for (const auto& p : ca.plurals) take(ca, p.first); }
        if (into != b) { for (const auto& e : cb.entries) take(cb, e.first); for (const auto& p : cb.plurals) take(cb, p.first); }
        if (out.working.language.empty()) out.working.language = lang;
        if (out.working.translatedBy.empty()) out.working.translatedBy = ca.translatedBy.empty() ? cb.translatedBy : ca.translatedBy;
    }
    // Collisions in the open language too, where the English agreed.
    if (language != "en") {
        const File* fa = FindFile(language, a, true);
        const File* fb = FindFile(language, b, true);
        if (fa && fb) {
            for (const auto& e : fa->working.entries) if (HasKey(fb->working, e.first) && !SameKey(fa->working, fb->working, e.first)) AddUnique(collisions, e.first);
            for (const auto& p : fa->working.plurals) if (HasKey(fb->working, p.first) && !SameKey(fa->working, fb->working, p.first)) AddUnique(collisions, p.first);
        }
    }
    std::vector<std::string> order;
    bool renamed = false;
    for (const std::string& name : bases) {
        const std::string n = (name == a || name == b) ? into : name;
        if (n != name) renamed = true;
        if (std::find(order.begin(), order.end(), n) == order.end()) order.push_back(n);
    }
    if (renamed) { bases = order; ReorderBases(bases); }
    return collisions;
}

// ===== UNDO =====

bool LocalisationWorkspace::Undo() {
    if (undo.empty()) return false;
    files = undo.back().files;
    bases = undo.back().bases;
    undo.pop_back();
    return true;
}

// ===== SAVING =====

bool LocalisationWorkspace::Dirty() const {
    for (const auto& [k, f] : files) if (f.Dirty()) return true;
    return false;
}

std::vector<std::string> LocalisationWorkspace::DirtyFiles() const {
    std::vector<std::string> paths;
    for (const auto& [k, f] : files) if (f.Dirty()) paths.push_back(ShippedPath(f.lang, f.set, f.base));
    std::sort(paths.begin(), paths.end());
    return paths;
}

LocalisationSaveTarget LocalisationWorkspace::DefaultSaveTarget() const {
    if (shipped.empty()) return LocalisationSaveTarget::SettingsFolder;
    std::error_code ec;
    const fs::path root = PathFromUtf8(shipped);
    if (!fs::is_directory(root, ec)) return LocalisationSaveTarget::SettingsFolder;
    const fs::path probe = root / ".ultracanvas-write-probe";
    {
        std::ofstream out(probe, std::ios::binary);
        if (!out) return LocalisationSaveTarget::SettingsFolder;
    }
    fs::remove(probe, ec);
    return LocalisationSaveTarget::SourceTree;
}

bool LocalisationWorkspace::WriteFile(const std::string& path, const std::string& text, std::string& error) {
    std::error_code ec;
    const fs::path p = PathFromUtf8(path);
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.parent_path() / PathFromUtf8(PathToUtf8(p.filename()) + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) { error = "cannot write " + path; return false; }
        out << text;
        if (!out) { error = "cannot write " + path; fs::remove(tmp, ec); return false; }
    }
    fs::rename(tmp, p, ec);
    if (ec) { error = "cannot replace " + path + ": " + ec.message(); fs::remove(tmp, ec); return false; }
    return true;
}

LocalisationCatalogue LocalisationWorkspace::Diff(const LocalisationCatalogue& shipped, const LocalisationCatalogue& working) {
    LocalisationCatalogue d;
    d.language = working.language;
    d.bases = working.bases != shipped.bases ? working.bases : std::vector<std::string>();
    for (const auto& [k, v] : working.entries) if (!SameKey(shipped, working, k)) d.entries[k] = v;
    for (const auto& [k, v] : working.plurals) if (!SameKey(shipped, working, k)) d.plurals[k] = v;
    return d;
}

void LocalisationWorkspace::StampTranslator(LocalisationCatalogue& c) const {
    if (translator.empty()) return;
    const std::string marker = "reviewed by -";
    const size_t at = c.translatedBy.find(marker);
    if (at != std::string::npos) c.translatedBy = c.translatedBy.substr(0, at) + "reviewed by " + translator;
    else if (c.translatedBy.empty()) c.translatedBy = translator;
    else if (c.translatedBy.find(translator) == std::string::npos) c.translatedBy += ", " + translator;
}

bool LocalisationWorkspace::Save(LocalisationSaveTarget target, std::string& report) {
    report.clear();
    bool ok = true;
    int written = 0;
    for (auto& [k, f] : files) {
        if (!f.Dirty()) continue;
        std::string error;
        if (target == LocalisationSaveTarget::SourceTree) {
            const std::string path = ShippedPath(f.lang, f.set, f.base);
            if (path.empty()) { report += "no shipped folder\n"; ok = false; continue; }
            if (f.working.Empty() && !f.exists && f.working.bases.empty() && f.working.inherited.empty()) { f.persisted = f.working; continue; }
            LocalisationCatalogue out = f.working;
            if (out.entries != f.shipped.entries || out.plurals != f.shipped.plurals) StampTranslator(out);
            if (out.language.empty()) out.language = f.lang;
            if (!WriteFile(path, FormatCatalogue(out), error)) { report += error + "\n"; ok = false; continue; }
            f.shipped = out;
            f.working = out;
            f.persisted = out;
            f.exists = true;
        } else {
            const std::string path = OverridePath(f.lang, f.set, f.base);
            if (path.empty()) { report += "no settings folder\n"; ok = false; continue; }
            LocalisationCatalogue d = Diff(f.shipped, f.working);
            std::error_code ec;
            if (d.Empty() && d.bases.empty()) {
                if (fs::exists(PathFromUtf8(path), ec)) { fs::remove(PathFromUtf8(path), ec); report += "removed " + path + "\n"; }
                f.persisted = f.working;
                continue;
            }
            d.nativeName = f.working.nativeName;
            d.translatedBy = translator.empty() ? f.working.translatedBy : translator;
            if (!WriteFile(path, FormatCatalogue(d), error)) { report += error + "\n"; ok = false; continue; }
            f.persisted = f.working;
            report += "wrote " + path + "\n";
        }
        ++written;
        if (target == LocalisationSaveTarget::SourceTree) report += "wrote " + ShippedPath(f.lang, f.set, f.base) + "\n";
    }
    if (written == 0 && ok && report.empty()) report = "nothing to save\n";
    return ok;
}

LocalisationCatalogue LocalisationWorkspace::MergedApplicationSet() const {
    LocalisationCatalogue out;
    out.language = language;
    if (const File* own = FindFile(language, application, false)) {
        out.nativeName = own->working.nativeName;
        out.translatedBy = own->working.translatedBy;
    }
    if (out.nativeName.empty()) if (const File* s = FindFile(language, kStandard, false)) out.nativeName = s->working.nativeName;
    out.bases = bases;
    for (const LocalisationRow& row : Rows(false)) {
        if (row.shownFrom.empty()) continue;
        auto f = row.forms.find(row.shownFrom);
        if (f != row.forms.end()) out.plurals[row.key] = f->second;
        else out.entries[row.key] = row.shown;
    }
    return out;
}

bool LocalisationWorkspace::ExportJson(const std::string& path, std::string& error) const {
    if (application.empty()) { error = "nothing open"; return false; }
    return WriteFile(path, FormatCatalogue(MergedApplicationSet()), error);
}

bool LocalisationWorkspace::ExportPo(const std::string& path, std::string& error) const {
    if (application.empty()) { error = "nothing open"; return false; }
    const LocalisationCatalogue merged = MergedApplicationSet();
    const std::vector<std::string> categories = Categories();
    auto po = [](const std::string& s) {
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else out += c;
        }
        return out + "\"";
    };
    std::string text = "msgid \"\"\nmsgstr \"\"\n\"Content-Type: text/plain; charset=UTF-8\\n\"\n\"Language: " + language + "\\n\"\n"
                       "\"Project-Id-Version: " + application + "\\n\"\n\"X-Generator: UltraCanvasLocalisationEditor\\n\"\n\n";
    std::vector<std::string> keys;
    for (const auto& e : merged.entries) keys.push_back(e.first);
    for (const auto& p : merged.plurals) keys.push_back(p.first);
    std::sort(keys.begin(), keys.end(), KeyLess);
    for (const std::string& key : keys) {
        const std::string context = ContextOf(key), source = SourceOf(key);
        if (!context.empty()) text += "msgctxt " + po(context) + "\n";
        auto p = merged.plurals.find(key);
        if (p == merged.plurals.end()) {
            text += "msgid " + po(source) + "\nmsgstr " + po(merged.entries.at(key)) + "\n\n";
            continue;
        }
        // The English plural of the key is in the English set when it is a
        // plural there; the key itself otherwise.
        std::string pluralSource = source;
        for (const auto& s : Sets()) {
            const File* en = FindFile("en", s.name, s.isBase);
            if (!en) continue;
            auto ep = en->working.plurals.find(key);
            if (ep != en->working.plurals.end()) { auto o = ep->second.find("other"); if (o != ep->second.end()) pluralSource = o->second; break; }
        }
        text += "msgid " + po(source) + "\nmsgid_plural " + po(pluralSource) + "\n";
        int i = 0;
        for (const std::string& cat : categories) {
            auto f = p->second.find(cat);
            text += "msgstr[" + std::to_string(i++) + "] " + po(f == p->second.end() ? std::string() : f->second) + "\n";
        }
        text += "\n";
    }
    return WriteFile(path, text, error);
}

// ===== THE ELEMENT =====

UltraCanvasLocalisationEditor::UltraCanvasLocalisationEditor(const std::string& id, float x, float y, float w, float h)
        : UltraCanvasContainer(id, x, y, w, h) {
    BuildUi();
}

void UltraCanvasLocalisationEditor::BuildUi() {
    this->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    SetPadding(8.0f);

    auto row = [&](const std::string& suffix) {
        auto r = std::make_shared<UltraCanvasContainer>(GetIdentifier() + "_" + suffix);
        r->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        r->layoutItem.SetFlexShrink(0);
        r->size.height = CSSLayout::Dimension::Px(32);
        AddChild(r);
        return r;
    };
    auto caption = [&](std::shared_ptr<UltraCanvasContainer>& r, const std::string& suffix, const std::string& text) {
        auto label = std::make_shared<UltraCanvasLabel>(GetIdentifier() + "_" + suffix, text);
        label->layoutItem.SetFlexShrink(0);     // the search field gives way, never a caption
        r->AddChild(label);
    };
    auto dropdown = [&](std::shared_ptr<UltraCanvasContainer>& r, const std::string& suffix, int width) {
        auto d = std::make_shared<UltraCanvasDropdown>(GetIdentifier() + "_" + suffix, 0, 0, width, 26);
        d->layoutItem.SetFlexShrink(0);
        r->AddChild(d);
        return d;
    };
    auto button = [&](std::shared_ptr<UltraCanvasContainer>& r, const std::string& suffix, const std::string& text,
                      std::function<void()> cb, float width = 96.0f) {
        auto b = std::make_shared<UltraCanvasButton>(GetIdentifier() + "_" + suffix, 0.0f, 0.0f, width, 26.0f, text);
        b->SetOnClick(std::move(cb));
        b->layoutItem.SetFlexShrink(0);
        r->AddChild(b);
        return b;
    };

    // ----- row 1: what is edited -----
    auto top = row("Top");
    caption(top, "AppCaption", Tr("Application"));
    applicationList = dropdown(top, "Application", 170);
    applicationList->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (rebuilding || index < 0 || index >= static_cast<int>(applicationNames.size())) return;
        Open(applicationNames[static_cast<size_t>(index)], workspace.Language());
    };
    caption(top, "LangCaption", Tr("Language"));
    languageList = dropdown(top, "Language", 150);
    languageList->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (rebuilding || index < 0 || index >= static_cast<int>(languageTags.size())) return;
        Open(workspace.Application(), languageTags[static_cast<size_t>(index)]);
    };
    caption(top, "CompareCaption", Tr("Compare with"));
    compareList = dropdown(top, "Compare", 120);
    compareList->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (rebuilding || index < 0 || index >= static_cast<int>(compareTags.size())) return;
        SetCompareLanguage(compareTags[static_cast<size_t>(index)]);
    };
    top->AddStretchSpacer(1);

    // ----- row 2: what is shown -----
    auto filter = row("Filter");
    searchField = std::make_shared<UltraCanvasTextInput>(GetIdentifier() + "_Search", 0, 0, 180, 26);
    searchField->SetPlaceholder(Tr("Search"));
    searchField->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    searchField->onTextChanged = [this](const std::string& text) { SetSearch(text); };
    filter->AddChild(searchField);
    caption(filter, "ShowCaption", Tr("Show"));
    statusList = dropdown(filter, "Status", 170);
    for (const auto& [value, text] : std::vector<std::pair<std::string, std::string>>{
            {"", Tr("All rows")}, {"untranslated", Tr("Untranslated")}, {"draft", Tr("Machine drafts")},
            {"override", Tr("Overrides")}, {"collision", Tr("Collisions between bases")}, {"changed", Tr("Changed since the shipped file")}}) {
        statusList->AddItem(text, value);
    }
    statusList->SetSelectedIndex(0, false);
    statusList->onSelectionChanged = [this](int, const DropdownItem& item) { if (!rebuilding) SetStatusFilter(item.value); };

    // ----- row 3: the sets, the switches, the actions -----
    auto tools = row("Tools");
    setsButton = button(tools, "Sets", Tr("Sets..."), [this]() { OpenSetsMenu(); }, 90.0f);
    setsButton->SetTooltip(Tr("The base sets the application takes, in lookup order; tick one to take it"));
    wholeBox = std::make_shared<UltraCanvasCheckbox>(GetIdentifier() + "_Whole", Tr("Show the whole sets"));
    wholeBox->onStateChanged = [this](CheckedState, CheckedState now) { if (!rebuilding) SetShowWholeSets(now == CheckedState::Checked); };
    tools->AddChild(wholeBox);
    sharedBox = std::make_shared<UltraCanvasCheckbox>(GetIdentifier() + "_Shared", Tr("Edit shared sets"));
    sharedBox->SetTooltip(Tr("A change in a base set or in Standard reaches every application that takes it"));
    sharedBox->onStateChanged = [this](CheckedState, CheckedState now) { if (!rebuilding) SetEditSharedSets(now == CheckedState::Checked); };
    tools->AddChild(sharedBox);
    tools->AddStretchSpacer(1);
    undoButton = button(tools, "Undo", Tr("Undo"), [this]() { Undo(); }, 70.0f);
    draftButton = button(tools, "Draft", Tr("Draft with UltraAI"), [this]() { Draft(selectedKey.empty()); }, 150.0f);
    draftButton->SetTooltip(Tr("Fill the selected row, or every untranslated row, with a machine draft to review"));
    exportButton = button(tools, "Export", Tr("Export..."), [this]() {
        FileDialogOptions opts;
        opts.title = Tr("Export the merged catalogue");
        opts.defaultFileName = workspace.Application() + "." + workspace.Language() + ".json";
        FileFilter json, po;
        json.description = Tr("Catalogue (JSON)");
        json.extensions = {"json"};
        po.description = Tr("gettext (PO)");
        po.extensions = {"po"};
        opts.filters = {json, po};
        UltraCanvasFileLoader::SaveFileDialog(opts, [this](DialogResult result, const std::string& path) {
            if (result == DialogResult::OK && !path.empty()) ExportTo(path);
        });
    }, 90.0f);
    saveButton = button(tools, "Save", Tr("Save"), [this]() { Save(); }, 80.0f);
    closeButton = button(tools, "Close", Tr("Close"), [this]() { if (onClose) onClose(); }, 80.0f);
    closeButton->SetVisible(false);

    // ----- the grid -----
    grid = std::make_shared<UltraCanvasColumnsTreeView>(GetIdentifier() + "_Grid");
    grid->SetDisplayMode(TreeDisplayMode::Columns);
    grid->SetShowColumnHeader(true);
    grid->SetColumnsResizable(true);
    grid->SetSelectionMode(TreeSelectionMode::Single);
    grid->SetRowHeight(22);
    grid->SetRootVisible(false);
    grid->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    grid->onNodeSelected = [this](TreeNode* n) {
        if (rebuilding) return;
        if (!n || n->data.nodeId.rfind("r:", 0) != 0) { selectedKey.clear(); selectedCategory.clear(); RefreshEditRow(); return; }
        const std::string id = n->data.nodeId.substr(2);
        const size_t hash = id.rfind('#');
        selectedKey = hash == std::string::npos ? id : id.substr(0, hash);
        selectedCategory = hash == std::string::npos ? std::string() : id.substr(hash + 1);
        RefreshEditRow();
    };
    grid->onNodeDoubleClicked = [this](TreeNode*) { if (editField) editField->SetFocus(true); };
    grid->onNodeRightClicked = [this](TreeNode* n, const UCEvent& e) { OpenRowMenu(n, e); };
    AddChild(grid);

    // ----- the edit row -----
    auto edit = row("Edit");
    keyLabel = std::make_shared<UltraCanvasLabel>(GetIdentifier() + "_Key", "");
    keyLabel->SetFontWeight(FontWeight::Bold);
    keyLabel->layoutItem.SetFlexShrink(0);
    edit->AddChild(keyLabel);
    caption(edit, "InCaption", Tr("in"));
    editSetList = dropdown(edit, "EditSet", 150);
    editSetList->onSelectionChanged = [this](int, const DropdownItem& item) { if (!rebuilding) SetEditSet(item.value); };
    editField = std::make_shared<UltraCanvasTextInput>(GetIdentifier() + "_Text", 0, 0, 300, 26);
    editField->SetPlaceholder(Tr("The translation"));
    editField->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    editField->onEnterPressed = [this](const std::string& text) { SetSelectedTranslation(text, selectedCategory); return true; };
    editField->onFocusLost = [this]() {
        if (rebuilding || !editField || selectedKey.empty()) return;
        if (editField->GetText() != workspace.Text(editSet, selectedKey, selectedCategory)) SetSelectedTranslation(editField->GetText(), selectedCategory);
    };
    edit->AddChild(editField);
    clearButton = button(edit, "Clear", Tr("Clear override"), [this]() { ClearSelectedOverride(); }, 120.0f);
    clearButton->SetTooltip(Tr("Remove the application's own text, so the row is inherited again"));

    // ----- the status line -----
    statusLabel = std::make_shared<UltraCanvasLabel>(GetIdentifier() + "_Status", "");
    statusLabel->SetTextColor(Color(90, 90, 90, 255));
    statusLabel->layoutItem.SetFlexShrink(0);
    AddChild(statusLabel);

    RebuildColumns();
}

// ===== WHAT IS EDITED =====

void UltraCanvasLocalisationEditor::SetFolders(const std::string& shippedLocaleDir, const std::string& settingsFolder) {
    workspace.SetFolders(shippedLocaleDir, settingsFolder);
    FillPickers();
}

void UltraCanvasLocalisationEditor::FillPickers() {
    rebuilding = true;
    applicationNames = workspace.Applications();
    if (!workspace.Application().empty() && std::find(applicationNames.begin(), applicationNames.end(), workspace.Application()) == applicationNames.end())
        applicationNames.insert(applicationNames.begin(), workspace.Application());
    applicationList->ClearItems();
    int appIndex = -1;
    for (size_t i = 0; i < applicationNames.size(); ++i) {
        applicationList->AddItem(applicationNames[i], applicationNames[i]);
        if (applicationNames[i] == workspace.Application()) appIndex = static_cast<int>(i);
    }
    if (appIndex >= 0) applicationList->SetSelectedIndex(appIndex, false);

    languageTags = workspace.Languages();
    if (!workspace.Language().empty() && std::find(languageTags.begin(), languageTags.end(), workspace.Language()) == languageTags.end())
        languageTags.push_back(workspace.Language());
    languageList->ClearItems();
    compareList->ClearItems();
    compareTags = languageTags;
    int langIndex = -1, compareIndex = -1;
    for (size_t i = 0; i < languageTags.size(); ++i) {
        const std::string name = UltraLocalisation::NativeName(languageTags[i]);
        const std::string label = name.empty() || name == languageTags[i] ? languageTags[i] : name + "  (" + languageTags[i] + ")";
        languageList->AddItem(label, languageTags[i]);
        compareList->AddItem(label, languageTags[i]);
        if (languageTags[i] == workspace.Language()) langIndex = static_cast<int>(i);
        if (languageTags[i] == workspace.CompareLanguage()) compareIndex = static_cast<int>(i);
    }
    if (langIndex >= 0) languageList->SetSelectedIndex(langIndex, false);
    if (compareIndex >= 0) compareList->SetSelectedIndex(compareIndex, false);
    rebuilding = false;
}

bool UltraCanvasLocalisationEditor::Open(const std::string& application, const std::string& language) {
    std::string app = application;
    if (app.empty()) {
        const std::vector<std::string> apps = workspace.Applications();
        if (!apps.empty()) app = apps.front();
    }
    const bool ok = workspace.Open(app, language);
    selectedKey.clear();
    selectedCategory.clear();
    draftedKeys.clear();
    editSet = workspace.Application();
    FillPickers();
    RebuildColumns();
    RebuildRows();
    SetStatus(ok ? TrF("{0} in {1}: {2} rows", workspace.Application(), workspace.Language(), rows.size())
                 : Tr("No application to open: no English set under the locale folder."));
    return ok;
}

void UltraCanvasLocalisationEditor::SetTranslatorName(const std::string& name) {
    workspace.SetTranslatorName(name);
}

void UltraCanvasLocalisationEditor::SetDrafter(LocalisationDrafter d) {
    drafter = std::move(d);
    if (draftButton) draftButton->SetVisible(static_cast<bool>(drafter));
}

void UltraCanvasLocalisationEditor::ShowCloseButton(bool show) {
    if (closeButton) closeButton->SetVisible(show);
}

void UltraCanvasLocalisationEditor::SetCompareLanguage(const std::string& language) {
    workspace.SetCompareLanguage(language);
    RebuildColumns();
    RebuildRows();
}

// ===== THE VIEW =====

void UltraCanvasLocalisationEditor::SetSearch(const std::string& text) {
    search = text;
    RebuildRows();
}

void UltraCanvasLocalisationEditor::SetStatusFilter(const std::string& filter) {
    statusFilter = filter;
    RebuildRows();
}

void UltraCanvasLocalisationEditor::SetShowWholeSets(bool on) {
    wholeSets = on;
    if (wholeBox && wholeBox->IsChecked() != on) { rebuilding = true; wholeBox->SetChecked(on); rebuilding = false; }
    RebuildRows();
}

void UltraCanvasLocalisationEditor::SetEditSharedSets(bool on) {
    editShared = on;
    if (sharedBox && sharedBox->IsChecked() != on) { rebuilding = true; sharedBox->SetChecked(on); rebuilding = false; }
    if (!on) editSet = workspace.Application();
    RefreshEditRow();
}

void UltraCanvasLocalisationEditor::SetColumnVisible(const std::string& set, bool visible) {
    if (visible) hiddenColumns.erase(set); else hiddenColumns.insert(set);
    RebuildColumns();
    RebuildRows();
}

bool UltraCanvasLocalisationEditor::IsColumnVisible(const std::string& set) const {
    return hiddenColumns.count(set) == 0;
}

void UltraCanvasLocalisationEditor::Rebuild() {
    RebuildColumns();
    RebuildRows();
}

void UltraCanvasLocalisationEditor::RebuildColumns() {
    if (!grid) return;
    std::vector<TreeViewColumn> columns;
    auto col = [&](const std::string& id, const std::string& title, int width, bool tree = false) {
        TreeViewColumn c;
        c.id = id;
        c.title = title;
        c.width = width;
        c.minWidth = 40;
        c.isTreeColumn = tree;
        columns.push_back(c);
    };
    col("key", Tr("Key (English)"), 0, true);
    columns.back().minWidth = 160;
    columns.back().flexWeight = 2.0f;
    col("context", Tr("Context"), 70);
    setNames.clear();
    for (const auto& s : workspace.Sets()) {
        setNames.push_back(s.name);
        if (!IsColumnVisible(s.name)) continue;
        col("set:" + s.name, s.isStandard ? Tr("Standard") : s.name, 0);
        columns.back().minWidth = 90;
    }
    if (workspace.CompareLanguage() != workspace.Language()) {
        col("compare", TrF("Compare ({0})", workspace.CompareLanguage()), 0);
        columns.back().minWidth = 90;
    }
    col("shown", Tr("Shown"), 0);
    columns.back().minWidth = 90;
    columns.back().flexWeight = 1.5f;
    col("status", Tr("Status"), 150);
    grid->SetColumns(columns);
}

bool UltraCanvasLocalisationEditor::RowPasses(const LocalisationRow& row) const {
    if (!search.empty() && !ContainsText(row.key, search) && !ContainsText(row.shown, search) && !ContainsText(row.compare, search)) {
        bool inSet = false;
        for (const auto& t : row.texts) if (ContainsText(t.second, search)) { inSet = true; break; }
        if (!inSet) return false;
    }
    if (statusFilter.empty()) return true;
    if (statusFilter == "untranslated") return row.status == LocalisationRowStatus::Untranslated;
    if (statusFilter == "draft") return row.draft || draftedKeys.count(row.key) > 0;
    if (statusFilter == "override") return row.status == LocalisationRowStatus::Override || row.status == LocalisationRowStatus::Duplicate;
    if (statusFilter == "collision") return row.status == LocalisationRowStatus::Collision;
    if (statusFilter == "changed") return row.changed;
    return true;
}

std::string UltraCanvasLocalisationEditor::NodeIdOf(const std::string& key, const std::string& category) const {
    return "r:" + key + (category.empty() ? "" : "#" + category);
}

std::string UltraCanvasLocalisationEditor::StatusLabel(const LocalisationRow& row) const {
    std::string s;
    switch (row.status) {
        case LocalisationRowStatus::Application: s = Tr("application"); break;
        case LocalisationRowStatus::Override: s = Tr("override"); break;
        case LocalisationRowStatus::Duplicate: s = Tr("duplicate: drop the override"); break;
        case LocalisationRowStatus::Base: s = TrF("base: {0}", row.shownFrom); break;
        case LocalisationRowStatus::Collision: s = TrF("base: {0} ({1} differs)", row.shownFrom, row.collidesWith); break;
        case LocalisationRowStatus::Standard: s = Tr("standard"); break;
        case LocalisationRowStatus::Untranslated: s = Tr("untranslated"); break;
    }
    if (row.plural) s += ", " + Tr("plural");
    if (row.draft || draftedKeys.count(row.key)) s += ", " + Tr("draft");
    if (row.changed) s += ", " + Tr("changed");
    return s;
}

void UltraCanvasLocalisationEditor::RebuildRows() {
    if (!grid) return;
    rebuilding = true;
    rows.clear();
    rowIndex.clear();
    for (LocalisationRow& row : workspace.Rows(wholeSets)) if (RowPasses(row)) rows.push_back(std::move(row));
    for (size_t i = 0; i < rows.size(); ++i) rowIndex[rows[i].key] = i;

    // Groups: where the key lives in English.
    enum Group { Own = 0, FromBases = 1, FromStandard = 2, Unused = 3 };
    auto groupOf = [&](const LocalisationRow& row) -> int {
        if (row.unusedSharedEntry) return Unused;
        if (row.status == LocalisationRowStatus::Application || row.status == LocalisationRowStatus::Override
            || row.status == LocalisationRowStatus::Duplicate) return Own;
        if (row.status == LocalisationRowStatus::Base || row.status == LocalisationRowStatus::Collision) return FromBases;
        if (row.status == LocalisationRowStatus::Standard) return FromStandard;
        // Untranslated: by which English set has the key.
        for (const auto& s : workspace.Sets()) {
            if (s.isApplication) continue;
            if (workspace.Has(s.name, row.key)) return s.isBase ? FromBases : FromStandard;
        }
        return Own;
    };
    const std::string groupTitles[] = {Tr("This application"), Tr("From its base sets"), Tr("From the standard set"),
                                       Tr("Unused entries of the shared sets")};
    std::vector<std::vector<size_t>> groups(4);
    for (size_t i = 0; i < rows.size(); ++i) groups[static_cast<size_t>(groupOf(rows[i]))].push_back(i);

    grid->SetRootNode(TreeNodeData("root", ""));
    const Color changedColor(0, 100, 0, 255), untranslatedColor(170, 40, 40, 255), mutedColor(120, 120, 120, 255);
    for (size_t g = 0; g < groups.size(); ++g) {
        if (groups[g].empty()) continue;
        TreeNodeData header("g:" + std::to_string(g), groupTitles[g] + "  (" + std::to_string(groups[g].size()) + ")");
        header.isGroupHeader = true;
        header.showCheckbox = false;
        grid->AddNode("root", header);
        for (size_t i : groups[g]) {
            const LocalisationRow& row = rows[i];
            TreeNodeData data(NodeIdOf(row.key), row.source);
            data.SetCell("context", row.context);
            for (const std::string& set : setNames) {
                if (!IsColumnVisible(set)) continue;
                auto t = row.texts.find(set);
                data.SetCell("set:" + set, t == row.texts.end() ? std::string() : t->second,
                             set == row.shownFrom ? Colors::Transparent : mutedColor);
            }
            data.SetCell("compare", row.compare);
            data.SetCell("shown", row.shown, row.status == LocalisationRowStatus::Untranslated ? untranslatedColor : Colors::Transparent);
            data.SetCell("status", StatusLabel(row), row.changed ? changedColor : Colors::Transparent);
            data.tooltip = row.key;
            grid->AddNode("root", data);
            if (row.plural) {
                for (const std::string& cat : row.categories) {
                    TreeNodeData form(NodeIdOf(row.key, cat), "    " + cat);
                    for (const std::string& set : setNames) {
                        if (!IsColumnVisible(set)) continue;
                        auto f = row.forms.find(set);
                        std::string text;
                        if (f != row.forms.end()) { auto c = f->second.find(cat); if (c != f->second.end()) text = c->second; }
                        form.SetCell("set:" + set, text, set == row.shownFrom ? Colors::Transparent : mutedColor);
                    }
                    auto shownForms = row.forms.find(row.shownFrom);
                    std::string shown;
                    if (shownForms != row.forms.end()) { auto c = shownForms->second.find(cat); if (c != shownForms->second.end()) shown = c->second; }
                    form.SetCell("shown", shown, shown.empty() ? untranslatedColor : Colors::Transparent);
                    grid->AddNode(NodeIdOf(row.key), form);
                }
            }
        }
    }
    grid->ExpandAll();
    if (!selectedKey.empty()) {
        if (TreeNode* n = grid->FindNode(NodeIdOf(selectedKey, selectedCategory))) grid->SelectNode(n);
        else if (TreeNode* k = grid->FindNode(NodeIdOf(selectedKey))) { selectedCategory.clear(); grid->SelectNode(k); }
        else { selectedKey.clear(); selectedCategory.clear(); }
    }
    rebuilding = false;
    RefreshEditRow();
}

void UltraCanvasLocalisationEditor::RefreshEditRow() {
    if (!keyLabel || !editField || !editSetList) return;
    rebuilding = true;
    keyLabel->SetText(selectedKey.empty() ? Tr("No row selected") : selectedKey + (selectedCategory.empty() ? "" : " [" + selectedCategory + "]"));
    editSetList->ClearItems();
    int index = 0, i = 0;
    for (const auto& s : workspace.Sets()) {
        if (!s.isApplication && !editShared) continue;
        editSetList->AddItem(s.isStandard ? Tr("Standard") : s.name, s.name);
        if (s.name == editSet) index = i;
        ++i;
    }
    if (i > 0) editSetList->SetSelectedIndex(index, false);
    if (std::find(setNames.begin(), setNames.end(), editSet) == setNames.end() || (!editShared && editSet != workspace.Application()))
        editSet = workspace.Application();
    editField->SetText(selectedKey.empty() ? std::string() : workspace.Text(editSet, selectedKey, selectedCategory));
    rebuilding = false;
}

void UltraCanvasLocalisationEditor::SetStatus(const std::string& text) {
    statusText = text;
    if (statusLabel) statusLabel->SetText(text);
    if (onStatus) onStatus(text);
}

// ===== EDITING =====

bool UltraCanvasLocalisationEditor::SelectRow(const std::string& key) {
    if (!rowIndex.count(key)) return false;
    selectedKey = key;
    selectedCategory.clear();
    if (grid) if (TreeNode* n = grid->FindNode(NodeIdOf(key))) grid->SelectNode(n);
    RefreshEditRow();
    return true;
}

void UltraCanvasLocalisationEditor::SetEditSet(const std::string& set) {
    if (set != workspace.Application() && !editShared) return;
    if (std::find(setNames.begin(), setNames.end(), set) == setNames.end()) return;
    editSet = set;
    RefreshEditRow();
}

bool UltraCanvasLocalisationEditor::SetSelectedTranslation(const std::string& text, const std::string& category) {
    if (selectedKey.empty()) { SetStatus(Tr("Select a row first.")); return false; }
    if (!workspace.SetText(editSet, selectedKey, text, category)) return false;
    draftedKeys.erase(selectedKey);     // a person's edit is no draft
    RebuildRows();
    SetStatus(text.empty() ? TrF("{0}: the entry of {1} removed", selectedKey, editSet)
                           : TrF("{0}: written into {1}", selectedKey, editSet));
    return true;
}

bool UltraCanvasLocalisationEditor::ClearSelectedOverride() {
    if (selectedKey.empty()) return false;
    const bool ok = workspace.Clear(workspace.Application(), selectedKey);
    if (ok) { RebuildRows(); SetStatus(TrF("{0}: the application's own text removed, inherited again", selectedKey)); }
    else SetStatus(TrF("{0}: the application has no text of its own for it", selectedKey));
    return ok;
}

bool UltraCanvasLocalisationEditor::Undo() {
    if (!workspace.Undo()) { SetStatus(Tr("Nothing to undo.")); return false; }
    RebuildColumns();
    RebuildRows();
    SetStatus(Tr("Undone."));
    return true;
}

bool UltraCanvasLocalisationEditor::TakeBaseSet(const std::string& base) {
    if (!workspace.TakeBaseSet(base)) { SetStatus(TrF("{0} is not a base set the application can take", base)); return false; }
    RebuildColumns();
    RebuildRows();
    int duplicates = 0;
    for (const auto& r : rows) if (r.status == LocalisationRowStatus::Duplicate) ++duplicates;
    SetStatus(duplicates ? TrF("{0} taken; {1} rows now duplicate a base's text and can be dropped", base, duplicates)
                         : TrF("{0} taken", base));
    return true;
}

bool UltraCanvasLocalisationEditor::DropBaseSet(const std::string& base) {
    if (!workspace.DropBaseSet(base)) return false;
    RebuildColumns();
    RebuildRows();
    SetStatus(TrF("{0} no longer taken", base));
    return true;
}

bool UltraCanvasLocalisationEditor::PromoteSelected(const std::string& base) {
    if (selectedKey.empty()) return false;
    const bool ok = workspace.PromoteToBase(selectedKey, base);
    if (ok) { RebuildRows(); SetStatus(TrF("{0}: moved into {1} in every language", selectedKey, base)); }
    else SetStatus(TrF("{0}: nothing of the application's to move into {1}", selectedKey, base));
    return ok;
}

bool UltraCanvasLocalisationEditor::DemoteSelected(const std::string& base) {
    if (selectedKey.empty()) return false;
    const bool ok = workspace.DemoteToApplication(selectedKey, base);
    if (ok) { RebuildRows(); SetStatus(TrF("{0}: moved from {1} into the application in every language", selectedKey, base)); }
    else SetStatus(TrF("{0}: {1} has no entry to move", selectedKey, base));
    return ok;
}

int UltraCanvasLocalisationEditor::Draft(bool everyUntranslated) {
    if (!drafter) { SetStatus(Tr("No drafting provider: configure UltraAI in the host application.")); return 0; }
    if (workspace.Language() == "en") { SetStatus(Tr("English is the source language; choose the language to draft.")); return 0; }
    struct Slot { std::string key, category, text; };
    std::vector<Slot> slots;
    auto addRow = [&](const LocalisationRow& row) {
        if (!row.plural) { slots.push_back({row.key, "", row.source}); return; }
        // The English forms of the key: the singular for "one", the plural
        // for every other form the language's rule needs.
        const std::string one = workspace.EnglishText(row.key, "one"), other = workspace.EnglishText(row.key, "other");
        for (const std::string& cat : row.categories) slots.push_back({row.key, cat, cat == "one" ? one : other});
    };
    if (!everyUntranslated) {
        auto it = rowIndex.find(selectedKey);
        if (it == rowIndex.end()) return 0;
        addRow(rows[it->second]);
    } else {
        for (const auto& row : rows) if (row.status == LocalisationRowStatus::Untranslated && row.inApplication) addRow(row);
    }
    if (slots.empty()) { SetStatus(Tr("Nothing to draft.")); return 0; }
    std::vector<std::string> texts, out;
    for (const Slot& s : slots) texts.push_back(s.text);
    std::string error;
    if (!drafter(workspace.Language(), texts, out, error) || out.size() != texts.size()) {
        SetStatus(TrF("Drafting failed: {0}", error.empty() ? Tr("wrong number of results") : error));
        return 0;
    }
    int written = 0;
    for (size_t i = 0; i < slots.size(); ++i) {
        if (out[i].empty()) continue;
        if (workspace.SetText(workspace.Application(), slots[i].key, out[i], slots[i].category)) { draftedKeys.insert(slots[i].key); ++written; }
    }
    if (!everyUntranslated && !slots.empty()) selectedKey = slots.front().key;
    RebuildRows();
    SetStatus(TrF("{0} rows drafted; review them before saving", written));
    return written;
}

bool UltraCanvasLocalisationEditor::Save() {
    return Save(workspace.DefaultSaveTarget());
}

bool UltraCanvasLocalisationEditor::Save(LocalisationSaveTarget target) {
    std::string report;
    const bool ok = workspace.Save(target, report);
    while (!report.empty() && report.back() == '\n') report.pop_back();
    const std::string where = target == LocalisationSaveTarget::SourceTree ? Tr("into the source tree") : Tr("into the settings folder");
    SetStatus((ok ? TrF("Saved {0}: {1}", where, report) : TrF("Not saved {0}: {1}", where, report)));
    RebuildRows();
    if (ok && onSaved) onSaved(report);
    return ok;
}

bool UltraCanvasLocalisationEditor::ExportTo(const std::string& path) {
    std::string error;
    const bool po = path.size() > 3 && Lower(path.substr(path.size() - 3)) == ".po";
    const bool ok = po ? workspace.ExportPo(path, error) : workspace.ExportJson(path, error);
    SetStatus(ok ? TrF("Exported to {0}", path) : TrF("Export failed: {0}", error));
    return ok;
}

// ===== MENUS =====

void UltraCanvasLocalisationEditor::OpenSetsMenu() {
    UltraCanvasWindowBase* window = GetWindow();
    if (!window || !setsButton) return;
    if (openMenu) openMenu->CloseMenu();
    auto menu = std::make_shared<UltraCanvasMenu>(GetIdentifier() + ".sets", 0, 0, 240, 0);
    menu->SetMenuType(MenuType::PopupMenu);
    const std::vector<std::string>& taken = workspace.Bases();
    for (const std::string& name : workspace.BaseSetNames()) {
        const bool on = std::find(taken.begin(), taken.end(), name) != taken.end();
        menu->AddItem(MenuItemData::Checkbox(name, on, [this, name](bool checked) {
            if (checked) TakeBaseSet(name); else DropBaseSet(name);
        }));
    }
    menu->AddItem(MenuItemData::Separator());
    menu->AddItem(MenuItemData::Checkbox(Tr("Standard column"), IsColumnVisible(kStandard), [this](bool on) { SetColumnVisible(kStandard, on); }));
    openMenu = menu;
    PopupElementSettings settings;
    settings.popupOwner = setsButton->weak_from_this();
    menu->OpenMenu(Point2Di(static_cast<int>(setsButton->GetXInWindow()),
                            static_cast<int>(setsButton->GetYInWindow() + setsButton->GetBounds().height)), *window, settings);
}

void UltraCanvasLocalisationEditor::OpenRowMenu(TreeNode* node, const UCEvent& event) {
    UltraCanvasWindowBase* window = GetWindow();
    if (!window || !node || node->data.nodeId.rfind("r:", 0) != 0) return;
    const std::string id = node->data.nodeId.substr(2);
    const size_t hash = id.rfind('#');
    selectedKey = hash == std::string::npos ? id : id.substr(0, hash);
    selectedCategory.clear();
    RefreshEditRow();
    auto it = rowIndex.find(selectedKey);
    if (it == rowIndex.end()) return;
    const LocalisationRow& row = rows[it->second];
    if (openMenu) openMenu->CloseMenu();
    auto menu = std::make_shared<UltraCanvasMenu>(GetIdentifier() + ".row", 0, 0, 260, 0);
    menu->SetMenuType(MenuType::PopupMenu);
    const std::vector<std::string> taken = workspace.Bases();
    if (!taken.empty() && (row.status == LocalisationRowStatus::Application || row.status == LocalisationRowStatus::Override
                           || row.status == LocalisationRowStatus::Duplicate || row.status == LocalisationRowStatus::Untranslated)) {
        std::vector<MenuItemData> targets;
        for (const std::string& b : taken) targets.push_back(MenuItemData::Action(b, [this, b]() { PromoteSelected(b); }));
        menu->AddItem(MenuItemData::Submenu(Tr("Promote to a base set"), targets));
    }
    if (row.status == LocalisationRowStatus::Base || row.status == LocalisationRowStatus::Collision) {
        const std::string from = row.shownFrom;
        menu->AddItem(MenuItemData::Action(TrF("Demote to the application (from {0})", from), [this, from]() { DemoteSelected(from); }));
    }
    if (row.status == LocalisationRowStatus::Override || row.status == LocalisationRowStatus::Duplicate) {
        menu->AddItem(MenuItemData::Action(Tr("Clear the override"), [this]() { ClearSelectedOverride(); }));
    }
    if (drafter) menu->AddItem(MenuItemData::Action(Tr("Draft this row with UltraAI"), [this]() { Draft(false); }));
    openMenu = menu;
    PopupElementSettings settings;
    settings.popupOwner = grid->weak_from_this();
    menu->OpenMenu(event.pointerWindow, *window, settings);
}

// ===== THE DIALOG =====

std::shared_ptr<UltraCanvasWindowBase> ShowLocalisationEditorDialog(
        UltraCanvasWindowBase* parent, const std::string& application, const std::string& language,
        const std::string& title, int width, int height) {
    WindowConfig wc;
    wc.title = title.empty() ? TrF("Translate {0}", application) : title;
    wc.width = width;
    wc.height = height;
    wc.resizable = true;
    wc.type = WindowType::Dialog;
    wc.parentWindow = parent;
    auto window = CreateWindow(wc);
    if (!window || !window->IsCreated()) return nullptr;
    window->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto editor = std::make_shared<UltraCanvasLocalisationEditor>("LocalisationEditorDialog");
    editor->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    const std::string settings = PathToUtf8(UltraCanvasSettingsFolder());
    editor->SetFolders(UltraLocalisation::ShippedFolder().empty() ? GetResourcesDir() + "media/locale" : UltraLocalisation::ShippedFolder(),
                       UltraLocalisation::SettingsFolder().empty() ? settings : UltraLocalisation::SettingsFolder());
    editor->SetTranslatorName(LanguageSettings::Load(settings).translator);
    editor->Open(application.empty() ? UltraLocalisation::CurrentApplication() : application,
                 language.empty() ? UltraLocalisation::CurrentLanguage() : language);
    UltraCanvasWindow* win = window.get();
    editor->onClose = [win]() { win->Close(); };
    editor->ShowCloseButton(true);
    window->AddChild(editor);
    window->Show();
    return window;
}

} // namespace UltraCanvas
