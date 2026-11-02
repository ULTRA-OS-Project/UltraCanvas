// Apps/UltraSnip/ui/UltraSnipSettingsDialog.h
// UltraSnip's settings window, laid out as UltraFiler's: a tree of settings
// pages on the left (Pictures > File type, Pictures > Saving, Videos >
// Recording, Behaviour > After a snip) and the chosen page on the right,
// with the page's "Restore defaults" and Close at the foot. Every change
// applies at once (onChanged) and is saved; there is nothing to confirm.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSnipSettings.h"

#include <functional>

namespace UltraCanvas { class UltraCanvasWindowBase; }

namespace UltraSnip {

class UltraSnipSettingsDialog {
public:
    // Opens the settings window, or raises the open one. `settings` must
    // outlive it; `onChanged` runs after every change.
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent, UltraSnipSettings* settings,
                     std::function<void()> onChanged);
    // Closes the window and lets its widgets go: call before the
    // application ends, while it can still tear a window down.
    static void Shutdown();
};

} // namespace UltraSnip
