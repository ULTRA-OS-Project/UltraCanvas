// Tests/SystemLanguageTest.cpp
// The desktop-language detector without a desktop: the POSIX locale and
// LANGUAGE rules, the canonical spelling of a tag, tag equality, and that
// the live detector answers without a display; the regional formats, and on
// Linux their reading of LC_TIME and LC_NUMERIC.
// Version: 1.1.0 - SystemRegionalFormats
// Version: 1.0.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework
#include "UltraCanvasSystemLanguage.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0, failCount = 0;

static void Check(bool ok, const char* what) {
    ++testCount;
    if (!ok) { ++failCount; std::printf("FAIL: %s\n", what); }
}

static bool Same(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    return a == b;
}

int main() {
    // The POSIX locale, in every spelling it comes in
    Check(LanguageTagFromPosixLocale("de_DE.UTF-8") == "de-DE", "charset dropped");
    Check(LanguageTagFromPosixLocale("de_DE.UTF-8@euro") == "de-DE", "modifier dropped");
    Check(LanguageTagFromPosixLocale("sr_RS@latin") == "sr-RS", "modifier without charset");
    Check(LanguageTagFromPosixLocale("de") == "de", "language alone");
    Check(LanguageTagFromPosixLocale("zh_hant_tw") == "zh-Hant-TW", "script title case, region upper");
    Check(LanguageTagFromPosixLocale("EN_us") == "en-US", "case folded");
    Check(LanguageTagFromPosixLocale("es-419") == "es-419", "numeric region kept");
    Check(LanguageTagFromPosixLocale("C").empty(), "C names no language");
    Check(LanguageTagFromPosixLocale("POSIX").empty(), "POSIX names no language");
    Check(LanguageTagFromPosixLocale("C.UTF-8").empty(), "C.UTF-8 names no language");
    Check(LanguageTagFromPosixLocale("").empty(), "empty");

    // The four variables, as gettext reads them
    Check(Same(LanguageTagsFromEnvironment("", "", "", "de_DE.UTF-8"), {"de-DE"}), "LANG alone");
    Check(Same(LanguageTagsFromEnvironment("", "fr_FR", "de_DE", "en_US"), {"fr-FR"}), "LC_ALL wins");
    Check(Same(LanguageTagsFromEnvironment("", "", "de_DE", "en_US"), {"de-DE"}), "LC_MESSAGES over LANG");
    Check(Same(LanguageTagsFromEnvironment("de_AT:de:en", "", "", "de_DE.UTF-8"), {"de-AT", "de", "en", "de-DE"}),
          "LANGUAGE list first, then the locale");
    Check(Same(LanguageTagsFromEnvironment("de_AT:de", "", "", "C"), {}), "LANGUAGE ignored under C");
    Check(Same(LanguageTagsFromEnvironment("de:de_DE", "", "", "de_DE"), {"de", "de-DE"}), "each tag once");
    Check(Same(LanguageTagsFromEnvironment("", "", "", ""), {}), "nothing set");

    // Tags
    Check(LanguageTagBase("de-AT") == "de", "base of a region tag");
    Check(LanguageTagBase("zh-Hant-TW") == "zh", "base of a script tag");
    Check(LanguageTagBase("en") == "en", "base of a language");
    Check(LanguageTagsEqual("de_AT", "de-at"), "separator and case");
    Check(LanguageTagsEqual("de-AT", "DE-AT"), "case");
    Check(!LanguageTagsEqual("de", "de-AT"), "a language is not its region");
    Check(!LanguageTagsEqual("en", "eng"), "whole components");

    // The live detector answers, whatever the machine
    const std::vector<std::string> live = SystemLanguageTags();
    for (const std::string& tag : live) Check(!tag.empty() && tag.find('_') == std::string::npos, "live tags canonical");
    Check(!SystemLanguage().empty(), "a language always");
    Check(live.empty() ? SystemLanguage() == "en" : SystemLanguage() == live.front(), "the first tag, or en");

    // The region's formats answer too: a tag, or nothing at all
    const LocaleFormats region = SystemRegionalFormats();
    Check(region.language.empty() || !region.decimalSeparator.empty(), "regional formats: a tag with its separators, or none");

#if defined(__linux__) && !defined(__ANDROID__)
    // On Linux the region is LC_TIME and LC_NUMERIC, under LC_ALL, over LANG
    {
        const char* names[] = {"LC_ALL", "LC_TIME", "LC_NUMERIC", "LANG"};
        std::string saved[4];
        bool had[4];
        for (int i = 0; i < 4; ++i) {
            const char* value = std::getenv(names[i]);
            had[i] = value != nullptr;
            saved[i] = value ? value : "";
        }
        unsetenv("LC_ALL");
        setenv("LANG", "en_US.UTF-8", 1);
        setenv("LC_TIME", "de_DE.UTF-8", 1);
        setenv("LC_NUMERIC", "fr_FR.UTF-8", 1);
        LocaleFormats split = SystemRegionalFormats();
        Check(split.language == "de-DE" && split.shortDate == "dd.MM.y", "LC_TIME names the dates' region");
        Check(split.decimalSeparator == "," && split.groupSeparator == "\xE2\x80\xAF", "LC_NUMERIC the numbers'");
        setenv("LC_ALL", "en_GB.UTF-8", 1);
        split = SystemRegionalFormats();
        Check(split.language == "en-GB" && split.shortDate == "dd/MM/y" && split.decimalSeparator == ".", "LC_ALL over both");
        setenv("LC_ALL", "C", 1);
        Check(SystemRegionalFormats().language.empty(), "the C locale names no region");
        for (int i = 0; i < 4; ++i) {
            if (had[i]) setenv(names[i], saved[i].c_str(), 1);
            else unsetenv(names[i]);
        }
    }
#endif

    std::printf("SystemLanguageTest: %d checks, %d failed\n", testCount, failCount);
    return failCount == 0 ? 0 : 1;
}
