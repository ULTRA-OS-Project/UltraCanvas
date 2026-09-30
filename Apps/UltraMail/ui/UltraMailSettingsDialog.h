// Apps/UltraMail/ui/UltraMailSettingsDialog.h
// UltraMail settings window, built like UltraFiler's: a tree of settings
// pages on the left (sections with their pages - Reading > Layout, Privacy >
// Images, ...) and the selected page on the right. App-wide options only; an
// account's servers and sign-in stay in its own Account Settings. Changes
// apply at once (through onChanged) and are saved at once.
// Version: 1.0.0
// Last Modified: 2026-09-30
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailPreferences.h"

#include <functional>

namespace UltraCanvas { class UltraCanvasWindowBase; }

namespace UltraMail {

class SettingsDialog {
public:
    // Opens the settings window, or raises it when already open. `prefs` must
    // outlive the window; `onChanged` runs after every change - the host
    // applies the preferences to its views and saves them.
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent, Preferences* prefs,
                     std::function<void()> onChanged);

    // Re-reads the preferences into an open window - after the reading pane
    // changed one ("Always from <sender>" adds a trusted sender). Does nothing
    // while no window is open.
    static void SyncWithPreferences();

    // Releases the retained window while the application is still alive
    // (call after the main loop ends), not at static destruction.
    static void Shutdown();
};

} // namespace UltraMail
