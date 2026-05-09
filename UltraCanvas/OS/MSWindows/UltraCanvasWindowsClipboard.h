// OS/MSWindows/UltraCanvasWindowsClipboard.h
// Win32 Clipboard implementation
// Supports text (CF_UNICODETEXT), images (CF_DIB), and files (CF_HDROP)
// Version: 1.0.0
// Last Modified: 2026-03-06
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_WINDOWS_CLIPBOARD_H
#define ULTRACANVAS_WINDOWS_CLIPBOARD_H

#include "../../include/UltraCanvasClipboard.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace UltraCanvas {

    class UltraCanvasWindowsClipboard : public UltraCanvasClipboardBackend {
    private:
        DWORD lastSequenceNumber;
        bool clipboardChanged;

        static UltraCanvasWindowsClipboard* instance;

    public:
        UltraCanvasWindowsClipboard();
        ~UltraCanvasWindowsClipboard() override;

        static UltraCanvasWindowsClipboard* GetInstance() { return instance; }

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
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_WINDOWS_CLIPBOARD_H
