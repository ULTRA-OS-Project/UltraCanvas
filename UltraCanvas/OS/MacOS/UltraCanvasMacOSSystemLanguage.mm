// OS/MacOS/UltraCanvasMacOSSystemLanguage.mm
// SystemLanguageTagsNative on macOS: the languages of System Settings >
// Language & Region, in the person's order, as "de-AT" tags, then the
// current locale's identifier.
//
// SystemRegionalFormatsNative: the region of System Settings > General > Language & Region -
// its separators, and its date and time formats as NSDateFormatter gives
// them for each style - over the built-in conventions of the region.
// NSLocale has the person's own changes (the 24-hour clock, the date
// format) in it.
// Version: 1.1.0 - SystemRegionalFormatsNative from NSLocale and NSDateFormatter
// Version: 1.0.0
// Last Modified: 2026-11-26
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

namespace {

std::string Utf8Of(NSString* text) {
    if (!text) return {};
    const char* utf8 = [text UTF8String];
    return utf8 ? std::string(utf8) : std::string();
}

// The date or time pattern of one style in the current locale, the person's
// own changes in Language & Region included.
std::string PatternOf(NSDateFormatterStyle date, NSDateFormatterStyle time) {
    NSDateFormatter* formatter = [[NSDateFormatter alloc] init];
    formatter.locale = [NSLocale currentLocale];
    formatter.dateStyle = date;
    formatter.timeStyle = time;
    return Utf8Of(formatter.dateFormat);
}

}  // namespace

bool SystemRegionalFormatsNative(LocaleFormats& formats) {
    @autoreleasepool {
        NSLocale* locale = [NSLocale currentLocale];
        std::string tag = Utf8Of([locale localeIdentifier]);   // "de_DE", "en_US@rg=gbzzzz"
        const size_t at = tag.find('@');
        if (at != std::string::npos) tag.erase(at);
        tag = LanguageTagFromPosixLocale(tag);
        if (tag.empty()) return false;
        formats = UltraLocalisation::FormatsFor(tag);

        const std::string decimal = Utf8Of([locale decimalSeparator]);
        if (!decimal.empty()) formats.decimalSeparator = decimal;
        NSNumberFormatter* numbers = [[NSNumberFormatter alloc] init];
        numbers.locale = locale;
        numbers.numberStyle = NSNumberFormatterDecimalStyle;
        formats.groupSeparator = numbers.usesGroupingSeparator ? Utf8Of(numbers.groupingSeparator) : std::string();
        if (numbers.groupingSize > 0) {
            formats.groupSize = static_cast<int>(numbers.groupingSize);
            formats.secondaryGroupSize = numbers.secondaryGroupingSize > 0 ? static_cast<int>(numbers.secondaryGroupingSize)
                                                                           : formats.groupSize;
        }

        const std::string shortDate = PatternOf(NSDateFormatterShortStyle, NSDateFormatterNoStyle);
        if (!shortDate.empty()) formats.shortDate = UltraLocalisation::PatternWithFullYear(shortDate);
        const std::string longDate = PatternOf(NSDateFormatterLongStyle, NSDateFormatterNoStyle);
        if (!longDate.empty()) formats.longDate = longDate;
        const std::string fullDate = PatternOf(NSDateFormatterFullStyle, NSDateFormatterNoStyle);
        if (!fullDate.empty()) formats.fullDate = fullDate;
        const std::string shortTime = PatternOf(NSDateFormatterNoStyle, NSDateFormatterShortStyle);
        if (!shortTime.empty()) formats.shortTime = shortTime;
        const std::string mediumTime = PatternOf(NSDateFormatterNoStyle, NSDateFormatterMediumStyle);
        if (!mediumTime.empty()) formats.mediumTime = mediumTime;
    }
    return true;
}

}  // namespace UltraCanvas
