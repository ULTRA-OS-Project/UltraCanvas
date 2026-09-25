// Apps/UltraMail/ui/UltraMailOAuthCodeDialog.h
// The prompt for an out-of-band (oob) OAuth2 sign-in: the provider (Yahoo)
// cannot redirect back to a loopback listener, so its consent page shows an
// authorization code the user copies here. Shown after the browser is opened;
// onSubmit receives the entered code when the user confirms.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasModalDialog.h"

#include <functional>
#include <string>

namespace UltraMail {

class OAuthCodeDialog {
public:
    // Prompt the user to paste the code `providerName` showed after they signed
    // in as `email`. onSubmit runs with the entered (trimmed) code when the user
    // confirms; a cancel closes the dialog and runs nothing.
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent,
                     const std::string& providerName,
                     const std::string& email,
                     std::function<void(const std::string& code)> onSubmit);
};

} // namespace UltraMail
