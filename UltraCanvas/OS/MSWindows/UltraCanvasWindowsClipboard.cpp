// OS/MSWindows/UltraCanvasWindowsClipboard.cpp
// Win32 Clipboard implementation
// Version: 1.1.0 - images go on as PNG, CF_DIBV5 and CF_DIB and come off
//                  as PNG; files go on the way Explorer puts them; secret
//                  text is kept out of clipboard histories
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "UltraCanvasWindowsClipboard.h"
#include "UltraCanvasWindowsApplication.h"
#include "UltraCanvasClipboardDib.h"
#include <shlobj.h>   // ILCreateFromPathW, ILGetSize, ILFree
#include <iostream>
#include <cstdio>
#include <cstring>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    namespace {
        // Another program - a clipboard manager, Remote Desktop's rdpclip -
        // can hold the clipboard open for a moment right after it changes,
        // and OpenClipboard fails while it does. Retrying briefly turns that
        // into a short wait instead of a copy or paste that does nothing.
        bool OpenClipboardRetrying() {
            for (int attempt = 0; attempt < 20; ++attempt) {
                if (OpenClipboard(nullptr)) return true;
                ::Sleep(10);   // qualified: UCKeys has a Sleep key in this namespace
            }
            debugOutput << "UltraCanvas Clipboard: OpenClipboard failed" << std::endl;
            return false;
        }

        // "PNG" is the name browsers, Office, GIMP, Paint.NET and Krita
        // register for a PNG on the clipboard.
        UINT PngClipboardFormat() {
            return RegisterClipboardFormatW(L"PNG");
        }

        // A movable global block holding a copy of size bytes, ready for
        // SetClipboardData.
        HGLOBAL GlobalCopyOf(const void* data, size_t size) {
            HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, size ? size : 1);
            if (!block) return nullptr;
            void* target = GlobalLock(block);
            if (!target) {
                GlobalFree(block);
                return nullptr;
            }
            if (size) std::memcpy(target, data, size);
            GlobalUnlock(block);
            return block;
        }

        HGLOBAL GlobalCopyOf(const std::vector<uint8_t>& bytes) {
            return bytes.empty() ? nullptr : GlobalCopyOf(bytes.data(), bytes.size());
        }

        // The bytes of one format; the clipboard must be open. A block is
        // often larger than what was put into it, so a reader that cares
        // trims the end (a PNG to its IEND).
        std::vector<uint8_t> ClipboardFormatBytes(UINT format) {
            std::vector<uint8_t> bytes;
            HANDLE block = GetClipboardData(format);
            if (!block) return bytes;
            const auto* data = static_cast<const uint8_t*>(GlobalLock(block));
            if (!data) return bytes;
            bytes.assign(data, data + GlobalSize(block));
            GlobalUnlock(block);
            return bytes;
        }

        // One format to put on the clipboard and the block that holds it.
        // A required format that is refused fails the whole write.
        struct ClipboardFormatBlock {
            UINT format;
            HGLOBAL block;
            bool required;
        };

        // Replaces the clipboard's content with these formats, in this order
        // - the order a program that takes the first format it knows reads
        // them in - with one open of the clipboard, so no other program can
        // read it half written. Owns every block: the clipboard keeps the
        // ones it accepts and the rest are freed.
        bool ReplaceClipboardContent(const std::vector<ClipboardFormatBlock>& formats) {
            bool missingRequired = false;
            for (const auto& entry : formats) {
                if (entry.required && !entry.block) missingRequired = true;
            }
            if (missingRequired || !OpenClipboardRetrying()) {
                for (const auto& entry : formats) {
                    if (entry.block) GlobalFree(entry.block);
                }
                return false;
            }
            EmptyClipboard();
            bool anySet = false, requiredSet = true;
            for (const auto& entry : formats) {
                if (!entry.block) continue;
                if (SetClipboardData(entry.format, entry.block)) {
                    anySet = true;
                } else {
                    GlobalFree(entry.block);
                    if (entry.required) requiredSet = false;
                }
            }
            CloseClipboard();
            if (!anySet || !requiredSet) {
                debugOutput << "UltraCanvas Clipboard: SetClipboardData failed" << std::endl;
            }
            return anySet && requiredSet;
        }

        // "Shell IDList Array" (CFSTR_SHELLIDLIST): the files as shell item
        // ID lists, which is what the shell's own data-object helpers read
        // (SHCreateShellItemArrayFromDataObject among them), so programs
        // built on them take a file list only when it carries this. A CIDA:
        // the count, then count + 1 offsets - the parent folder's ID list,
        // then each item's relative to it. The parent here is the desktop,
        // whose ID list is empty, so each file's absolute list serves as its
        // relative one.
        HGLOBAL ShellIdListOf(const std::vector<std::wstring>& paths) {
            std::vector<PIDLIST_ABSOLUTE> items;
            size_t total = sizeof(UINT) * (paths.size() + 2) + sizeof(USHORT);
            for (const auto& path : paths) {
                PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.c_str());
                if (!item) break;
                items.push_back(item);
                total += ILGetSize(item);
            }
            HGLOBAL block = nullptr;
            if (!items.empty() && items.size() == paths.size()) {
                block = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, total);
                auto* base = block ? static_cast<uint8_t*>(GlobalLock(block)) : nullptr;
                if (base) {
                    auto* header = reinterpret_cast<UINT*>(base);
                    header[0] = static_cast<UINT>(items.size());
                    size_t at = sizeof(UINT) * (items.size() + 2);
                    header[1] = static_cast<UINT>(at);   // the desktop: an empty list, already zero
                    at += sizeof(USHORT);
                    for (size_t i = 0; i < items.size(); ++i) {
                        header[i + 2] = static_cast<UINT>(at);
                        const UINT size = ILGetSize(items[i]);
                        std::memcpy(base + at, items[i], size);
                        at += size;
                    }
                    GlobalUnlock(block);
                } else if (block) {
                    GlobalFree(block);
                    block = nullptr;
                }
            }
            for (PIDLIST_ABSOLUTE item : items) ILFree(item);
            return block;
        }
    }

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
        debugOutput << "UltraCanvas: Windows clipboard initialized" << std::endl;
        return true;
    }

    void UltraCanvasWindowsClipboard::Shutdown() {
        debugOutput << "UltraCanvas: Windows clipboard shut down" << std::endl;
    }

// ===== TEXT OPERATIONS =====

    bool UltraCanvasWindowsClipboard::GetClipboardText(std::string& text) {
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            return false;
        }

        if (!OpenClipboardRetrying()) {
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
        return WriteText(text, false);
    }

    bool UltraCanvasWindowsClipboard::SetClipboardSecretText(const std::string& text) {
        return WriteText(text, true);
    }

    bool UltraCanvasWindowsClipboard::WriteText(const std::string& text, bool secret) {
        std::wstring wtext = UltraCanvasWindowsApplication::Utf8ToUtf16(text);
        size_t byteSize = (wtext.size() + 1) * sizeof(wchar_t);

        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, byteSize);
        if (!hMem) {
            debugOutput << "UltraCanvas Clipboard: GlobalAlloc failed" << std::endl;
            return false;
        }

        auto* pMem = static_cast<wchar_t*>(GlobalLock(hMem));
        if (!pMem) {
            GlobalFree(hMem);
            return false;
        }
        std::memcpy(pMem, wtext.c_str(), byteSize);
        GlobalUnlock(hMem);

        if (!OpenClipboardRetrying()) {
            GlobalFree(hMem);
            return false;
        }

        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
            GlobalFree(hMem);
            CloseClipboard();
            debugOutput << "UltraCanvas Clipboard: SetClipboardData failed" << std::endl;
            return false;
        }

        if (secret) {
            // Each marker is a registered format holding a DWORD; for
            // ExcludeClipboardContentFromMonitorProcessing its presence is
            // what counts, for the other two the value 0 means "no".
            for (const wchar_t* name : {L"ExcludeClipboardContentFromMonitorProcessing",
                                        L"CanIncludeInClipboardHistory",
                                        L"CanUploadToCloudClipboard"}) {
                const UINT format = RegisterClipboardFormatW(name);
                HGLOBAL hFlag = format ? GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD)) : nullptr;
                if (hFlag && !SetClipboardData(format, hFlag)) GlobalFree(hFlag);
            }
        }

        CloseClipboard();
        return true;
    }

    bool UltraCanvasWindowsClipboard::IsClipboardMarkedSecret() {
        const UINT exclude = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
        if (exclude && IsClipboardFormatAvailable(exclude)) return true;

        const UINT history = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
        if (!history || !IsClipboardFormatAvailable(history) || !OpenClipboard(nullptr)) return false;
        bool secret = false;
        if (HANDLE hFlag = GetClipboardData(history)) {
            if (GlobalSize(hFlag) >= sizeof(DWORD)) {
                if (const auto* value = static_cast<const DWORD*>(GlobalLock(hFlag))) {
                    secret = *value == 0;
                    GlobalUnlock(hFlag);
                }
            }
        }
        CloseClipboard();
        return secret;
    }

// ===== HTML =====
    // CF_HTML: UTF-8 with a header giving byte offsets of the document and of
    // the fragment inside it.
    bool UltraCanvasWindowsClipboard::SetClipboardHtml(const std::string& html, const std::string& plainText) {
        const UINT format = RegisterClipboardFormatW(L"HTML Format");
        if (!format) return SetClipboardText(plainText);
        const std::string prefix = "<html><body>\r\n<!--StartFragment-->";
        const std::string suffix = "<!--EndFragment-->\r\n</body></html>";
        // Fixed-width offsets, so the header's length is known before them.
        const char* headerTemplate = "Version:0.9\r\nStartHTML:%010u\r\nEndHTML:%010u\r\n"
                                     "StartFragment:%010u\r\nEndFragment:%010u\r\n";
        char header[160];
        const unsigned headerLength = static_cast<unsigned>(std::snprintf(header, sizeof(header), headerTemplate, 0u, 0u, 0u, 0u));
        const unsigned startHtml = headerLength;
        const unsigned startFragment = startHtml + static_cast<unsigned>(prefix.size());
        const unsigned endFragment = startFragment + static_cast<unsigned>(html.size());
        const unsigned endHtml = endFragment + static_cast<unsigned>(suffix.size());
        std::snprintf(header, sizeof(header), headerTemplate, startHtml, endHtml, startFragment, endFragment);
        const std::string payload = std::string(header) + prefix + html + suffix;

        std::wstring wtext = UltraCanvasWindowsApplication::Utf8ToUtf16(plainText);
        const size_t textBytes = (wtext.size() + 1) * sizeof(wchar_t);
        HGLOBAL hText = GlobalAlloc(GMEM_MOVEABLE, textBytes);
        HGLOBAL hHtml = GlobalAlloc(GMEM_MOVEABLE, payload.size() + 1);
        if (!hText || !hHtml) {
            if (hText) GlobalFree(hText);
            if (hHtml) GlobalFree(hHtml);
            return false;
        }
        std::memcpy(GlobalLock(hText), wtext.c_str(), textBytes);
        GlobalUnlock(hText);
        std::memcpy(GlobalLock(hHtml), payload.c_str(), payload.size() + 1);
        GlobalUnlock(hHtml);
        if (!OpenClipboardRetrying()) {
            GlobalFree(hText);
            GlobalFree(hHtml);
            return false;
        }
        EmptyClipboard();
        const bool textSet = SetClipboardData(CF_UNICODETEXT, hText) != nullptr;
        if (!textSet) GlobalFree(hText);
        const bool htmlSet = SetClipboardData(format, hHtml) != nullptr;
        if (!htmlSet) GlobalFree(hHtml);
        CloseClipboard();
        return textSet;
    }

    bool UltraCanvasWindowsClipboard::GetClipboardHtml(std::string& html) {
        const UINT format = RegisterClipboardFormatW(L"HTML Format");
        if (!format || !IsClipboardFormatAvailable(format)) return false;
        if (!OpenClipboardRetrying()) return false;
        HANDLE hData = GetClipboardData(format);
        std::string payload;
        if (hData) {
            if (const char* data = static_cast<const char*>(GlobalLock(hData))) {
                payload.assign(data, strnlen(data, GlobalSize(hData)));
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
        if (payload.empty()) return false;
        // The fragment between its offsets; the whole document without them.
        auto offset = [&](const char* key) -> long {
            const size_t at = payload.find(key);
            if (at == std::string::npos) return -1;
            long value = 0;
            for (size_t i = at + std::strlen(key); i < payload.size() && payload[i] >= '0' && payload[i] <= '9'; i++) {
                value = value * 10 + (payload[i] - '0');
            }
            return value;
        };
        const long start = offset("StartFragment:"), end = offset("EndFragment:");
        if (start >= 0 && end > start && static_cast<size_t>(end) <= payload.size()) {
            html = payload.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
        } else {
            const long startHtml = offset("StartHTML:");
            html = startHtml >= 0 && static_cast<size_t>(startHtml) < payload.size()
                 ? payload.substr(static_cast<size_t>(startHtml)) : payload;
        }
        return !html.empty();
    }

// ===== IMAGE OPERATIONS =====

    // A picture comes off the clipboard as PNG, the form every decoder in the
    // framework reads. Most programs that copy one put a "PNG" next to the
    // bitmap (browsers, Office, GIMP, Paint.NET, Krita, and the framework
    // itself), which keeps transparency and comes over byte for byte.
    // Without one, the DIB that every Windows program copying a picture
    // offers (Windows makes one from a CF_BITMAP too) is converted. This used
    // to hand back the raw CF_DIB block as "image/bmp": a bitmap with no file
    // header, which nothing could decode, so a picture copied in another
    // program pasted as nothing.
    bool UltraCanvasWindowsClipboard::GetClipboardImage(
            std::vector<uint8_t>& imageData, std::string& format) {
        const UINT pngFormat = PngClipboardFormat();
        const bool hasPng = pngFormat && IsClipboardFormatAvailable(pngFormat);
        // CF_DIBV5 first: only its header can say the fourth byte is alpha.
        std::vector<UINT> dibFormats;
        if (IsClipboardFormatAvailable(CF_DIBV5)) dibFormats.push_back(CF_DIBV5);
        if (IsClipboardFormatAvailable(CF_DIB)) dibFormats.push_back(CF_DIB);
        if (!hasPng && dibFormats.empty()) {
            return false;
        }

        if (hasPng && OpenClipboardRetrying()) {
            std::vector<uint8_t> png = ClipboardFormatBytes(pngFormat);
            CloseClipboard();
            const size_t length = ClipboardDib::PngStreamLength(png.data(), png.size());
            if (length) {
                png.resize(length);
                imageData = std::move(png);
                format = "image/png";
                return true;
            }
        }

        for (UINT dibFormat : dibFormats) {
            if (!OpenClipboardRetrying()) return false;
            std::vector<uint8_t> dib = ClipboardFormatBytes(dibFormat);
            CloseClipboard();   // before converting: other programs wait while it is open
            std::vector<uint8_t> png;
            if (ClipboardDib::DibToPng(dib.data(), dib.size(), png)) {
                imageData = std::move(png);
                format = "image/png";
                return true;
            }
        }
        return false;
    }

    // Every program reads a picture from the clipboard its own way, so it
    // goes on in each form one may ask for: "PNG" (browsers, Office, GIMP,
    // Paint.NET, Krita - transparency survives), CF_DIBV5 (alpha-aware
    // readers of a bitmap) and CF_DIB (the programs that read only a bitmap;
    // Windows makes CF_BITMAP from it). This used to put the
    // PNG bytes under CF_DIB, which gave every other program a bitmap header
    // it could not read, so nothing copied here pasted anywhere else.
    bool UltraCanvasWindowsClipboard::SetClipboardImage(
            const std::vector<uint8_t>& imageData, const std::string& format) {
        if (imageData.empty()) return false;

        ClipboardDib::RgbaImage image;
        std::vector<uint8_t> png;
        if (ClipboardDib::IsPng(imageData.data(), imageData.size())) {
            png = imageData;
            ClipboardDib::DecodePng(png.data(), png.size(), image);
        } else {
            // A .bmp file, or a bare DIB as this backend used to hand out.
            std::vector<uint8_t> dib = ClipboardDib::DibOfBmpFile(imageData.data(), imageData.size());
            const std::vector<uint8_t>& source = dib.empty() ? imageData : dib;
            if (ClipboardDib::DecodeDib(source.data(), source.size(), image)) {
                ClipboardDib::EncodePng(image, png);
            }
        }

        std::vector<ClipboardFormatBlock> formats;
        if (!png.empty()) {
            formats.push_back({PngClipboardFormat(), GlobalCopyOf(png), false});
        }
        if (image.IsValid()) {
            formats.push_back({CF_DIBV5, GlobalCopyOf(ClipboardDib::EncodeDibV5(image)), false});
            formats.push_back({CF_DIB, GlobalCopyOf(ClipboardDib::EncodeDib24(image)), false});
        }
        if (formats.empty()) {
            // A JPEG, a GIF, ... that this backend cannot decode: under the
            // name Windows programs know it by, else its MIME type, so a
            // program that reads that format still gets it.
            const std::wstring name = format == "image/jpeg" ? L"JFIF"
                                    : format == "image/gif"  ? L"GIF"
                                    : UltraCanvasWindowsApplication::Utf8ToUtf16(format);
            const UINT registered = name.empty() ? 0 : RegisterClipboardFormatW(name.c_str());
            if (!registered) return false;
            formats.push_back({registered, GlobalCopyOf(imageData), true});
        }
        return ReplaceClipboardContent(formats);
    }

// ===== FILE OPERATIONS =====

    bool UltraCanvasWindowsClipboard::GetClipboardFiles(
            std::vector<std::string>& filePaths) {
        if (!IsClipboardFormatAvailable(CF_HDROP)) {
            return false;
        }

        if (!OpenClipboardRetrying()) {
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

    bool UltraCanvasWindowsClipboard::GetClipboardFiles(
            std::vector<std::string>& filePaths, bool& cutOperation) {
        cutOperation = false;
        if (!GetClipboardFiles(filePaths)) {
            return false;
        }

        // The shell marks a cut (Ctrl+X in Explorer) with the registered
        // "Preferred DropEffect" format holding DROPEFFECT_MOVE.
        UINT dropEffectFormat = RegisterClipboardFormatW(L"Preferred DropEffect");
        if (dropEffectFormat != 0 &&
            IsClipboardFormatAvailable(dropEffectFormat) &&
            OpenClipboardRetrying()) {
            HANDLE hEffect = GetClipboardData(dropEffectFormat);
            if (hEffect) {
                DWORD* pEffect = static_cast<DWORD*>(GlobalLock(hEffect));
                if (pEffect) {
                    cutOperation = (*pEffect & 2 /* DROPEFFECT_MOVE */) != 0;
                    GlobalUnlock(hEffect);
                }
            }
            CloseClipboard();
        }
        return true;
    }

    // A file list goes on the way Explorer puts one, so a program that
    // pastes files copied in Explorer pastes them from here too: CF_HDROP
    // (the list most programs read), "Shell IDList Array" (what programs
    // built on the shell's data-object helpers read), "FileNameW" (the first
    // file, for programs that take one) and "Preferred DropEffect" (copy or
    // cut). All in one open of the clipboard: the cut marker used to follow
    // in a second open, after another program could already have read the
    // list as a copy.
    bool UltraCanvasWindowsClipboard::SetClipboardFiles(
            const std::vector<std::string>& filePaths, bool cutOperation) {
        if (filePaths.empty()) return false;

        // DROPFILES followed by the double-null-terminated wide file list:
        // file1\0file2\0...fileN\0\0
        std::vector<std::wstring> widePaths;
        std::wstring allPaths;
        for (const auto& path : filePaths) {
            widePaths.push_back(UltraCanvasWindowsApplication::Utf8ToUtf16(path));
            allPaths += widePaths.back();
            allPaths += L'\0';
        }
        allPaths += L'\0';  // Double-null terminator

        std::vector<uint8_t> dropFiles(sizeof(DROPFILES) + allPaths.size() * sizeof(wchar_t), 0);
        auto* header = reinterpret_cast<DROPFILES*>(dropFiles.data());
        header->pFiles = sizeof(DROPFILES);
        header->fWide = TRUE;  // Using wide characters
        std::memcpy(dropFiles.data() + sizeof(DROPFILES), allPaths.data(), allPaths.size() * sizeof(wchar_t));

        // Copy is DROPEFFECT_COPY | DROPEFFECT_LINK, as Explorer writes it;
        // cut is DROPEFFECT_MOVE.
        const DWORD dropEffect = cutOperation ? 2 : 5;
        const std::wstring& firstPath = widePaths.front();

        std::vector<ClipboardFormatBlock> formats;
        if (UINT idList = RegisterClipboardFormatW(L"Shell IDList Array")) {
            formats.push_back({idList, ShellIdListOf(widePaths), false});
        }
        formats.push_back({CF_HDROP, GlobalCopyOf(dropFiles), true});
        if (UINT fileName = RegisterClipboardFormatW(L"FileNameW")) {
            formats.push_back({fileName, GlobalCopyOf(firstPath.c_str(), (firstPath.size() + 1) * sizeof(wchar_t)), false});
        }
        if (UINT effect = RegisterClipboardFormatW(L"Preferred DropEffect")) {
            formats.push_back({effect, GlobalCopyOf(&dropEffect, sizeof(dropEffect)), false});
        }
        return ReplaceClipboardContent(formats);
    }

    bool UltraCanvasWindowsClipboard::SetClipboardFiles(
            const std::vector<std::string>& filePaths) {
        return SetClipboardFiles(filePaths, false);
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

        if (!OpenClipboardRetrying()) {
            return formats;
        }

        UINT format = 0;
        while ((format = EnumClipboardFormats(format)) != 0) {
            wchar_t name[256] = {};
            int nameLen = GetClipboardFormatNameW(format, name, 256);

            if (nameLen > 0) {
                std::string formatName = UltraCanvasWindowsApplication::Utf16ToUtf8(name);
                // What GetClipboardImage reads it as.
                formats.push_back(formatName == "PNG" ? "image/png" : formatName);
            } else {
                // Standard format - convert to string
                switch (format) {
                    case CF_TEXT:        formats.push_back("text/plain"); break;
                    case CF_UNICODETEXT: formats.push_back("text/plain;charset=utf-8"); break;
                    case CF_BITMAP:      formats.push_back("image/bmp"); break;
                    case CF_DIB:         formats.push_back("image/bmp"); break;
                    case CF_DIBV5:       formats.push_back("image/bmp"); break;
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
            // Any of them is read as an image, and handed over as PNG.
            const UINT pngFormat = PngClipboardFormat();
            return (pngFormat && IsClipboardFormatAvailable(pngFormat)) ||
                   IsClipboardFormatAvailable(CF_DIBV5) ||
                   IsClipboardFormatAvailable(CF_DIB) ||
                   IsClipboardFormatAvailable(CF_BITMAP);
        }
        if (format == "text/uri-list") {
            return IsClipboardFormatAvailable(CF_HDROP);
        }
        return false;
    }

} // namespace UltraCanvas
