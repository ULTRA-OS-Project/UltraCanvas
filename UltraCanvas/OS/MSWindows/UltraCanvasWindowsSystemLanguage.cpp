// OS/MSWindows/UltraCanvasWindowsSystemLanguage.cpp
// SystemLanguageTagsNative on Windows: the user's preferred UI languages
// (Settings > Language, in their order), then the user default locale, as
// "de-AT" tags. The POSIX variables are normally unset on Windows, so
// without this every first run took whatever enumerated first.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#include "UltraCanvasSystemLanguage.h"

#include "UltraCanvasPathUtf8.h"   // PathUtf8Detail::Utf16ToUtf8

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <vector>

namespace UltraCanvas {

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

}  // namespace UltraCanvas
