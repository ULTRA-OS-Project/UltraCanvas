// Apps/UltraSocial/ui/UltraSocialAccountWizard.h
// The add-account dialog: pick a network, fill the fields that network
// needs (server / identifier / secret — labels and hints switch with the
// selection), submit. The actual sign-in runs in UltraSocialApp so the
// dialog stays a dumb form.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSocialTypes.h"

#include "UltraCanvasWindow.h"

#include <functional>
#include <string>

namespace UltraSocial {

struct WizardInput {
    SocialNetwork network = SocialNetwork::Mastodon;
    std::string server;
    std::string identifier;
    std::string secret;
    std::string clientId;
};

class AccountWizard {
public:
    void Show(UltraCanvas::UltraCanvasWindowBase* parent,
              std::function<void(const WizardInput&)> onSubmit);
};

} // namespace UltraSocial
