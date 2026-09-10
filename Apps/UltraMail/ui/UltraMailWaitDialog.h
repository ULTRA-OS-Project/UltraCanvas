// Apps/UltraMail/ui/UltraMailWaitDialog.h
// A dialog for a step that runs elsewhere — in the browser (an OAuth2 sign-in)
// or on the network (the server-settings lookup): says what is going on and
// offers Cancel. The app closes it when the step finished (Close), or the user
// cancels (onCancel runs).
// Version: 0.2.0 - generic WaitDialog; OAuthWaitDialog is the sign-in wording
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasModalDialog.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraMail {

class WaitDialog {
public:
    // Show `text` under `title`. Returns the dialog so the caller can close it
    // programmatically once the step finished; keep only a weak_ptr — the
    // dialog manager owns it and it may already be gone by then.
    static std::shared_ptr<UltraCanvas::UltraCanvasModalDialog> Show(
        UltraCanvas::UltraCanvasWindowBase* parent,
        const std::string& title,
        const std::string& text,
        std::function<void()> onCancel);

    // Close a dialog returned by Show as "done" (no onCancel).
    static void Close(const std::weak_ptr<UltraCanvas::UltraCanvasModalDialog>& dialog);
};

// The wording for a browser sign-in with `providerName` (e.g. "Google") as `email`.
class OAuthWaitDialog {
public:
    static std::shared_ptr<UltraCanvas::UltraCanvasModalDialog> Show(
        UltraCanvas::UltraCanvasWindowBase* parent,
        const std::string& providerName,
        const std::string& email,
        std::function<void()> onCancel);
    static void Close(const std::weak_ptr<UltraCanvas::UltraCanvasModalDialog>& dialog) {
        WaitDialog::Close(dialog);
    }
};

} // namespace UltraMail
