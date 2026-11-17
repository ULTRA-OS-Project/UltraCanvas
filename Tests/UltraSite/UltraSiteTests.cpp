// Tests/UltraSite/UltraSiteTests.cpp
// UltraSite without a window: a fixture site is built into a temporary
// folder and read back. Every page is a whole document with its head, its
// frame and its body; the stylesheet and the scripts carry their content
// hash and change name only when their content does; a behaviour module
// reaches exactly the pages that ask for it; a broken link or anchor and a
// page over budget fail the build; what an earlier build left is removed.
// Version: 0.3.0 - the swap navigation
// Version: 0.2.0 - Markdown (front matter, ids, admonitions, tabs, wrapped lines, highlighting), AddDocs
//                  (folders, categories, links, translations), the doc layout, the search index
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSite/UltraSite.h"
#include "UltraSite/UltraSiteMarkdown.h"
#include "UltraSite/UltraSiteServer.h"
#include "UltraNet/UltraNetSocket.h"

#include "HTMLReader/HTMLDocument.h"
#include "HTMLReader/HTMLParser.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace UltraSite;
namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

static bool Contains(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

static fs::path TempDir(const std::string& name) {
    static const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / ("ultrasite-test-" + name + "-" + std::to_string(stamp));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

static std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static void Write(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

static bool HasError(const BuildResult& result, const std::string& needle) {
    return std::any_of(result.errors.begin(), result.errors.end(), [&](const std::string& e) { return Contains(e, needle); });
}

static void PrintErrors(const BuildResult& result) {
    for (const std::string& error : result.errors) std::printf("  error: %s\n", error.c_str());
}

// The site every test starts from: two languages of tokens, a navbar with a
// submenu, a footer, three pages (one a post) and a media folder.
static SiteConfig FixtureConfig() {
    SiteConfig config;
    config.url = "https://example.org";
    config.name = "Example";
    config.tagline = "A fixture";
    config.tokens.light = {{"bg", "#ffffff"}, {"fg", "#111111"}, {"accent", "#a90707"}};
    config.tokens.dark = {{"bg", "#101014"}, {"fg", "#eeeeee"}, {"accent", "#ff6b6b"}};
    config.navbar = {{"Home", "/", {}}, {"About", "/about/", {{"Team", "/about/#team", {}}}}, {"Blog", "/blog/hello/", {}}};
    config.footer = {{"About", "/about/", {}}, {"Source", "https://example.com/source", {}}};
    config.footerText = "Example Ltd";
    config.extraCss = ".hero{padding:2rem}";
    config.noPrefetch = {"/media/"};
    return config;
}

static void AddFixtureRoutes(Builder& builder) {
    Route home;
    home.path = "/";
    home.title = "Example";
    home.description = "The front page";
    home.bodyHtml = "<section class=\"hero\"><h1>Welcome</h1><p><a href=\"/about/#team\">The team</a></p></section>\n"
                    "<img src=\"/media/pic.png\" alt=\"A picture\" width=\"2\" height=\"2\">\n";
    home.jsonLd = {"{\"@context\":\"https://schema.org\",\"@type\":\"WebSite\",\"name\":\"</script><b>x\"}"};
    home.alternates = {{"de", "/de/"}};
    builder.AddRoute(home);

    Route about;
    about.path = "/about/";
    about.title = "About";
    about.description = "Who we are";
    about.date = "2026-11-17";
    about.bodyHtml = "<h1>About</h1>\n<h2 id=\"team\">Team</h2>\n<div data-us=\"tabs\"><p>One</p><p>Two</p></div>\n"
                     "<p><a href=\"../blog/hello/\">Relative</a> <a href=\"#team\">Up</a></p>\n";
    builder.AddRoute(about);

    Route post;
    post.path = "/blog/hello/";
    post.layout = "post";
    post.kind = "article";
    post.title = "Hello";
    post.date = "2026-11-01";
    post.bodyHtml = "<p>The first post.</p>\n";
    builder.AddRoute(post);

    Route german;
    german.path = "/de/";
    german.language = "de";
    german.title = "Beispiel";
    german.bodyHtml = "<h1>Willkommen</h1>\n";
    german.alternates = {{"en", "/"}};
    builder.AddRoute(german);
}

static void TestHelpers() {
    std::printf("Helpers\n");
    Check(RouteFile("/") == "index.html" && RouteFile("/a/b/") == "a/b/index.html" && RouteFile("/x.html") == "x.html" &&
              RouteFile("/a") == "a/index.html",
          "RouteFile maps a route path to its file");
    Check(ContentHash("abc").size() == 10 && ContentHash("abc") == ContentHash("abc") && ContentHash("abc") != ContentHash("abd"),
          "ContentHash is ten hex digits and follows the content");
    std::string big(4000, 'a');
    Check(CompressedSize(big) <= big.size(), "a repetitive text compresses (or stays its size without zlib)");

    std::vector<std::string> errors;
    Tokens tokens;
    tokens.light = {{"bg", "#fff"}, {"Bad Name", "x"}, {"fg", "red;}body{display:none"}};
    tokens.dark = {{"bg", "#000"}};
    const std::string css = TokensCss(tokens, errors);
    Check(Contains(css, ":root{--us-bg:#fff;}") && errors.size() == 2, "a bad token name and an injecting value are refused");
    Check(Contains(css, "@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--us-bg:#000;color-scheme:dark;}}") &&
              Contains(css, ":root[data-theme=dark]{--us-bg:#000;color-scheme:dark;}"),
          "the dark tokens follow the system unless the visitor chose");

    const std::string minified = MinifyCss("/* note */\na  >  b {\n  color : red ;\n  content: \"a  /* b */\";\n}\n");
    Check(minified == "a>b{color : red;content: \"a  /* b */\"}", "MinifyCss drops comments and spaces, keeps strings: " + minified);
    Check(StripScript("// a comment\n  const a = 1;\n\n  // another\nlet b = 'http://x';\n") == "const a = 1;\nlet b = 'http://x';\n",
          "StripScript drops comment lines and indentation, keeps // inside code");
}

static void TestBuild() {
    std::printf("Build a site\n");
    const fs::path dir = TempDir("build");
    const fs::path media = dir / "source-media";
    Write(media / "pic.png", "not really a picture");
    Write(media / "docs" / "guide.pdf", "pdf");
    const fs::path out = dir / "out";

    Builder builder(FixtureConfig());
    builder.AddDirectory(UltraCanvas::PathToUtf8(media), "media");
    builder.AddFile("feed.xml", "<rss/>");
    builder.AddScript("tabs", "// tabs\nfor (const el of document.querySelectorAll('[data-us~=\"tabs\"]')) el.dataset.ready = '1';\n");
    builder.AddScript("unused", "console.log('never');\n");
    AddFixtureRoutes(builder);
    BuildResult result = builder.Build(UltraCanvas::PathToUtf8(out));
    PrintErrors(result);
    Check(result.ok, "the fixture site builds");
    Check(fs::exists(out / "index.html") && fs::exists(out / "about" / "index.html") && fs::exists(out / "blog" / "hello" / "index.html") &&
              fs::exists(out / "de" / "index.html") && fs::exists(out / "404.html") && fs::exists(out / "sitemap.xml") &&
              fs::exists(out / "robots.txt") && fs::exists(out / ".htaccess") && fs::exists(out / "_headers") &&
              fs::exists(out / "feed.xml") && fs::exists(out / "media" / "pic.png") && fs::exists(out / "media" / "docs" / "guide.pdf"),
          "every page and every file is written");

    const std::string home = Read(out / "index.html");
    UltraCanvas::HTML::Parser parser;
    UltraCanvas::HTML::Document document = parser.Parse(home);
    int h1 = 0;
    std::string cssHref, coreSrc;
    bool themeScript = false, rules = false;
    if (document.root) {
        document.root->ForEachElement([&](UltraCanvas::HTML::Node& node) {
            if (node.tag == "h1") ++h1;
            if (node.tag == "link" && node.GetAttribute("rel") == "stylesheet") cssHref = node.GetAttribute("href");
            if (node.tag == "script" && node.GetAttribute("type") == "module" && Contains(node.GetAttribute("src"), "/core.")) {
                coreSrc = node.GetAttribute("src");
            }
            if (node.tag == "script" && !node.HasAttribute("type") && !node.HasAttribute("src")) themeScript = true;
            if (node.tag == "script" && node.GetAttribute("type") == "speculationrules") rules = true;
            return true;
        });
    }
    Check(document.title == "Example" && h1 == 1, "the front page has the site's title and one <h1>");
    Check(Contains(home, "<html lang=\"en\">") && Contains(home, "<meta name=\"description\" content=\"The front page\">") &&
              Contains(home, "<link rel=\"canonical\" href=\"https://example.org/\">") &&
              Contains(home, "hreflang=\"de\" href=\"https://example.org/de/\"") && Contains(home, "hreflang=\"en\" href=\"https://example.org/\""),
          "the head carries the description, the canonical link and both languages");
    Check(!cssHref.empty() && cssHref.rfind("/assets/site.", 0) == 0 && fs::exists(out / cssHref.substr(1)), "the stylesheet is hashed and written");
    Check(!coreSrc.empty() && fs::exists(out / coreSrc.substr(1)), "the core module is linked and written");
    Check(themeScript && rules && Contains(home, "data-us-theme-toggle hidden"), "a site with a dark look has the theme script and a hidden toggle");
    Check(Contains(home, "http-equiv=\"Content-Security-Policy\"") == Contains(Read(out / "_headers"), "Content-Security-Policy"),
          "the policy is in the page and the host's headers alike");
    Check(!Contains(home, "</script><b>") && Contains(home, "<\\/script><b>x"), "JSON-LD cannot close its <script> early");
    Check(Contains(home, "{\"not\":{\"href_matches\":\"/media/*\"}}"), "the speculation rules skip the paths the app excludes");
    Check(Contains(home, "<a href=\"/\" aria-current=\"page\">Home</a>") && Contains(home, "rel=\"noopener\">Source</a>") &&
              Contains(home, "<p>Example Ltd</p>"),
          "the navbar marks the current page; the footer has its links and its line");

    const std::string css = Read(out / cssHref.substr(1));
    Check(Contains(css, "--us-accent:#a90707") && Contains(css, ".hero{padding:2rem}") && !Contains(css, "/*"),
          "the stylesheet holds the base, the tokens and the app's rules, minified");

    const std::string about = Read(out / "about" / "index.html");
    Check(Contains(about, "/assets/js/tabs.") && !Contains(home, "/assets/js/tabs."), "a behaviour module is linked only where it is used");
    bool tabsWritten = false, unusedWritten = false;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(out / "assets")) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("tabs.", 0) == 0) tabsWritten = true;
        if (name.rfind("unused.", 0) == 0) unusedWritten = true;
    }
    Check(tabsWritten && !unusedWritten, "only the modules some page uses are written");

    const std::string post = Read(out / "blog" / "hello" / "index.html");
    Check(Contains(post, "<article class=\"us-post\">\n<h1>Hello</h1>") && Contains(post, "<time datetime=\"2026-11-01\">") &&
              Contains(post, "og:type\" content=\"article\""),
          "the post layout writes the title, the date and the article type");
    Check(Contains(Read(out / "404.html"), "content=\"noindex\""), "the 404 page is written and not indexed");
    const std::string sitemap = Read(out / "sitemap.xml");
    Check(Contains(sitemap, "<loc>https://example.org/about/</loc><lastmod>2026-11-17</lastmod>") && !Contains(sitemap, "404"),
          "the sitemap lists the pages with their dates, not the 404 page");
    Check(Contains(Read(out / ".htaccess"), "ErrorDocument 404 /404.html") && Contains(Read(out / "_headers"), "immutable"),
          "the host's rules cache the hashed files and serve the 404 page");

    std::printf("Rebuild\n");
    Write(out / ".deploy.json", "{}");
    Write(out / "stray" / "old.html", "old");
    BuildResult again = builder.Build(UltraCanvas::PathToUtf8(out));
    Check(again.ok && again.removed.size() == 2 && !fs::exists(out / "stray") && !fs::exists(out / ".deploy.json"),
          "a rebuild removes what it did not write, and the folders left empty");
    Write(out / ".deploy.json", "{}");
    BuildOptions keep;
    keep.keep = {".deploy.json"};
    builder.Build(UltraCanvas::PathToUtf8(out), keep);
    Check(fs::exists(out / ".deploy.json"), "a kept file survives");

    SiteConfig changed = FixtureConfig();
    changed.extraCss = ".hero{padding:3rem}";
    Builder second(changed);
    second.AddDirectory(UltraCanvas::PathToUtf8(media), "media");
    second.AddScript("tabs", "for (const el of document.querySelectorAll('[data-us~=\"tabs\"]')) el.dataset.ready = '1';\n");
    AddFixtureRoutes(second);
    BuildResult third = second.Build(UltraCanvas::PathToUtf8(out), keep);
    PrintErrors(third);
    Check(third.ok && !fs::exists(out / cssHref.substr(1)) && fs::exists(out / coreSrc.substr(1)) &&
              std::any_of(third.removed.begin(), third.removed.end(), [&](const std::string& r) { return "/" + r == cssHref; }),
          "a changed stylesheet gets a new name and the old one goes; the unchanged core keeps its name");
    Check(!third.SizeReport().empty() && !third.sizes.empty(), "the size report lists what was measured");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

static void TestChecks() {
    std::printf("Links, anchors, behaviours, budgets\n");
    const fs::path dir = TempDir("checks");
    const fs::path media = dir / "source-media";
    Write(media / "pic.png", "not really a picture");
    Builder builder(FixtureConfig());
    builder.AddDirectory(UltraCanvas::PathToUtf8(media), "media");
    builder.AddScript("tabs", "1;\n");
    AddFixtureRoutes(builder);
    Route broken;
    broken.path = "/broken/";
    broken.title = "Broken";
    broken.bodyHtml = "<p><a href=\"/missing/\">x</a> <a href=\"/about/#nobody\">y</a> <a href=\"/about#team\">z</a>"
                      " <img src=\"/media/none.png\" alt=\"\"> <a href=\"https://elsewhere.example/\">w</a>"
                      " <a href=\"mailto:a@example.org\">m</a></p><div data-us=\"carousel\"></div>\n";
    builder.AddRoute(broken);
    const BuildResult result = builder.Build(UltraCanvas::PathToUtf8(dir / "out"));
    Check(!result.ok, "a site with broken links does not build");
    Check(HasError(result, "/broken/: broken link to /missing/"), "a missing page is reported");
    Check(HasError(result, "/broken/: broken anchor /about/#nobody"), "a missing anchor is reported");
    Check(!HasError(result, "/about#team"), "a page without its trailing slash and an anchor that exists pass");
    Check(HasError(result, "broken link to /media/none.png") && !HasError(result, "/media/pic.png"),
          "a missing picture is reported, one in the copied folder is not");
    Check(!HasError(result, "elsewhere") && !HasError(result, "mailto"), "outside links are not checked");
    Check(HasError(result, "data-us=\"carousel\" names a behaviour no module provides"), "an unknown behaviour is reported");

    SiteConfig tight = FixtureConfig();
    tight.budgets.pageHtml = 200;
    Builder small(tight);
    small.AddDirectory(UltraCanvas::PathToUtf8(media), "media");
    small.AddScript("tabs", "1;\n");
    AddFixtureRoutes(small);
    const BuildResult over = small.Build(UltraCanvas::PathToUtf8(dir / "small"));
    Check(!over.ok && HasError(over, "over its budget"), "a page over budget fails the build");
    BuildOptions lenient;
    lenient.enforceBudgets = false;
    const BuildResult warned = small.Build(UltraCanvas::PathToUtf8(dir / "small"), lenient);
    Check(warned.ok && !warned.warnings.empty(), "with budgets not enforced it is a warning");

    SiteConfig light = FixtureConfig();
    light.tokens.dark.clear();
    Builder plain(light);
    Route only;
    only.path = "/";
    only.title = "Example";
    only.bodyHtml = "<h1>Hi</h1>";
    const std::string page = plain.RenderPage(only);
    Check(!Contains(page, "data-us-theme-toggle") && Contains(page, "name=\"color-scheme\" content=\"light\"") &&
              !Contains(page, "localStorage"),
          "a site without a dark look has no toggle and no theme script");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

static void TestMarkdown() {
    std::printf("Markdown\n");
    const FrontMatter matter = ParseFrontMatter("---\ntitle: \"Hello: world\"\nsidebar_position: 3\n---\n# Body\n");
    Check(matter.present && matter.values.at("title") == "Hello: world" && matter.values.at("sidebar_position") == "3" &&
              matter.body == "# Body\n",
          "front matter is read, its quotes removed");
    Check(!ParseFrontMatter("---\nno end\n").present && ParseFrontMatter("Plain").body == "Plain", "text without front matter is the body");

    Check(Slugify("Second: one & two") == "second-one--two", "a heading's id as GitHub spells it");
    Check(Slugify("Library links \xE2\x80\x94 websites") == "library-links--websites", "a dash is dropped, as GitHub drops it");
    Check(Slugify("\xC3\x9C" "ber \xC3\xA4hnliche Dinge") == "\xC3\xBC" "ber-\xC3\xA4hnliche-dinge", "letters beyond ASCII are kept, in lower case");
    HeadingIds ids;
    Check(ids.Next("Usage") == "usage" && ids.Next("Usage") == "usage-1" && ids.Next("Usage") == "usage-2", "a repeated heading is numbered");

    const std::string code = HighlightCode("cpp", "int main() { return 0; } // done\n", "Copy", "Copied");
    Check(Contains(code, "<pre class=\"us-code\" data-us=\"copy\" data-copy-label=\"Copy\" data-copied-label=\"Copied\" data-language=\"cpp\">") &&
              Contains(code, "<code class=\"language-cpp\">") && Contains(code, "<span class=\"tok-k\">return</span>") &&
              Contains(code, "<span class=\"tok-c\">// done</span>"),
          "code is highlighted when the site is built, with a copy button");
    Check(Contains(HighlightCode("no-such-language", "a < b"), "<code class=\"language-no-such-language\">a &lt; b</code>"),
          "an unknown language is plain, escaped");

    MarkdownOptions options;
    std::vector<std::string> linked;
    options.link = [&](const std::string& target) {
        linked.push_back(target);
        return "/docs/" + target.substr(0, target.find('.')) + "/";
    };
    const MarkdownResult result = MarkdownToHtml(
        "# The title\n\nA paragraph\nwrapped over\nthree lines.\n\n## First part\n\nSee [the other](other.md) and "
        "[outside](https://example.org) and [here](#first-part).\n\n:::warning Mind this\nIt **matters**.\n:::\n\n"
        "> [!TIP]\n> A hint\n> on two lines.\n\n:::tabs\n::tab[One]\nFirst.\n::tab[Two]\n```js\nconst a = 1;\n```\n:::\n\n"
        "### Deeper\n\n- an item\n  that wraps\n- another\n",
        options);
    Check(result.title == "The title", "the first h1 is the title");
    Check(Contains(result.html, "<p>A paragraph wrapped over three lines.</p>"), "wrapped lines are one paragraph");
    Check(Contains(result.html, "<li>an item that wraps</li>"), "a wrapped list item is one item");
    Check(Contains(result.html, "<h2 id=\"first-part\">First part</h2>") && Contains(result.html, "<h3 id=\"deeper\">"),
          "headings carry their ids");
    Check(result.toc.size() == 2 && result.toc[0].id == "first-part" && result.toc[1].level == 3, "the h2 and h3 are the table of contents");
    Check(linked.size() == 1 && linked[0] == "other.md" && Contains(result.html, "href=\"/docs/other/\"") &&
              Contains(result.html, "href=\"https://example.org\"") && Contains(result.html, "href=\"#first-part\""),
          "a relative link goes through the caller; an outside link and an anchor stay");
    Check(Contains(result.html, "<div class=\"us-admonition us-admonition-warning\" role=\"note\">\n<p class=\"us-admonition-title\">Mind this</p>") &&
              Contains(result.html, "<b>matters</b>"),
          "an admonition with its title and its Markdown");
    Check(Contains(result.html, "us-admonition-tip") && Contains(result.html, "A hint on two lines."), "GitHub's alert is an admonition");
    Check(Contains(result.html, "<div class=\"us-tabs\" data-us=\"tabs\">") && Contains(result.html, "<section class=\"us-tab\" data-label=\"Two\">") &&
              Contains(result.html, "<p class=\"us-tab-label\">One</p>") && Contains(result.html, "<span class=\"tok-k\">const</span>"),
          "tabs: one section per tab, labelled, readable without script");
}

static void TestDocs() {
    std::printf("Docs\n");
    const fs::path dir = TempDir("docs");
    const fs::path docs = dir / "docs";
    Write(docs / "index.md", "# Welcome\n\nThe start. Read [the guide](guide/install.md#before-you-start).\n");
    Write(docs / "guide" / "_category_.json", "{\"label\": \"The guide\", \"position\": 1}");
    Write(docs / "guide" / "install.md",
          "---\nsidebar_position: 2\n---\n# Install\n\n## Before you start\n\nSee [usage](usage.md), "
          "[the source](../../src/main.cpp) and [a file](files/notes.txt).\n\n## Steps\n\nRun it.\n");
    Write(docs / "guide" / "usage.md", "---\nsidebar_position: 1\nsidebar_label: Use it\n---\n# Usage\n\nUse it. Back [home](../index.md).\n");
    Write(docs / "guide" / "files" / "notes.txt", "notes");
    Write(docs / "reference" / "api.md", "# API\n\nThe calls.\n");
    Write(docs / "draft.md", "---\ndraft: true\n---\n# Not yet\n");
    Write(docs / "CHANGELOG.md", "# Changes\n");
    Write(dir / "src" / "main.cpp", "int main() {}");
    Write(dir / "de" / "index.md", "# Willkommen\n\nDer Anfang.\n");

    SiteConfig config = FixtureConfig();
    config.search = true;
    config.languageNames = {{"de", "Deutsch"}, {"en", "English"}};
    config.navbar = {{"Docs", "/docs/", {}}};
    config.footer.clear();
    Route front;
    front.path = "/";
    front.title = "Example";
    front.bodyHtml = "<h1>Example</h1>\n<p><a href=\"/docs/\">The docs</a></p>\n";
    Builder builder(config);
    builder.AddRoute(front);
    DocsOptions options;
    options.folder = UltraCanvas::PathToUtf8(docs);
    options.editUrl = "https://example.com/edit/docs/";
    options.sourceRoot = UltraCanvas::PathToUtf8(dir);
    options.sourceUrl = "https://example.com/blob/";
    options.exclude = {"CHANGELOG.md"};
    options.translations = {{"de", UltraCanvas::PathToUtf8(dir / "de"), "/de/docs/"}};
    const DocsReport report = builder.AddDocs(options);
    for (const std::string& warning : report.warnings) std::printf("  warning: %s\n", warning.c_str());
    Check(report.errors.empty() && report.warnings.empty() && report.pages == 5, "the docs load: four pages and one translation");

    const fs::path out = dir / "out";
    const BuildResult result = builder.Build(UltraCanvas::PathToUtf8(out));
    PrintErrors(result);
    Check(result.ok, "the docs site builds, every link between the files resolved");
    Check(fs::exists(out / "docs" / "index.html") && fs::exists(out / "docs" / "guide" / "install" / "index.html") &&
              fs::exists(out / "docs" / "guide" / "usage" / "index.html") && fs::exists(out / "docs" / "reference" / "api" / "index.html") &&
              fs::exists(out / "de" / "docs" / "index.html"),
          "a page per file, at its path without .md");
    Check(!fs::exists(out / "docs" / "draft") && !fs::exists(out / "docs" / "CHANGELOG"), "drafts and excluded files are left out");
    Check(fs::exists(out / "docs" / "guide" / "index.html") && fs::exists(out / "docs" / "reference" / "index.html"),
          "a folder without an index page gets one");

    const std::string install = Read(out / "docs" / "guide" / "install" / "index.html");
    Check(Contains(install, "<div class=\"us-doc\">") && Contains(install, "<aside class=\"us-sidebar\" id=\"us-sidebar\">"),
          "the doc layout: a sidebar beside the page");
    Check(Contains(install, "href=\"/docs/guide/usage/\"") && Contains(install, "href=\"https://example.com/blob/src/main.cpp\"") &&
              Contains(install, "href=\"/assets/notes."),
          "links: another doc to its page, the source to sourceUrl, a file in the folder to an asset");
    Check(Contains(install, "<a href=\"/docs/guide/install/\" aria-current=\"page\">Install</a>"), "the sidebar marks the page");
    const size_t use = install.find(">Use it</a>"), inst = install.find("aria-current=\"page\">Install</a>");
    Check(use != std::string::npos && inst != std::string::npos && use < inst, "the sidebar follows sidebar_position and sidebar_label");
    Check(Contains(install, "<nav class=\"us-breadcrumbs\"") && Contains(install, "<a href=\"/docs/guide/\">The guide</a>"),
          "the breadcrumbs name the category from _category_.json");
    Check(Contains(install, "rel=\"prev\"><small>Previous</small><span>Use it</span>") && Contains(install, "rel=\"next\""),
          "previous and next follow the sidebar");
    Check(Contains(install, "data-us=\"toc\"") && Contains(install, "<a href=\"#before-you-start\">Before you start</a>") &&
              Contains(install, "/assets/js/toc."),
          "the table of contents, with its module");
    Check(Contains(install, "href=\"https://example.com/edit/docs/guide/install.md\""), "the edit link");
    Check(Contains(install, "data-us=\"search\"") && Contains(install, "/assets/js/search."), "the search box on every page");

    const std::string home = Read(out / "docs" / "index.html");
    Check(Contains(home, "href=\"/docs/guide/install/#before-you-start\""), "a link with an anchor keeps it");
    Check(Contains(home, "<nav class=\"us-langs\"") && Contains(home, "hreflang=\"de\" lang=\"de\">Deutsch</a>") &&
              Contains(Read(out / "de" / "docs" / "index.html"), "<html lang=\"de\">"),
          "a translated page: the language switcher, the page in its language");
    const std::string reference = Read(out / "docs" / "reference" / "index.html");
    Check(Contains(reference, "<ul class=\"us-cards\">") && Contains(reference, "href=\"/docs/reference/api/\""),
          "the generated index lists the folder's pages");

    // The search: an index per language, a page that lists every page.
    std::string index;
    for (const fs::directory_entry& entry : fs::directory_iterator(out / "search")) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("en.", 0) == 0) index = Read(entry.path());
    }
    Check(Contains(index, "[\"Install\",\"/docs/guide/install/\",") && Contains(index, "[\"before-you-start\",\"Before you start\""),
          "the index holds the pages, their sections and their text");
    Check(fs::exists(out / "search" / "index.html") && Contains(Read(out / "search" / "index.html"), "<ul class=\"us-site-index\">"),
          "the search page lists every page for a browser without script");

    // An index over budget keeps titles and headings only.
    SiteConfig small = config;
    small.budgets.searchIndex = 60;
    Builder tight(small);
    tight.AddRoute(front);
    tight.AddDocs(options);
    BuildOptions lenient;
    lenient.enforceBudgets = false;
    const BuildResult trimmed = tight.Build(UltraCanvas::PathToUtf8(dir / "tight"), lenient);
    Check(std::any_of(trimmed.warnings.begin(), trimmed.warnings.end(),
                      [](const std::string& w) { return Contains(w, "titles, headings and descriptions only"); }),
          "a search index too large drops the texts and says so");

    // A link to a file that is not there is reported and fails the build;
    // as a warning it does not.
    Write(docs / "reference" / "broken.md", "# Broken\n\nSee [nothing](missing.md).\n");
    Builder broken(config);
    broken.AddRoute(front);
    const DocsReport brokenReport = broken.AddDocs(options);
    Check(!brokenReport.warnings.empty(), "a link to a missing file is reported when the docs load");
    Check(!broken.Build(UltraCanvas::PathToUtf8(dir / "broken")).ok, "and fails the build");
    BuildOptions warn;
    warn.brokenLinksWarn = true;
    Check(broken.Build(UltraCanvas::PathToUtf8(dir / "broken"), warn).ok, "brokenLinksWarn makes it a warning");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

static void TestBasePath() {
    std::printf("A site under a path\n");
    const fs::path dir = TempDir("base");
    SiteConfig config = FixtureConfig();
    config.url = "https://example.org/v1";
    config.navbar = {{"About", "/about/", {}}};
    config.footer.clear();
    config.search = true;
    Builder builder(config);
    Check(builder.BasePath() == "/v1/" && builder.Url("/about/") == "/v1/about/" && builder.Url("https://x.org/") == "https://x.org/" &&
              builder.Url("#top") == "#top",
          "the site's path comes from its url; Url puts it in front of a site path");
    Route home;
    home.path = "/";
    home.title = "Example";
    home.bodyHtml = "<h1>Home</h1><p><a href=\"" + builder.Url("/about/") + "\">About</a> <a href=\"about/#team\">relative</a>"
                    " <a href=\"" + builder.Url("/catalog.json") + "\">the catalog</a></p>";
    builder.AddRoute(home);
    Route about;
    about.path = "/about/";
    about.title = "About";
    about.bodyHtml = "<h1>About</h1><h2 id=\"team\">Team</h2><p><a href=\"../\">back</a></p>";
    builder.AddRoute(about);
    const fs::path out = dir / "out";
    Write(out / "catalog.json", "{}");   // a file the folder already holds
    BuildOptions keep;
    keep.removeStale = false;
    const BuildResult result = builder.Build(UltraCanvas::PathToUtf8(out), keep);
    PrintErrors(result);
    Check(result.ok, "a site under a path builds, its links resolved in that path");
    const std::string page = Read(out / "about" / "index.html");
    Check(fs::exists(out / "index.html") && Contains(page, "<link rel=\"stylesheet\" href=\"/v1/assets/site.") &&
              Contains(page, "<a class=\"us-brand\" href=\"/v1/\">") && Contains(page, "<a href=\"/v1/about/\" aria-current=\"page\">About</a>") &&
              Contains(page, "<link rel=\"canonical\" href=\"https://example.org/v1/about/\">") &&
              Contains(page, "action=\"/v1/search/\"") && Contains(page, "data-index=\"/v1/search/en."),
          "every address the builder writes carries the path; the files stay at the folder's root");
    Check(Contains(Read(out / "sitemap.xml"), "<loc>https://example.org/v1/about/</loc>") &&
              Contains(Read(out / ".htaccess"), "ErrorDocument 404 /v1/404.html") && Contains(Read(out / "_headers"), "/v1/assets/*"),
          "the sitemap and the host's rules too");
    Check(fs::exists(out / "catalog.json"), "with removeStale off, the folder's other files stay");

    Builder outside(config);
    Route stray = home;
    stray.bodyHtml = "<h1>Home</h1><p><a href=\"/about/\">a link without the path</a></p>";
    outside.AddRoute(stray);
    outside.AddRoute(about);
    const BuildResult refused = outside.Build(UltraCanvas::PathToUtf8(dir / "outside"));
    Check(!refused.ok && HasError(refused, "leaves the site's path /v1/"), "a link that leaves the site's path is reported");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

static void TestPhaseD() {
    std::printf("Precompressed copies, the preview server, pictures\n");
    const fs::path dir = TempDir("phase-d");
    SiteConfig config = FixtureConfig();
    config.url = "https://example.org/v1";
    config.navbar = {{"About", "/about/", {}}};
    config.footer.clear();
    Builder builder(config);
    PictureOptions picture;
    picture.alt = "A landscape";
    picture.widths = {480, 960, 1440};
    picture.first = true;
    const std::string html = builder.Picture(ULTRASITE_TEST_PICTURE, picture);
    const bool decoded = Contains(html, "<picture>");
    Check(Contains(html, "alt=\"A landscape\"") && Contains(html, "src=\"/v1/assets/landscape."), "the original is a hashed asset under the site's path");
    if (decoded) {
        Check((Contains(html, "type=\"image/avif\"") || Contains(html, "type=\"image/webp\"")) && Contains(html, " 480w, ") &&
                  Contains(html, " 960w, ") && Contains(html, " 1000w\"") &&
                  !Contains(html, "1440w") && Contains(html, "width=\"1000\" height=\"563\"") && Contains(html, "fetchpriority=\"high\""),
              "a picture's variants at the widths below its own and its own, the <img> with its size");
    } else {
        std::printf("  (no image library in this build: the picture is a plain <img>)\n");
    }
    Route home;
    home.path = "/";
    home.title = "Example";
    home.bodyHtml = "<h1>Home</h1>" + html + "<p><a href=\"" + builder.Url("/about/") + "\">About</a></p>" + std::string(2000, ' ') + "<p>text</p>";
    builder.AddRoute(home);
    Route about;
    about.path = "/about/";
    about.title = "About";
    about.bodyHtml = "<h1>About</h1><p>Who.</p>";
    builder.AddRoute(about);
    const fs::path out = dir / "out";
    BuildOptions options;
    options.precompress = true;
    const BuildResult result = builder.Build(UltraCanvas::PathToUtf8(out), options);
    PrintErrors(result);
    Check(result.ok, "the site builds with its variants and its compressed copies");
    const std::string gz = Read(out / "index.html.gz");
    Check(gz.size() > 2 && static_cast<unsigned char>(gz[0]) == 0x1f && static_cast<unsigned char>(gz[1]) == 0x8b,
          "a page's .gz copy is gzip");
    Check(fs::exists(out / "index.html.br") == BrotliAvailable(), "a .br copy where the build has Brotli");
    if (decoded) {
        size_t written = 0;
        for (const fs::directory_entry& entry : fs::directory_iterator(out / "assets" / "img")) {
            if (entry.file_size() > 0) ++written;
        }
        const size_t formats = (Contains(html, "image/avif") ? 1u : 0u) + (Contains(html, "image/webp") ? 1u : 0u);
        Check(written == 3 * formats, "three widths in each format the image library writes");
        const auto stamp = fs::last_write_time(*fs::directory_iterator(out / "assets" / "img"));
        builder.Build(UltraCanvas::PathToUtf8(out), options);
        Check(fs::last_write_time(*fs::directory_iterator(out / "assets" / "img")) == stamp, "a later build reuses the variants");
    }

    // The preview server, request by request.
    const std::string folder = UltraCanvas::PathToUtf8(out);
    ServeResponse page = Respond(folder, "/v1/", "GET", "/v1/", "");
    Check(page.status == 200 && Contains(page.body, "<h1>Home</h1>") && page.contentType == "text/html; charset=utf-8" &&
              page.cacheControl == "no-cache",
          "a folder's address serves its index.html, checked on every visit");
    Check(Respond(folder, "/v1/", "GET", "/v1/about", "").location == "/v1/about/", "an address without its slash is sent to it");
    Check(Respond(folder, "/v1/", "GET", "/v1", "").location == "/v1/", "the site's path without its slash too");
    const ServeResponse missing = Respond(folder, "/v1/", "GET", "/v1/nothing/", "");
    Check(missing.status == 404 && Contains(missing.body, "Page not found"), "a missing page answers 404 with the 404 page");
    Check(Respond(folder, "/v1/", "GET", "/about/", "").status == 404, "an address outside the site's path is not there");
    Check(Respond(folder, "/v1/", "GET", "/v1/.htaccess", "").status == 403 && Respond(folder, "/v1/", "GET", "/v1/%2e%2e/x", "").status == 403,
          "nothing hidden, nothing outside the folder");
    Check(Respond(folder, "/v1/", "POST", "/v1/", "").status == 405, "GET and HEAD only");
    const ServeResponse compressed = Respond(folder, "/v1/", "GET", "/v1/?x=1", "gzip, deflate, br");
    Check(compressed.contentEncoding == (BrotliAvailable() ? "br" : "gzip") && compressed.body != page.body,
          "the compressed copy the browser takes");
    Check(Respond(folder, "/v1/", "HEAD", "/v1/", "").body.empty(), "HEAD sends no body");
    bool immutable = false;
    for (const fs::directory_entry& entry : fs::directory_iterator(out / "assets")) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("site.", 0) == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, ".css") == 0) {
            immutable = Respond(folder, "/v1/", "GET", "/v1/assets/" + name, "").cacheControl == "public, max-age=31536000, immutable";
        }
    }
    Check(immutable, "a hashed file is cached for a year");

    // And over a socket, on the loopback interface.
    std::atomic<bool> stop{false};
    std::atomic<int> port{0};
    ServeOptions serve;
    serve.port = 0;
    serve.basePath = "/v1/";
    serve.stop = &stop;
    serve.onReady = [&](int p) { port = p; };
    std::thread server([&]() { Serve(folder, serve); });
    for (int i = 0; i < 100 && port == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::string reply;
    if (port != 0) {
        const UltraNetHandle client = UltraNet_TcpConnect("127.0.0.1", port);
        if (client != UltraNetInvalidHandle) {
            const std::string request = "GET /v1/about/ HTTP/1.1\r\nHost: localhost\r\n\r\n";
            int sent = 0;
            UltraNet_TcpSend(client, std::vector<uint8_t>(request.begin(), request.end()), sent);
            for (int i = 0; i < 50; ++i) {
                std::vector<uint8_t> chunk;
                if (!UltraNet_TcpReceive(client, chunk, 65536, 2000).success || chunk.empty()) break;
                reply.append(chunk.begin(), chunk.end());
            }
            UltraNet_SocketClose(client);
        }
    }
    stop = true;
    server.join();
    Check(reply.rfind("HTTP/1.1 200 OK\r\n", 0) == 0 && Contains(reply, "<h1>About</h1>"), "the server answers over the loopback interface");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

static void TestVersions() {
    std::printf("Versioned docs\n");
    const fs::path dir = TempDir("versions");
    Write(dir / "docs" / "index.md", "# Docs\n\nThe latest.\n");
    Write(dir / "docs" / "guide.md", "# Guide\n\nHow it works now.\n");
    Write(dir / "v1" / "index.md", "# Docs\n\nVersion one.\n");
    Write(dir / "v1" / "guide.md", "# Guide\n\nHow it worked.\n");
    Write(dir / "v1" / "removed.md", "# Removed\n\nGone since.\n");
    SiteConfig config = FixtureConfig();
    config.navbar = {{"Docs", "/docs/", {}}};
    config.footer.clear();
    config.search = true;
    Builder builder(config);
    Route front;
    front.path = "/";
    front.title = "Example";
    front.bodyHtml = "<h1>Example</h1>";
    builder.AddRoute(front);
    DocsOptions options;
    options.folder = UltraCanvas::PathToUtf8(dir / "docs");
    options.currentVersion = "2.0";
    options.versions = {{"1.0", UltraCanvas::PathToUtf8(dir / "v1"), "/docs/1.0/"}};
    const DocsReport report = builder.AddDocs(options);
    Check(report.errors.empty() && report.pages == 5, "the latest docs and an earlier version load");
    const fs::path out = dir / "out";
    const BuildResult result = builder.Build(UltraCanvas::PathToUtf8(out));
    PrintErrors(result);
    Check(result.ok && fs::exists(out / "docs" / "guide" / "index.html") && fs::exists(out / "docs" / "1.0" / "guide" / "index.html") &&
              fs::exists(out / "docs" / "1.0" / "removed" / "index.html"),
          "each version at its own prefix");
    const std::string latest = Read(out / "docs" / "guide" / "index.html");
    Check(Contains(latest, "<nav class=\"us-versions\"") && Contains(latest, "<a href=\"/docs/guide/\" aria-current=\"page\">2.0</a>") &&
              Contains(latest, "<a href=\"/docs/1.0/guide/\">1.0</a>") && !Contains(latest, "us-outdated"),
          "the version switcher leads to the same page in each version");
    const std::string old = Read(out / "docs" / "1.0" / "removed" / "index.html");
    Check(Contains(old, "us-outdated") && Contains(old, "This is the documentation of version 1.0.") && Contains(old, "<a href=\"/docs/\">2.0</a>"),
          "an earlier version's page says so; a page the latest lacks leads to its root");
    std::string index;
    for (const fs::directory_entry& entry : fs::directory_iterator(out / "search")) {
        if (entry.path().extension() == ".json") index = Read(entry.path());
    }
    Check(Contains(index, "How it works now") && !Contains(index, "How it worked") && !Contains(index, "Gone since"),
          "the search finds the latest version only");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// The swap navigation: the pages say so (data-us-nav), every module that
// starts again on us:page says so (data-us-swap), the swap module is
// written and linked; an app's module is not ready unless the app says so;
// a site that loads pages has none of it.
static void TestSwap() {
    std::printf("The swap navigation\n");
    const fs::path dir = TempDir("swap");
    auto build = [&](Navigation navigation, bool tabsReady, const fs::path& out) {
        SiteConfig config = FixtureConfig();
        config.navigation = navigation;
        Builder builder(config);
        builder.AddScript("tabs", "document.addEventListener('us:page', () => {});\n", tabsReady);
        builder.AddFile("media/pic.png", "picture");
        AddFixtureRoutes(builder);
        BuildResult result = builder.Build(UltraCanvas::PathToUtf8(out));
        PrintErrors(result);
        return result.ok;
    };
    auto swapFile = [](const fs::path& out) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(out / "assets" / "js", ec)) {
            if (entry.path().filename().string().rfind("swap.", 0) == 0) return entry.path().filename().string();
        }
        return std::string();
    };

    const fs::path swapped = dir / "swap";
    Check(build(Navigation::Swap, false, swapped), "a swap site builds");
    const std::string home = Read(swapped / "index.html");
    const std::string about = Read(swapped / "about" / "index.html");
    const std::string file = swapFile(swapped);
    Check(Contains(home, "<html lang=\"en\" data-us-nav=\"swap\">"), "a swap site's pages say so on <html>");
    Check(!file.empty() && Contains(home, "/assets/js/" + file + "\" data-us-swap></script>") &&
              Contains(Read(swapped / "assets" / "js" / file), "us:page"),
          "the swap module is written and linked on every page");
    Check(Contains(home, "/assets/js/core.") && Contains(home, ".js\" data-us-swap></script>\n<script type=\"module\" src=\"/assets/js/swap."),
          "the core module is marked as starting again");
    Check(Contains(about, "/assets/js/tabs.") && !Contains(about, "\" data-us-swap></script>\n<script type=\"speculationrules\">"),
          "an app's module the app did not mark is not marked: a page with it is loaded the ordinary way");

    const fs::path ready = dir / "ready";
    Check(build(Navigation::Swap, true, ready), "a swap site with a ready module builds");
    Check(Contains(Read(ready / "about" / "index.html"), "\" data-us-swap></script>\n<script type=\"speculationrules\">"),
          "an app's module marked swapReady is marked on the page");

    const fs::path loaded = dir / "load";
    Check(build(Navigation::Load, true, loaded), "a site that loads pages builds");
    const std::string plain = Read(loaded / "index.html");
    Check(Contains(plain, "<html lang=\"en\">") && !Contains(plain, "data-us-swap") && !Contains(plain, "/swap.") && swapFile(loaded).empty(),
          "a site that loads pages has no swap module and no marks");

    SiteConfig config = FixtureConfig();
    Builder reserved(config);
    reserved.AddScript("swap", "1;\n");
    reserved.AddFile("media/pic.png", "picture");
    AddFixtureRoutes(reserved);
    BuildResult refused = reserved.Build(UltraCanvas::PathToUtf8(dir / "reserved"));
    Check(!refused.ok && HasError(refused, "'swap'"), "an app cannot name a behaviour 'swap'");
}

int main() {
    TestHelpers();
    TestBuild();
    TestChecks();
    TestMarkdown();
    TestDocs();
    TestBasePath();
    TestPhaseD();
    TestVersions();
    TestSwap();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
