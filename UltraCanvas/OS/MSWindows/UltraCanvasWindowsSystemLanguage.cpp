// OS/MSWindows/UltraCanvasWindowsSystemLanguage.cpp
// SystemLanguageTagsNative on Windows: the user's preferred UI languages
// (Settings > Language, in their order), then the user default locale, as
// "de-AT" tags. The POSIX variables are normally unset on Windows, so
// without this every first run took whatever enumerated first.
//
// SystemRegionalFormatsNative: Settings > Time & language > Region, as
// GetLocaleInfoEx gives it for the user default locale - the person's own
// changes to the separators and the date and time formats included - over
// the built-in conventions of the region.
// Version: 1.1.0 - SystemRegionalFormatsNative from GetLocaleInfoEx
// Version: 1.0.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework
#include "UltraCanvasSystemLanguage.h"

#include "UltraCanvasPathUtf8.h"   // PathUtf8Detail::Utf16ToUtf8

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

// One value of the user default locale, UTF-8; empty when it has none.
std::string UserLocaleInfo(LCTYPE type) {
    wchar_t buffer[128] = {0};
    const int length = GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, type, buffer, static_cast<int>(sizeof buffer / sizeof buffer[0]));
    if (length <= 1) return {};
    return PathUtf8Detail::Utf16ToUtf8(std::wstring(buffer, static_cast<size_t>(length - 1)));
}

}  // namespace

std::vector<std::string> SystemLanguageTagsNative() {
    std::vector<std::string> tags;

    // A multi-string: "en-US\0de-DE\0\0".
    ULONG count = 0;
    ULONG size = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size > 0) {
        std::vector<wchar_t> buffer(size);
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &size)) {
            const wchar_t* p = buffer.data();
            while (*p) {
                const std::wstring one(p);
                tags.push_back(PathUtf8Detail::Utf16ToUtf8(one));
                p += one.size() + 1;
            }
        }
    }

    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {0};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0) {
        tags.push_back(PathUtf8Detail::Utf16ToUtf8(std::wstring(name)));
    }
    return tags;
}

bool SystemRegionalFormatsNative(LocaleFormats& formats) {
    const std::string tag = UserLocaleInfo(LOCALE_SNAME);   // "de-DE"
    if (tag.empty()) return false;
    formats = UltraLocalisation::FormatsFor(tag);

    const std::string decimal = UserLocaleInfo(LOCALE_SDECIMAL);
    if (!decimal.empty()) formats.decimalSeparator = decimal;
    // An empty thousands separator is the person's choice of none
    wchar_t probe[8] = {0};
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, probe, 8) > 0) formats.groupSeparator = UserLocaleInfo(LOCALE_STHOUSAND);
    // "3;0" is groups of three, "3;2;0" three then twos (India)
    const std::string grouping = UserLocaleInfo(LOCALE_SGROUPING);
    if (!grouping.empty() && grouping[0] >= '1' && grouping[0] <= '9') {
        formats.groupSize = grouping[0] - '0';
        formats.secondaryGroupSize = formats.groupSize;
        const size_t semicolon = grouping.find(';');
        if (semicolon != std::string::npos && semicolon + 1 < grouping.size() && grouping[semicolon + 1] >= '1' &&
            grouping[semicolon + 1] <= '9')
            formats.secondaryGroupSize = grouping[semicolon + 1] - '0';
    }

    const std::string shortDate = UserLocaleInfo(LOCALE_SSHORTDATE);
    if (!shortDate.empty()) formats.shortDate = UltraLocalisation::PatternWithFullYear(UltraLocalisation::PatternFromWindows(shortDate));
    // Windows' long date usually carries the weekday: it is our full one
    const std::string longDate = UserLocaleInfo(LOCALE_SLONGDATE);
    if (!longDate.empty()) {
        const std::string pattern = UltraLocalisation::PatternFromWindows(longDate);
        if (pattern.find("EEEE") != std::string::npos) formats.fullDate = pattern;
        else formats.longDate = pattern;
    }
    const std::string shortTime = UserLocaleInfo(LOCALE_SSHORTTIME);
    if (!shortTime.empty()) formats.shortTime = UltraLocalisation::PatternFromWindows(shortTime);
    const std::string mediumTime = UserLocaleInfo(LOCALE_STIMEFORMAT);
    if (!mediumTime.empty()) formats.mediumTime = UltraLocalisation::PatternFromWindows(mediumTime);
    return true;
}

}  // namespace UltraCanvas
