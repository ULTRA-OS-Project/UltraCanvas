// Apps/UltraFiler/UltraFilerSettingsDialog.h
// UltraFiler settings window: a tree of settings pages on the left (main
// pages with sub pages, e.g. Display > Treeview) and the selected page on
// the right. Selecting a main page moves on to its first sub page, since a
// main page has no content of its own. Changes apply to the running
// application immediately (via the onChanged callback) and are persisted
// right away.
// Version: 1.6.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraFilerSettings.h"

#include "UltraCanvasFilerWidget.h"   // ThumbCacheStats, for CacheHooks

#include <functional>

namespace UltraCanvas {

class UltraCanvasWindowBase;

class UltraFilerSettingsDialog {
public:
    // Which page the window opens on. Default is the page it always opened
    // on; the others let a caller point straight at a setting - the file
    // display's Display > Thumbnails / Detail view submenus open the matching
    // list of files this way.
    enum class Page { Default, Thumbnails, DetailView, FileExtensions, Cache };

    // What the Extras > Cache page needs from the running application. The
    // disk cache is process-wide and the page reads it directly; the memory
    // figures belong to whichever file display is in front, which only the
    // host knows. Supplied separately from Show() so the existing callers do
    // not change: a host that sets nothing gets the page with its disk half
    // and the memory half reported as unavailable.
    struct CacheHooks {
        // The in-memory thumbnail figures of the file display in front.
        std::function<UltraCanvasFilerWidget::ThumbCacheStats()> memoryStats;
        // Drop those retained thumbnails (every open display, not just the
        // one in front). Backs the Empty cache button together with the disk
        // cache's own Clear.
        std::function<void()> clearMemory;
        // Apply the two switches to every open file display.
        std::function<void()> apply;
    };
    static void SetCacheHooks(CacheHooks hooks);

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
