// Apps/UltraPress/engine/UltraPressPublish.cpp
// Static publishing and the starter site.
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressPublish.h"

#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using UltraCanvas::JSONValue;

namespace UltraPress {

bool ReadTextFile(const std::string& path, std::string& out) {
    std::ifstream in(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

bool WriteTextFile(const std::string& path, const std::string& text) {
    std::ofstream out(UltraCanvas::PathFromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    return static_cast<bool>(out);
}

namespace {

const char* kHtaccess =
    "# Written by UltraPress. The renderer and its data are named by content\n"
    "# hash, so they may be cached for a year; pages change on every publish.\n"
    "AddType application/wasm .wasm\n"
    "<IfModule mod_headers.c>\n"
    "  <FilesMatch \"\\.(wasm|js|data)$\">\n"
    "    Header set Cache-Control \"public, max-age=31536000, immutable\"\n"
    "  </FilesMatch>\n"
    "  <FilesMatch \"\\.(html|xml|txt)$\">\n"
    "    Header set Cache-Control \"no-cache\"\n"
    "  </FilesMatch>\n"
    "</IfModule>\n"
    "<IfModule mod_deflate.c>\n"
    "  AddOutputFilterByType DEFLATE text/html text/plain text/xml application/xml application/wasm application/javascript\n"
    "</IfModule>\n";

bool WriteOutput(const fs::path& outDir, const std::string& relative, const std::string& text, PublishResult& result) {
    const fs::path path = outDir / UltraCanvas::PathFromUtf8(relative);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (!WriteTextFile(UltraCanvas::PathToUtf8(path), text)) {
        result.errors.push_back("cannot write " + relative);
        return false;
    }
    result.files.push_back(relative);
    return true;
}

} // namespace

PublishResult PublishStatic(const Site& site, const std::string& siteDirectory,
                            const std::string& outputDirectory, const PublishOptions& options) {
    PublishResult result;
    result.issues = ValidateSite(site);
    const fs::path outDir = UltraCanvas::PathFromUtf8(outputDirectory);
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        result.errors.push_back("cannot create " + outputDirectory + ": " + ec.message());
        return result;
    }
    HtmlEmitter emitter(site, options.html);
    bool ok = true;
    for (const Page& page : site.pages) {
        if (!page.IsPublished() && !options.includeDrafts) continue;
        const std::string relative = page.slug == "home" ? "index.html" : page.slug + "/index.html";
        ok = WriteOutput(outDir, relative, emitter.Page(page), result) && ok;
    }
    ok = WriteOutput(outDir, "sitemap.xml", emitter.Sitemap(), result) && ok;
    ok = WriteOutput(outDir, "robots.txt", emitter.Robots(), result) && ok;
    ok = WriteOutput(outDir, "feed.xml", emitter.Feed(), result) && ok;
    if (options.writeHtaccess) ok = WriteOutput(outDir, ".htaccess", kHtaccess, result) && ok;
    if (options.copyMedia) {
        const fs::path media = UltraCanvas::PathFromUtf8(siteDirectory) / "media";
        if (fs::is_directory(media, ec)) {
            fs::copy(media, outDir / "media", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) {
                result.errors.push_back("cannot copy media: " + ec.message());
                ok = false;
            } else {
                result.files.push_back("media/");
            }
        }
    }
    result.ok = ok;
    return result;
}

Site CreateStarterSite(const std::string& name, const std::string& url, const std::string& language) {
    Site site;
    site.name = name;
    site.tagline = "A new site, made with UltraPress";
    site.url = url;
    while (!site.url.empty() && site.url.back() == '/') site.url.pop_back();
    site.language = language;
    site.owner = name;
    site.theme.name = "default";
    site.theme.colors = {{"accent", "#a90707"}, {"ink", "#202020"}, {"paper", "#ffffff"}, {"night", "#0d0c22"}};
    site.theme.fontFamily = "Ubuntu";

    Menu main{"main", {{"Home", "home", "", {}}, {"About", "about", "", {}}}};
    Menu footer{"footer", {{"About", "about", "", {}}, {"Contact", "home", "", {}}}};
    site.menus = {main, footer};

    Page home;
    home.slug = "home";
    home.title = name;
    home.pinned = true;
    home.seo.description = "What " + name + " is, and what it offers.";
    {
        Block hero{"hero"};
        hero.Set("kicker", "Welcome");
        hero.Set("title", name);
        hero.Set("text", "Say in one or two sentences what this site is for.");
        JSONValue buttons = JSONValue::MakeArray();
        buttons.Append(JSONValue::MakeObject().Set("title", "Learn more").Set("page", "about").Set("primary", true));
        hero.Set("buttons", buttons);
        home.blocks.push_back(hero);
    }
    {
        Block text{"text"};
        text.Set("heading", "Introduction");
        text.Set("text", "Replace this paragraph with your own.\n\nA blank line starts a new paragraph.");
        home.blocks.push_back(text);
    }
    {
        Block features{"features"};
        features.Set("heading", "Why");
        JSONValue items = JSONValue::MakeArray();
        items.Append("The first reason");
        items.Append("The second reason");
        items.Append("The third reason");
        features.Set("items", items);
        home.blocks.push_back(features);
    }
    {
        Block faq{"faq"};
        faq.Set("heading", "Questions");
        JSONValue items = JSONValue::MakeArray();
        items.Append(JSONValue::MakeObject().Set("q", "What is this?").Set("a", "A site made with UltraPress."));
        items.Append(JSONValue::MakeObject().Set("q", "How do I change it?").Set("a", "Edit the page in UltraPress and publish again."));
        faq.Set("items", items);
        home.blocks.push_back(faq);
    }
    {
        Block form{"form"};
        form.Set("id", "contact");
        form.Set("heading", "Contact");
        JSONValue fields = JSONValue::MakeArray();
        fields.Append(JSONValue::MakeObject().Set("name", "name").Set("label", "Name").Set("type", "text").Set("required", true));
        fields.Append(JSONValue::MakeObject().Set("name", "email").Set("label", "E-mail").Set("type", "email").Set("required", true));
        fields.Append(JSONValue::MakeObject().Set("name", "message").Set("label", "Message").Set("type", "textarea").Set("required", true));
        form.Set("fields", fields);
        form.Set("submit", "Send");
        home.blocks.push_back(form);
    }
    site.pages.push_back(home);

    Page about;
    about.slug = "about";
    about.title = "About";
    about.seo.description = "Who is behind " + name + ".";
    {
        Block text{"text"};
        text.Set("text", "Who you are and what you do.");
        about.blocks.push_back(text);
        Block quote{"quote"};
        quote.Set("text", "A sentence someone said about you.");
        quote.Set("cite", "Someone");
        about.blocks.push_back(quote);
    }
    site.pages.push_back(about);
    return site;
}

} // namespace UltraPress
