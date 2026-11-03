// OS/MacOS/UltraCanvasMacOSSystemLanguage.mm
// SystemLanguageTagsNative on macOS: the languages of System Settings >
// Language & Region, in the person's order, as "de-AT" tags, then the
// current locale's identifier.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#include "UltraCanvasSystemLanguage.h"

#import <Foundation/Foundation.h>

namespace UltraCanvas {

std::vector<std::string> SystemLanguageTagsNative() {
    std::vector<std::string> tags;
    @autoreleasepool {
        for (NSString* tag in [NSLocale preferredLanguages]) {
            if (const char* utf8 = [tag UTF8String]) tags.emplace_back(utf8);
        }
        if (NSString* current = [[NSLocale currentLocale] localeIdentifier]) {
            if (const char* utf8 = [current UTF8String]) tags.emplace_back(utf8);
        }
    }
    return tags;
}

}  // namespace UltraCanvas
