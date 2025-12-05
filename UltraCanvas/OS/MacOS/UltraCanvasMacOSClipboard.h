// OS/MacOS/UltraCanvasMacOSClipboard.h
// macOS-specific clipboard implementation using NSPasteboard
// Version: 1.0.0
// Last Modified: 2025-12-05
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_MAC_CLIPBOARD_H
#define ULTRACANVAS_MAC_CLIPBOARD_H

#include "../../include/UltraCanvasClipboard.h"
#include <memory>
#include <chrono>
#include <string>
#include <vector>

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
#else
typedef struct objc_object NSPasteboard;
typedef struct objc_object NSString;
typedef struct objc_object NSData;
typedef struct objc_object NSArray;
typedef struct objc_object NSURL;
typedef long NSInteger;
#endif

namespace UltraCanvas {

// ===== MACOS CLIPBOARD BACKEND =====
    class UltraCanvasMacOSClipboard : public UltraCanvasClipboardBackend {
    private:
        // ===== PASTEBOARD RESOURCES =====
        NSPasteboard* generalPasteboard;

        // ===== CLIPBOARD STATE =====
        std::chrono::steady_clock::time_point lastChangeCheck;
        std::string lastClipboardText;
        bool clipboardChanged;
        NSInteger lastChangeCount;

        // ===== CONSTANTS =====
        static constexpr size_t MAX_CLIPBOARD_SIZE = 10 * 1024 * 1024; // 10MB
        static UltraCanvasMacOSClipboard* instance;

    public:
        UltraCanvasMacOSClipboard();
        ~UltraCanvasMacOSClipboard() override;

        static UltraCanvasMacOSClipboard* GetInstance() { return instance; }

        // ===== INITIALIZATION =====
        bool Initialize() override;
        void Shutdown() override;

        // ===== CLIPBOARD OPERATIONS =====
        bool GetClipboardText(std::string& text) override;
        bool SetClipboardText(const std::string& text) override;
        bool GetClipboardImage(std::vector<uint8_t>& imageData, std::string& format) override;
        bool SetClipboardImage(const std::vector<uint8_t>& imageData, const std::string& format) override;
        bool GetClipboardFiles(std::vector<std::string>& filePaths) override;
        bool SetClipboardFiles(const std::vector<std::string>& filePaths) override;

        // ===== MONITORING =====
        bool HasClipboardChanged() override;
        void ResetChangeState() override;

        // ===== FORMAT DETECTION =====
        std::vector<std::string> GetAvailableFormats() override;
        bool IsFormatAvailable(const std::string& format) override;

    private:
        // ===== HELPER METHODS =====
        std::string NSStringToStdString(NSString* nsString);
        NSString* StdStringToNSString(const std::string& str);

        // ===== FORMAT CONVERSION =====
        std::string PasteboardTypeToMimeType(NSString* pasteboardType);
        NSString* MimeTypeToPasteboardType(const std::string& mimeType);

        // ===== IMAGE OPERATIONS =====
        bool ReadImageAsPNG(std::vector<uint8_t>& imageData);
        bool ReadImageAsJPEG(std::vector<uint8_t>& imageData);
        bool ReadImageAsTIFF(std::vector<uint8_t>& imageData);

        // ===== LOGGING =====
        void LogError(const std::string& operation, const std::string& details);
        void LogInfo(const std::string& operation, const std::string& details);
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_MAC_CLIPBOARD_H