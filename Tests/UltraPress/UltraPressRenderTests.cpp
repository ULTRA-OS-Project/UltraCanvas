// Tests/UltraPress/UltraPressRenderTests.cpp
// The block renderer without a window: every block type becomes elements,
// the texts are on labels, links call back with the right address, the FAQ
// accordion opens and closes, a form hands its fields to the callback, the
// theme's colours reach the palette, and the example site renders with no
// warning but its missing pictures.
// Version: 0.2.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressBlocks.h"
#include "UltraPressModel.h"
#include "UltraPressPublish.h"
#include "UltraPressRender.h"
#include "UltraPressWindow.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextInput.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using namespace UltraPress;
using UltraCanvas::JSONValue;

static int g_failures = 0;
static int g_checks = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

// Every element of a tree, depth first.
static void Walk(const std::shared_ptr<UltraCanvas::UltraCanvasUIElement>& root,
                 const std::function<void(const std::shared_ptr<UltraCanvas::UltraCanvasUIElement>&)>& visit) {
    if (!root) return;
    visit(root);
    if (auto container = std::dynamic_pointer_cast<UltraCanvas::UltraCanvasContainer>(root)) {
        for (const auto& child : container->GetChildren()) Walk(child, visit);
    }
}

static std::shared_ptr<UltraCanvas::UltraCanvasLabel> FindLabel(const std::shared_ptr<UltraCanvas::UltraCanvasUIElement>& root,
                                                                const std::string& text) {
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> found;
    Walk(root, [&](const auto& element) {
        if (found) return;
        if (auto label = std::dynamic_pointer_cast<UltraCanvas::UltraCanvasLabel>(element)) {
            if (label->GetText() == text) found = label;
        }
    });
    return found;
}

static std::shared_ptr<UltraCanvas::UltraCanvasButton> FindButton(const std::shared_ptr<UltraCanvas::UltraCanvasUIElement>& root,
                                                                  const std::string& text) {
    std::shared_ptr<UltraCanvas::UltraCanvasButton> found;
    Walk(root, [&](const auto& element) {
        if (found) return;
        if (auto button = std::dynamic_pointer_cast<UltraCanvas::UltraCanvasButton>(element)) {
            if (button->GetText() == text) found = button;
        }
    });
    return found;
}

template <typename T>
static std::shared_ptr<T> FindById(const std::shared_ptr<UltraCanvas::UltraCanvasUIElement>& root, const std::string& id) {
    std::shared_ptr<T> found;
    Walk(root, [&](const auto& element) {
        if (found) return;
        if (element->GetIdentifier() == id) found = std::dynamic_pointer_cast<T>(element);
    });
    return found;
}

static void TestStarterSite() {
    std::printf("The starter site on elements\n");
    Site site = CreateStarterSite("Render & Co", "https://render.example");
    std::vector<std::string> links;
    std::string formId;
    std::vector<std::pair<std::string, std::string>> formFields;
    RenderOptions options;
    options.onLink = [&](const std::string& href) { links.push_back(href); };
    options.onFormSubmit = [&](const std::string& id, const std::vector<std::pair<std::string, std::string>>& fields) {
        formId = id;
        formFields = fields;
    };
    PageRenderer renderer(site, options);
    RenderedPage home = renderer.Render(site.pages[0]);
    Check(home.root != nullptr, "the front page renders");
    Check(home.warnings.empty(), "no warnings: " + (home.warnings.empty() ? std::string() : home.warnings[0]));
    Check(home.elementCount > 40, "the front page is a real tree: " + std::to_string(home.elementCount));
    Check(FindLabel(home.root, "Render & Co") != nullptr, "the hero title is a label with the site's name");
    Check(FindLabel(home.root, "WELCOME") != nullptr, "the kicker is upper-case");
    Check(FindLabel(home.root, "Replace this paragraph with your own.") != nullptr &&
              FindLabel(home.root, "A blank line starts a new paragraph.") != nullptr,
          "paragraphs are separate labels");
    Check(FindLabel(home.root, "The second reason") != nullptr, "a feature item is on a label");

    auto learnMore = FindButton(home.root, "Learn more");
    Check(learnMore != nullptr, "the hero button exists");
    if (learnMore && learnMore->onClick) learnMore->onClick();
    Check(links.size() == 1 && links[0] == "/about/", "the hero's page link resolves to the page's path");
    auto about = FindButton(home.root, "About");
    Check(about != nullptr, "the main menu has its item");
    if (about && about->onClick) about->onClick();
    Check(links.size() == 2 && links[1] == "/about/", "a menu item calls back with the page's path");
    auto brand = FindButton(home.root, "Render & Co");
    if (brand && brand->onClick) brand->onClick();
    Check(links.size() == 3 && links[2] == "/", "the brand leads to the front page");

    // The FAQ accordion: the first answer is open, the second closed; a click swaps.
    auto first = FindButton(home.root, "-  What is this?");
    auto second = FindButton(home.root, "+  How do I change it?");
    Check(first != nullptr && second != nullptr, "the questions are buttons with their state in front");
    auto secondAnswer = FindById<UltraCanvas::UltraCanvasContainer>(home.root, "B3Entry1Answer");
    Check(secondAnswer != nullptr && !secondAnswer->IsVisible(), "the second answer starts closed");
    if (second && second->onClick) second->onClick();
    Check(secondAnswer != nullptr && secondAnswer->IsVisible() && second->GetText() == "-  How do I change it?",
          "a click opens the answer and turns the sign");

    // The form: fields reach the callback with what was typed.
    auto name = FindById<UltraCanvas::UltraCanvasTextInput>(home.root, "B4Field0");
    Check(name != nullptr, "the form's first field is a text input");
    if (name) name->SetText("Ada");
    auto send = FindButton(home.root, "Send");
    Check(send != nullptr, "the form has its button");
    if (send && send->onClick) send->onClick();
    Check(formId == "contact" && formFields.size() == 3 && formFields[0].first == "name" && formFields[0].second == "Ada" &&
              formFields[2].first == "message",
          "the submit hands over the form id and the fields");

    RenderedPage aboutPage = renderer.Render(site.pages[1]);
    Check(FindLabel(aboutPage.root, "A sentence someone said about you.") != nullptr &&
              FindLabel(aboutPage.root, "\xE2\x80\x94 Someone") != nullptr,
          "the quote and its source render");
}

static void TestEveryBlock() {
    std::printf("Every block type\n");
    Site site = CreateStarterSite("All", "https://all.example");
    Page page;
    page.slug = "all";
    page.title = "All";
    auto add = [&](const char* type, std::function<void(Block&)> fill) {
        Block block{type};
        fill(block);
        page.blocks.push_back(block);
    };
    add("heading", [](Block& b) { b.Set("text", "A heading").Set("level", 3); });
    add("richtext", [](Block& b) { b.Set("html", "<p>Rich <b>text</b></p>"); });
    add("html", [](Block& b) { b.Set("html", "<ul><li>one</li><li>two</li></ul>"); });
    add("picture", [](Block& b) { b.Set("src", "media/missing.png").Set("alt", "A missing picture").Set("caption", "Caption"); });
    add("columns", [](Block& b) {
        b.Set("heading", "Three");
        for (int i = 0; i < 3; ++i) {
            Block card{"card"};
            card.Set("title", "Card " + std::to_string(i)).Set("text", "Text").Set("link", JSONValue::MakeObject().Set("page", "about"));
            b.children.push_back(card);
        }
    });
    add("cta", [](Block& b) {
        b.Set("title", "Act").Set("buttons", JSONValue::MakeArray().Append(JSONValue::MakeObject().Set("title", "Go").Set("url", "https://x.example")));
    });
    add("buttons", [](Block& b) { b.Set("items", JSONValue::MakeArray().Append(JSONValue::MakeObject().Set("title", "One").Set("page", "home"))); });
    add("newsletter", [](Block& b) { b.Set("heading", "News").Set("list", "weekly"); });
    add("downloads", [](Block& b) {
        b.Set("items", JSONValue::MakeArray().Append(JSONValue::MakeObject().Set("title", "The file").Set("file", "media/file.zip").Set("version", "1.0").Set("size", "2 MB")));
    });
    add("spacer", [](Block& b) { b.Set("height", 48); });
    add("unknown", [](Block&) {});
    site.pages.push_back(page);

    std::string list, email;
    RenderOptions options;
    options.onNewsletterSubscribe = [&](const std::string& l, const std::string& e) { list = l; email = e; };
    options.onLink = [](const std::string&) {};
    PageRenderer renderer(site, options);
    RenderedPage rendered = renderer.Render(page);
    Check(rendered.warnings.size() == 2, "two warnings, the missing picture and the unknown block: " + std::to_string(rendered.warnings.size()));
    bool missing = false, unknown = false;
    for (const std::string& warning : rendered.warnings) {
        if (warning.find("picture not found") != std::string::npos) missing = true;
        if (warning.find("unknown block type: unknown") != std::string::npos) unknown = true;
    }
    Check(missing && unknown, "the warnings name the picture and the block");
    Check(FindLabel(rendered.root, "A heading") != nullptr, "heading");
    Check(FindLabel(rendered.root, "A missing picture") != nullptr, "a missing picture is a tile with its alt text");
    Check(FindLabel(rendered.root, "Caption") != nullptr, "the picture's caption");
    Check(FindLabel(rendered.root, "Card 2") != nullptr && FindButton(rendered.root, "Read more") != nullptr, "cards in columns with their links");
    Check(FindLabel(rendered.root, "Act") != nullptr && FindButton(rendered.root, "Go") != nullptr, "the call to action");
    Check(FindButton(rendered.root, "One") != nullptr, "a buttons block");
    Check(FindButton(rendered.root, "The file") != nullptr && FindLabel(rendered.root, "version 1.0, 2 MB") != nullptr, "downloads with details");
    Check(rendered.elementCount > 60, "rich text and html blocks built elements too: " + std::to_string(rendered.elementCount));
    auto emailField = FindById<UltraCanvas::UltraCanvasTextInput>(rendered.root, "B7Email");
    Check(emailField != nullptr, "the newsletter's address field");
    if (emailField) emailField->SetText("ada@example.org");
    auto subscribe = FindButton(rendered.root, "Subscribe");
    if (subscribe && subscribe->onClick) subscribe->onClick();
    Check(list == "weekly" && email == "ada@example.org", "the newsletter callback gets the list and the address");
}

static void TestPaletteAndExample() {
    std::printf("Theme colours and the example site\n");
    UltraCanvas::Color color;
    Check(ParseHexColor("#a90707", color) && color.r == 169 && color.g == 7 && color.b == 7 && color.a == 255, "#rrggbb parses");
    Check(ParseHexColor("#10203040", color) && color.a == 0x40, "#rrggbbaa parses");
    Check(!ParseHexColor("a90707", color) && !ParseHexColor("#zz0000", color), "bad colours are refused");
    Theme theme;
    theme.colors = {{"accent", "#0000ff"}, {"paper", "#fafafa"}};
    theme.fontFamily = "Ubuntu Mono";
    const Palette palette = PaletteFromTheme(theme);
    Check(palette.accent.b == 255 && palette.accent.r == 0 && palette.paper.r == 250 && palette.font == "Ubuntu Mono",
          "the theme's colours and font reach the palette");
    Check(palette.ink.r == 32, "a colour the theme does not name keeps its default");

    Site site;
    std::vector<std::string> errors;
    Check(LoadSite(ULTRAPRESS_EXAMPLE_DIR, site, errors), "the example site loads");
    RenderOptions options;
    options.mediaRoot = ULTRAPRESS_EXAMPLE_DIR;
    options.onLink = [](const std::string&) {};
    PageRenderer renderer(site, options);
    for (const Page& page : site.pages) {
        RenderedPage rendered = renderer.Render(page);
        Check(rendered.elementCount > 20, page.slug + " renders: " + std::to_string(rendered.elementCount) + " elements");
        Check(rendered.warnings.empty(), page.slug + " renders without a warning");
    }
    RenderedPage home = renderer.Render(*site.FindPage("home"));
    Check(FindLabel(home.root, "INTRODUCING ULTRA OS") != nullptr, "the front page's kicker");
    Check(home.menus.size() == 1, "the one drop-down of the main menu is kept alive with the page");
    Check(FindButton(home.root, "Details") != nullptr && FindButton(home.root, "Donate") != nullptr, "the header and footer menus");
}

static void TestEditor() {
    std::printf("The editor without a window\n");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() /
        ("ultrapress-editor-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::vector<std::string> errors;
    Check(SaveSite(UltraCanvas::PathToUtf8(dir), CreateStarterSite("Edit", "https://edit.example"), errors), "a site to edit");

    Site site;
    Check(LoadSite(UltraCanvas::PathToUtf8(dir), site, errors), "it loads");
    EditorWindow editor(site, UltraCanvas::PathToUtf8(dir));
    editor.ShowPage("home");
    Check(editor.CurrentPage() == "home" && editor.SelectedBlock() == -1, "the front page is shown, no block selected");
    Check(editor.CurrentSite().FindPage("home")->blocks.size() == 5, "five blocks to start");

    editor.AddBlock("quote");
    Check(editor.CurrentSite().FindPage("home")->blocks.size() == 6 && editor.SelectedBlock() == 5 &&
              editor.CurrentSite().FindPage("home")->blocks[5].type == "quote",
          "a block is appended and selected when none was selected");
    editor.SelectBlock(0);
    editor.AddBlock("cta");
    const Page* home = editor.CurrentSite().FindPage("home");
    Check(home->blocks.size() == 7 && home->blocks[1].type == "cta" && editor.SelectedBlock() == 1,
          "a block is inserted after the selected one");
    Check(ValidateSite(editor.CurrentSite()).empty(), "new blocks carry defaults that validate");
    editor.AddBlock("no-such-block");
    Check(editor.CurrentSite().FindPage("home")->blocks.size() == 7, "an unknown type adds nothing");

    editor.MoveBlock(1, 1);
    home = editor.CurrentSite().FindPage("home");
    Check(home->blocks[2].type == "cta" && home->blocks[1].type == "text" && editor.SelectedBlock() == 2, "a block moves down and stays selected");
    editor.MoveBlock(0, -1);
    Check(editor.CurrentSite().FindPage("home")->blocks[0].type == "hero", "the first block cannot move up");
    editor.RemoveBlock(2);
    home = editor.CurrentSite().FindPage("home");
    Check(home->blocks.size() == 6 && home->blocks[2].type == "features", "a block is removed");
    editor.RemoveBlock(99);
    Check(editor.CurrentSite().FindPage("home")->blocks.size() == 6, "an index past the end removes nothing");

    Check(editor.Save(), "the site saves");
    Site saved;
    Check(LoadSite(UltraCanvas::PathToUtf8(dir), saved, errors) && saved.FindPage("home")->blocks.size() == 6 &&
              saved.FindPage("home")->blocks[5].type == "quote",
          "the saved site has the edits");
    editor.ShowPage("about");
    Check(editor.CurrentPage() == "about", "another page can be shown");
    editor.ShowPage("missing");
    editor.AddBlock("text");
    Check(editor.CurrentPage() == "missing" && ValidateSite(editor.CurrentSite()).empty(), "a page that does not exist takes no block");
    fs::remove_all(dir, ec);
}

int main() {
    TestStarterSite();
    TestEveryBlock();
    TestPaletteAndExample();
    TestEditor();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
