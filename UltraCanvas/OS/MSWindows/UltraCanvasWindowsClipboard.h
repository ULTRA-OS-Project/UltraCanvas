// OS/MSWindows/UltraCanvasWindowsClipboard.h
// Windows platform clipboard implementation for UltraCanvas Framework
// TODO: Implement full Win32 clipboard backend
#pragma once

#ifndef ULTRACANVAS_WINDOWS_CLIPBOARD_H
#define ULTRACANVAS_WINDOWS_CLIPBOARD_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../../include/UltraCanvasClipboard.h"

namespace UltraCanvas {

class UltraCanvasWindowsClipboard : public UltraCanvasClipboardBackend {
public:
    UltraCanvasWindowsClipboard() = default;
    ~UltraCanvasWindowsClipboard() override = default;

    bool Initialize() override { return true; }
    void Shutdown() override {}

    bool GetClipboardText(std::string& text) override {
        if (!OpenClipboard(nullptr)) return false;
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            wchar_t* pData = static_cast<wchar_t*>(GlobalLock(hData));
            if (pData) {
                int len = WideCharToMultiByte(CP_UTF8, 0, pData, -1, nullptr, 0, nullptr, nullptr);
                if (len > 0) {
                    text.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, pData, -1, text.data(), len, nullptr, nullptr);
                }
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
        return !text.empty();
    }

    bool SetClipboardText(const std::string& text) override {
        if (!OpenClipboard(nullptr)) return false;
        EmptyClipboard();
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
        if (hMem) {
            wchar_t* pMem = static_cast<wchar_t*>(GlobalLock(hMem));
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, pMem, wlen);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
        return true;
    }

    bool GetClipboardImage(std::vector<uint8_t>&, std::string&) override { return false; }
    bool SetClipboardImage(const std::vector<uint8_t>&, const std::string&) override { return false; }
    bool GetClipboardFiles(std::vector<std::string>&) override { return false; }
    bool SetClipboardFiles(const std::vector<std::string>&) override { return false; }

    bool HasClipboardChanged() override { return false; }
    void ResetChangeState() override {}

    std::vector<std::string> GetAvailableFormats() override { return {}; }
    bool IsFormatAvailable(const std::string&) override { return false; }
};

} // namespace UltraCanvas

#endif // ULTRACANVAS_WINDOWS_CLIPBOARD_H
