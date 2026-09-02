// OS/MSWindows/UltraCanvasWindowsVolumeMonitor.cpp
// WM_DEVICECHANGE backend for UltraCanvasVolumeMonitor: Windows broadcasts a
// message when a volume arrives or goes away - a USB stick, a card, an
// optical disc, a mounted VHD, and (with DBTF_NET set) a network drive mapped
// or dropped - so the volume list is re-read exactly when it changed.
//
// The window has to be a real top-level window, invisible and never shown:
// broadcast messages, and WM_DEVICECHANGE is one, are NOT delivered to
// message-only (HWND_MESSAGE) windows. It gets its own thread with its own
// message loop so it neither depends on nor competes with the application's.
// Version: 1.0.0
// Last Modified: 2026-09-01
// Author: UltraCanvas Framework

#include "UltraCanvasVolumeMonitor.h"

#include <atomic>
#include <set>
#include <string>
#include <thread>

#include <windows.h>
#include <dbt.h>

namespace UltraCanvas {

    namespace {

        constexpr const wchar_t* kWindowClass = L"UltraCanvasVolumeMonitorWindow";
        // Posted to the window's own thread to end its message loop.
        constexpr UINT WM_ULTRACANVAS_VOLUME_QUIT = WM_APP + 1;

        class DeviceChangeVolumeBackend final : public IVolumeMonitorBackend {
        public:
            ~DeviceChangeVolumeBackend() override { Stop(); }

            bool Start(std::function<void()> onChanged) override {
                Stop();
                if (!onChanged) return false;
                callback = std::move(onChanged);

                // The window must be created on the thread that pumps it, so
                // the thread reports back whether it got one.
                started.store(false);
                ready.store(false);
                worker = std::thread([this]() { Run(); });
                while (!ready.load()) ::Sleep(1);
                if (!started.load()) {
                    if (worker.joinable()) worker.join();
                    callback = nullptr;
                    return false;
                }
                return true;
            }

            void Stop() override {
                if (worker.joinable()) {
                    // PostMessage, not SendMessage: this is the caller's
                    // thread, and the target thread must be free to finish
                    // whatever it is dispatching first.
                    const HWND hwnd = window.load();
                    if (hwnd) ::PostMessageW(hwnd, WM_ULTRACANVAS_VOLUME_QUIT, 0, 0);
                    worker.join();
                }
                window.store(nullptr);
                callback = nullptr;
                started.store(false);
                ready.store(false);
            }

        private:
            static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message,
                                               WPARAM wParam, LPARAM lParam) {
                if (message == WM_ULTRACANVAS_VOLUME_QUIT) {
                    ::PostQuitMessage(0);
                    return 0;
                }
                if (message == WM_DEVICECHANGE) {
                    auto* self = reinterpret_cast<DeviceChangeVolumeBackend*>(
                            ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                    if (self) self->OnDeviceChange(wParam, lParam);
                    return TRUE;
                }
                return ::DefWindowProcW(hwnd, message, wParam, lParam);
            }

            void OnDeviceChange(WPARAM wParam, LPARAM lParam) {
                switch (wParam) {
                    case DBT_DEVICEARRIVAL:
                    case DBT_DEVICEREMOVECOMPLETE: {
                        // Only volume events change what is mountable; a
                        // camera or a printer announcing itself does not.
                        auto* header = reinterpret_cast<DEV_BROADCAST_HDR*>(lParam);
                        if (header && header->dbch_devicetype != DBT_DEVTYP_VOLUME)
                            return;
                        break;
                    }
                    case DBT_DEVNODES_CHANGED:
                        // No payload: something in the device tree moved. It
                        // is cheap to re-list and this is what catches the
                        // cases the volume broadcast misses.
                        break;
                    default:
                        return;
                }
                if (callback) callback();
            }

            void Run() {
                WNDCLASSEXW wc = {};
                wc.cbSize = sizeof(wc);
                wc.lpfnWndProc = &WindowProc;
                wc.hInstance = ::GetModuleHandleW(nullptr);
                wc.lpszClassName = kWindowClass;
                // Registering twice (a second monitor, or a re-Start) is not
                // an error: the class stays for the life of the process.
                if (!::RegisterClassExW(&wc) &&
                    ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                    ready.store(true);
                    return;
                }

                // WS_POPUP with no WS_VISIBLE: a top-level window that never
                // appears, has no taskbar entry, and still receives the
                // broadcast a message-only window would not.
                const HWND hwnd = ::CreateWindowExW(
                        0, kWindowClass, L"UltraCanvas Volume Monitor",
                        WS_POPUP, 0, 0, 0, 0,
                        nullptr, nullptr, wc.hInstance, nullptr);
                if (!hwnd) {
                    ready.store(true);
                    return;
                }
                ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                    reinterpret_cast<LONG_PTR>(this));
                window.store(hwnd);
                started.store(true);
                ready.store(true);

                MSG msg;
                while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
                    ::TranslateMessage(&msg);
                    ::DispatchMessageW(&msg);
                }

                // Drop the back-pointer before the window dies: a message
                // still queued must not reach a callback that is going away.
                ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                window.store(nullptr);
                ::DestroyWindow(hwnd);
            }

            std::atomic<HWND> window{nullptr};
            std::atomic<bool> started{false};   // the window exists
            std::atomic<bool> ready{false};     // the thread answered either way
            std::thread worker;
            std::function<void()> callback;
        };

    } // namespace

    std::unique_ptr<IVolumeMonitorBackend> CreateNativeVolumeMonitorBackend() {
        return std::make_unique<DeviceChangeVolumeBackend>();
    }

    std::set<std::string> ListPlatformMountPoints() {
        // Windows mounts volumes at drive letters, and GetLogicalDrives()
        // already answers which of them exist - the same one call the
        // enumeration itself uses, with no medium touched. (The enumeration
        // does not need this on Windows; it is defined so the function means
        // the same thing on every platform for anyone else who calls it.)
        std::set<std::string> points;
        const DWORD mask = ::GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if (mask & (DWORD(1) << i))
                points.insert(std::string(1, char('A' + i)) + ":\\");
        }
        return points;
    }

} // namespace UltraCanvas
