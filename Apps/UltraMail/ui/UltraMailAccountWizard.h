// Apps/UltraMail/ui/UltraMailAccountWizard.h
// The account-setup wizard. Phase 1 implements step 1 (identity: name, email,
// password) as a modal dialog; the discovery/verify pipeline (provider presets
// -> autoconfig -> DNS SRV -> probing, via UltraNet) is a later engine slice,
// so on submit the entered identity is handed back through onSubmit.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasWindow.h"

#include <functional>
#include <string>

namespace UltraMail {

struct AccountDraft {
    std::string displayName;
    std::string email;
    std::string password;
};

class AccountWizard {
public:
    // Show the wizard modally over `parent`. onSubmit is called with the entered
    // identity when the user confirms; nothing is called on cancel.
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent,
                     std::function<void(const AccountDraft&)> onSubmit);
};

} // namespace UltraMail
