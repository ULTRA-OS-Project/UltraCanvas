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

    // ===== DRAG SOURCE (drag files OUT of a window) =====
    // Counterpart of the drop target: hands a set of files to the shell so it
    // can be dropped on Explorer, another application, or another UltraCanvas
    // window. Both objects are OLE-reference-counted and delete themselves.

    // A CF_HDROP data object built from a list of UTF-8 paths.
    class UltraCanvasWindowsFileDataObject : public IDataObject {
    public:
        explicit UltraCanvasWindowsFileDataObject(const std::vector<std::string>& filePaths);
        virtual ~UltraCanvasWindowsFileDataObject();

        // ===== IUnknown =====
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;

        // ===== IDataObject =====
        HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override;
        HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC* format, STGMEDIUM* medium) override;
        HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override;
        HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC* in, FORMATETC* out) override;
        HRESULT STDMETHODCALLTYPE SetData(FORMATETC* format, STGMEDIUM* medium,
                                          BOOL release) override;
        HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,
                                                IEnumFORMATETC** ppEnum) override;
        HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC* format, DWORD advf,
                                          IAdviseSink* sink, DWORD* connection) override;
        HRESULT STDMETHODCALLTYPE DUnadvise(DWORD connection) override;
        HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** ppEnum) override;

    private:
        LONG refCount;
        std::vector<std::string> paths;
        // DROPFILES header + double-null-terminated wide path list.
        HGLOBAL BuildHDrop() const;
    };

    // Standard IDropSource: Escape / losing the button ends the drag, the
    // shell draws the feedback cursors.
    class UltraCanvasWindowsDragSource : public IDropSource {
    public:
        UltraCanvasWindowsDragSource();
        virtual ~UltraCanvasWindowsDragSource();

        // ===== IUnknown =====
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;

        // ===== IDropSource =====
        HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed,
                                                    DWORD grfKeyState) override;
        HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD dwEffect) override;

    private:
        LONG refCount;
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_WINDOWS_DRAGDROP_H
