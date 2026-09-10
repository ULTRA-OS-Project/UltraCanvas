// Apps/UltraMail/ui/UltraMailOAuthWaitDialog.h
// The dialog shown while the browser holds the provider's sign-in page: says
// what to do there, and offers Cancel. The app closes it when the loopback
// redirect arrives (Close), or the user cancels (onCancel runs).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasModalDialog.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraMail {

class OAuthWaitDialog {
public:
    // Show the dialog for a sign-in with `providerName` (e.g. "Google") as
    // `email`. Returns the dialog so the caller can close it programmatically
    // once the sign-in finished; keep only a weak_ptr — the dialog manager owns
    // it and it may already be gone when the sign-in completes.
    static std::shared_ptr<UltraCanvas::UltraCanvasModalDialog> Show(
        UltraCanvas::UltraCanvasWindowBase* parent,
        const std::string& providerName,
        const std::string& email,
        std::function<void()> onCancel);

    // Close a dialog returned by Show as "done" (no onCancel).
    static void Close(const std::weak_ptr<UltraCanvas::UltraCanvasModalDialog>& dialog);
};

} // namespace UltraMail
