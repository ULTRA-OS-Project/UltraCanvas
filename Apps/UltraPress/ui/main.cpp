// Apps/UltraPress/ui/main.cpp
// UltraPress: the site in a window - the page list on the left, the page
// rendered on real elements on the right, the same renderer that becomes
// the published page under Emscripten. 0.2.0 shows; editing follows.
//
//   UltraPress <site-dir>            open a site folder
//   UltraPress <site-dir> --check    render every page without a window and
//                                    print the element counts and warnings
//   UltraPress --version
// Version: 0.2.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressBlocks.h"
#include "UltraPressModel.h"
#include "UltraPressRender.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef ULTRAPRESS_VERSION
#error "ULTRAPRESS_VERSION comes from the first line of Docs/UltraPress/CHANGELOG.md through CMake; never a literal here"
#endif

using namespace UltraCanvas;

namespace {

const Color kNight(13, 12, 34, 255);
const Color kPaper(255, 255, 255, 255);
const Color kOnDark(208, 208, 220, 255);
const Color kAccent(169, 7, 7, 255);

int Usage() {
    std::fprintf(stderr,
                 "UltraPress %s\n"
                 "  UltraPress <site-dir>           open a site folder\n"
                 "  UltraPress <site-dir> --check   render every page without a window, print what was built\n"
                 "  UltraPress --version\n",
                 ULTRAPRESS_VERSION);
    return 2;
}

int RunCheck(const UltraPress::Site& site, const std::string& siteDir) {
    UltraPress::RenderOptions options;
    options.mediaRoot = siteDir;
    options.onLink = [](const std::string&) {};
    UltraPress::PageRenderer renderer(site, options);
    int failures = 0;
    for (const UltraPress::Page& page : site.pages) {
        UltraPress::RenderedPage rendered = renderer.Render(page);
        std::printf("%s: %d elements\n", page.slug.c_str(), rendered.elementCount);
        for (const std::string& warning : rendered.warnings) {
            std::printf("  %s\n", warning.c_str());
            if (warning.rfind("unknown block", 0) == 0) ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}

// The window: a dark column with the site's name and one button per page,
// and the page beside it, rebuilt when another is chosen.
struct Viewer {
    UltraPress::Site site;
    std::string siteDir;
    std::shared_ptr<UltraCanvasContainer> pageArea;
    UltraPress::RenderedPage shown;
    std::string current;

    void Show(const std::string& slug) {
        const UltraPress::Page* page = site.FindPage(slug);
        if (!page || !pageArea) return;
        current = slug;
        pageArea->ClearChildren();
        UltraPress::RenderOptions options;
        options.mediaRoot = siteDir;
        options.onLink = [this](const std::string& href) {
            // A site path opens that page here; anything else leaves for the browser.
            if (!href.empty() && href[0] == '/') {
                const std::string target = href == "/" ? "home" : href.substr(1, href.size() - 2);
                if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
                    app->PostToUIThread([this, target]() { Show(target); });
                }
            } else {
                OpenURL(href);
            }
        };
        options.onFormSubmit = [](const std::string& id, const std::vector<std::pair<std::string, std::string>>& fields) {
            std::printf("form %s: %zu fields (no server tier yet)\n", id.c_str(), fields.size());
        };
        options.onNewsletterSubscribe = [](const std::string& list, const std::string& email) {
            std::printf("newsletter %s: %s (no server tier yet)\n", list.c_str(), email.c_str());
        };
        UltraPress::PageRenderer renderer(site, options);
        shown = renderer.Render(*page);
        pageArea->AddChild(shown.root);
    }
};

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) return Usage();
    if (args[0] == "--version" || args[0] == "-v") {
        std::printf("UltraPress %s\n", ULTRAPRESS_VERSION);
        return 0;
    }
    const std::string siteDir = args[0];
    bool check = false;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--check") check = true;
        else return Usage();
    }

    // Static lifetime: under Emscripten Run() never returns and unwinds
    // main()'s frame while the loop keeps ticking.
    static Viewer viewer;
    viewer.siteDir = siteDir;
    std::vector<std::string> errors;
    if (!UltraPress::LoadSite(siteDir, viewer.site, errors)) {
        for (const std::string& error : errors) std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    for (const UltraPress::ValidationIssue& issue : UltraPress::ValidateSite(viewer.site)) {
        std::fprintf(stderr, "%s %s: %s\n", issue.page.c_str(), issue.path.c_str(), issue.message.c_str());
    }
    if (check) return RunCheck(viewer.site, siteDir);

    static UltraCanvasApplication app;
    if (!app.Initialize(argv[0])) return 1;

    WindowConfig cfg;
    cfg.title = viewer.site.name + " - UltraPress " ULTRAPRESS_VERSION;
    cfg.width = 1400;
    cfg.height = 900;
    cfg.backgroundColor = kPaper;
    auto window = CreateWindow(cfg);
    if (!window) return 1;

    auto frame = CreateContainer("Frame");
    frame->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Pct(100));
    frame->layout.SetFlexRow();

    auto side = CreateContainer("Side");
    side->SetBackgroundColor(kNight);
    side->SetElementSize(CSSLayout::Dimension::Px(240), CSSLayout::Dimension::Pct(100));
    side->layout.SetDisplay(CSSLayout::DisplayType::Block);
    side->SetPadding(16);
    side->layoutItem.SetFlexShrink(0);
    auto title = CreateLabel("SideTitle", viewer.site.name);
    title->SetFont("Ubuntu", 18, FontWeight::Bold);
    title->SetTextColor(kPaper);
    title->SetWrap(TextWrap::WrapWord);
    title->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    side->AddChild(title);
    auto subtitle = CreateLabel("SideSubtitle", "Pages");
    subtitle->SetFont("Ubuntu", 12, FontWeight::Bold);
    subtitle->SetTextColor(kAccent);
    subtitle->SetMargin(18, 0, 6, 0);
    side->AddChild(subtitle);
    for (const UltraPress::Page* page : UltraPress::SortedPages(viewer.site, UltraPress::PageOrder::Stored)) {
        auto button = CreateButton("Page" + page->slug, 0, 0, 0, 32,
                                   (page->pinned ? "\xE2\x98\x85 " : "") + page->title +
                                       (page->IsPublished() ? "" : " (" + page->status + ")"));
        button->SetFont("Ubuntu", 14);
        button->SetColors(Colors::Transparent, Color(255, 255, 255, 24));
        button->SetTextColors(kOnDark, kPaper);
        button->SetBorder(0, Colors::Transparent);
        button->SetCornerRadius(6);
        button->SetTextAlign(TextAlignment::Left);
        button->SetPadding(0, 8);
        button->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(32));
        button->SetMouseCursor(UCMouseCursor::Hand);
        const std::string slug = page->slug;
        button->onClick = [slug]() {
            if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
                app->PostToUIThread([slug]() { viewer.Show(slug); });
            }
        };
        side->AddChild(button);
    }
    frame->AddChild(side);

    auto pageArea = CreateScrollableContainer("PageArea", 0, 0, 0, 0, true, false);
    pageArea->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Pct(100));
    pageArea->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
    {
        ContainerStyle style = pageArea->GetContainerStyle();
        style.autoShowHorizontalScrollbar = false;
        pageArea->SetContainerStyle(style);
    }
    pageArea->SetBackgroundColor(kPaper);
    pageArea->layout.SetDisplay(CSSLayout::DisplayType::Block);
    frame->AddChild(pageArea);
    viewer.pageArea = pageArea;
    window->AddChild(frame);

    viewer.Show(viewer.site.FindPage("home") ? "home" : viewer.site.pages.empty() ? "" : viewer.site.pages.front().slug);
#if defined(__EMSCRIPTEN__)
    window->Maximize();
#endif
    window->Show();
    app.Run();
    return 0;
}
