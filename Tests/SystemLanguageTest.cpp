// Tests/SystemLanguageTest.cpp
// The desktop-language detector without a desktop: the POSIX locale and
// LANGUAGE rules, the canonical spelling of a tag, tag equality, and that
// the live detector answers without a display.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#include "UltraCanvasSystemLanguage.h"

#include <cstdio>
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

    std::printf("SystemLanguageTest: %d checks, %d failed\n", testCount, failCount);
    return failCount == 0 ? 0 : 1;
}
