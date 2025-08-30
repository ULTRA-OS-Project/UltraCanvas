// UltraCanvasKeyboardManager.h
// Enhanced keyboard input management system for UltraCanvas Framework
// Version: 1.0.1
// Last Modified: 2025-01-07
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <vector>
#include <chrono>
#include <memory>

namespace UltraCanvas {

// ===== KEYBOARD STATE MANAGEMENT =====
    enum class KeyState {
        Released = 0,
        Pressed = 1,
        Repeat = 2,
        JustPressed = 3,
        JustReleased = 4
    };

    enum class ModifierKeys {
        NoModifier = 0,
        Shift = 1,
        Ctrl = 2,
        Alt = 4,
        Meta = 8,
        CapsLock = 16,
        NumLock = 32,
        ScrollLock = 64
    };

    struct KeyboardState {
        std::unordered_set<int> pressedKeys;
        std::unordered_set<int> justPressedKeys;
        std::unordered_set<int> justReleasedKeys;
        std::unordered_map<int, std::chrono::steady_clock::time_point> keyPressTimes;
        std::unordered_map<int, int> keyRepeatCounts;
        int modifierFlags = 0;
        bool anyKeyPressed = false;

        void Clear() {
            pressedKeys.clear();
            justPressedKeys.clear();
            justReleasedKeys.clear();
            keyPressTimes.clear();
            keyRepeatCounts.clear();
            modifierFlags = 0;
            anyKeyPressed = false;
        }
    };

    struct KeyboardEvent {
        int keyCode = 0;
        UCKeys virtualKey = UCKeys::Unknown;
        char character = 0;
        std::string text;
        KeyState state = KeyState::Released;
        int modifierFlags = 0;
        bool isRepeat = false;
        int repeatCount = 0;
        std::chrono::steady_clock::time_point timestamp;

        bool HasModifier(ModifierKeys modifier) const {
            return (modifierFlags & static_cast<int>(modifier)) != 0;
        }

        bool IsShiftHeld() const { return HasModifier(ModifierKeys::Shift); }
        bool IsCtrlHeld() const { return HasModifier(ModifierKeys::Ctrl); }
        bool IsAltHeld() const { return HasModifier(ModifierKeys::Alt); }
        bool IsMetaHeld() const { return HasModifier(ModifierKeys::Meta); }
    };

// ===== KEYBOARD SHORTCUT SYSTEM =====
    struct KeyboardShortcut {
        std::vector<int> keys;
        int modifierFlags = 0;
        std::function<void()> callback;
        std::string description;
        bool enabled = true;

        KeyboardShortcut() = default;
        KeyboardShortcut(int key, int modifiers, std::function<void()> cb, const std::string& desc = "")
                : keys({key}), modifierFlags(modifiers), callback(cb), description(desc) {}

        KeyboardShortcut(const std::vector<int>& keySequence, int modifiers, std::function<void()> cb, const std::string& desc = "")
                : keys(keySequence), modifierFlags(modifiers), callback(cb), description(desc) {}

        bool Matches(const std::vector<int>& pressedKeys, int currentModifiers) const {
            if (!enabled || keys.empty()) return false;
            if (modifierFlags != currentModifiers) return false;

            if (keys.size() == 1) {
                return std::find(pressedKeys.begin(), pressedKeys.end(), keys[0]) != pressedKeys.end();
            }

            // For key sequences, check if keys match in order
            if (pressedKeys.size() < keys.size()) return false;

            auto it = pressedKeys.end() - keys.size();
            return std::equal(keys.begin(), keys.end(), it);
        }
    };

// ===== MAIN KEYBOARD MANAGER =====
    class UltraCanvasKeyboardManager {
    private:
        static KeyboardState currentState;
        static KeyboardState previousState;
        static std::vector<KeyboardShortcut> shortcuts;
        static std::vector<int> keySequence;
        static std::chrono::steady_clock::time_point lastKeyTime;
        static const int MAX_SEQUENCE_LENGTH = 10;
        static const int SEQUENCE_TIMEOUT_MS = 2000;
        static std::vector<std::function<bool(const KeyboardEvent&)>> globalHandlers;

    public:
        // ===== INITIALIZATION =====
        static bool Initialize() {
            currentState.Clear();
            previousState.Clear();
            shortcuts.clear();
            keySequence.clear();
            globalHandlers.clear();
            return true;
        }

        static void Shutdown() {
            currentState.Clear();
            previousState.Clear();
            shortcuts.clear();
            keySequence.clear();
            globalHandlers.clear();
        }

        // ===== EVENT PROCESSING =====
        static void HandleEvent(const UCEvent& event) {
            auto now = std::chrono::steady_clock::now();

            // Store previous state
            previousState = currentState;
            currentState.justPressedKeys.clear();
            currentState.justReleasedKeys.clear();

            // Update modifier state
            UpdateModifierFlags(event);

            if (event.type == UCEventType::KeyDown) {
                HandleKeyPress(event.nativeKeyCode, event.character, event.text, now);
            } else if (event.type == UCEventType::KeyUp) {
                HandleKeyRelease(event.nativeKeyCode, now);
            }

            // Check shortcuts
            CheckShortcuts();

            // Create keyboard event for handlers
            KeyboardEvent kbEvent;
            kbEvent.keyCode = event.nativeKeyCode;
            kbEvent.virtualKey = event.virtualKey;
            kbEvent.character = event.character;
            kbEvent.text = event.text;
            kbEvent.modifierFlags = currentState.modifierFlags;
            kbEvent.timestamp = now;

            if (event.type == UCEventType::KeyDown) {
                if (IsKeyPressed(event.nativeKeyCode) && !WasKeyPressed(event.nativeKeyCode)) {
                    kbEvent.state = KeyState::JustPressed;
                    currentState.justPressedKeys.insert(event.nativeKeyCode);
                } else if (IsKeyPressed(event.nativeKeyCode)) {
                    kbEvent.state = KeyState::Repeat;
                    kbEvent.isRepeat = true;
                    kbEvent.repeatCount = ++currentState.keyRepeatCounts[event.nativeKeyCode];
                }
            } else if (event.type == UCEventType::KeyUp) {
                kbEvent.state = KeyState::JustReleased;
                currentState.justReleasedKeys.insert(event.nativeKeyCode);
            }

            // Dispatch to global handlers
            for (auto& handler : globalHandlers) {
                if (handler(kbEvent)) break; // Handler consumed the event
            }
        }

        // ===== KEYBOARD STATE QUERIES =====
        static bool IsKeyPressed(int keyCode) {
            return currentState.pressedKeys.count(keyCode) > 0;
        }

        static bool WasKeyPressed(int keyCode) {
            return previousState.pressedKeys.count(keyCode) > 0;
        }

        static bool WasKeyJustPressed(int keyCode) {
            return IsKeyPressed(keyCode) && !WasKeyPressed(keyCode);
        }

        static bool WasKeyJustReleased(int keyCode) {
            return !IsKeyPressed(keyCode) && WasKeyPressed(keyCode);
        }

        static bool IsKeyRepeating(int keyCode) {
            return IsKeyPressed(keyCode) && WasKeyPressed(keyCode);
        }

        static int GetKeyRepeatCount(int keyCode) {
            auto it = currentState.keyRepeatCounts.find(keyCode);
            return (it != currentState.keyRepeatCounts.end()) ? it->second : 0;
        }

        static float GetKeyPressDuration(int keyCode) {
            auto it = currentState.keyPressTimes.find(keyCode);
            if (it == currentState.keyPressTimes.end()) return 0.0f;

            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
            return duration.count() / 1000.0f;
        }

        // ===== MODIFIER KEY QUERIES =====
        static bool IsShiftHeld() {
            return (currentState.modifierFlags & static_cast<int>(ModifierKeys::Shift)) != 0;
        }

        static bool IsCtrlHeld() {
            return (currentState.modifierFlags & static_cast<int>(ModifierKeys::Ctrl)) != 0;
        }

        static bool IsAltHeld() {
            return (currentState.modifierFlags & static_cast<int>(ModifierKeys::Alt)) != 0;
        }

        static bool IsMetaHeld() {
            return (currentState.modifierFlags & static_cast<int>(ModifierKeys::Meta)) != 0;
        }

        static bool HasModifier(ModifierKeys modifier) {
            return (currentState.modifierFlags & static_cast<int>(modifier)) != 0;
        }

        static int GetModifierFlags() {
            return currentState.modifierFlags;
        }

        // ===== MULTIPLE KEY QUERIES =====
        static bool AreKeysPressed(const std::vector<int>& keys) {
            for (int key : keys) {
                if (!IsKeyPressed(key)) return false;
            }
            return true;
        }

        static bool AnyKeyPressed() {
            return currentState.anyKeyPressed;
        }

        static const std::unordered_set<int>& GetPressedKeys() {
            return currentState.pressedKeys;
        }

        static const std::unordered_set<int>& GetJustPressedKeys() {
            return currentState.justPressedKeys;
        }

        static const std::unordered_set<int>& GetJustReleasedKeys() {
            return currentState.justReleasedKeys;
        }

        // ===== SHORTCUT MANAGEMENT =====
        static void RegisterShortcut(int key, int modifiers, std::function<void()> callback, const std::string& description = "") {
            shortcuts.emplace_back(key, modifiers, callback, description);
        }

        static void RegisterKeySequence(const std::vector<int>& keys, int modifiers, std::function<void()> callback, const std::string& description = "") {
            shortcuts.emplace_back(keys, modifiers, callback, description);
        }

        static void UnregisterShortcut(int key, int modifiers) {
            shortcuts.erase(
                    std::remove_if(shortcuts.begin(), shortcuts.end(),
                                   [key, modifiers](const KeyboardShortcut& shortcut) {
                                       return shortcut.keys.size() == 1 &&
                                              shortcut.keys[0] == key &&
                                              shortcut.modifierFlags == modifiers;
                                   }),
                    shortcuts.end());
        }

        static void ClearShortcuts() {
            shortcuts.clear();
        }

        static void EnableShortcut(int key, int modifiers, bool enabled = true) {
            for (auto& shortcut : shortcuts) {
                if (shortcut.keys.size() == 1 &&
                    shortcut.keys[0] == key &&
                    shortcut.modifierFlags == modifiers) {
                    shortcut.enabled = enabled;
                    break;
                }
            }
        }

        // ===== GLOBAL HANDLERS =====
        static void RegisterGlobalKeyboardHandler(std::function<bool(const KeyboardEvent&)> handler) {
            globalHandlers.push_back(handler);
        }

        static void ClearGlobalKeyboardHandlers() {
            globalHandlers.clear();
        }

    private:
        static void HandleKeyPress(int keyCode, char character, const std::string& text, std::chrono::steady_clock::time_point time) {
            currentState.pressedKeys.insert(keyCode);
            currentState.keyPressTimes[keyCode] = time;
            currentState.anyKeyPressed = true;

            // Add to key sequence for shortcut detection
            if (keySequence.size() >= MAX_SEQUENCE_LENGTH) {
                keySequence.erase(keySequence.begin());
            }
            keySequence.push_back(keyCode);
            lastKeyTime = time;
        }

        static void HandleKeyRelease(int keyCode, std::chrono::steady_clock::time_point time) {
            currentState.pressedKeys.erase(keyCode);
            currentState.keyPressTimes.erase(keyCode);
            currentState.keyRepeatCounts.erase(keyCode);
            currentState.anyKeyPressed = !currentState.pressedKeys.empty();
        }

        static void UpdateModifierFlags(const UCEvent& event) {
            int flags = 0;
            if (event.shift) flags |= static_cast<int>(ModifierKeys::Shift);
            if (event.ctrl) flags |= static_cast<int>(ModifierKeys::Ctrl);
            if (event.alt) flags |= static_cast<int>(ModifierKeys::Alt);
            if (event.meta) flags |= static_cast<int>(ModifierKeys::Meta);

            currentState.modifierFlags = flags;
        }

        static void CheckShortcuts() {
            // Clear expired key sequence
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastKeyTime);
            if (elapsed.count() > SEQUENCE_TIMEOUT_MS) {
                keySequence.clear();
            }

            // Check all registered shortcuts
            for (const auto& shortcut : shortcuts) {
                if (shortcut.Matches(keySequence, currentState.modifierFlags)) {
                    if (shortcut.callback) {
                        shortcut.callback();
                    }
                }
            }
        }
    };

// Static member definitions
    inline KeyboardState UltraCanvasKeyboardManager::currentState;
    inline KeyboardState UltraCanvasKeyboardManager::previousState;
    inline std::vector<KeyboardShortcut> UltraCanvasKeyboardManager::shortcuts;
    inline std::vector<int> UltraCanvasKeyboardManager::keySequence;
    inline std::chrono::steady_clock::time_point UltraCanvasKeyboardManager::lastKeyTime;
    inline std::vector<std::function<bool(const KeyboardEvent&)>> UltraCanvasKeyboardManager::globalHandlers;

} // namespace UltraCanvas
