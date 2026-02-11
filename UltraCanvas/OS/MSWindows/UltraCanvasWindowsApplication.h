// OS/MSWindows/UltraCanvasWindowsApplication.h
// Windows platform application implementation for UltraCanvas Framework
// TODO: Implement full Windows application backend (cpp file is WIP)
#pragma once

#ifndef ULTRACANVAS_WINDOWS_APPLICATION_H
#define ULTRACANVAS_WINDOWS_APPLICATION_H

// ===== WINDOWS INCLUDES =====
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

// Undefine problematic macros AFTER including windows.h
#ifdef RGB
    #undef RGB
#endif
#ifdef CreateWindow
    #undef CreateWindow
#endif
#ifdef DrawText
    #undef DrawText
#endif
#ifdef CreateDialog
    #undef CreateDialog
#endif

// ===== STANDARD INCLUDES =====
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <chrono>
#include <queue>
#include <condition_variable>

// ===== ULTRACANVAS INCLUDES =====
#include "../../include/UltraCanvasCommonTypes.h"
#include "../../include/UltraCanvasEvent.h"

// Forward declare base class
namespace UltraCanvas {
    class UltraCanvasBaseApplication;
    class UltraCanvasWindowsWindow;
}

// Include base after forward declarations
#include "../../include/UltraCanvasApplication.h"

namespace UltraCanvas {

// ===== WINDOWS APPLICATION CLASS (stub - implementations are WIP) =====
class UltraCanvasWindowsApplication : public UltraCanvasBaseApplication {
private:
    static inline UltraCanvasWindowsApplication* instance = nullptr;

    HINSTANCE hInstance = nullptr;
    std::wstring windowClassName = L"UltraCanvasWindow";

    ID2D1Factory* d2dFactory = nullptr;
    IDWriteFactory* dwriteFactory = nullptr;
    IWICImagingFactory* wicFactory = nullptr;

    std::unordered_map<HWND, UltraCanvasWindowsWindow*> windowMap;
    std::mutex windowMapMutex;

public:
    UltraCanvasWindowsApplication() { instance = this; }
    ~UltraCanvasWindowsApplication() { if (instance == this) instance = nullptr; }

    static UltraCanvasWindowsApplication* GetInstance() { return instance; }

    // ===== OVERRIDES FROM BASE =====
    bool InitializeNative() override {
        // Set console to UTF-8 for proper Unicode output
        SetConsoleOutputCP(CP_UTF8);

        hInstance = GetModuleHandle(nullptr);
        if (!hInstance) return false;

        // Initialize COM (needed for native file dialogs)
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

        // Register window class
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        wc.lpszClassName = windowClassName.c_str();
        RegisterClassExW(&wc);

        initialized = true;
        return true;
    }
    void ShutdownNative() override {
        CoUninitialize();
    }
    void CollectAndProcessNativeEvents() override {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                return;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    void RunInEventLoop() override {
        ::Sleep(1); // Yield CPU when idle to prevent busy-waiting
    }
    void RunBeforeMainLoop() override {}
    void CaptureMouseNative() override {}
    void ReleaseMouseNative() override {}
    bool SelectMouseCursorNative(UltraCanvasWindowBase*, UCMouseCursor) override { return true; }
    bool SelectMouseCursorNative(UltraCanvasWindowBase*, UCMouseCursor, const char*, int, int) override { return true; }

    // ===== FACTORY ACCESS =====
    ID2D1Factory* GetD2DFactory() const { return d2dFactory; }
    IDWriteFactory* GetDWriteFactory() const { return dwriteFactory; }
    IWICImagingFactory* GetWICFactory() const { return wicFactory; }
    HINSTANCE GetHInstance() const { return hInstance; }
    const std::wstring& GetWindowClassName() const { return windowClassName; }

    // ===== WINDOW REGISTRATION =====
    void RegisterWindowHandle(HWND hwnd, UltraCanvasWindowsWindow* window) {
        std::lock_guard<std::mutex> lock(windowMapMutex);
        windowMap[hwnd] = window;
    }
    void UnregisterWindowHandle(HWND hwnd) {
        std::lock_guard<std::mutex> lock(windowMapMutex);
        windowMap.erase(hwnd);
    }
    UltraCanvasWindowsWindow* FindWindowByHandle(HWND hwnd) {
        std::lock_guard<std::mutex> lock(windowMapMutex);
        auto it = windowMap.find(hwnd);
        return (it != windowMap.end()) ? it->second : nullptr;
    }

    // ===== CLIPBOARD =====
    std::string GetClipboardTextNative() { return ""; }
    void SetClipboardTextNative(const std::string&) {}

    // ===== WINDOW PROCEDURE =====
    // Declared here, defined in UltraCanvasWindowsWindow.h (after Window class is complete)
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // ===== STRING CONVERSION =====
    static std::wstring StringToWString(const std::string& str) {
        if (str.empty()) return L"";
        int needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
        if (needed <= 0) return L"";
        std::wstring result(needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), needed);
        return result;
    }
    static std::string WStringToString(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
        if (needed <= 0) return "";
        std::string result(needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), needed, nullptr, nullptr);
        return result;
    }
};

} // namespace UltraCanvas

#endif // ULTRACANVAS_WINDOWS_APPLICATION_H
