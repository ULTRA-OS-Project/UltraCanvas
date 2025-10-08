// OS/MacOS/UltraCanvasMacOSEventBridge.h
// Event translation system for UltraCanvas Framework on macOS
// Version: 1.0.1
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_MACOS_EVENT_BRIDGE_H
#define ULTRACANVAS_MACOS_EVENT_BRIDGE_H

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasCommonTypes.h"

// ===== MACOS PLATFORM INCLUDES =====
#ifdef __OBJC__
    #import <Cocoa/Cocoa.h>
    #import <Carbon/Carbon.h>
#else
    // Forward declarations for C++ only files
    typedef struct objc_object NSEvent;
    typedef unsigned long NSEventType;
    typedef unsigned long NSEventModifierFlags;
    typedef unsigned short unichar;
#endif

// ===== STANDARD INCLUDES =====
#include <string>
#include <unordered_map>
#include <chrono>

namespace UltraCanvas {

// ===== EVENT BRIDGE CLASS =====
class MacOSEventBridge {
public:
    // ===== MAIN CONVERSION FUNCTIONS =====
    static UCEvent ConvertNSEventToUCEvent(NSEvent* nsEvent, void* targetWindow = nullptr);
    
    // ===== SPECIFIC EVENT TYPE CONVERTERS =====
    static UCEvent ConvertMouseEvent(NSEvent* nsEvent, void* targetWindow);
    static UCEvent ConvertKeyboardEvent(NSEvent* nsEvent, void* targetWindow);
    static UCEvent ConvertScrollEvent(NSEvent* nsEvent, void* targetWindow);
    static UCEvent ConvertWindowEvent(NSEvent* nsEvent, void* targetWindow);
    
    // ===== KEY CODE CONVERSION =====
    static UCKeyCode ConvertNSEventKeyCode(unsigned short keyCode);
    static UCKeyCode ConvertCharacterToKeyCode(unichar character);
    static unsigned short ConvertUCKeyCodeToNSEventKeyCode(UCKeyCode keyCode);
    
    // ===== MODIFIER FLAGS CONVERSION =====
    static UCModifierFlags ConvertNSEventModifierFlags(NSEventModifierFlags modifierFlags);
    static NSEventModifierFlags ConvertUCModifierFlags(UCModifierFlags modifierFlags);
    
    // ===== MOUSE BUTTON CONVERSION =====
    static UCMouseButton ConvertNSEventMouseButton(NSEvent* nsEvent);
    static int ConvertUCMouseButtonToNSEventButton(UCMouseButton button);
    
    // ===== COORDINATE CONVERSION =====
    static Point2D ConvertNSEventLocationToUCPoint(NSEvent* nsEvent, void* targetWindow);
    static Point2D ConvertCocoaCoordinates(float x, float y, float windowHeight);
    
    // ===== STRING CONVERSION =====
    static std::string ConvertNSStringToStdString(void* nsString);
    static void* ConvertStdStringToNSString(const std::string& str);
    
    // ===== EVENT TYPE MAPPING =====
    static UCEventType GetUCEventTypeFromNSEventType(NSEventType eventType);
    
private:
    // ===== STATIC LOOKUP TABLES =====
    static void InitializeKeyCodeMappings();
    static void InitializeModifierMappings();
    static void InitializeEventTypeMappings();
    
    // Static initialization flag
    static bool s_initialized;
    
    // ===== KEY CODE MAPPING TABLES =====
    static std::unordered_map<unsigned short, UCKeyCode> s_nsKeyCodeToUCKeyCode;
    static std::unordered_map<UCKeyCode, unsigned short> s_ucKeyCodeToNSKeyCode;
    static std::unordered_map<unichar, UCKeyCode> s_characterToKeyCode;
    
    // ===== MODIFIER FLAG MAPPINGS =====
    static std::unordered_map<NSEventModifierFlags, UCModifierFlags> s_nsModifierToUCModifier;
    static std::unordered_map<UCModifierFlags, NSEventModifierFlags> s_ucModifierToNSModifier;
    
    // ===== EVENT TYPE MAPPINGS =====
    static std::unordered_map<NSEventType, UCEventType> s_nsEventTypeToUCEventType;
    
    // ===== HELPER METHODS =====
    static void EnsureInitialized();
    static bool IsMouseEvent(NSEventType eventType);
    static bool IsKeyboardEvent(NSEventType eventType);
    static bool IsScrollEvent(NSEventType eventType);
    static bool IsWindowEvent(NSEventType eventType);
    
    // ===== EVENT PROPERTY HELPERS =====
    static void SetCommonEventProperties(UCEvent& ucEvent, NSEvent* nsEvent, void* targetWindow);
    static std::chrono::steady_clock::time_point ConvertNSEventTimestamp(NSEvent* nsEvent);
    
    // ===== SPECIAL KEY HANDLING =====
    static bool IsSpecialKey(unsigned short keyCode);
    static UCKeyCode HandleSpecialKey(unsigned short keyCode);
    
    // ===== COORDINATE SYSTEM HELPERS =====
    static float GetWindowHeight(void* targetWindow);
    static Point2D AdjustForWindowCoordinates(const Point2D& point, void* targetWindow);
};

// ===== KEY CODE CONSTANTS (macOS Virtual Key Codes) =====
namespace MacOSKeyCodes {
    // Letter keys
    constexpr unsigned short kVK_ANSI_A                    = 0x00;
    constexpr unsigned short kVK_ANSI_S                    = 0x01;
    constexpr unsigned short kVK_ANSI_D                    = 0x02;
    constexpr unsigned short kVK_ANSI_F                    = 0x03;
    constexpr unsigned short kVK_ANSI_H                    = 0x04;
    constexpr unsigned short kVK_ANSI_G                    = 0x05;
    constexpr unsigned short kVK_ANSI_Z                    = 0x06;
    constexpr unsigned short kVK_ANSI_X                    = 0x07;
    constexpr unsigned short kVK_ANSI_C                    = 0x08;
    constexpr unsigned short kVK_ANSI_V                    = 0x09;
    constexpr unsigned short kVK_ANSI_B                    = 0x0B;
    constexpr unsigned short kVK_ANSI_Q                    = 0x0C;
    constexpr unsigned short kVK_ANSI_W                    = 0x0D;
    constexpr unsigned short kVK_ANSI_E                    = 0x0E;
    constexpr unsigned short kVK_ANSI_R                    = 0x0F;
    constexpr unsigned short kVK_ANSI_Y                    = 0x10;
    constexpr unsigned short kVK_ANSI_T                    = 0x11;
    constexpr unsigned short kVK_ANSI_1                    = 0x12;
    constexpr unsigned short kVK_ANSI_2                    = 0x13;
    constexpr unsigned short kVK_ANSI_3                    = 0x14;
    constexpr unsigned short kVK_ANSI_4                    = 0x15;
    constexpr unsigned short kVK_ANSI_6                    = 0x16;
    constexpr unsigned short kVK_ANSI_5                    = 0x17;
    constexpr unsigned short kVK_ANSI_Equal                = 0x18;
    constexpr unsigned short kVK_ANSI_9                    = 0x19;
    constexpr unsigned short kVK_ANSI_7                    = 0x1A;
    constexpr unsigned short kVK_ANSI_Minus                = 0x1B;
    constexpr unsigned short kVK_ANSI_8                    = 0x1C;
    constexpr unsigned short kVK_ANSI_0                    = 0x1D;
    constexpr unsigned short kVK_ANSI_RightBracket         = 0x1E;
    constexpr unsigned short kVK_ANSI_O                    = 0x1F;
    constexpr unsigned short kVK_ANSI_U                    = 0x20;
    constexpr unsigned short kVK_ANSI_LeftBracket          = 0x21;
    constexpr unsigned short kVK_ANSI_I                    = 0x22;
    constexpr unsigned short kVK_ANSI_P                    = 0x23;
    constexpr unsigned short kVK_ANSI_L                    = 0x25;
    constexpr unsigned short kVK_ANSI_J                    = 0x26;
    constexpr unsigned short kVK_ANSI_Quote                = 0x27;
    constexpr unsigned short kVK_ANSI_K                    = 0x28;
    constexpr unsigned short kVK_ANSI_Semicolon           = 0x29;
    constexpr unsigned short kVK_ANSI_Backslash           = 0x2A;
    constexpr unsigned short kVK_ANSI_Comma                = 0x2B;
    constexpr unsigned short kVK_ANSI_Slash                = 0x2C;
    constexpr unsigned short kVK_ANSI_N                    = 0x2D;
    constexpr unsigned short kVK_ANSI_M                    = 0x2E;
    constexpr unsigned short kVK_ANSI_Period               = 0x2F;
    constexpr unsigned short kVK_ANSI_Grave                = 0x32;
    
    // Keypad keys
    constexpr unsigned short kVK_ANSI_KeypadDecimal        = 0x41;
    constexpr unsigned short kVK_ANSI_KeypadMultiply      = 0x43;
    constexpr unsigned short kVK_ANSI_KeypadPlus          = 0x45;
    constexpr unsigned short kVK_ANSI_KeypadClear         = 0x47;
    constexpr unsigned short kVK_ANSI_KeypadDivide        = 0x4B;
    constexpr unsigned short kVK_ANSI_KeypadEnter         = 0x4C;
    constexpr unsigned short kVK_ANSI_KeypadMinus         = 0x4E;
    constexpr unsigned short kVK_ANSI_KeypadEquals        = 0x51;
    constexpr unsigned short kVK_ANSI_Keypad0             = 0x52;
    constexpr unsigned short kVK_ANSI_Keypad1             = 0x53;
    constexpr unsigned short kVK_ANSI_Keypad2             = 0x54;
    constexpr unsigned short kVK_ANSI_Keypad3             = 0x55;
    constexpr unsigned short kVK_ANSI_Keypad4             = 0x56;
    constexpr unsigned short kVK_ANSI_Keypad5             = 0x57;
    constexpr unsigned short kVK_ANSI_Keypad6             = 0x58;
    constexpr unsigned short kVK_ANSI_Keypad7             = 0x59;
    constexpr unsigned short kVK_ANSI_Keypad8             = 0x5B;
    constexpr unsigned short kVK_ANSI_Keypad9             = 0x5C;
    
    // Function keys and special keys
    constexpr unsigned short kVK_Return                    = 0x24;
    constexpr unsigned short kVK_Tab                       = 0x30;
    constexpr unsigned short kVK_Space                     = 0x31;
    constexpr unsigned short kVK_Delete                    = 0x33;
    constexpr unsigned short kVK_Escape                    = 0x35;
    constexpr unsigned short kVK_Command                   = 0x37;
    constexpr unsigned short kVK_Shift                     = 0x38;
    constexpr unsigned short kVK_CapsLock                  = 0x39;
    constexpr unsigned short kVK_Option                    = 0x3A;
    constexpr unsigned short kVK_Control                   = 0x3B;
    constexpr unsigned short kVK_RightShift               = 0x3C;
    constexpr unsigned short kVK_RightOption              = 0x3D;
    constexpr unsigned short kVK_RightControl             = 0x3E;
    constexpr unsigned short kVK_Function                  = 0x3F;
    constexpr unsigned short kVK_F17                       = 0x40;
    constexpr unsigned short kVK_VolumeUp                  = 0x48;
    constexpr unsigned short kVK_VolumeDown                = 0x49;
    constexpr unsigned short kVK_Mute                      = 0x4A;
    constexpr unsigned short kVK_F18                       = 0x4F;
    constexpr unsigned short kVK_F19                       = 0x50;
    constexpr unsigned short kVK_F20                       = 0x5A;
    constexpr unsigned short kVK_F5                        = 0x60;
    constexpr unsigned short kVK_F6                        = 0x61;
    constexpr unsigned short kVK_F7                        = 0x62;
    constexpr unsigned short kVK_F3                        = 0x63;
    constexpr unsigned short kVK_F8                        = 0x64;
    constexpr unsigned short kVK_F9                        = 0x65;
    constexpr unsigned short kVK_F11                       = 0x67;
    constexpr unsigned short kVK_F13                       = 0x69;
    constexpr unsigned short kVK_F16                       = 0x6A;
    constexpr unsigned short kVK_F14                       = 0x6B;
    constexpr unsigned short kVK_F10                       = 0x6D;
    constexpr unsigned short kVK_F12                       = 0x6F;
    constexpr unsigned short kVK_F15                       = 0x71;
    constexpr unsigned short kVK_Help                      = 0x72;
    constexpr unsigned short kVK_Home                      = 0x73;
    constexpr unsigned short kVK_PageUp                    = 0x74;
    constexpr unsigned short kVK_ForwardDelete            = 0x75;
    constexpr unsigned short kVK_F4                        = 0x76;
    constexpr unsigned short kVK_End                       = 0x77;
    constexpr unsigned short kVK_F2                        = 0x78;
    constexpr unsigned short kVK_PageDown                  = 0x79;
    constexpr unsigned short kVK_F1                        = 0x7A;
    constexpr unsigned short kVK_LeftArrow                 = 0x7B;
    constexpr unsigned short kVK_RightArrow               = 0x7C;
    constexpr unsigned short kVK_DownArrow                 = 0x7D;
    constexpr unsigned short kVK_UpArrow                   = 0x7E;
} // namespace MacOSKeyCodes

// ===== EVENT TYPE MAPPING HELPERS =====
namespace MacOSEventTypeMapping {
    // Mouse event types
    constexpr NSEventType NSEventTypeLeftMouseDown        = 1;
    constexpr NSEventType NSEventTypeLeftMouseUp          = 2;
    constexpr NSEventType NSEventTypeRightMouseDown       = 3;
    constexpr NSEventType NSEventTypeRightMouseUp         = 4;
    constexpr NSEventType NSEventTypeMouseMoved           = 5;
    constexpr NSEventType NSEventTypeLeftMouseDragged     = 6;
    constexpr NSEventType NSEventTypeRightMouseDragged    = 7;
    constexpr NSEventType NSEventTypeMouseEntered         = 8;
    constexpr NSEventType NSEventTypeMouseExited          = 9;
    constexpr NSEventType NSEventTypeKeyDown              = 10;
    constexpr NSEventType NSEventTypeKeyUp                = 11;
    constexpr NSEventType NSEventTypeFlagsChanged         = 12;
    constexpr NSEventType NSEventTypeScrollWheel          = 22;
    constexpr NSEventType NSEventTypeOtherMouseDown       = 25;
    constexpr NSEventType NSEventTypeOtherMouseUp         = 26;
    constexpr NSEventType NSEventTypeOtherMouseDragged    = 27;
} // namespace MacOSEventTypeMapping

// ===== UTILITY FUNCTIONS =====
namespace MacOSEventUtils {
    // Quick conversion utilities for common operations
    Point2D NSPointToUCPoint(void* nsPoint);
    void* UCPointToNSPoint(const Point2D& point);
    
    Size2D NSSizeToUCSize(void* nsSize);
    void* UCSizeToNSSize(const Size2D& size);
    
    Rect2D NSRectToUCRect(void* nsRect);
    void* UCRectToNSRect(const Rect2D& rect);
    
    // Event timing utilities
    std::chrono::steady_clock::time_point GetCurrentEventTime();
    double GetEventDeltaTime(NSEvent* event1, NSEvent* event2);
    
    // Modifier key utilities
    bool IsCommandKeyPressed(NSEventModifierFlags flags);
    bool IsOptionKeyPressed(NSEventModifierFlags flags);
    bool IsControlKeyPressed(NSEventModifierFlags flags);
    bool IsShiftKeyPressed(NSEventModifierFlags flags);
    bool IsFunctionKeyPressed(NSEventModifierFlags flags);
} // namespace MacOSEventUtils

} // namespace UltraCanvas

#endif // ULTRACANVAS_MACOS_EVENT_BRIDGE_H