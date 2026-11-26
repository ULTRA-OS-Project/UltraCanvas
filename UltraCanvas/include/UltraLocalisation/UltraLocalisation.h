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
// Numbers, dates and times in the person's conventions (phase L6):
// FormatNumber, FormatInteger, FormatPercent, FormatFileSize, FormatDate,
// FormatTime, FormatDateTime, from the platform's regional settings or the
// built-in CLDR conventions of the language, with the month and weekday
// names of the standard set. TrF writes a number argument in the language's
// digits and decimal separator, ungrouped, since an argument is as often a
// year, a port or a line number as a quantity; TrN groups its count, which
// is always a quantity.
//
// doc-check: int count = 3; int done = 1;
// doc-check: example
//   std::string save = UltraCanvas::Tr("Save");                                      // "Speichern"
//   std::string open = UltraCanvas::TrC("button", "Open");                           // the "button|Open" entry
//   std::string files = UltraCanvas::TrN("{n} file", "{n} files", count);            // "3 Dateien"
//   std::string progress = UltraCanvas::TrF("Deleted {0} of {1}", done, count);      // "1 von 3 gelöscht"
//   std::string language = UltraCanvas::UltraLocalisation::CurrentLanguage();        // "de"
//   std::string size = UltraCanvas::UltraLocalisation::FormatFileSize(1258291);      // "1,2 MB"
//   std::string day = UltraCanvas::UltraLocalisation::FormatDate(std::time(nullptr), UltraCanvas::DateStyle::Long);   // "21. Oktober 2026"
//
// Version: 1.1.0 - phase L6: numbers, dates and times in the person's conventions; DateStyle::Medium,
//                  FormatMonthYear / FormatMonthDay, FormatFileSize with decimals, ExamplesFor
// Version: 1.0.0 - phase L1: the three layers, the fallback walk, plurals, Language.conf
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework
#pragma once

#include <ctime>
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
        // The text an argument becomes: strings as they are, integers in the
        // language's digits, ungrouped, floating point with its decimal
        // separator, at most six decimals with the trailing zeros dropped.
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

    // ===== NUMBERS, DATES AND TIMES =====

    // How a date is written. Short is all digits with the year in full
    // (21.10.2026, 10/21/2026, 2026-10-21); Medium abbreviates the month
    // (21. Okt. 2026, Oct 21, 2026); Long names it (21. Oktober 2026); Full
    // adds the weekday (Mittwoch, 21. Oktober 2026).
    enum class DateStyle { Short, Medium, Long, Full };

    // A clock time without (14:05, 2:05 PM) or with its seconds.
    enum class TimeStyle { Short, Medium };

    // The shape of numbers, dates and times in one region: what the
    // Format... calls write with. The platform's regional settings give it
    // where it has them (SetRegionalFormats); FormatsFor has the conventions
    // of every language UltraCanvas ships and of the common regional
    // variants, from CLDR. Date and time patterns are CLDR (LDML) patterns:
    // d dd M MM MMM MMMM LLL LLLL y yy EEE EEEE H HH h hh m mm s ss a, text
    // in quotes. The month and weekday names and the AM/PM words come from
    // the standard set in the person's language, not from the region: MMMM
    // date-month|October and MMM date-month-short|Oct (as written inside a
    // date), LLLL month|October and LLL month-short|Oct (on their own, as a
    // heading), EEEE date-weekday|Monday, EEE weekday|Mo, a time|AM.
    struct LocaleFormats {
        std::string language = "en";             // the tag these describe: "de-DE", "en-GB"
        std::string decimalSeparator = ".";
        std::string groupSeparator = ",";        // "" for none
        int groupSize = 3;                       // the group nearest the decimal separator
        int secondaryGroupSize = 3;              // every group before it: 2 in India
        int minimumGroupingDigits = 1;           // 2: 1234 stays whole and 12 345 is grouped (es, pl, pt-PT)
        std::string digits;                      // the ten digits 0-9, UTF-8; "" for 0123456789
        std::string percentPattern = "{0}%";     // "{0} %", "%{0}"
        std::string shortDate = "M/d/y";
        std::string longDate = "MMMM d, y";
        std::string fullDate = "EEEE, MMMM d, y";
        std::string mediumDate = "MMM d, y";     // the month abbreviated: "Oct 21, 2026"
        std::string monthYear = "LLL y";         // a month on its own: "Oct 2026"
        std::string monthDay = "MMM d";          // a day without its year: "Oct 21"
        std::string shortTime = "h:mm a";
        std::string mediumTime = "h:mm:ss a";
        std::string dateTimePattern = "{1}, {0}";   // {1} the date, {0} the time

        bool operator==(const LocaleFormats&) const = default;
    };

    // A date and a wall-clock time, as a calendar shows them.
    struct CalendarTime {
        int year = 1970;
        int month = 1;          // 1-12
        int day = 1;            // 1-31
        int weekday = -1;       // 0 Sunday - 6 Saturday; -1 to work it out from the date
        int hour = 0;           // 0-23
        int minute = 0;
        int second = 0;
    };

    // One language setting's numbers and dates, written out: what a
    // settings page shows as a preview before the setting is used.
    struct LocaleFormatExamples {
        std::string formats;       // the tag of the conventions used: "de-DE"
        bool fromPlatform = false; // the platform's regional settings, not the built-in ones
        std::string number;        // 1234567.891 with two decimals: "1.234.567,89"
        std::string percent;       // 0.125: "12,5 %"
        std::string fileSize;      // 1258291 bytes: "1,2 MB"
        std::string shortDate;     // "26.11.2026"
        std::string fullDate;      // "Donnerstag, 26. November 2026"
        std::string shortTime;     // "14:05"
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

        // ----- Numbers, dates and times in the person's conventions -----
        // Text a person reads. A file format or a protocol writes its numbers
        // with FormatFloatClassic instead, whatever the language.

        // 1.234,5 - exactly `decimals` digits after the separator, rounded.
        static std::string FormatNumber(double value, int decimals);
        // 1.234 - grouped.
        static std::string FormatInteger(long long value);
        // 12,5 % from 0.125.
        static std::string FormatPercent(double fraction, int decimals);
        // 512 Bytes, 1,2 KB, 3,4 MB: 1024 bytes to the kilobyte, one decimal,
        // the unit names from the standard set.
        static std::string FormatFileSize(unsigned long long bytes);
        // The same with `decimals` digits after the separator from a kilobyte
        // up - 0 for "96 MB" - or -1 for three significant digits: "1,23 GB",
        // "12,3 GB", "123 GB", as an information panel or a progress line
        // writes them.
        static std::string FormatFileSize(unsigned long long bytes, int decimals);
        // In the local time zone.
        static std::string FormatDate(std::time_t when, DateStyle style);
        static std::string FormatTime(std::time_t when, TimeStyle style);
        static std::string FormatDateTime(std::time_t when, DateStyle date, TimeStyle time);
        // The same for a date and time given as a calendar shows them.
        static std::string FormatDate(const CalendarTime& when, DateStyle style);
        static std::string FormatTime(const CalendarTime& when, TimeStyle style);
        static std::string FormatDateTime(const CalendarTime& when, DateStyle date, TimeStyle time);
        // A month on its own and a day without its year, as a chart's or a
        // calendar's heading writes them: "Okt 2026", "21. Okt.".
        static std::string FormatMonthYear(std::time_t when);
        static std::string FormatMonthYear(const CalendarTime& when);
        static std::string FormatMonthDay(std::time_t when);
        static std::string FormatMonthDay(const CalendarTime& when);
        // Any CLDR pattern - "EEEE", "MMMM y" - with the person's names and digits.
        static std::string FormatPattern(const CalendarTime& when, const std::string& pattern);
        // What a numeric field accepts and shows: "," on a German desktop.
        static std::string DecimalSeparator();
        static std::string GroupSeparator();

        // The formats the calls above use: the platform's regional settings
        // when it gave them and they are for the language shown - or the
        // language came from the desktop, so the region is the person's
        // choice too - else FormatsFor the language, its regional tag kept
        // ("en-GB" although the catalogue is "en").
        static LocaleFormats Formats();
        // The built-in conventions for a tag, walked down ("de-CH" has its
        // own, "de-LU" is "de"); English for a language it does not know.
        // Pure.
        static LocaleFormats FormatsFor(const std::string& language);
        // What the platform says, from SystemRegionalFormats() in the
        // application base; a LocaleFormats with an empty language drops it.
        static void SetRegionalFormats(const LocaleFormats& formats);
        static LocaleFormats RegionalFormats();

        // The local time a std::time_t is, the weekday filled in.
        static CalendarTime LocalTime(std::time_t when);
        // A strftime format ("%d.%m.%Y", "%I:%M:%S %p") and a Windows
        // date/time picture ("dddd, d. MMMM yyyy", "h:mm tt") as a CLDR
        // pattern, for the platform readers. Pure.
        static std::string PatternFromStrftime(const std::string& format);
        static std::string PatternFromWindows(const std::string& picture);
        // A time pattern without its seconds: "HH:mm:ss" -> "HH:mm",
        // "h:mm:ss a" -> "h:mm a". Pure.
        static std::string PatternWithoutSeconds(const std::string& pattern);
        // A two-digit year as the year in full: "dd.MM.yy" -> "dd.MM.y", for
        // a platform's short date where DateStyle::Short writes the year in
        // full. Pure.
        static std::string PatternWithFullYear(const std::string& pattern);
        // The examples a language would show at `when` (local time): in its
        // standard set's words and the conventions it would get - the
        // platform's regional settings when they are for that language, or
        // when `fromDesktop` (the setting is "System"), else FormatsFor it.
        // The loaded language is left as it is.
        static LocaleFormatExamples ExamplesFor(const std::string& language, std::time_t when, bool fromDesktop = false);

        // Everything back to the start: no folders, no sets, English. For
        // tests.
        static void Reset();
    };

}  // namespace UltraCanvas
