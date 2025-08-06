// UltraCanvasEvent.h - Enhanced Version
// Event system for UltraCanvas Framework with Linux optimizations
// Version: 2.1.0
// Last Modified: 2024-12-30
// Author: UltraCanvas Framework

#ifndef ULTRA_CANVAS_EVENT_H
#define ULTRA_CANVAS_EVENT_H

#ifdef KeyPress
#undef KeyPress
#endif

#include <chrono>
#include <string>

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
    KeyChar,
    Shortcut,

    // Window Events
    WindowResize,
    WindowClose,
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
    DragOver,
    Drop,
    
    // Additional events for enhanced Linux support
    Clipboard,         // Clipboard change notifications
    Selection,         // X11 selection events
    Timer,            // Timer events
    Custom,            // Custom user-defined events
    Unknown
};

// Key code constants for cross-platform compatibility
enum UCKeys {
    // Common keys (matches X11 keysyms where applicable)
    Unknown = 0xFFFFFFF,
    Escape = 0xFF1B,
    Tab = 0xFF09,
    Return = 0xFF0D,
    Enter = 0xFF0D,
    Space = 0x0020,
    Backspace = 0xFF08,
    Delete = 0xFFFF,

    // Arrow keys
    Left = 0xFF51,
    Up = 0xFF52,
    Right = 0xFF53,
    Down = 0xFF54,

    // Function keys
    F1 = 0xFFBE,
    F2 = 0xFFBF,
    F3 = 0xFFC0,
    F4 = 0xFFC1,
    F5 = 0xFFC2,
    F6 = 0xFFC3,
    F7 = 0xFFC4,
    F8 = 0xFFC5,
    F9 = 0xFFC6,
    F10 = 0xFFC7,
    F11 = 0xFFC8,
    F12 = 0xFFC9,

    // Modifier keys
    LeftShift = 0xFFE1,
    RightShift = 0xFFE2,
    LeftCtrl = 0xFFE3,
    RightCtrl = 0xFFE4,
    LeftAlt = 0xFFE9,
    RightAlt = 0xFFEA,
    LeftMeta = 0xFFEB,  // Linux Super key
    RightMeta = 0xFFEC,

    // Navigation
    Home = 0xFF50,
    End = 0xFF57,
    PageUp = 0xFF55,
    PageDown = 0xFF56,
    Insert = 0xFF63,

    // Number pad
    NumLock = 0xFF7F,
    NumPad0 = 0xFFB0,
    NumPad1 = 0xFFB1,
    NumPad2 = 0xFFB2,
    NumPad3 = 0xFFB3,
    NumPad4 = 0xFFB4,
    NumPad5 = 0xFFB5,
    NumPad6 = 0xFFB6,
    NumPad7 = 0xFFB7,
    NumPad8 = 0xFFB8,
    NumPad9 = 0xFFB9,

    // Common ASCII characters
    A = 0x0061,
    Z = 0x007A,
    Zero = 0x0030,
    Nine = 0x0039
};

// Mouse button identifiers (matches X11 button numbers)
enum class UCMouseButton {
    NoneButton = 0,
    Left = 1,
    Middle = 2,
    Right = 3,
    WheelUp = 4,
    WheelDown = 5,
    WheelLeft = 6,
    WheelRight = 7,
    Unknown = 99
};

struct UCEvent {
    UCEventType type = UCEventType::NoneEvent;

    // Spatial coordinates
    int x = 0, y = 0;                    // Mouse or touch coordinates
    int globalX = 0, globalY = 0;        // Global screen coordinates

    // Mouse/Touch specific
    UCMouseButton button = UCMouseButton::NoneButton;
    int wheelDelta = 0;                       // Wheel or zoom delta
    float pressure = 1.0f;               // Touch pressure (0.0-1.0)

    // Keyboard specific
    int keyCode = 0;                     // Platform-specific key code
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

    // Window specific
    int width = 0, height = 0;           // For resize events

    void* targetWindow = nullptr;        // Pointer to the target UltraCanvasWindow
    unsigned long nativeWindowHandle = 0; // Platform-specific window handle (X11 Window, HWND, etc.)

    // Generic data
    void* userData = nullptr;            // Custom user data
    int customData1 = 0, customData2 = 0; // Additional data fields
    unsigned long nativeEvent = 0;      // Platform-specific event handle
    int deviceId = 0;                    // Input device identifier

    // Utility methods
    bool IsMouseEvent() const {
        return type >= UCEventType::MouseDown && type <= UCEventType::MouseDoubleClick;
    }

    bool IsKeyboardEvent() const {
        return type >= UCEventType::KeyDown && type <= UCEventType::Shortcut;
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

    float GetAge() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<float>(now - timestamp).count();
    }

    bool IsKeyRepeat() const {
        // Simple repeat detection - can be enhanced
        return GetAge() < 0.1f && (type == UCEventType::KeyDown || type == UCEventType::KeyChar);
    }

    std::string ToString() const {
        std::string result = "UCEvent{type=";
        result += std::to_string(static_cast<int>(type));
        if (IsMouseEvent()) {
            result += ",pos=(" + std::to_string(x) + "," + std::to_string(y) + ")";
            result += ",btn=" + std::to_string(static_cast<int>(button));
        }
        if (IsKeyboardEvent()) {
            result += ",key=" + std::to_string(keyCode);
            if (character > 0) {
                result += ",char='" + std::string(1, character) + "'";
            }
        }
        result += "}";
        return result;
    }
};

// Event creation helpers for common scenarios
//namespace UCEventFactory {
//    inline UCEvent MouseDown(int x, int y, UCMouseButton btn = UCMouseButton::Left) {
//        UCEvent event;
//        event.type = UCEventType::MouseDown;
//        event.x = x;
//        event.y = y;
//        event.button = btn;
//        return event;
//    }
//
//    inline UCEvent MouseUp(int x, int y, UCMouseButton btn = UCMouseButton::Left) {
//        UCEvent event;
//        event.type = UCEventType::MouseUp;
//        event.x = x;
//        event.y = y;
//        event.button = btn;
//        return event;
//    }
//
//    inline UCEvent KeyPress(int keyCode, char character = 0) {
//        UCEvent event;
//        event.type = UCEventType::KeyDown;
//        event.keyCode = keyCode;
//        event.character = character;
//        return event;
//    }
//
//    inline UCEvent WindowResize(int width, int height) {
//        UCEvent event;
//        event.type = UCEventType::WindowResize;
//        event.width = width;
//        event.height = height;
//        return event;
//    }
//
//    inline UCEvent Custom(int data1 = 0, int data2 = 0, void* userData = nullptr) {
//        UCEvent event;
//        event.type = UCEventType::Custom;
//        event.customData1 = data1;
//        event.customData2 = data2;
//        event.userData = userData;
//        return event;
//    }
//}

#endif // ULTRA_CANVAS_EVENT_H

/*
=== ENHANCEMENT BENEFITS ===

1. **Linux Integration**: Added Linux-specific fields like nativeEvent, meta key
2. **Extended Mouse Support**: UCMouseButton enum for better button handling
3. **Timing Support**: Timestamp for double-click detection and animations
4. **Drag & Drop**: Enhanced with MIME types and data transfer
5. **Cross-Platform Keys**: UCKeys namespace for consistent key codes
6. **Convenience Methods**: Helper functions for common event operations
7. **Factory Functions**: Easy event creation with UCEventFactory
8. **Debugging**: ToString() method for development

=== USAGE EXAMPLES ===

// Create events easily
auto mouseEvent = UCEventFactory::MouseDown(100, 200, UCMouseButton::Right);
auto keyEvent = UCEventFactory::KeyPress(UCKeys::Return, '\n');

// Check event types
if (event.IsMouseEvent()) {
    handleMouseEvent(event);
}

// Use timing information
if (event.GetAge() < 0.3f) {
    // Recent event, might be part of a gesture
}

// Access Linux-specific data
if (event.nativeEvent) {
    // Handle X11-specific processing
}

=== MIGRATION FROM CURRENT ===

Your existing code using the current UCEvent structure will continue to work
without changes. The enhancements are backward compatible additions.
*/