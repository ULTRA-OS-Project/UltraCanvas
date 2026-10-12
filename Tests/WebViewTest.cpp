// Tests/WebViewTest.cpp
// UltraCanvasWebView against a fake web: a page loads and builds, its title
// and URL are reported, links navigate (and a click is followed on the next
// turn, not inside the label's handler), #fragments scroll without a
// reload, back and forward restore pages and their scroll positions, a
// failed load shows an error page, pictures that arrive later rebuild the
// page, a non-HTML answer goes to the host, mailto: links go to the host,
// and a page left before it answered never replaces the next one.
//
// Headless: the view is laid out by the CSSLayout engine on an offscreen
// render context; "the next turn of the UI loop" is a queue the test runs.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraCanvasWebView.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRenderContext.h"
#include "CSSLayout/CSSLayout.h"

#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& what) {
    std::printf("  %s %s\n", cond ? "ok:  " : "FAIL:", what.c_str());
    if (!cond) ++g_failures;
}

bool Resolve(const std::string& base, const std::string& ref, std::string& out) {
    if (ref.find(':') != std::string::npos && ref.find(':') < ref.find('/')) { out = ref; return true; }
    const size_t schemeEnd = base.find("://");
    if (schemeEnd == std::string::npos) return false;
    const size_t pathStart = base.find('/', schemeEnd + 3);
    const std::string origin = base.substr(0, pathStart);
    const std::string noFrag = base.substr(0, base.find('#'));
    if (!ref.empty() && ref[0] == '#') { out = noFrag + ref; return true; }
    if (!ref.empty() && ref[0] == '/') { out = origin + ref; return true; }
    const std::string path = pathStart == std::string::npos ? "/" : noFrag.substr(pathStart);
    out = origin + path.substr(0, path.rfind('/') + 1) + ref;
    return true;
}

struct Web {
    std::map<std::string, HTML::FetchResult> pages;
    std::vector<std::string> asked;
    bool hold = false;
    std::deque<std::pair<std::string, std::function<void(HTML::FetchResult)>>> held;

    void Add(const std::string& url, const std::string& body, const std::string& type = "text/html") {
        HTML::FetchResult r;
        r.ok = true;
        r.status = 200;
        r.contentType = type;
        r.body.assign(body.begin(), body.end());
        pages[url] = r;
    }
    void Answer(const std::string& url, const std::function<void(HTML::FetchResult)>& done) {
        auto it = pages.find(url);
        if (it != pages.end()) { done(it->second); return; }
        HTML::FetchResult r;
        r.error = "could not connect";
        done(r);
    }
    HTML::PageLoaderServices Services() {
        HTML::PageLoaderServices s;
        s.fetch = [this](const std::string& url, std::function<void(HTML::FetchResult)> done) {
            asked.push_back(url);
            if (hold) held.emplace_back(url, std::move(done));
            else Answer(url, done);
        };
        s.resolve = Resolve;
        return s;
    }
    // Answers what is held now; what those answers ask for waits.
    void Release() {
        auto now = std::move(held);
        held.clear();
        for (auto& next : now) Answer(next.first, next.second);
    }
};

struct Host : UltraCanvasContainer {
    Host() : UltraCanvasContainer("host") {}
    void Adopt(std::unique_ptr<IRenderContext> c) { renderContext = std::move(c); }
};

struct Fixture {
    Web web;
    std::deque<std::function<void()>> turn;     // the UI loop's next turn
    std::shared_ptr<Host> host = std::make_shared<Host>();
    std::shared_ptr<UltraCanvasWebView> view = std::make_shared<UltraCanvasWebView>("web");
    std::vector<std::string> urls, titles, external, nonHtml;

    Fixture() {
        host->Adopt(CreateRenderContext(Size2Di(400, 300), nullptr));
        host->AddChild(view);
        view->SetServices(web.Services());
        view->SetScheduler([this](std::function<void()> task) { turn.push_back(std::move(task)); });
        view->onUrlChanged = [this](const std::string& u) { urls.push_back(u); };
        view->onTitleChanged = [this](const std::string& t) { titles.push_back(t); };
        view->onExternalLink = [this](const std::string& u) { external.push_back(u); };
        view->onNonHtml = [this](const HTML::LoadedPage& p) { nonHtml.push_back(p.url); };
    }
    void LayOut() {
        CSSLayout::LayoutContext ctx;
        ctx.viewportWidth = 400;
        ctx.viewportHeight = 300;
        view->size.width = CSSLayout::Dimension::Px(400);
        view->size.height = CSSLayout::Dimension::Px(300);
        CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 400 },
                                          { CSSLayout::ConstraintMode::Exact, 300 } };
        view->Measure(mc, ctx);
        view->Arrange(Rect2Df{ 0, 0, 400, 300 }, ctx);
    }
    // Lays out and runs queued turns until nothing is left.
    void Settle() {
        for (int i = 0; i < 10; ++i) {
            LayOut();
            if (turn.empty()) return;
            auto tasks = std::move(turn);
            turn.clear();
            for (auto& t : tasks) t();
        }
    }
    // The label holding `text`, anywhere in the page.
    UltraCanvasLabel* Label(const std::string& text) {
        std::function<UltraCanvasLabel*(UltraCanvasUIElement*)> find = [&](UltraCanvasUIElement* e) -> UltraCanvasLabel* {
            if (auto* l = dynamic_cast<UltraCanvasLabel*>(e))
                if (l->GetText().find(text) != std::string::npos) return l;
            if (auto* c = dynamic_cast<UltraCanvasContainer*>(e))
                for (auto& child : c->GetChildren())
                    if (auto* found = find(child.get())) return found;
            return nullptr;
        };
        return find(view.get());
    }
    int ScrollY() {
        auto* pane = dynamic_cast<UltraCanvasContainer*>(view->GetChildren().front().get());
        return pane ? pane->GetVerticalScrollPosition() : -1;
    }
};

std::string LongPage(const std::string& title, const std::string& extra = "") {
    std::string html = "<html><head><title>" + title + "</title></head><body>";
    for (int i = 0; i < 60; ++i) html += "<p>Paragraph " + std::to_string(i) + " of " + title + ".</p>";
    html += extra + "<h2 id='end'>The end</h2></body></html>";
    return html;
}

} // namespace

static void TestLoadAndLinks() {
    std::printf("a page loads; links navigate on the next turn\n");
    Fixture f;
    f.web.Add("https://x.org/", "<html><head><title>Home</title></head><body>"
                                "<p><a href='/about'>About us</a> <a href='mailto:me@x.org'>mail</a></p>"
                                "<noscript><p>No script here</p></noscript>"
                                "<template><p>Hidden template</p></template></body></html>");
    f.web.Add("https://x.org/about", "<title>About</title><p>All about us</p>");
    f.view->Navigate("https://x.org/");
    f.Settle();
    Check(f.view->GetTitle() == "Home", "the title is the page's");
    Check(!f.titles.empty() && f.titles.back() == "Home", "and is reported");
    Check(f.Label("About us") != nullptr, "the page is built");
    Check(f.Label("No script here") != nullptr, "<noscript> content is shown");
    Check(f.Label("Hidden template") == nullptr, "<template> content is not");
    Check(f.view->ResolveLink("/about") == "https://x.org/about", "links resolve against the page");

    // A click: followed on the next turn, not inside the handler.
    auto* about = f.Label("About us");
    Check(about && about->onLinkActivated, "the link is clickable");
    if (about && about->onLinkActivated) about->onLinkActivated("/about");
    Check(f.view->GetTitle() == "Home", "nothing happens inside the click");
    f.Settle();
    Check(f.view->GetTitle() == "About", "the next turn navigates");
    Check(f.view->GetUrl() == "https://x.org/about", "to the link's target");
    Check(f.view->CanGoBack() && !f.view->CanGoForward(), "with history");

    f.view->GoBack();
    f.Settle();
    Check(f.view->GetTitle() == "Home", "back");
    f.view->GoForward();
    f.Settle();
    Check(f.view->GetTitle() == "About", "forward");

    f.view->GoBack();
    f.Settle();
    auto* mail = f.Label("mail");
    if (mail && mail->onLinkActivated) mail->onLinkActivated("mailto:me@x.org");
    f.Settle();
    Check(f.external.size() == 1 && f.external[0] == "mailto:me@x.org", "mailto: goes to the host");
    Check(f.view->GetTitle() == "Home", "and the page stays");
}

static void TestFragmentsAndScroll() {
    std::printf("fragments scroll; back restores the scroll position\n");
    Fixture f;
    f.web.Add("https://x.org/long", LongPage("Long"));
    f.web.Add("https://x.org/other", "<title>Other</title><p>Other page</p>");
    f.view->Navigate("https://x.org/long");
    f.Settle();
    Check(f.ScrollY() == 0, "a page opens at the top");

    const size_t fetches = f.web.asked.size();
    f.view->Navigate("https://x.org/long#end");
    f.Settle();
    Check(f.web.asked.size() == fetches, "a #fragment of the page on show is not fetched again");
    const int atEnd = f.ScrollY();
    Check(atEnd > 100, "it scrolls to the target");
    Check(f.view->GetUrl() == "https://x.org/long#end", "and the URL says where");

    f.view->GoBack();
    f.Settle();
    Check(f.ScrollY() == 0, "back to the top of the same page");
    f.view->GoForward();
    f.Settle();
    Check(f.ScrollY() == atEnd, "forward to the fragment");

    f.view->Navigate("https://x.org/other");
    f.Settle();
    f.view->GoBack();
    f.Settle();
    Check(f.view->GetTitle() == "Long", "back to the long page");
    Check(f.ScrollY() == atEnd, "where it was scrolled to");

    // A fragment in the address of a new page.
    Fixture g;
    g.web.Add("https://x.org/long", LongPage("Long"));
    g.view->Navigate("https://x.org/long#end");
    g.Settle();
    Check(g.ScrollY() > 100, "a new page opens at its #fragment");
}

static void TestErrorsAndOther() {
    std::printf("errors, non-HTML answers, leaving a page\n");
    Fixture f;
    f.view->Navigate("https://nowhere.example/");
    f.Settle();
    Check(f.Label("could not be shown") != nullptr, "a failed load shows an error page");
    Check(f.Label("could not connect") != nullptr, "saying why");
    Check(f.Label("nowhere.example") != nullptr, "and where");
    Check(!f.view->IsLoading(), "and is not loading");

    f.web.Add("https://x.org/app.wasm", std::string("\0asm", 4), "application/wasm");
    f.view->Navigate("https://x.org/app.wasm");
    f.Settle();
    Check(f.nonHtml.size() == 1 && f.nonHtml[0] == "https://x.org/app.wasm", "a non-HTML answer goes to the host");

    // A slow page left for a fast one never replaces it.
    f.web.Add("https://slow.org/", "<title>Slow</title><p>slow</p>");
    f.web.Add("https://fast.org/", "<title>Fast</title><p>fast</p>");
    f.web.hold = true;
    f.view->Navigate("https://slow.org/");
    Check(f.view->IsLoading(), "loading");
    f.view->Navigate("https://fast.org/");
    f.web.Release();
    f.Settle();
    Check(f.view->GetTitle() == "Fast", "the page navigated to last is shown");
    Check(!f.view->IsLoading(), "and loading is over");
}

static void TestResourcesRebuild() {
    std::printf("pictures that arrive later rebuild the page\n");
    Fixture f;
    // A 1x1 PNG.
    const unsigned char png[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
        0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0,
        0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
        0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
    f.web.Add("https://x.org/pic", "<title>Pic</title><p>Before</p><img src='dot.png' width='50' height='50'>");
    f.web.Add("https://x.org/dot.png", std::string(reinterpret_cast<const char*>(png), sizeof(png)), "image/png");
    f.web.hold = true;
    f.view->Navigate("https://x.org/pic");
    f.web.Release();                     // the page
    f.Settle();                          // built: the picture is asked for
    Check(!f.web.held.empty(), "the picture is fetched after the page");
    f.web.Release();                     // the picture arrives
    f.Settle();                          // and the page is built again
    std::function<int(UltraCanvasUIElement*)> images = [&](UltraCanvasUIElement* e) {
        int n = dynamic_cast<UltraCanvasImageElement*>(e) ? 1 : 0;
        if (auto* c = dynamic_cast<UltraCanvasContainer*>(e))
            for (auto& child : c->GetChildren()) n += images(child.get());
        return n;
    };
    Check(images(f.view.get()) >= 1, "with the picture in it");
}

int main() {
    UCImage::InitializeImageSubsysterm("WebViewTest");
    TestLoadAndLinks();
    TestFragmentsAndScroll();
    TestErrorsAndOther();
    TestResourcesRebuild();
    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
