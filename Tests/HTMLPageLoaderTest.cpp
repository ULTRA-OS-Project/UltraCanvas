// Tests/HTMLPageLoaderTest.cpp
// HTML::PageLoader offline: a fake web (URL -> response) stands in for
// UltraNet, answering at once or when the test releases it. Covers charset
// detection, <base href>, style sheets fetched before the page is ready
// (with their url()s made absolute), resources on demand and their cache,
// the scheme and mixed-content rules, data: URLs, text and image pages,
// errors, and a navigation that is left before it answers.
// Framework-independent: builds against the HTMLReader sources only.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLPageLoader.h"
#include "HTMLReader/HTMLDocument.h"

#include <cstdio>
#include <deque>
#include <map>
#include <string>
#include <vector>

using namespace UltraCanvas::HTML;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

namespace {

std::vector<uint8_t> Bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

// Enough of RFC 3986 section 5 for the tests (UltraWeb uses UltraNet_ResolveUrl).
bool TestResolve(const std::string& base, const std::string& ref, std::string& out) {
    if (ref.find("://") != std::string::npos || ref.rfind("data:", 0) == 0 ||
        ref.rfind("mailto:", 0) == 0 || ref.rfind("javascript:", 0) == 0) { out = ref; return true; }
    const size_t schemeEnd = base.find("://");
    if (schemeEnd == std::string::npos) return false;
    const size_t pathStart = base.find('/', schemeEnd + 3);
    const std::string origin = base.substr(0, pathStart);
    std::string path = pathStart == std::string::npos ? "/" : base.substr(pathStart);
    path = path.substr(0, path.find_first_of("?#"));
    if (ref.rfind("//", 0) == 0) { out = base.substr(0, schemeEnd + 1) + ref; return true; }
    if (!ref.empty() && ref[0] == '#') { out = base.substr(0, base.find('#')) + ref; return true; }
    if (!ref.empty() && ref[0] == '?') { out = origin + path + ref; return true; }
    std::string joined = (!ref.empty() && ref[0] == '/') ? ref : path.substr(0, path.rfind('/') + 1) + ref;
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= joined.size()) {
        size_t j = joined.find('/', i);
        if (j == std::string::npos) j = joined.size();
        const std::string seg = joined.substr(i, j - i);
        if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (seg != "." && !(seg.empty() && j != joined.size())) parts.push_back(seg);
        i = j + 1;
    }
    out = origin;
    for (const auto& p : parts) out += "/" + p;
    if (parts.empty()) out += "/";
    return true;
}

struct FakeWeb {
    std::map<std::string, FetchResult> pages;
    std::vector<std::string> asked;
    bool hold = false;                                   // answer only on Release()
    std::deque<std::pair<std::string, std::function<void(FetchResult)>>> held;

    void Add(const std::string& url, const std::string& body, const std::string& type = "text/html",
             int status = 200) {
        FetchResult r;
        r.ok = true;
        r.status = status;
        r.contentType = type;
        r.body = Bytes(body);
        pages[url] = r;
    }
    void Answer(const std::string& url, const std::function<void(FetchResult)>& done) {
        auto it = pages.find(url);
        if (it == pages.end()) {
            FetchResult r;
            r.ok = false;
            r.error = "no such host";
            done(r);
        } else {
            done(it->second);
        }
    }
    PageLoaderServices Services() {
        PageLoaderServices s;
        s.fetch = [this](const std::string& url, std::function<void(FetchResult)> done) {
            asked.push_back(url);
            if (hold) held.emplace_back(url, std::move(done));
            else Answer(url, done);
        };
        s.resolve = TestResolve;
        return s;
    }
    // Answers what is held now; what those answers ask for waits.
    void Release() {
        auto now = std::move(held);
        held.clear();
        for (auto& next : now) Answer(next.first, next.second);
    }
};

LoadedPage LoadNow(PageLoader& loader, const std::string& url) {
    LoadedPage result;
    bool called = false;
    loader.Load(url, [&](LoadedPage page) { result = std::move(page); called = true; });
    CHECK(called);
    return result;
}

} // namespace

static void TestCharsets() {
    std::printf("charsets\n");
    CHECK(PageLoader::DetectCharset(Bytes("\xEF\xBB\xBFhi"), "") == "utf-8");
    CHECK(PageLoader::DetectCharset(Bytes("hi"), "text/html; charset=ISO-8859-2") == "iso-8859-2");
    CHECK(PageLoader::DetectCharset(Bytes("hi"), "text/html; charset=\"Shift_JIS\"") == "shift_jis");
    CHECK(PageLoader::DetectCharset(Bytes("<html><head><meta charset='windows-1251'>"), "text/html") == "windows-1251");
    CHECK(PageLoader::DetectCharset(
              Bytes("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=koi8-r\">"), "") == "koi8-r");
    CHECK(PageLoader::DetectCharset(Bytes("<meta charset=utf-16>"), "") == "utf-8");  // ASCII bytes are not utf-16
    CHECK(PageLoader::DetectCharset(Bytes("caf\xC3\xA9"), "") == "utf-8");
    CHECK(PageLoader::DetectCharset(Bytes("caf\xE9"), "") == "windows-1252");

    // Without a converter a Latin-1 page still comes out as valid UTF-8.
    FakeWeb web;
    web.Add("https://x.org/", "<p>caf\xE9</p>", "text/html; charset=iso-8859-1");
    PageLoader loader(web.Services());
    LoadedPage page = LoadNow(loader, "https://x.org/");
    CHECK(page.kind == PageKind::Html);
    CHECK(page.charset == "iso-8859-1");
    CHECK(page.document && page.document->root &&
          page.document->root->TextContent().find("caf\xC3\xA9") != std::string::npos);

    // With one, the page's charset goes to it.
    std::string seen;
    PageLoaderServices services = web.Services();
    services.toUtf8 = [&](const std::string& bytes, const std::string& cs) { seen = cs; return bytes; };
    web.Add("https://x.org/jp", "<p>x</p>", "text/html; charset=Shift_JIS");
    PageLoader converting(services);
    LoadNow(converting, "https://x.org/jp");
    CHECK(seen == "shift_jis");
}

static void TestBaseAndStyleSheets() {
    std::printf("base href and style sheets\n");
    FakeWeb web;
    web.Add("https://x.org/docs/page.html",
            "<html><head><base href='/static/'>"
            "<link rel='stylesheet' href='site.css'>"
            "<link rel='stylesheet' href='//cdn.x.org/theme.css'></head>"
            "<body><img src='logo.png'></body></html>");
    web.Add("https://x.org/static/site.css",
            "body{background:url(img/bg.png)} /* url(not/this.png) */ .a{background-image:url('../up.png')}",
            "text/css");
    web.Add("https://cdn.x.org/theme.css", "p{color:red}", "text/css");
    web.Add("https://x.org/static/logo.png", "PNG", "image/png");
    PageLoader loader(web.Services());
    LoadedPage page = LoadNow(loader, "https://x.org/docs/page.html");
    CHECK(page.kind == PageKind::Html);
    CHECK(page.baseUrl == "https://x.org/static/");
    CHECK(web.asked.size() == 3);                                 // the page and both sheets
    // The builder asks for a sheet by the href the document wrote.
    const std::vector<uint8_t> css = loader.GetResource("site.css");
    const std::string text(css.begin(), css.end());
    CHECK(text.find("url(\"https://x.org/static/img/bg.png\")") != std::string::npos);
    CHECK(text.find("url(\"https://x.org/up.png\")") != std::string::npos);
    CHECK(text.find("/* url(not/this.png) */") != std::string::npos);   // comments untouched
    CHECK(!loader.GetResource("//cdn.x.org/theme.css").empty());
    CHECK(web.asked.size() == 3);                                 // both answered from the cache
}

static void TestImports() {
    std::printf("@import\n");
    FakeWeb web;
    web.Add("https://x.org/p/page.html",
            "<html><head><link rel='stylesheet' href='css/a.css'>"
            "<style>@import '/s.css'; p{}</style></head><body></body></html>");
    web.Add("https://x.org/p/css/a.css", "@import \"b.css\";\n@import url(sub/c.css) print;\nh1{color:red}", "text/css");
    web.Add("https://x.org/p/css/b.css", "@import url(a.css); b{color:blue}", "text/css");   // a cycle
    web.Add("https://x.org/p/css/sub/c.css", "c{background:url(i.png)}", "text/css");
    web.Add("https://x.org/s.css", "s{color:green}", "text/css");

    // Not ready until every imported sheet is in: answer one round at a time.
    web.hold = true;
    PageLoader loader(web.Services());
    bool ready = false;
    LoadedPage page;
    loader.Load("https://x.org/p/page.html", [&](LoadedPage p) { page = std::move(p); ready = true; });
    web.Release();                         // the page: asks for a.css and /s.css
    CHECK(!ready);
    web.Release();                         // a.css and s.css: a asks for b and c
    CHECK(!ready);
    web.Release();                         // b and c (b's import of a is a repeat)
    CHECK(ready);
    CHECK(web.held.empty());
    CHECK(web.asked.size() == 5);          // page, a, s, b, c - each once
    CHECK(page.warnings.empty());

    // The importing sheet names its imports by absolute address now.
    const std::vector<uint8_t> a = loader.GetResource("css/a.css");
    const std::string text(a.begin(), a.end());
    CHECK(text.find("@import url(\"https://x.org/p/css/b.css\")") != std::string::npos);
    CHECK(text.find("url(\"https://x.org/p/css/sub/c.css\") print") != std::string::npos);
    const std::vector<uint8_t> c = loader.GetResource("https://x.org/p/css/sub/c.css");
    CHECK(std::string(c.begin(), c.end()).find("https://x.org/p/css/sub/i.png") != std::string::npos);
    CHECK(!loader.GetResource("https://x.org/s.css").empty());
    CHECK(web.asked.size() == 5);          // from the cache

    // Imports nest at most 4 deep below the page's sheets.
    FakeWeb deep;
    deep.Add("https://d.org/", "<link rel='stylesheet' href='/0.css'>");
    for (int k = 0; k < 7; ++k)
        deep.Add("https://d.org/" + std::to_string(k) + ".css",
                 "@import '" + std::to_string(k + 1) + ".css';", "text/css");
    PageLoader deepLoader(deep.Services());
    LoadedPage deepPage = LoadNow(deepLoader, "https://d.org/");
    CHECK(deep.asked.size() == 6);         // the page, 0.css and four imports
    bool warned = false;
    for (const auto& w : deepPage.warnings) warned |= w.find("nested too deep") != std::string::npos;
    CHECK(warned);

    // An https page imports no http sheet.
    FakeWeb mixed;
    mixed.Add("https://m.org/", "<style>@import 'http://m.org/x.css';</style>");
    mixed.Add("http://m.org/x.css", "p{}", "text/css");
    PageLoader mixedLoader(mixed.Services());
    LoadedPage mixedPage = LoadNow(mixedLoader, "https://m.org/");
    CHECK(mixed.asked.size() == 1);
    CHECK(!mixedPage.warnings.empty());
}

static void TestResourcesOnDemand() {
    std::printf("resources on demand\n");
    FakeWeb web;
    web.Add("https://x.org/", "<img src='a.png'><img src='b.png'>");
    web.Add("https://x.org/a.png", "AAAA", "image/png");
    PageLoader loader(web.Services());
    LoadNow(loader, "https://x.org/");

    web.hold = true;
    int arrived = 0;
    loader.onResourcesArrived = [&]() { ++arrived; };
    CHECK(loader.GetResource("a.png").empty());                   // not here yet: queued
    CHECK(loader.PendingResources() == 1);
    CHECK(loader.GetResource("a.png").empty());                   // asked once only
    CHECK(web.held.size() == 1);
    loader.GetResource("b.png");                                  // a 404 (no such page)
    web.Release();
    CHECK(arrived == 2);
    CHECK(loader.PendingResources() == 0);
    CHECK(loader.GetResource("a.png") == Bytes("AAAA"));
    CHECK(loader.GetResource("b.png").empty());
    CHECK(web.held.empty());                                      // a failure is not asked again
}

static void TestRules() {
    std::printf("scheme and mixed-content rules\n");
    FakeWeb web;
    web.Add("https://x.org/", "<p>secure</p>");
    web.Add("http://x.org/a.png", "A", "image/png");
    web.Add("http://plain.org/", "<p>plain</p>");
    web.Add("http://plain.org/b.png", "B", "image/png");
    web.Add("file:///home/u/page.html", "<p>local</p>", "");
    web.Add("file:///home/u/c.png", "C", "");
    PageLoader loader(web.Services());

    LoadNow(loader, "https://x.org/");
    CHECK(loader.GetResource("http://x.org/a.png").empty());      // http on an https page
    CHECK(loader.GetResource("file:///etc/passwd").empty());      // a web page cannot read files
    CHECK(loader.GetResource("ftp://x.org/f").empty());
    CHECK(loader.GetResource("javascript:alert(1)").empty());
    for (const auto& url : web.asked) CHECK(url.rfind("https://", 0) == 0);

    // An http page may load http resources; the first ask queues the
    // fetch, the next one is answered from the cache.
    LoadNow(loader, "http://plain.org/");
    CHECK(loader.GetResource("b.png").empty());
    CHECK(loader.GetResource("b.png") == Bytes("B"));

    LoadedPage local = LoadNow(loader, "file:///home/u/page.html");
    CHECK(local.kind == PageKind::Html);                          // sniffed from .html
    loader.GetResource("c.png");
    CHECK(loader.GetResource("c.png") == Bytes("C"));             // a local page may read local files

    // data: URLs, base64 and percent-encoded.
    std::vector<uint8_t> out;
    CHECK(PageLoader::DecodeDataUrl("data:image/png;base64,QUJD", out) && out == Bytes("ABC"));
    CHECK(PageLoader::DecodeDataUrl("data:text/plain,a%20b", out) && out == Bytes("a b"));
    CHECK(!PageLoader::DecodeDataUrl("data:nocomma", out));
    CHECK(loader.GetResource("data:,hi") == Bytes("hi"));
}

static void TestKinds() {
    std::printf("text, image, other and error pages\n");
    FakeWeb web;
    web.Add("https://x.org/notes.txt", "a < b & c", "text/plain");
    web.Add("https://x.org/pic.png", "PNGDATA", "image/png");
    web.Add("https://x.org/app.wasm", "\0asm", "application/wasm");
    web.Add("https://x.org/missing", "<h1>Not found</h1>", "text/html", 404);
    PageLoader loader(web.Services());

    LoadedPage text = LoadNow(loader, "https://x.org/notes.txt");
    CHECK(text.kind == PageKind::Text);
    CHECK(text.document && text.document->root->TextContent().find("a < b & c") != std::string::npos);

    LoadedPage image = LoadNow(loader, "https://x.org/pic.png");
    CHECK(image.kind == PageKind::Image);
    CHECK(loader.GetResource("https://x.org/pic.png") == Bytes("PNGDATA"));   // no second fetch
    CHECK(image.document && image.document->root->FindFirst("img") != nullptr);

    LoadedPage other = LoadNow(loader, "https://x.org/app.wasm");
    CHECK(other.kind == PageKind::Other);
    CHECK(other.contentType == "application/wasm");
    CHECK(!other.document);

    LoadedPage notFound = LoadNow(loader, "https://x.org/missing");
    CHECK(notFound.kind == PageKind::Html);                       // the server's own error page
    CHECK(notFound.status == 404);

    LoadedPage error = LoadNow(loader, "https://nowhere.example/");
    CHECK(error.kind == PageKind::Error);
    CHECK(error.error == "no such host");

    PageLoaderLimits small;
    small.maxDocumentBytes = 4;
    PageLoader tight(web.Services(), small);
    LoadedPage big = LoadNow(tight, "https://x.org/missing");
    CHECK(big.kind == PageKind::Error);
}

static void TestLeavingAPage() {
    std::printf("a navigation left before it answers\n");
    FakeWeb web;
    web.Add("https://slow.org/", "<p>slow</p>");
    web.Add("https://fast.org/", "<p>fast</p>");
    web.hold = true;
    PageLoader loader(web.Services());
    std::vector<std::string> ready;
    loader.Load("https://slow.org/", [&](LoadedPage p) { ready.push_back(p.url); });
    loader.Load("https://fast.org/", [&](LoadedPage p) { ready.push_back(p.url); });
    CHECK(loader.IsLoading());
    web.Release();
    CHECK(ready.size() == 1 && ready[0] == "https://fast.org/");
    CHECK(!loader.IsLoading());

    // A redirect: the page is where it ended up.
    FakeWeb moved;
    moved.Add("https://old.org/", "<p>new home</p>");
    moved.pages["https://old.org/"].finalUrl = "https://new.org/home";
    PageLoader redirected(moved.Services());
    LoadedPage page = LoadNow(redirected, "https://old.org/");
    CHECK(page.url == "https://new.org/home");
    CHECK(redirected.Resolve("img.png") == "https://new.org/img.png");

    // A #fragment is not sent, and stays with the page's address.
    FakeWeb frag;
    frag.Add("https://x.org/doc", "<h2 id='s'>S</h2>");
    PageLoader fragments(frag.Services());
    LoadedPage withFragment = LoadNow(fragments, "https://x.org/doc#s");
    CHECK(frag.asked.size() == 1 && frag.asked[0] == "https://x.org/doc");
    CHECK(withFragment.url == "https://x.org/doc#s");
    CHECK(withFragment.baseUrl == "https://x.org/doc");

    // A loader destroyed before its fetch answers: the answer is dropped.
    FakeWeb later;
    later.Add("https://x.org/", "<p>x</p>");
    later.hold = true;
    bool called = false;
    {
        PageLoader gone(later.Services());
        gone.Load("https://x.org/", [&](LoadedPage) { called = true; });
    }
    later.Release();
    CHECK(!called);
}

static void TestLimits() {
    std::printf("limits\n");
    FakeWeb web;
    std::string html = "<head>";
    for (int i = 0; i < 40; ++i) html += "<link rel=stylesheet href='s" + std::to_string(i) + ".css'>";
    html += "</head><body>";
    for (int i = 0; i < 20; ++i) html += "<img src='i" + std::to_string(i) + ".png'>";
    web.Add("https://x.org/", html);
    PageLoaderLimits limits;
    limits.maxStyleSheets = 5;
    limits.maxResources = 3;
    PageLoader loader(web.Services(), limits);
    LoadedPage page = LoadNow(loader, "https://x.org/");
    CHECK(web.asked.size() == 1 + 5);
    CHECK(!page.warnings.empty());
    for (int i = 0; i < 20; ++i) loader.GetResource("i" + std::to_string(i) + ".png");
    CHECK(web.asked.size() == 1 + 5 + 3);
}

int main() {
    TestCharsets();
    TestBaseAndStyleSheets();
    TestImports();
    TestResourcesOnDemand();
    TestRules();
    TestKinds();
    TestLeavingAPage();
    TestLimits();
    std::printf("%s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}
