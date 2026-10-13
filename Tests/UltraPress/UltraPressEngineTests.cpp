// Tests/UltraPress/UltraPressEngineTests.cpp
// The UltraPress engine without a window: the content model round-trips
// through its files, the validator finds what is wrong, the HTML shadow is
// well-formed HTML that carries the text, the metadata and the structured
// data, the sitemap and feed list what they should, and a static publish
// writes the files. The example site under Apps/UltraPress/examples/ultraos
// is loaded as a real site.
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressBlocks.h"
#include "UltraPressHtml.h"
#include "UltraPressModel.h"
#include "UltraPressPublish.h"

#include "HTMLReader/HTMLDocument.h"
#include "HTMLReader/HTMLParser.h"
#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace UltraPress;
using UltraCanvas::JSONValue;
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

static bool Contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

static fs::path TempDir(const std::string& name) {
    static const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();   // one folder per run
    const fs::path dir = fs::temp_directory_path() / ("ultrapress-test-" + name + "-" + std::to_string(stamp));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

// A node of the parsed shadow with this tag, anywhere in the tree.
static const UltraCanvas::HTML::Node* FindTag(const UltraCanvas::HTML::Node* node, const std::string& tag) {
    if (!node) return nullptr;
    if (node->type == UltraCanvas::HTML::NodeType::Element && node->tag == tag) return node;
    for (const auto& child : node->children) {
        if (const auto* found = FindTag(child.get(), tag)) return found;
    }
    return nullptr;
}

static void TestStarterRoundTrip() {
    std::printf("Starter site: save, load, compare\n");
    const Site site = CreateStarterSite("Test & Co", "https://test.example/", "de");
    Check(site.url == "https://test.example", "the url loses its trailing slash");
    Check(ValidateSite(site).empty(), "the starter site validates clean");

    const fs::path dir = TempDir("roundtrip");
    std::vector<std::string> errors;
    Check(SaveSite(UltraCanvas::PathToUtf8(dir), site, errors), "SaveSite succeeds");
    Check(fs::exists(dir / "site.json") && fs::exists(dir / "theme.json") && fs::exists(dir / "menus.json") &&
              fs::exists(dir / "pages" / "home.json") && fs::exists(dir / "pages" / "about.json"),
          "the site folder has its files");

    Site loaded;
    Check(LoadSite(UltraCanvas::PathToUtf8(dir), loaded, errors), "LoadSite succeeds");
    for (const std::string& error : errors) std::printf("  error: %s\n", error.c_str());
    Check(loaded.name == site.name && loaded.tagline == site.tagline && loaded.url == site.url &&
              loaded.language == "de" && loaded.owner == site.owner,
          "site.json round-trips");
    Check(loaded.theme.colors == site.theme.colors && loaded.theme.fontFamily == "Ubuntu", "theme.json round-trips");
    Check(loaded.menus.size() == 2 && loaded.menus[0].name == "main" && loaded.menus[0].items.size() == 2 &&
              loaded.menus[0].items[1].page == "about",
          "menus.json round-trips");
    Check(loaded.pages.size() == 2, "both pages load");
    const Page* home = loaded.FindPage("home");
    Check(home && home->pinned && home->blocks.size() == 5 && home->blocks[0].type == "hero" &&
              home->blocks[0].Text("title") == "Test & Co",
          "the front page round-trips with its blocks");
    Check(home && home->blocks[3].Items("items").GetSize() == 2, "an items array round-trips");
    Check(UltraCanvas::JSON::Serialize(PageToJson(*home)) == UltraCanvas::JSON::Serialize(PageToJson(site.pages[0])),
          "a page's JSON is identical after the round trip");

    // Removing a page removes its file on the next save.
    Site smaller = loaded;
    std::erase_if(smaller.pages, [](const Page& page) { return page.slug == "about"; });
    Check(smaller.pages.size() == 1, "the about page is taken out");
    Check(SaveSite(UltraCanvas::PathToUtf8(dir), smaller, errors), "SaveSite with one page fewer");
    Check(!fs::exists(dir / "pages" / "about.json"), "the removed page's file is gone");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

static void TestValidation() {
    std::printf("Validation\n");
    Site site = CreateStarterSite("V", "https://v.example");
    Block unknown{"carousel"};
    site.pages[0].blocks.push_back(unknown);
    Block hero{"hero"};   // no title
    hero.Set("buttons", JSONValue::MakeArray().Append(JSONValue::MakeObject().Set("title", "Go").Set("page", "missing")));
    hero.Set("colour", "red");
    site.pages[0].blocks.push_back(hero);
    Block text{"text"};
    text.Set("text", "x");
    text.children.push_back(Block{"heading"});
    site.pages[0].blocks.push_back(text);
    site.menus[0].items.push_back({"Nowhere", "", "", {}});
    site.pages.push_back(site.pages[1]);   // duplicate slug

    const std::vector<ValidationIssue> issues = ValidateSite(site);
    auto has = [&](const std::string& needle) {
        for (const ValidationIssue& issue : issues) {
            if (Contains(issue.message, needle)) return true;
        }
        return false;
    };
    Check(has("unknown block type 'carousel'"), "an unknown block type is reported");
    Check(has("hero needs 'title'"), "a missing required property is reported");
    Check(has("names a page the site does not have: 'missing'"), "a link to a missing page is reported");
    Check(has("does not have a property 'colour'"), "an unknown property is reported");
    Check(has("text takes no children"), "children on a leaf block are reported");
    Check(has("leads nowhere"), "a menu item without a target is reported");
    Check(has("two pages with this slug"), "a duplicate slug is reported");
    Check(has("heading needs 'text'"), "a child of a leaf block is still validated");
    Check(issues.size() == 8, "exactly these eight issues: " + std::to_string(issues.size()));
}

static void TestShadowPage() {
    std::printf("The HTML shadow\n");
    Site site = CreateStarterSite("Shadow <Site> & Co", "https://shadow.example");
    site.tagline = "Tag \"line\"";
    site.owner = "Owner GmbH";
    site.pages[0].seo.description = "A description with <tags> & ampersands";
    site.pages[0].seo.image = "media/card.png";
    HtmlEmitter emitter(site);
    const std::string html = emitter.Page(site.pages[0]);

    Check(Contains(html, "<!doctype html>") && Contains(html, "<html lang=\"en\">"), "doctype and language");
    Check(Contains(html, "<title>Shadow &lt;Site&gt; &amp; Co - Tag &quot;line&quot;</title>"), "the title is escaped");
    Check(Contains(html, "<meta name=\"description\" content=\"A description with &lt;tags&gt; &amp; ampersands\">"),
          "the description is escaped");
    Check(Contains(html, "<link rel=\"canonical\" href=\"https://shadow.example/\">"), "the front page's canonical URL is the root");
    Check(Contains(html, "<meta property=\"og:image\" content=\"https://shadow.example/media/card.png\">"), "the social card picture is absolute");
    Check(Contains(html, "\"@type\":\"WebSite\"") && Contains(html, "\"@type\":\"FAQPage\"") &&
              Contains(html, "\"name\":\"What is this?\""),
          "the structured data names the site and the FAQ");
    Check(Contains(html, "<h1>Shadow &lt;Site&gt; &amp; Co</h1>"), "the hero title is the h1");
    Check(Contains(html, "<a href=\"/about/\" class=\"primary\">Learn more</a>"), "a page link resolves to its path");
    Check(Contains(html, "<form method=\"post\" action=\"/api/form/contact\"") && Contains(html, "<textarea name=\"message\" required>"),
          "the form has its route and fields");
    Check(Contains(html, "<nav aria-label=\"Main\">") && Contains(html, "<a href=\"/about/\">About</a>"), "the main menu is in the header");
    Check(!Contains(html, "<script src="), "without a renderer there is no script");
    Check(!Contains(html, "<p>Replace this paragraph with your own.<br>"), "a blank line starts a new paragraph, not a break");
    Check(Contains(html, "<p>Replace this paragraph with your own.</p>\n<p>A blank line starts a new paragraph.</p>"),
          "paragraphs are separate");

    UltraCanvas::HTML::Parser parser;
    UltraCanvas::HTML::Document document = parser.Parse(html);
    Check(document.root != nullptr, "the shadow parses");
    Check(FindTag(document.root.get(), "main") && FindTag(document.root.get(), "footer") && FindTag(document.root.get(), "h1"),
          "the parsed shadow has main, footer and h1");
    const std::string text = UltraCanvas::HTML::ExtractPlainText(html);
    Check(Contains(text, "Shadow <Site> & Co") && Contains(text, "Say in one or two sentences"),
          "the text reads back decoded");

    // The renderer hook and the UltraWeb app.
    HtmlOptions options;
    options.rendererScript = "ultrapress-0.1.0.js";
    options.ultrawebApp = "site.wasm";
    const std::string hooked = HtmlEmitter(site, options).Page(site.pages[1]);
    Check(Contains(hooked, "<meta name=\"ultrapress-page\" content=\"about\">") &&
              Contains(hooked, "<script src=\"ultrapress-0.1.0.js\" async></script>") &&
              Contains(hooked, "<link rel=\"ultraweb-app\" href=\"site.wasm\">"),
          "the renderer and the UltraWeb app are referenced when asked");
    Check(Contains(hooked, "<title>About - Shadow &lt;Site&gt; &amp; Co</title>") && Contains(hooked, "<h1>About</h1>"),
          "an inner page has its own title and h1");
    Check(Contains(hooked, "<link rel=\"canonical\" href=\"https://shadow.example/about/\">"), "an inner page's canonical URL");

    // A post, and the html block passing through.
    Page post;
    post.slug = "news";
    post.title = "News";
    post.kind = "post";
    post.date = "2026-10-13";
    Block raw{"html"};
    raw.Set("html", "<p class=\"own\">kept &amp; as is</p>");
    post.blocks.push_back(raw);
    site.pages.push_back(post);
    const std::string postHtml = HtmlEmitter(site).Page(post);
    Check(Contains(postHtml, "<article>") && Contains(postHtml, "<time datetime=\"2026-10-13\">") &&
              Contains(postHtml, "\"@type\":\"Article\"") && Contains(postHtml, "<p class=\"own\">kept &amp; as is</p>"),
          "a post is an article with its date, and an html block passes through");
}

static void TestSitemapAndFeed() {
    std::printf("Sitemap, robots, feed\n");
    Site site = CreateStarterSite("S", "https://s.example");
    Page draft;
    draft.slug = "draft";
    draft.title = "Draft";
    draft.status = "draft";
    site.pages.push_back(draft);
    Page hidden;
    hidden.slug = "hidden";
    hidden.title = "Hidden";
    hidden.seo.noindex = true;
    site.pages.push_back(hidden);
    for (int i = 0; i < 2; ++i) {
        Page post;
        post.slug = i == 0 ? "older" : "newer";
        post.title = post.slug;
        post.kind = "post";
        post.date = i == 0 ? "2026-01-05" : "2026-10-13";
        site.pages.push_back(post);
    }
    HtmlEmitter emitter(site);
    const std::string sitemap = emitter.Sitemap();
    Check(Contains(sitemap, "<loc>https://s.example/</loc>") && Contains(sitemap, "<loc>https://s.example/about/</loc>"),
          "the sitemap lists the published pages");
    Check(!Contains(sitemap, "/draft/") && !Contains(sitemap, "/hidden/"), "drafts and noindex pages are not in the sitemap");
    Check(Contains(sitemap, "<lastmod>2026-10-13</lastmod>"), "a dated page has lastmod");
    Check(emitter.Robots() == "User-agent: *\nAllow: /\nSitemap: https://s.example/sitemap.xml\n", "robots.txt");
    const std::string feed = emitter.Feed();
    Check(feed.find("<title>newer</title>") < feed.find("<title>older</title>"), "the feed is newest first");
    Check(Contains(feed, "<pubDate>Tue, 13 Oct 2026 00:00:00 +0000</pubDate>") &&
              Contains(feed, "<pubDate>Mon, 05 Jan 2026 00:00:00 +0000</pubDate>"),
          "RFC 822 dates with the right weekday");
    Check(!Contains(feed, "<title>About</title>"), "pages are not in the feed");
    Check(Rfc822Date("not a date").empty() && Rfc822Date("2026-13-01").empty(), "a bad date gives no pubDate");
}

static void TestPublishAndExample() {
    std::printf("Publish the example site\n");
    Site site;
    std::vector<std::string> errors;
    const bool loaded = LoadSite(ULTRAPRESS_EXAMPLE_DIR, site, errors);
    for (const std::string& error : errors) std::printf("  error: %s\n", error.c_str());
    Check(loaded, "the example site loads");
    Check(site.name == "ULTRA OS" && site.pages.size() >= 2 && site.FindPage("home") && site.FindPage("the-plan"),
          "the example site has its pages");
    const std::vector<ValidationIssue> issues = ValidateSite(site);
    for (const ValidationIssue& issue : issues) std::printf("  issue: %s %s: %s\n", issue.page.c_str(), issue.path.c_str(), issue.message.c_str());
    Check(issues.empty(), "the example site validates clean");

    const fs::path out = TempDir("publish");
    const PublishResult result = PublishStatic(site, ULTRAPRESS_EXAMPLE_DIR, UltraCanvas::PathToUtf8(out));
    for (const std::string& error : result.errors) std::printf("  error: %s\n", error.c_str());
    Check(result.ok, "the publish succeeds");
    Check(fs::exists(out / "index.html") && fs::exists(out / "the-plan" / "index.html") && fs::exists(out / "sitemap.xml") &&
              fs::exists(out / "robots.txt") && fs::exists(out / "feed.xml") && fs::exists(out / ".htaccess"),
          "the published folder has its files");
    std::string home;
    Check(ReadTextFile(UltraCanvas::PathToUtf8(out / "index.html"), home), "the front page can be read back");
    Check(Contains(home, "Introducing ULTRA OS") && Contains(home, "<a href=\"/the-plan/\""), "the front page carries the site's text and links");
    std::error_code ec;
    fs::remove_all(out, ec);

    std::printf("The page list\n");
    std::map<std::string, long long> views = {{"the-plan", 10}, {"home", 100}};
    const std::vector<const Page*> popular = SortedPages(site, PageOrder::MostPopular, views);
    Check(!popular.empty() && popular.front()->slug == "home", "the pinned front page sorts first");
    Check(MakeSlug("The Development Plan!") == "the-development-plan" && MakeSlug("  ") == "page", "slugs");
}

int main() {
    TestStarterRoundTrip();
    TestValidation();
    TestShadowPage();
    TestSitemapAndFeed();
    TestPublishAndExample();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
