// OS/iOS/UltraCanvasiOSSystemLanguage.mm
// The languages the user chose in Settings, most preferred first, as BCP 47
// tags ("de-DE", "zh-Hans-CN").
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include "UltraCanvasSystemLanguage.h"

#import <Foundation/Foundation.h>

namespace UltraCanvas {

    std::vector<std::string> SystemLanguageTagsNative() {
        std::vector<std::string> tags;
        @autoreleasepool {
            for (NSString* tag in [NSLocale preferredLanguages]) {
                const char* utf8 = tag.UTF8String;
                if (utf8 && *utf8) tags.emplace_back(utf8);
            }
        }
        return tags;
    }

} // namespace UltraCanvas
