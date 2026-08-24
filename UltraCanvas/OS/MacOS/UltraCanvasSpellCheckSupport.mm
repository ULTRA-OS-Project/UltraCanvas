// OS/MacOS/UltraCanvasSpellCheckSupport.mm
// macOS native spell check backend built on NSSpellChecker
// Version: 1.0.1
// Last Modified: 2026-08-24
// Author: UltraCanvas Framework
//
// NSSpellChecker is the system service behind every Cocoa text view, so this
// backend inherits the user's installed languages, their learned words and any
// third-party dictionaries without shipping a single dictionary file.
//
// Link: -framework AppKit -framework Foundation

#include "ISpellCheckBackend.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <mutex>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

NSString* ToNSString(const std::string& text) {
    return [NSString stringWithUTF8String:text.c_str()];
}

// NSSpellChecker treats a nil language as "use the current one". An empty
// NSString is not the same thing and makes it look for a dictionary named "".
NSString* LanguageOrNil(const std::string& languageCode) {
    if (languageCode.empty()) return nil;
    return [NSString stringWithUTF8String:languageCode.c_str()];
}

std::string FromNSString(NSString* text) {
    if (!text) return std::string();
    const char* utf8 = [text UTF8String];
    return utf8 ? std::string(utf8) : std::string();
}

// NSSpellChecker reports "en_US" style tags already, so only the separator
// needs normalising for codes that arrive as "en-US".
std::string NormalizeTag(const std::string& tag) {
    std::string result = tag;
    for (char& c : result) {
        if (c == '-') c = '_';
    }
    return result;
}

} // namespace

class SpellCheckBackendMacOS : public ISpellCheckBackend {
public:
    ~SpellCheckBackendMacOS() override {
        Shutdown();
    }

    std::string GetBackendName() const override { return "NSSpellChecker"; }

    bool Initialize() override {
        std::lock_guard<std::mutex> lock(mutex);
        @autoreleasepool {
            NSSpellChecker* shared = [NSSpellChecker sharedSpellChecker];
            if (!shared) return false;

            // A dedicated tag keeps our ignored-word list separate from any
            // Cocoa text views the host application may also be running.
            if (documentTag == 0) {
                documentTag = [NSSpellChecker uniqueSpellDocumentTag];
            }
            if (currentLanguage.empty()) {
                currentLanguage = NormalizeTag(FromNSString([shared language]));
            }
            return true;
        }
    }

    void Shutdown() override {
        std::lock_guard<std::mutex> lock(mutex);
        @autoreleasepool {
            if (documentTag != 0) {
                [[NSSpellChecker sharedSpellChecker] closeSpellDocumentWithTag:documentTag];
                documentTag = 0;
            }
        }
        currentLanguage.clear();
    }

    std::vector<SpellLanguageInfo> EnumerateLanguages() override {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<SpellLanguageInfo> languages;

        @autoreleasepool {
            NSArray<NSString*>* available =
                [[NSSpellChecker sharedSpellChecker] availableLanguages];
            if (!available) return languages;

            for (NSString* tag in available) {
                const std::string code = NormalizeTag(FromNSString(tag));
                if (code.empty()) continue;

                SpellLanguageInfo info = ResolveSpellLanguageNames(code);
                info.code = code;
                info.isAvailable = true;

                // Prefer the system's localized names when it has them: they
                // are already correct for every locale macOS ships.
                NSLocale* locale = [NSLocale currentLocale];
                NSString* english = [[NSLocale localeWithLocaleIdentifier:@"en_US"]
                                     localizedStringForLocaleIdentifier:tag];
                NSString* native = [locale localizedStringForLocaleIdentifier:tag];
                if (english.length > 0) info.displayName = FromNSString(english);
                if (native.length > 0) info.nativeName = FromNSString(native);

                languages.push_back(std::move(info));
            }
        }
        return languages;
    }

    bool SetLanguage(const std::string& languageCode) override {
        std::lock_guard<std::mutex> lock(mutex);
        @autoreleasepool {
            const BOOL accepted =
                [[NSSpellChecker sharedSpellChecker] setLanguage:ToNSString(languageCode)];
            if (!accepted) return false;
            currentLanguage = NormalizeTag(languageCode);
            return true;
        }
    }

    std::string GetLanguage() const override {
        std::lock_guard<std::mutex> lock(mutex);
        return currentLanguage;
    }

    bool IsWordCorrect(const std::string& word) override {
        std::lock_guard<std::mutex> lock(mutex);
        @autoreleasepool {
            NSString* subject = ToNSString(word);
            if (!subject || subject.length == 0) return true;

            const NSRange found =
                [[NSSpellChecker sharedSpellChecker]
                    checkSpellingOfString:subject
                               startingAt:0
                                 language:LanguageOrNil(currentLanguage)
                                     wrap:NO
                   inSpellDocumentWithTag:documentTag
                                wordCount:nullptr];

            return found.location == NSNotFound || found.length == 0;
        }
    }

    std::vector<std::string> GetSuggestions(const std::string& word, int maxCount) override {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::string> suggestions;

        @autoreleasepool {
            NSString* subject = ToNSString(word);
            if (!subject || subject.length == 0) return suggestions;

            NSArray<NSString*>* guesses =
                [[NSSpellChecker sharedSpellChecker]
                    guessesForWordRange:NSMakeRange(0, subject.length)
                               inString:subject
                               language:LanguageOrNil(currentLanguage)
                 inSpellDocumentWithTag:documentTag];

            if (!guesses) return suggestions;

            for (NSString* guess in guesses) {
                std::string candidate = FromNSString(guess);
                if (candidate.empty()) continue;
                suggestions.push_back(std::move(candidate));
                if (maxCount > 0 && suggestions.size() >= static_cast<size_t>(maxCount)) break;
            }
        }
        return suggestions;
    }

    bool AddWordToBackendDictionary(const std::string& word) override {
        std::lock_guard<std::mutex> lock(mutex);
        @autoreleasepool {
            // Learns the word system-wide, exactly as the Cocoa context menu does.
            [[NSSpellChecker sharedSpellChecker] learnWord:ToNSString(word)];
            return true;
        }
    }

    bool IsThreadSafe() const override { return false; }

private:
    mutable std::mutex mutex;
    NSInteger documentTag = 0;
    std::string currentLanguage;
};

std::unique_ptr<ISpellCheckBackend> CreateNativeSpellCheckBackend() {
    return std::make_unique<SpellCheckBackendMacOS>();
}

} // namespace UltraCanvas
