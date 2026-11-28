// OS/iOS/UltraCanvasiOSClipboard.h
// iOS clipboard backend: the general UIPasteboard. Text, and images as PNG;
// files have no place on an iOS pasteboard (a share sheet carries them).
// iOS shows the user a "pasted from" notice the first time an app reads
// another app's copy - which is why nothing here polls the content: a change
// is noticed through the pasteboard's changeCount, which reads nothing.
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_IOS_CLIPBOARD_H
#define ULTRACANVAS_IOS_CLIPBOARD_H

#include "../../include/UltraCanvasClipboard.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasiOSClipboard : public UltraCanvasClipboardBackend {
    public:
        UltraCanvasiOSClipboard() = default;
        ~UltraCanvasiOSClipboard() override = default;

        bool Initialize() override;
        void Shutdown() override;

        bool GetClipboardText(std::string& text) override;
        bool SetClipboardText(const std::string& text) override;
        bool SetClipboardSecretText(const std::string& text) override;
        bool GetClipboardImage(std::vector<uint8_t>& imageData, std::string& format) override;
        bool SetClipboardImage(const std::vector<uint8_t>& imageData, const std::string& format) override;
        bool GetClipboardFiles(std::vector<std::string>&) override { return false; }
        bool SetClipboardFiles(const std::vector<std::string>&) override { return false; }

        bool HasClipboardChanged() override;
        void ResetChangeState() override;

        std::vector<std::string> GetAvailableFormats() override;
        bool IsFormatAvailable(const std::string& format) override;

    private:
        long seenChangeCount = 0;   // UIPasteboard.changeCount at the last reset
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_IOS_CLIPBOARD_H
