// OS/MacOS/UltraCanvasMacOSEventBridge.cpp
// Complete event translation system implementation
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasMacOSEventBridge.h"
#include "UltraCanvasMacOSWindow.h"

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>

#include <iostream>
#include <unordered_map>
#include <chrono>

namespace UltraCanvas {

// ===== STATIC INITIALIZATION =====
bool MacOSEventBridge::s_initialized = false;

std::unordered_map<unsigned short, UCKeyCode> MacOSEventBridge::s_nsKeyCodeToUCKeyCode;
std::unordered_map<UCKeyCode, unsigned short> MacOSEventBridge::s_ucKeyCodeToNSKeyCode;
std::unordered_map<unichar, UCKeyCode> MacOSEventBridge::s_characterToKeyCode;
std::unordered_map<NSEventModifierFlags, UCModifierFlags> MacOSEventBridge::s_nsModifierToUCModifier;
std::unordered_map<UCModifierFlags, NSEventModifierFlags> MacOSEventBridge::s_ucModifierToNSModifier;
std::unordered_map<NSEventType, UCEventType> MacOSEventBridge::s_nsEventTypeToUCEventType;

// ===== MAIN CONVERSION FUNCTION =====
UCEvent MacOSEventBridge::ConvertNSEventToUCEvent(NSEvent* nsEvent, void* targetWindow) {
    EnsureInitialized();
    
    if (!nsEvent) {
        return UCEvent{}; // Return empty event
    }
    
    NSEventType eventType = [nsEvent type];
    UCEvent ucEvent;
    
    // Set common properties
    SetCommonEventProperties(ucEvent, nsEvent, targetWindow);
    
    // Route to specific converters
    switch (eventType) {
        case NSEventTypeKeyDown:
        case NSEventTypeKeyUp:
        case NSEventTypeFlagsChanged:
            return ConvertKeyboardEvent(nsEvent, targetWindow);
            
        case NSEventTypeLeftMouseDown:
        case NSEventTypeLeftMouseUp:
        case NSEventTypeRightMouseDown:
        case NSEventTypeRightMouseUp:
        case NSEventTypeOtherMouseDown:
        case NSEventTypeOtherMouseUp:
        case NSEventTypeLeftMouseDragged:
        case NSEventTypeRightMouseDragged:
        case NSEventTypeOtherMouseDragged:
        case NSEventTypeMouseMoved:
        case NSEventTypeMouseEntered:
        case NSEventTypeMouseExited:
            return ConvertMouseEvent(nsEvent, targetWindow);
            
        case NSEventTypeScrollWheel:
            return ConvertScrollEvent(nsEvent, targetWindow);
            
        default:
            return ConvertWindowEvent(nsEvent, targetWindow);
    }
}

// ===== SPECIFIC EVENT CONVERTERS =====
UCEvent MacOSEventBridge::ConvertMouseEvent(NSEvent* nsEvent, void* targetWindow) {
    UCEvent ucEvent;
    NSEventType eventType = [nsEvent type];
    
    // Set common properties
    SetCommonEventProperties(ucEvent, nsEvent, targetWindow);
    
    // Set event type
    switch (eventType) {
        case NSEventTypeLeftMouseDown:
        case NSEventTypeRightMouseDown:
        case NSEventTypeOtherMouseDown:
            ucEvent.type = UCEventType::MouseButtonPressed;
            break;
            
        case NSEventTypeLeftMouseUp:
        case NSEventTypeRightMouseUp:
        case NSEventTypeOtherMouseUp:
            ucEvent.type = UCEventType::MouseButtonReleased;
            break;
            
        case NSEventTypeLeftMouseDragged:
        case NSEventTypeRightMouseDragged:
        case NSEventTypeOtherMouseDragged:
        case NSEventTypeMouseMoved:
            ucEvent.type = UCEventType::MouseMoved;
            break;
            
        case NSEventTypeMouseEntered:
            ucEvent.type = UCEventType::MouseEntered;
            break;
            
        case NSEventTypeMouseExited:
            ucEvent.type = UCEventType::MouseExited;
            break;
            
        default:
            ucEvent.type = UCEventType::Unknown;
            break;
    }
    
    // Set mouse properties
    ucEvent.mouse.button = ConvertNSEventMouseButton(nsEvent);
    ucEvent.mouse.position = ConvertNSEventLocationToUCPoint(nsEvent, targetWindow);
    ucEvent.mouse.clickCount = [nsEvent clickCount];
    
    // Set modifier flags
    ucEvent.modifiers = ConvertNSEventModifierFlags([nsEvent modifierFlags]);
    
    return ucEvent;
}

UCEvent MacOSEventBridge::ConvertKeyboardEvent(NSEvent* nsEvent, void* targetWindow) {
    UCEvent ucEvent;
    NSEventType eventType = [nsEvent type];
    
    // Set common properties
    SetCommonEventProperties(ucEvent, nsEvent, targetWindow);
    
    // Set event type
    switch (eventType) {
        case NSEventTypeKeyDown:
            ucEvent.type = UCEventType::KeyPressed;
            break;
        case NSEventTypeKeyUp:
            ucEvent.type = UCEventType::KeyReleased;
            break;
        case NSEventTypeFlagsChanged:
            ucEvent.type = UCEventType::ModifierChanged;
            break;
        default:
            ucEvent.type = UCEventType::Unknown;
            break;
    }
    
    if (eventType == NSEventTypeKeyDown || eventType == NSEventTypeKeyUp) {
        // Set key properties
        ucEvent.key.keyCode = ConvertNSEventKeyCode([nsEvent keyCode]);
        
        // Get characters
        NSString* characters = [nsEvent characters];
        if (characters && [characters length] > 0) {
            std::string keyText = [characters UTF8String];
            ucEvent.key.text = keyText;
            
            if (keyText.length() == 1) {
                ucEvent.key.character = static_cast<uint32_t>(keyText[0]);
            }
        }
        
        // Check for special keys
        if (IsSpecialKey([nsEvent keyCode])) {
            ucEvent.key.isSpecialKey = true;
        }
        
        ucEvent.key.isRepeat = [nsEvent isARepeat];
    }
    
    // Set modifier flags
    ucEvent.modifiers = ConvertNSEventModifierFlags([nsEvent modifierFlags]);
    
    return ucEvent;
}

UCEvent MacOSEventBridge::ConvertScrollEvent(NSEvent* nsEvent, void* targetWindow) {
    UCEvent ucEvent;
    
    // Set common properties
    SetCommonEventProperties(ucEvent, nsEvent, targetWindow);
    ucEvent.type = UCEventType::MouseScrolled;
    
    // Set scroll properties
    ucEvent.scroll.deltaX = static_cast<float>([nsEvent scrollingDeltaX]);
    ucEvent.scroll.deltaY = static_cast<float>([nsEvent scrollingDeltaY]);
    ucEvent.scroll.position = ConvertNSEventLocationToUCPoint(nsEvent, targetWindow);
    
    // Check scroll type (pixel-based vs line-based)
    if ([nsEvent hasPreciseScrollingDeltas]) {
        ucEvent.scroll.isPixelBased = true;
    } else {
        ucEvent.scroll.isPixelBased = false;
        // Convert line-based scrolling to approximate pixel values
        ucEvent.scroll.deltaX *= 10.0f; // Approximate line height
        ucEvent.scroll.deltaY *= 10.0f;
    }
    
    // Set modifier flags
    ucEvent.modifiers = ConvertNSEventModifierFlags([nsEvent modifierFlags]);
    
    return ucEvent;
}

UCEvent MacOSEventBridge::ConvertWindowEvent(NSEvent* nsEvent, void* targetWindow) {
    UCEvent ucEvent;
    
    // Set common properties
    SetCommonEventProperties(ucEvent, nsEvent, targetWindow);
    
    // Most window events are handled directly by the window
    // This is mainly for application-level events
    ucEvent.type = UCEventType::Unknown;
    
    return ucEvent;
}

// ===== KEY CODE CONVERSION =====
UCKeyCode MacOSEventBridge::ConvertNSEventKeyCode(unsigned short keyCode) {
    EnsureInitialized();
    
    auto it = s_nsKeyCodeToUCKeyCode.find(keyCode);
    if (it != s_nsKeyCodeToUCKeyCode.end()) {
        return it->second;
    }
    
    // Handle special keys
    if (IsSpecialKey(keyCode)) {
        return HandleSpecialKey(keyCode);
    }
    
    return UCKeyCode::Unknown;
}

UCKeyCode MacOSEventBridge::ConvertCharacterToKeyCode(unichar character) {
    EnsureInitialized();
    
    auto it = s_characterToKeyCode.find(character);
    if (it != s_characterToKeyCode.end()) {
        return it->second;
    }
    
    // Handle ASCII range
    if (character >= 'A' && character <= 'Z') {
        return static_cast<UCKeyCode>(static_cast<int>(UCKeyCode::A) + (character - 'A'));
    }
    
    if (character >= 'a' && character <= 'z') {
        return static_cast<UCKeyCode>(static_cast<int>(UCKeyCode::A) + (character - 'a'));
    }
    
    if (character >= '0' && character <= '9') {
        return static_cast<UCKeyCode>(static_cast<int>(UCKeyCode::Num0) + (character - '0'));
    }
    
    return UCKeyCode::Unknown;
}

unsigned short MacOSEventBridge::ConvertUCKeyCodeToNSEventKeyCode(UCKeyCode keyCode) {
    EnsureInitialized();
    
    auto it = s_ucKeyCodeToNSKeyCode.find(keyCode);
    if (it != s_ucKeyCodeToNSKeyCode.end()) {
        return it->second;
    }
    
    return 0; // Invalid key code
}

// ===== MODIFIER FLAGS CONVERSION =====
UCModifierFlags MacOSEventBridge::ConvertNSEventModifierFlags(NSEventModifierFlags modifierFlags) {
    UCModifierFlags ucFlags = UCModifierFlags::None;
    
    if (modifierFlags & NSEventModifierFlagShift) {
        ucFlags |= UCModifierFlags::Shift;
    }
    if (modifierFlags & NSEventModifierFlagControl) {
        ucFlags |= UCModifierFlags::Control;
    }
    if (modifierFlags & NSEventModifierFlagOption) {
        ucFlags |= UCModifierFlags::Alt;
    }
    if (modifierFlags & NSEventModifierFlagCommand) {
        ucFlags |= UCModifierFlags::Meta;
    }
    if (modifierFlags & NSEventModifierFlagCapsLock) {
        ucFlags |= UCModifierFlags::CapsLock;
    }
    if (modifierFlags & NSEventModifierFlagFunction) {
        ucFlags |= UCModifierFlags::Function;
    }
    
    return ucFlags;
}

NSEventModifierFlags MacOSEventBridge::ConvertUCModifierFlags(UCModifierFlags modifierFlags) {
    NSEventModifierFlags nsFlags = 0;
    
    if (modifierFlags & UCModifierFlags::Shift) {
        nsFlags |= NSEventModifierFlagShift;
    }
    if (modifierFlags & UCModifierFlags::Control) {
        nsFlags |= NSEventModifierFlagControl;
    }
    if (modifierFlags & UCModifierFlags::Alt) {
        nsFlags |= NSEventModifierFlagOption;
    }
    if (modifierFlags & UCModifierFlags::Meta) {
        nsFlags |= NSEventModifierFlagCommand;
    }
    if (modifierFlags & UCModifierFlags::CapsLock) {
        nsFlags |= NSEventModifierFlagCapsLock;
    }
    if (modifierFlags & UCModifierFlags::Function) {
        nsFlags |= NSEventModifierFlagFunction;
    }
    
    return nsFlags;
}

// ===== MOUSE BUTTON CONVERSION =====
UCMouseButton MacOSEventBridge::ConvertNSEventMouseButton(NSEvent* nsEvent) {
    NSEventType eventType = [nsEvent type];
    NSInteger buttonNumber = [nsEvent buttonNumber];
    
    switch (eventType) {
        case NSEventTypeLeftMouseDown:
        case NSEventTypeLeftMouseUp:
        case NSEventTypeLeftMouseDragged:
            return UCMouseButton::Left;
            
        case NSEventTypeRightMouseDown:
        case NSEventTypeRightMouseUp:
        case NSEventTypeRightMouseDragged:
            return UCMouseButton::Right;
            
        case NSEventTypeOtherMouseDown:
        case NSEventTypeOtherMouseUp:
        case NSEventTypeOtherMouseDragged:
            switch (buttonNumber) {
                case 2: return UCMouseButton::Middle;
                case 3: return UCMouseButton::X1;
                case 4: return UCMouseButton::X2;
                default: return UCMouseButton::Unknown;
            }
            
        default:
            return UCMouseButton::None;
    }
}

int MacOSEventBridge::ConvertUCMouseButtonToNSEventButton(UCMouseButton button) {
    switch (button) {
        case UCMouseButton::Left: return 0;
        case UCMouseButton::Right: return 1;
        case UCMouseButton::Middle: return 2;
        case UCMouseButton::X1: return 3;
        case UCMouseButton::X2: return 4;
        default: return -1;
    }
}

// ===== COORDINATE CONVERSION =====
Point2D MacOSEventBridge::ConvertNSEventLocationToUCPoint(NSEvent* nsEvent, void* targetWindow) {
    NSPoint locationInWindow = [nsEvent locationInWindow];
    
    if (targetWindow) {
        UltraCanvasMacOSWindow* ucWindow = static_cast<UltraCanvasMacOSWindow*>(targetWindow);
        NSWindow* nsWindow = ucWindow->GetNSWindow();
        
        if (nsWindow) {
            // Convert from window coordinates to UltraCanvas coordinates
            NSRect windowFrame = [nsWindow frame];
            float windowHeight = windowFrame.size.height;
            
            // Flip Y coordinate (Cocoa origin is bottom-left, UltraCanvas is top-left)
            return Point2D(locationInWindow.x, windowHeight - locationInWindow.y);
        }
    }
    
    return Point2D(locationInWindow.x, locationInWindow.y);
}

Point2D MacOSEventBridge::ConvertCocoaCoordinates(float x, float y, float windowHeight) {
    return Point2D(x, windowHeight - y);
}

// ===== STRING CONVERSION =====
std::string MacOSEventBridge::ConvertNSStringToStdString(void* nsString) {
    if (!nsString) return "";
    
    NSString* string = (NSString*)nsString;
    const char* cString = [string UTF8String];
    return cString ? std::string(cString) : std::string();
}

void* MacOSEventBridge::ConvertStdStringToNSString(const std::string& str) {
    return [NSString stringWithUTF8String:str.c_str()];
}

// ===== EVENT TYPE MAPPING =====
UCEventType MacOSEventBridge::GetUCEventTypeFromNSEventType(NSEventType eventType) {
    EnsureInitialized();
    
    auto it = s_nsEventTypeToUCEventType.find(eventType);
    if (it != s_nsEventTypeToUCEventType.end()) {
        return it->second;
    }
    
    return UCEventType::Unknown;
}

// ===== INITIALIZATION =====
void MacOSEventBridge::EnsureInitialized() {
    if (!s_initialized) {
        InitializeKeyCodeMappings();
        InitializeModifierMappings();
        InitializeEventTypeMappings();
        s_initialized = true;
        
        std::cout << "UltraCanvas macOS: Event bridge initialized" << std::endl;
    }
}

void MacOSEventBridge::InitializeKeyCodeMappings() {
    using namespace MacOSKeyCodes;
    
    // Letter keys
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_A] = UCKeyCode::A;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_B] = UCKeyCode::B;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_C] = UCKeyCode::C;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_D] = UCKeyCode::D;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_E] = UCKeyCode::E;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_F] = UCKeyCode::F;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_G] = UCKeyCode::G;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_H] = UCKeyCode::H;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_I] = UCKeyCode::I;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_J] = UCKeyCode::J;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_K] = UCKeyCode::K;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_L] = UCKeyCode::L;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_M] = UCKeyCode::M;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_N] = UCKeyCode::N;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_O] = UCKeyCode::O;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_P] = UCKeyCode::P;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_Q] = UCKeyCode::Q;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_R] = UCKeyCode::R;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_S] = UCKeyCode::S;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_T] = UCKeyCode::T;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_U] = UCKeyCode::U;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_V] = UCKeyCode::V;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_W] = UCKeyCode::W;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_X] = UCKeyCode::X;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_Y] = UCKeyCode::Y;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_Z] = UCKeyCode::Z;
    
    // Number keys
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_0] = UCKeyCode::Num0;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_1] = UCKeyCode::Num1;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_2] = UCKeyCode::Num2;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_3] = UCKeyCode::Num3;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_4] = UCKeyCode::Num4;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_5] = UCKeyCode::Num5;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_6] = UCKeyCode::Num6;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_7] = UCKeyCode::Num7;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_8] = UCKeyCode::Num8;
    s_nsKeyCodeToUCKeyCode[kVK_ANSI_9] = UCKeyCode::Num9;
    
    // Function keys
    s_nsKeyCodeToUCKeyCode[kVK_F1] = UCKeyCode::F1;
    s_nsKeyCodeToUCKeyCode[kVK_F2] = UCKeyCode::F2;
    s_nsKeyCodeToUCKeyCode[kVK_F3] = UCKeyCode::F3;
    s_nsKeyCodeToUCKeyCode[kVK_F4] = UCKeyCode::F4;
    s_nsKeyCodeToUCKeyCode[kVK_F5] = UCKeyCode::F5;
    s_nsKeyCodeToUCKeyCode[kVK_F6] = UCKeyCode::F6;
    s_nsKeyCodeToUCKeyCode[kVK_F7] = UCKeyCode::F7;
    s_nsKeyCodeToUCKeyCode[kVK_F8] = UCKeyCode::F8;
    s_nsKeyCodeToUCKeyCode[kVK_F9] = UCKeyCode::F9;
    s_nsKeyCodeToUCKeyCode[kVK_F10] = UCKeyCode::F10;
    s_nsKeyCodeToUCKeyCode[kVK_F11] = UCKeyCode::F11;
    s_nsKeyCodeToUCKeyCode[kVK_F12] = UCKeyCode::F12;
    
    // Special keys
    s_nsKeyCodeToUCKeyCode[kVK_Return] = UCKeyCode::Return;
    s_nsKeyCodeToUCKeyCode[kVK_Tab] = UCKeyCode::Tab;
    s_nsKeyCodeToUCKeyCode[kVK_Space] = UCKeyCode::Space;
    s_nsKeyCodeToUCKeyCode[kVK_Delete] = UCKeyCode::Backspace;
    s_nsKeyCodeToUCKeyCode[kVK_ForwardDelete] = UCKeyCode::Delete;
    s_nsKeyCodeToUCKeyCode[kVK_Escape] = UCKeyCode::Escape;
    s_nsKeyCodeToUCKeyCode[kVK_Command] = UCKeyCode::LeftMeta;
    s_nsKeyCodeToUCKeyCode[kVK_Shift] = UCKeyCode::LeftShift;
    s_nsKeyCodeToUCKeyCode[kVK_RightShift] = UCKeyCode::RightShift;
    s_nsKeyCodeToUCKeyCode[kVK_CapsLock] = UCKeyCode::CapsLock;
    s_nsKeyCodeToUCKeyCode[kVK_Option] = UCKeyCode::LeftAlt;
    s_nsKeyCodeToUCKeyCode[kVK_RightOption] = UCKeyCode::RightAlt;
    s_nsKeyCodeToUCKeyCode[kVK_Control] = UCKeyCode::LeftControl;
    s_nsKeyCodeToUCKeyCode[kVK_RightControl] = UCKeyCode::RightControl;
    s_nsKeyCodeToUCKeyCode[kVK_Function] = UCKeyCode::Function;
    
    // Arrow keys
    s_nsKeyCodeToUCKeyCode[kVK_LeftArrow] = UCKeyCode::Left;
    s_nsKeyCodeToUCKeyCode[kVK_RightArrow] = UCKeyCode::Right;
    s_nsKeyCodeToUCKeyCode[kVK_UpArrow] = UCKeyCode::Up;
    s_nsKeyCodeToUCKeyCode[kVK_DownArrow] = UCKeyCode::Down;
    
    // Navigation keys
    s_nsKeyCodeToUCKeyCode[kVK_Home] = UCKeyCode::Home;
    s_nsKeyCodeToUCKeyCode[kVK_End] = UCKeyCode::End;
    s_nsKeyCodeToUCKeyCode[kVK_PageUp] = UCKeyCode::PageUp;
    s_nsKeyCodeToUCKeyCode[kVK_PageDown] = UCKeyCode::PageDown;
    
    // Create reverse mapping
    for (const auto& pair : s_nsKeyCodeToUCKeyCode) {
        s_ucKeyCodeToNSKeyCode[pair.second] = pair.first;
    }
    
    // Character mappings
    for (char c = 'A'; c <= 'Z'; ++c) {
        UCKeyCode keyCode = static_cast<UCKeyCode>(static_cast<int>(UCKeyCode::A) + (c - 'A'));
        s_characterToKeyCode[c] = keyCode;
        s_characterToKeyCode[c + ('a' - 'A')] = keyCode; // lowercase
    }
    
    for (char c = '0'; c <= '9'; ++c) {
        UCKeyCode keyCode = static_cast<UCKeyCode>(static_cast<int>(UCKeyCode::Num0) + (c - '0'));
        s_characterToKeyCode[c] = keyCode;
    }
}

void MacOSEventBridge::InitializeModifierMappings() {
    // NSEventModifierFlags to UCModifierFlags
    s_nsModifierToUCModifier[NSEventModifierFlagShift] = UCModifierFlags::Shift;
    s_nsModifierToUCModifier[NSEventModifierFlagControl] = UCModifierFlags::Control;
    s_nsModifierToUCModifier[NSEventModifierFlagOption] = UCModifierFlags::Alt;
    s_nsModifierToUCModifier[NSEventModifierFlagCommand] = UCModifierFlags::Meta;
    s_nsModifierToUCModifier[NSEventModifierFlagCapsLock] = UCModifierFlags::CapsLock;
    s_nsModifierToUCModifier[NSEventModifierFlagFunction] = UCModifierFlags::Function;
    
    // Create reverse mapping
    for (const auto& pair : s_nsModifierToUCModifier) {
        s_ucModifierToNSModifier[pair.second] = pair.first;
    }
}

void MacOSEventBridge::InitializeEventTypeMappings() {
    s_nsEventTypeToUCEventType[NSEventTypeKeyDown] = UCEventType::KeyPressed;
    s_nsEventTypeToUCEventType[NSEventTypeKeyUp] = UCEventType::KeyReleased;
    s_nsEventTypeToUCEventType[NSEventTypeFlagsChanged] = UCEventType::ModifierChanged;
    
    s_nsEventTypeToUCEventType[NSEventTypeLeftMouseDown] = UCEventType::MouseButtonPressed;
    s_nsEventTypeToUCEventType[NSEventTypeLeftMouseUp] = UCEventType::MouseButtonReleased;
    s_nsEventTypeToUCEventType[NSEventTypeRightMouseDown] = UCEventType::MouseButtonPressed;
    s_nsEventTypeToUCEventType[NSEventTypeRightMouseUp] = UCEventType::MouseButtonReleased;
    s_nsEventTypeToUCEventType[NSEventTypeOtherMouseDown] = UCEventType::MouseButtonPressed;
    s_nsEventTypeToUCEventType[NSEventTypeOtherMouseUp] = UCEventType::MouseButtonReleased;
    
    s_nsEventTypeToUCEventType[NSEventTypeMouseMoved] = UCEventType::MouseMoved;
    s_nsEventTypeToUCEventType[NSEventTypeLeftMouseDragged] = UCEventType::MouseMoved;
    s_nsEventTypeToUCEventType[NSEventTypeRightMouseDragged] = UCEventType::MouseMoved;
    s_nsEventTypeToUCEventType[NSEventTypeOtherMouseDragged] = UCEventType::MouseMoved;
    
    s_nsEventTypeToUCEventType[NSEventTypeMouseEntered] = UCEventType::MouseEntered;
    s_nsEventTypeToUCEventType[NSEventTypeMouseExited] = UCEventType::MouseExited;
    s_nsEventTypeToUCEventType[NSEventTypeScrollWheel] = UCEventType::MouseScrolled;
}

// ===== HELPER METHODS =====
void MacOSEventBridge::SetCommonEventProperties(UCEvent& ucEvent, NSEvent* nsEvent, void* targetWindow) {
    ucEvent.timestamp = ConvertNSEventTimestamp(nsEvent);
    ucEvent.targetWindow = targetWindow;
    ucEvent.nativeWindowHandle = reinterpret_cast<unsigned long>([nsEvent window]);
}

std::chrono::steady_clock::time_point MacOSEventBridge::ConvertNSEventTimestamp(NSEvent* nsEvent) {
    NSTimeInterval timestamp = [nsEvent timestamp];
    
    // Convert from NSTimeInterval (seconds since system boot) to steady_clock
    auto now = std::chrono::steady_clock::now();
    auto bootTime = now - std::chrono::duration<double>(timestamp);
    return bootTime + std::chrono::duration<double>(timestamp);
}

bool MacOSEventBridge::IsSpecialKey(unsigned short keyCode) {
    using namespace MacOSKeyCodes;
    
    return (keyCode == kVK_Return || keyCode == kVK_Tab || keyCode == kVK_Space ||
            keyCode == kVK_Delete || keyCode == kVK_Escape || keyCode == kVK_ForwardDelete ||
            keyCode == kVK_Home || keyCode == kVK_End || keyCode == kVK_PageUp || keyCode == kVK_PageDown ||
            keyCode == kVK_LeftArrow || keyCode == kVK_RightArrow || keyCode == kVK_UpArrow || keyCode == kVK_DownArrow ||
            (keyCode >= kVK_F1 && keyCode <= kVK_F20));
}

UCKeyCode MacOSEventBridge::HandleSpecialKey(unsigned short keyCode) {
    using namespace MacOSKeyCodes;
    
    switch (keyCode) {
        case kVK_Return: return UCKeyCode::Return;
        case kVK_Tab: return UCKeyCode::Tab;
        case kVK_Space: return UCKeyCode::Space;
        case kVK_Delete: return UCKeyCode::Backspace;
        case kVK_ForwardDelete: return UCKeyCode::Delete;
        case kVK_Escape: return UCKeyCode::Escape;
        case kVK_Home: return UCKeyCode::Home;
        case kVK_End: return UCKeyCode::End;
        case kVK_PageUp: return UCKeyCode::PageUp;
        case kVK_PageDown: return UCKeyCode::PageDown;
        case kVK_LeftArrow: return UCKeyCode::Left;
        case kVK_RightArrow: return UCKeyCode::Right;
        case kVK_UpArrow: return UCKeyCode::Up;
        case kVK_DownArrow: return UCKeyCode::Down;
        default: return UCKeyCode::Unknown;
    }
}

bool MacOSEventBridge::IsMouseEvent(NSEventType eventType) {
    return (eventType >= NSEventTypeLeftMouseDown && eventType <= NSEventTypeOtherMouseDragged) ||
           eventType == NSEventTypeMouseMoved || eventType == NSEventTypeMouseEntered || 
           eventType == NSEventTypeMouseExited;
}

bool MacOSEventBridge::IsKeyboardEvent(NSEventType eventType) {
    return eventType == NSEventTypeKeyDown || eventType == NSEventTypeKeyUp || 
           eventType == NSEventTypeFlagsChanged;
}

bool MacOSEventBridge::IsScrollEvent(NSEventType eventType) {
    return eventType == NSEventTypeScrollWheel;
}

bool MacOSEventBridge::IsWindowEvent(NSEventType eventType) {
    return !IsMouseEvent(eventType) && !IsKeyboardEvent(eventType) && !IsScrollEvent(eventType);
}

float MacOSEventBridge::GetWindowHeight(void* targetWindow) {
    if (!targetWindow) return 0.0f;
    
    UltraCanvasMacOSWindow* ucWindow = static_cast<UltraCanvasMacOSWindow*>(targetWindow);
    NSWindow* nsWindow = ucWindow->GetNSWindow();
    
    if (nsWindow) {
        NSRect frame = [nsWindow frame];
        return frame.size.height;
    }
    
    return 0.0f;
}

Point2D MacOSEventBridge::AdjustForWindowCoordinates(const Point2D& point, void* targetWindow) {
    if (!targetWindow) return point;
    
    float windowHeight = GetWindowHeight(targetWindow);
    return ConvertCocoaCoordinates(point.x, point.y, windowHeight);
}

} // namespace UltraCanvas

// ===== UTILITY FUNCTIONS IMPLEMENTATION =====
namespace UltraCanvas {
namespace MacOSEventUtils {

Point2D NSPointToUCPoint(void* nsPoint) {
    if (!nsPoint) return Point2D(0, 0);
    
    NSPoint* point = static_cast<NSPoint*>(nsPoint);
    return Point2D(point->x, point->y);
}

void* UCPointToNSPoint(const Point2D& point) {
    NSPoint* nsPoint = new NSPoint;
    nsPoint->x = point.x;
    nsPoint->y = point.y;
    return nsPoint;
}

Size2D NSSizeToUCSize(void* nsSize) {
    if (!nsSize) return Size2D(0, 0);
    
    NSSize* size = static_cast<NSSize*>(nsSize);
    return Size2D(size->width, size->height);
}

void* UCSizeToNSSize(const Size2D& size) {
    NSSize* nsSize = new NSSize;
    nsSize->width = size.width;
    nsSize->height = size.height;
    return nsSize;
}

Rect2D NSRectToUCRect(void* nsRect) {
    if (!nsRect) return Rect2D(0, 0, 0, 0);
    
    NSRect* rect = static_cast<NSRect*>(nsRect);
    return Rect2D(rect->origin.x, rect->origin.y, rect->size.width, rect->size.height);
}

void* UCRectToNSRect(const Rect2D& rect) {
    NSRect* nsRect = new NSRect;
    nsRect->origin.x = rect.x;
    nsRect->origin.y = rect.y;
    nsRect->size.width = rect.width;
    nsRect->size.height = rect.height;
    return nsRect;
}

std::chrono::steady_clock::time_point GetCurrentEventTime() {
    return std::chrono::steady_clock::now();
}

double GetEventDeltaTime(NSEvent* event1, NSEvent* event2) {
    if (!event1 || !event2) return 0.0;
    
    NSTimeInterval time1 = [event1 timestamp];
    NSTimeInterval time2 = [event2 timestamp];
    
    return std::abs(time2 - time1);
}

bool IsCommandKeyPressed(NSEventModifierFlags flags) {
    return (flags & NSEventModifierFlagCommand) != 0;
}

bool IsOptionKeyPressed(NSEventModifierFlags flags) {
    return (flags & NSEventModifierFlagOption) != 0;
}

bool IsControlKeyPressed(NSEventModifierFlags flags) {
    return (flags & NSEventModifierFlagControl) != 0;
}

bool IsShiftKeyPressed(NSEventModifierFlags flags) {
    return (flags & NSEventModifierFlagShift) != 0;
}

bool IsFunctionKeyPressed(NSEventModifierFlags flags) {
    return (flags & NSEventModifierFlagFunction) != 0;
}

} // namespace MacOSEventUtils
} // namespace UltraCanvas