// Apps/UltraMail/ui/UltraMailSignatureDialog.h
// The signature editor, opened from an account's settings page. The account
// signs with nothing, a plain-text signature or an HTML one:
//   * Plain text is typed into a text area.
//   * HTML is designed in a WYSIWYG editor (UltraCanvasRichTextEdit): bold,
//     italic, underline, strike, font, size, colour, alignment, lists, links,
//     pictures and a rule - or written as HTML source, for a signature made
//     elsewhere.
// Both versions are kept whichever is chosen. A checkbox decides whether the
// signature also goes under replies and forwards. Nothing is saved on Cancel.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailTypes.h"   // Signature

#include <functional>
#include <string>

namespace UltraCanvas { class UltraCanvasWindowBase; }

namespace UltraMail {

class SignatureDialog {
public:
    // Opens the editor for the account `email` with `current` filled in.
    // `onSave` receives the edited signature when Save is clicked.
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent,
                     const std::string& email,
                     const Signature& current,
                     std::function<void(const Signature&)> onSave);
};

} // namespace UltraMail
