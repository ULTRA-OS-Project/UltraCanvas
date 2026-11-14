// Apps/UltraSocial/ui/UltraSocialAccountWizard.h
// The add-account dialog: pick a network, fill the fields that network
// needs (server / identifier / secret — labels and hints switch with the
// selection), submit. The actual sign-in runs in UltraSocialApp so the
// dialog stays a dumb form.
// Version: 0.2.0 - the app registration (client id / secret) and the
//                  default servers come from Settings; the form only asks
//                  for a client id the settings do not have yet
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSocialSettings.h"
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
    // `settings` decide what the form asks: a network whose app
    // registration is already set there shows no client-id fields, and the
    // server fields show the default they fall back to.
    void Show(UltraCanvas::UltraCanvasWindowBase* parent,
              const SignInSettings& settings,
              std::function<void(const WizardInput&)> onSubmit);
};

} // namespace UltraSocial
