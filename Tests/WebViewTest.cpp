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
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasRenderContext.h"
#include "CSSLayout/CSSLayout.h"

#include <cstdio>
#include <deque>
#include <filesystem>
#include <functional>
#include <algorithm>
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
        s.post = [this](const std::string& url, const std::string& type, const std::string& body,
                        std::function<void(HTML::FetchResult)> done) {
            asked.push_back("POST " + url);
            posted.push_back({ type, body });
            Answer(url, done);
        };
        return s;
    }
    std::vector<std::pair<std::string, std::string>> posted;   // content type, body
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

static void TestImportedStyleSheets() {
    std::printf("@import: imported sheets style the page, in cascade order\n");
    Fixture f;
    f.web.Add("https://x.org/doc/page",
              "<html><head><title>Import</title><link rel='stylesheet' href='css/site.css'>"
              "<style>@import 'inline.css';</style></head><body>"
              "<p>One <span class='x'>imported</span> two <span class='z'>overridden</span>"
              " three <span class='p'>printonly</span> four <span class='i'>fromstyle</span></p>"
              "</body></html>");
    f.web.Add("https://x.org/doc/css/site.css",
              "@import \"base.css\";\n@import url(print.css) print;\n.z { color: #222222 }", "text/css");
    f.web.Add("https://x.org/doc/css/base.css", ".x { color: #FF0000 } .z { color: #111111 }", "text/css");
    f.web.Add("https://x.org/doc/css/print.css", ".p { color: #0000FF }", "text/css");
    f.web.Add("https://x.org/doc/inline.css", ".i { color: #00AA00 }", "text/css");
    f.view->Navigate("https://x.org/doc/page");
    f.Settle();
    UltraCanvasLabel* label = f.Label("imported");
    Check(label != nullptr, "the page is built");
    const std::string markup = label ? label->GetText() : std::string();
    Check(markup.find("foreground=\"#FF0000\">imported") != std::string::npos, "an imported sheet's rule applies");
    Check(markup.find("foreground=\"#222222\">overridden") != std::string::npos,
          "the importing sheet's rule wins over the imported one");
    Check(markup.find("#0000FF") == std::string::npos, "a print-only import does not apply on screen");
    Check(markup.find("foreground=\"#00AA00\">fromstyle") != std::string::npos,
          "a <style>'s import is read against the page");
}

static void TestImportsWithoutLoader() {
    std::printf("@import in the builder alone: relative to the importing sheet (an EPUB's folders)\n");
    std::map<std::string, std::string> files = {
        { "OEBPS/css/book.css", "@import 'fonts/faces.css'; @import url(../shared/base.css); .b { color: #333333 }" },
        { "OEBPS/css/fonts/faces.css", "@import '../book.css'; .f { color: #444444 }" },   // a cycle back
        { "OEBPS/shared/base.css", ".s { color: #555555 }" },
    };
    std::vector<std::string> asked;
    HTML::BuildOptions opts;
    opts.resourceLoader = [&](const std::string& href) {
        asked.push_back(href);
        auto it = files.find(href);
        return it == files.end() ? std::vector<uint8_t>{} : std::vector<uint8_t>(it->second.begin(), it->second.end());
    };
    HTML::ElementBuilder builder;
    HTML::BuildResult r = builder.Build(
        "<html><head><link rel='stylesheet' href='OEBPS/css/book.css'></head><body>"
        "<p>a <span class='f'>face</span> b <span class='s'>shared</span> c <span class='b'>book</span></p>"
        "</body></html>", opts);
    std::string markup;
    std::function<void(UltraCanvasUIElement*)> collect = [&](UltraCanvasUIElement* e) {
        if (auto* l = dynamic_cast<UltraCanvasLabel*>(e)) markup += l->GetText();
        if (auto* c = dynamic_cast<UltraCanvasContainer*>(e))
            for (auto& child : c->GetChildren()) collect(child.get());
    };
    if (r.root) collect(r.root.get());
    Check(markup.find("foreground=\"#444444\">face") != std::string::npos, "an import in a subfolder");
    Check(markup.find("foreground=\"#555555\">shared") != std::string::npos, "an import through ..");
    Check(markup.find("foreground=\"#333333\">book") != std::string::npos, "the importing sheet's own rules");
    Check(std::count(asked.begin(), asked.end(), "OEBPS/css/book.css") == 1, "a cycle reads no sheet twice");
}

template <typename T> static T* FindWidget(UltraCanvasUIElement* e) {
    if (auto* t = dynamic_cast<T*>(e)) return t;
    if (auto* c = dynamic_cast<UltraCanvasContainer*>(e))
        for (auto& child : c->GetChildren())
            if (auto* found = FindWidget<T>(child.get())) return found;
    return nullptr;
}

static void TestForms() {
    std::printf("forms: GET as a navigation, POST through the post service\n");
    Fixture f;
    f.web.Add("https://x.org/form",
              "<title>Form</title><form action='/find?old=1#top'><p>Find <input name='q'> <button>Go</button></p></form>"
              "<form action='https://x.org/send' method='post'><input name='msg' value='a b'>"
              "<input type='submit' name='s' value='Send'></form>");
    f.web.Add("https://x.org/find?q=caf%C3%A9+au+lait", "<title>Found</title><p>results</p>");
    f.web.Add("https://x.org/send", "<title>Sent</title><p>thanks</p>");
    f.view->Navigate("https://x.org/form");
    f.Settle();
    auto* input = FindWidget<UltraCanvasTextInput>(f.view.get());
    Check(input != nullptr, "the field is a text input");
    if (!input) return;
    input->SetText("caf\xC3\xA9 au lait");
    Check(input->onEnterPressed && input->onEnterPressed("x"), "Enter submits");
    f.Settle();
    Check(f.view->GetUrl() == "https://x.org/find?q=caf%C3%A9+au+lait",
          "GET: the fields replace the action's query - " + f.view->GetUrl());
    Check(f.view->GetTitle() == "Found", "and the answer is shown");
    f.view->GoBack();
    f.Settle();
    Check(f.view->GetTitle() == "Form", "back to the form");

    // The second form's button: a POST.
    std::vector<UltraCanvasButton*> buttons;
    std::function<void(UltraCanvasUIElement*)> collect = [&](UltraCanvasUIElement* e) {
        if (auto* b = dynamic_cast<UltraCanvasButton*>(e)) buttons.push_back(b);
        if (auto* c = dynamic_cast<UltraCanvasContainer*>(e))
            for (auto& child : c->GetChildren()) collect(child.get());
    };
    collect(f.view.get());
    Check(buttons.size() == 2, "two buttons");
    if (buttons.size() != 2) return;
    buttons[1]->onClick();
    f.Settle();
    Check(!f.web.posted.empty() && f.web.posted.back().first == "application/x-www-form-urlencoded" &&
          f.web.posted.back().second == "msg=a+b&s=Send", "POST: the body, urlencoded, with the button");
    Check(f.view->GetTitle() == "Sent", "the answer is shown");
    Check(f.view->GetUrl() == "https://x.org/send", "at the action's address");

    std::printf("what was typed survives a rebuild (pictures arriving)\n");
    Fixture g;
    const unsigned char png[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
        0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0,
        0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
        0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
    g.web.Add("https://x.org/typed", "<title>T</title><form><input name='q'></form><img src='dot.png' width='9' height='9'>");
    g.web.Add("https://x.org/dot.png", std::string(reinterpret_cast<const char*>(png), sizeof(png)), "image/png");
    g.web.hold = true;
    g.view->Navigate("https://x.org/typed");
    g.web.Release();
    g.Settle();
    auto* before = FindWidget<UltraCanvasTextInput>(g.view.get());
    Check(before != nullptr, "the field");
    if (before) before->SetText("half-typed");
    g.web.Release();                    // the picture: the page is built again
    g.Settle();
    auto* after = FindWidget<UltraCanvasTextInput>(g.view.get());
    Check(after && after != before, "a new field after the rebuild");
    Check(after && after->GetText() == "half-typed", "with the text that was typed");
}

static void TestFileUpload() {
    std::printf("a file upload through the view: multipart POST with the file's bytes\n");
    const std::string path = PathToUtf8(std::filesystem::temp_directory_path() / "ucwebview-upload-test.txt");
    { std::FILE* out = OpenFileUtf8(path, "wb"); if (out) { std::fputs("file body", out); std::fclose(out); } }
    Fixture f;
    f.view->onChooseFiles = [path](bool, const std::string&, std::function<void(std::vector<std::string>)> done) {
        done({ path });
    };
    f.web.Add("https://x.org/up", "<title>Up</title><form action='/recv' method='post' enctype='multipart/form-data'>"
              "<label for='f'>File:</label> <input type='file' id='f' name='upload'> <button>Send</button></form>");
    f.web.Add("https://x.org/recv", "<title>Received</title><p>ok</p>");
    f.view->Navigate("https://x.org/up");
    f.Settle();
    std::vector<UltraCanvasButton*> buttons;
    std::function<void(UltraCanvasUIElement*)> collect = [&](UltraCanvasUIElement* e) {
        if (auto* b = dynamic_cast<UltraCanvasButton*>(e)) buttons.push_back(b);
        if (auto* c = dynamic_cast<UltraCanvasContainer*>(e))
            for (auto& child : c->GetChildren()) collect(child.get());
    };
    collect(f.view.get());
    Check(buttons.size() == 2, "the chooser and the send button");
    if (buttons.size() != 2) return;
    buttons[0]->onClick();                // choose
    buttons[1]->onClick();                // send
    f.Settle();
    Check(!f.web.posted.empty(), "posted");
    if (f.web.posted.empty()) return;
    const auto& [type, body] = f.web.posted.back();
    Check(type.rfind("multipart/form-data; boundary=", 0) == 0, "as multipart");
    Check(body.find("name=\"upload\"; filename=\"ucwebview-upload-test.txt\"\r\nContent-Type: text/plain\r\n\r\nfile body\r\n")
              != std::string::npos, "with the file's name, type and bytes");
    Check(f.view->GetTitle() == "Received", "and the answer is shown");
    std::error_code ec;
    std::filesystem::remove(PathFromUtf8(path), ec);
}

int main() {
    UCImage::InitializeImageSubsysterm("WebViewTest");
    TestLoadAndLinks();
    TestFragmentsAndScroll();
    TestErrorsAndOther();
    TestResourcesRebuild();
    TestImportedStyleSheets();
    TestImportsWithoutLoader();
    TestForms();
    TestFileUpload();
    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
