// Apps/UltraMail/ui/UltraMailSettingsDialog.h
// UltraMail settings window, built like UltraFiler's: a tree of settings
// pages on the left (sections with their pages - Reading > Layout, Privacy >
// Images, ...) and the selected page on the right. App-wide options only; an
// account's servers and sign-in stay in its own Account Settings. Changes
// apply at once (through onChanged) and are saved at once.
// Version: 1.1.0 - MakeGearButton (toolbar and start page)
// Last Modified: 2026-09-30
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailPreferences.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
class UltraCanvasWindowBase;
class UltraCanvasButton;
}

namespace UltraMail {

class SettingsDialog {
public:
    // Opens the settings window, or raises it when already open. `prefs` must
    // outlive the window; `onChanged` runs after every change - the host
    // applies the preferences to its views and saves them.
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent, Preferences* prefs,
                     std::function<void()> onChanged);

    // The gear that opens this window - UltraFiler's gear tool button: icon
    // only, 30 wide, white with a thin border. The toolbar and the start page
    // both use it, so it looks the same wherever it is.
    static std::shared_ptr<UltraCanvas::UltraCanvasButton> MakeGearButton(
        const std::string& id, float height, std::function<void()> onClick);

    // Re-reads the preferences into an open window - after the reading pane
    // changed one ("Always from <sender>" adds a trusted sender). Does nothing
    // while no window is open.
    static void SyncWithPreferences();

    // Releases the retained window while the application is still alive
    // (call after the main loop ends), not at static destruction.
    static void Shutdown();
};

} // namespace UltraMail
