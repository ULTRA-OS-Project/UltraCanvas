// Apps/UltraWeb/ui/UltraWebWindow.h
// The UltraWeb browser window: an address bar with back / forward, the area
// a WebAssembly app builds its UI into (UC_ROOT_HANDLE of the element ABI) or
// a web page is shown in (UltraCanvasWebView), and a status bar. One app or
// page at a time in this version; tabs come next.
// Version: 0.6.0 - a downloads list: a panel above the status bar
// Version: 0.5.0 - downloads: a link to no page is saved through the save dialog
// Version: 0.3.0 - web pages: addresses that are not apps open in a web view
// Version: 0.1.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "../host/UltraWebGuest.h"
#include "../host/UltraWebDownloadList.h"
#include "../host/UltraWebDownloads.h"
#include "../host/UltraWebLoader.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWebView.h"
#include "UltraCanvasWindow.h"
#include "UltraWebDownloadsPanel.h"

#include <map>
#include <memory>
#include <string>

namespace UltraWeb {

class UltraWebWindow {
public:
    UltraWebWindow();
    ~UltraWebWindow();

    bool Initialize();
    void Show();
    // Open what an address names: an app (about:demo, a .wasm or .wat path
    // or URL) runs, anything else opens as a web page.
    void Navigate(const std::string& address);

private:
    void RunApp(const std::string& address);
    void OpenPage(const std::string& url);
    void UpdateNavigationButtons();
    void ShowApp(const LoadedApp& app);
    void ShowError(const std::string& title, const std::string& detail);
    void ClearAppArea();
    void SetStatus(const std::string& text);
    // A link that led to no page: ask where to save it, then save the bytes
    // the loader has, or download it again to disk when it was too large.
    void SaveDownload(const UltraCanvas::HTML::LoadedPage& page);
    // The downloads panel shown above the status bar, or not.
    void ShowDownloads(bool show);
    // "Downloads", or "Downloads (2)" while two run.
    void UpdateDownloadsButton();
    void SetTitleFor(const std::string& address);
    void LayoutForSize(float width, float height);

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> page_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> address_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> backButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> forwardButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> goButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> reloadButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> downloadsButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> appArea_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> statusBar_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> status_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> errorTitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> errorDetail_;

    UltraWebLoader loader_;
    std::unique_ptr<UltraWebGuest> guest_;
    // Web pages; in the app area while one is shown (showingPage_).
    std::shared_ptr<UltraCanvas::UltraCanvasWebView> webView_;
    bool showingPage_ = false;
    // The downloads of this run, the panel that shows them, and the ones in
    // progress (by list id, each until it finishes).
    DownloadList downloadList_;
    std::unique_ptr<UltraWebDownloadsPanel> downloadsPanel_;
    bool downloadsShown_ = false;
    std::map<int, std::shared_ptr<Download>> downloads_;
    std::string currentAddress_;
    // Deferred work (a guest failure reported on a later turn) checks this
    // before touching the window.
    std::shared_ptr<bool> alive_;
};

} // namespace UltraWeb
