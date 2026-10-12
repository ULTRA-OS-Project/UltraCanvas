// OS/WASM/demo/ultraos-frontpage.cpp
// A clone of the https://ultraos.eu front page built out of UltraCanvas
// elements, for the WebAssembly backend: a header with the navigation, a
// hero, the "For desktops" / "For mobile" pair, the reasons for ULTRA OS,
// the three ways to support the project, the status timeline, the
// newsletter form and the footer. Every piece is a framework element laid
// out by the CSS layout engine - no HTML, no JavaScript - so the same source
// builds as a desktop application for checking the page before the wasm
// pipeline runs (BUILD_WASM_DEMOS in the root CMakeLists.txt).
//
// The texts follow the published site; edit the tables at the top to keep
// them in step with it.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

// ---- palette (the site: white pages, near-black text, the logo's red) ----
const Color kRed(170, 0, 20, 255);
const Color kRedHover(140, 0, 16, 255);
const Color kInk(28, 28, 32, 255);
const Color kInkSoft(90, 90, 98, 255);
const Color kPaper(255, 255, 255, 255);
const Color kPaperSoft(246, 246, 248, 255);
const Color kRule(226, 226, 230, 255);
const Color kDark(24, 24, 28, 255);
const Color kDarkSoft(200, 200, 206, 255);

const char* kFont = "Ubuntu";
const float kContentWidth = 1140.f;   // the centred column, like the site's container
const std::string kSiteUrl = "https://ultraos.eu/";

// ---- texts ----
struct NavItem { const char* title; const char* path; };
const std::vector<NavItem> kNav = {
    {"Home", ""}, {"ULTRA OS", "ultra-os/"}, {"UltraCanvas", "ultracanvas/"},
    {"The Kernel", "the-kernel/"}, {"Programs", "programs/"}, {"Status", "status/"},
    {"Download", "download-file/"}, {"Contact", "contact-us/"},
};

struct Card { const char* title; const char* body; };
const std::vector<Card> kPlatforms = {
    {"For desktops",
     "The first release of ULTRA OS is for desktop PCs and laptops. It is built on a "
     "Linux kernel - the open source kernel Merlin is preselected, and the decision is "
     "not final - with UltraCanvas as the GUI every program is built on."},
    {"For mobile",
     "Phones and tablets follow after the desktop release, on the Android kernel. The "
     "same UltraCanvas API runs on both, so a program written once runs on every "
     "device ULTRA OS supports."},
};
const std::vector<Card> kReasons = {
    {"Cheaper development",
     "One cross-platform GUI, UltraCanvas, instead of a toolkit per platform: a "
     "program is written once and runs on ULTRA OS, Windows, Linux and macOS."},
    {"Lower licensing fees",
     "Hardware makers pay no licence for the operating system, so a device can be "
     "sold cheaper and still ship with a complete, modern system."},
    {"Resource-efficient",
     "A comprehensive system that stays usable on low-cost hardware, with a hardened "
     "variant on the roadmap for people who need an architecture that is hard to attack."},
};
struct Audience { const char* title; const char* body; const char* action; const char* path; };
const std::vector<Audience> kAudiences = {
    {"Donors and investors",
     "The founder funds the work personally. Donations keep the project going full "
     "time; investors can read how ULTRA OS is meant to earn money.", "Donate", "donation/"},
    {"Developers and enthusiasts",
     "UltraCanvas is on GitHub. Build the framework, write a program against it, "
     "report what breaks, or port your favourite open source application.",
     "Visit GitHub", "https://github.com/ULTRA-OS-Project"},
    {"Computer hobbyists",
     "Try the compiled UltraCanvas demo on Linux, tell us what you think, and spread "
     "the word so that more people hear about Europe's open source operating system.",
     "Download the demo", "download-file/"},
};
struct Milestone { const char* date; const char* text; };
const std::vector<Milestone> kStatus = {
    {"2025", "Concept and prototyping phase: the UltraCanvas GUI framework and the first applications."},
    {"17.7.2025", "First working program with the UltraCanvas library."},
    {"2025 / 2026", "Compiled UltraCanvas demo for Linux; Windows, macOS and BSD builds follow."},
    {"1 to 4 years", "Time to a first release of ULTRA OS for desktops, depending on funding and donations."},
    {"later", "ULTRA OS for mobile, after the desktop release."},
};

std::string Url(const std::string& path) {
    return path.rfind("https://", 0) == 0 ? path : kSiteUrl + path;
}

// ---- element helpers ----
std::shared_ptr<UltraCanvasLabel> Text(const std::string& id, const std::string& text,
                                       float size, const Color& color,
                                       FontWeight weight = FontWeight::Normal,
                                       TextAlignment align = TextAlignment::Left) {
    auto label = CreateLabel(id, text);
    label->SetFont(kFont, size, weight);
    label->SetTextColor(color);
    label->SetWrap(TextWrap::WrapWord);
    label->SetAlignment(align, VerticalAlignment::Top);
    label->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    return label;
}

std::shared_ptr<UltraCanvasButton> Action(const std::string& id, const std::string& text,
                                          const std::string& url, bool primary,
                                          bool onDark = false) {
    auto button = CreateButton(id, 0, 0, 0, 44, text);
    button->SetFont(kFont, 15, FontWeight::Bold);
    button->SetCornerRadius(6);
    button->SetPadding(0, 22);
    if (primary) {
        button->SetColors(kRed, kRedHover);
        button->SetTextColors(kPaper);
        button->SetBorder(0, Colors::Transparent);
    } else if (onDark) {
        button->SetColors(Colors::Transparent, Color(255, 255, 255, 30));
        button->SetTextColors(kPaper);
        button->SetBorder(1.5f, kPaper);
    } else {
        button->SetColors(kPaper, kPaperSoft);
        button->SetTextColors(kRed);
        button->SetBorder(1.5f, kRed);
    }
    button->SetMouseCursor(UCMouseCursor::Hand);
    button->onClick = [url]() { OpenURL(url); };
    return button;
}

// A full-width band; the content it holds is centred and at most kContentWidth.
struct Band {
    std::shared_ptr<UltraCanvasContainer> outer;   // spans the page
    std::shared_ptr<UltraCanvasContainer> inner;   // the centred column (Block layout)
};
Band MakeBand(const std::string& id, const Color& background, float paddingV) {
    Band band;
    band.outer = CreateContainer(id);
    band.outer->SetBackgroundColor(background);
    band.outer->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    band.outer->SetPadding(paddingV, 32);
    // A flex row centres the column; its item is measured at its exact width,
    // so wrapped text inside reports the height it really takes.
    band.outer->layout.SetFlexRow().SetFlexJustifyContent(CSSLayout::JustifyContent::Center);

    band.inner = CreateContainer(id + "Inner");
    band.inner->layout.SetDisplay(CSSLayout::DisplayType::Block);
    band.inner->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    CSSLayout::BoxConstraints limits;
    limits.maxWidth = CSSLayout::Dimension::Px(kContentWidth);
    band.inner->boxConstraints = limits;
    band.inner->layoutItem.SetFlexShrink(1);
    band.outer->AddChild(band.inner);
    return band;
}

// A row of equal cards that wraps when the window is narrow.
std::shared_ptr<UltraCanvasContainer> CardRow(const std::string& id, float gap) {
    auto row = CreateContainer(id);
    row->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    row->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap).SetFlexGap(gap)
        .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    return row;
}

std::shared_ptr<UltraCanvasContainer> CardBox(const std::string& id, float minWidth,
                                              const Color& background, bool framed) {
    auto card = CreateContainer(id);
    card->layout.SetDisplay(CSSLayout::DisplayType::Block);
    card->SetBackgroundColor(background);
    card->SetPadding(28);
    if (framed) card->SetBorders(1, kRule, 10);
    else card->SetBorderRadius(10);
    card->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(minWidth));
    return card;
}

std::shared_ptr<UltraCanvasContainer> Gap(float height) {
    auto spacer = CreateContainer("");
    spacer->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(height));
    return spacer;
}

// ---- the sections ----
std::shared_ptr<UltraCanvasContainer> BuildHeader() {
    auto band = MakeBand("Header", kPaper, 12);
    band.outer->SetBorderBottom(1, kRule);
    band.inner->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center).SetFlexGap(2);

    auto logo = CreateImageFromFile("Logo", 0, 0, 44, 44,
                                    GetResourcesDir() + "media/images/UOS_logo_white.png");
    logo->SetFitMode(ImageFitMode::Contain);
    band.inner->AddChild(logo);

    auto brand = CreateLabel("Brand", "ULTRA OS");
    brand->SetFont(kFont, 22, FontWeight::Bold);
    brand->SetTextColor(kInk);
    brand->SetPadding(0, 14, 0, 8);
    brand->layoutItem.SetFlexShrink(0);
    band.inner->AddChild(brand);

    band.inner->AddStretchSpacer(1);

    for (size_t i = 0; i < kNav.size(); ++i) {
        auto item = CreateButton("Nav" + std::to_string(i), 0, 0, 0, 36, kNav[i].title);
        item->SetFont(kFont, 14, i == 0 ? FontWeight::Bold : FontWeight::Normal);
        item->SetColors(Colors::Transparent, kPaperSoft);
        item->SetTextColors(i == 0 ? kRed : kInk, kRed);
        item->SetBorder(0, Colors::Transparent);
        item->SetCornerRadius(6);
        item->SetPadding(0, 9);
        // Never ellipsised: the row's slack is the stretch spacer's, not the items'.
        item->layoutItem.SetFlexShrink(0);
        item->SetMouseCursor(UCMouseCursor::Hand);
        const std::string url = Url(kNav[i].path);
        item->onClick = [url]() { OpenURL(url); };
        band.inner->AddChild(item);
    }
    band.inner->AddSpacer(10);
    auto donate = Action("HeaderDonate", "Donate", Url("donation/"), true);
    donate->layoutItem.SetFlexShrink(0);
    band.inner->AddChild(donate);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildHero() {
    auto band = MakeBand("Hero", kDark, 88);

    auto tag = Text("HeroTag", "# userfriendly   # cross-platform API   # open source", 15, kRed,
                    FontWeight::Bold, TextAlignment::Center);
    band.inner->AddChild(tag);
    band.inner->AddChild(Gap(18));

    auto title = Text("HeroTitle", "Europe's Open Source Operating System", 48, kPaper,
                      FontWeight::Bold, TextAlignment::Center);
    band.inner->AddChild(title);
    band.inner->AddChild(Gap(22));

    auto lead = Text("HeroLead",
                     "ULTRA OS is an open source initiative to create a resource-efficient but "
                     "comprehensive operating system for PCs and, later, for mobile devices - "
                     "unified across devices, simple to use, built on one cross-platform GUI: "
                     "UltraCanvas.",
                     19, kDarkSoft, FontWeight::Normal, TextAlignment::Center);
    lead->SetPadding(0, 80);
    lead->box.boxSizing = CSSLayout::BoxSizing::BorderBox;   // the 100% includes the padding
    band.inner->AddChild(lead);
    band.inner->AddChild(Gap(36));

    auto actions = CreateContainer("HeroActions");
    actions->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    actions->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap).SetFlexGap(14)
        .SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    actions->AddChild(Action("HeroDownload", "Download the demo", Url("download-file/"), true));
    actions->AddChild(Action("HeroSupport", "Support the project", Url("donation/"), false, true));
    band.inner->AddChild(actions);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildPlatforms() {
    auto band = MakeBand("Platforms", kPaper, 72);
    auto row = CardRow("PlatformCards", 28);
    for (size_t i = 0; i < kPlatforms.size(); ++i) {
        auto card = CardBox("Platform" + std::to_string(i), 400, kPaperSoft, false);
        card->AddChild(Text("PlatformTitle" + std::to_string(i), kPlatforms[i].title, 26, kInk,
                            FontWeight::Bold));
        card->AddChild(Gap(12));
        card->AddChild(Text("PlatformBody" + std::to_string(i), kPlatforms[i].body, 16, kInkSoft));
        row->AddChild(card);
    }
    band.inner->AddChild(row);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildReasons() {
    auto band = MakeBand("Reasons", kPaperSoft, 72);
    band.inner->AddChild(Text("ReasonsKicker", "THE REASONS FOR ULTRA OS", 13, kRed,
                              FontWeight::Bold, TextAlignment::Center));
    band.inner->AddChild(Gap(10));
    band.inner->AddChild(Text("ReasonsTitle", "Why another operating system?", 34, kInk,
                              FontWeight::Bold, TextAlignment::Center));
    band.inner->AddChild(Gap(36));
    auto row = CardRow("ReasonCards", 24);
    for (size_t i = 0; i < kReasons.size(); ++i) {
        auto card = CardBox("Reason" + std::to_string(i), 280, kPaper, true);
        auto index = CreateLabel("ReasonIndex" + std::to_string(i), "0" + std::to_string(i + 1));
        index->SetFont(kFont, 15, FontWeight::Bold);
        index->SetTextColor(kRed);
        card->AddChild(index);
        card->AddChild(Gap(10));
        card->AddChild(Text("ReasonTitle" + std::to_string(i), kReasons[i].title, 21, kInk,
                            FontWeight::Bold));
        card->AddChild(Gap(10));
        card->AddChild(Text("ReasonBody" + std::to_string(i), kReasons[i].body, 15, kInkSoft));
        row->AddChild(card);
    }
    band.inner->AddChild(row);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildSupport() {
    auto band = MakeBand("Support", kPaper, 72);
    band.inner->AddChild(Text("SupportKicker", "WHY YOU SHOULD SUPPORT US", 13, kRed,
                              FontWeight::Bold, TextAlignment::Center));
    band.inner->AddChild(Gap(10));
    band.inner->AddChild(Text("SupportTitle", "Three ways to help", 34, kInk, FontWeight::Bold,
                              TextAlignment::Center));
    band.inner->AddChild(Gap(36));
    auto row = CardRow("SupportCards", 24);
    for (size_t i = 0; i < kAudiences.size(); ++i) {
        auto card = CardBox("Audience" + std::to_string(i), 280, kPaperSoft, false);
        card->AddChild(Text("AudienceTitle" + std::to_string(i), kAudiences[i].title, 21, kInk,
                            FontWeight::Bold));
        card->AddChild(Gap(10));
        card->AddChild(Text("AudienceBody" + std::to_string(i), kAudiences[i].body, 15, kInkSoft));
        card->AddChild(Gap(20));
        card->AddChild(Action("AudienceAction" + std::to_string(i), kAudiences[i].action,
                              Url(kAudiences[i].path), i == 0));
        row->AddChild(card);
    }
    band.inner->AddChild(row);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildStatus() {
    auto band = MakeBand("Status", kPaperSoft, 72);
    band.inner->AddChild(Text("StatusKicker", "STATUS", 13, kRed, FontWeight::Bold,
                              TextAlignment::Center));
    band.inner->AddChild(Gap(10));
    band.inner->AddChild(Text("StatusTitle", "Where the project stands", 34, kInk,
                              FontWeight::Bold, TextAlignment::Center));
    band.inner->AddChild(Gap(10));
    band.inner->AddChild(Text("StatusLead",
                              "ULTRA OS is in its concept and prototyping phase. The GUI framework "
                              "comes first, because every program of the system is built on it.",
                              17, kInkSoft, FontWeight::Normal, TextAlignment::Center));
    band.inner->AddChild(Gap(32));

    auto list = CreateContainer("StatusList");
    list->layout.SetDisplay(CSSLayout::DisplayType::Block);
    list->SetBackgroundColor(kPaper);
    list->SetBorders(1, kRule, 10);
    list->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    for (size_t i = 0; i < kStatus.size(); ++i) {
        auto row = CreateContainer("Milestone" + std::to_string(i));
        row->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
        row->layout.SetFlexRow().SetFlexGap(20).SetFlexAlignItems(CSSLayout::AlignItems::Start);
        row->SetPadding(16, 24);
        if (i + 1 < kStatus.size()) row->SetBorderBottom(1, kRule);

        auto when = CreateLabel("MilestoneDate" + std::to_string(i), kStatus[i].date);
        when->SetFont(kFont, 15, FontWeight::Bold);
        when->SetTextColor(kRed);
        when->SetElementSize(CSSLayout::Dimension::Px(130), CSSLayout::Dimension::Auto());
        row->AddChild(when);

        auto what = Text("MilestoneText" + std::to_string(i), kStatus[i].text, 15, kInk);
        what->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
        what->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
        row->AddChild(what);
        list->AddChild(row);
    }
    band.inner->AddChild(list);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildNewsletter() {
    auto band = MakeBand("Newsletter", kPaper, 72);
    band.inner->AddChild(Text("NewsKicker", "ABOUT US", 13, kRed, FontWeight::Bold,
                              TextAlignment::Center));
    band.inner->AddChild(Gap(10));
    band.inner->AddChild(Text("NewsTitle", "Stay informed", 34, kInk, FontWeight::Bold,
                              TextAlignment::Center));
    band.inner->AddChild(Gap(10));
    band.inner->AddChild(Text("NewsLead",
                              "ULTRA OS is developed in Olpe, Germany, and published under open "
                              "source licences. Subscribe to the newsletter for update information, "
                              "news and insight.",
                              17, kInkSoft, FontWeight::Normal, TextAlignment::Center));
    band.inner->AddChild(Gap(28));

    auto form = CreateContainer("NewsForm");
    form->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    form->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap).SetFlexGap(12)
        .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
        .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto email = CreateTextInput("NewsEmail", 0, 0, 340, 44);
    email->SetPlaceholder("Your e-mail address");
    email->SetFontSize(15);
    form->AddChild(email);
    auto subscribe = Action("NewsSubscribe", "Subscribe", "", true);
    form->AddChild(subscribe);
    band.inner->AddChild(form);
    band.inner->AddChild(Gap(14));

    auto reply = Text("NewsReply", " ", 14, kInkSoft, FontWeight::Normal, TextAlignment::Center);
    band.inner->AddChild(reply);

    auto submit = [email, reply]() {
        const std::string address = email->GetText();
        if (address.find('@') == std::string::npos) {
            reply->SetTextColor(kRed);
            reply->SetText("Please enter an e-mail address.");
        } else {
            reply->SetTextColor(kInkSoft);
            reply->SetText("Thank you - " + address + " is on the list. (This demo sends nothing.)");
        }
    };
    subscribe->onClick = submit;
    email->onEnterPressed = [submit](const std::string&) { submit(); return true; };
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildFooter() {
    auto band = MakeBand("Footer", kDark, 36);
    band.inner->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap).SetFlexGap(16)
        .SetFlexAlignItems(CSSLayout::AlignItems::Center);

    auto copyright = CreateLabel("FooterCopyright",
                                 "© 2026 ULTRA OS Project · Olpe, Germany · info@ultraos.eu");
    copyright->SetFont(kFont, 14);
    copyright->SetTextColor(kDarkSoft);
    band.inner->AddChild(copyright);
    band.inner->AddStretchSpacer(1);

    const std::vector<NavItem> links = {
        {"Open Source", "open-source/"}, {"Investor", "investor/"},
        {"Philosophies", "ultra-os-philisophies/"}, {"GitHub", "https://github.com/ULTRA-OS-Project"},
    };
    for (size_t i = 0; i < links.size(); ++i) {
        auto link = CreateButton("FooterLink" + std::to_string(i), 0, 0, 0, 32, links[i].title);
        link->SetFont(kFont, 14);
        link->SetColors(Colors::Transparent, Color(255, 255, 255, 24));
        link->SetTextColors(kDarkSoft, kPaper);
        link->SetBorder(0, Colors::Transparent);
        link->SetCornerRadius(6);
        link->SetPadding(0, 10);
        link->SetMouseCursor(UCMouseCursor::Hand);
        const std::string url = Url(links[i].path);
        link->onClick = [url]() { OpenURL(url); };
        band.inner->AddChild(link);
    }
    return band.outer;
}

} // namespace

// ultraos-frontpage [width height] - the window size on the desktop (the
// browser build fills the viewport); a tall one shows the whole page at once.
int main(int argc, char* argv[]) {
    // Static lifetime is required under Emscripten: Run() never returns -
    // emscripten_set_main_loop() unwinds main()'s frame (running destructors
    // under -fwasm-exceptions), so a stack-local application would be
    // destroyed while its main loop is still ticking.
    static UltraCanvasApplication app;
    if (!app.Initialize(argv[0])) {
        return 1;
    }

    WindowConfig cfg;
    cfg.title = "ULTRA OS - Europe's Open Source Operating System";
    cfg.width = 1280;
    cfg.height = 860;
    if (argc >= 3) {
        cfg.width = std::max(320, std::atoi(argv[1]));   // locale-ok: a size typed on the command line
        cfg.height = std::max(240, std::atoi(argv[2]));  // locale-ok: a size typed on the command line
    }
    cfg.backgroundColor = kPaper;
    auto window = CreateWindow(cfg);
    if (!window) {
        return 1;
    }

    // The page scrolls inside the window like a browser's viewport; the
    // sections are stacked by Block layout, which measures each wrapped text
    // against the width it really gets.
    auto page = CreateScrollableContainer("Page", 0, 0, 0, 0, true, false);
    page->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Pct(100));
    {
        // The page reflows to its width, so it never scrolls sideways: the
        // vertical bar narrows the viewport by its track, and a horizontal
        // bar would only ever show that track's worth of fabricated overflow.
        ContainerStyle style = page->GetContainerStyle();
        style.autoShowHorizontalScrollbar = false;
        page->SetContainerStyle(style);
    }
    page->SetBackgroundColor(kPaper);
    page->layout.SetDisplay(CSSLayout::DisplayType::Block);
    page->AddChild(BuildHeader());
    page->AddChild(BuildHero());
    page->AddChild(BuildPlatforms());
    page->AddChild(BuildReasons());
    page->AddChild(BuildSupport());
    page->AddChild(BuildStatus());
    page->AddChild(BuildNewsletter());
    page->AddChild(BuildFooter());
    window->AddChild(page);

#if defined(__EMSCRIPTEN__)
    window->Maximize();   // fill the browser viewport
#endif
    window->Show();
    app.Run(); // never returns in the browser; the main loop keeps ticking
    return 0;
}
