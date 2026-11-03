// include/UltraCanvasSystemLanguage.h
// The languages the person's desktop is set to, best first, as BCP 47 tags
// ("de-AT", "en", "zh-Hant-TW"): what a first run picks a spelling
// dictionary by today, and what a translation catalogue, a date format and a
// localised desktop entry pick by next. One detector for every caller, so
// the spell checker, the desktop-entry reader and the localisation module
// (Docs/UltraCanvas/UltraCanvasLocalisationProposal.md) agree on whose
// language it is.
//
// Two sources, in this order:
//   1. The POSIX variables, read as gettext reads them: LC_ALL, else
//      LC_MESSAGES, else LANG names the locale; when that locale is not C or
//      POSIX, LANGUAGE (a colon-separated priority list) is honoured first.
//      "de_DE.UTF-8@euro" becomes "de-DE".
//   2. The platform's own answer, SystemLanguageTagsNative(), defined under
//      OS/<Platform>/: the user's preferred UI languages and default locale
//      on Windows, where the POSIX variables are normally unset;
//      NSLocale.preferredLanguages on macOS; navigator.languages in a
//      browser; the device configuration on Android. Nothing on Linux and
//      the BSDs, where the variables are the desktop's own answer.
// Each tag once, the environment's first. Empty when nothing is set at all.
//
// Pure helpers for the parsing, so a test covers every platform's rule
// anywhere: LanguageTagFromPosixLocale, LanguageTagsFromEnvironment,
// LanguageTagBase, LanguageTagsEqual.
//
// doc-check: example
//   const std::vector<std::string> wanted = UltraCanvas::SystemLanguageTags();   // {"de-AT", "de", "en"}
//   const std::string language = UltraCanvas::SystemLanguage();                 // "de-AT", or "en" when nothing is set
//   const std::string base = UltraCanvas::LanguageTagBase(language);             // "de"
//   const bool same = UltraCanvas::LanguageTagsEqual("de_AT", "de-at");          // true
//
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>

namespace UltraCanvas {

    // "de_DE.UTF-8@euro" -> "de-DE", "zh_hant_tw" -> "zh-Hant-TW", "de" ->
    // "de". "" for "", "C", "POSIX" and "C.UTF-8", which name no language.
    // Pure.
    std::string LanguageTagFromPosixLocale(const std::string& locale);

    // The tags the four POSIX variables name, best first, each once, given
    // their values (each may be empty): the LANGUAGE list first when the
    // locale from LC_ALL / LC_MESSAGES / LANG is a real one, then that
    // locale. Pure.
    std::vector<std::string> LanguageTagsFromEnvironment(const std::string& language,
                                                         const std::string& lcAll,
                                                         const std::string& lcMessages,
                                                         const std::string& lang);

    // "de-AT" -> "de", "zh-Hant-TW" -> "zh". Pure.
    std::string LanguageTagBase(const std::string& tag);

    // Whether two tags name the same thing whatever their spelling: "de_AT",
    // "de-at" and "de-AT" are one tag, "de" and "de-AT" are not. Pure.
    bool LanguageTagsEqual(const std::string& a, const std::string& b);

    // The platform's own answer, best first, in whatever spelling it uses;
    // empty where the platform has none. Defined in OS/<Platform>/.
    std::vector<std::string> SystemLanguageTagsNative();

    // The person's languages, best first, each once: the environment's
    // tags, then the platform's. Empty when nothing names one.
    std::vector<std::string> SystemLanguageTags();

    // The first of SystemLanguageTags(), "en" when it has none.
    std::string SystemLanguage();

}  // namespace UltraCanvas
