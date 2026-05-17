// UltraCanvasApplication.cpp
// Main UltraCanvas App
// Version: 1.4.2 - PANGO_BACKEND=fontconfig env so MSYS2 Pango uses FC instead of Win32 backend
// Last Modified: 2026-05-10
// Author: UltraCanvas Framework

#include <algorithm>
#include <atomic>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasDebug.h"

#if !defined(__APPLE__)
#include <fontconfig/fontconfig.h>
#endif

#if defined(__linux__) || defined(__unix__)
#include <unistd.h>
#include <linux/limits.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#include "UltraCanvasDebug.h"
#endif


namespace UltraCanvas {

    // ===== Singleton accessor for cross-thread PostToUIThread =====
    namespace {
        std::atomic<UltraCanvasApplicationBase*> g_currentApplication{nullptr};
    }

    UltraCanvasApplicationBase::UltraCanvasApplicationBase() {
        // Latch the most-recently-constructed app as the "current" one;
        // UltraCanvas assumes one app per process.
        g_currentApplication.store(this, std::memory_order_release);
    }

    UltraCanvasApplicationBase::~UltraCanvasApplicationBase() {
        UltraCanvasApplicationBase* expected = this;
        g_currentApplication.compare_exchange_strong(
            expected, nullptr, std::memory_order_release);
    }

    UltraCanvasApplicationBase* UltraCanvasApplicationBase::GetCurrent() {
        return g_currentApplication.load(std::memory_order_acquire);
    }

    void UltraCanvasApplicationBase::PostToUIThread(std::function<void()> task) {
        if (!task) return;
        {
            std::lock_guard<std::mutex> lk(postedTasksMutex_);
            postedTasks_.push_back(std::move(task));
        }
        // Wake the loop so the task runs promptly instead of waiting for
        // the next OS event.
        WakeUpEventLoop();
    }

    void UltraCanvasApplicationBase::ProcessPostedTasks() {
        // Swap under lock so concurrent posts can keep enqueueing while we
        // run tasks lock-free. Tasks running here might re-enter
        // PostToUIThread to chain follow-up work; that lands in the next
        // iteration, never the current vector.
        std::vector<std::function<void()>> local;
        {
            std::lock_guard<std::mutex> lk(postedTasksMutex_);
            local.swap(postedTasks_);
        }
        for (auto& fn : local) {
            try {
                fn();
            } catch (const std::exception& e) {
                std::cerr << "UltraCanvas PostToUIThread task threw: "
                          << e.what() << std::endl;
            } catch (...) {
                std::cerr << "UltraCanvas PostToUIThread task threw "
                             "non-std exception" << std::endl;
            }
        }
    }

    const char* const kDejaVuAllFonts[] = {
        "DejaVuSans.ttf", "DejaVuSans-Bold.ttf",
        "DejaVuSans-Oblique.ttf", "DejaVuSans-BoldOblique.ttf",
        "DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf",
        "DejaVuSansMono-Oblique.ttf", "DejaVuSansMono-BoldOblique.ttf",
    };
    const size_t kDejaVuAllFontsCount = sizeof(kDejaVuAllFonts) / sizeof(kDejaVuAllFonts[0]);

    const char* const kDejaVuMonoFonts[] = {
        "DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf",
        "DejaVuSansMono-Oblique.ttf", "DejaVuSansMono-BoldOblique.ttf",
    };
    const size_t kDejaVuMonoFontsCount = sizeof(kDejaVuMonoFonts) / sizeof(kDejaVuMonoFonts[0]);

    std::string GetBundledFontsDir() {
        std::string p = NormalizePath(GetResourcesDir() + "media/fonts/dejavu/");
        return p;
    }

//#if !defined(__APPLE__)
//    // Inline FC rule that pins hinting/AA for the bundled DejaVu families so
//    // system fontconfig defaults (which differ between MSYS2 and Linux distros)
//    // can't change the way the framework's text looks.
//    static const char* kDejaVuFcRules = R"FC(<?xml version="1.0"?>
//<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
//<fontconfig>
//  <match target="font">
//    <test name="family"><string>DejaVu Sans</string></test>
//    <edit name="antialias"      mode="assign"><bool>true</bool></edit>
//    <edit name="hinting"        mode="assign"><bool>true</bool></edit>
//    <edit name="hintstyle"      mode="assign"><const>hintslight</const></edit>
//    <edit name="autohint"       mode="assign"><bool>false</bool></edit>
//    <edit name="embeddedbitmap" mode="assign"><bool>false</bool></edit>
//    <edit name="lcdfilter"      mode="assign"><const>lcddefault</const></edit>
//  </match>
//  <match target="font">
//    <test name="family"><string>DejaVu Sans Mono</string></test>
//    <edit name="antialias"      mode="assign"><bool>true</bool></edit>
//    <edit name="hinting"        mode="assign"><bool>true</bool></edit>
//    <edit name="hintstyle"      mode="assign"><const>hintslight</const></edit>
//    <edit name="autohint"       mode="assign"><bool>false</bool></edit>
//    <edit name="embeddedbitmap" mode="assign"><bool>false</bool></edit>
//    <edit name="lcdfilter"      mode="assign"><const>lcddefault</const></edit>
//  </match>
//</fontconfig>
//)FC";
//
//    bool LoadDejaVuFcRules(void* fcConfig) {
//        FcConfig* cfg = static_cast<FcConfig*>(fcConfig);
//        if (!cfg) return false;
//        FcBool ok = FcConfigParseAndLoadFromMemory(
//            cfg,
//            reinterpret_cast<const FcChar8*>(kDejaVuFcRules),
//            FcTrue);
//        if (!ok) {
//            debugOutput << "UltraCanvas: FcConfigParseAndLoadFromMemory failed for DejaVu rules" << std::endl;
//            return false;
//        }
//        return true;
//    }
//#else
//    bool LoadDejaVuFcRules(void*) { return true; }
//#endif

//#if !defined(__APPLE__)
//    namespace {
//        // Fontconfig's <dir>/<cachedir> path consumer is rooted in POSIX —
//        // it expects forward-slash separators. On Windows our paths come from
//        // NormalizePath() which uses backslashes, and FC silently fails to
//        // scan such paths. Normalise to '/' for everything that goes into
//        // fonts.conf XML element text. (std::filesystem and the WinAPI we
//        // use elsewhere accept both forms, so we only convert here.)
//        std::string ToFcPath(std::string p) {
//#if defined(_WIN32) || defined(_WIN64)
//            std::replace(p.begin(), p.end(), '\\', '/');
//#endif
//            return p;
//        }
//
//        // Builds a minimal fontconfig config XML that points at the absolute
//        // path of the bundled DejaVu directory and (on Linux) <include>s the
//        // system fonts.conf so apps still see non-DejaVu system fonts.
//        std::string ComposeBundledFontconfigXml(const std::string& bundledDir,
//                                                const std::string& cacheDir) {
//            std::ostringstream conf;
//            conf << "<?xml version=\"1.0\"?>\n"
//                 << "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
//                 << "<fontconfig>\n"
//                 << "  <dir>" << ToFcPath(bundledDir) << "</dir>\n";
//#if defined(__linux__) || defined(__unix__)
//            // Pull in system config when present so apps can still resolve
//            // non-DejaVu families on Linux. ignore_missing keeps things
//            // working when the file is absent (containers, AppImages on
//            // hosts without a fontconfig install).
//            conf << "  <include ignore_missing=\"yes\">/etc/fonts/fonts.conf</include>\n";
//            conf << "  <include ignore_missing=\"yes\">/usr/local/etc/fonts/fonts.conf</include>\n";
//#endif
//            // Absolute writable cachedir — `~` doesn't reliably expand on
//            // Windows fontconfig, and a failed cache write can abort the
//            // dir scan entirely.
//            conf << "  <cachedir>" << ToFcPath(cacheDir) << "</cachedir>\n";
//            conf << "</fontconfig>\n";
//            return conf.str();
//        }
//
//        bool WriteFile(const std::string& path, const std::string& contents) {
//            try {
//                std::ofstream out(path, std::ios::binary | std::ios::trunc);
//                if (!out) return false;
//                out << contents;
//                return out.good();
//            } catch (...) {
//                return false;
//            }
//        }
//    }
//
//    void SetupBundledFontconfig() {
//        // Honour an externally-set FONTCONFIG_FILE so users / packagers can
//        // override the framework's choice without code changes.
//        if (const char* existing = std::getenv("FONTCONFIG_FILE")) {
//            if (existing[0] != '\0') {
//                debugOutput << "UltraCanvas: FONTCONFIG_FILE already set to '"
//                            << existing << "', not overriding" << std::endl;
//                return;
//            }
//        }
//
//        // Strip trailing separator from the bundled dir so the <dir> element
//        // doesn't end with a slash that some FC versions don't normalise.
//        std::string dir = GetBundledFontsDir();
//        while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) {
//            dir.pop_back();
//        }
//        if (dir.empty() || !std::filesystem::exists(dir)) {
//            debugOutput << "UltraCanvas: bundled fonts dir not found at '" << dir
//                        << "'; skipping FONTCONFIG_FILE setup" << std::endl;
//            return;
//        }
//
//        std::filesystem::path tempDir;
//        try {
//            tempDir = std::filesystem::temp_directory_path();
//        } catch (...) {
//            debugOutput << "UltraCanvas: could not resolve temp dir; skipping FONTCONFIG_FILE setup" << std::endl;
//            return;
//        }
//        std::filesystem::path confPath = tempDir / "ultracanvas-fonts.conf";
//        std::string confPathStr = confPath.string();
//
//        // Cachedir under the same temp tree — known writable. Best-effort
//        // create; FC will create files inside it as it scans.
//        std::filesystem::path cacheDirPath = tempDir / "uc-fc-cache";
//        {
//            std::error_code ec;
//            std::filesystem::create_directories(cacheDirPath, ec);
//        }
//
//        std::string xml = ComposeBundledFontconfigXml(dir, cacheDirPath.string());
//        if (!WriteFile(confPathStr, xml)) {
//            debugOutput << "UltraCanvas: failed to write fonts.conf at " << confPathStr << std::endl;
//            return;
//        }
//
//#if defined(_WIN32) || defined(_WIN64)
//        if (_putenv_s("FONTCONFIG_FILE", confPathStr.c_str()) != 0) {
//            debugOutput << "UltraCanvas: _putenv_s(FONTCONFIG_FILE) failed" << std::endl;
//            return;
//        }
//#else
//        if (setenv("FONTCONFIG_FILE", confPathStr.c_str(), 1) != 0) {
//            debugOutput << "UltraCanvas: setenv(FONTCONFIG_FILE) failed" << std::endl;
//            return;
//        }
//#endif
//        debugOutput << "UltraCanvas: FONTCONFIG_FILE set to " << confPathStr << std::endl;
//
//        // Force Pango to use the fontconfig backend. On MSYS2 Pango is built
//        // with both fontconfig and win32 backends and may default to win32 —
//        // that backend ignores FC entirely, so even with a perfect FC config
//        // pointing at our DejaVu directory, Pango wouldn't see the fonts.
//        // No-op on Linux (Pango is fontconfig-only there).
//        const char* existingBackend = std::getenv("PANGO_BACKEND");
//        if (existingBackend && existingBackend[0] != '\0') {
//            debugOutput << "UltraCanvas: PANGO_BACKEND already set to '"
//                        << existingBackend << "', not overriding" << std::endl;
//        } else {
//#if defined(_WIN32) || defined(_WIN64)
//            if (_putenv_s("PANGO_BACKEND", "fontconfig") != 0) {
//                debugOutput << "UltraCanvas: _putenv_s(PANGO_BACKEND) failed" << std::endl;
//            } else {
//                debugOutput << "UltraCanvas: PANGO_BACKEND set to fontconfig" << std::endl;
//            }
//#else
//            if (setenv("PANGO_BACKEND", "fontconfig", 1) != 0) {
//                debugOutput << "UltraCanvas: setenv(PANGO_BACKEND) failed" << std::endl;
//            } else {
//                debugOutput << "UltraCanvas: PANGO_BACKEND set to fontconfig" << std::endl;
//            }
//#endif
//        }
//    }
//#else
//    void SetupBundledFontconfig() {}
//#endif

    FontStyle UltraCanvasApplicationBase::GetSystemFontStyle() {
        if (!cachedSystemFontStyle_.has_value()) {
            cachedSystemFontStyle_ = DetectSystemFontStyleNative();
        }
        return cachedSystemFontStyle_.value();
    }

    FontStyle UltraCanvasApplicationBase::GetDefaultMonospacedFontStyle() {
        if (!cachedMonospacedFontStyle_.has_value()) {
            cachedMonospacedFontStyle_ = DetectMonospacedFontStyleNative();
        }
        return cachedMonospacedFontStyle_.value();
    }

    bool UltraCanvasApplicationBase::Initialize(const std::string& app) {
        appName = app;

        UCImage::InitializeImageSubsysterm(appName.c_str());

        if (InitializeNative()) {
            // Register bundled DejaVu fonts before any text rendering / default
            // detection runs, so platform Detect*FontStyleNative() can return
            // the just-registered families.
            LoadBundledFontsNative();

            if (!InitializeClipboard()) {
                debugOutput << "UltraCanvas: Failed to initialize clipboard" << std::endl;
            }

            // Auto-set default window icon if available
            std::string iconPath = NormalizePath(GetResourcesDir() + UC_DEFAULT_ICON_SUBPATH);
            if (std::filesystem::exists(iconPath)) {
                SetDefaultWindowIcon(iconPath);
                debugOutput << "UltraCanvas: Default window icon set to: " << iconPath << std::endl;
            }
#ifdef UCAPP_ICON_PATH
            else {
                // Fallback to app-specific icon defined at build time
                std::string appIconPath = NormalizePath(GetResourcesDir() + UCAPP_ICON_PATH);
                if (std::filesystem::exists(appIconPath)) {
                    SetDefaultWindowIcon(appIconPath);
                    debugOutput << "UltraCanvas: App icon set to: " << appIconPath << std::endl;
                } else {
                    debugOutput << "UltraCanvas: App icon not found at: " << appIconPath << std::endl;
                }
            }
#else
            else {
                debugOutput << "UltraCanvas: Default icon not found at: " << iconPath << std::endl;
            }
#endif

            return true;
        } else {
            debugOutput << "UltraCanvas: Failed to initialize application" << std::endl;
            return false;
        }
    }

    void UltraCanvasApplicationBase::Run() {
        debugOutput << "UltraCanvasBaseApplication::Run Starting app" << std::endl;
        if (!initialized) {
            debugOutput << "UltraCanvas: Cannot run - application not initialized" << std::endl;
            return;
        }

        running = true;

        // Start the event processing thread
        RunBeforeMainLoop();

        auto clipbrd = GetClipboard();

        debugOutput << "UltraCanvas: Starting main loop..." << std::endl;
        try {
            while (running && !windows.empty()) {
                CollectAndProcessNativeEvents();

                // Process all pending events
                ProcessEvents();

                // Fire expired timers
                ProcessTimers();

                // Run anything PostToUIThread enqueued from a background
                // thread (async HTTP callbacks, std::thread, plug-ins).
                ProcessPostedTasks();

                std::erase_if(windows, [](const auto &w) {
                    return (w->GetState() == WindowState::Closed && w->GetConfig().deleteOnClose);
                });
                // Check for visible windows, delete/cleanup windows

                for (auto it = windows.begin(); it != windows.end(); it++) {
                    auto window = it->get();
//                    debugOutput << "window w=" << window << " nativeh=" << window->GetNativeHandle() << " visible=" << window->IsVisible() << " needredraw=" << window->IsNeedsRedraw() << " ctx=" << window->GetRenderContext() << std::endl;
                    if (window->IsVisible()) {
                        window->UpdateAndRender();
//                        auto ctx = window->GetRenderContext();
//                        if (window->IsNeedsResize()) {
//                            window->DoResize();
//                        }
//                        if (ctx) {
//                            if (window->IsNeedsUpdateGeometry() || window->IsNeedsRedraw()) {
//                                window->UpdateGeometry(ctx);
//                            }
//                            if (window->IsNeedsRedraw()) {
////                                debugOutput << "Redraw window w=" << window << " nativeh=" << window->GetNativeHandle() << std::endl;
//                                window->Render(ctx);
//                                window->Flush();
//                            }
//                        }
                    }

                }

                if (windows.empty()) {
                    debugOutput << "UltraCanvas: No windows, exiting..." << std::endl;
                    break;
                }
                
                // Clean up stale modal windows (expired weak_ptrs)
                activeModalWindows.erase(
                    std::remove_if(activeModalWindows.begin(), activeModalWindows.end(),
                        [](const std::weak_ptr<UltraCanvasWindowBase>& w) {
                            return w.expired();
                        }),
                    activeModalWindows.end()
                );

                // Update and render all windows
                if (clipbrd) {
                    clipbrd->Update();
                }
                if (eventLoopCallback) {
                    eventLoopCallback();
                }

                RunInEventLoop();
            }

        } catch (const std::exception& e) {
            debugOutput << "UltraCanvas: Exception in main loop: " << e.what() << std::endl;
        }

        // Clean shutdown
        debugOutput << "UltraCanvas: Main loop ended, performing cleanup..." << std::endl;
        //StopEventThread();

        debugOutput << "UltraCanvas: Destroying all windows..." << std::endl;
        while (!windows.empty()) {
            try {
                auto window = windows.back();
                window->PerformClose();
                windows.pop_back();
            } catch (const std::exception& e) {
                debugOutput << "UltraCanvas: Exception destroying window: " << e.what() << std::endl;
            }
        }

        if (onApplicationExit) {
            onApplicationExit();
        }

        initialized = false;
        debugOutput << "UltraCanvas: main loop completed, shutting down.." << std::endl;
        
        ShutdownClipboard();
        ShutdownNative();

        UCImage::ShutdownImageSubsysterm();
    }

    bool UltraCanvasApplicationBase::RequestExit() {
        debugOutput << "UltraCanvas: application exit requested" << std::endl;
        if (onApplicationExitRequest) {
            if (onApplicationExitRequest()) {
                Exit();
                return true;
            } else {
                debugOutput << "UltraCanvas: application exit requested denied" << std::endl;
                return false;
            }
        } else {
            Exit();
            return true;
        }
    }

    void UltraCanvasApplicationBase::Exit() {
        debugOutput << "UltraCanvas: application exit (set running=false)" << std::endl;
        running = false;
    }

    void UltraCanvasApplicationBase::PushEvent(const UCEvent& event) {
        {
            std::lock_guard<std::mutex> lock(eventQueueMutex);
            eventQueue.push(event);
        }
        eventCondition.notify_one();
        WakeUpEventLoop();
    }

    bool UltraCanvasApplicationBase::PopEvent(UCEvent& event) {
        std::lock_guard<std::mutex> lock(eventQueueMutex);
        if (eventQueue.empty()) {
            return false;
        }

        event = eventQueue.front();
        eventQueue.pop();
        return true;
    }

    void UltraCanvasApplicationBase::ProcessEvents() {
        UCEvent event;
        int processedEvents = 0;

        while (PopEvent(event) && processedEvents < 100) {
            processedEvents++;
            if (!running) {
                break;
            }
            DispatchEvent(event);
        }
    }

    void UltraCanvasApplicationBase::WaitForEvents(int timeoutMs) {
        std::unique_lock<std::mutex> lock(eventQueueMutex);
        if (timeoutMs < 0) {
            eventCondition.wait(lock, [this] { return !eventQueue.empty() || !running; });
        } else {
            eventCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                    [this] { return !eventQueue.empty() || !running; });
        }
    }

    // ===== WINDOW MANAGEMENT =====
    void UltraCanvasApplicationBase::RegisterWindow(const std::shared_ptr<UltraCanvasWindowBase>& window) {
        if (window && window->GetNativeHandle() != 0) {
            windows.push_back(window);
            debugOutput << "UltraCanvas: Window registered with Native ID: " << window->GetNativeHandle() << std::endl;

            // Auto-register modal windows
            if (window->GetConfig().modal) {
                RegisterModalWindow(window);
            }
        }
    }

    void UltraCanvasApplicationBase::CleanupWindowReferences(UltraCanvasWindowBase* win) {
        UnregisterModalWindow(win);
        if (focusedWindow == win) {
            focusedWindow = nullptr;

            // if (win->IsFocused()) {
            //     auto parentWin = win->GetParentWindow();
            //     if (parentWin && parentWin->IsVisible()) {
            //         auto parentWinState = parentWin->GetState();
            //         if ( parentWinState == WindowState::Normal || parentWinState == WindowState::Maximized || parentWinState == WindowState::Fullscreen) {
            //             parentWin->RaiseAndFocus();
            //             focusedWindow = (UltraCanvasWindow*)parentWin;
            //         }
            //     }
            // }
        }
        if (capturedElement && capturedElement->GetWindow() == win) {
            capturedElement = nullptr;
        }
        if (hoveredElement && hoveredElement->GetWindow() == win) {
            hoveredElement = nullptr;
        }
        if (draggedElement && draggedElement->GetWindow() == win) {
            draggedElement = nullptr;
        }
        debugOutput << "UltraCanvas: window found and unregistered successfully" << std::endl;
    }

    void UltraCanvasApplicationBase::CleanupElementReferences(UltraCanvasUIElement* elem) {
        if (capturedElement == elem) {
            ReleaseMouse(elem);
        }
        if (hoveredElement == elem) {
            hoveredElement = nullptr;
        }
        if (draggedElement == elem) {
            draggedElement = nullptr;
        }
        auto win = elem->GetWindow();
        if (win && win->IsCreated() && win->GetState() != WindowState::Closing &&  win->GetState() != WindowState::Closed && win->GetFocusedElement() == elem) {
            win->SetFocusedElement(nullptr);
        }
    }

    // ===== MODAL WINDOW MANAGEMENT =====
    UltraCanvasWindowBase* UltraCanvasApplicationBase::GetCurrentModalWindow() {
        for (auto it = activeModalWindows.rbegin(); it != activeModalWindows.rend(); ++it) {
            auto locked = it->lock();
            if (!locked) continue;
            if (!locked->IsVisible()) continue;
            auto state = locked->GetState();
            if (state == WindowState::Closing || state == WindowState::Closed) continue;
            return locked.get();
        }
        return nullptr;
    }

    bool UltraCanvasApplicationBase::HasActiveModalWindow() {
        return GetCurrentModalWindow() != nullptr;
    }

    bool UltraCanvasApplicationBase::HandleModalWindowEvents(const UCEvent& event, UltraCanvasWindow* targetWindow) {
        auto* modalWindow = GetCurrentModalWindow();
        if (!modalWindow) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
            case UCEventType::MouseUp:
            case UCEventType::MouseMove:
            case UCEventType::MouseWheel:
            case UCEventType::MouseDoubleClick:
            case UCEventType::MouseEnter:
            case UCEventType::MouseLeave:
            case UCEventType::KeyDown:
            case UCEventType::KeyUp:
            case UCEventType::TextInput:
            case UCEventType::Shortcut:
                if (targetWindow != modalWindow) return true;
                break;
            case UCEventType::WindowFocus:
                if (targetWindow && targetWindow != modalWindow) {
                    modalWindow->RaiseAndFocus();
                    return true;
                }
                break;
            default:
                return false;
        }
        return false;
    }

    void UltraCanvasApplicationBase::RegisterModalWindow(const std::shared_ptr<UltraCanvasWindowBase>& window) {
        if (window) {
            activeModalWindows.push_back(window);
        }
    }

    void UltraCanvasApplicationBase::UnregisterModalWindow(UltraCanvasWindowBase* window) {
        std::erase_if(activeModalWindows, 
            [window](const std::weak_ptr<UltraCanvasWindowBase>& w) {
                auto locked = w.lock();
                return !locked || locked.get() == window;
            }
        );
    }

    void UltraCanvasApplicationBase::UnregisterWindow(UltraCanvasWindowBase* window) {
        std::erase_if(windows, 
            [window](const std::shared_ptr<UltraCanvasWindowBase>& w) {
                return w.get() == window;
            }
        );
    }
    
    bool UltraCanvasApplicationBase::IsWindowRegistered(UltraCanvasWindowBase* window) {
        return (std::find_if(windows.begin(), windows.end(), 
            [window](const std::shared_ptr<UltraCanvasWindowBase>& w) {
                return w.get() == window;
            }
        ) != windows.end());
    }

    UltraCanvasWindow* UltraCanvasApplicationBase::FindWindow(NativeWindowHandle nativeHandle) {
        auto it = std::find_if(windows.begin(), windows.end(),
                               [nativeHandle](const std::shared_ptr<UltraCanvasWindowBase>& ptr) {
                                   return ptr->GetNativeHandle() == nativeHandle;
                               });

        if (it != windows.end()) {
           return (UltraCanvasWindow*)(it->get());
        } else {
            return nullptr;
        }
    }

    UltraCanvasUIElement* UltraCanvasApplicationBase::GetFocusedElement() {
        if (focusedWindow) {
            return focusedWindow->GetFocusedElement();
        }
        return nullptr;
    }

    bool UltraCanvasApplicationBase::IsDoubleClick(const UCEvent &event) {
        auto now = std::chrono::steady_clock::now();
        auto timeDiff = std::chrono::duration<float>(now - lastClickTime).count();

        bool isDoubleClick = false;
        if (timeDiff <= DOUBLE_CLICK_TIME) {
            int dx = event.pointer.x - lastMouseEvent.pointer.x;
            int dy = event.pointer.y - lastMouseEvent.pointer.y;
            int distance = static_cast<int>(std::sqrt(dx * dx + dy * dy));

            if (distance <= DOUBLE_CLICK_DISTANCE) {
                isDoubleClick = true;
            }
        }

        lastMouseEvent = event;
        lastClickTime = now;

        return isDoubleClick;
    }

    void UltraCanvasApplicationBase::DispatchEvent(const UCEvent& event) {
        // Update modifier states
        if (event.IsKeyboardEvent()) {
            shiftHeld = event.shift;
            ctrlHeld = event.ctrl;
            altHeld = event.alt;
            metaHeld = event.meta;
        }

        // Call global handlers first
        for (auto& handler : globalEventHandlers) {
            if (handler(event)) {
                return; // Event consumed by global handler
            }
        }

        // ===== NEW: IMPROVED TARGET WINDOW DETECTION =====
        UltraCanvasWindow* targetWindow = nullptr;

        // First priority: Use the window information stored in the event
        if (event.targetWindow != nullptr) {
            targetWindow = static_cast<UltraCanvasWindow*>(event.targetWindow);
            if (std::find_if(windows.begin(), windows.end(), [targetWindow](auto const &item) {
                return item.get() == targetWindow;
            }) == windows.end()) {
                debugOutput << "UltraCanvasApplicationBase::DispatchEvent stale event for already deleted window ev=" << event.ToString() << " win="<<targetWindow << std::endl;
                return;
            }
        }
            // Fallback: Try to find window by native handle
        else if (event.nativeWindowHandle != 0) {
            targetWindow = FindWindow(event.nativeWindowHandle);
        }
            // Last resort: Use focused window for certain event types
        else {
            // Only use focused window for keyboard events when no target is found
            if (event.type == UCEventType::KeyDown ||
                event.type == UCEventType::KeyUp ||
                event.type == UCEventType::Shortcut) {
                targetWindow = focusedWindow;
            }
        }

        // block some events if modal window active
        if (HandleModalWindowEvents(event, targetWindow)) {
            return;
        }

       if (event.type == UCEventType::MouseDown) {
           if (targetWindow && event.type == UCEventType::MouseDown && focusedWindow != targetWindow) {
               debugOutput << "Window clicked but not focused, set focus. target=" << targetWindow << " focused=" << focusedWindow << std::endl;
               if (focusedWindow) {
                   UCEvent ev{.type = UCEventType::WindowBlur};
                   DispatchEventToElement(focusedWindow, event);
               }
               UCEvent ev{.type = UCEventType::WindowFocus};
               DispatchEventToElement(targetWindow, event);
               focusedWindow = targetWindow;
           }
       }

        // Handle different event types
        switch (event.type) {
            case UCEventType::MouseMove:
            case UCEventType::MouseUp:
                if (capturedElement) {
                    if (DispatchEventToElement(capturedElement, event)) {
                        return;
                    }
                }
                break;

            case UCEventType::KeyDown:
            case UCEventType::KeyUp:
                if (event.nativeKeyCode >= 0 && event.nativeKeyCode < 256) {
                    keyStates[event.nativeKeyCode] = (event.type == UCEventType::KeyDown);
                }
                break;
            case UCEventType::WindowFocus:
                if (targetWindow && focusedWindow != targetWindow) {
                    // Update focused window
                    DispatchEventToElement(targetWindow, event);
                    focusedWindow = targetWindow;
                    debugOutput << "UltraCanvasBaseApplication: Window " << focusedWindow << " (native=" << focusedWindow->GetNativeHandle() << ") gained focus" << std::endl;
                }
                return;
            case UCEventType::WindowBlur:
                if (targetWindow && targetWindow == focusedWindow) {
                    debugOutput << "UltraCanvasBaseApplication: Window " << focusedWindow << " (native=" << focusedWindow->GetNativeHandle() << ") lost focus" << std::endl;
                    DispatchEventToElement(targetWindow, event);
                    focusedWindow = nullptr;
                }
                return;
        }
        // Dispatch other events to focused element
        if (targetWindow) {
            UltraCanvasUIElement* elementUnderPointer = nullptr;
//            UltraCanvasUIElement* originalElementUnderPointer = nullptr;

            PopupElement* popupElement = targetWindow->GetActivePopupElement();
//            std::shared_ptr<UltraCanvasUIElement> popupOwner;
            bool isPointerOutsidePopupElement = false;

//            if (popupElement) {
//                popupOwner = popupElement->settings.popUpOwner.lock();
//            }

            // Check if an element is the popup owner or a child of it
//            auto isPopupOwnerOrChild = [&popupElement](UltraCanvasUIElement* elem) -> bool {
//                if (!popupElement || !elem) return false;
//                auto owner = popupElement->settings.popUpOwner.lock();
//                if (!owner) return false;
//                if (elem == owner.get()) return true;
//                auto* ownerContainer = dynamic_cast<UltraCanvasContainer*>(owner.get());
//                return ownerContainer && ownerContainer->HasChild(elem);
//            };

            if (event.IsMouseEvent() || event.IsDragEvent()) { // change mouse cursor first
                elementUnderPointer = targetWindow->FindElementAtPoint(event.pointerWindow);
  //              originalElementUnderPointer = elementUnderPointer;
                // set pointerElem to popup element if it points outside
                if (popupElement) {
                    if (elementUnderPointer) {
                        if (elementUnderPointer != popupElement->element) {
                            auto *popupContainer = dynamic_cast<UltraCanvasContainer *>(popupElement->element);
                            if (!popupContainer || !popupContainer->HasChild(elementUnderPointer)) {
                                auto popupOwner = popupElement->settings.popupOwner.lock();
                                if (!popupOwner) {
                                    isPointerOutsidePopupElement = true;
                                } else {
                                    if (popupOwner.get() != elementUnderPointer) {
                                        auto* ownerContainer = dynamic_cast<UltraCanvasContainer*>(popupOwner.get());
                                        if (!ownerContainer || !ownerContainer->HasChild(elementUnderPointer)) {
                                            isPointerOutsidePopupElement = true;
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        isPointerOutsidePopupElement = true;
                    }

                    if (isPointerOutsidePopupElement) {
                        elementUnderPointer = popupElement->element;
                    }
                }
                // change mouse cursor
                if (elementUnderPointer) {
                    if (targetWindow->GetCurrentMouseCursor() != elementUnderPointer->GetMouseCursor()) {
                        targetWindow->SelectMouseCursor(elementUnderPointer->GetMouseCursor());
                    }
                } else {
                    // if no element pointed then select window's cursor
                    if (targetWindow->GetCurrentMouseCursor() != targetWindow->GetMouseCursor()) {
                        targetWindow->SelectMouseCursor(targetWindow->GetMouseCursor());
                    }
                }
            }

            // close popup element handling first
            if (popupElement) {
                // THERE element->Contains() WHICH IS NOT THE SAME AS element->GetBounds().Contains(),
                // THE element->Contains() MAY CHECK CHILD ELEMENTS TOO (SUBMENUS OR PARENT MENUS FOR EXAMPLE)
                if (popupElement->settings.closeByClickOutside
                    && event.type == UCEventType::MouseDown
                    && event.button != UCMouseButton::NoneButton
                    && isPointerOutsidePopupElement
                    && !popupElement->element->ContainsInWindow(event.pointerWindow)) {
                    targetWindow->ClosePopup(*popupElement->element, ClosePopupReason::ClickOutside);
                    goto finish;
                }
                if (popupElement->settings.closeByEscapeKey
                    && event.IsKeyboardEvent()
                    && event.virtualKey == UCKeys::Escape) {
                    targetWindow->ClosePopup(*popupElement->element, ClosePopupReason::EscapeKey);
                    goto finish;
                }
            }

            if (event.IsKeyboardEvent()) {
                UltraCanvasUIElement* focused =  targetWindow->GetFocusedElement();
                // sent event to popup directly if real focused element outside popup
                if (popupElement && focused != popupElement->element) {
                    UltraCanvasContainer* popupContainer = dynamic_cast<UltraCanvasContainer*>(popupElement->element);
                    if (!popupContainer || !popupContainer->HasChild(focused)) {
                        auto popupOwner = popupElement->settings.popupOwner.lock();
                        if (!popupOwner) {
                            focused = popupElement->element;
                        } else if (popupOwner.get() != focused) {
                            auto* ownerContainer = dynamic_cast<UltraCanvasContainer*>(popupOwner.get());
                            if (!ownerContainer || !ownerContainer->HasChild(focused)) {
                                focused = popupElement->element;
                            }
                        }
                    }
                }
                if (focused) {
                    HandleEventWithBubbling(focused, event);
                    goto finish;
                }
            }

            if (event.type == UCEventType::MouseWheel && elementUnderPointer) {
                HandleEventWithBubbling(elementUnderPointer, event);
                goto finish;

            }
            if (event.IsDragEvent()) {
                if (elementUnderPointer) {
                    HandleEventWithBubbling(elementUnderPointer, event);
                } else {
                    DispatchEventToElement(targetWindow, event);
                }
                goto finish;
            }

            if (event.IsMouseEvent()) {
                if (hoveredElement && hoveredElement != elementUnderPointer) {
                    if (hoveredElement->GetWindow() == targetWindow && hoveredElement->IsVisible()) {
                        UCEvent leaveEvent = event;
                        leaveEvent.type = UCEventType::MouseLeave;
                        leaveEvent.pointer = { -1, -1 };
                        DispatchEventToElement(hoveredElement, leaveEvent);
                    }
                    UltraCanvasTooltipManager::HideTooltip();
                    hoveredElement = nullptr;
                }
                if (!elementUnderPointer || elementUnderPointer == targetWindow) {
                    UltraCanvasTooltipManager::HideTooltip();
                }
                if (elementUnderPointer) {
                    if (hoveredElement != elementUnderPointer) {
                        auto enterEvent = event.Clone();
                        enterEvent.type = UCEventType::MouseEnter;
                        DispatchEventToElement(elementUnderPointer, enterEvent);

                        hoveredElement = elementUnderPointer;
                        // Show tooltip if element has one
                        if (!elementUnderPointer->GetTooltip().empty()) {
                            UltraCanvasTooltipManager::UpdateAndShowTooltip(
                                    targetWindow, elementUnderPointer->GetTooltip(),
                                    event.pointerWindow);
                        }
                    }
                    // Update tooltip position as mouse moves
                    if (!elementUnderPointer->GetTooltip().empty() &&
                        (UltraCanvasTooltipManager::IsVisible() || UltraCanvasTooltipManager::IsPending())) {
                        UltraCanvasTooltipManager::UpdateTooltipPosition(
                            event.pointerWindow);
                    }

                    if (DispatchEventToElement(elementUnderPointer, event)) {
                        goto finish;
                    }
                }
            }

            if (event.isCommandEvent()) {
                HandleEventWithBubbling(event.targetElement, event);
                goto finish;
            }
            DispatchEventToElement(targetWindow, event);

    finish:
            return;
//            // Debug logging
//            if (event.type != UCEventType::MouseMove) {
//                debugOutput << "UltraCanvas: Event type " << static_cast<int>(event.type)
//                          << " dispatched to window " << targetWindow
//                          << " (X11 Window: " << std::hex << event.nativeWindowHandle << std::dec << ")"
//                          << " focused=" << (targetWindow == focusedWindow ? "yes" : "no") << std::endl;
        } else {
            // No target window found - this might be normal for some system events
            debugOutput << "UltraCanvas: Warning - Event " << event.ToString()
                      << " has no target window (Native Window: " << std::hex << event.nativeWindowHandle << std::dec << ")" << std::endl;
        }
    }

    bool UltraCanvasApplicationBase::HandleEventWithBubbling(UltraCanvasUIElement *elem, const UCEvent &event) {
        if (!event.isCommandEvent()) {
            if (DispatchEventToElement(elem, event)) {
                return true;
            }
        }
        auto parent = elem->GetParentContainer();
        while(parent) {
            if (DispatchEventToElement(parent, event)) {
                return true;
            }
            parent = parent->GetParentContainer();
        }
        return false;
    }


    void UltraCanvasApplicationBase::FocusNextElement() {
        if (focusedWindow) {
            focusedWindow->FocusNextElement();
        }
    }

    void UltraCanvasApplicationBase::FocusPreviousElement() {
        if (focusedWindow) {
            focusedWindow->FocusPreviousElement();
        }
    }

    void UltraCanvasApplicationBase::RegisterEventLoopRunCallback(std::function<void()> callback) {
        eventLoopCallback = callback;
    }

    bool UltraCanvasApplicationBase::DispatchEventToElement(UltraCanvasUIElement* elem, UCEvent event) {
        event.targetElement = elem;
        auto window = elem->GetWindow();
        if (!window) {
            debugOutput << "UltraCanvasApplicationBase::DispatchEventToElement window == null for elem=" << elem << std::ends;
            return false;
        }
        if (event.type != UCEventType::MouseMove) {
            debugOutput << "DispatchEventToElement ev=" << event.ToString() << " target elem=" << elem << " target win=" << elem->GetWindow() << " focused=" << focusedWindow << std::endl;
        }
        if (event.IsMouseEvent() || event.IsDragEvent() || event.type == UCEventType::MouseEnter) {
            event.pointer = elem->MapToLocal(event.pointerWindow, nullptr);
        }

        currentEvent = event;

        if (window->HandleEventFilters(event)) {
            return true;
        }

        return elem->OnEvent(event);
    }

    void UltraCanvasApplicationBase::CaptureMouse(UltraCanvasUIElement *element) {
        CaptureMouseNative();
        capturedElement = element;
    }

    void UltraCanvasApplicationBase::ReleaseMouse(UltraCanvasUIElement *element) {
        if (element && element == capturedElement) {
            capturedElement = nullptr;
        }
        ReleaseMouseNative();
    }

    // ===== TIMER SYSTEM =====

    TimerId UltraCanvasApplicationBase::StartTimer(unsigned int ms_interval, bool periodic,
                                                    std::function<void(TimerId)> callback) {
        std::lock_guard<std::mutex> lock(timersMutex_);
        UltraCanvasTimer timer;
        timer.id = nextTimerId_++;
        timer.interval = std::chrono::milliseconds(ms_interval);
        timer.periodic = periodic;
        timer.active = true;
        timer.nextFire = std::chrono::steady_clock::now() + timer.interval;
        timer.callback = std::move(callback);
        timers_.push_back(std::move(timer));
        WakeUpEventLoop();
        return timers_.back().id;
    }

    void UltraCanvasApplicationBase::StopTimer(TimerId id) {
        std::lock_guard<std::mutex> lock(timersMutex_);
        for (auto& timer : timers_) {
            if (timer.id == id) {
                timer.active = false;
                return;
            }
        }
    }

    void UltraCanvasApplicationBase::ProcessTimers() {
        auto now = std::chrono::steady_clock::now();

        // Snapshot the current timer count so timers added during callbacks are not processed this round
        size_t count;
        {
            std::lock_guard<std::mutex> lock(timersMutex_);
            count = timers_.size();
        }

        for (size_t i = 0; i < count; ++i) {
            // Re-check bounds in case timers were removed
            if (i >= timers_.size()) break;

            auto& timer = timers_[i];
            if (!timer.active) continue;
            if (timer.nextFire > now) continue;

            // Fire the timer
            if (timer.callback) {
                timer.callback(timer.id);
            } else {
                UCEvent timerEvent;
                timerEvent.type = UCEventType::Timer;
                timerEvent.userDataInt = static_cast<int>(timer.id);
                // Push directly to queue without calling WakeUpEventLoop (we're already on the main thread)
                {
                    std::lock_guard<std::mutex> lock(eventQueueMutex);
                    eventQueue.push(timerEvent);
                }
            }

            // Advance or deactivate
            if (timer.periodic && timer.active) {
                timer.nextFire += timer.interval;
                // If we fell behind, skip to next future fire time
                if (timer.nextFire <= now) {
                    auto elapsed = now - timer.nextFire;
                    auto periods = elapsed / timer.interval + 1;
                    timer.nextFire += timer.interval * periods;
                }
            } else {
                timer.active = false;
            }
        }

        // Clean up inactive timers
        {
            std::lock_guard<std::mutex> lock(timersMutex_);
            timers_.erase(
                std::remove_if(timers_.begin(), timers_.end(),
                               [](const UltraCanvasTimer& t) { return !t.active; }),
                timers_.end());
        }
    }

    std::chrono::milliseconds UltraCanvasApplicationBase::GetTimeUntilNextTimer() const {
        std::lock_guard<std::mutex> lock(timersMutex_);
        auto earliest = std::chrono::steady_clock::time_point::max();
        for (const auto& timer : timers_) {
            if (timer.active && timer.nextFire < earliest) {
                earliest = timer.nextFire;
            }
        }
        if (earliest == std::chrono::steady_clock::time_point::max()) {
            return std::chrono::milliseconds::max(); // No active timers
        }
        auto now = std::chrono::steady_clock::now();
        if (earliest <= now) {
            return std::chrono::milliseconds(0);
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(earliest - now);
    }
}
