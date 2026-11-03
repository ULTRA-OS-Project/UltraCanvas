// include/UltraLocalisation/UltraLocalisation.h
// UltraLocalisation - the text of every UltraCanvas application in the
// person's language. Tr("Save") gives the translation of the English text
// written at the site, from three layers of catalogues:
//
//   the application's own set        media/locale/<lang>/<App>.json
//   the base sets of its kind        media/locale/<lang>/base/Video.json ...
//   the standard set, every app's    media/locale/<lang>/Standard.json
//
// looked up in that order, the English itself when none has it, so an
// untranslated string is always shown, never empty. A file of the same
// name under <settings>/locale/<lang>/ - the UltraCanvas settings folder,
// where a translator drops a draft and the editor saves on a packaged
// build - is read before the shipped one. A regional tag walks down
// ("zh-Hant-TW" -> "zh-Hant" -> "zh") and the more specific file wins per
// key. The design is Docs/UltraCanvas/UltraCanvasLocalisationProposal.md;
// this is its phase L1.
//
// UI-free and self-contained (the vendored JSON engine and the header-only
// path helpers, nothing of the core), so a headless engine links it as it
// links UltraVault, and the shared core absorbs it whole so an application
// and its plugins share one loaded set. The folders and the desktop's
// languages are handed in - SetFolders, Initialize - because the module
// knows neither the resources dir nor the settings folder; the application
// base passes them in Initialize(), a test passes a temporary folder.
//
// Keys are the English text, with an optional context: TrC("button",
// "Open") is the entry "button|Open". Plurals are TrN(singular, plural,
// count), one entry keyed on the singular whose translation is an object
// of CLDR categories ("one", "few", "many", "other"); "{n}" in it is the
// count. Placeholders are "{0}", "{1}", ... filled by TrF.
//
// doc-check: int count = 3; int done = 1;
// doc-check: example
//   std::string save = UltraCanvas::Tr("Save");                                      // "Speichern"
//   std::string open = UltraCanvas::TrC("button", "Open");                           // the "button|Open" entry
//   std::string files = UltraCanvas::TrN("{n} file", "{n} files", count);            // "3 Dateien"
//   std::string progress = UltraCanvas::TrF("Deleted {0} of {1}", done, count);      // "1 von 3 gelöscht"
//   std::string language = UltraCanvas::UltraLocalisation::CurrentLanguage();        // "de"
//
// Version: 1.0.0 - phase L1: the three layers, the fallback walk, plurals, Language.conf
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace UltraCanvas {

    // ===== THE CALLS AT THE SITE =====

    // The text for `source` in the current language; `source` itself when no
    // set has it.
    std::string Tr(std::string_view source);

    // The same, disambiguated: "button|Open" and "state|Open" are two entries.
    std::string TrC(std::string_view context, std::string_view source);

    // Singular or plural by `count`, "{n}" replaced by the count. The entry
    // is keyed on `singular`; a language with more forms than two lists them
    // under the CLDR category names. Untranslated, the English singular for
    // 1 and the plural otherwise.
    std::string TrN(std::string_view singular, std::string_view plural, long long count);

    namespace LocalisationDetail {
        std::string Substitute(std::string_view text, const std::vector<std::string>& args);
        // The text an argument becomes: strings as they are, integers as
        // digits, floating point dot-decimal with the trailing zeros dropped
        // (the locale's own formatting is phase L6 of the proposal).
        std::string ArgText(std::string_view value);
        inline std::string ArgText(const std::string& value) { return value; }
        inline std::string ArgText(const char* value) { return value ? std::string(value) : std::string(); }
        std::string ArgText(long long value);
        std::string ArgText(unsigned long long value);
        std::string ArgText(double value);
        inline std::string ArgText(bool value) { return value ? "true" : "false"; }
        template <typename T>
        std::string ArgTextOf(const T& value) {
            if constexpr (std::is_same_v<T, bool>) return ArgText(static_cast<bool>(value));
            else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) return ArgText(static_cast<long long>(value));
            else if constexpr (std::is_integral_v<T>) return ArgText(static_cast<unsigned long long>(value));
            else if constexpr (std::is_floating_point_v<T>) return ArgText(static_cast<double>(value));
            else return ArgText(value);
        }
    }

    // Tr with "{0}", "{1}", ... replaced by the arguments in order. A
    // translation may use them in any order, or leave one out.
    template <typename... Args>
    std::string TrF(std::string_view source, const Args&... args) {
        return LocalisationDetail::Substitute(Tr(source), {LocalisationDetail::ArgTextOf(args)...});
    }

    // ===== THE CATALOGUE FILE =====

    // Which layer answered a key - for the editor's marking and for a
    // start-up report of what an application still shows in English.
    enum class LocalisationOrigin { Application, Library, Base, Standard, Source };

    // One catalogue file: the entries of one set in one language. Keys are
    // the source text (with its context prefix); a plural entry's forms are
    // keyed by CLDR category. Metadata keys start with '@'.
    struct LocalisationCatalogue {
        std::string language;                              // "@language"
        std::string nativeName;                            // "@name": "Deutsch"
        std::string sourceLanguage;                        // "@source-language": "de" for a program written in German
        std::string translatedBy;                          // "@translated-by"
        std::vector<std::string> bases;                    // "@bases": the base sets an application takes, most specific first
        std::vector<std::string> inherited;                // "@inherited": keys a set below has, listed by the extraction
        std::map<std::string, std::string> entries;        // key -> translation
        std::map<std::string, std::map<std::string, std::string>> plurals;   // key -> category -> form

        // The file's text and back. Parse keeps what it does not know out
        // and says why in `error` when the text is not a JSON object.
        static bool Parse(const std::string& text, LocalisationCatalogue& out, std::string& error);
        std::string Serialize() const;   // pretty, keys in order, metadata first
        bool Empty() const { return entries.empty() && plurals.empty(); }
    };

    // ===== THE LANGUAGE SETTING =====

    // Language.conf in the UltraCanvas settings folder, shared by every
    // application and written by UOS-Settings: one language for all, and
    // the translator's name the editor writes into a catalogue.
    struct LanguageSettings {
        std::string language = "system";   // "system", or a tag: "de", "pt-BR"
        std::string translator;            // for "@translated-by"

        bool operator==(const LanguageSettings&) const = default;
        static LanguageSettings Parse(const std::string& text);
        std::string Serialize() const;
        // <settingsFolder>/Language.conf; empty when the folder is empty.
        static std::string FilePath(const std::string& settingsFolder);
        // A missing or unreadable file is the defaults.
        static LanguageSettings Load(const std::string& settingsFolder);
        // Written beside the file and renamed over it.
        bool Save(const std::string& settingsFolder) const;
    };

    // ===== THE MODULE =====

    class UltraLocalisation {
    public:
        // Where the catalogues are: the shipped tree (GetResourcesDir() +
        // "media/locale/" in an application) and the UltraCanvas settings
        // folder, whose locale/ holds the overrides and which holds
        // Language.conf. UTF-8, with or without a trailing separator; either
        // may be empty.
        static void SetFolders(const std::string& shippedLocaleDir, const std::string& settingsFolder);
        static std::string ShippedFolder();
        static std::string SettingsFolder();

        // A language given on the command line (--lang=de), which beats
        // everything; empty to drop it.
        static void SetLanguageOverride(const std::string& language);

        // The language to load, first answer wins: the override, the
        // ULTRACANVAS_LANG variable, Language.conf, the first of
        // `systemLanguages` (the desktop's, best first) that a catalogue
        // exists for - walking "de-AT" down to "de" - and "en".
        static std::string ChooseLanguage(const std::vector<std::string>& systemLanguages);

        // ChooseLanguage, then Load. What UltraCanvasApplicationBase::Initialize
        // calls with the application's name and SystemLanguageTags().
        static bool Initialize(const std::string& application, const std::vector<std::string>& systemLanguages);

        // Loads the sets for the language: the application's, the library
        // sets added with AddSet, its base sets (UseBaseSets, else the
        // "@bases" of its catalogue), the standard set - each from the
        // settings folder over the shipped tree, each tag of the fallback
        // walk, the more specific first. True when the language is English
        // or any set was found; false, with everything English, otherwise.
        static bool Load(const std::string& language, const std::string& application);

        // Load for the current application, then every change handler.
        static bool SetLanguage(const std::string& language);
        static std::string CurrentLanguage();
        static std::string CurrentApplication();

        using ChangeHandler = std::function<void(const std::string& language)>;
        static int AddChangeHandler(ChangeHandler handler);
        static void RemoveChangeHandler(int id);

        // The languages a catalogue exists for in either folder (a folder
        // under locale/ with any .json in it), "en" always, sorted.
        static std::vector<std::string> AvailableLanguages();
        // The language's own name for itself from its standard set's
        // "@name"; the tag when it has none.
        static std::string NativeName(const std::string& language);

        // The base sets the application takes, most specific first - set
        // here for an application without a catalogue, read from its
        // catalogue's "@bases" otherwise. Takes effect at the next Load.
        static void UseBaseSets(const std::vector<std::string>& bases);
        static std::vector<std::string> BaseSets();
        // A library's own set (a plugin with strings of its own), looked up
        // after the application's and before its bases. Takes effect at the
        // next Load.
        static void AddSet(const std::string& name);
        static std::vector<std::string> LibrarySets();

        // Which layer has `source` in the current language.
        static LocalisationOrigin OriginOf(std::string_view source);
        // Every key the standard set knows in the current language.
        static std::vector<std::string> StandardKeys();
        // How many entries each loaded layer has, for a start-up report.
        static size_t EntryCount(LocalisationOrigin origin);

        // The CLDR plural category of an integer count in a language:
        // "zero", "one", "two", "few", "many" or "other". Pure.
        static std::string PluralCategory(const std::string& language, long long count);

        // Reads one catalogue file as Load does; false when it is not there
        // or not a catalogue.
        static bool ReadCatalogue(const std::string& path, LocalisationCatalogue& out, std::string& error);

        // Everything back to the start: no folders, no sets, English. For
        // tests.
        static void Reset();
    };

}  // namespace UltraCanvas
