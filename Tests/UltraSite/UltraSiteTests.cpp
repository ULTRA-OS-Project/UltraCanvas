// Tests/UltraSite/UltraSiteTests.cpp
// UltraSite without a window: a fixture site is built into a temporary
// folder and read back. Every page is a whole document with its head, its
// frame and its body; the stylesheet and the scripts carry their content
// hash and change name only when their content does; a behaviour module
// reaches exactly the pages that ask for it; a broken link or anchor and a
// page over budget fail the build; what an earlier build left is removed.
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSite/UltraSite.h"

#include "HTMLReader/HTMLDocument.h"
#include "HTMLReader/HTMLParser.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
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

int main() {
    TestHelpers();
    TestBuild();
    TestChecks();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
