// OS/MSWindows/UltraCanvasWindowsTrash.cpp
// MoveToTrash on Windows: the Recycle Bin, through the shell, so the item is
// restored by Explorer's own "Restore" and counted in the Bin's size.
//
// SHFileOperationW with FOF_ALLOWUNDO recycles instead of deleting. Silent
// otherwise (no confirmation, no progress window - the caller shows its own),
// with one exception kept on purpose: FOF_WANTNUKEWARNING. An item the Recycle
// Bin cannot take - on a network share, on a drive whose Bin is switched off,
// or larger than the Bin - would otherwise be deleted for good without a
// word; with the flag the shell asks first, and a "No" there reports as a
// failure here, leaving the file where it was.
// The Filer calls this from its file-operation worker thread; the shell's file
// operations expect COM there, so the call brings its own apartment.
// Version: 1.0.0
// Last Modified: 2026-09-24
// Author: UltraCanvas Framework

#include "UltraCanvasTrash.h"
#include "UltraCanvasUtils.h"   // Utf8ToWide

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>

namespace UltraCanvas {

    bool NativeMoveToTrash(const std::string& path, std::string& error) {
        std::wstring wide = Utf8ToWide(path);
        if (wide.empty()) {
            error = "the file name cannot be converted";
            return false;
        }
        for (wchar_t& c : wide) if (c == L'/') c = L'\\';
        while (wide.size() > 3 && wide.back() == L'\\') wide.pop_back();
        // SHFileOperation takes a list terminated by an empty string.
        wide.push_back(L'\0');
        wide.push_back(L'\0');

        // Every successful call is balanced - S_FALSE (COM already running
        // in this model) included. RPC_E_CHANGED_MODE means the thread runs
        // the other model: nothing was opened, so nothing is closed.
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                    COINIT_DISABLE_OLE1DDE);
        struct ComScope {
            bool opened;
            ~ComScope() { if (opened) CoUninitialize(); }
        } comScope{SUCCEEDED(com)};

        SHFILEOPSTRUCTW operation{};
        operation.wFunc  = FO_DELETE;
        operation.pFrom  = wide.c_str();
        operation.fFlags = static_cast<FILEOP_FLAGS>(
                FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI |
                FOF_SILENT | FOF_WANTNUKEWARNING);
        const int result = SHFileOperationW(&operation);
        if (operation.fAnyOperationsAborted) {
            error = "it was not moved to the Recycle Bin";
            return false;
        }
        if (result != 0) {
            // SHFileOperation answers with its own DE_* codes, not Win32 ones;
            // the number is still what a support request needs.
            error = "the shell refused to recycle it (code " +
                    std::to_string(result) + ")";
            return false;
        }
        return true;
    }

} // namespace UltraCanvas
