// core/UltraCanvasEvent.cpp - Enhanced Version
// Event system for UltraCanvas Framework with Linux optimizations
// Version: 2.2.0
// Last Modified: 2026-04-21
// Author: UltraCanvas Framework
#include "UltraCanvasEvent.h"
#include <iterator>   // std::size
namespace UltraCanvas {
    // Indexed by UCEventType, so this table must list exactly the enum's
    // members in the enum's order. It once carried three names with no enum
    // counterpart ("KeyChar", "Shortcut", "WindowClosing"), so every event
    // from TextInput onwards printed as the name of an earlier one - a
    // WindowResize showed up as "WindowCloseRequest" in the debug output.
    // The static_assert below fails the build when the two drift apart again;
    // a name added here without its enum member, or the reverse, is caught at
    // compile time rather than in a misleading log line.
    namespace {
    constexpr const char* const kEventTypeNames[] = {
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
            "TextInput",        // Added for text input events

            // Window Events
            "WindowCloseRequest",
            "WindowResize",
            "WindowMove",
            "WindowMinimize",
            "WindowRestore",
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
    static_assert(std::size(kEventTypeNames) == static_cast<size_t>(UCEventType::Unknown) + 1,
                  "eventTypeNames must have one entry per UCEventType member, in enum order");
    } // namespace

    std::string UCEvent::ToString() const {
        std::string result = "UCEvent{type=";
        const auto index = static_cast<size_t>(type);
        result += index < std::size(kEventTypeNames) ? kEventTypeNames[index] : "OutOfRange";
        if (IsMouseEvent()) {
            result += ",pos=(" + std::to_string(pointer.x) + "," + std::to_string(pointer.y) + ")";
            result += ",btn=" + std::to_string(static_cast<int>(button));
        }
        if (IsTouchEvent()) {
            result += ",pos=(" + std::to_string(pointer.x) + "," + std::to_string(pointer.y) + ")";
            result += ",id=" + std::to_string(pointerId);
            result += ",fingers=" + std::to_string(touchPointCount);
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
