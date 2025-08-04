// UltraCanvasKeyboardManager.cpp
// Cross-platform keyboard input management implementation
// Version: 2.1.0
// Last Modified: 2025-07-08
// Author: UltraCanvas Framework

#include "UltraCanvasKeyboardManager.h"
#include "UltraCanvasUIElement.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>

namespace UltraCanvas {

// ===== STATIC MEMBER DEFINITIONS =====
    std::unordered_set<UCKeys> UltraCanvasKeyboardManager::pressedKeys;
    std::unordered_set<UCKeys> UltraCanvasKeyboardManager::previousFrameKeys;
    int UltraCanvasKeyboardManager::currentModifiers = 0;
    bool UltraCanvasKeyboardManager::initialized = false;
    bool UltraCanvasKeyboardManager::enabled = true;

    std::vector<Hotkey> UltraCanvasKeyboardManager::registeredHotkeys;
    std::unordered_map<KeyCombination, size_t> UltraCanvasKeyboardManager::hotkeyMap;
    bool UltraCanvasKeyboardManager::hotkeysEnabled = true;

    KeyboardRepeatSettings UltraCanvasKeyboardManager::repeatSettings;
    std::unordered_map<UCKeys, std::chrono::steady_clock::time_point> UltraCanvasKeyboardManager::keyPressTime;
    std::unordered_map<UCKeys, std::chrono::steady_clock::time_point> UltraCanvasKeyboardManager::lastRepeatTime;
    std::unordered_set<UCKeys> UltraCanvasKeyboardManager::repeatingKeys;

    IMEComposition UltraCanvasKeyboardManager::currentComposition;
    bool UltraCanvasKeyboardManager::imeEnabled = false;

    KeyboardLayoutInfo UltraCanvasKeyboardManager::currentLayout;
    std::vector<KeyboardLayoutInfo> UltraCanvasKeyboardManager::availableLayouts;

    std::function<bool(const UCEvent&)> UltraCanvasKeyboardManager::keyEventFilter = nullptr;
    std::vector<std::function<void(UCKeys, KeyState)>> UltraCanvasKeyboardManager::keyStateCallbacks;
    std::vector<std::function<void(const std::string&)>> UltraCanvasKeyboardManager::textInputCallbacks;

    UltraCanvasElement* UltraCanvasKeyboardManager::focusedElement = nullptr;
    bool UltraCanvasKeyboardManager::captureFocus = false;

    bool UltraCanvasKeyboardManager::debugMode = false;
    std::vector<UCEvent> UltraCanvasKeyboardManager::eventHistory;
    size_t UltraCanvasKeyboardManager::maxHistorySize = 100;

// ===== INITIALIZATION IMPLEMENTATION =====

    bool UltraCanvasKeyboardManager::Initialize() {
        if (initialized) {
            return true;
        }

        // Initialize default settings
        repeatSettings = KeyboardRepeatSettings();
        currentLayout = KeyboardLayoutInfo(KeyboardLayout::QWERTY_US, "US English");

        InitializeDefaultLayouts();

        initialized = true;

        if (debugMode) {
            std::cout << "UltraCanvas Keyboard Manager initialized" << std::endl;
        }

        return true;
    }

    void UltraCanvasKeyboardManager::Shutdown() {
        if (!initialized) {
            return;
        }

        // Clear all state
        pressedKeys.clear();
        previousFrameKeys.clear();
        currentModifiers = 0;

        UnregisterAllHotkeys();
        ClearKeyStateCallbacks();
        ClearTextInputCallbacks();
        ClearEventHistory();

        currentComposition.Clear();
        focusedElement = nullptr;

        initialized = false;

        if (debugMode) {
            std::cout << "UltraCanvas Keyboard Manager shutdown" << std::endl;
        }
    }

    bool UltraCanvasKeyboardManager::IsInitialized() {
        return initialized;
    }

    void UltraCanvasKeyboardManager::Update() {
        if (!initialized || !enabled) {
            return;
        }

        UpdateKeyStates();
        ProcessKeyRepeat();
        ProcessHotkeys();
    }

// ===== ENABLE/DISABLE IMPLEMENTATION =====

    void UltraCanvasKeyboardManager::SetEnabled(bool isEnabled) {
        enabled = isEnabled;
        if (!enabled) {
            pressedKeys.clear();
            repeatingKeys.clear();
            currentModifiers = 0;
        }
    }

    bool UltraCanvasKeyboardManager::IsEnabled() {
        return enabled;
    }

// ===== KEY STATE QUERIES IMPLEMENTATION =====

    bool UltraCanvasKeyboardManager::IsKeyPressed(UCKeys key) {
        return pressedKeys.find(key) != pressedKeys.end();
    }

    bool UltraCanvasKeyboardManager::IsKeyJustPressed(UCKeys key) {
        return IsKeyPressed(key) && (previousFrameKeys.find(key) == previousFrameKeys.end());
    }

    bool UltraCanvasKeyboardManager::IsKeyJustReleased(UCKeys key) {
        return !IsKeyPressed(key) && (previousFrameKeys.find(key) != previousFrameKeys.end());
    }

    bool UltraCanvasKeyboardManager::WasKeyPressed(UCKeys key) {
        return previousFrameKeys.find(key) != previousFrameKeys.end();
    }

    KeyState UltraCanvasKeyboardManager::GetKeyState(UCKeys key) {
        bool currentlyPressed = IsKeyPressed(key);
        bool wasPressed = WasKeyPressed(key);

        if (!currentlyPressed) {
            return KeyState::Released;
        }

        if (currentlyPressed && !wasPressed) {
            return KeyState::Pressed;
        }

        return KeyState::Repeat;
    }

// ===== MODIFIER KEY QUERIES IMPLEMENTATION =====

    bool UltraCanvasKeyboardManager::IsModifierPressed(ModifierKeys modifier) {
        return (currentModifiers & static_cast<int>(modifier)) != 0;
    }

    int UltraCanvasKeyboardManager::GetCurrentModifiers() {
        return currentModifiers;
    }

    bool UltraCanvasKeyboardManager::IsShiftPressed() {
        return IsModifierPressed(ModifierKeys::Shift);
    }

    bool UltraCanvasKeyboardManager::IsControlPressed() {
        return IsModifierPressed(ModifierKeys::Control);
    }

    bool UltraCanvasKeyboardManager::IsAltPressed() {
        return IsModifierPressed(ModifierKeys::Alt);
    }

    bool UltraCanvasKeyboardManager::IsSuperPressed() {
        return IsModifierPressed(ModifierKeys::Super);
    }

    bool UltraCanvasKeyboardManager::IsCapsLockOn() {
        return IsModifierPressed(ModifierKeys::CapsLock);
    }

    bool UltraCanvasKeyboardManager::IsNumLockOn() {
        return IsModifierPressed(ModifierKeys::NumLock);
    }

    bool UltraCanvasKeyboardManager::IsScrollLockOn() {
        return IsModifierPressed(ModifierKeys::ScrollLock);
    }

// ===== KEY COMBINATION UTILITIES IMPLEMENTATION =====

    bool UltraCanvasKeyboardManager::IsKeyCombinationPressed(const KeyCombination& combo) {
        return IsKeyPressed(combo.key) && (currentModifiers & combo.modifiers) == combo.modifiers;
    }

    KeyCombination UltraCanvasKeyboardManager::GetCurrentKeyCombination() {
        // Return the first pressed key with current modifiers
        if (!pressedKeys.empty()) {
            return KeyCombination(*pressedKeys.begin(), currentModifiers);
        }
        return KeyCombination();
    }

    std::vector<UCKeys> UltraCanvasKeyboardManager::GetPressedKeys() {
        return std::vector<UCKeys>(pressedKeys.begin(), pressedKeys.end());
    }

    std::string UltraCanvasKeyboardManager::GetKeyName(UCKeys key) {
        // This would typically use platform-specific functions to get localized key names
        // For now, return a basic mapping
        switch (key) {
            case UCKeys::Space: return "Space";
            case UCKeys::Return: return "Enter";
            case UCKeys::Escape: return "Escape";
            case UCKeys::Tab: return "Tab";
            case UCKeys::Backspace: return "Backspace";
            case UCKeys::Delete: return "Delete";
            case UCKeys::LeftArrow: return "Left";
            case UCKeys::RightArrow: return "Right";
            case UCKeys::UpArrow: return "Up";
            case UCKeys::DownArrow: return "Down";
            case UCKeys::LeftShift: return "Left Shift";
            case UCKeys::RightShift: return "Right Shift";
            case UCKeys::LeftControl: return "Left Ctrl";
            case UCKeys::RightControl: return "Right Ctrl";
            case UCKeys::LeftAlt: return "Left Alt";
            case UCKeys::RightAlt: return "Right Alt";
            case UCKeys::A: return "A";
            case UCKeys::B: return "B";
            case UCKeys::C: return "C";
            case UCKeys::D: return "D";
            case UCKeys::E: return "E";
            case UCKeys::F: return "F";
            case UCKeys::G: return "G";
            case UCKeys::H: return "H";
            case UCKeys::I: return "I";
            case UCKeys::J: return "J";
            case UCKeys::K: return "K";
            case UCKeys::L: return "L";
            case UCKeys::M: return "M";
            case UCKeys::N: return "N";
            case UCKeys::O: return "O";
            case UCKeys::P: return "P";
            case UCKeys::Q: return "Q";
            case UCKeys::R: return "R";
            case UCKeys::S: return "S";
            case UCKeys::T: return "T";
            case UCKeys::U: return "U";
            case UCKeys::V: return "V";
            case UCKeys::W: return "W";
            case UCKeys::X: return "X";
            case UCKeys::Y: return "Y";
            case UCKeys::Z: return "Z";
            default: return "Unknown";
        }
    }

    UCKeys UltraCanvasKeyboardManager::GetKeyFromName(const std::string& name) {
        // Reverse lookup for key names
        std::string lowerName = name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

        if (lowerName == "space") return UCKeys::Space;
        if (lowerName == "enter" || lowerName == "return") return UCKeys::Return;
        if (lowerName == "escape" || lowerName == "esc") return UCKeys::Escape;
        if (lowerName == "tab") return UCKeys::Tab;
        if (lowerName == "backspace") return UCKeys::Backspace;
        if (lowerName == "delete" || lowerName == "del") return UCKeys::Delete;
        if (lowerName == "left") return UCKeys::LeftArrow;
        if (lowerName == "right") return UCKeys::RightArrow;
        if (lowerName == "up") return UCKeys::UpArrow;
        if (lowerName == "down") return UCKeys::DownArrow;

        // Single letters
        if (lowerName.length() == 1 && lowerName[0] >= 'a' && lowerName[0] <= 'z') {
            return static_cast<UCKeys>(static_cast<int>(UCKeys::A) + (lowerName[0] - 'a'));
        }

        return UCKeys::Unknown;
    }

// ===== HOTKEY SYSTEM IMPLEMENTATION =====

    size_t UltraCanvasKeyboardManager::RegisterHotkey(const KeyCombination& combo, std::function<void()> callback,
                                                      const std::string& description) {
        size_t id = GenerateHotkeyId();

        Hotkey hotkey(combo, callback, description);
        registeredHotkeys.push_back(hotkey);
        hotkeyMap[combo] = registeredHotkeys.size() - 1;

        if (debugMode) {
            std::cout << "Registered hotkey: " << combo.ToString() << " (ID: " << id << ")" << std::endl;
        }

        return id;
    }

    size_t UltraCanvasKeyboardManager::RegisterHotkey(UCKeys key, int modifiers, std::function<void()> callback,
                                                      const std::string& description) {
        return RegisterHotkey(KeyCombination(key, modifiers), callback, description);
    }

    bool UltraCanvasKeyboardManager::UnregisterHotkey(size_t hotkeyId) {
        // Find and remove hotkey by ID
        for (auto it = registeredHotkeys.begin(); it != registeredHotkeys.end(); ++it) {
            if (std::distance(registeredHotkeys.begin(), it) == hotkeyId) {
                // Remove from map
                hotkeyMap.erase(it->combination);
                // Remove from vector
                registeredHotkeys.erase(it);

                if (debugMode) {
                    std::cout << "Unregistered hotkey ID: " << hotkeyId << std::endl;
                }

                return true;
            }
        }
        return false;
    }

    void UltraCanvasKeyboardManager::UnregisterAllHotkeys() {
        registeredHotkeys.clear();
        hotkeyMap.clear();

        if (debugMode) {
            std::cout << "Unregistered all hotkeys" << std::endl;
        }
    }

    void UltraCanvasKeyboardManager::SetHotkeyEnabled(size_t hotkeyId, bool enabled) {
        if (hotkeyId < registeredHotkeys.size()) {
            registeredHotkeys[hotkeyId].enabled = enabled;
        }
    }

    void UltraCanvasKeyboardManager::SetHotkeysEnabled(bool enabled) {
        hotkeysEnabled = enabled;
    }

    bool UltraCanvasKeyboardManager::AreHotkeysEnabled() {
        return hotkeysEnabled;
    }

    std::vector<Hotkey> UltraCanvasKeyboardManager::GetRegisteredHotkeys() {
        return registeredHotkeys;
    }

// ===== EVENT HANDLING IMPLEMENTATION =====

    bool UltraCanvasKeyboardManager::HandleKeyboardEvent(const UCEvent& event) {
        if (!initialized || !enabled) {
            return false;
        }

        // Apply event filter if set
        if (keyEventFilter && !keyEventFilter(event)) {
            return false;
        }

        // Add to event history
        AddToEventHistory(event);

        bool handled = false;

        switch (event.type) {
            case UCEventType::KeyDown: {
                UCKeys key = event.key.code;

                // Update pressed keys
                pressedKeys.insert(key);

                // Update modifiers
                switch (key) {
                    case UCKeys::LeftShift:
                    case UCKeys::RightShift:
                        currentModifiers |= static_cast<int>(ModifierKeys::Shift);
                        break;
                    case UCKeys::LeftControl:
                    case UCKeys::RightControl:
                        currentModifiers |= static_cast<int>(ModifierKeys::Control);
                        break;
                    case UCKeys::LeftAlt:
                    case UCKeys::RightAlt:
                        currentModifiers |= static_cast<int>(ModifierKeys::Alt);
                        break;
                    default:
                        break;
                }

                // Record key press time for repeat
                keyPressTime[key] = std::chrono::steady_clock::now();

                // Notify callbacks
                NotifyKeyStateCallbacks(key, KeyState::Pressed);

                handled = true;
                break;
            }

            case UCEventType::KeyUp: {
                UCKeys key = event.key.code;

                // Update pressed keys
                pressedKeys.erase(key);

                // Update modifiers
                switch (key) {
                    case UCKeys::LeftShift:
                    case UCKeys::RightShift:
                        currentModifiers &= ~static_cast<int>(ModifierKeys::Shift);
                        break;
                    case UCKeys::LeftControl:
                    case UCKeys::RightControl:
                        currentModifiers &= ~static_cast<int>(ModifierKeys::Control);
                        break;
                    case UCKeys::LeftAlt:
                    case UCKeys::RightAlt:
                        currentModifiers &= ~static_cast<int>(ModifierKeys::Alt);
                        break;
                    default:
                        break;
                }

                // Stop key repeat
                repeatingKeys.erase(key);
                keyPressTime.erase(key);
                lastRepeatTime.erase(key);

                // Notify callbacks
                NotifyKeyStateCallbacks(key, KeyState::Released);

                handled = true;
                break;
            }

            case UCEventType::TextInput: {
                std::string text = event.text.text;
                NotifyTextInputCallbacks(text);
                handled = true;
                break;
            }

            default:
                break;
        }

        return handled;
    }

// ===== PRIVATE HELPER METHODS IMPLEMENTATION =====

    void UltraCanvasKeyboardManager::UpdateKeyStates() {
        // Update previous frame state
        previousFrameKeys = pressedKeys;
    }

    void UltraCanvasKeyboardManager::ProcessKeyRepeat() {
        if (!repeatSettings.enabled) {
            return;
        }

        auto now = std::chrono::steady_clock::now();

        for (UCKeys key : pressedKeys) {
            auto pressTimeIt = keyPressTime.find(key);
            if (pressTimeIt == keyPressTime.end()) {
                continue;
            }

            float elapsed = std::chrono::duration<float>(now - pressTimeIt->second).count();

            // Check if we should start repeating
            bool isRepeating = repeatingKeys.find(key) != repeatingKeys.end();

            if (!isRepeating && elapsed >= repeatSettings.initialDelay) {
                // Start repeating
                repeatingKeys.insert(key);
                lastRepeatTime[key] = now;
                NotifyKeyStateCallbacks(key, KeyState::Repeat);
            } else if (isRepeating) {
                // Check if we should send another repeat
                auto lastRepeatIt = lastRepeatTime.find(key);
                if (lastRepeatIt != lastRepeatTime.end()) {
                    float repeatElapsed = std::chrono::duration<float>(now - lastRepeatIt->second).count();
                    if (repeatElapsed >= repeatSettings.repeatRate) {
                        lastRepeatTime[key] = now;
                        NotifyKeyStateCallbacks(key, KeyState::Repeat);
                    }
                }
            }
        }
    }

    void UltraCanvasKeyboardManager::ProcessHotkeys() {
        if (!hotkeysEnabled) {
            return;
        }

        for (const auto& hotkey : registeredHotkeys) {
            if (!hotkey.enabled || !hotkey.callback) {
                continue;
            }

            if (IsKeyCombinationPressed(hotkey.combination)) {
                // Only trigger on key press, not repeat
                if (IsKeyJustPressed(hotkey.combination.key)) {
                    hotkey.callback();

                    if (debugMode) {
                        std::cout << "Triggered hotkey: " << hotkey.combination.ToString() << std::endl;
                    }
                }
            }
        }
    }

    void UltraCanvasKeyboardManager::NotifyKeyStateCallbacks(UCKeys key, KeyState state) {
        for (const auto& callback : keyStateCallbacks) {
            callback(key, state);
        }
    }

    void UltraCanvasKeyboardManager::NotifyTextInputCallbacks(const std::string& text) {
        for (const auto& callback : textInputCallbacks) {
            callback(text);
        }
    }

    void UltraCanvasKeyboardManager::AddToEventHistory(const UCEvent& event) {
        eventHistory.push_back(event);

        // Maintain max history size
        while (eventHistory.size() > maxHistorySize) {
            eventHistory.erase(eventHistory.begin());
        }
    }

    size_t UltraCanvasKeyboardManager::GenerateHotkeyId() {
        static size_t nextId = 0;
        return nextId++;
    }

    void UltraCanvasKeyboardManager::InitializeDefaultLayouts() {
        availableLayouts.clear();

        // Add common keyboard layouts
        availableLayouts.emplace_back(KeyboardLayout::QWERTY_US, "US English", "en", "US");
        availableLayouts.emplace_back(KeyboardLayout::QWERTY_UK, "UK English", "en", "GB");
        availableLayouts.emplace_back(KeyboardLayout::QWERTZ_DE, "German", "de", "DE");
        availableLayouts.emplace_back(KeyboardLayout::AZERTY_FR, "French", "fr", "FR");
        availableLayouts.emplace_back(KeyboardLayout::Dvorak, "Dvorak", "en", "US");
        availableLayouts.emplace_back(KeyboardLayout::Colemak, "Colemak", "en", "US");
    }

    KeyboardLayoutInfo UltraCanvasKeyboardManager::DetectSystemLayout() {
        // Platform-specific detection would go here
        // For now, return default US layout
        return KeyboardLayoutInfo(KeyboardLayout::QWERTY_US, "US English", "en", "US");
    }

// ===== KEY COMBINATION STRING METHODS =====

    std::string KeyCombination::ToString() const {
        std::string result;

        if (modifiers & static_cast<int>(ModifierKeys::Control)) {
            result += "Ctrl+";
        }
        if (modifiers & static_cast<int>(ModifierKeys::Shift)) {
            result += "Shift+";
        }
        if (modifiers & static_cast<int>(ModifierKeys::Alt)) {
            result += "Alt+";
        }
        if (modifiers & static_cast<int>(ModifierKeys::Super)) {
            result += "Super+";
        }

        result += UltraCanvasKeyboardManager::GetKeyName(key);

        return result;
    }

    KeyCombination KeyCombination::FromString(const std::string& str) {
        KeyCombination combo;

        std::string remaining = str;

        // Parse modifiers
        if (remaining.find("Ctrl+") == 0) {
            combo.modifiers |= static_cast<int>(ModifierKeys::Control);
            remaining = remaining.substr(5);
        }
        if (remaining.find("Shift+") == 0) {
            combo.modifiers |= static_cast<int>(ModifierKeys::Shift);
            remaining = remaining.substr(6);
        }
        if (remaining.find("Alt+") == 0) {
            combo.modifiers |= static_cast<int>(ModifierKeys::Alt);
            remaining = remaining.substr(4);
        }
        if (remaining.find("Super+") == 0) {
            combo.modifiers |= static_cast<int>(ModifierKeys::Super);
            remaining = remaining.substr(6);
        }

        // Parse key
        combo.key = UltraCanvasKeyboardManager::GetKeyFromName(remaining);

        return combo;
    }

// Additional implementation methods would continue here...
// For brevity, showing the pattern for the major functionality

} // namespace UltraCanvas