// OS/MSWindows/UltraCanvasWindowsCursor.cpp
// Win32 cursor management implementation
// Version: 1.0.0
// Last Modified: 2026-03-06
// Author: UltraCanvas Framework

#include "UltraCanvasWindowsApplication.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <iostream>

namespace UltraCanvas {

    bool UltraCanvasWindowsApplication::SelectMouseCursorNative(
            UltraCanvasWindowBase* win, UCMouseCursor cursor) {

        auto it = cursors.find(cursor);
        if (it != cursors.end()) {
            SetCursor(it->second);
            return true;
        }

        HCURSOR hCursor = nullptr;
        LPCTSTR cursorId = nullptr;

        switch (cursor) {
            case UCMouseCursor::Arrow:
                cursorId = IDC_ARROW;
                break;
            case UCMouseCursor::NoCursor:
                cursors[cursor] = nullptr;
                SetCursor(nullptr);
                return true;
            case UCMouseCursor::Hand:
                cursorId = IDC_HAND;
                break;
            case UCMouseCursor::Text:
                cursorId = IDC_IBEAM;
                break;
            case UCMouseCursor::Wait:
                cursorId = IDC_WAIT;
                break;
            case UCMouseCursor::Cross:
                cursorId = IDC_CROSS;
                break;
            case UCMouseCursor::Help:
                cursorId = IDC_HELP;
                break;
            case UCMouseCursor::NotAllowed:
                cursorId = IDC_NO;
                break;
            case UCMouseCursor::LookingGlass:
                cursorId = IDC_CROSS;
                break;
            case UCMouseCursor::SizeAll:
                cursorId = IDC_SIZEALL;
                break;
            case UCMouseCursor::SizeNS:
                cursorId = IDC_SIZENS;
                break;
            case UCMouseCursor::SizeWE:
                cursorId = IDC_SIZEWE;
                break;
            case UCMouseCursor::SizeNWSE:
                cursorId = IDC_SIZENWSE;
                break;
            case UCMouseCursor::SizeNESW:
                cursorId = IDC_SIZENESW;
                break;
            default:
                cursorId = IDC_ARROW;
                break;
        }

        if (cursorId) {
            hCursor = LoadCursor(nullptr, cursorId);
        }

        if (hCursor) {
            cursors[cursor] = hCursor;
            SetCursor(hCursor);
            return true;
        }

        // Fallback to arrow
        hCursor = LoadCursor(nullptr, IDC_ARROW);
        cursors[cursor] = hCursor;
        SetCursor(hCursor);
        return true;
    }

    bool UltraCanvasWindowsApplication::SelectMouseCursorNative(
            UltraCanvasWindowBase* win, UCMouseCursor cursor,
            const char* filename, int hotspotX, int hotspotY) {

        if (!filename || filename[0] == '\0') {
            return SelectMouseCursorNative(win, cursor);
        }

        HCURSOR hCursor = LoadCursorFromImageFile(filename, hotspotX, hotspotY);
        if (hCursor) {
            auto it = cursors.find(cursor);
            if (it != cursors.end() && it->second) {
                DestroyCursor(it->second);
            }
            cursors[cursor] = hCursor;
            SetCursor(hCursor);
            return true;
        }

        std::cerr << "UltraCanvas Cursor: Failed to load cursor from '"
                  << filename << "'" << std::endl;
        return SelectMouseCursorNative(win, cursor);
    }

    HCURSOR UltraCanvasWindowsApplication::LoadCursorFromImageFile(
            const char* filename, int hotspotX, int hotspotY) {

        std::wstring wpath = Utf8ToUtf16(filename);

        // Try loading as .cur or .ani file first
        HCURSOR hCursor = LoadCursorFromFileW(wpath.c_str());
        if (hCursor) {
            return hCursor;
        }

        // Try loading as an icon file (.ico) and convert to cursor
        HICON hIcon = static_cast<HICON>(LoadImageW(
            nullptr, wpath.c_str(), IMAGE_ICON, 0, 0,
            LR_LOADFROMFILE | LR_DEFAULTSIZE));
        if (hIcon) {
            ICONINFO iconInfo = {};
            if (GetIconInfo(hIcon, &iconInfo)) {
                iconInfo.fIcon = FALSE;  // Mark as cursor
                iconInfo.xHotspot = hotspotX;
                iconInfo.yHotspot = hotspotY;
                hCursor = CreateIconIndirect(&iconInfo);
                if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
                if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
            }
            DestroyIcon(hIcon);
            return hCursor;
        }

        return nullptr;
    }

} // namespace UltraCanvas
