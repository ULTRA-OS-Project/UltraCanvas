// include/UltraCanvasEvent.h - Enhanced Version
// Event system for UltraCanvas Framework with Linux optimizations
// Version: 2.3.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#ifndef ULTRA_CANVAS_EVENT_H
#define ULTRA_CANVAS_EVENT_H

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasNativeHandle.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
    class UltraCanvasUIElement;
    class UltraCanvasWindowBase;

    enum class UCEventType {
        NoneEvent,

        // Mouse Events
        MouseDown,
        MouseUp,
        MouseMove,
        MouseEnter,
        MouseLeave,
        MouseWheel,
        MouseWheelHorizontal,
        MouseDoubleClick,

        // Keyboard Events
        KeyDown,
        KeyUp,
        TextInput,        // Added for text input events

        // Window Events
        WindowCloseRequest,
        WindowResize,
        WindowMove,
        WindowMinimize,
        WindowFocus,
        WindowBlur,
        WindowRepaint,

        // Touch Events (for future mobile support)
        TouchStart,
        TouchMove,
        TouchEnd,
        Tap,
        PinchZoom,

        // Focus Events
        FocusGained,
        FocusLost,

        // Drag and Drop
        DragStart,
        DragEnter,
        DragLeave,
        DragOver,
        Drop,

        // Additional events for enhanced Linux support
        Clipboard,         // Clipboard change notifications
        Selection,         // X11 selection events
        Timer,            // Timer events
        Custom,            // Custom user-defined events

        // command events
        CommandEventsStart, // custom events just to mark the range
        ButtonClick,
        DropdownSelect,
        MenuClick,
        CommandEventsEnd, // custom events just to mark the range

        Redraw,
        Unknown
    };

// Key code constants for cross-platform compatibility
    enum UCKeys {
        // Special values
        Unknown = 0,

        // Control keys
        Escape = 0x1B,
        Tab = 0x09,
        Return = 0x0D,
        Enter = 0x0D,
        Space = 0x20,
        Backspace = 0x08,
        Delete = 0x07,

        // Number row (0-9)
        Key0 = 0x30,
        Key1 = 0x31,
        Key2 = 0x32,
        Key3 = 0x33,
        Key4 = 0x34,
        Key5 = 0x35,
        Key6 = 0x36,
        Key7 = 0x37,
        Key8 = 0x38,
        Key9 = 0x39,

        // Letters A-Z (ASCII values)
        A = 0x41,
        B = 0x42,
        C = 0x43,
        D = 0x44,
        E = 0x45,
        F = 0x46,
        G = 0x47,
        H = 0x48,
        I = 0x49,
        J = 0x4A,
        K = 0x4B,
        L = 0x4C,
        M = 0x4D,
        N = 0x4E,
        O = 0x4F,
        P = 0x50,
        Q = 0x51,
        R = 0x52,
        S = 0x53,
        T = 0x54,
        U = 0x55,
        V = 0x56,
        W = 0x57,
        X = 0x58,
        Y = 0x59,
        Z = 0x5A,

        // Punctuation and symbols (commonly used)
        Semicolon = 0x3B,      // ;
        Equal = 0x3D,          // =
        Comma = 0x2C,          // ,
        Minus = 0x2D,          // -
        Period = 0x2E,         // .
        Slash = 0x2F,          // /
        Grave = 0x60,          // `
        LeftBracket = 0x5B,    // [
        Backslash = 0x5C,      // backslash
        RightBracket = 0x5D,   // ]
        Quote = 0x27,          // '

        // Additional symbols
        Exclamation = 0x21,    // !
        At = 0x40,             // @
        Hash = 0x23,           // #
        Dollar = 0x24,         // $
        Percent = 0x25,        // %
        Caret = 0x5E,          // ^
        Ampersand = 0x26,      // &
        Asterisk = 0x2A,       // *
        LeftParen = 0x28,      // (
        RightParen = 0x29,     // )
        Underscore = 0x5F,     // _
        Plus = 0x2B,           // +
        LeftBrace = 0x7B,      // {
        Pipe = 0x7C,           // |
        RightBrace = 0x7D,     // }
        Tilde = 0x7E,          // ~
        DoubleQuote = 0x22,    // "
        Colon = 0x3A,          // :
        Less = 0x3C,           // <
        Greater = 0x3E,        // >
        Question = 0x3F,       // ?

        // Arrow keys
        Left = 0x80,
        Up,
        Right,
        Down,

        // Navigation keys
        Home,
        End,
        PageUp,
        PageDown,
        Insert,

        // Function keys
        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,

        // Modifier keys
        LeftShift,
        RightShift,
        LeftCtrl,
        RightCtrl,
        LeftAlt,
        RightAlt,
        LeftMeta,  // Linux Super key
        RightMeta,

        // Number pad
        NumLock,
        NumPadInsert,
        NumPadDelete,
        NumPadHome,
        NumPadLeft,
        NumPadUp,
        NumPadRight,
        NumPadDown,
        NumPadPageUp,
        NumPadPageDown,
        NumPadEnd,
        NumPad0,
        NumPad1,
        NumPad2,
        NumPad3,
        NumPad4,
        NumPad5,
        NumPad6,
        NumPad7,
        NumPad8,
        NumPad9,
        NumPadDecimal,
        NumPadPlus,
        NumPadMinus,
        NumPadMultiply,
        NumPadDivide,
        NumPadEnter,

        // Special system keys
        CapsLock,
        ScrollLock,
        Pause,
        PrintScreen,
        SysReq,
        Break,
        Menu,
        Power,
        Sleep,

        // Media keys (where supported)
        VolumeUp,
        VolumeDown,
        VolumeMute,
        MediaPlay,
        MediaStop,
        MediaPrevious,
        MediaNext,

        // Browser keys (where supported)
        BrowserBack,
        BrowserForward,
        BrowserRefresh,
        BrowserStop,
        BrowserSearch,
        BrowserFavorites,
        BrowserHome
    };

// Convenience aliases for commonly misnamed keys
    constexpr UCKeys Ctrl = LeftCtrl;
    constexpr UCKeys Control = LeftCtrl;
    constexpr UCKeys Alt = LeftAlt;
    constexpr UCKeys Meta = LeftMeta;
    constexpr UCKeys Super = LeftMeta;
    constexpr UCKeys Windows = LeftMeta;

// Mouse button identifiers (matches X11 button numbers)
    enum class UCMouseButton {
        NoneButton = 0,
        Left = 1,
        Middle = 2,
        Right = 4,
        WheelUp = 8,
        WheelDown = 16,
        WheelLeft = 32,
        WheelRight = 64,
        Unknown = 99,
        Back = 128,      // side/thumb "back" button (X11 button 8, Windows XBUTTON1)
        Forward = 256    // side/thumb "forward" button (X11 button 9, Windows XBUTTON2)
    };

    struct UCEvent {
        UCEventType type = UCEventType::NoneEvent;
        UltraCanvasUIElement* targetElement = nullptr;

        // Spatial coordinates
        Point2Di pointer;                    // Mouse or touch coordinates
        Point2Di pointerWindow;              // Window-relative coordinates
        Point2Di pointerGlobal;              // Global screen coordinates

        // Mouse/Touch specific
        UCMouseButton button = UCMouseButton::NoneButton;
        int wheelDelta = 0;                       // Wheel or zoom delta
        float pressure = 1.0f;               // Touch pressure (0.0-1.0)

        // Keyboard specific
        int nativeKeyCode = 0;                     // Platform-specific key code
        UCKeys virtualKey = UCKeys::Unknown;                  // Virtual key code (cross-platform)
        char character = 0;                  // Character representation
        std::string text;                    // For multi-character input (IME, etc.)

        // Modifier keys
        bool ctrl = false, shift = false, alt = false, meta = false;

        // Timing (useful for double-click detection, animations)
        std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();

        // Drag and Drop
        std::string dragData;                // Dragged data (text, file paths, etc.)
        std::string dragMimeType;            // MIME type of dragged data
        std::vector<std::string> droppedFiles; // Multiple file paths for file drop

        // Window specific
        int width = 0, height = 0;           // For resize events

        std::weak_ptr<UltraCanvasWindowBase> targetWindow;    // Weak ref to the target UltraCanvasWindow
        // Platform-specific window handle (X11 Window, HWND, etc.)
#if defined(_WIN32) || defined(_WIN64)
        NativeWindowHandle nativeWindowHandle = nullptr;
#elif defined(__linux__) || defined(__unix__)
        NativeWindowHandle nativeWindowHandle = 0;
#elif defined(__APPLE__)
        NativeWindowHandle nativeWindowHandle = nullptr;
#else
        NativeWindowHandle nativeWindowHandle = nullptr;
#endif
        // Generic data
        union {
            void *userDataPtr = nullptr;            // Custom user data
            int userDataInt;
            float userDataFloat;
            double userDataDouble;
        };
//        int customData1 = 0, customData2 = 0; // Additional data fields
//        unsigned long nativeEvent = 0;      // Platform-specific event handle

        UCEvent Clone() const {
            return *this;
        }

        // Utility methods
        bool IsMouseEvent() const {
            return type == UCEventType::MouseDown ||
                   type == UCEventType::MouseUp ||
                   type == UCEventType::MouseMove ||
                   type == UCEventType::MouseDoubleClick ||
                   type == UCEventType::MouseWheel;
        }

        bool IsMouseClickEvent() const {
            return type == UCEventType::MouseDown ||
                   type == UCEventType::MouseUp ||
                   type == UCEventType::MouseDoubleClick;
        }

        bool IsKeyboardEvent() const {
            return type >= UCEventType::KeyDown && type <= UCEventType::TextInput;
        }

        bool IsWindowEvent() const {
            return type >= UCEventType::WindowResize && type <= UCEventType::WindowBlur;
        }

        bool IsTouchEvent() const {
            return type >= UCEventType::TouchStart && type <= UCEventType::PinchZoom;
        }

        bool IsDragEvent() const {
            return type >= UCEventType::DragStart && type <= UCEventType::Drop;
        }

        bool isCommandEvent() const {
            return type > UCEventType::CommandEventsStart && type < UCEventType::CommandEventsEnd;
        }

//        float GetAge() const {
//            auto now = std::chrono::steady_clock::now();
//            return std::chrono::duration<float>(now - timestamp).count();
//        }
//
//        bool IsKeyRepeat() const {
//            // Simple repeat detection - can be enhanced
//            return GetAge() < 0.1f && (type == UCEventType::KeyDown);
//        }

        std::string ToString() const;
    };

}
#endif // ULTRA_CANVAS_EVENT_H
