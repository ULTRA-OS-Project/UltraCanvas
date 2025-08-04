// UltraCanvasEventDispatcher.h
// Event dispatching and focus management system - unified header-only version
// Version: 2.1.0
// Last Modified: 2024-12-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasEvent.h"        // MUST be first - provides UCEvent definition
#include "UltraCanvasCommonTypes.h"  // Then common types
#include "UltraCanvasUIElement.h"    // Then UI elements
#include <vector>
#include <algorithm>
#include <functional>
#include <iostream>
#include <chrono>

namespace UltraCanvas {

// Forward declaration
    class UltraCanvasElement;

// ===== EVENT DISPATCHER CLASS =====
    class UltraCanvasEventDispatcher {
    private:
        // State tracking
        static UltraCanvasElement* focusedElement;
        static UltraCanvasElement* hoveredElement;
        static UltraCanvasElement* capturedElement;
        static UltraCanvasElement* draggedElement;

        // Double-click detection
        static UCEvent lastMouseEvent;
        static std::chrono::steady_clock::time_point lastClickTime;
        static const float DOUBLE_CLICK_TIME;
        static const int DOUBLE_CLICK_DISTANCE;

        // Keyboard state
        static bool keyStates[256];
        static bool shiftHeld;
        static bool ctrlHeld;
        static bool altHeld;
        static bool metaHeld;

        // Global event handlers
        static std::vector<std::function<bool(const UCEvent&)>> globalEventHandlers;

    public:
        // ===== MAIN EVENT DISPATCH =====
        static bool DispatchEvent(const UCEvent& event, std::vector<UltraCanvasElement*>& elements);

        // ===== SIMPLIFIED DISPATCH FOR COMPATIBILITY =====
        static void DispatchEventToElements(const UCEvent& event, std::vector<UltraCanvasElement*>& elements) {
            DispatchEvent(event, elements);
        }

        // ===== FOCUS MANAGEMENT =====
        static void SetFocusedElement(UltraCanvasElement* element);

        static UltraCanvasElement* GetFocusedElement() {
            return focusedElement;
        }

        static UltraCanvasElement* GetHoveredElement() {
            return hoveredElement;
        }

        static UltraCanvasElement* GetCapturedElement() {
            return capturedElement;
        }

        // ===== FOCUS NAVIGATION =====
        static void FocusNextElement(std::vector<UltraCanvasElement*>& elements, bool reverse = false);

        static void FocusPreviousElement(std::vector<UltraCanvasElement*>& elements) {
            FocusNextElement(elements, true);
        }

        // ===== GLOBAL EVENT HANDLERS =====
        static void RegisterGlobalEventHandler(std::function<bool(const UCEvent&)> handler);

        static void ClearGlobalEventHandlers() {
            globalEventHandlers.clear();
        }

        // ===== KEYBOARD STATE QUERIES =====
        static bool IsKeyPressed(int keyCode);

        static bool IsShiftHeld() { return shiftHeld; }
        static bool IsCtrlHeld() { return ctrlHeld; }
        static bool IsAltHeld() { return altHeld; }
        static bool IsMetaHeld() { return metaHeld; }

        // ===== MOUSE CAPTURE =====
        static void CaptureMouse(UltraCanvasElement* element) {
            capturedElement = element;
        }

        static void ReleaseMouse() {
            capturedElement = nullptr;
        }

        // ===== RESET STATE =====
        static void Reset();

    private:
        // ===== PRIVATE EVENT HANDLERS =====

        static bool HandleMouseDown(const UCEvent& event, std::vector<UltraCanvasElement*>& elements);

        static bool HandleMouseUp(const UCEvent& event, std::vector<UltraCanvasElement*>& elements);

        static bool HandleMouseMove(const UCEvent& event, std::vector<UltraCanvasElement*>& elements);

        static bool HandleMouseDoubleClick(const UCEvent& event, std::vector<UltraCanvasElement*>& elements);

        static bool HandleMouseWheel(const UCEvent& event, std::vector<UltraCanvasElement*>& elements);

        static bool HandleKeyboardEvent(const UCEvent& event, std::vector<UltraCanvasElement*>& elements);

        // ===== UTILITY FUNCTIONS =====

        static UltraCanvasElement* FindElementAtPoint(int x, int y, std::vector<UltraCanvasElement*>& elements);

        static void UpdateModifierStates(const UCEvent& event);

        static bool IsDoubleClick(const UCEvent& event);
    };

// ===== STANDALONE CONVENIENCE FUNCTIONS =====

// Main event dispatch function
    inline void DispatchEventToElements(const UCEvent& event, std::vector<UltraCanvasElement*>& elements) {
        UltraCanvasEventDispatcher::DispatchEventToElements(event, elements);
    }

// Focus management functions
    inline void FocusNextElement(std::vector<UltraCanvasElement*>& elements, bool reverse = false) {
        UltraCanvasEventDispatcher::FocusNextElement(elements, reverse);
    }

    inline void FocusPreviousElement(std::vector<UltraCanvasElement*>& elements) {
        UltraCanvasEventDispatcher::FocusPreviousElement(elements);
    }

// State query functions
    inline UltraCanvasElement* GetFocusedElement() {
        return UltraCanvasEventDispatcher::GetFocusedElement();
    }

    inline UltraCanvasElement* GetHoveredElement() {
        return UltraCanvasEventDispatcher::GetHoveredElement();
    }

    inline void SetGlobalFocus(UltraCanvasElement* element) {
        UltraCanvasEventDispatcher::SetFocusedElement(element);
    }

    inline void ClearGlobalFocus() {
        UltraCanvasEventDispatcher::SetFocusedElement(nullptr);
    }

// Keyboard state queries
    inline bool IsKeyPressed(int keyCode) {
        return UltraCanvasEventDispatcher::IsKeyPressed(keyCode);
    }

    inline bool IsShiftHeld() {
        return UltraCanvasEventDispatcher::IsShiftHeld();
    }

    inline bool IsCtrlHeld() {
        return UltraCanvasEventDispatcher::IsCtrlHeld();
    }

    inline bool IsAltHeld() {
        return UltraCanvasEventDispatcher::IsAltHeld();
    }

    inline bool IsMetaHeld() {
        return UltraCanvasEventDispatcher::IsMetaHeld();
    }

// Global event handler management
    inline void RegisterGlobalEventHandler(std::function<bool(const UCEvent&)> handler) {
        UltraCanvasEventDispatcher::RegisterGlobalEventHandler(handler);
    }

    inline void ClearGlobalEventHandlers() {
        UltraCanvasEventDispatcher::ClearGlobalEventHandlers();
    }

// Mouse capture functions
    inline void CaptureMouse(UltraCanvasElement* element) {
        UltraCanvasEventDispatcher::CaptureMouse(element);
    }

    inline void ReleaseMouse() {
        UltraCanvasEventDispatcher::ReleaseMouse();
    }

// ===== DEBUG UTILITIES =====
    inline void PrintEventInfo(const UCEvent& event) {
        std::cout << "Event: type=" << static_cast<int>(event.type)
                  << " pos=(" << event.x << "," << event.y << ")"
                  << " key=" << event.keyCode
                  << " modifiers=[" << (event.ctrl ? "C" : "")
                  << (event.shift ? "S" : "") << (event.alt ? "A" : "") << (event.meta ? "M" : "") << "]"
                  << std::endl;
    }

    inline void PrintFocusInfo() {
        auto* focused = GetFocusedElement();
        auto* hovered = GetHoveredElement();

        std::cout << "Focus: " << (focused ? focused->GetIdentifier() : "none")
                  << ", Hover: " << (hovered ? hovered->GetIdentifier() : "none") << std::endl;
    }

    inline void ResetEventDispatcher() {
        UltraCanvasEventDispatcher::Reset();
    }

} // namespace UltraCanvas