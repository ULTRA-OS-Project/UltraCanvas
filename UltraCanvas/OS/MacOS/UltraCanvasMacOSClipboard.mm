// OS/MacOS/UltraCanvasMacOSClipboard.mm
// macOS-specific clipboard implementation using NSPasteboard
// Version: 1.0.0
// Last Modified: 2025-12-05
// Author: UltraCanvas Framework

#include "UltraCanvasMacOSClipboard.h"
#import <Cocoa/Cocoa.h>
#import <AppKit/AppKit.h>
#include <iostream>
#include <algorithm>

namespace UltraCanvas {

// ===== STATIC INSTANCE =====
    UltraCanvasMacOSClipboard* UltraCanvasMacOSClipboard::instance = nullptr;

// ===== CONSTRUCTOR & DESTRUCTOR =====
    UltraCanvasMacOSClipboard::UltraCanvasMacOSClipboard()
            : generalPasteboard(nil)
            , clipboardChanged(false)
            , lastChangeCount(0) {

        lastChangeCheck = std::chrono::steady_clock::now();
        instance = this;
    }

    UltraCanvasMacOSClipboard::~UltraCanvasMacOSClipboard() {
        Shutdown();
        instance = nullptr;
    }

// ===== INITIALIZATION =====
    bool UltraCanvasMacOSClipboard::Initialize() {
        LogInfo("Initialize", "Initializing macOS clipboard...");

        @autoreleasepool {
            generalPasteboard = [NSPasteboard generalPasteboard];

            if (!generalPasteboard) {
                LogError("Initialize", "Failed to get general pasteboard");
                return false;
            }

            lastChangeCount = [generalPasteboard changeCount];

            std::string initialText;
            if (GetClipboardText(initialText)) {
                lastClipboardText = initialText;
            }
        }

        LogInfo("Initialize", "macOS clipboard initialized successfully");
        return true;
    }

    void UltraCanvasMacOSClipboard::Shutdown() {
        generalPasteboard = nil;
        LogInfo("Shutdown", "macOS clipboard shut down");
    }

// ===== TEXT OPERATIONS =====
    bool UltraCanvasMacOSClipboard::GetClipboardText(std::string& text) {
        if (!generalPasteboard) return false;

        @autoreleasepool {
            NSString* nsText = [generalPasteboard stringForType:NSPasteboardTypeString];

            if (nsText) {
                text = NSStringToStdString(nsText);
                return true;
            }

            nsText = [generalPasteboard stringForType:NSPasteboardTypeHTML];
            if (nsText) {
                text = NSStringToStdString(nsText);
                return true;
            }

            nsText = [generalPasteboard stringForType:NSPasteboardTypeRTF];
            if (nsText) {
                text = NSStringToStdString(nsText);
                return true;
            }
        }

        return false;
    }

    bool UltraCanvasMacOSClipboard::SetClipboardText(const std::string& text) {
        if (!generalPasteboard) return false;

        @autoreleasepool {
            [generalPasteboard clearContents];

            NSString* nsText = StdStringToNSString(text);
            if (!nsText) {
                LogError("SetClipboardText", "Failed to convert text to NSString");
                return false;
            }

            BOOL success = [generalPasteboard setString:nsText forType:NSPasteboardTypeString];

            if (success) {
                lastChangeCount = [generalPasteboard changeCount];
                lastClipboardText = text;
                LogInfo("SetClipboardText", "Successfully set clipboard text");
            } else {
                LogError("SetClipboardText", "Failed to set clipboard text");
            }

            return success == YES;
        }
    }

// ===== IMAGE OPERATIONS =====
    bool UltraCanvasMacOSClipboard::GetClipboardImage(std::vector<uint8_t>& imageData, std::string& format) {
        if (!generalPasteboard) return false;

        @autoreleasepool {
            if (ReadImageAsPNG(imageData)) {
                format = "image/png";
                return true;
            }

            if (ReadImageAsJPEG(imageData)) {
                format = "image/jpeg";
                return true;
            }

            if (ReadImageAsTIFF(imageData)) {
                format = "image/tiff";
                return true;
            }
        }

        return false;
    }

    bool UltraCanvasMacOSClipboard::SetClipboardImage(const std::vector<uint8_t>& imageData, const std::string& format) {
        if (!generalPasteboard || imageData.empty()) return false;

        @autoreleasepool {
            [generalPasteboard clearContents];

            NSData* nsData = [NSData dataWithBytes:imageData.data() length:imageData.size()];
            if (!nsData) {
                LogError("SetClipboardImage", "Failed to create NSData from image data");
                return false;
            }

            NSString* pasteboardType = MimeTypeToPasteboardType(format);
            if (!pasteboardType) {
                pasteboardType = NSPasteboardTypePNG;
            }

            BOOL success = [generalPasteboard setData:nsData forType:pasteboardType];

            if (success) {
                lastChangeCount = [generalPasteboard changeCount];
                LogInfo("SetClipboardImage", "Successfully set clipboard image");
            } else {
                LogError("SetClipboardImage", "Failed to set clipboard image");
            }

            return success == YES;
        }
    }

// ===== FILE OPERATIONS =====
    bool UltraCanvasMacOSClipboard::GetClipboardFiles(std::vector<std::string>& filePaths) {
        if (!generalPasteboard) return false;

        @autoreleasepool {
            NSArray* classes = @[[NSURL class]];
            NSDictionary* options = @{NSPasteboardURLReadingFileURLsOnlyKey: @YES};

            NSArray* urls = [generalPasteboard readObjectsForClasses:classes options:options];

            if (urls && [urls count] > 0) {
                filePaths.clear();

                for (NSURL* url in urls) {
                    if ([url isFileURL]) {
                        NSString* path = [url path];
                        if (path) {
                            filePaths.push_back(NSStringToStdString(path));
                        }
                    }
                }

                return !filePaths.empty();
            }

            NSString* fileListString = [generalPasteboard stringForType:NSPasteboardTypeFileURL];
            if (fileListString) {
                NSURL* url = [NSURL URLWithString:fileListString];
                if (url && [url isFileURL]) {
                    NSString* path = [url path];
                    if (path) {
                        filePaths.push_back(NSStringToStdString(path));
                        return true;
                    }
                }
            }
        }

        return false;
    }

    bool UltraCanvasMacOSClipboard::SetClipboardFiles(const std::vector<std::string>& filePaths) {
        if (!generalPasteboard || filePaths.empty()) return false;

        @autoreleasepool {
            [generalPasteboard clearContents];

            NSMutableArray* urls = [NSMutableArray arrayWithCapacity:filePaths.size()];

            for (const std::string& path : filePaths) {
                NSString* nsPath = StdStringToNSString(path);
                if (nsPath) {
                    NSURL* url = [NSURL fileURLWithPath:nsPath];
                    if (url) {
                        [urls addObject:url];
                    }
                }
            }

            if ([urls count] == 0) {
                LogError("SetClipboardFiles", "No valid file URLs created");
                return false;
            }

            BOOL success = [generalPasteboard writeObjects:urls];

            if (success) {
                lastChangeCount = [generalPasteboard changeCount];
                LogInfo("SetClipboardFiles", "Successfully set clipboard files");
            } else {
                LogError("SetClipboardFiles", "Failed to set clipboard files");
            }

            return success == YES;
        }
    }

// ===== MONITORING =====
    bool UltraCanvasMacOSClipboard::HasClipboardChanged() {
        if (!generalPasteboard) return false;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastChangeCheck);

        if (elapsed.count() < 100) {
            return clipboardChanged;
        }

        lastChangeCheck = now;

        @autoreleasepool {
            NSInteger currentChangeCount = [generalPasteboard changeCount];

            if (currentChangeCount != lastChangeCount) {
                lastChangeCount = currentChangeCount;

                std::string currentText;
                if (GetClipboardText(currentText)) {
                    if (currentText != lastClipboardText) {
                        lastClipboardText = currentText;
                        clipboardChanged = true;
                        LogInfo("HasClipboardChanged", "Clipboard content changed");
                    }
                } else {
                    clipboardChanged = true;
                }
            }
        }

        return clipboardChanged;
    }

    void UltraCanvasMacOSClipboard::ResetChangeState() {
        clipboardChanged = false;
    }

// ===== FORMAT DETECTION =====
    std::vector<std::string> UltraCanvasMacOSClipboard::GetAvailableFormats() {
        std::vector<std::string> formats;

        if (!generalPasteboard) return formats;

        @autoreleasepool {
            NSArray* types = [generalPasteboard types];

            for (NSString* type in types) {
                std::string mimeType = PasteboardTypeToMimeType(type);
                if (!mimeType.empty()) {
                    formats.push_back(mimeType);
                }
            }
        }

        return formats;
    }

    bool UltraCanvasMacOSClipboard::IsFormatAvailable(const std::string& format) {
        if (!generalPasteboard) return false;

        @autoreleasepool {
            NSString* pasteboardType = MimeTypeToPasteboardType(format);
            if (pasteboardType) {
                return [generalPasteboard availableTypeFromArray:@[pasteboardType]] != nil;
            }

            NSArray* types = [generalPasteboard types];
            for (NSString* type in types) {
                std::string mimeType = PasteboardTypeToMimeType(type);
                if (mimeType == format) {
                    return true;
                }
            }
        }

        return false;
    }

// ===== STRING CONVERSION HELPERS =====
    std::string UltraCanvasMacOSClipboard::NSStringToStdString(NSString* nsString) {
        if (!nsString) return "";

        const char* utf8String = [nsString UTF8String];
        if (utf8String) {
            return std::string(utf8String);
        }

        return "";
    }

    NSString* UltraCanvasMacOSClipboard::StdStringToNSString(const std::string& str) {
        return [NSString stringWithUTF8String:str.c_str()];
    }

// ===== FORMAT CONVERSION =====
    std::string UltraCanvasMacOSClipboard::PasteboardTypeToMimeType(NSString* pasteboardType) {
        if (!pasteboardType) return "";

        if ([pasteboardType isEqualToString:NSPasteboardTypeString]) {
            return "text/plain";
        }
        if ([pasteboardType isEqualToString:NSPasteboardTypeHTML]) {
            return "text/html";
        }
        if ([pasteboardType isEqualToString:NSPasteboardTypeRTF]) {
            return "text/rtf";
        }
        if ([pasteboardType isEqualToString:NSPasteboardTypePNG]) {
            return "image/png";
        }
        if ([pasteboardType isEqualToString:NSPasteboardTypeTIFF]) {
            return "image/tiff";
        }
        if ([pasteboardType isEqualToString:NSPasteboardTypePDF]) {
            return "application/pdf";
        }
        if ([pasteboardType isEqualToString:NSPasteboardTypeFileURL]) {
            return "text/uri-list";
        }
        if ([pasteboardType isEqualToString:NSPasteboardTypeURL]) {
            return "text/uri-list";
        }

        return NSStringToStdString(pasteboardType);
    }

    NSString* UltraCanvasMacOSClipboard::MimeTypeToPasteboardType(const std::string& mimeType) {
        if (mimeType == "text/plain" || mimeType == "UTF8_STRING" || mimeType == "STRING") {
            return NSPasteboardTypeString;
        }
        if (mimeType == "text/html") {
            return NSPasteboardTypeHTML;
        }
        if (mimeType == "text/rtf") {
            return NSPasteboardTypeRTF;
        }
        if (mimeType == "image/png") {
            return NSPasteboardTypePNG;
        }
        if (mimeType == "image/tiff") {
            return NSPasteboardTypeTIFF;
        }
        if (mimeType == "image/jpeg" || mimeType == "image/jpg") {
            return @"public.jpeg";
        }
        if (mimeType == "application/pdf") {
            return NSPasteboardTypePDF;
        }
        if (mimeType == "text/uri-list") {
            return NSPasteboardTypeFileURL;
        }

        return nil;
    }

// ===== IMAGE READING HELPERS =====
    bool UltraCanvasMacOSClipboard::ReadImageAsPNG(std::vector<uint8_t>& imageData) {
        NSData* pngData = [generalPasteboard dataForType:NSPasteboardTypePNG];

        if (pngData && [pngData length] > 0) {
            const uint8_t* bytes = static_cast<const uint8_t*>([pngData bytes]);
            imageData.assign(bytes, bytes + [pngData length]);
            return true;
        }

        NSData* tiffData = [generalPasteboard dataForType:NSPasteboardTypeTIFF];
        if (tiffData && [tiffData length] > 0) {
            NSBitmapImageRep* imageRep = [NSBitmapImageRep imageRepWithData:tiffData];
            if (imageRep) {
                NSData* convertedPNG = [imageRep representationUsingType:NSBitmapImageFileTypePNG
                                                              properties:@{}];
                if (convertedPNG && [convertedPNG length] > 0) {
                    const uint8_t* bytes = static_cast<const uint8_t*>([convertedPNG bytes]);
                    imageData.assign(bytes, bytes + [convertedPNG length]);
                    return true;
                }
            }
        }

        return false;
    }

    bool UltraCanvasMacOSClipboard::ReadImageAsJPEG(std::vector<uint8_t>& imageData) {
        NSData* jpegData = [generalPasteboard dataForType:@"public.jpeg"];

        if (jpegData && [jpegData length] > 0) {
            const uint8_t* bytes = static_cast<const uint8_t*>([jpegData bytes]);
            imageData.assign(bytes, bytes + [jpegData length]);
            return true;
        }

        return false;
    }

    bool UltraCanvasMacOSClipboard::ReadImageAsTIFF(std::vector<uint8_t>& imageData) {
        NSData* tiffData = [generalPasteboard dataForType:NSPasteboardTypeTIFF];

        if (tiffData && [tiffData length] > 0) {
            const uint8_t* bytes = static_cast<const uint8_t*>([tiffData bytes]);
            imageData.assign(bytes, bytes + [tiffData length]);
            return true;
        }

        return false;
    }

// ===== LOGGING =====
    void UltraCanvasMacOSClipboard::LogError(const std::string& operation, const std::string& details) {
        std::cerr << "UltraCanvas macOS Clipboard Error [" << operation << "]: " << details << std::endl;
    }

    void UltraCanvasMacOSClipboard::LogInfo(const std::string& operation, const std::string& details) {
        std::cout << "UltraCanvas: " << operation << " - " << details << std::endl;
    }

} // namespace UltraCanvas