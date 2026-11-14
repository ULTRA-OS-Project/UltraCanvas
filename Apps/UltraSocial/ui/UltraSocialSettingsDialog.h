// Apps/UltraSocial/ui/UltraSocialSettingsDialog.h
// The Settings dialog behind the gear on the start page (and in the compose
// toolbar): the sign-in settings every account shares - default Mastodon
// instance and Bluesky server, and the app registrations Reddit, X and
// LinkedIn sign in through. They come set as standard
// (UltraSocialSettings.h); this is where they are changed. A dumb form, like
// the account wizard: saving runs in UltraSocialApp.
// Version: 0.1.0 - sign-in settings set as standard, editable here
// Last Modified: 2026-11-14
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSocialSettings.h"

#include "UltraCanvasWindow.h"

#include <functional>

namespace UltraSocial {

class SettingsDialog {
public:
    // Show `current`; `onSave` receives the edited settings when the user
    // clicks Save (not on Cancel).
    void Show(UltraCanvas::UltraCanvasWindowBase* parent,
              const SignInSettings& current,
              std::function<void(const SignInSettings&)> onSave);
};

} // namespace UltraSocial
