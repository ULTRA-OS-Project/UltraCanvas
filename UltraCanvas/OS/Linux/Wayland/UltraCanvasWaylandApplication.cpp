// OS/Linux/Wayland/UltraCanvasWaylandApplication.cpp
// The Wayland windowing backend's application. See the header.
// Version: 0.1.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasWaylandApplication.h"
#include "UltraCanvasWaylandWindow.h"
#include "../UltraCanvasLinuxAccessibility.h"
#include "../UltraCanvasLinuxFonts.h"
#include "../UltraCanvasLinuxKeys.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasDebug.h"

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"

#include <wayland-cursor.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace UltraCanvas {

    UltraCanvasWaylandApplication* UltraCanvasWaylandApplication::instance = nullptr;

    namespace {
        // One touchpad "notch": the logical pixels of smooth scrolling that
        // stand for one click of a wheel, where the compositor sends no
        // discrete steps.
        constexpr double kSmoothScrollStep = 10.0;

        UltraCanvasWaylandWindow* WindowOfSurface(UltraCanvasWaylandApplication* app, wl_surface* surface) {
            if (!surface) return nullptr;
            return static_cast<UltraCanvasWaylandWindow*>(
                    app->FindWindow(reinterpret_cast<NativeWindowHandle>(surface)));
        }
    } // namespace

    // ===== THE LISTENERS =====
    // libwayland's C callbacks; each finds the application in `data`.
    struct WaylandListeners {
        static UltraCanvasWaylandApplication* App(void* data) {
            return static_cast<UltraCanvasWaylandApplication*>(data);
        }

        // ----- registry -----
        static void RegistryGlobal(void* data, wl_registry*, uint32_t name, const char* interface, uint32_t version) {
            App(data)->BindGlobal(name, interface, version);
        }
        static void RegistryGlobalRemove(void* data, wl_registry*, uint32_t name) {
            App(data)->RemoveGlobal(name);
        }

        // ----- outputs -----
        static WaylandOutput* Output(void* data, wl_output* output) {
            auto* app = App(data);
            for (auto& o : app->outputs) {
                if (o.output == output) return &o;
            }
            return nullptr;
        }
        static void OutputGeometry(void* data, wl_output* output, int32_t x, int32_t y, int32_t, int32_t,
                                   int32_t, const char* make, const char* model, int32_t) {
            if (auto* o = Output(data, output)) {
                o->x = x;
                o->y = y;
                if (o->description.empty()) {
                    o->description = std::string(make ? make : "") + " " + (model ? model : "");
                }
            }
        }
        static void OutputMode(void* data, wl_output* output, uint32_t flags, int32_t width, int32_t height, int32_t) {
            if (!(flags & WL_OUTPUT_MODE_CURRENT)) return;
            if (auto* o = Output(data, output)) {
                o->pixelWidth = width;
                o->pixelHeight = height;
            }
        }
        static void OutputDone(void* data, wl_output* output) {
            if (auto* o = Output(data, output)) {
                o->done = true;
            }
            auto* app = App(data);
            for (auto& w : app->windows) {
                static_cast<UltraCanvasWaylandWindow*>(w.get())->HandleOutputsChanged();
            }
        }
        static void OutputScale(void* data, wl_output* output, int32_t factor) {
            if (auto* o = Output(data, output)) {
                o->scale = factor > 0 ? factor : 1;
            }
        }
        static void OutputName(void* data, wl_output* output, const char* name) {
            if (auto* o = Output(data, output)) o->name = name ? name : "";
        }
        static void OutputDescription(void* data, wl_output* output, const char* description) {
            if (auto* o = Output(data, output)) o->description = description ? description : "";
        }

        // ----- seat -----
        static void SeatCapabilities(void* data, wl_seat*, uint32_t capabilities) {
            auto* app = App(data);
            app->seatCapabilities = capabilities;
            app->UpdateSeatDevices();
        }
        static void SeatName(void*, wl_seat*, const char*) {}

        // ----- keyboard -----
        static void KeyboardKeymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
            auto* app = App(data);
            if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || !app->xkbContext) {
                close(fd);
                return;
            }
            void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (map == MAP_FAILED) {
                close(fd);
                return;
            }
            xkb_keymap* keymap = xkb_keymap_new_from_string(app->xkbContext, static_cast<const char*>(map),
                                                            XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            munmap(map, size);
            close(fd);
            if (!keymap) {
                debugOutput << "UltraCanvas Wayland: the seat's keymap could not be compiled" << std::endl;
                return;
            }
            xkb_state* state = xkb_state_new(keymap);
            if (!state) {
                xkb_keymap_unref(keymap);
                return;
            }
            if (app->xkbState) xkb_state_unref(app->xkbState);
            if (app->xkbKeymap) xkb_keymap_unref(app->xkbKeymap);
            app->xkbKeymap = keymap;
            app->xkbState = state;
        }
        static void KeyboardEnter(void* data, wl_keyboard*, uint32_t serial, wl_surface* surface, wl_array*) {
            auto* app = App(data);
            app->lastInputSerial = serial;
            auto* window = WindowOfSurface(app, surface);
            app->keyboardWindow = window;
            if (window) {
                app->PushEvent(app->MakeEvent(window, UCEventType::WindowFocus));
            }
        }
        static void KeyboardLeave(void* data, wl_keyboard*, uint32_t serial, wl_surface* surface) {
            auto* app = App(data);
            app->lastInputSerial = serial;
            app->StopKeyRepeat();
            auto* window = WindowOfSurface(app, surface);
            if (window) {
                app->PushEvent(app->MakeEvent(window, UCEventType::WindowBlur));
            }
            if (app->keyboardWindow == window) app->keyboardWindow = nullptr;
        }
        static void KeyboardKey(void* data, wl_keyboard*, uint32_t serial, uint32_t, uint32_t key, uint32_t state) {
            auto* app = App(data);
            app->lastInputSerial = serial;
            const bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
            if (pressed) {
                app->PushKeyEvent(key, true, false);
                if (app->xkbKeymap && xkb_keymap_key_repeats(app->xkbKeymap, key + 8)) {
                    app->StartKeyRepeat(key);
                }
            } else {
                if (app->repeatKey == key) app->StopKeyRepeat();
                app->PushKeyEvent(key, false, false);
            }
        }
        static void KeyboardModifiers(void* data, wl_keyboard*, uint32_t serial, uint32_t depressed,
                                      uint32_t latched, uint32_t locked, uint32_t group) {
            auto* app = App(data);
            app->lastInputSerial = serial;
            if (!app->xkbState) return;
            xkb_state_update_mask(app->xkbState, depressed, latched, locked, 0, 0, group);
            auto active = [&](const char* name) {
                return xkb_state_mod_name_is_active(app->xkbState, name, XKB_STATE_MODS_EFFECTIVE) > 0;
            };
            app->modShift = active(XKB_MOD_NAME_SHIFT);
            app->modCtrl = active(XKB_MOD_NAME_CTRL);
            app->modAlt = active(XKB_MOD_NAME_ALT);
            app->modMeta = active(XKB_MOD_NAME_LOGO);
        }
        static void KeyboardRepeatInfo(void* data, wl_keyboard*, int32_t rate, int32_t delay) {
            auto* app = App(data);
            app->repeatRate = rate;
            app->repeatDelay = delay;
        }

        // ----- pointer -----
        static void PointerEnter(void* data, wl_pointer*, uint32_t serial, wl_surface* surface,
                                 wl_fixed_t sx, wl_fixed_t sy) {
            auto* app = App(data);
            app->pointerEnterSerial = serial;
            auto* window = WindowOfSurface(app, surface);
            app->pointerWindow = window;
            app->pointerX = wl_fixed_to_double(sx);
            app->pointerY = wl_fixed_to_double(sy);
            app->pointerKnown = true;
            if (!window) return;
            // The cursor is per surface entry: put up the one this window has.
            app->pointerCursor = window->GetCurrentMouseCursor();
            app->ApplyCursor(window);
            UCEvent event = app->MakeEvent(window, UCEventType::MouseEnter);
            event.pointerWindow = { static_cast<int>(app->pointerX), static_cast<int>(app->pointerY) };
            event.pointer = event.pointerWindow;
            event.pointerGlobal = event.pointerWindow;
            app->PushEvent(event);
        }
        static void PointerLeave(void* data, wl_pointer*, uint32_t serial, wl_surface* surface) {
            auto* app = App(data);
            app->pointerEnterSerial = serial;
            auto* window = WindowOfSurface(app, surface);
            if (window) {
                UCEvent event = app->MakeEvent(window, UCEventType::MouseLeave);
                event.pointerWindow = { static_cast<int>(app->pointerX), static_cast<int>(app->pointerY) };
                event.pointer = event.pointerWindow;
                event.pointerGlobal = event.pointerWindow;
                app->PushEvent(event);
            }
            if (app->pointerWindow == window) app->pointerWindow = nullptr;
        }
        static void PointerMotion(void* data, wl_pointer*, uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
            auto* app = App(data);
            app->pointerX = wl_fixed_to_double(sx);
            app->pointerY = wl_fixed_to_double(sy);
            app->pointerKnown = true;
            if (!app->pointerWindow) return;
            UCEvent event = app->MakeEvent(app->pointerWindow, UCEventType::MouseMove);
            event.pointerWindow = { static_cast<int>(app->pointerX), static_cast<int>(app->pointerY) };
            event.pointer = event.pointerWindow;
            event.pointerGlobal = event.pointerWindow;
            app->FillModifiers(event);
            app->PushEvent(event);
        }
        static void PointerButton(void* data, wl_pointer*, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
            auto* app = App(data);
            app->lastInputSerial = serial;
            auto* window = app->pointerWindow;
            if (!window) return;
            const bool pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;
            UCEvent event = app->MakeEvent(window, pressed ? UCEventType::MouseDown : UCEventType::MouseUp);
            switch (button) {
                case BTN_LEFT: event.button = UCMouseButton::Left; break;
                case BTN_MIDDLE: event.button = UCMouseButton::Middle; break;
                case BTN_RIGHT: event.button = UCMouseButton::Right; break;
                case BTN_SIDE: event.button = UCMouseButton::Back; break;
                case BTN_EXTRA: event.button = UCMouseButton::Forward; break;
                default: event.button = UCMouseButton::Unknown; break;
            }
            const int x = static_cast<int>(app->pointerX);
            const int y = static_cast<int>(app->pointerY);
            event.pointerWindow = { x, y };
            event.pointer = event.pointerWindow;
            event.pointerGlobal = event.pointerWindow;
            app->FillModifiers(event);
            if (pressed) {
                // A second press of the same button, close in time and place,
                // is a double click (and clears the memory, so a third press
                // starts afresh) - as the X11 backend counts them.
                const bool doubleClick = app->lastClickWindow == window && app->lastClickButton == button &&
                                         time - app->lastClickTime <= app->doubleClickTimeMs &&
                                         std::abs(x - app->lastClickX) <= app->doubleClickDistance &&
                                         std::abs(y - app->lastClickY) <= app->doubleClickDistance;
                if (doubleClick) {
                    event.type = UCEventType::MouseDoubleClick;
                    app->lastClickWindow = nullptr;
                    app->lastClickTime = 0;
                } else {
                    app->lastClickWindow = window;
                    app->lastClickTime = time;
                    app->lastClickButton = button;
                    app->lastClickX = x;
                    app->lastClickY = y;
                }
            }
            app->PushEvent(event);
        }
        static void PointerAxis(void* data, wl_pointer* pointer, uint32_t, uint32_t axis, wl_fixed_t value) {
            auto* app = App(data);
            const double amount = wl_fixed_to_double(value);
            if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) app->pendingAxisVertical += amount;
            else app->pendingAxisHorizontal += amount;
            app->pendingAxis = true;
            // A compositor older than v5 sends no frame: the event is whole.
            if (wl_proxy_get_version(reinterpret_cast<wl_proxy*>(pointer)) < WL_POINTER_FRAME_SINCE_VERSION) {
                app->FlushPointerFrame();
            }
        }
        static void PointerFrame(void* data, wl_pointer*) {
            App(data)->FlushPointerFrame();
        }
        static void PointerAxisSource(void*, wl_pointer*, uint32_t) {}
        static void PointerAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
        static void PointerAxisDiscrete(void* data, wl_pointer*, uint32_t axis, int32_t discrete) {
            auto* app = App(data);
            if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) app->pendingDiscreteVertical += discrete;
            else app->pendingDiscreteHorizontal += discrete;
            app->pendingAxis = true;
        }

        // ----- xdg_wm_base -----
        static void WmBasePing(void*, xdg_wm_base* base, uint32_t serial) {
            xdg_wm_base_pong(base, serial);
        }

        static void ShmFormat(void*, wl_shm*, uint32_t) {}
    };

    namespace {
        const wl_registry_listener kRegistryListener = {
                WaylandListeners::RegistryGlobal,
                WaylandListeners::RegistryGlobalRemove,
        };
        const wl_output_listener kOutputListener = {
                WaylandListeners::OutputGeometry,
                WaylandListeners::OutputMode,
                WaylandListeners::OutputDone,
                WaylandListeners::OutputScale,
                WaylandListeners::OutputName,
                WaylandListeners::OutputDescription,
        };
        const wl_seat_listener kSeatListener = {
                WaylandListeners::SeatCapabilities,
                WaylandListeners::SeatName,
        };
        const wl_keyboard_listener kKeyboardListener = {
                WaylandListeners::KeyboardKeymap,
                WaylandListeners::KeyboardEnter,
                WaylandListeners::KeyboardLeave,
                WaylandListeners::KeyboardKey,
                WaylandListeners::KeyboardModifiers,
                WaylandListeners::KeyboardRepeatInfo,
        };
        const wl_pointer_listener kPointerListener = {
                WaylandListeners::PointerEnter,
                WaylandListeners::PointerLeave,
                WaylandListeners::PointerMotion,
                WaylandListeners::PointerButton,
                WaylandListeners::PointerAxis,
                WaylandListeners::PointerFrame,
                WaylandListeners::PointerAxisSource,
                WaylandListeners::PointerAxisStop,
                WaylandListeners::PointerAxisDiscrete,
        };
        const xdg_wm_base_listener kWmBaseListener = {
                WaylandListeners::WmBasePing,
        };
        const wl_shm_listener kShmListener = {
                WaylandListeners::ShmFormat,
        };
    } // namespace

    // ===== CONSTRUCTION =====
    UltraCanvasWaylandApplication::UltraCanvasWaylandApplication() {
        instance = this;
    }

    UltraCanvasWaylandApplication::~UltraCanvasWaylandApplication() {
        if (display) ShutdownNative();
        if (instance == this) instance = nullptr;
    }

    // ===== INITIALISATION =====
    bool UltraCanvasWaylandApplication::InitializeNative() {
        if (initialized) return true;
        debugOutput << "UltraCanvas: Initializing Wayland application..." << std::endl;

        xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!ConnectDisplay()) {
            ShutdownNative();
            return false;
        }
        InitializeWakeUp();
        LinuxAccessibility::Start();

        initialized = true;
        running = false;
        debugOutput << "UltraCanvas: Wayland application initialized (" << outputs.size() << " output(s))" << std::endl;
        return true;
    }

    bool UltraCanvasWaylandApplication::ConnectDisplay() {
        display = wl_display_connect(nullptr);
        if (!display) {
            const char* name = std::getenv("WAYLAND_DISPLAY");
            debugOutput << "UltraCanvas Wayland: no compositor at WAYLAND_DISPLAY="
                        << (name ? name : "(unset)") << std::endl;
            return false;
        }
        registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &kRegistryListener, this);
        // One round trip announces the globals, a second brings what binding
        // them asked for: the seat's capabilities, the outputs' modes.
        wl_display_roundtrip(display);
        wl_display_roundtrip(display);
        if (!compositor || !shm || !xdgWmBase) {
            debugOutput << "UltraCanvas Wayland: the compositor lacks "
                        << (!compositor ? "wl_compositor" : !shm ? "wl_shm" : "xdg_wm_base") << std::endl;
            return false;
        }
        cursorSurface = wl_compositor_create_surface(compositor);
        return true;
    }

    void UltraCanvasWaylandApplication::BindGlobal(uint32_t name, const char* interface, uint32_t version) {
        auto bind = [&](const wl_interface* iface, uint32_t wanted) {
            return wl_registry_bind(registry, name, iface, std::min(version, wanted));
        };
        const std::string what = interface ? interface : "";
        if (what == wl_compositor_interface.name) {
            compositor = static_cast<wl_compositor*>(bind(&wl_compositor_interface, 4));
        } else if (what == wl_shm_interface.name) {
            shm = static_cast<wl_shm*>(bind(&wl_shm_interface, 1));
            wl_shm_add_listener(shm, &kShmListener, this);
        } else if (what == wl_seat_interface.name) {
            if (seat) return;   // one seat is all this backend follows
            seat = static_cast<wl_seat*>(bind(&wl_seat_interface, 5));
            wl_seat_add_listener(seat, &kSeatListener, this);
            UpdateSeatDevices();
        } else if (what == wl_output_interface.name) {
            WaylandOutput output;
            output.output = static_cast<wl_output*>(bind(&wl_output_interface, 4));
            output.registryName = name;
            outputs.push_back(output);
            wl_output_add_listener(outputs.back().output, &kOutputListener, this);
        } else if (what == xdg_wm_base_interface.name) {
            xdgWmBase = static_cast<xdg_wm_base*>(bind(&xdg_wm_base_interface, 2));
            xdg_wm_base_add_listener(xdgWmBase, &kWmBaseListener, this);
        } else if (what == zxdg_decoration_manager_v1_interface.name) {
            decorationManager = static_cast<zxdg_decoration_manager_v1*>(bind(&zxdg_decoration_manager_v1_interface, 1));
        } else if (what == wl_data_device_manager_interface.name) {
            dataDeviceManager = static_cast<wl_data_device_manager*>(bind(&wl_data_device_manager_interface, 3));
            UpdateSeatDevices();
        }
    }

    void UltraCanvasWaylandApplication::RemoveGlobal(uint32_t name) {
        for (auto it = outputs.begin(); it != outputs.end(); ++it) {
            if (it->registryName != name) continue;
            for (auto& w : windows) {
                static_cast<UltraCanvasWaylandWindow*>(w.get())->HandleOutputLeave(it->output);
            }
            wl_output_release(it->output);
            outputs.erase(it);
            for (auto& w : windows) {
                static_cast<UltraCanvasWaylandWindow*>(w.get())->HandleOutputsChanged();
            }
            return;
        }
    }

    void UltraCanvasWaylandApplication::UpdateSeatDevices() {
        if (!seat) return;
        const bool wantKeyboard = seatCapabilities & WL_SEAT_CAPABILITY_KEYBOARD;
        const bool wantPointer = seatCapabilities & WL_SEAT_CAPABILITY_POINTER;
        if (wantKeyboard && !keyboard) {
            keyboard = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(keyboard, &kKeyboardListener, this);
        } else if (!wantKeyboard && keyboard) {
            StopKeyRepeat();
            wl_keyboard_release(keyboard);
            keyboard = nullptr;
            keyboardWindow = nullptr;
        }
        if (wantPointer && !pointer) {
            pointer = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(pointer, &kPointerListener, this);
        } else if (!wantPointer && pointer) {
            wl_pointer_release(pointer);
            pointer = nullptr;
            pointerWindow = nullptr;
        }
        if (dataDeviceManager && !dataDevice) {
            dataDevice = wl_data_device_manager_get_data_device(dataDeviceManager, seat);
        }
    }

    void UltraCanvasWaylandApplication::ShutdownNative() {
        LinuxAccessibility::Stop();
        StopKeyRepeat();
        ShutdownWakeUp();
        FreeImageCursors();
        for (auto& theme : cursorThemes) {
            if (theme.second) wl_cursor_theme_destroy(theme.second);
        }
        cursorThemes.clear();
        if (cursorSurface) { wl_surface_destroy(cursorSurface); cursorSurface = nullptr; }
        if (dataDevice) { wl_data_device_release(dataDevice); dataDevice = nullptr; }
        if (dataDeviceManager) { wl_data_device_manager_destroy(dataDeviceManager); dataDeviceManager = nullptr; }
        if (keyboard) { wl_keyboard_release(keyboard); keyboard = nullptr; }
        if (pointer) { wl_pointer_release(pointer); pointer = nullptr; }
        if (seat) { wl_seat_release(seat); seat = nullptr; }
        if (xkbState) { xkb_state_unref(xkbState); xkbState = nullptr; }
        if (xkbKeymap) { xkb_keymap_unref(xkbKeymap); xkbKeymap = nullptr; }
        if (xkbContext) { xkb_context_unref(xkbContext); xkbContext = nullptr; }
        for (auto& o : outputs) {
            if (o.output) wl_output_release(o.output);
        }
        outputs.clear();
        if (decorationManager) { zxdg_decoration_manager_v1_destroy(decorationManager); decorationManager = nullptr; }
        if (xdgWmBase) { xdg_wm_base_destroy(xdgWmBase); xdgWmBase = nullptr; }
        if (shm) { wl_shm_destroy(shm); shm = nullptr; }
        if (compositor) { wl_compositor_destroy(compositor); compositor = nullptr; }
        if (registry) { wl_registry_destroy(registry); registry = nullptr; }
        if (display) {
            wl_display_flush(display);
            wl_display_disconnect(display);
            display = nullptr;
        }
        keyboardWindow = nullptr;
        pointerWindow = nullptr;
        lastClickWindow = nullptr;
    }

    // ===== THE EVENT LOOP =====
    void UltraCanvasWaylandApplication::InitializeWakeUp() {
        wakeupFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (wakeupFd < 0) {
            debugOutput << "UltraCanvas Wayland: eventfd failed: " << std::strerror(errno) << std::endl;
        }
    }

    void UltraCanvasWaylandApplication::ShutdownWakeUp() {
        if (wakeupFd >= 0) {
            close(wakeupFd);
            wakeupFd = -1;
        }
    }

    void UltraCanvasWaylandApplication::WakeUpEventLoop() {
        if (wakeupFd >= 0) {
            uint64_t one = 1;
            (void)!write(wakeupFd, &one, sizeof(one));
        }
    }

    void UltraCanvasWaylandApplication::Flush() {
        if (display) wl_display_flush(display);
    }

    void UltraCanvasWaylandApplication::DispatchPendingEvents() {
        if (!display) return;
        if (wl_display_dispatch_pending(display) < 0 || wl_display_get_error(display) != 0) {
            debugOutput << "UltraCanvas Wayland: the connection to the compositor is gone ("
                        << std::strerror(wl_display_get_error(display)) << ")" << std::endl;
            running = false;
        }
    }

    void UltraCanvasWaylandApplication::CollectAndProcessNativeEvents() {
        if (!display) return;

        // 1. What has already been read
        DispatchPendingEvents();
        if (!running) return;

        // 2. Take the read lock; another dispatch is needed while events are
        //    still queued (prepare_read insists the queue is empty).
        while (wl_display_prepare_read(display) != 0) {
            if (wl_display_dispatch_pending(display) < 0) {
                running = false;
                return;
            }
        }
        wl_display_flush(display);

        // 3. Wait on the connection, the wakeup, and the fd watches, until
        //    the next timer is due
        std::vector<pollfd> fds;
        fds.push_back({ wl_display_get_fd(display), POLLIN, 0 });
        if (wakeupFd >= 0) fds.push_back({ wakeupFd, POLLIN, 0 });
        auto fdWatchKeys = SnapshotFdWatchKeys();
        for (const auto& key : fdWatchKeys) {
            if (key.fd < 0) continue;
            fds.push_back({ key.fd, static_cast<short>(key.type == FdWatchType::Write ? POLLOUT : POLLIN), 0 });
        }
        auto timeout = GetTimeUntilNextTimer();
        int timeoutMs = -1;
        if (timeout != std::chrono::milliseconds::max()) {
            timeoutMs = static_cast<int>(std::min<long long>(timeout.count(), 1'000'000));
        }
        int result = poll(fds.data(), fds.size(), timeoutMs);
        if (result < 0 && errno != EINTR) {
            debugOutput << "UltraCanvas Wayland: poll failed: " << std::strerror(errno) << std::endl;
        }

        // 4. Read what the compositor sent, or give the lock back
        if (result > 0 && (fds[0].revents & (POLLIN | POLLERR | POLLHUP))) {
            if (wl_display_read_events(display) < 0) {
                debugOutput << "UltraCanvas Wayland: reading from the compositor failed" << std::endl;
                running = false;
                return;
            }
        } else {
            wl_display_cancel_read(display);
        }

        // 5. The wakeup, then the watches (unlocked: a handler may add or
        //    remove watches)
        size_t index = 1;
        if (wakeupFd >= 0) {
            if (result > 0 && (fds[index].revents & POLLIN)) {
                uint64_t value;
                (void)!read(wakeupFd, &value, sizeof(value));
            }
            ++index;
        }
        if (result > 0) {
            for (const auto& key : fdWatchKeys) {
                if (key.fd < 0) continue;
                if (fds[index].revents & (POLLIN | POLLOUT | POLLERR | POLLHUP)) {
                    FireFdWatch(key.id);
                }
                ++index;
            }
        }

        // 6. Turn what arrived into UCEvents
        DispatchPendingEvents();
    }

    void UltraCanvasWaylandApplication::CaptureMouseNative() {
        // The compositor keeps the pointer's events coming to the surface a
        // button was pressed on until it is released: nothing to grab.
    }

    void UltraCanvasWaylandApplication::ReleaseMouseNative() {}

    // ===== EVENTS =====
    UCEvent UltraCanvasWaylandApplication::MakeEvent(UltraCanvasWaylandWindow* window, UCEventType type) const {
        UCEvent event;
        event.type = type;
        event.timestamp = std::chrono::steady_clock::now();
        if (window) {
            event.nativeWindowHandle = reinterpret_cast<NativeWindowHandle>(window->GetSurface());
            event.targetWindow = window->GetWindowWeakPtr();
        }
        return event;
    }

    void UltraCanvasWaylandApplication::FillModifiers(UCEvent& event) const {
        event.shift = modShift;
        event.ctrl = modCtrl;
        event.alt = modAlt;
        event.meta = modMeta;
    }

    void UltraCanvasWaylandApplication::PushKeyEvent(uint32_t key, bool pressed, bool) {
        if (!keyboardWindow || !xkbState || !xkbKeymap) return;
        const xkb_keycode_t code = key + 8;   // evdev to X keycode, which the keymap speaks
        UCEvent event = MakeEvent(keyboardWindow, pressed ? UCEventType::KeyDown : UCEventType::KeyUp);
        event.nativeKeyCode = static_cast<int>(code);
        // The virtual key is the key's own, unshifted, meaning (the X11
        // backend takes the level-0 keysym the same way): Shift+a is A.
        const xkb_keysym_t* syms = nullptr;
        const xkb_layout_index_t layout = xkb_state_key_get_layout(xkbState, code);
        xkb_keysym_t sym = XKB_KEY_NoSymbol;
        if (xkb_keymap_key_get_syms_by_level(xkbKeymap, code, layout, 0, &syms) > 0 && syms) {
            sym = syms[0];
        } else {
            sym = xkb_state_key_get_one_sym(xkbState, code);
        }
        event.virtualKey = UCKeyFromKeysym(static_cast<unsigned long>(sym));
        if (pressed) {
            char buffer[64];
            const int length = xkb_state_key_get_utf8(xkbState, code, buffer, sizeof(buffer));
            if (length > 0 && length < static_cast<int>(sizeof(buffer))) {
                event.text.assign(buffer, static_cast<size_t>(length));
                event.character = (length == 1 && static_cast<unsigned char>(buffer[0]) < 128) ? buffer[0] : 0;
            }
        }
        FillModifiers(event);
        PushEvent(event);
    }

    void UltraCanvasWaylandApplication::StartKeyRepeat(uint32_t key) {
        StopKeyRepeat();
        if (repeatRate <= 0) return;
        repeatKey = key;
        const unsigned int interval = static_cast<unsigned int>(std::max(1, 1000 / std::max(1, repeatRate)));
        repeatTimer = StartTimer(static_cast<unsigned int>(std::max(1, repeatDelay)), false, [this, key, interval](TimerId) {
            if (repeatKey != key) return;
            PushKeyEvent(key, true, true);
            repeatTimer = StartTimer(interval, true, [this, key](TimerId) {
                if (repeatKey != key) return;
                PushKeyEvent(key, true, true);
            });
        });
    }

    void UltraCanvasWaylandApplication::StopKeyRepeat() {
        if (repeatTimer) {
            StopTimer(repeatTimer);
            repeatTimer = 0;
        }
        repeatKey = 0;
    }

    void UltraCanvasWaylandApplication::FlushPointerFrame() {
        if (!pendingAxis) return;
        pendingAxis = false;
        auto* window = pointerWindow;
        // Leftover smooth scrolling carries into the next frame, so a slow
        // touchpad scroll still arrives, a notch at a time.
        double& residualVertical = residualAxisVertical;
        double& residualHorizontal = residualAxisHorizontal;
        int vertical = pendingDiscreteVertical;
        int horizontal = pendingDiscreteHorizontal;
        if (vertical == 0 && pendingAxisVertical != 0) {
            residualVertical += pendingAxisVertical;
            vertical = static_cast<int>(residualVertical / kSmoothScrollStep);
            residualVertical -= vertical * kSmoothScrollStep;
        }
        if (horizontal == 0 && pendingAxisHorizontal != 0) {
            residualHorizontal += pendingAxisHorizontal;
            horizontal = static_cast<int>(residualHorizontal / kSmoothScrollStep);
            residualHorizontal -= horizontal * kSmoothScrollStep;
        }
        pendingAxisVertical = pendingAxisHorizontal = 0;
        pendingDiscreteVertical = pendingDiscreteHorizontal = 0;
        if (!window) return;
        auto push = [&](UCEventType type, int steps) {
            // One notch at a time, as X11's button presses arrive
            const int sign = steps > 0 ? 1 : -1;
            for (int i = 0; i < std::abs(steps); ++i) {
                UCEvent event = MakeEvent(window, type);
                event.wheelDelta = sign;
                event.pointerWindow = { static_cast<int>(pointerX), static_cast<int>(pointerY) };
                event.pointer = event.pointerWindow;
                event.pointerGlobal = event.pointerWindow;
                FillModifiers(event);
                PushEvent(event);
            }
        };
        // Wayland counts downwards and rightwards as positive; the framework
        // counts a wheel notch away from the user (up) as +1, right as +1.
        if (vertical != 0) push(UCEventType::MouseWheel, -vertical);
        if (horizontal != 0) push(UCEventType::MouseWheelHorizontal, horizontal);
    }

    void UltraCanvasWaylandApplication::ForgetWindow(UltraCanvasWaylandWindow* window) {
        if (keyboardWindow == window) {
            StopKeyRepeat();
            keyboardWindow = nullptr;
        }
        if (pointerWindow == window) pointerWindow = nullptr;
        if (lastClickWindow == window) lastClickWindow = nullptr;
    }

    // ===== OUTPUTS =====
    const WaylandOutput* UltraCanvasWaylandApplication::FindOutput(wl_output* output) const {
        for (const auto& o : outputs) {
            if (o.output == output) return &o;
        }
        return nullptr;
    }

    void UltraCanvasWaylandApplication::GetScreenSize(int& width, int& height) const {
        width = height = 0;
        for (const auto& o : outputs) {
            if (o.Width() * o.Height() > width * height) {
                width = o.Width();
                height = o.Height();
            }
        }
    }

    bool UltraCanvasWaylandApplication::GetLastPointerPosition(int& x, int& y) const {
        x = static_cast<int>(pointerX);
        y = static_cast<int>(pointerY);
        return pointerKnown;
    }

    std::string UltraCanvasWaylandApplication::GetKeyboardLayoutName() const {
        if (!xkbKeymap || !xkbState) return {};
        const xkb_layout_index_t count = xkb_keymap_num_layouts(xkbKeymap);
        for (xkb_layout_index_t i = 0; i < count; ++i) {
            if (xkb_state_layout_index_is_active(xkbState, i, XKB_STATE_LAYOUT_EFFECTIVE) > 0) {
                const char* name = xkb_keymap_layout_get_name(xkbKeymap, i);
                return name ? name : "";
            }
        }
        return {};
    }

    // ===== FONTS =====
    FontStyle UltraCanvasWaylandApplication::DetectSystemFontStyleNative() {
        return LinuxFonts::DetectSystemFontStyle();
    }

    FontStyle UltraCanvasWaylandApplication::DetectMonospacedFontStyleNative() {
        return LinuxFonts::DetectMonospacedFontStyle();
    }

    void UltraCanvasWaylandApplication::LoadBundledFontsNative() {
        LinuxFonts::LoadBundledFonts();
    }

    bool UltraCanvasWaylandApplication::RegisterFontFileNative(const std::string& fontFilePath) {
        return LinuxFonts::RegisterFontFile(fontFilePath);
    }

} // namespace UltraCanvas
