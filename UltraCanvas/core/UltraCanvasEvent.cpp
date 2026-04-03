// core/UltraCanvasEvent.cpp - Enhanced Version
// Event system for UltraCanvas Framework with Linux optimizations
// Version: 2.1.0
// Last Modified: 2024-12-30
// Author: UltraCanvas Framework
#include "UltraCanvasEvent.h"
namespace UltraCanvas {
    std::vector<std::string> eventTypeNames = {
            "NoneEvent",

            // Mouse Events
            "MouseDown",
            "MouseUp",
            "MouseMove",
            "MouseEnter",
            "MouseLeave",
            "MouseWheel",
            "MouseWheelHorizontal",
            "MouseDoubleClick",

            // Keyboard Events
            "KeyDown",
            "KeyUp",
            "KeyChar",
            "TextInput",        // Added for text input events
            "Shortcut",

            // Window Events
            "WindowCloseRequest",
            "WindowClosing",
            "WindowResize",
            "WindowMove",
            "WindowMinimize",
            "WindowFocus",
            "WindowBlur",
            "WindowRepaint",

            // Touch Events (for future mobile support)
            "TouchStart",
            "TouchMove",
            "TouchEnd",
            "Tap",
            "PinchZoom",

            // Focus Events
            "FocusGained",
            "FocusLost",

            // Drag and Drop
            "DragStart",
            "DragEnter",
            "DragLeave",
            "DragOver",
            "Drop",

            // Additional events for enhanced Linux support
            "Clipboard",         // Clipboard change notifications
            "Selection",         // X11 selection events
            "Timer",            // Timer events
            "Custom",            // Custom user-defined events

            // command events
            "CommandEventsStart", // custom events just to mark the range
            "ButtonClick",
            "DropdownSelect",
            "MenuClick",
            "CommandEventsEnd", // custom events just to mark the range

            "Redraw",
            "Unknown"
    };

    std::string UCEvent::ToString() const {
        std::string result = "UCEvent{type=";
        result += eventTypeNames[static_cast<int>(type)];
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
}
