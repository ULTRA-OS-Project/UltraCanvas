// OS/Linux/UltraCanvasLinuxSystemLanguage.cpp
// SystemLanguageTagsNative on Linux and the BSDs: nothing beyond the POSIX
// variables, which are the desktop's own answer there - GNOME, KDE and the
// display managers export LANG / LANGUAGE for the session from the language
// the person chose. UltraCanvasSystemLanguage.cpp reads those.
//
// SystemRegionalFormatsNative: the region is LC_NUMERIC and LC_TIME (each
// under LC_ALL, over LANG), which is what the desktops' Region & Formats
// settings write, so a person reading English with German dates has
// LANG=en_US.UTF-8 and LC_TIME=de_DE.UTF-8. The conventions are the built-in
// CLDR ones for those regions rather than glibc's nl_langinfo patterns,
// which are coarser (en_US's "%r" pads the hour, en_GB's date has a
// two-digit year) and missing where the locale is not installed.
// Version: 1.1.0 - SystemRegionalFormatsNative from LC_NUMERIC and LC_TIME
// Version: 1.0.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework
#include "UltraCanvasSystemLanguage.h"

#include "UltraCanvasPathUtf8.h"   // GetEnvUtf8

namespace UltraCanvas {

std::vector<std::string> SystemLanguageTagsNative() {
    return {};
}

namespace {

// The locale a category is in, as the C library reads it: LC_ALL, else the
// category's own variable, else LANG.
std::string CategoryLocale(const char* category) {
    std::string value = GetEnvUtf8("LC_ALL");
    if (value.empty()) value = GetEnvUtf8(category);
    if (value.empty()) value = GetEnvUtf8("LANG");
    return value;
}

}  // namespace

bool SystemRegionalFormatsNative(LocaleFormats& formats) {
    const std::string timeTag = LanguageTagFromPosixLocale(CategoryLocale("LC_TIME"));
    const std::string numericTag = LanguageTagFromPosixLocale(CategoryLocale("LC_NUMERIC"));
    if (timeTag.empty() && numericTag.empty()) return false;   // C or POSIX: the region is not set
    formats = UltraLocalisation::FormatsFor(timeTag.empty() ? numericTag : timeTag);
    if (!numericTag.empty() && !timeTag.empty() && !LanguageTagsEqual(numericTag, timeTag)) {
        const LocaleFormats numbers = UltraLocalisation::FormatsFor(numericTag);
        formats.decimalSeparator = numbers.decimalSeparator;
        formats.groupSeparator = numbers.groupSeparator;
        formats.groupSize = numbers.groupSize;
        formats.secondaryGroupSize = numbers.secondaryGroupSize;
        formats.minimumGroupingDigits = numbers.minimumGroupingDigits;
        formats.digits = numbers.digits;
        formats.percentPattern = numbers.percentPattern;
    }
    return true;
}

}  // namespace UltraCanvas
