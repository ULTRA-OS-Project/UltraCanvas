// Tests/HTMLMediaTest.cpp
// <video> and <audio> in the HTML reader (HTML::ElementBuilder with
// BuildOptions::mediaFactory): what a page asks the host for - its sources in
// order, poster, controls / autoplay / loop / muted, the size a browser gives
// it - the fallback content when the host makes no player, an <audio> without
// controls that shows nothing, a player inside an inline element and in a
// flex line, and the same element pointer on every build of one document.
// Then UltraCanvasWebView: a local page's <audio> plays in the framework's
// player when this build has a media backend, which a rebuild keeps and
// leaving the page lets go; and an http video on an https page is refused.
//
// Headless: built on an offscreen render context, laid out by CSSLayout.
// Version: 0.1.0
// Last Modified: 2026-10-22
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLElementBuilder.h"
#include "HTMLReader/HTMLParser.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasWebView.h"
#include "CSSLayout/CSSLayout.h"

#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
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

bool Near(float a, float b) { return a > b - 0.5f && a < b + 0.5f; }

// Builds `html` with a factory that records each request and answers with a
// plain container (or null when `refuse`).
struct Built {
    std::vector<HTML::MediaRequest> requests;
    HTML::BuildResult result;
};

Built Build(const std::string& html, bool refuse = false, float viewport = 800.f) {
    Built built;
    HTML::BuildOptions options;
    options.viewportWidth = viewport;
    options.mediaFactory = [&built, refuse](const HTML::MediaRequest& request) -> std::shared_ptr<UltraCanvasUIElement> {
        built.requests.push_back(request);
        if (refuse) return nullptr;
        return std::make_shared<UltraCanvasContainer>("player" + std::to_string(built.requests.size()));
    };
    HTML::ElementBuilder builder;
    built.result = builder.Build(html, options);
    return built;
}

bool HasText(UltraCanvasUIElement* element, const std::string& text) {
    if (auto* label = dynamic_cast<UltraCanvasLabel*>(element))
        if (label->GetText().find(text) != std::string::npos) return true;
    if (auto* container = dynamic_cast<UltraCanvasContainer*>(element))
        for (const auto& child : container->GetChildren())
            if (HasText(child.get(), text)) return true;
    return false;
}

UltraCanvasUIElement* FindId(UltraCanvasUIElement* element, const std::string& id) {
    if (element->GetIdentifier() == id) return element;
    if (auto* container = dynamic_cast<UltraCanvasContainer*>(element))
        for (const auto& child : container->GetChildren())
            if (auto* found = FindId(child.get(), id)) return found;
    return nullptr;
}

void TestRequests() {
    std::printf("what a page asks for\n");
    Built a = Build("<p>Before</p><video src='clip.mp4' controls width='320' poster='p.jpg'></video>");
    Check(a.requests.size() == 1, "one <video>, one request");
    if (a.requests.size() == 1) {
        const HTML::MediaRequest& r = a.requests[0];
        Check(r.video && r.sources.size() == 1 && r.sources[0].src == "clip.mp4", "its src");
        Check(r.controls && !r.autoplay && !r.loop && !r.muted, "controls, nothing else");
        Check(r.poster == "p.jpg", "the poster, unresolved");
        Check(Near(r.width, 320.f) && Near(r.height, 180.f), "width 320 and the height at 16:9");
        Check(r.node != nullptr, "the element it is");
    }
    Check(a.result.root && FindId(a.result.root.get(), "player1") != nullptr, "the factory's element is in the tree");

    Built b = Build("<video autoplay muted loop>"
                    "<source src='clip.webm' type='video/webm'><source src='clip.mp4' type='video/mp4'>"
                    "</video>");
    Check(b.requests.size() == 1 && b.requests[0].sources.size() == 2 && b.requests[0].sources[0].src == "clip.webm" &&
              b.requests[0].sources[0].type == "video/webm" && b.requests[0].sources[1].src == "clip.mp4",
          "<source>s in order, with their types");
    Check(b.requests.size() == 1 && b.requests[0].autoplay && b.requests[0].muted && b.requests[0].loop,
          "autoplay, muted, loop");
    Check(b.requests.size() == 1 && Near(b.requests[0].width, 640.f) && Near(b.requests[0].height, 360.f),
          "no size: 640 x 360");

    Built c = Build("<video src='a.mp4' style='height:90px'></video><video src='b.mp4' width='200' height='100'></video>");
    Check(c.requests.size() == 2 && Near(c.requests[0].width, 160.f) && Near(c.requests[0].height, 90.f),
          "a CSS height alone: the width at 16:9");
    Check(c.requests.size() == 2 && Near(c.requests[1].width, 200.f) && Near(c.requests[1].height, 100.f),
          "width and height attributes as they are");

    Built d = Build("<div style='width:400px'><video src='a.mp4' style='width:100%'></video></div>");
    Check(d.requests.size() == 1 && Near(d.requests[0].width, 400.f) && Near(d.requests[0].height, 225.f),
          "width: 100% of a 400px box");

    Built e = Build("<audio src='song.mp3' controls></audio>");
    Check(e.requests.size() == 1 && !e.requests[0].video && Near(e.requests[0].width, 300.f) &&
              Near(e.requests[0].height, 40.f),
          "an <audio> with controls: 300 x 40");
    Built f = Build("<p>Song</p><audio src='song.mp3'></audio>");
    Check(f.requests.empty(), "an <audio> without controls or autoplay asks for nothing");
}

void TestFallback() {
    std::printf("fallback content\n");
    const std::string html = "<video src='clip.mp4' controls>Your reader cannot play it: "
                             "<a href='clip.mp4'>download the clip</a></video>";
    Built refused = Build(html, /*refuse=*/true);
    Check(refused.requests.size() == 1, "the host was asked");
    Check(refused.result.root && HasText(refused.result.root.get(), "download the clip"),
          "and when it makes no player, the fallback content shows");
    Built played = Build(html);
    Check(played.result.root && !HasText(played.result.root.get(), "download the clip"),
          "with a player, the fallback content does not");

    HTML::ElementBuilder builder;
    HTML::BuildResult plain = builder.Build(html, HTML::BuildOptions());
    Check(plain.root && HasText(plain.root.get(), "download the clip"), "no factory: the fallback content");

    Built none = Build("<video controls>No source here</video>");
    Check(none.requests.empty() && none.result.root && HasText(none.result.root.get(), "No source here"),
          "a <video> without a source shows its fallback without asking");
}

void TestPlacement() {
    std::printf("where the player goes\n");
    Built inSpan = Build("<p>Watch <span><video src='a.mp4' controls></video></span> now</p>");
    Check(inSpan.requests.size() == 1, "a player inside an inline element is built");
    Check(inSpan.result.root && HasText(inSpan.result.root.get(), "Watch") && HasText(inSpan.result.root.get(), "now"),
          "and the text around it stays");

    Built flex = Build("<div style='display:flex'><video src='a.mp4' width='200'></video><p>beside</p></div>");
    Check(flex.requests.size() == 1 && flex.result.root && FindId(flex.result.root.get(), "player1"),
          "a player as a flex item");

    // One parsed document built twice: the same element both times, which
    // is what lets a host keep its player across a rebuild.
    HTML::Parser parser;
    HTML::Document document = parser.Parse("<video src='a.mp4' controls></video>");
    std::vector<const HTML::Node*> nodes;
    HTML::BuildOptions options;
    options.mediaFactory = [&nodes](const HTML::MediaRequest& r) -> std::shared_ptr<UltraCanvasUIElement> {
        nodes.push_back(r.node);
        return std::make_shared<UltraCanvasContainer>("p");
    };
    HTML::ElementBuilder first, second;
    first.BuildDocument(document, options);
    options.viewportWidth = 500.f;
    second.BuildDocument(document, options);
    Check(nodes.size() == 2 && nodes[0] == nodes[1], "the same node on every build of a document");
}

// ===== THE WEB VIEW ==========================================================

struct Host : UltraCanvasContainer {
    Host() : UltraCanvasContainer("host") {}
    void Adopt(std::unique_ptr<IRenderContext> c) { renderContext = std::move(c); }
};

struct Fixture {
    std::map<std::string, std::string> pages;
    std::deque<std::function<void()>> turn;
    std::shared_ptr<Host> host = std::make_shared<Host>();
    std::shared_ptr<UltraCanvasWebView> view = std::make_shared<UltraCanvasWebView>("web");
    float width = 400.f;

    Fixture() {
        host->Adopt(CreateRenderContext(Size2Di(800, 600), nullptr));
        host->AddChild(view);
        HTML::PageLoaderServices services;
        services.fetch = [this](const std::string& url, std::function<void(HTML::FetchResult)> done) {
            HTML::FetchResult r;
            auto it = pages.find(url);
            if (it != pages.end()) {
                r.ok = true;
                r.status = 200;
                r.contentType = "text/html";
                r.body.assign(it->second.begin(), it->second.end());
            } else {
                r.error = "no such page";
            }
            done(r);
        };
        services.resolve = [](const std::string& base, const std::string& ref, std::string& out) {
            if (ref.find("://") != std::string::npos) { out = ref; return true; }
            out = base.substr(0, base.rfind('/') + 1) + ref;
            return true;
        };
        view->SetServices(services);
        view->SetScheduler([this](std::function<void()> task) { turn.push_back(std::move(task)); });
    }
    void Settle() {
        for (int i = 0; i < 10; ++i) {
            CSSLayout::LayoutContext ctx;
            ctx.viewportWidth = width;
            ctx.viewportHeight = 300;
            view->size.width = CSSLayout::Dimension::Px(width);
            view->size.height = CSSLayout::Dimension::Px(300);
            CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, width },
                                              { CSSLayout::ConstraintMode::Exact, 300 } };
            view->Measure(mc, ctx);
            view->Arrange(Rect2Df{ 0, 0, width, 300 }, ctx);
            if (turn.empty()) return;
            auto tasks = std::move(turn);
            turn.clear();
            for (auto& t : tasks) t();
        }
    }
};

// A second of silence as a 16-bit mono WAV, which any media framework plays.
bool WriteWav(const std::filesystem::path& path) {
    const uint32_t rate = 8000, samples = 8000, dataBytes = samples * 2;
    std::vector<uint8_t> bytes;
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) bytes.push_back(uint8_t(v >> (8 * i))); };
    auto put16 = [&](uint16_t v) { bytes.push_back(uint8_t(v)); bytes.push_back(uint8_t(v >> 8)); };
    auto tag = [&](const char* t) { bytes.insert(bytes.end(), t, t + 4); };
    tag("RIFF"); put32(36 + dataBytes); tag("WAVE");
    tag("fmt "); put32(16); put16(1); put16(1); put32(rate); put32(rate * 2); put16(2); put16(16);
    tag("data"); put32(dataBytes);
    bytes.resize(bytes.size() + dataBytes, 0);
    std::FILE* file = OpenFileUtf8(PathToUtf8(path), "wb");
    if (!file) return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    return std::fclose(file) == 0 && ok;
}

void TestWebView() {
    std::printf("the web view plays them\n");
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "HTMLMediaTest";
    std::filesystem::create_directories(dir, ec);
    const bool wrote = WriteWav(dir / "tone.wav");
    Check(wrote, "a WAV to play");
    std::string base = PathToUtf8(dir);
    if (!base.empty() && base[0] != '/') base = "/" + base;
    const std::string pageUrl = "file://" + base + "/page.html";

    Fixture f;
    f.pages[pageUrl] = "<title>Tone</title><p>Listen</p><audio controls src='tone.wav'>"
                       "<a href='tone.wav'>the tone</a></audio>";
    f.pages["file://" + base + "/other.html"] = "<title>Other</title><p>elsewhere</p>";
    f.view->Navigate(pageUrl);
    f.Settle();
    const auto players = f.view->GetMediaPlayers();
    if (players.empty()) {
        // No media framework in this build (the null backend): the reader
        // gets the fallback link instead.
        std::printf("  (no media backend in this build)\n");
        Check(HasText(f.view.get(), "the tone"), "without a backend the fallback content shows");
    } else {
        Check(players.size() == 1, "one player for the page's <audio>");
        Check(!HasText(f.view.get(), "the tone"), "and no fallback");
        UltraCanvasVideoPlayerElement* player = players[0].get();
        f.width = 600.f;            // a new width rebuilds the page
        f.Settle();
        const auto again = f.view->GetMediaPlayers();
        Check(again.size() == 1 && again[0].get() == player, "a rebuild keeps the same player");
        Check(player->GetParentContainer() != nullptr, "in the new tree");
        f.view->Navigate("file://" + base + "/other.html");
        f.Settle();
        Check(f.view->GetMediaPlayers().empty(), "leaving the page lets it go");
    }

    // An https page cannot play an http stream (mixed content); it is not
    // even opened, whatever the backend.
    Fixture g;
    g.pages["https://x.org/watch"] = "<title>Watch</title><video controls src='http://x.org/clip.mp4'>"
                                     "<a href='http://x.org/clip.mp4'>the clip</a></video>";
    g.view->Navigate("https://x.org/watch");
    g.Settle();
    Check(g.view->GetMediaPlayers().empty() && HasText(g.view.get(), "the clip"),
          "an http video on an https page shows its fallback");

    Fixture h;
    h.view->SetMediaPlayback(false);
    h.pages[pageUrl] = f.pages[pageUrl];
    h.view->Navigate(pageUrl);
    h.Settle();
    Check(h.view->GetMediaPlayers().empty() && HasText(h.view.get(), "the tone"),
          "SetMediaPlayback(false): the fallback content");
    std::filesystem::remove_all(dir, ec);
}

} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("HTMLMediaTest");
    TestRequests();
    TestFallback();
    TestPlacement();
    TestWebView();
    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
