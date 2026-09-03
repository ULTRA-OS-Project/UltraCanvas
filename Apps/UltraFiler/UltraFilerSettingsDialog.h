// Apps/UltraFiler/UltraFilerSettingsDialog.h
// UltraFiler settings window: a tree of settings pages on the left (main
// pages with sub pages, e.g. Media Viewer > Transparent Images) and the
// selected page on the right. Changes apply to the running application
// immediately (via the onChanged callback) and are persisted right away.
// Version: 1.2.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework
#pragma once

#include "UltraFilerSettings.h"

#include <functional>

namespace UltraCanvas {

class UltraCanvasWindowBase;

class UltraFilerSettingsDialog {
public:
    // Which page the window opens on. Default is the page it always opened
    // on; the others let a caller point straight at a setting - the file
    // display's Display > Thumbnails / Detail view submenus open the matching
    // list of files this way.
    enum class Page { Default, Thumbnails, DetailView };

    // Opens the settings window (or raises it when already open). `settings`
    // must outlive the dialog; `onChanged` is called after every change so the
    // host can re-apply the settings to its widgets. `onClearHistory`,
    // `onClearFavorites` and `onClearFolderViews` back the Lists page's clear
    // buttons; leaving one empty disables its button.
    static void Show(UltraCanvasWindowBase* parent, UltraFilerSettings* settings,
                     std::function<void()> onChanged,
                     std::function<void()> onClearHistory = {},
                     std::function<void()> onClearFavorites = {},
                     std::function<void()> onClearFolderViews = {},
                     Page initialPage = Page::Default);

    // Releases the retained settings-dialog widget tree. Call during app
    // shutdown so it is torn down while the application is still alive, rather
    // than at static-destruction time when the Application singleton is gone.
    static void Shutdown();
};

} // namespace UltraCanvas
