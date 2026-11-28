// OS/MacOS/UltraCanvasMacOSInstalledFonts.mm
// FillNativeFontDetails on macOS (UltraCanvasInstalledFonts.h): the text
// engine's families are CoreText's, so each style's format and file come
// from CoreText's descriptor of the same family and style name - the one
// Pango's CoreText font map lists the face by - and a family's characters
// from its regular face's character set. CoreText takes no per-face family
// name ("Helvetica Neue Light" is not a family there), so faceFamily stays
// empty.
// Version: 1.0.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework
#include "UltraCanvasInstalledFonts.h"

#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <vector>

namespace UltraCanvas {

namespace {

std::string LowerName(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Utf8Of(CFStringRef text) {
    if (!text) return std::string();
    if (const char* direct = CFStringGetCStringPtr(text, kCFStringEncodingUTF8)) return direct;
    const CFIndex size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(text), kCFStringEncodingUTF8) + 1;
    std::vector<char> buffer(static_cast<size_t>(size), '\0');
    if (!CFStringGetCString(text, buffer.data(), size, kCFStringEncodingUTF8)) return std::string();
    return std::string(buffer.data());
}

struct CoreTextFace {
    std::string style;                       // lower case
    FontFileFormat format = FontFileFormat::Unknown;
    std::string file;
    CTFontDescriptorRef descriptor = nullptr;   // owned by the matching array
};

FontFileFormat FormatOf(int coreTextFormat, const std::string& file) {
    std::string extension;
    const size_t dot = file.find_last_of('.');
    if (dot != std::string::npos) extension = LowerName(file.substr(dot + 1));
    switch (coreTextFormat) {
        case kCTFontFormatOpenTypePostScript: return FontFileFormat::OpenType;
        case kCTFontFormatOpenTypeTrueType:
            return extension == "otf" || extension == "otc" ? FontFileFormat::OpenType : FontFileFormat::TrueType;
        case kCTFontFormatTrueType:           return FontFileFormat::TrueType;
        case kCTFontFormatPostScript:         return FontFileFormat::Type1;
        case kCTFontFormatBitmap:             return FontFileFormat::BitmapFont;
        default:                              break;
    }
    if (extension == "otf" || extension == "otc") return FontFileFormat::OpenType;
    if (extension == "ttf" || extension == "ttc" || extension == "dfont") return FontFileFormat::TrueType;
    return FontFileFormat::Unknown;
}

// Whether CoreText classes the face as symbolic (kCTFontSymbolicClass).
bool IsSymbolicClass(CTFontDescriptorRef descriptor) {
    bool symbolic = false;
    CFDictionaryRef traits = static_cast<CFDictionaryRef>(CTFontDescriptorCopyAttribute(descriptor, kCTFontTraitsAttribute));
    if (traits) {
        CFNumberRef value = static_cast<CFNumberRef>(CFDictionaryGetValue(traits, kCTFontSymbolicTrait));
        uint32_t bits = 0;
        if (value && CFNumberGetValue(value, kCFNumberSInt32Type, &bits))
            symbolic = (bits & kCTFontClassMaskTrait) == static_cast<uint32_t>(kCTFontSymbolicClass);
        CFRelease(traits);
    }
    return symbolic;
}

} // namespace

void FillNativeFontDetails(std::vector<InstalledFontFamily>& families) {
    if (families.empty()) return;
    @autoreleasepool {
        CTFontCollectionRef collection = CTFontCollectionCreateFromAvailableFonts(nullptr);
        if (!collection) return;
        CFArrayRef descriptors = CTFontCollectionCreateMatchingFontDescriptors(collection);
        CFRelease(collection);
        if (!descriptors) return;

        std::map<std::string, std::vector<CoreTextFace>> byFamily;
        const CFIndex count = CFArrayGetCount(descriptors);
        for (CFIndex i = 0; i < count; ++i) {
            CTFontDescriptorRef descriptor = static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(descriptors, i));
            if (!descriptor) continue;
            CFStringRef family = static_cast<CFStringRef>(CTFontDescriptorCopyAttribute(descriptor, kCTFontFamilyNameAttribute));
            CFStringRef style = static_cast<CFStringRef>(CTFontDescriptorCopyAttribute(descriptor, kCTFontStyleNameAttribute));
            CFNumberRef format = static_cast<CFNumberRef>(CTFontDescriptorCopyAttribute(descriptor, kCTFontFormatAttribute));
            CFURLRef url = static_cast<CFURLRef>(CTFontDescriptorCopyAttribute(descriptor, kCTFontURLAttribute));
            CoreTextFace face;
            face.descriptor = descriptor;
            face.style = LowerName(Utf8Of(style));
            if (url) {
                if (CFStringRef path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle)) {
                    face.file = Utf8Of(path);
                    CFRelease(path);
                }
            }
            int formatValue = 0;
            if (format) CFNumberGetValue(format, kCFNumberIntType, &formatValue);
            face.format = FormatOf(formatValue, face.file);
            const std::string familyName = Utf8Of(family);
            if (!familyName.empty()) byFamily[LowerName(familyName)].push_back(std::move(face));
            if (family) CFRelease(family);
            if (style) CFRelease(style);
            if (format) CFRelease(format);
            if (url) CFRelease(url);
        }

        std::vector<InstalledFontFamily> kept;
        kept.reserve(families.size());
        for (InstalledFontFamily& family : families) {
            auto found = byFamily.find(LowerName(family.name));
            if (found == byFamily.end()) { kept.push_back(std::move(family)); continue; }
            const std::vector<CoreTextFace>& faces = found->second;
            auto faceFor = [&faces](const InstalledFontStyle& style) -> const CoreTextFace* {
                const std::string name = LowerName(style.name);
                for (const CoreTextFace& face : faces)
                    if (face.style == name) return &face;
                return faces.empty() ? nullptr : &faces.front();
            };
            for (InstalledFontStyle& style : family.styles) {
                if (const CoreTextFace* face = faceFor(style)) {
                    style.format = face->format;
                    style.file = face->file;
                }
            }
            const InstalledFontStyle* regular = family.RegularStyle();
            const CoreTextFace* regularFace = regular ? faceFor(*regular) : (faces.empty() ? nullptr : &faces.front());
            if (regularFace && regularFace->descriptor) {
                family.format = regularFace->format;
                CFCharacterSetRef characters = static_cast<CFCharacterSetRef>(
                        CTFontDescriptorCopyAttribute(regularFace->descriptor, kCTFontCharacterSetAttribute));
                if (characters) {
                    auto covers = [characters](char32_t c) {
                        return CFCharacterSetIsLongCharacterMember(characters, static_cast<UTF32Char>(c)) != false;
                    };
                    const bool spells = FontCoversItsName(family.name, covers);
                    family.symbol = !spells || IsSymbolicClass(regularFace->descriptor);
                    if (family.symbol) {
                        std::vector<char32_t> covered;
                        for (char32_t c = 0x20; c < 0x30000 && covered.size() < 4000; ++c)
                            if (covers(c)) covered.push_back(c);
                        family.sample = ChooseFontSample(covered);
                        if (covered.empty()) {   // no characters: nothing to show
                            CFRelease(characters);
                            continue;
                        }
                    }
                    CFRelease(characters);
                }
            }
            kept.push_back(std::move(family));
        }
        families = std::move(kept);
        CFRelease(descriptors);
    }
}

} // namespace UltraCanvas
