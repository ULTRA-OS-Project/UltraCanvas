// OS/MSWindows/UltraCanvasWindowsDragDrop.cpp
// OLE IDropTarget implementation for drag-and-drop support
// Version: 1.0.0
// Last Modified: 2026-03-06
// Author: UltraCanvas Framework

#include "UltraCanvasWindowsDragDrop.h"
#include "../../include/UltraCanvasWindow.h"
#include "UltraCanvasWindowsApplication.h"
#include <iostream>
#include <cstring>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

// ===== CONSTRUCTOR & DESTRUCTOR =====

    UltraCanvasWindowsDropTarget::UltraCanvasWindowsDropTarget(UltraCanvasWindowsWindow* window)
            : refCount(1)
            , ownerWindow(window)
            , acceptDrop(false)
            , lastDragX(0)
            , lastDragY(0) {
    }

    UltraCanvasWindowsDropTarget::~UltraCanvasWindowsDropTarget() = default;

// ===== IUnknown =====

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsDropTarget::QueryInterface(
            REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE UltraCanvasWindowsDropTarget::AddRef() {
        return InterlockedIncrement(&refCount);
    }

    ULONG STDMETHODCALLTYPE UltraCanvasWindowsDropTarget::Release() {
        LONG count = InterlockedDecrement(&refCount);
        if (count == 0) {
            delete this;
        }
        return count;
    }

// ===== IDropTarget =====

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsDropTarget::DragEnter(
            IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {

        acceptDrop = ContainsFileDropData(pDataObj);
        *pdwEffect = acceptDrop ? DROPEFFECT_COPY : DROPEFFECT_NONE;

        if (acceptDrop && onDragEnter) {
            POINT clientPt = ScreenToClientPoint(pt);
            lastDragX = clientPt.x;
            lastDragY = clientPt.y;
            onDragEnter(clientPt.x, clientPt.y);
        }

        debugOutput << "UltraCanvas DragDrop: DragEnter - "
                  << (acceptDrop ? "accepting" : "rejecting") << std::endl;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsDropTarget::DragOver(
            DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {

        *pdwEffect = acceptDrop ? DROPEFFECT_COPY : DROPEFFECT_NONE;

        if (acceptDrop && onDragOver) {
            POINT clientPt = ScreenToClientPoint(pt);
            lastDragX = clientPt.x;
            lastDragY = clientPt.y;
            onDragOver(clientPt.x, clientPt.y);
        }

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsDropTarget::DragLeave() {
        debugOutput << "UltraCanvas DragDrop: DragLeave" << std::endl;

        if (onDragLeave) {
            onDragLeave(lastDragX, lastDragY);
        }

        acceptDrop = false;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsDropTarget::Drop(
            IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {

        *pdwEffect = DROPEFFECT_NONE;

        if (!acceptDrop) {
            return S_OK;
        }

        auto paths = ExtractFilePaths(pDataObj);

        debugOutput << "UltraCanvas DragDrop: Dropped " << paths.size()
                  << " file(s)" << std::endl;
        for (const auto& path : paths) {
            debugOutput << "  -> " << path << std::endl;
        }

        if (!paths.empty() && onFileDrop) {
            POINT clientPt = ScreenToClientPoint(pt);
            onFileDrop(paths, clientPt.x, clientPt.y);
            *pdwEffect = DROPEFFECT_COPY;
        }

        acceptDrop = false;
        return S_OK;
    }

// ===== HELPER METHODS =====

    bool UltraCanvasWindowsDropTarget::ContainsFileDropData(IDataObject* pDataObj) {
        FORMATETC fmt = {};
        fmt.cfFormat = CF_HDROP;
        fmt.ptd = nullptr;
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex = -1;
        fmt.tymed = TYMED_HGLOBAL;

        return pDataObj->QueryGetData(&fmt) == S_OK;
    }

    std::vector<std::string> UltraCanvasWindowsDropTarget::ExtractFilePaths(
            IDataObject* pDataObj) {
        std::vector<std::string> paths;

        FORMATETC fmt = {};
        fmt.cfFormat = CF_HDROP;
        fmt.ptd = nullptr;
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex = -1;
        fmt.tymed = TYMED_HGLOBAL;

        STGMEDIUM stg = {};

        if (SUCCEEDED(pDataObj->GetData(&fmt, &stg))) {
            HDROP hDrop = static_cast<HDROP>(stg.hGlobal);
            UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);

            for (UINT i = 0; i < count; i++) {
                UINT len = DragQueryFileW(hDrop, i, nullptr, 0) + 1;
                std::wstring wpath(len, 0);
                DragQueryFileW(hDrop, i, &wpath[0], len);
                // Remove null terminator
                if (!wpath.empty() && wpath.back() == L'\0') {
                    wpath.pop_back();
                }
                paths.push_back(UltraCanvasWindowsApplication::Utf16ToUtf8(wpath));
            }

            ReleaseStgMedium(&stg);
        }

        return paths;
    }

    POINT UltraCanvasWindowsDropTarget::ScreenToClientPoint(POINTL pt) {
        POINT result = {pt.x, pt.y};
        if (ownerWindow && ownerWindow->GetHWND()) {
            ScreenToClient(ownerWindow->GetHWND(), &result);
        }
        return result;
    }

// =============================================================================
// DRAG SOURCE — CF_HDROP DATA OBJECT
// =============================================================================

    UltraCanvasWindowsFileDataObject::UltraCanvasWindowsFileDataObject(
            const std::vector<std::string>& filePaths)
            : refCount(1)
            , paths(filePaths) {
    }

    UltraCanvasWindowsFileDataObject::~UltraCanvasWindowsFileDataObject() = default;

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsFileDataObject::QueryInterface(
            REFIID riid, void** ppv) {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE UltraCanvasWindowsFileDataObject::AddRef() {
        return InterlockedIncrement(&refCount);
    }

    ULONG STDMETHODCALLTYPE UltraCanvasWindowsFileDataObject::Release() {
        LONG count = InterlockedDecrement(&refCount);
        if (count == 0) delete this;
        return count;
    }

    HGLOBAL UltraCanvasWindowsFileDataObject::BuildHDrop() const {
        // CF_HDROP payload: a DROPFILES header followed by the wide-char paths,
        // each NUL-terminated and the list closed by one more NUL.
        std::wstring list;
        for (const std::string& p : paths) {
            list += UltraCanvasWindowsApplication::Utf8ToUtf16(p);
            list.push_back(L'\0');
        }
        list.push_back(L'\0');

        SIZE_T bytes = sizeof(DROPFILES) + list.size() * sizeof(wchar_t);
        HGLOBAL mem = GlobalAlloc(GHND, bytes);
        if (!mem) return nullptr;

        auto* header = static_cast<DROPFILES*>(GlobalLock(mem));
        if (!header) {
            GlobalFree(mem);
            return nullptr;
        }
        header->pFiles = sizeof(DROPFILES);
        header->pt.x = 0;
        header->pt.y = 0;
        header->fNC = FALSE;
        header->fWide = TRUE;
        std::memcpy(reinterpret_cast<BYTE*>(header) + sizeof(DROPFILES),
                    list.data(), list.size() * sizeof(wchar_t));
        GlobalUnlock(mem);
        return mem;
    }

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsFileDataObject::GetData(
            FORMATETC* format, STGMEDIUM* medium) {
        if (!format || !medium) return E_POINTER;
        if (QueryGetData(format) != S_OK) return DV_E_FORMATETC;

        HGLOBAL mem = BuildHDrop();
        if (!mem) return E_OUTOFMEMORY;

        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = mem;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsFileDataObject::GetDataHere(
            FORMATETC* format, STGMEDIUM* medium) {
        (void)format; (void)medium;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsFileDataObject::QueryGetData(
            FORMATETC* format) {
        if (!format) return E_POINTER;
        if (format->cfFormat != CF_HDROP) return DV_E_FORMATETC;
        if (!(format->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
        if (format->dwAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
        return paths.empty() ? DV_E_FORMATETC : S_OK;
    }

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsFileDataObject::GetCanonicalFormatEtc(
            FORMATETC* in, FORMATETC* out) {
        (void)in;
        if (!out) return E_POINTER;
        out->ptd = nullptr;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsFileDataObject::SetData(
            FORMATETC* format, STGMEDIUM* medium, BOOL release) {
        (void)format; (void)medium; (void)release;
        return E_NOTIMPL;   // read-only source
    }

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsFileDataObject::EnumFormatEtc(
            DWORD direction, IEnumFORMATETC** ppEnum) {
        if (!ppEnum) return E_POINTER;
        *ppEnum = nullptr;
        if (direction != DATADIR_GET) return E_NOTIMPL;

        FORMATETC fmt = {};
        fmt.cfFormat = CF_HDROP;
        fmt.ptd = nullptr;
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex = -1;
        fmt.tymed = TYMED_HGLOBAL;
        return SHCreateStdEnumFmtEtc(1, &fmt, ppEnum);
    }

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsFileDataObject::DAdvise(
            FORMATETC* format, DWORD advf, IAdviseSink* sink, DWORD* connection) {
        (void)format; (void)advf; (void)sink; (void)connection;
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsFileDataObject::DUnadvise(DWORD connection) {
        (void)connection;
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsFileDataObject::EnumDAdvise(
            IEnumSTATDATA** ppEnum) {
        (void)ppEnum;
        return OLE_E_ADVISENOTSUPPORTED;
    }

// =============================================================================
// DRAG SOURCE — IDropSource
// =============================================================================

    UltraCanvasWindowsDragSource::UltraCanvasWindowsDragSource()
            : refCount(1) {
    }

    UltraCanvasWindowsDragSource::~UltraCanvasWindowsDragSource() = default;

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsDragSource::QueryInterface(
            REFIID riid, void** ppv) {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE UltraCanvasWindowsDragSource::AddRef() {
        return InterlockedIncrement(&refCount);
    }

    ULONG STDMETHODCALLTYPE UltraCanvasWindowsDragSource::Release() {
        LONG count = InterlockedDecrement(&refCount);
        if (count == 0) delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsDragSource::QueryContinueDrag(
            BOOL escapePressed, DWORD grfKeyState) {
        if (escapePressed) return DRAGDROP_S_CANCEL;
        // The gesture ends with the button that started it.
        if (!(grfKeyState & (MK_LBUTTON | MK_RBUTTON))) return DRAGDROP_S_DROP;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE UltraCanvasWindowsDragSource::GiveFeedback(DWORD dwEffect) {
        (void)dwEffect;
        return DRAGDROP_S_USEDEFAULTCURSORS;   // the shell draws the feedback
    }

} // namespace UltraCanvas
