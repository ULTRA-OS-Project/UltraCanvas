// OS/MSWindows/UltraCanvasWindowsDragDrop.h
// OLE IDropTarget implementation for drag-and-drop support
// Implements file drop from Windows Explorer and other applications
// Version: 1.0.0
// Last Modified: 2026-03-06
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_WINDOWS_DRAGDROP_H
#define ULTRACANVAS_WINDOWS_DRAGDROP_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <shellapi.h>

// Undefine Windows macros that conflict with UltraCanvas method names
#ifdef DrawText
#undef DrawText
#endif
#ifdef CreateWindow
#undef CreateWindow
#endif
#ifdef CreateDialog
#undef CreateDialog
#endif
#ifdef RGB
#undef RGB
#endif

#include <string>
#include <vector>
#include <functional>

namespace UltraCanvas {

    class UltraCanvasWindowsWindow;

    // Callback types matching Linux XDnD pattern
    using FileDropCallback = std::function<void(const std::vector<std::string>& filePaths, int x, int y)>;
    using DragEnterCallback = std::function<void(int x, int y)>;
    using DragLeaveCallback = std::function<void(int x, int y)>;
    using DragOverCallback = std::function<void(int x, int y)>;

    class UltraCanvasWindowsDropTarget : public IDropTarget {
    public:
        explicit UltraCanvasWindowsDropTarget(UltraCanvasWindowsWindow* window);
        virtual ~UltraCanvasWindowsDropTarget();

        // ===== IUnknown =====
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;

        // ===== IDropTarget =====
        HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pDataObj, DWORD grfKeyState,
                                            POINTL pt, DWORD* pdwEffect) override;
        HRESULT STDMETHODCALLTYPE DragOver(DWORD grfKeyState, POINTL pt,
                                           DWORD* pdwEffect) override;
        HRESULT STDMETHODCALLTYPE DragLeave() override;
        HRESULT STDMETHODCALLTYPE Drop(IDataObject* pDataObj, DWORD grfKeyState,
                                       POINTL pt, DWORD* pdwEffect) override;

        // ===== CALLBACKS =====
        FileDropCallback onFileDrop;
        DragEnterCallback onDragEnter;
        DragLeaveCallback onDragLeave;
        DragOverCallback onDragOver;

    private:
        LONG refCount;
        UltraCanvasWindowsWindow* ownerWindow;
        bool acceptDrop;
        int lastDragX;
        int lastDragY;

        bool ContainsFileDropData(IDataObject* pDataObj);
        std::vector<std::string> ExtractFilePaths(IDataObject* pDataObj);
        POINT ScreenToClientPoint(POINTL pt);
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_WINDOWS_DRAGDROP_H
