// Apps/UltraWeb/ui/UltraWebWindow.h
// The UltraWeb browser window: a tab strip, an address bar with back /
// forward, the active tab's page or app (UltraWebTab: a web page in an
// UltraCanvasWebView, or the area a WebAssembly app builds its UI into), the
// downloads panel and a status bar.
// Version: 0.7.0 - tabs
// Version: 0.6.0 - a downloads list: a panel above the status bar
// Version: 0.5.0 - downloads: a link to no page is saved through the save dialog
// Version: 0.3.0 - web pages: addresses that are not apps open in a web view
// Version: 0.1.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// The tab first: it brings the WasmHost header, which must come before
// anything that pulls in X11's macros (Success, Status).
#include "UltraWebTab.h"
#include "../host/UltraWebDownloadList.h"
#include "../host/UltraWebDownloads.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"
#include "UltraWebDownloadsPanel.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraWeb {

class UltraWebWindow {
public:
    UltraWebWindow();
    ~UltraWebWindow();

    bool Initialize();
    void Show();
    // Open what an address names in the active tab: an app (about:demo, a
    // .wasm or .wat path or URL) runs, anything else opens as a web page.
    void Navigate(const std::string& address);
    // A new tab, made the active one; it opens `address` ("" leaves it empty
    // with the address bar focused).
    UltraWebTab& NewTab(const std::string& address);

private:
    // The strip asks before it closes a tab: false keeps it (the last one).
    bool CloseTab(int index);
    void CloseActiveTab();
    // The next (+1) or previous (-1) tab, round the ends.
    void SwitchTab(int step);
    UltraWebTab* ActiveTab() const;
    int IndexOf(const UltraWebTab& tab) const;
    // A tab changed: its title on the strip; the toolbar and status when it
    // is the active one.
    void TabChanged(UltraWebTab& tab);
    // The toolbar, status line and window title show the active tab.
    void ShowActiveTab();
    // Ctrl+T, Ctrl+W, Ctrl+Tab / Ctrl+Shift+Tab, Ctrl+L.
    void InstallShortcuts();
    void SetStatus(const std::string& text);
    // A link that led to no page: ask where to save it, then save the bytes
    // the loader has, or download it again to disk when it was too large.
    void SaveDownload(const UltraCanvas::HTML::LoadedPage& page);
    // The downloads panel shown above the status bar, or not.
    void ShowDownloads(bool show);
    // "Downloads", or "Downloads (2)" while two run.
    void UpdateDownloadsButton();
    void LayoutForSize(float width, float height);

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> page_;
    std::shared_ptr<UltraCanvas::UltraCanvasTabbedContainer> tabStrip_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> tabPages_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> address_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> backButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> forwardButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> goButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> reloadButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> downloadsButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> statusBar_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> status_;

    // The tabs, in the strip's order.
    std::vector<std::unique_ptr<UltraWebTab>> tabs_;
    int tabSerial_ = 0;
    // What the address field was last set from (see ShowActiveTab).
    const UltraWebTab* shownTab_ = nullptr;
    std::string shownAddress_;

    // The downloads of this run, the panel that shows them, and the ones in
    // progress (by list id, each until it finishes).
    DownloadList downloadList_;
    std::unique_ptr<UltraWebDownloadsPanel> downloadsPanel_;
    bool downloadsShown_ = false;
    std::map<int, std::shared_ptr<Download>> downloads_;
    // Deferred work (a save dialog answering late) checks this before
    // touching the window.
    std::shared_ptr<bool> alive_;
};

} // namespace UltraWeb
