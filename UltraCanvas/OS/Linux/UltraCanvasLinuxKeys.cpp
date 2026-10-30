// OS/Linux/UltraCanvasLinuxKeys.cpp
// The X keysym to UCKeys table. See the header.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasLinuxKeys.h"

#include <X11/keysym.h>

#include <cctype>

namespace UltraCanvas {

    UCKeys UCKeyFromKeysym(unsigned long keysym) {
        switch (keysym) {
            // Special keys
            case XK_Return: return UCKeys::Enter;
            case XK_Escape: return UCKeys::Escape;
            case XK_space: return UCKeys::Space;
            case XK_BackSpace: return UCKeys::Backspace;
            case XK_Tab: return UCKeys::Tab;
            case XK_Delete: return UCKeys::Delete;
            case XK_Insert: return UCKeys::Insert;

                // Arrow keys
            case XK_Left: return UCKeys::Left;
            case XK_Right: return UCKeys::Right;
            case XK_Up: return UCKeys::Up;
            case XK_Down: return UCKeys::Down;

                // Navigation
            case XK_Home: return UCKeys::Home;
            case XK_End: return UCKeys::End;
            case XK_Page_Up: return UCKeys::PageUp;
            case XK_Page_Down: return UCKeys::PageDown;

                // Function keys
            case XK_F1: return UCKeys::F1;
            case XK_F2: return UCKeys::F2;
            case XK_F3: return UCKeys::F3;
            case XK_F4: return UCKeys::F4;
            case XK_F5: return UCKeys::F5;
            case XK_F6: return UCKeys::F6;
            case XK_F7: return UCKeys::F7;
            case XK_F8: return UCKeys::F8;
            case XK_F9: return UCKeys::F9;
            case XK_F10: return UCKeys::F10;
            case XK_F11: return UCKeys::F11;
            case XK_F12: return UCKeys::F12;

                // Modifier keys
            case XK_Shift_L: return UCKeys::LeftShift;
            case XK_Shift_R: return UCKeys::RightShift;
            case XK_Control_L: return UCKeys::LeftCtrl;
            case XK_Control_R: return UCKeys::RightCtrl;
            case XK_Alt_L: return UCKeys::LeftAlt;
            case XK_Alt_R: return UCKeys::RightAlt;
            case XK_Super_L: return UCKeys::LeftMeta;
            case XK_Super_R: return UCKeys::RightMeta;

                // Number row (main row 0-9)
            case XK_0: return UCKeys::Key0;
            case XK_1: return UCKeys::Key1;
            case XK_2: return UCKeys::Key2;
            case XK_3: return UCKeys::Key3;
            case XK_4: return UCKeys::Key4;
            case XK_5: return UCKeys::Key5;
            case XK_6: return UCKeys::Key6;
            case XK_7: return UCKeys::Key7;
            case XK_8: return UCKeys::Key8;
            case XK_9: return UCKeys::Key9;

                // ASCII punctuation / symbols (main row)
            case XK_equal: return UCKeys::Equal;
            case XK_plus: return UCKeys::Plus;
            case XK_minus: return UCKeys::Minus;
            case XK_underscore: return UCKeys::Underscore;
            case XK_comma: return UCKeys::Comma;
            case XK_period: return UCKeys::Period;
            case XK_slash: return UCKeys::Slash;
            case XK_backslash: return UCKeys::Backslash;
            case XK_semicolon: return UCKeys::Semicolon;
            case XK_colon: return UCKeys::Colon;
            case XK_apostrophe: return UCKeys::Quote;
            case XK_quotedbl: return UCKeys::DoubleQuote;
            case XK_grave: return UCKeys::Grave;
            case XK_asciitilde: return UCKeys::Tilde;
            case XK_bracketleft: return UCKeys::LeftBracket;
            case XK_bracketright: return UCKeys::RightBracket;
            case XK_braceleft: return UCKeys::LeftBrace;
            case XK_braceright: return UCKeys::RightBrace;
            case XK_parenleft: return UCKeys::LeftParen;
            case XK_parenright: return UCKeys::RightParen;
            case XK_bar: return UCKeys::Pipe;
            case XK_less: return UCKeys::Less;
            case XK_greater: return UCKeys::Greater;
            case XK_question: return UCKeys::Question;
            case XK_exclam: return UCKeys::Exclamation;
            case XK_at: return UCKeys::At;
            case XK_numbersign: return UCKeys::Hash;
            case XK_dollar: return UCKeys::Dollar;
            case XK_percent: return UCKeys::Percent;
            case XK_asciicircum: return UCKeys::Caret;
            case XK_ampersand: return UCKeys::Ampersand;
            case XK_asterisk: return UCKeys::Asterisk;

                // Number pad
            case XK_Num_Lock: return UCKeys::NumLock;
            case XK_KP_Home: return UCKeys::NumPadHome;
            case XK_KP_Up: return UCKeys::NumPadUp;
            case XK_KP_Page_Up: return UCKeys::NumPadPageUp;
            case XK_KP_Left: return UCKeys::NumPadLeft;
            case XK_KP_Right: return UCKeys::NumPadRight;
            case XK_KP_End: return UCKeys::NumPadEnd;
            case XK_KP_Down: return UCKeys::NumPadDown;
            case XK_KP_Page_Down: return UCKeys::NumPadPageDown;
            case XK_KP_0: return UCKeys::NumPad0;
            case XK_KP_1: return UCKeys::NumPad1;
            case XK_KP_2: return UCKeys::NumPad2;
            case XK_KP_3: return UCKeys::NumPad3;
            case XK_KP_4: return UCKeys::NumPad4;
            case XK_KP_5: return UCKeys::NumPad5;
            case XK_KP_6: return UCKeys::NumPad6;
            case XK_KP_7: return UCKeys::NumPad7;
            case XK_KP_8: return UCKeys::NumPad8;
            case XK_KP_9: return UCKeys::NumPad9;
            case XK_KP_Add: return UCKeys::NumPadPlus;
            case XK_KP_Subtract: return UCKeys::NumPadMinus;
            case XK_KP_Divide: return UCKeys::NumPadDivide;
            case XK_KP_Enter: return UCKeys::NumPadEnter;
            case XK_KP_Multiply: return UCKeys::NumPadMultiply;
            case XK_KP_Decimal: return UCKeys::NumPadDecimal;
            case XK_KP_Insert: return UCKeys::NumPadInsert;
            case XK_KP_Delete: return UCKeys::NumPadDelete;

                // ASCII characters
            default:
                if (keysym >= 0x0020 && keysym <= 0x007E) {
                    return static_cast<UCKeys>(toupper((char)keysym));
                }
                return UCKeys::Unknown;
        }
    }

} // namespace UltraCanvas
