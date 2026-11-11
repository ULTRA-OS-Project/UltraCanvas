// include/UltraCanvasWebView.h
// A web page as an element: give it a URL and it fetches the page through
// HTML::PageLoader, builds it with the HTML reader into native elements, and
// scrolls it. It keeps the history (back / forward, with each page's scroll
// position), follows links - a #fragment scrolls, http(s) and file links
// navigate, anything else (mailto:, tel:) goes to the host - shows an error
// page when a page cannot be loaded, builds the page again when its width
// changes or its pictures arrive, and reports its URL, title and hovered
// link so a browser frame can show them.
//
// The host supplies the network: SetServices() with a fetch, a URL resolver
// and a charset converter (UltraWeb passes UltraNet's), and SetScheduler()
// with a way to run work on a later turn of the UI loop (PostToUIThread).
// Without a scheduler the view uses the running application's.
//
// No JavaScript: <script> is not run, <noscript> content is shown.
//
// <video> and <audio> play in the framework's video player
// (UltraCanvasVideoPlayerElement, which plays sound too): http(s) streamed by
// the platform's media framework, local files from a local page. The player
// lives as long as its page - a rebuild keeps it playing, leaving the page
// stops it. A page plays nothing by itself unless it is muted (autoplay with
// sound waits for the reader's click, as browsers do).
// Version: 1.6.0 - SetOpensWebLinks: a viewer of local files sends http(s)
//                  links to the host; ScrollBy for a host's keys; Clear
// Version: 1.5.0 - <video> and <audio>: SetMediaPlayback, the players kept
//                  across rebuilds of one page
// Version: 1.4.0 - onOpenInNewWindow: target="_blank" links go to the host
// Version: 1.3.0 - a download keeps the page on show and its history
// Version: 1.2.0 - onChooseFiles: file uploads
// Version: 1.1.0 - SetInteractiveForms / SubmitForm: forms with real widgets,
//                  sent by GET or POST
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "HTMLReader/HTMLElementBuilder.h"
#include "HTMLReader/HTMLPageLoader.h"
#include "UltraCanvasVideoPlayerElement.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace UltraCanvas {

class UltraCanvasWebView : public UltraCanvasContainer {
public:
    explicit UltraCanvasWebView(const std::string& identifier, float w = 0, float h = 0);
    ~UltraCanvasWebView() override;

    // ===== SET-UP =====
    void SetServices(HTML::PageLoaderServices services, HTML::PageLoaderLimits limits = {});
    // Runs a task on a later turn of the UI loop. Rebuilds go through it,
    // never from inside a layout pass.
    void SetScheduler(std::function<void(std::function<void()>)> post);
    // Typography, colours and the reader's own CSS for every page. The
    // resource loader and the link callbacks are the view's own and are
    // replaced.
    void SetBuildOptions(const HTML::BuildOptions& options);
    // Forms the reader can fill in and send (on by default): real widgets
    // (BuildOptions::interactiveForms), sent by GET as a navigation or by
    // POST through PageLoaderServices::post. What was typed survives a
    // rebuild of the page (a new width, pictures arriving). Off: forms are
    // drawn for display only.
    void SetInteractiveForms(bool on) { interactiveForms = on; }
    // Send a form as its submit button would: GET appends the fields to the
    // action as a query, POST sends them in the form's enctype.
    void SubmitForm(const HTML::FormSubmission& submission);
    // <video> and <audio> as players (on by default). Off: their fallback
    // content is shown, as in a reader that plays nothing.
    void SetMediaPlayback(bool on) { mediaPlayback = on; }
    // http(s) links followed in the view (on by default). Off - a viewer of
    // local files with no network, such as the media viewer's HTML preview -
    // they go to onExternalLink as mailto: does, and file: links still open
    // here.
    void SetOpensWebLinks(bool on) { opensWebLinks = on; }
    // The players of the page on show, in no particular order.
    std::vector<std::shared_ptr<UltraCanvasVideoPlayerElement>> GetMediaPlayers() const;
    // Players on one page at most; a <video> past it shows its fallback.
    static constexpr size_t kMaxMediaPerPage = 16;

    // ===== NAVIGATION =====
    void Navigate(const std::string& url);
    void Reload();
    void Stop();
    // Nothing on show: the load in progress stopped, the page, its players
    // and the history let go.
    void Clear();
    bool CanGoBack() const;
    bool CanGoForward() const;
    void GoBack();
    void GoForward();
    // Scrolls to the element with this id (or <a name>); "" is the top.
    bool ScrollToFragment(const std::string& fragment);
    // Scrolls the page by `dy` pixels (down when positive), for a host that
    // keeps the keyboard itself.
    bool ScrollBy(int dy);

    // ===== STATE =====
    std::string GetUrl() const;
    std::string GetTitle() const;
    bool IsLoading() const { return loading; }
    // The page on show (null before the first one).
    const HTML::LoadedPage* GetPage() const { return page.get(); }
    // A link's target as the current page reads it ("" when it cannot).
    std::string ResolveLink(const std::string& href) const;

    // ===== EVENTS =====
    std::function<void(const std::string& url)> onUrlChanged;
    std::function<void(const std::string& title)> onTitleChanged;
    std::function<void(bool loading)> onLoadingChanged;
    // The link under the pointer, as an absolute URL ("" when it leaves).
    std::function<void(const std::string& url)> onLinkHovered;
    // A link the view does not follow itself: mailto:, tel:, another scheme.
    std::function<void(const std::string& url)> onExternalLink;
    // A link that asks for another window - <a target="_blank">, a named
    // target, <base target="_blank"> - with the absolute address it leads
    // to: a browser opens it in a new tab. Unset (or set after the page was
    // built, until the next build), such links open in this view. Only
    // addresses the view would open itself come here (http(s), and file:
    // from a local page); mailto: and the like still go to onExternalLink.
    std::function<void(const std::string& url)> onOpenInNewWindow;
    // A file input's button: show a file dialog (several files when
    // `multiple`; `accept` is the input's accept="" list as written) and
    // call `done` with the chosen UTF-8 paths, none when cancelled. Unset,
    // file inputs cannot choose (their button is disabled).
    std::function<void(bool multiple, const std::string& accept,
                       std::function<void(std::vector<std::string> paths)> done)> onChooseFiles;
    // A navigation that found no page (PageKind::Other - an app, a file to
    // download: page.body, page.fileName, page.truncated): the host decides
    // what to do with it. The page on show stays, with its address and
    // history.
    std::function<void(const HTML::LoadedPage& page)> onNonHtml;

    void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override;

private:
    struct HistoryEntry {
        std::string url;
        std::string title;
        int scrollY = 0;
    };

    void StartLoad(const std::string& url, int restoreScrollY, bool pushedHistory = false);
    void OnPage(HTML::LoadedPage loaded, int restoreScrollY);
    void Build();
    void ScheduleRebuild();
    void OnLink(const std::string& href);
    void OnNewWindowLink(const std::string& href);
    void ShowErrorPage(const std::string& url, const std::string& message);
    void SetLoading(bool value);
    void RememberScroll();
    void Post(std::function<void()> task);
    // BuildOptions::mediaFactory: the page's player for a <video> or
    // <audio>, the one built before for it when the document is the same.
    std::shared_ptr<UltraCanvasUIElement> MakeMedia(const HTML::MediaRequest& request);
    void DropMedia();

    std::unique_ptr<HTML::PageLoader> loader;
    std::function<void(std::function<void()>)> scheduler;
    HTML::BuildOptions baseOptions;
    std::shared_ptr<UltraCanvasContainer> content;      // the scrolling pane
    std::shared_ptr<HTML::LoadedPage> page;
    std::unordered_map<std::string, std::shared_ptr<UltraCanvasUIElement>> anchors;
    // The page's forms, and which document they were built from (a rebuild
    // of the same document puts back what was typed).
    std::vector<std::shared_ptr<HTML::FormController>> forms;
    const HTML::Document* formsDocument = nullptr;
    bool interactiveForms = true;
    bool mediaPlayback = true;
    bool opensWebLinks = true;
    // The page's players by the element they play, and the document they
    // belong to (another document: they are let go).
    std::unordered_map<const HTML::Node*, std::shared_ptr<UltraCanvasVideoPlayerElement>> media;
    const HTML::Document* mediaDocument = nullptr;
    unsigned formCounter = 0;
    std::vector<HistoryEntry> history;
    int historyIndex = -1;
    bool loading = false;
    bool loadPushedHistory = false;   // the load in progress added its history entry
    bool rebuildQueued = false;
    float builtWidth = -1.f;
    std::string pendingFragment;
    int pendingScrollY = -1;
    // Deferred tasks check this before touching the view.
    std::shared_ptr<bool> alive;
};

} // namespace UltraCanvas
