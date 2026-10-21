// OS/MSWindows/UltraCanvasWindowsSessionLock.cpp
// The session-lock backend of UltraCanvasSessionLock.h for Windows: WTS
// session notifications. Windows sends WM_WTSSESSION_CHANGE with
// WTS_SESSION_LOCK when the workstation locks (Win+L, the screensaver's
// "On resume, display logon screen", a remote session disconnecting to the
// lock screen) and WTS_SESSION_UNLOCK when the user signs back in.
//
// The notifications go to a window, so there is one: message-only, created
// on the UI thread, whose messages the application's own loop dispatches -
// no thread of its own. wtsapi32 is loaded when the first listener arrives
// rather than linked, so a program that never listens does not load it.
// Version: 1.0.0
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework
#include "UltraCanvasSessionLock.h"
#include "UltraCanvasApplication.h"

#include <windows.h>
#include <wtsapi32.h>

namespace UltraCanvas {

    namespace {
        constexpr const wchar_t* kWindowClass = L"UltraCanvasSessionLockWindow";

        using RegisterNotification = BOOL (WINAPI*)(HWND, DWORD);
        using UnregisterNotification = BOOL (WINAPI*)(HWND);

        HMODULE gWtsApi = nullptr;
        UnregisterNotification gUnregister = nullptr;
        HWND gWindow = nullptr;

        LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
            if (message == WM_WTSSESSION_CHANGE && (wParam == WTS_SESSION_LOCK || wParam == WTS_SESSION_UNLOCK)) {
                const bool locked = wParam == WTS_SESSION_LOCK;
                // Reported from a fresh turn of the loop: a listener that
                // removes itself destroys this window.
                if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
                    app->PostToUIThread([locked]() { NotifySessionLockChanged(locked); });
                }
                return 0;
            }
            return ::DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    namespace SessionLockBackend {

        bool Start() {
            if (gWindow) return true;
            gWtsApi = ::LoadLibraryW(L"wtsapi32.dll");
            if (!gWtsApi) return false;
            auto registerFn = reinterpret_cast<RegisterNotification>(
                    reinterpret_cast<void*>(::GetProcAddress(gWtsApi, "WTSRegisterSessionNotification")));
            gUnregister = reinterpret_cast<UnregisterNotification>(
                    reinterpret_cast<void*>(::GetProcAddress(gWtsApi, "WTSUnRegisterSessionNotification")));
            if (!registerFn || !gUnregister) {
                Stop();
                return false;
            }

            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = &WindowProc;
            wc.hInstance = ::GetModuleHandleW(nullptr);
            wc.lpszClassName = kWindowClass;
            if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                Stop();
                return false;
            }
            gWindow = ::CreateWindowExW(0, kWindowClass, L"UltraCanvas Session Lock", 0, 0, 0, 0, 0,
                                        HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
            if (!gWindow || !registerFn(gWindow, NOTIFY_FOR_THIS_SESSION)) {
                Stop();
                return false;
            }
            return true;
        }

        void Stop() {
            if (gWindow) {
                if (gUnregister) gUnregister(gWindow);
                ::DestroyWindow(gWindow);
                gWindow = nullptr;
            }
            gUnregister = nullptr;
            if (gWtsApi) {
                ::FreeLibrary(gWtsApi);
                gWtsApi = nullptr;
            }
        }

    } // namespace SessionLockBackend

} // namespace UltraCanvas
