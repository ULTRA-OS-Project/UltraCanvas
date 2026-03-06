// OS/MSWindows/UltraCanvasWindowsClipboard.cpp
// Win32 Clipboard implementation
// Version: 1.0.0
// Last Modified: 2026-03-06
// Author: UltraCanvas Framework

#include "UltraCanvasWindowsClipboard.h"
#include "UltraCanvasWindowsApplication.h"
#include <iostream>
#include <cstring>

namespace UltraCanvas {

    UltraCanvasWindowsClipboard* UltraCanvasWindowsClipboard::instance = nullptr;

    UltraCanvasWindowsClipboard::UltraCanvasWindowsClipboard()
            : lastSequenceNumber(0)
            , clipboardChanged(false) {
        instance = this;
    }

    UltraCanvasWindowsClipboard::~UltraCanvasWindowsClipboard() {
        Shutdown();
        if (instance == this) instance = nullptr;
    }

// ===== INITIALIZATION =====

    bool UltraCanvasWindowsClipboard::Initialize() {
        lastSequenceNumber = GetClipboardSequenceNumber();
        clipboardChanged = false;
        std::cerr << "UltraCanvas: Windows clipboard initialized" << std::endl;
        return true;
    }

    void UltraCanvasWindowsClipboard::Shutdown() {
        std::cerr << "UltraCanvas: Windows clipboard shut down" << std::endl;
    }

// ===== TEXT OPERATIONS =====

    bool UltraCanvasWindowsClipboard::GetClipboardText(std::string& text) {
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            return false;
        }

        if (!OpenClipboard(nullptr)) {
            std::cerr << "UltraCanvas Clipboard: OpenClipboard failed" << std::endl;
            return false;
        }

        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (!hData) {
            CloseClipboard();
            return false;
        }

        auto* pData = static_cast<const wchar_t*>(GlobalLock(hData));
        if (!pData) {
            CloseClipboard();
            return false;
        }

        text = UltraCanvasWindowsApplication::Utf16ToUtf8(pData);

        GlobalUnlock(hData);
        CloseClipboard();
        return true;
    }

    bool UltraCanvasWindowsClipboard::SetClipboardText(const std::string& text) {
        std::wstring wtext = UltraCanvasWindowsApplication::Utf8ToUtf16(text);
        size_t byteSize = (wtext.size() + 1) * sizeof(wchar_t);

        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, byteSize);
        if (!hMem) {
            std::cerr << "UltraCanvas Clipboard: GlobalAlloc failed" << std::endl;
            return false;
        }

        auto* pMem = static_cast<wchar_t*>(GlobalLock(hMem));
        if (!pMem) {
            GlobalFree(hMem);
            return false;
        }
        std::memcpy(pMem, wtext.c_str(), byteSize);
        GlobalUnlock(hMem);

        if (!OpenClipboard(nullptr)) {
            GlobalFree(hMem);
            std::cerr << "UltraCanvas Clipboard: OpenClipboard failed" << std::endl;
            return false;
        }

        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
            GlobalFree(hMem);
            CloseClipboard();
            std::cerr << "UltraCanvas Clipboard: SetClipboardData failed" << std::endl;
            return false;
        }

        CloseClipboard();
        return true;
    }

// ===== IMAGE OPERATIONS =====

    bool UltraCanvasWindowsClipboard::GetClipboardImage(
            std::vector<uint8_t>& imageData, std::string& format) {
        if (!IsClipboardFormatAvailable(CF_DIB)) {
            return false;
        }

        if (!OpenClipboard(nullptr)) {
            return false;
        }

        HANDLE hData = GetClipboardData(CF_DIB);
        if (!hData) {
            CloseClipboard();
            return false;
        }

        auto* pData = static_cast<const uint8_t*>(GlobalLock(hData));
        if (!pData) {
            CloseClipboard();
            return false;
        }

        SIZE_T dataSize = GlobalSize(hData);
        imageData.assign(pData, pData + dataSize);
        format = "image/bmp";

        GlobalUnlock(hData);
        CloseClipboard();
        return true;
    }

    bool UltraCanvasWindowsClipboard::SetClipboardImage(
            const std::vector<uint8_t>& imageData, const std::string& format) {
        if (imageData.empty()) return false;

        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, imageData.size());
        if (!hMem) return false;

        auto* pMem = static_cast<uint8_t*>(GlobalLock(hMem));
        if (!pMem) {
            GlobalFree(hMem);
            return false;
        }
        std::memcpy(pMem, imageData.data(), imageData.size());
        GlobalUnlock(hMem);

        if (!OpenClipboard(nullptr)) {
            GlobalFree(hMem);
            return false;
        }

        EmptyClipboard();
        if (!SetClipboardData(CF_DIB, hMem)) {
            GlobalFree(hMem);
            CloseClipboard();
            return false;
        }

        CloseClipboard();
        return true;
    }

// ===== FILE OPERATIONS =====

    bool UltraCanvasWindowsClipboard::GetClipboardFiles(
            std::vector<std::string>& filePaths) {
        if (!IsClipboardFormatAvailable(CF_HDROP)) {
            return false;
        }

        if (!OpenClipboard(nullptr)) {
            return false;
        }

        HANDLE hData = GetClipboardData(CF_HDROP);
        if (!hData) {
            CloseClipboard();
            return false;
        }

        HDROP hDrop = static_cast<HDROP>(hData);
        UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);

        for (UINT i = 0; i < fileCount; i++) {
            UINT pathLen = DragQueryFileW(hDrop, i, nullptr, 0) + 1;
            std::wstring wpath(pathLen, 0);
            DragQueryFileW(hDrop, i, &wpath[0], pathLen);
            // Remove null terminator from wstring
            if (!wpath.empty() && wpath.back() == L'\0') {
                wpath.pop_back();
            }
            filePaths.push_back(UltraCanvasWindowsApplication::Utf16ToUtf8(wpath));
        }

        CloseClipboard();
        return !filePaths.empty();
    }

    bool UltraCanvasWindowsClipboard::SetClipboardFiles(
            const std::vector<std::string>& filePaths) {
        if (filePaths.empty()) return false;

        // Build DROPFILES structure followed by double-null-terminated file list
        // Format: DROPFILES header + file1\0file2\0...fileN\0\0
        std::wstring allPaths;
        for (const auto& path : filePaths) {
            allPaths += UltraCanvasWindowsApplication::Utf8ToUtf16(path);
            allPaths += L'\0';
        }
        allPaths += L'\0';  // Double-null terminator

        size_t totalSize = sizeof(DROPFILES) + allPaths.size() * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, totalSize);
        if (!hMem) return false;

        auto* pDrop = static_cast<DROPFILES*>(GlobalLock(hMem));
        if (!pDrop) {
            GlobalFree(hMem);
            return false;
        }

        pDrop->pFiles = sizeof(DROPFILES);
        pDrop->fWide = TRUE;  // Using wide characters
        std::memcpy(reinterpret_cast<uint8_t*>(pDrop) + sizeof(DROPFILES),
                     allPaths.c_str(), allPaths.size() * sizeof(wchar_t));
        GlobalUnlock(hMem);

        if (!OpenClipboard(nullptr)) {
            GlobalFree(hMem);
            return false;
        }

        EmptyClipboard();
        if (!SetClipboardData(CF_HDROP, hMem)) {
            GlobalFree(hMem);
            CloseClipboard();
            return false;
        }

        CloseClipboard();
        return true;
    }

// ===== MONITORING =====

    bool UltraCanvasWindowsClipboard::HasClipboardChanged() {
        DWORD currentSeq = GetClipboardSequenceNumber();
        if (currentSeq != lastSequenceNumber) {
            clipboardChanged = true;
            lastSequenceNumber = currentSeq;
            return true;
        }
        return false;
    }

    void UltraCanvasWindowsClipboard::ResetChangeState() {
        clipboardChanged = false;
        lastSequenceNumber = GetClipboardSequenceNumber();
    }

// ===== FORMAT DETECTION =====

    std::vector<std::string> UltraCanvasWindowsClipboard::GetAvailableFormats() {
        std::vector<std::string> formats;

        if (!OpenClipboard(nullptr)) {
            return formats;
        }

        UINT format = 0;
        while ((format = EnumClipboardFormats(format)) != 0) {
            wchar_t name[256] = {};
            int nameLen = GetClipboardFormatNameW(format, name, 256);

            if (nameLen > 0) {
                formats.push_back(UltraCanvasWindowsApplication::Utf16ToUtf8(name));
            } else {
                // Standard format - convert to string
                switch (format) {
                    case CF_TEXT:        formats.push_back("text/plain"); break;
                    case CF_UNICODETEXT: formats.push_back("text/plain;charset=utf-8"); break;
                    case CF_BITMAP:      formats.push_back("image/bmp"); break;
                    case CF_DIB:         formats.push_back("image/bmp"); break;
                    case CF_HDROP:       formats.push_back("text/uri-list"); break;
                    default:
                        formats.push_back("format/" + std::to_string(format));
                        break;
                }
            }
        }

        CloseClipboard();
        return formats;
    }

    bool UltraCanvasWindowsClipboard::IsFormatAvailable(const std::string& format) {
        if (format == "text/plain" || format == "text/plain;charset=utf-8") {
            return IsClipboardFormatAvailable(CF_UNICODETEXT) ||
                   IsClipboardFormatAvailable(CF_TEXT);
        }
        if (format == "image/bmp" || format == "image/png" || format == "image/jpeg") {
            return IsClipboardFormatAvailable(CF_DIB) ||
                   IsClipboardFormatAvailable(CF_BITMAP);
        }
        if (format == "text/uri-list") {
            return IsClipboardFormatAvailable(CF_HDROP);
        }
        return false;
    }

} // namespace UltraCanvas
