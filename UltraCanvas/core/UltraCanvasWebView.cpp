// core/UltraCanvasWebView.cpp
// A web page as an element: PageLoader + ElementBuilder + history.
// See include/UltraCanvasWebView.h.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraCanvasWebView.h"
#include "UltraCanvasApplication.h"
#include "HTMLReader/HTMLParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace UltraCanvas {

namespace {

std::string WithoutFragment(const std::string& url) {
    const size_t hash = url.find('#');
    return hash == std::string::npos ? url : url.substr(0, hash);
}

std::string FragmentOf(const std::string& url) {
    const size_t hash = url.find('#');
    return hash == std::string::npos ? std::string() : url.substr(hash + 1);
}

std::string SchemeOf(const std::string& url) {
    const size_t colon = url.find(':');
    if (colon == std::string::npos || colon == 0) return std::string();
    std::string scheme = url.substr(0, colon);
    for (char& c : scheme) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && c != '+' && c != '-' && c != '.') return std::string();
        c = static_cast<char>(std::tolower(u));
    }
    return scheme;
}

// "%20" and friends, as a fragment may carry them.
std::string PercentDecoded(const std::string& text) {
    auto hex = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && hex(text[i + 1]) >= 0 && hex(text[i + 2]) >= 0) {
            out += static_cast<char>(hex(text[i + 1]) * 16 + hex(text[i + 2]));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

} // namespace

UltraCanvasWebView::UltraCanvasWebView(const std::string& identifier, float w, float h)
    : UltraCanvasContainer(identifier, w, h), alive(std::make_shared<bool>(true)) {
    layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    ContainerStyle own = GetContainerStyle();
    own.autoShowScrollbars = false;
    SetContainerStyle(own);

    // The page scrolls inside this pane; it reflows to the width, so it never
    // scrolls sideways.
    content = std::make_shared<UltraCanvasContainer>(identifier + "_page");
    content->layoutItem.SetFlexGrow(1.f).SetFlexShrink(1.f);
    ContainerStyle style = content->GetContainerStyle();
    style.autoShowScrollbars = true;
    style.autoShowHorizontalScrollbar = false;
    content->SetContainerStyle(style);
    content->SetBackgroundColor(Colors::White);
    AddChild(content);
}

UltraCanvasWebView::~UltraCanvasWebView() {
    *alive = false;
}

void UltraCanvasWebView::SetServices(HTML::PageLoaderServices services, HTML::PageLoaderLimits limits) {
    loader = std::make_unique<HTML::PageLoader>(std::move(services), limits);
    loader->onResourcesArrived = [this]() { ScheduleRebuild(); };
}

void UltraCanvasWebView::SetScheduler(std::function<void(std::function<void()>)> post) {
    scheduler = std::move(post);
}

void UltraCanvasWebView::SetBuildOptions(const HTML::BuildOptions& options) {
    baseOptions = options;
    if (page) ScheduleRebuild();
}

void UltraCanvasWebView::Post(std::function<void()> task) {
    std::weak_ptr<bool> weak = alive;
    auto guarded = [weak, task = std::move(task)]() {
        auto living = weak.lock();
        if (living && *living) task();
    };
    if (scheduler) { scheduler(std::move(guarded)); return; }
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) { app->PostToUIThread(std::move(guarded)); return; }
    guarded();
}

// ===== NAVIGATION ============================================================

void UltraCanvasWebView::Navigate(const std::string& url) {
    if (url.empty()) return;
    // Another place on the page on show: scroll there, and remember it.
    if (page && !loading && !FragmentOf(url).empty() &&
        WithoutFragment(url) == WithoutFragment(page->url)) {
        RememberScroll();
        history.resize(static_cast<size_t>(historyIndex + 1));
        history.push_back({ url, GetTitle(), 0 });
        ++historyIndex;
        page->url = url;
        ScrollToFragment(FragmentOf(url));
        if (onUrlChanged) onUrlChanged(url);
        return;
    }
    RememberScroll();
    history.resize(static_cast<size_t>(historyIndex + 1));
    history.push_back({ url, std::string(), 0 });
    ++historyIndex;
    StartLoad(url, 0);
}

void UltraCanvasWebView::Reload() {
    if (historyIndex < 0) return;
    RememberScroll();
    StartLoad(history[static_cast<size_t>(historyIndex)].url, history[static_cast<size_t>(historyIndex)].scrollY);
}

void UltraCanvasWebView::Stop() {
    if (loader) loader->Cancel();
    SetLoading(false);
}

bool UltraCanvasWebView::CanGoBack() const { return historyIndex > 0; }
bool UltraCanvasWebView::CanGoForward() const {
    return historyIndex >= 0 && static_cast<size_t>(historyIndex + 1) < history.size();
}

void UltraCanvasWebView::GoBack() {
    if (!CanGoBack()) return;
    RememberScroll();
    --historyIndex;
    const HistoryEntry& entry = history[static_cast<size_t>(historyIndex)];
    if (page && !loading && WithoutFragment(entry.url) == WithoutFragment(page->url)) {
        page->url = entry.url;                  // the same document: just scroll
        pendingScrollY = entry.scrollY;
        content->ScrollToVertical(entry.scrollY);
        if (onUrlChanged) onUrlChanged(entry.url);
        RequestRedraw();
        return;
    }
    StartLoad(entry.url, entry.scrollY);
}

void UltraCanvasWebView::GoForward() {
    if (!CanGoForward()) return;
    RememberScroll();
    ++historyIndex;
    const HistoryEntry& entry = history[static_cast<size_t>(historyIndex)];
    if (page && !loading && WithoutFragment(entry.url) == WithoutFragment(page->url)) {
        page->url = entry.url;
        if (!FragmentOf(entry.url).empty()) ScrollToFragment(FragmentOf(entry.url));
        else content->ScrollToVertical(entry.scrollY);
        if (onUrlChanged) onUrlChanged(entry.url);
        RequestRedraw();
        return;
    }
    StartLoad(entry.url, entry.scrollY);
}

void UltraCanvasWebView::RememberScroll() {
    if (historyIndex < 0 || !page) return;
    history[static_cast<size_t>(historyIndex)].scrollY = content->GetVerticalScrollPosition();
}

void UltraCanvasWebView::StartLoad(const std::string& url, int restoreScrollY) {
    if (onUrlChanged) onUrlChanged(url);
    if (!loader) {
        ShowErrorPage(url, "This view has no way to fetch pages (no services were set).");
        return;
    }
    SetLoading(true);
    loader->Load(url, [this, restoreScrollY](HTML::LoadedPage loaded) {
        OnPage(std::move(loaded), restoreScrollY);
    });
}

void UltraCanvasWebView::OnPage(HTML::LoadedPage loaded, int restoreScrollY) {
    SetLoading(false);
    if (loaded.kind == HTML::PageKind::Other) {
        if (onNonHtml) onNonHtml(loaded);
        return;
    }
    if (loaded.kind == HTML::PageKind::Error || !loaded.document) {
        ShowErrorPage(loaded.url, loaded.error.empty() ? "The page could not be shown." : loaded.error);
        return;
    }
    page = std::make_shared<HTML::LoadedPage>(std::move(loaded));
    if (historyIndex >= 0) {
        HistoryEntry& entry = history[static_cast<size_t>(historyIndex)];
        entry.url = page->url;                  // where a redirect ended
        entry.title = GetTitle();
    }
    if (onUrlChanged) onUrlChanged(page->url);
    if (onTitleChanged) onTitleChanged(GetTitle());
    Build();
    const std::string fragment = FragmentOf(page->url);
    if (!fragment.empty() && restoreScrollY <= 0) pendingFragment = fragment;
    else pendingScrollY = std::max(0, restoreScrollY);
    content->ScrollToVertical(0);
}

void UltraCanvasWebView::ShowErrorPage(const std::string& url, const std::string& message) {
    const std::string html =
        "<!DOCTYPE html><html><head><title>Page not shown</title></head>"
        "<body style='margin:40px;font-family:sans-serif;color:#333'>"
        "<h1 style='font-size:22px'>This page could not be shown</h1>"
        "<p style='color:#666'>" + HTML::EscapeText(url) + "</p>"
        "<p>" + HTML::EscapeText(message) + "</p></body></html>";
    auto error = std::make_shared<HTML::LoadedPage>();
    error->kind = HTML::PageKind::Error;
    error->url = url;
    error->error = message;
    HTML::Parser parser;
    error->document = std::make_shared<HTML::Document>(parser.Parse(html));
    page = error;
    SetLoading(false);
    if (onTitleChanged) onTitleChanged(GetTitle());
    Build();
    content->ScrollToVertical(0);
}

void UltraCanvasWebView::SetLoading(bool value) {
    if (loading == value) return;
    loading = value;
    if (onLoadingChanged) onLoadingChanged(value);
}

// ===== BUILDING ==============================================================

void UltraCanvasWebView::Build() {
    if (!page || !page->document) return;
    HTML::BuildOptions options = baseOptions;
    const float width = content->GetWidth() > 0 ? static_cast<float>(content->GetWidth())
                                                : static_cast<float>(GetWidth());
    if (width > 0.f) options.viewportWidth = width;
    // An error page never reaches the network.
    const bool live = page->kind != HTML::PageKind::Error;
    options.resourceLoader = [this, live](const std::string& href) {
        return (live && loader) ? loader->GetResource(href) : std::vector<uint8_t>{};
    };
    // A click rebuilds the page under the label being clicked: follow it on
    // the next turn, not from inside the label's own handler.
    options.onLinkActivated = [this](const std::string& href) {
        Post([this, href]() { OnLink(href); });
    };
    options.onLinkHovered = [this](const std::string& href) {
        if (onLinkHovered) onLinkHovered(href.empty() ? std::string() : ResolveLink(href));
    };

    HTML::ElementBuilder builder;
    HTML::BuildResult result = builder.BuildDocument(*page->document, options);
    content->ClearChildren();
    anchors.clear();
    if (result.root) {
        result.root->size.width = CSSLayout::Dimension::Pct(100.f);
        content->AddChild(result.root);
        anchors = std::move(result.anchors);
    }
    builtWidth = width;
    RequestRedraw();
}

void UltraCanvasWebView::ScheduleRebuild() {
    if (rebuildQueued) return;
    rebuildQueued = true;
    Post([this]() {
        rebuildQueued = false;
        const int y = content->GetVerticalScrollPosition();
        Build();
        pendingScrollY = y;
    });
}

void UltraCanvasWebView::Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) {
    UltraCanvasContainer::Arrange(finalRect, ctx);
    // A new width: build again at it (later - never from inside layout),
    // so @media rules and auto-fill grids follow the window.
    const float width = static_cast<float>(content->GetWidth());
    if (page && width > 0.f && std::fabs(width - builtWidth) > 1.f) ScheduleRebuild();
    if (!pendingFragment.empty()) {
        const std::string fragment = std::move(pendingFragment);
        pendingFragment.clear();
        ScrollToFragment(fragment);
    } else if (pendingScrollY >= 0) {
        const int y = pendingScrollY;
        pendingScrollY = -1;
        content->ScrollToVertical(y);
    }
}

bool UltraCanvasWebView::ScrollToFragment(const std::string& fragment) {
    if (fragment.empty() || fragment == "top") {
        content->ScrollToVertical(0);
        RequestRedraw();
        return true;
    }
    auto it = anchors.find(fragment);
    if (it == anchors.end()) it = anchors.find(PercentDecoded(fragment));
    if (it == anchors.end() || !it->second) return false;
    // finalBounds are relative to the parent: the offset is the sum up the
    // chain to the scrolling pane.
    float y = 0.f;
    CSSLayout::Element* element = it->second.get();
    while (element && element != content.get()) {
        y += element->finalBounds.y;
        element = element->Parent();
    }
    if (!element) return false;
    y -= content->GetBorderTopWidth() + content->GetPaddingTop();
    content->ScrollToVertical(std::max(0, static_cast<int>(y)));
    RequestRedraw();
    return true;
}

// ===== LINKS =================================================================

std::string UltraCanvasWebView::ResolveLink(const std::string& href) const {
    if (!page) return std::string();
    if (page->kind == HTML::PageKind::Error) return href;
    return loader ? loader->Resolve(href) : std::string();
}

void UltraCanvasWebView::OnLink(const std::string& href) {
    const std::string target = ResolveLink(href);
    if (target.empty()) return;
    const std::string scheme = SchemeOf(target);
    if (scheme == "http" || scheme == "https") { Navigate(target); return; }
    if (scheme == "file") {
        // Only a local page may open local files.
        if (page && SchemeOf(page->url) == "file") Navigate(target);
        return;
    }
    if (scheme == "javascript" || scheme == "data" || scheme.empty()) return;
    if (onExternalLink) onExternalLink(target);
}

std::string UltraCanvasWebView::GetUrl() const {
    if (historyIndex >= 0) return history[static_cast<size_t>(historyIndex)].url;
    return page ? page->url : std::string();
}

std::string UltraCanvasWebView::GetTitle() const {
    if (!page) return std::string();
    if (page->document && !page->document->title.empty()) return page->document->title;
    return page->url;
}

} // namespace UltraCanvas
