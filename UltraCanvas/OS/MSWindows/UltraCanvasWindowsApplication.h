// OS/MSWindows/UltraCanvasWindowsApplication.h
// Windows platform implementation for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2026-03-06
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_WINDOWS_APPLICATION_H
#define ULTRACANVAS_WINDOWS_APPLICATION_H

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasApplication.h"
#include "../../include/UltraCanvasCommonTypes.h"
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasWindow.h"

// ===== WINDOWS PLATFORM INCLUDES =====
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM, GET_Y_LPARAM macros
#include <imm.h>       // Input Method Editor support

// Undefine Windows macros that conflict with our method names
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

// ===== STANDARD INCLUDES =====
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>

namespace UltraCanvas {

    // Forward declarations
    class UltraCanvasWindowsWindow;

    struct MouseClickInfo {
        HWND window = nullptr;
        DWORD lastClickTime = 0;
        int lastClickX = 0;
        int lastClickY = 0;
        UINT lastButton = 0;
        DWORD doubleClickTime = 250;  // milliseconds
        int doubleClickDistance = 5;   // pixels
    };

// ===== WINDOWS APPLICATION CLASS =====
    class UltraCanvasWindowsApplication : public UltraCanvasBaseApplication {
    private:
        static UltraCanvasWindowsApplication* instance;

        // ===== WIN32 RESOURCES =====
        HINSTANCE hInstance;
        ATOM windowClassAtom;

        // ===== CURSOR CACHE =====
        std::unordered_map<UCMouseCursor, HCURSOR> cursors;

        // ===== DOUBLE-CLICK TRACKING =====
        MouseClickInfo mouseClickInfo;

        // ===== UTF-16 SURROGATE STATE (for WM_CHAR) =====
        wchar_t highSurrogate = 0;

        // ===== WINDOW CLASS NAME =====
        static const wchar_t* WINDOW_CLASS_NAME;

    public:
        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasWindowsApplication();

        static UltraCanvasWindowsApplication* GetInstance() {
            return instance;
        }

        // ===== WINDOWS-SPECIFIC METHODS =====
        HINSTANCE GetHInstance() const { return hInstance; }
        ATOM GetWindowClassAtom() const { return windowClassAtom; }
        static const wchar_t* GetWindowClassName() { return WINDOW_CLASS_NAME; }

        void ProcessWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        /**
         * Set the maximum time interval (in milliseconds) between clicks
         * to be considered a double-click
         */
        void SetDoubleClickTime(unsigned int milliseconds) {
            mouseClickInfo.doubleClickTime = milliseconds;
        }

        /**
         * Set the maximum distance (in pixels) the mouse can move between
         * clicks to be considered a double-click
         */
        void SetDoubleClickDistance(int pixels) {
            mouseClickInfo.doubleClickDistance = pixels;
        }

        bool SelectMouseCursorNative(UltraCanvasWindowBase* win, UCMouseCursor cur) override;
        bool SelectMouseCursorNative(UltraCanvasWindowBase* win, UCMouseCursor cur,
                                     const char* filename, int hotspotX, int hotspotY) override;

        // ===== UTF-8 CONVERSION UTILITIES =====
        static std::wstring Utf8ToUtf16(const std::string& utf8);
        static std::string Utf16ToUtf8(const std::wstring& utf16);

    protected:
        // ===== INHERITED FROM BASE APPLICATION =====
        bool InitializeNative() override;
        void ShutdownNative() override;
        void CaptureMouseNative() override;
        void ReleaseMouseNative() override;
        void CollectAndProcessNativeEvents() override;

    private:
        // ===== INTERNAL INITIALIZATION =====
        bool RegisterWindowClass();
        void UnregisterWindowClass();

        // ===== WNDPROC =====
        static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        // ===== EVENT CONVERSION =====
        UCKeys ConvertVKToUCKey(WPARAM vk);
        UCMouseButton ConvertWin32ButtonToUCButton(UINT msg, WPARAM wParam);

        // ===== CURSOR LOADING =====
        HCURSOR LoadCursorFromImageFile(const char* filename, int hotspotX, int hotspotY);
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_WINDOWS_APPLICATION_H
