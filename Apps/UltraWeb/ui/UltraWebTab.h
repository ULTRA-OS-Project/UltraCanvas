// Apps/UltraWeb/ui/UltraWebTab.h
// One tab of the browser window: its own area, which shows a web page (an
// UltraCanvasWebView with its history) or runs a WebAssembly app (the area
// is that app's UC_ROOT_HANDLE), and what the window shows for it - the
// address, the title, the status line, whether back / forward can go. The
// window (UltraWebWindow) puts the area under its tab strip and points its
// toolbar at the active tab.
// Version: 0.1.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "../host/UltraWebGuest.h"
#include "../host/UltraWebLoader.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasWebView.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraWeb {

class UltraWebTab {
public:
    // What every tab is given by its window.
    struct Host {
        // Runs a task on a later turn of the UI loop.
        std::function<void(std::function<void()>)> post;
        // Something about the tab changed: address, title, status, loading,
        // back / forward.
        std::function<void(UltraWebTab& tab)> onChanged;
        // A link led to no page: a file to save.
        std::function<void(UltraWebTab& tab, const UltraCanvas::HTML::LoadedPage& page)> onDownload;
    };

    UltraWebTab(const std::string& id, Host host);
    // The guest first: it removes its elements from the area.
    ~UltraWebTab();
    UltraWebTab(const UltraWebTab&) = delete;
    UltraWebTab& operator=(const UltraWebTab&) = delete;

    // The tab's page, for the window's tab strip to show.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Area() const { return area_; }

    // Open what an address names: an app (about:demo, a .wasm or .wat path
    // or URL) runs, anything else opens as a web page.
    void Navigate(const std::string& address);
    void Reload();
    void GoBack();
    void GoForward();
    bool CanGoBack() const;
    bool CanGoForward() const;

    // The address on show ("" for a new, empty tab).
    const std::string& Address() const { return address_; }
    // The page's title, else its address, else "New tab".
    std::string Title() const;
    // The status line while this tab is the active one.
    const std::string& StatusText() const { return status_; }
    bool IsLoading() const { return showingPage_ && webView_->IsLoading(); }
    // Running an app rather than showing a page.
    bool IsApp() const { return guest_ != nullptr; }

private:
    void RunApp(const std::string& address);
    void OpenPage(const std::string& url);
    void ShowApp(const LoadedApp& app);
    void ShowError(const std::string& title, const std::string& detail);
    void ClearArea();
    void SetStatus(const std::string& text);
    void Changed();

    Host host_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> area_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> errorTitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> errorDetail_;
    // Web pages; in the area while one is shown (showingPage_).
    std::shared_ptr<UltraCanvas::UltraCanvasWebView> webView_;
    bool showingPage_ = false;
    UltraWebLoader loader_;
    std::unique_ptr<UltraWebGuest> guest_;
    std::string address_;
    std::string status_;
    // Deferred work (a guest failure reported on a later turn) checks this
    // before touching the tab.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

} // namespace UltraWeb
