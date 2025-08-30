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
    TextInput,        // Added for text input events
    Shortcut,

    // Window Events
    WindowResize,
    WindowMove,
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
    // Special values
    Unknown = 0xFFFFFFF,

    // Control keys
    Escape = 0xFF1B,
    Tab = 0xFF09,
    Return = 0xFF0D,
    Enter = 0xFF0D,
    Space = 0x0020,
    Backspace = 0xFF08,
    Delete = 0xFFFF,

    // Arrow keys
    Left = 0xFF51,
    LeftArrow = 0xFF51,
    Up = 0xFF52,
    UpArrow = 0xFF52,
    Right = 0xFF53,
    RightArrow = 0xFF53,
    Down = 0xFF54,
    DownArrow = 0xFF54,

    // Navigation keys
    Home = 0xFF50,
    End = 0xFF57,
    PageUp = 0xFF55,
    PageDown = 0xFF56,
    Insert = 0xFF63,

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
    LeftControl = 0xFFE3,
    RightCtrl = 0xFFE4,
    RightControl = 0xFFE4,
    LeftAlt = 0xFFE9,
    RightAlt = 0xFFEA,
    LeftMeta = 0xFFEB,  // Linux Super key
    RightMeta = 0xFFEC,

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
    NumPadDecimal = 0xFFAE,
    NumPadAdd = 0xFFAB,
    NumPadSubtract = 0xFFAD,
    NumPadMultiply = 0xFFAA,
    NumPadDivide = 0xFFAF,
    NumPadEnter = 0xFF8D,

    // Number row (0-9)
    Key0 = 0x0030,
    Key1 = 0x0031,
    Key2 = 0x0032,
    Key3 = 0x0033,
    Key4 = 0x0034,
    Key5 = 0x0035,
    Key6 = 0x0036,
    Key7 = 0x0037,
    Key8 = 0x0038,
    Key9 = 0x0039,

    // Letters A-Z (ASCII values)
    A = 0x0041,
    B = 0x0042,
    C = 0x0043,
    D = 0x0044,
    E = 0x0045,
    F = 0x0046,
    G = 0x0047,
    H = 0x0048,
    I = 0x0049,
    J = 0x004A,
    K = 0x004B,
    L = 0x004C,
    M = 0x004D,
    N = 0x004E,
    O = 0x004F,
    P = 0x0050,
    Q = 0x0051,
    R = 0x0052,
    S = 0x0053,
    T = 0x0054,
    U = 0x0055,
    V = 0x0056,
    W = 0x0057,
    X = 0x0058,
    Y = 0x0059,
    Z = 0x005A,

    // Punctuation and symbols (commonly used)
    Semicolon = 0x003B,      // ;
    Equal = 0x003D,          // =
    Comma = 0x002C,          // ,
    Minus = 0x002D,          // -
    Period = 0x002E,         // .
    Slash = 0x002F,          // /
    Grave = 0x0060,          // `
    LeftBracket = 0x005B,    // [
    Backslash = 0x005C,      // \
    RightBracket = 0x005D,   // ]
    Quote = 0x0027,          // '

    // Additional symbols
    Exclamation = 0x0021,    // !
    At = 0x0040,             // @
    Hash = 0x0023,           // #
    Dollar = 0x0024,         // $
    Percent = 0x0025,        // %
    Caret = 0x005E,          // ^
    Ampersand = 0x0026,      // &
    Asterisk = 0x002A,       // *
    LeftParen = 0x0028,      // (
    RightParen = 0x0029,     // )
    Underscore = 0x005F,     // _
    Plus = 0x002B,           // +
    LeftBrace = 0x007B,      // {
    Pipe = 0x007C,           // |
    RightBrace = 0x007D,     // }
    Tilde = 0x007E,          // ~
    DoubleQuote = 0x0022,    // "
    Colon = 0x003A,          // :
    Less = 0x003C,           // <
    Greater = 0x003E,        // >
    Question = 0x003F,       // ?

    // Special system keys
    CapsLock = 0xFFE5,
    ScrollLock = 0xFF14,
    Pause = 0xFF13,
    PrintScreen = 0xFF61,
    SysReq = 0xFF15,
    Break = 0xFF6B,
    Menu = 0xFF67,
    Power = 0xFF2A,
    Sleep = 0xFF2F,

    // Media keys (where supported)
    VolumeUp = 0x1008FF13,
    VolumeDown = 0x1008FF11,
    VolumeMute = 0x1008FF12,
    MediaPlay = 0x1008FF14,
    MediaStop = 0x1008FF15,
    MediaPrevious = 0x1008FF16,
    MediaNext = 0x1008FF17,

    // Browser keys (where supported)
    BrowserBack = 0x1008FF26,
    BrowserForward = 0x1008FF27,
    BrowserRefresh = 0x1008FF29,
    BrowserStop = 0x1008FF28,
    BrowserSearch = 0x1008FF1B,
    BrowserFavorites = 0x1008FF30,
    BrowserHome = 0x1008FF18
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
    int x, y;                        // Mouse or touch coordinates
    int globalX, globalY;        // Global screen coordinates

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
            result += ",nativeKey=" + std::to_string(nativeKeyCode);
            if (character > 0) {
                result += ",char='" + std::string(1, character) + "'";
            }
        }
        result += "}";
        return result;
    }
};

#endif // ULTRA_CANVAS_EVENT_H
