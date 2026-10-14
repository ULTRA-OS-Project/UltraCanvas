// Tests/UltraPress/UltraPressEngineTests.cpp
// The UltraPress engine without a window: the content model round-trips
// through its files, the validator finds what is wrong, the HTML shadow is
// well-formed HTML that carries the text, the metadata and the structured
// data, the sitemap and feed list what they should, and a static publish
// writes the files. The example site under Apps/UltraPress/examples/ultraos
// is loaded as a real site. The deploy sends a published folder through a
// recording uploader and, where UltraCloud is built, through the hosts onto
// the in-memory provider.
// Version: 0.7.2 - the shadow's picture sizes
// Version: 0.7.0 - text in a block: paragraphs, locations, block paths
// Version: 0.6.0 - the media library
// Version: 0.5.0 - the deploy and the hosts
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressBlocks.h"
#include "UltraPressDeploy.h"
#include "UltraPressHtml.h"
#include "UltraPressImport.h"
#include "UltraPressMedia.h"
#include "UltraPressModel.h"
#include "UltraPressPublish.h"
#if ULTRAPRESS_HAS_ULTRACLOUD
#include "UltraPressHosting.h"
#endif

#include "HTMLReader/HTMLDocument.h"
#include "HTMLReader/HTMLParser.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
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
    // The output mirrors the site: a page removed from the site leaves the
    // folder on the next publish, a stray file too, the deploy manifest not.
    WriteTextFile(UltraCanvas::PathToUtf8(out / "stray" / "old.html"), "old");
    fs::create_directories(out / "stray", ec);
    WriteTextFile(UltraCanvas::PathToUtf8(out / "stray" / "old.html"), "old");
    WriteTextFile(UltraCanvas::PathToUtf8(out / ".ultrapress-deploy.json"), "{}");
    Site smaller = site;
    smaller.pages.erase(std::remove_if(smaller.pages.begin(), smaller.pages.end(),
                                       [](const Page& page) { return page.slug == "the-plan"; }),
                        smaller.pages.end());
    const PublishResult again = PublishStatic(smaller, ULTRAPRESS_EXAMPLE_DIR, UltraCanvas::PathToUtf8(out));
    Check(again.ok && again.removed.size() == 2 && !fs::exists(out / "the-plan", ec) && !fs::exists(out / "stray", ec) &&
              fs::exists(out / ".ultrapress-deploy.json", ec) && fs::exists(out / "index.html", ec),
          "the output mirrors the site: the removed page and a stray file go, the manifest stays");
    PublishOptions keep;
    keep.removeStale = false;
    WriteTextFile(UltraCanvas::PathToUtf8(out / "kept.txt"), "kept");
    const PublishResult kept = PublishStatic(smaller, ULTRAPRESS_EXAMPLE_DIR, UltraCanvas::PathToUtf8(out), keep);
    Check(kept.ok && kept.removed.empty() && fs::exists(out / "kept.txt", ec), "removeStale off leaves everything");
    fs::remove_all(out, ec);

    std::printf("The page list\n");
    std::map<std::string, long long> views = {{"the-plan", 10}, {"home", 100}};
    const std::vector<const Page*> popular = SortedPages(site, PageOrder::MostPopular, views);
    Check(!popular.empty() && popular.front()->slug == "home", "the pinned front page sorts first");
    Check(MakeSlug("The Development Plan!") == "the-development-plan" && MakeSlug("  ") == "page", "slugs");
}

static void TestWordPressImport() {
    std::printf("The WordPress importer\n");
    Site site;
    ImportReport report;
    std::vector<std::pair<std::string, std::string>> fetched;
    ImportOptions options;
    options.fetchMedia = [&](const std::string& url, const std::string& local) {
        fetched.emplace_back(url, local);
        return url.find("hero") != std::string::npos;   // one succeeds, one fails
    };
    const std::string path = std::string(ULTRAPRESS_TEST_DATA_DIR) + "/wordpress-export.xml";
    Check(ImportWordPressFile(path, options, site, report), "the export imports");
    for (const std::string& warning : report.warnings) std::printf("  warning: %s\n", warning.c_str());

    Check(site.name == "ULTRA OS & Friends" && site.url == "https://example.org" && site.language == "en" &&
              site.tagline == "Europe\xE2\x80\x99s open source operating system" && site.owner == "Stefan",
          "the site's name, address, language, tagline and owner");
    Check(report.pages == 2 && report.posts == 2 && report.drafts == 1 && report.attachments == 2 && report.menus == 2,
          "the counts: " + report.Summary());
    const Page* home = site.FindPage("home");
    const Page* plan = site.FindPage("the-plan");
    const Page* release = site.FindPage("release-0-9");
    const Page* notes = site.FindPage("notes");
    Check(home && home->pinned && home->kind == "page" && home->IsPublished() && home->date == "2026-09-01", "the front page is home, pinned and dated");
    Check(home && home->seo.description == "The front page of ULTRA OS.", "the excerpt is the description, as text");
    Check(plan && plan->IsPublished() && release && release->kind == "post", "the other page and the post");
    Check(notes && notes->status == "draft" && notes->title == "Notes", "the draft keeps its status and gets a slug from its title");

    // The blocks of the front page.
    Check(home && home->blocks.size() >= 10, "the front page has its blocks: " + std::to_string(home ? home->blocks.size() : 0));
    if (home && home->blocks.size() >= 10) {
        const auto& b = home->blocks;
        Check(b[0].type == "hero" && b[0].Text("title") == "It\xE2\x80\x99s time to build on what we\xE2\x80\x99ve learned" &&
                  b[0].Text("text") == "Unlike fragmented solutions, ULTRA OS unifies desktop and mobile." &&
                  b[0].Text("picture") == "media/hero.jpg",
              "a cover is a hero with its title, lead and picture at the media path");
        Check(b[1].type == "heading" && b[1].Text("text") == "What ULTRA OS is" && b[1].Number("level") == 2, "a heading");
        Check(b[2].type == "text" && b[2].Text("text") == "A next-generation operating system project.\nOpen source, for everyone.\n\nOur goal is a compelling alternative.",
              "plain paragraphs merge into one text block, a break kept as a line");
        Check(b[3].type == "richtext" && b[3].Text("html").find("<a href=\"https://example.org/the-plan/\">") != std::string::npos,
              "a paragraph with a link stays rich text");
        Check(b[4].type == "picture" && b[4].Text("src") == "media/desktop.png" && b[4].Text("alt") == "The desktop" &&
                  b[4].Text("caption") == "The ULTRA OS desktop",
              "an image with a sized file name becomes a picture of the original at the media path");
        Check(b[5].type == "quote" && b[5].Text("text") == "Build on what we have learned." && b[5].Text("cite") == "The team", "a quote with its source");
        Check(b[6].type == "buttons" && b[6].Items("items").GetSize() == 2 && b[6].Items("items")[0]["page"].GetString() == "the-plan" &&
                  b[6].Items("items")[0]["primary"].GetBoolean(false) && b[6].Items("items")[1]["url"].GetString() == "https://github.com/ULTRA-OS-Project",
              "buttons: a link into the site names the page, another keeps its URL");
        Check(b[7].type == "columns" && b[7].children.size() == 2 && b[7].children[0].type == "heading" && b[7].children[1].type == "richtext",
              "columns: one block becomes that block, several become rich text");
        Check(b[8].type == "richtext" && b[8].Text("html").find("<li>One</li>") != std::string::npos && b[8].Text("html").find("wp:") == std::string::npos,
              "a list is rich text without markers");
        Check(b[9].type == "spacer" && b[9].Number("height") == 80, "a spacer with its height");
        Check(b.size() == 11 && b[10].type == "richtext" && b[10].Text("html").find("Slides") != std::string::npos, "an unknown block is kept as rich text");
    }
    bool unknownWarned = false, shortcodeWarned = false;
    for (const std::string& warning : report.warnings) {
        if (warning.find("wp:some-plugin/carousel") != std::string::npos) unknownWarned = true;
        if (warning.find("shortcode") != std::string::npos) shortcodeWarned = true;
    }
    Check(unknownWarned && shortcodeWarned, "the report names the unknown block and the shortcodes");
    Check(notes && notes->blocks.size() == 1 && notes->blocks[0].type == "richtext" &&
              notes->blocks[0].Text("html").find("[caption") == std::string::npos &&
              notes->blocks[0].Text("html").find("/media/desktop.png") != std::string::npos,
          "classic content is one rich-text block, captions unwrapped, the sized picture rewritten");

    // Menus.
    const Menu* main = site.FindMenu("main");
    const Menu* footer = site.FindMenu("footer");
    Check(main && main->items.size() == 2 && main->items[0].title == "Home" && main->items[0].page == "home", "the main menu's first item is the front page");
    Check(main && main->items[1].title == "Details" && main->items[1].url == "#" && main->items[1].children.size() == 1 &&
              main->items[1].children[0].page == "the-plan" && main->items[1].children[0].title == "The development plan",
          "a nested item under a custom one, titled from its page");
    Check(footer && footer->items.size() == 1 && footer->items[0].url == "https://github.com/ULTRA-OS-Project", "the footer menu");

    // Media and redirects.
    Check(report.media.size() == 2 && report.media[1].second == "media/desktop.png", "the attachments land flat in media/ under clean names");
    Check(fetched.size() == 2 && report.fetched.size() == 1 && report.fetched[0].second == "media/hero.jpg", "the fetcher is asked for each, and what succeeded is listed");
    bool redirected = false;
    for (const auto& [from, to] : report.redirects) {
        if (from == "/2026/10/05/release-0-9/" && to == "/release-0-9/") redirected = true;
    }
    Check(redirected, "a post's dated address gets a redirect to its new path");
    Check(ValidateSite(site).empty(), "the imported site validates clean");

    // Not an export.
    Site none;
    ImportReport bad;
    Check(!ImportWordPress("<html><body>no</body></html>", {}, none, bad) && !bad.warnings.empty(), "a page is refused");
    Check(!ImportWordPress("<rss><channel><title>x</title></channel></rss>", {}, none, bad), "an RSS feed without items is refused");
    Check(!ImportWordPress("<rss><channel>", {}, none, bad), "broken XML is refused");
}

// A folder the deploy tests send: two pages, a picture, the .htaccess.
static fs::path MakeOutput(const std::string& name) {
    const fs::path out = TempDir(name);
    std::error_code ec;
    fs::create_directories(out / "the-plan", ec);
    fs::create_directories(out / "media" / "2026", ec);
    WriteTextFile(UltraCanvas::PathToUtf8(out / "index.html"), "<html>home</html>");
    WriteTextFile(UltraCanvas::PathToUtf8(out / "the-plan" / "index.html"), "<html>plan</html>");
    WriteTextFile(UltraCanvas::PathToUtf8(out / ".htaccess"), "AddType application/wasm .wasm");
    std::ofstream png(out / "media" / "2026" / "a.png", std::ios::binary);
    const unsigned char bytes[] = {0x89, 'P', 'N', 'G', 0, 1, 2, 3, 0xff};
    png.write(reinterpret_cast<const char*>(bytes), sizeof bytes);
    return out;
}

// Records what the deploy asked for, and fails the files it is told to.
struct Recorder {
    std::vector<std::string> uploads;      // remote paths, in order
    std::vector<std::string> folders;
    std::vector<std::string> removals;     // "<remote>" for a file, "<remote>/" for a folder
    std::set<std::string> failing;         // remote paths that refuse
    bool canRemove = true;
    Uploader AsUploader() {
        Uploader uploader;
        if (canRemove) {
            uploader.remove = [this](const std::string& remote, bool isDirectory, std::string& error) {
                if (failing.count(remote)) { error = "kept"; return false; }
                removals.push_back(isDirectory ? remote + "/" : remote);
                return true;
            };
        }
        uploader.makeDirectory = [this](const std::string& remote, std::string&) {
            folders.push_back(remote);
            return true;
        };
        uploader.upload = [this](const std::string& local, const std::string& remote, std::string& error) {
            if (failing.count(remote)) { error = "refused"; return false; }
            std::string text;
            if (!ReadTextFile(local, text)) { error = "unreadable"; return false; }
            uploads.push_back(remote);
            return true;
        };
        return uploader;
    }
};

static bool Has(const std::vector<std::string>& list, const std::string& item) {
    for (const std::string& entry : list) if (entry == item) return true;
    return false;
}

static void TestDeploy() {
    std::printf("The deploy\n");
    Check(JoinRemote("/", "index.html") == "/index.html" && JoinRemote("public_html/", "a/b") == "/public_html/a/b" &&
              JoinRemote("", "x") == "/x" && JoinRemote("/www", "") == "/www" && JoinRemote("/", "") == "/",
          "remote paths join under the root");
    const fs::path out = MakeOutput("deploy");
    const std::string fp = Fingerprint(UltraCanvas::PathToUtf8(out / "index.html"));
    Check(!fp.empty() && fp == Fingerprint(UltraCanvas::PathToUtf8(out / "index.html")) &&
              fp != Fingerprint(UltraCanvas::PathToUtf8(out / "the-plan" / "index.html")) &&
              Fingerprint(UltraCanvas::PathToUtf8(out / "missing")).empty(),
          "a fingerprint is stable, differs between files and is empty for a missing one");

    Recorder recorder;
    DeployOptions options;
    options.remoteRoot = "/public_html";
    size_t progress = 0;
    options.onProgress = [&progress](const std::string&, size_t, size_t) { ++progress; };
    const std::string host = "ftp-demo:/public_html";
    DeployResult first = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, options);
    for (const std::string& error : first.errors) std::printf("  error: %s\n", error.c_str());
    Check(first.ok && first.uploaded.size() == 4 && first.unchanged.empty() && first.failed.empty() && progress == 4,
          "the first deploy uploads every file: " + first.Summary());
    Check(Has(recorder.uploads, "/public_html/index.html") && Has(recorder.uploads, "/public_html/.htaccess") &&
              Has(recorder.uploads, "/public_html/the-plan/index.html") && Has(recorder.uploads, "/public_html/media/2026/a.png"),
          "under the remote root, with '/' between the parts");
    Check(recorder.folders.size() == 4 && recorder.folders[0] == "/public_html" && recorder.folders[1] == "/public_html/media" &&
              recorder.folders[2] == "/public_html/media/2026" && recorder.folders[3] == "/public_html/the-plan",
          "the root and each folder under it are created once, outermost first");
    Check(fs::exists(out / kDeployManifestName) && !Has(recorder.uploads, std::string("/public_html/") + kDeployManifestName),
          "the manifest is written beside the output and never uploaded");

    recorder = Recorder();
    DeployResult second = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, options);
    Check(second.ok && second.uploaded.empty() && second.unchanged.size() == 4 && recorder.uploads.empty() && recorder.folders.empty(),
          "the second deploy uploads nothing: " + second.Summary());

    WriteTextFile(UltraCanvas::PathToUtf8(out / "the-plan" / "index.html"), "<html>plan, revised</html>");
    recorder = Recorder();
    DeployResult third = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, options);
    Check(third.ok && third.uploaded.size() == 1 && third.uploaded[0] == "the-plan/index.html" && third.unchanged.size() == 3,
          "a changed page is uploaded, the rest not: " + third.Summary());

    recorder = Recorder();
    DeployOptions slash = options;
    slash.remoteRoot = "/";
    DeployResult other = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), "webdav-other:/", slash);
    Check(other.ok && other.uploaded.size() == 4 && recorder.folders.size() == 3 && recorder.folders[0] == "/media",
          "another host gets everything; a root of '/' is not created");
    recorder = Recorder();
    DeployOptions deep = options;
    deep.remoteRoot = "/home/me/public_html/";
    DeployResult nested = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), "deep:/home/me/public_html", deep);
    Check(nested.ok && recorder.folders.size() == 6 && recorder.folders[0] == "/home" && recorder.folders[1] == "/home/me" &&
              recorder.folders[2] == "/home/me/public_html" && Has(recorder.uploads, "/home/me/public_html/index.html"),
          "a root several levels deep is created level by level");

    recorder = Recorder();
    DeployOptions forced = options;
    forced.force = true;
    DeployResult all = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, forced);
    Check(all.ok && all.uploaded.size() == 4 && all.unchanged.empty(), "--force uploads everything again");

    WriteTextFile(UltraCanvas::PathToUtf8(out / "index.html"), "<html>home, revised</html>");
    recorder = Recorder();
    recorder.failing.insert("/public_html/index.html");
    DeployResult failed = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, options);
    Check(!failed.ok && failed.failed.size() == 1 && failed.failed[0] == "index.html" && failed.errors.size() == 1 &&
              Contains(failed.errors[0], "refused") && failed.unchanged.size() == 3,
          "a refused upload is reported and the others go on: " + failed.Summary());
    recorder = Recorder();
    DeployResult retry = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, options);
    Check(retry.ok && retry.uploaded.size() == 1 && retry.uploaded[0] == "index.html", "the failed file is tried again next time");

    recorder = Recorder();
    recorder.failing = {"/public_html/.htaccess", "/public_html/index.html", "/public_html/media/2026/a.png"};
    DeployOptions patient = forced;
    patient.stopAfterFailures = 2;
    DeployResult down = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, patient);
    Check(!down.ok && down.failed.size() == 2 && down.notTried.size() == 2 && recorder.uploads.empty() &&
              Contains(down.Summary(), "2 not tried") && Contains(down.errors.back(), "stopped after 2"),
          "a host that is down stops the run after the failures in a row: " + down.Summary());
    patient.stopAfterFailures = 0;
    recorder = Recorder();
    recorder.failing = {"/public_html/.htaccess", "/public_html/index.html", "/public_html/media/2026/a.png"};
    DeployResult stubborn = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, patient);
    Check(!stubborn.ok && stubborn.failed.size() == 3 && stubborn.notTried.empty() && stubborn.uploaded.size() == 1,
          "0 never stops: " + stubborn.Summary());

    std::printf("Pruning\n");
    std::error_code ec;
    recorder = Recorder();
    DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, options);   // the manifest settled: all four
    fs::remove_all(out / "the-plan", ec);
    recorder = Recorder();
    DeployResult kept = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, options);
    Check(kept.ok && recorder.removals.empty() && kept.removed.empty() && kept.unchanged.size() == 3,
          "without prune a removed page stays on the host, and is remembered: " + kept.Summary());
    DeployOptions pruning = options;
    pruning.prune = true;
    recorder = Recorder();
    DeployResult pruned = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, pruning);
    Check(pruned.ok && pruned.removed.size() == 1 && pruned.removed[0] == "the-plan/index.html" && recorder.removals.size() == 2 &&
              recorder.removals[0] == "/public_html/the-plan/index.html" && recorder.removals[1] == "/public_html/the-plan/" &&
              Contains(pruned.Summary(), "1 removed"),
          "with prune the page and its emptied folder are removed: " + pruned.Summary());
    recorder = Recorder();
    DeployResult prunedAgain = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, pruning);
    Check(prunedAgain.ok && prunedAgain.removed.empty() && recorder.removals.empty(), "and the manifest forgot it");
    fs::remove(out / "media" / "2026" / "a.png", ec);
    recorder = Recorder();
    recorder.failing.insert("/public_html/media/2026/a.png");
    DeployResult refused = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, pruning);
    Check(!refused.ok && refused.notRemoved.size() == 1 && Contains(refused.errors.back(), "not removed") &&
              Contains(refused.Summary(), "1 not removed"),
          "a host that keeps the file is reported: " + refused.Summary());
    recorder = Recorder();
    DeployResult retried = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, pruning);
    Check(retried.ok && retried.removed.size() == 1 && recorder.removals.size() == 3 &&
              recorder.removals[1] == "/public_html/media/2026/" && recorder.removals[2] == "/public_html/media/",
          "it is tried again next time, and the folders go innermost first: " + retried.Summary());
    WriteTextFile(UltraCanvas::PathToUtf8(out / "gone.txt"), "soon gone");
    DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, options);
    fs::remove(out / "gone.txt", ec);
    recorder = Recorder();
    recorder.canRemove = false;
    DeployResult cannot = DeployFolder(UltraCanvas::PathToUtf8(out), recorder.AsUploader(), host, pruning);
    Check(!cannot.ok && cannot.notRemoved.size() == 1 && Contains(cannot.errors.back(), "cannot delete"),
          "a host without the verb says what is left on it: " + cannot.Summary());

    DeployResult nowhere = DeployFolder(UltraCanvas::PathToUtf8(out / "nope"), recorder.AsUploader(), host, options);
    Check(!nowhere.ok && !nowhere.errors.empty(), "a missing folder is an error");

    std::printf("The hosting target in site.json\n");
    Site site = CreateStarterSite("Hosted", "https://hosted.example");
    site.hosting.account = "ftp-me-ftp-example-org";
    site.hosting.path = "/public_html";
    const fs::path dir = TempDir("hosting");
    std::vector<std::string> errors;
    Check(SaveSite(UltraCanvas::PathToUtf8(dir), site, errors), "a site with a host saves");
    Site back;
    Check(LoadSite(UltraCanvas::PathToUtf8(dir), back, errors) && back.hosting.account == site.hosting.account &&
              back.hosting.path == "/public_html",
          "and loads with its host and folder");
    back.hosting = Hosting();
    Check(SaveSite(UltraCanvas::PathToUtf8(dir), back, errors), "a site without a host saves");
    std::string siteJson;
    ReadTextFile(UltraCanvas::PathToUtf8(dir / "site.json"), siteJson);
    Check(!Contains(siteJson, "hosting"), "and writes no hosting member");
    fs::remove_all(dir, ec);

#if ULTRAPRESS_HAS_ULTRACLOUD
    std::printf("The hosts, over the in-memory provider\n");
    Hosts hosts;
    Check(hosts.OpenInMemory() && hosts.IsOpen() && hosts.List().empty(), "the host list opens in memory, empty");
    Check(Hosts::IsHostProvider("ftp") && Hosts::IsHostProvider("webdav") && Hosts::IsHostProvider("nextcloud") &&
              !Hosts::IsHostProvider("dropbox") && !Hosts::IsHostProvider("memory"),
          "the web hosts are the providers that take a folder of files");
    UltraCloud::Account demo;
    demo.providerId = "memory";
    demo.displayName = "Demo host";
    demo.username = "demo";
    demo.remoteFolder = "/www";
    UltraCloud::Credentials credentials;
    credentials.password = "secret";
    const UltraCloud::Result added = hosts.Add(demo, credentials, true);
    Check(added.IsOk() && !demo.accountId.empty() && hosts.List().size() == 1 && hosts.List()[0].isDefault,
          "a host is added, verified, and the first is the default: " + added.message);
    UltraCloud::Account found;
    Check(hosts.Find(demo.accountId, found).IsOk() && hosts.Find("Demo host", found).IsOk() && found.accountId == demo.accountId &&
              !hosts.Find("nobody", found).IsOk(),
          "a host is found by id or by name");
    Check(Contains(DescribeHost(found), demo.accountId) && Contains(DescribeHost(found), "(default)"), "the list line names it");

    DeployOptions onto;
    onto.remoteRoot = "/www";
    const DeployResult sent = DeployFolder(UltraCanvas::PathToUtf8(out), hosts.UploaderFor(demo.accountId), demo.accountId + ":/www", onto);
    for (const std::string& error : sent.errors) std::printf("  error: %s\n", error.c_str());
    Check(sent.ok && sent.uploaded.size() == 2, "the deploy goes through the host (the two files the pruning left): " + sent.Summary());
    const fs::path back2 = TempDir("downloaded") / "index.html";
    fs::create_directories(back2.parent_path(), ec);
    const UltraCloud::Result got = hosts.Service().Download(demo.accountId, "/www/index.html", UltraCanvas::PathToUtf8(back2));
    std::string text;
    Check(got.IsOk() && ReadTextFile(UltraCanvas::PathToUtf8(back2), text) && text == "<html>home, revised</html>",
          "and the file on the host is the file: " + got.message);
    const DeployResult again = DeployFolder(UltraCanvas::PathToUtf8(out), hosts.UploaderFor(demo.accountId), demo.accountId + ":/www", onto);
    Check(again.ok && again.uploaded.empty() && again.unchanged.size() == 2, "a second publish to the host sends nothing");
    fs::remove(out / "index.html", ec);
    DeployOptions pruneOnto = onto;
    pruneOnto.prune = true;
    const DeployResult cannotPrune = DeployFolder(UltraCanvas::PathToUtf8(out), hosts.UploaderFor(demo.accountId), demo.accountId + ":/www", pruneOnto);
    Check(!cannotPrune.ok && cannotPrune.notRemoved.size() == 1 && Contains(cannotPrune.errors.back(), "cannot delete"),
          "the in-memory provider cannot delete, and the prune says so: " + cannotPrune.Summary());
    std::string error;
    const Uploader closed = Hosts().UploaderFor("x");
    Check(!closed.upload("a", "/b", error) && Contains(error, "not open"), "an uploader of an unopened list refuses");
    Check(hosts.Remove(demo.accountId).IsOk() && hosts.List().empty(), "a host is removed");
    fs::remove_all(back2.parent_path(), ec);
#endif
    fs::remove_all(out, ec);
}

static void TestMedia() {
    std::printf("The media library\n");
    Check(MediaKind("Launch.PNG") == "image" && MediaKind("a.pdf") == "document" && MediaKind("clip.mp4") == "video" &&
              MediaKind("song.ogg") == "audio" && MediaKind("setup.zip") == "archive" && MediaKind("noext") == "other",
          "kinds from the extension, whatever its case");
    Check(HumanSize(512) == "512 B" && HumanSize(12 * 1024) == "12 KB" && HumanSize(3 * 1024 * 1024 + 400 * 1024) == "3.4 MB",
          "sizes for people");

    const fs::path dir = TempDir("media-site");
    const fs::path elsewhere = TempDir("media-source");
    std::error_code ec;
    std::vector<std::string> errors;
    Site site = CreateStarterSite("Pictures", "https://pictures.example");
    Check(SaveSite(UltraCanvas::PathToUtf8(dir), site, errors), "a site without media");
    Check(ListMedia(UltraCanvas::PathToUtf8(dir)).empty(), "an empty library");

    WriteTextFile(UltraCanvas::PathToUtf8(elsewhere / "My Launch Photo.PNG"), "not really a png");
    WriteTextFile(UltraCanvas::PathToUtf8(elsewhere / "Prospekt 2026.pdf"), "%PDF-1.7 pretend");
    std::string path, error;
    Check(ImportMedia(UltraCanvas::PathToUtf8(dir), UltraCanvas::PathToUtf8(elsewhere / "My Launch Photo.PNG"), path, error) &&
              path == "media/my-launch-photo.png" && fs::exists(dir / "media" / "my-launch-photo.png", ec),
          "a file is copied in under a clean name: " + path + " " + error);
    std::string second;
    Check(ImportMedia(UltraCanvas::PathToUtf8(dir), UltraCanvas::PathToUtf8(elsewhere / "My Launch Photo.PNG"), second, error) &&
              second == "media/my-launch-photo-2.png",
          "a second copy of the same name gets a number: " + second);
    std::string own;
    Check(ImportMedia(UltraCanvas::PathToUtf8(dir), UltraCanvas::PathToUtf8(dir / "media" / "my-launch-photo.png"), own, error) &&
              own == "media/my-launch-photo.png" && ListMedia(UltraCanvas::PathToUtf8(dir)).size() == 2,
          "a file already in the library is named, not copied again: " + own);
    std::string pdf;
    Check(ImportMedia(UltraCanvas::PathToUtf8(dir), UltraCanvas::PathToUtf8(elsewhere / "Prospekt 2026.pdf"), pdf, error) &&
              pdf == "media/prospekt-2026.pdf", "a document too: " + pdf);
    Check(!ImportMedia(UltraCanvas::PathToUtf8(dir), UltraCanvas::PathToUtf8(elsewhere / "missing.png"), path, error) && Contains(error, "not a file"),
          "a missing source is an error");

    std::vector<MediaFile> files = ListMedia(UltraCanvas::PathToUtf8(dir));
    Check(files.size() == 3 && files[0].path == "media/my-launch-photo-2.png" && files[1].name == "my-launch-photo.png" &&
              files[1].kind == "image" && files[1].size == 16 && files[2].kind == "document",
          "the list is in path order with name, kind and size");

    // References: the hero's picture, a downloads item, the social card.
    Page& home = site.pages[0];
    home.blocks[0].Set("picture", "media/my-launch-photo.png");
    home.seo.image = "media/my-launch-photo.png";
    Block downloads{"downloads"};
    downloads.Set("title", "Files");
    downloads.Set("items", JSONValue::MakeArray()
                               .Append(JSONValue::MakeObject().Set("title", "Prospekt").Set("file", "media/prospekt-2026.pdf"))
                               .Append(JSONValue::MakeObject().Set("title", "Elsewhere").Set("file", "https://example.org/x.zip")));
    home.blocks.push_back(downloads);
    Block columns{"columns"};
    Block inner{"picture"};
    inner.Set("src", "media/nowhere.png");
    columns.children.push_back(inner);
    home.blocks.push_back(columns);
    const std::vector<MediaReference> refs = MediaReferences(site);
    Check(refs.size() == 4 && refs[0].at == "seo.image" && refs[1].at == "blocks[0].picture" &&
              refs[2].at == "blocks[5].items[0].file" && Contains(refs[3].at, "children[0].src") && refs[3].file == "media/nowhere.png",
          "every media reference is found, children included, absolute URLs left out: " + std::to_string(refs.size()));
    files = ListMedia(UltraCanvas::PathToUtf8(dir), &site);
    Check(files[1].usedBy == 2 && files[2].usedBy == 1 && files[0].usedBy == 0, "the list counts the uses");
    const std::vector<ValidationIssue> issues = ValidateMedia(site, UltraCanvas::PathToUtf8(dir));
    Check(issues.size() == 1 && issues[0].page == "home" && Contains(issues[0].message, "media/nowhere.png"),
          "a reference to a file the site does not have is an issue");
    home.blocks[0].Set("picture", "pictures/outside.png");
    Check(ValidateMedia(site, UltraCanvas::PathToUtf8(dir)).size() == 2 &&
              Contains(ValidateMedia(site, UltraCanvas::PathToUtf8(dir))[0].message, "outside media/"),
          "a path outside media/ is an issue too");
    home.blocks[0].Set("picture", "media/my-launch-photo.png");

    Check(!RemoveMedia(UltraCanvas::PathToUtf8(dir), "media/my-launch-photo.png", error, &site) && Contains(error, "is used by home"),
          "a used file is not removed: " + error);
    Check(RemoveMedia(UltraCanvas::PathToUtf8(dir), "media/my-launch-photo-2.png", error, &site) &&
              ListMedia(UltraCanvas::PathToUtf8(dir)).size() == 2,
          "an unused one is");
    Check(!RemoveMedia(UltraCanvas::PathToUtf8(dir), "pages/home.json", error) && Contains(error, "not a file of the library") &&
              !RemoveMedia(UltraCanvas::PathToUtf8(dir), "media/../site.json", error),
          "nothing outside media/ can be removed through the library");
    Check(!RemoveMedia(UltraCanvas::PathToUtf8(dir), "media/gone.png", error) && Contains(error, "no such file"), "a missing file says so");

    std::printf("Pictures' pixels from their headers\n");
    auto write = [&](const std::string& name, const std::vector<unsigned char>& bytes) {
        std::ofstream out(elsewhere / name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return UltraCanvas::PathToUtf8(elsewhere / name);
    };
    int w = 0, h = 0;
    // PNG: signature, IHDR length, "IHDR", width 1600, height 900.
    Check(ImageDimensions(write("a.png", {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 0x0D, 'I', 'H', 'D', 'R',
                                          0, 0, 0x06, 0x40, 0, 0, 0x03, 0x84, 8, 6, 0, 0, 0}), w, h) && w == 1600 && h == 900,
          "PNG: " + std::to_string(w) + "x" + std::to_string(h));
    Check(ImageDimensions(write("a.gif", {'G', 'I', 'F', '8', '9', 'a', 0x2C, 0x01, 0xC8, 0x00, 0, 0, 0}), w, h) && w == 300 && h == 200,
          "GIF: " + std::to_string(w) + "x" + std::to_string(h));
    std::vector<unsigned char> bmp(30, 0);
    bmp[0] = 'B'; bmp[1] = 'M'; bmp[18] = 0x80; bmp[19] = 0x02; bmp[22] = 0xE0; bmp[23] = 0x01;
    Check(ImageDimensions(write("a.bmp", bmp), w, h) && w == 640 && h == 480, "BMP: " + std::to_string(w) + "x" + std::to_string(h));
    // JPEG: SOI, an APP0 segment of 16 bytes, then SOF0 with height 480 and width 720.
    std::vector<unsigned char> jpg = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10};
    jpg.insert(jpg.end(), 14, 0);
    const unsigned char sof[] = {0xFF, 0xC0, 0x00, 0x11, 0x08, 0x01, 0xE0, 0x02, 0xD0, 0x03};
    jpg.insert(jpg.end(), sof, sof + sizeof sof);
    jpg.insert(jpg.end(), 12, 0);
    Check(ImageDimensions(write("a.jpg", jpg), w, h) && w == 720 && h == 480, "JPEG: " + std::to_string(w) + "x" + std::to_string(h));
    // WebP VP8X: canvas width - 1 = 1023, height - 1 = 767 as 24-bit little-endian.
    std::vector<unsigned char> webp = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P', 'V', 'P', '8', 'X', 10, 0, 0, 0, 0, 0, 0, 0,
                                       0xFF, 0x03, 0x00, 0xFF, 0x02, 0x00, 0, 0};
    Check(ImageDimensions(write("a.webp", webp), w, h) && w == 1024 && h == 768, "WebP: " + std::to_string(w) + "x" + std::to_string(h));
    Check(!ImageDimensions(write("a.txt", {'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd', '!'}), w, h) && w == 0 &&
              !ImageDimensions(UltraCanvas::PathToUtf8(elsewhere / "nothing.png"), w, h),
          "not a picture, or no file: no pixels");
    std::string pngPath;
    Check(ImportMedia(UltraCanvas::PathToUtf8(dir), UltraCanvas::PathToUtf8(elsewhere / "a.png"), pngPath, error), "the PNG is imported");
    {
        int listedW = 0, listedH = 0;
        for (const MediaFile& file : ListMedia(UltraCanvas::PathToUtf8(dir))) {
            if (file.path == pngPath) { listedW = file.width; listedH = file.height; }
        }
        Check(listedW == 1600 && listedH == 900, "the list carries a picture's pixels: " + std::to_string(listedW) + "x" + std::to_string(listedH));
    }

    std::printf("Clean names and the prune\n");
    std::set<std::string> taken = {"launch.png"};
    auto isTaken = [&taken](const std::string& name) { return taken.count(name) > 0; };
    Check(CleanMediaName("Launch.PNG", isTaken) == "launch-2.png" && CleanMediaName("Desktop Shot.jpeg", isTaken) == "desktop-shot.jpeg" &&
              CleanMediaName("README", isTaken) == "readme" && CleanMediaName(".hidden", isTaken) == "hidden",
          "a clean name: slugged stem, lower-case extension, numbered when taken");
    std::vector<std::string> removed, pruneErrors;
    Check(PruneMedia(UltraCanvas::PathToUtf8(dir), site, removed, pruneErrors) && removed.size() == 1 && removed[0] == "media/a.png" &&
              ListMedia(UltraCanvas::PathToUtf8(dir)).size() == 2 && pruneErrors.empty(),
          "prune removes every unused file and no used one: " + std::to_string(removed.size()));
    removed.clear();
    Check(PruneMedia(UltraCanvas::PathToUtf8(dir), site, removed, pruneErrors) && removed.empty(), "nothing unused, nothing removed");
    fs::remove_all(dir, ec);
    fs::remove_all(elsewhere, ec);
}

static void TestShadowImageSizes() {
    std::printf("The shadow's picture sizes\n");
    const fs::path dir = TempDir("shadow-sizes");
    std::error_code ec;
    fs::create_directories(dir / "media", ec);
    {
        // A PNG header: 1600 x 900.
        std::ofstream out(dir / "media" / "wide.png", std::ios::binary);
        const unsigned char bytes[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 0x0D, 'I', 'H', 'D', 'R',
                                       0, 0, 0x06, 0x40, 0, 0, 0x03, 0x84, 8, 6, 0, 0, 0};
        out.write(reinterpret_cast<const char*>(bytes), sizeof bytes);
    }
    Site site = CreateStarterSite("Sizes", "https://sizes.example");
    site.pages[0].blocks[0].Set("picture", "media/wide.png");
    site.pages[0].seo.image = "media/wide.png";
    Block picture{"picture"};
    picture.Set("src", "media/missing.png").Set("alt", "gone");
    site.pages[0].blocks.push_back(picture);
    Block remote{"picture"};
    remote.Set("src", "https://elsewhere.example/far.png").Set("alt", "far");
    site.pages[0].blocks.push_back(remote);
    HtmlOptions options;
    options.siteDirectory = UltraCanvas::PathToUtf8(dir);
    const std::string html = HtmlEmitter(site, options).Page(site.pages[0]);
    Check(Contains(html, "<img src=\"/media/wide.png\" alt=\"\" width=\"1600\" height=\"900\">"), "a picture in the folder carries its width and height");
    Check(Contains(html, "<img src=\"/media/missing.png\" alt=\"gone\">"), "a missing one carries none");
    Check(Contains(html, "<img src=\"https://elsewhere.example/far.png\" alt=\"far\">"), "an absolute URL carries none");
    Check(Contains(html, "<meta property=\"og:image:width\" content=\"1600\">") && Contains(html, "<meta property=\"og:image:height\" content=\"900\">"),
          "the social card names its size");
    const std::string without = HtmlEmitter(site).Page(site.pages[0]);
    Check(!Contains(without, "width=\"1600\"") && !Contains(without, "og:image:width"), "without the site folder, no sizes");
    const PublishResult published = PublishStatic(site, UltraCanvas::PathToUtf8(dir), UltraCanvas::PathToUtf8(dir / "out"));
    std::string index;
    Check(published.ok && ReadTextFile(UltraCanvas::PathToUtf8(dir / "out" / "index.html"), index) && Contains(index, "width=\"1600\" height=\"900\""),
          "a publish passes the site folder on");
    fs::remove_all(dir, ec);
}

static void TestTextInBlocks() {
    std::printf("Text in a block\n");
    const std::vector<std::string> paragraphs = SplitParagraphs("  First.\n\n\nSecond line one\nline two  \n \n");
    Check(paragraphs.size() == 2 && paragraphs[0] == "First." && paragraphs[1] == "Second line one\nline two",
          "paragraphs split on blank lines and are trimmed");
    Check(SplitParagraphs("").empty() && SplitParagraphs(" \n ").empty(), "no text, no paragraphs");

    Block text{"text"};
    text.Set("heading", "Old").Set("text", "One.\n\nTwo.\n\nThree.");
    Check(SetBlockText(text, {"heading"}, "New") && text.Text("heading") == "New", "a property is replaced whole");
    Check(SetBlockText(text, TextLocation{"text", -1, "", 1}, "Two, revised.") && text.Text("text") == "One.\n\nTwo, revised.\n\nThree.",
          "one paragraph is replaced, the others kept: " + text.Text("text"));
    Check(!SetBlockText(text, TextLocation{"text", -1, "", 5}, "x"), "a paragraph past the end is refused");
    Check(!SetBlockText(text, {""}, "x"), "no property, nothing written");

    Block features{"features"};
    features.Set("items", JSONValue::MakeArray().Append("A").Append("B").Append("C"));
    Check(SetBlockText(features, TextLocation{"items", 1, "", -1}, "Bee") && features.Items("items")[1].GetString() == "Bee" &&
              features.Items("items").GetSize() == 3 && features.Items("items")[2].GetString() == "C",
          "an item of a Strings property is replaced");
    Check(!SetBlockText(features, TextLocation{"items", 3, "", -1}, "x"), "an item past the end is refused");

    Block faq{"faq"};
    faq.Set("items", JSONValue::MakeArray()
                         .Append(JSONValue::MakeObject().Set("q", "Why?").Set("a", "Because.\n\nAnd more."))
                         .Append(JSONValue::MakeObject().Set("q", "How?").Set("a", "Like this.")));
    Check(SetBlockText(faq, TextLocation{"items", 0, "q", -1}, "Why not?") && faq.Items("items")[0]["q"].GetString() == "Why not?" &&
              faq.Items("items")[0]["a"].GetString() == "Because.\n\nAnd more.",
          "an item's key is replaced, its other keys kept");
    Check(SetBlockText(faq, TextLocation{"items", 0, "a", 1}, "And less.") && faq.Items("items")[0]["a"].GetString() == "Because.\n\nAnd less.",
          "a paragraph of an item's key is replaced");
    Check(faq.Items("items")[1]["q"].GetString() == "How?", "the other item is untouched");

    Page page;
    page.blocks.push_back(Block{"hero"});
    Block columns{"columns"};
    columns.children.push_back(Block{"card"});
    columns.children.push_back(Block{"picture"});
    page.blocks.push_back(columns);
    Check(ResolveBlockPath(page, "blocks[0]") == &page.blocks[0] && ResolveBlockPath(page, "blocks[1]")->type == "columns" &&
              ResolveBlockPath(page, "blocks[1].children[1]")->type == "picture",
          "a block path resolves, children included");
    Check(!ResolveBlockPath(page, "blocks[2]") && !ResolveBlockPath(page, "blocks[1].children[2]") && !ResolveBlockPath(page, "blocks[x]") &&
              !ResolveBlockPath(page, "pages[0]") && !ResolveBlockPath(page, "blocks[0]x"),
          "a path past the end, or malformed, resolves to nothing");
}

int main() {
    TestStarterRoundTrip();
    TestValidation();
    TestShadowPage();
    TestSitemapAndFeed();
    TestPublishAndExample();
    TestWordPressImport();
    TestDeploy();
    TestMedia();
    TestTextInBlocks();
    TestShadowImageSizes();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
