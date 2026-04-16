// OS/MacOS/UltraCanvasMacOSShortcutFormat.h
// macOS shortcut symbol formatter — converts canonical shortcut strings
// (e.g. "Ctrl+Shift+Z") to macOS Unicode glyph notation (e.g. "⇧⌘Z")
// Version: 1.0.0
// Last Modified: 2026-04-12
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace UltraCanvas {

    // ===== macOS MODIFIER SYMBOL MAP =====
    // macOS standard order: Control ⌃ · Option ⌥ · Shift ⇧ · Command ⌘
    // Displayed without separators, uppercase key appended last.

    inline std::string GetDisplayShortcut(const std::string& shortcut) {
        if (shortcut.empty()) return shortcut;

        // Tokenise on '+' — e.g. "Ctrl+Shift+Z" → ["Ctrl","Shift","Z"]
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream stream(shortcut);
        while (std::getline(stream, token, '+')) {
            if (!token.empty()) tokens.push_back(token);
        }

        // Modifier presence flags (macOS display order: ⌃⌥⇧⌘)
        bool hasCtrl  = false;
        bool hasAlt   = false;
        bool hasShift = false;
        bool hasMeta  = false;
        std::string key;

        for (const auto& t : tokens) {
            std::string lower = t;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            if      (lower == "ctrl"  || lower == "control") hasCtrl  = true;
            else if (lower == "alt"   || lower == "option")  hasAlt   = true;
            else if (lower == "shift")                        hasShift = true;
            else if (lower == "cmd"   || lower == "command"
                     || lower == "meta"  || lower == "super")   hasMeta  = true;
            else {
                // Regular key — uppercase single letter, or named key symbol
                key = t;
            }
        }

        // Map named keys to macOS glyphs
        std::string keyGlyph;
        std::string lowerKey = key;
        std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::tolower);

        if      (lowerKey == "return" || lowerKey == "enter")     keyGlyph = u8"\u21a9"; // ↩
        else if (lowerKey == "backspace" || lowerKey == "delete")  keyGlyph = u8"\u232b"; // ⌫
        else if (lowerKey == "tab")                                keyGlyph = u8"\u21e5"; // ⇥
        else if (lowerKey == "escape" || lowerKey == "esc")        keyGlyph = u8"\u238b"; // ⎋
        else if (lowerKey == "space")                              keyGlyph = u8"\u2423"; // ␣
        else if (lowerKey == "up")                                 keyGlyph = u8"\u2191"; // ↑
        else if (lowerKey == "down")                               keyGlyph = u8"\u2193"; // ↓
        else if (lowerKey == "left")                               keyGlyph = u8"\u2190"; // ←
        else if (lowerKey == "right")                              keyGlyph = u8"\u2192"; // →
        else if (lowerKey == "pageup")                             keyGlyph = u8"\u21de"; // ⇞
        else if (lowerKey == "pagedown")                           keyGlyph = u8"\u21df"; // ⇟
        else if (lowerKey == "home")                               keyGlyph = u8"\u2196"; // ↖
        else if (lowerKey == "end")                                keyGlyph = u8"\u2198"; // ↘
        else if (key.size() == 1) {
            // Single character — uppercase it (macOS convention)
            keyGlyph = std::string(1, static_cast<char>(::toupper(key[0])));
        } else {
            // Multi-char key name (F1, F12, etc.) — keep as-is
            keyGlyph = key;
        }

        // Build result: ⌃⌥⇧⌘ + key  (no separators — Apple HIG)
        std::string result;
        if (hasCtrl)  result += u8"\u2303"; // ⌃
        if (hasAlt)   result += u8"\u2325"; // ⌥
        if (hasShift) result += u8"\u21e7"; // ⇧
        if (hasMeta)  result += u8"\u2318"; // ⌘
        result += keyGlyph;

        return result;
    }

} // namespace UltraCanvas

#define ULTRACANVAS_PLATFORM_SHORTCUT_DEFINED