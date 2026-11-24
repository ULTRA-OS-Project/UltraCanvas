// OS/iOS/UltraCanvasiOSClipboard.mm
// iOS clipboard backend over the general UIPasteboard.
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include "UltraCanvasiOSClipboard.h"
#include "UltraCanvasDebug.h"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace UltraCanvas {

    bool UltraCanvasiOSClipboard::Initialize() {
        seenChangeCount = static_cast<long>([UIPasteboard generalPasteboard].changeCount);
        return true;
    }

    void UltraCanvasiOSClipboard::Shutdown() {}

    bool UltraCanvasiOSClipboard::GetClipboardText(std::string& text) {
        @autoreleasepool {
            UIPasteboard* board = [UIPasteboard generalPasteboard];
            if (!board.hasStrings) return false;
            NSString* value = board.string;
            if (!value) return false;
            const char* utf8 = value.UTF8String;
            text = utf8 ? utf8 : "";
            return true;
        }
    }

    bool UltraCanvasiOSClipboard::SetClipboardText(const std::string& text) {
        @autoreleasepool {
            NSString* value = [NSString stringWithUTF8String:text.c_str()];
            if (!value) return false;
            [UIPasteboard generalPasteboard].string = value;
            return true;
        }
    }

    // A password or a key: kept on this device (no Universal Clipboard) and
    // gone after two minutes - iOS's own form of the "leave this out"
    // markers the desktop backends set.
    bool UltraCanvasiOSClipboard::SetClipboardSecretText(const std::string& text) {
        @autoreleasepool {
            NSString* value = [NSString stringWithUTF8String:text.c_str()];
            if (!value) return false;
            NSDictionary* item = @{ UTTypeUTF8PlainText.identifier : value };
            NSDictionary* options = @{
                UIPasteboardOptionLocalOnly : @YES,
                UIPasteboardOptionExpirationDate : [NSDate dateWithTimeIntervalSinceNow:120]
            };
            [[UIPasteboard generalPasteboard] setItems:@[ item ] options:options];
            return true;
        }
    }

    bool UltraCanvasiOSClipboard::GetClipboardImage(std::vector<uint8_t>& imageData, std::string& format) {
        @autoreleasepool {
            UIPasteboard* board = [UIPasteboard generalPasteboard];
            if (!board.hasImages) return false;
            UIImage* image = board.image;
            NSData* png = image ? UIImagePNGRepresentation(image) : nil;
            if (!png || png.length == 0) return false;
            const auto* bytes = static_cast<const uint8_t*>(png.bytes);
            imageData.assign(bytes, bytes + png.length);
            format = "image/png";
            return true;
        }
    }

    bool UltraCanvasiOSClipboard::SetClipboardImage(const std::vector<uint8_t>& imageData,
                                                    const std::string& /*format*/) {
        @autoreleasepool {
            if (imageData.empty()) return false;
            NSData* data = [NSData dataWithBytes:imageData.data() length:imageData.size()];
            UIImage* image = [UIImage imageWithData:data];
            if (!image) return false;
            [UIPasteboard generalPasteboard].image = image;
            return true;
        }
    }

    bool UltraCanvasiOSClipboard::HasClipboardChanged() {
        return static_cast<long>([UIPasteboard generalPasteboard].changeCount) != seenChangeCount;
    }

    void UltraCanvasiOSClipboard::ResetChangeState() {
        seenChangeCount = static_cast<long>([UIPasteboard generalPasteboard].changeCount);
    }

    std::vector<std::string> UltraCanvasiOSClipboard::GetAvailableFormats() {
        std::vector<std::string> formats;
        @autoreleasepool {
            UIPasteboard* board = [UIPasteboard generalPasteboard];
            // hasStrings / hasImages answer without reading the content, so
            // they raise no "pasted from" notice.
            if (board.hasStrings) formats.push_back("text/plain");
            if (board.hasImages) formats.push_back("image/png");
        }
        return formats;
    }

    bool UltraCanvasiOSClipboard::IsFormatAvailable(const std::string& format) {
        @autoreleasepool {
            UIPasteboard* board = [UIPasteboard generalPasteboard];
            if (format == "text/plain" || format == "text") return board.hasStrings;
            if (format.rfind("image/", 0) == 0) return board.hasImages;
            return false;
        }
    }

} // namespace UltraCanvas
