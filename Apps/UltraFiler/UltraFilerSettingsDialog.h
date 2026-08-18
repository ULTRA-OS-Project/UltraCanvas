// Apps/UltraFiler/UltraFilerSettingsDialog.h
// UltraFiler settings window: a tree of settings pages on the left (main
// pages with sub pages, e.g. Media Viewer > Transparent Images) and the
// selected page on the right. Changes apply to the running application
// immediately (via the onChanged callback) and are persisted right away.
// Version: 1.0.0
// Last Modified: 2026-08-05
// Author: UltraCanvas Framework
#pragma once

#include "UltraFilerSettings.h"

#include <functional>

namespace UltraCanvas {

class UltraCanvasWindowBase;

class UltraFilerSettingsDialog {
public:
    // Opens the settings window (or raises it when already open). `settings`
    // must outlive the dialog; `onChanged` is called after every change so the
    // host can re-apply the settings to its widgets. `onClearHistory` /
    // `onClearFavorites` back the History & Favorites page's clear buttons;
    // leaving one empty disables its button.
    static void Show(UltraCanvasWindowBase* parent, UltraFilerSettings* settings,
                     std::function<void()> onChanged,
                     std::function<void()> onClearHistory = {},
                     std::function<void()> onClearFavorites = {});
};

} // namespace UltraCanvas
