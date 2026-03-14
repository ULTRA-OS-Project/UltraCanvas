// OS/MSWindows/UltraCanvasWindowsDragDrop.cpp
// OLE IDropTarget implementation for drag-and-drop support
// Version: 1.0.0
// Last Modified: 2026-03-06
// Author: UltraCanvas Framework

#include "UltraCanvasWindowsDragDrop.h"
#include "../../include/UltraCanvasWindow.h"
#include "UltraCanvasWindowsApplication.h"
#include <iostream>
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

} // namespace UltraCanvas
