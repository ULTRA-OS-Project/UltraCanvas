// OS/WASM/demo/ultraos-frontpage.cpp
// The https://ultraos.eu front page built out of UltraCanvas elements, for
// the WebAssembly backend: the site's header with its drop-down navigation,
// the hero, the desktop / mobile cards, the reasons for ULTRA OS, the
// newsletter, the status, the two pre-order offers, the donation links, the
// social networks, the ULTRA OS variants, the purpose-based editions, the
// FAQ, the contact form and the footer. Every piece is a framework element
// laid out by the CSS layout engine - no HTML, no JavaScript - so the same
// source builds as a desktop application for checking the page before the
// wasm pipeline runs (BUILD_WASM_DEMOS in the root CMakeLists.txt).
//
// The texts are the published page's, in the tables below; the pictures the
// page shows are placeholders except the three the repository carries
// (media/images/ultraos/). Edit the tables to keep the page in step with
// the site.
//
// One module, several addresses: a link into the site (a navigation item, a
// menu entry, a footer service, a hero button) opens a page of the demo
// under `?page=<site path>` through the WebAssembly backend's History API,
// so the address bar changes, the browser's back button returns to the front
// page and a reload opens the same page again. The query form rather than a
// path because a static server (serve.py) serves only the demo's own file.
// On the desktop the same pages are shown without an address bar.
// Version: 2.1.0 - pages of the demo under the History API (PushUrl, SetOnNavigate)
// Version: 2.0.0 - the real page: navigation menus, offers, variants, FAQ, contact
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasPathUtf8.h"
#if defined(__EMSCRIPTEN__)
#include "OS/WASM/UltraCanvasWASMSupport.h"   // WASMURL: the History API
#endif

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

// ---- palette: the site's stylesheet colours ---------------------------------
const Color kNight(13, 12, 34, 255);        // #0D0C22 - header, footer
const Color kNightSoft(24, 25, 63, 255);    // #18193f - hero, dark bands
const Color kRed(169, 7, 7, 255);           // #a90707 - the logo's red
const Color kRedHover(130, 5, 5, 255);
const Color kInk(32, 32, 32, 255);          // #202020 - body text
const Color kInkSoft(90, 90, 98, 255);
const Color kPaper(255, 255, 255, 255);
const Color kPaperSoft(239, 239, 239, 255); // #efefef - alternate bands
const Color kRule(220, 220, 224, 255);
const Color kOnDark(208, 208, 220, 255);
const Color kTile(226, 228, 236, 255);      // where the page shows a photo we do not carry

const char* kFont = "Ubuntu";
const float kContentWidth = 1140.f;
const std::string kSiteUrl = "https://ultraos.eu/";

// A path of the site ("the-plan/"), as opposed to an absolute URL, a mailto:
// or a tel: link.
bool IsSitePath(const std::string& path) {
    return path.find("://") == std::string::npos && path.rfind("mailto:", 0) != 0 &&
           path.rfind("tel:", 0) != 0;
}
std::string Url(const std::string& path) {
    return IsSitePath(path) ? kSiteUrl + path : path;
}

// ---- pages: one module, several addresses -------------------------------------
std::shared_ptr<UltraCanvasContainer> gPage;   // the scrolling page; its sections are rebuilt per address
void ShowDemoPage(const std::string& sitePath);     // "" is the front page

// Opens a page of the demo: the address bar first (in the browser), then the
// page - deferred, because the click that brought us here came from a button
// of the page being replaced, and that button must finish its callback first.
void GoTo(const std::string& sitePath) {
#if defined(__EMSCRIPTEN__)
    WASMURL::PushUrl(sitePath.empty() ? WASMURL::GetPath() : "?page=" + sitePath);
#endif
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
        app->PostToUIThread([sitePath]() { ShowDemoPage(sitePath); });
    }
}
// A link's click: a page of the demo for a site path, the system browser for
// everything else (another site, mailto:, tel:).
std::function<void()> Open(const std::string& path) {
    if (IsSitePath(path)) return [path]() { GoTo(path); };
    return [path]() { OpenURL(path); };
}
std::string Picture(const char* file) {
    return GetResourcesDir() + "media/images/ultraos/" + file;
}
// A picture of the page the repository carries: media/images/ultraos/<stem>.png.
// Those the repository does not have (yet) draw as a tile with their name,
// so adding one is dropping the file in, no code change.
bool HasPicture(const std::string& stem) {
    return !stem.empty() && std::filesystem::exists(PathFromUtf8(Picture((stem + ".png").c_str())));
}

// ---- texts: the page, top to bottom -------------------------------------------
struct Link { const char* title; const char* path; };
struct NavItem { const char* title; const char* path; std::vector<Link> menu; };
const std::vector<NavItem> kNav = {
    {"Home", "", {}},
    {"Details", "", {{"The development plan", "the-plan/"}, {"ULTRA OS Modules", "module/"},
                     {"The Kernel", "the-kernel/"}, {"Local AI", "local-ai/"}, {"CoderBox", "coderbox/"},
                     {"FileLoader Module", "fileloader-module/"},
                     {"ULTRA OS Philisophies", "ultra-os-philisophies/"}, {"Slogans", "slogan/"},
                     {"Wish list", "wish-list/"}}},
    {"UltraCanvas", "", {{"UltraCanvas Overview", "ultracanvas/"},
                         {"UltraCanvas vs other GUI", "ultracanvas-vs-other-gui/"},
                         {"UltraCanvas Insights", "ultracanvas-insights/"},
                         {"UltraCanvas libraries", "ultracanvas-libraries/"},
                         {"UltraCanvas examples", "ultracanvas-examples/"},
                         {"Dependencies", "dependencies/"},
                         {"UltraCanvas Server & Terminal", "ultracanvas-server-terminal/"},
                         {"UltraCanvas document file", "ultracanvas-document-file/"}}},
    {"Open Source", "open-source/", {}},
    {"ULTRA OS Github", "ultra-os/", {}},
    {"Programs", "programs/", {}},
    {"Financing", "", {{"Donation", "donation/"}, {"Investor", "investor/"}}},
    {"Status", "status/", {}},
    {"Contact", "contact-us/", {}},
    {"Feedback", "feedback/", {}},
    {"Download", "download-file/", {}},
    {"UOS Applications", "", {{"UltraDesktop", "ultradesktop/"}, {"UltraFiler", "ultrafiler/"},
                              {"Texter", "texter/"}, {"Visualizer", "visualizer/"},
                              {"Memorizer", "memorizer/"}, {"ArtBox", "artbox/"},
                              {"ChatBox", "chatbox/"}, {"CoderBox", "coderbox-2/"}}},
};

const char* kHeroKicker = "Introducing ULTRA OS";
const char* kHeroTitle = "The last 30 years have been the period of computer research - "
                         "It\xE2\x80\x99s time to build on what we\xE2\x80\x99ve learned.";
const char* kHeroText = "Unlike fragmented solutions, ULTRA OS aims to unify simplicity, performance, "
                        "and modern design across all devices desktop and mobile\xE2\x80\x94under one "
                        "cohesive platform.";
const char* kIntroText = "ULTRA OS will be a next-generation operating system project initially "
                         "targeting desktop PCs and laptops, with plans to expand to mobile phones and "
                         "tablets in later stages. Our ultimate goal is to provide a compelling, "
                         "open-source alternative to today\xE2\x80\x99s dominant platforms.";

const std::vector<const char*> kReasons = {
    "We create user-friendly, no-nonsense, open source software",
    "We reduce the development cost for programmers by a comprehensive cross-platform GUI",
    "We create more choices for consumers",
    "We increase the quality of services and programs for users by more global competition",
    "We set free computer and mobile phone manufacturers from licensing fees",
    "We want to break the American monopoly in operating systems and software",
    "We reduce maintenance and upgrade costs for companies and administration",
    "No dirty AI - Server centers should be powered by 100% green energy",
};

const char* kNewsletterText = "Signup our newsletter to get update information, news, insight or promotions.";

const char* kStatusText = "Currently in the concept and prototyping phase, ULTRA OS is an open-source "
                          "initiative to create a Resource-efficient but comprehensive operating system "
                          "to cover both PC and mobile hardware.";
const std::vector<const char*> kAudiences = {
    "Donors & Investors", "Developers & Enthusiasts", "Computer enthusiasts & hobbyists",
};

struct Offer { const char* name; const char* subtitle; const char* price; std::vector<const char*> items; const char* release; };
const std::vector<Offer> kOffers = {
    {"ULTRA OS Desktop LX/MC/WN", "Replacement for desktop of Linux, Windows and macOS", "\xE2\x82\xAC 49",
     {"UltraCanvas GUI/API", "UltraDesktop", "UltraFiler", "Texter", "Visualizer", "Memorizer", "PixelEd"},
     "Release approx. 12/2026"},
    {"ULTRA OS", "Complete OS based on Linux", "\xE2\x82\xAC 99",
     {"UltraCanvas GUI/API", "UltraDesktop", "UltraFiler", "Texter", "Visualizer", "Memorizer",
      "ArtBox", "ChatBox", "CoderBox", "Multi-OS AppInstaller"},
     "Release approx. 07/2027"},
};
const char* kPreorderUrl = "https://www.patreon.com/cw/ULTRAOS/shop";

const std::vector<Link> kDonate = {
    {"Support us with 20 USD - PayPal", "http://paypal.me/ultraos"},
    {"Support us with 5 USD - Buy me a coffee", "https://buymeacoffee.com/ultraos"},
    {"Support us with 20 USD - Patreon", "http://patreon.com/ULTRAOS"},
};
const char* kDonateText1 = "ULTRA OS is a practical rethinking of what an operating system should be: fast, "
                           "focused, and free from bloat. We\xE2\x80\x99re building a platform that prioritizes "
                           "clarity, performance, and user control \xE2\x80\x94 with no corporate influence.";
const char* kDonateText2 = "Your donation helps us stay independent, improve developer tools, refine the user "
                           "experience, and deliver a system built to serve you, not advertisers or shareholders.";
const char* kDonateText3 = "Support ULTRA OS \xE2\x80\x94 and help shape the future of computing";

const std::vector<Link> kSocial = {
    {"X", "https://x.com/ULTRAOS2025"}, {"Youtube", "https://www.youtube.com/@ULTRA-OS-2025"},
    {"Facebook", "https://www.facebook.com/61579912464136/"}, {"Ko-fi", "https://ko-fi.com/ultraos"},
    {"Mastodon", "https://mastodon.social/@ultraos"},
    {"Bluesky", "https://bsky.app/profile/ultra-os.bsky.social"},
};

// A variant: a picture beside a heading and its paragraphs. `picture` is the
// stem of the page's picture in media/images/ultraos/ (shown when the file is
// there), `caption` the tile's text until it is.
struct Variant { const char* title; const char* picture; const char* caption; const char* text; };
const std::vector<Variant> kVariants = {
    {"ULTRA Mobile OS", "Mobile-phone", "Mobile phone",
     "Google\xE2\x80\x99s Android operating system dominates the global mobile market with an estimated 70% "
     "share, while Apple\xE2\x80\x99s iOS holds around 27%. This leaves an open opportunity for a strong "
     "alternative \xE2\x80\x94 ULTRA OS \xE2\x80\x94 to emerge as a viable third option. The mobile market "
     "generates the largest revenue stream in the entire IT sector. For example:\n\n"
     "The Google Play Store produces about $45 billion USD in annual gross sales, yielding Google an "
     "estimated $10 billion USD in commission fees (taking a 15\xE2\x80\x93" "30% share of all transactions).\n\n"
     "Apple\xE2\x80\x99s App Store earns even more \xE2\x80\x94 approximately $80 billion USD in annual sales "
     "\xE2\x80\x94 with Apple collecting roughly $21 billion USD in fees.\n\n"
     "Beyond app marketplaces, mobile phone companies generate substantial income by selling hardware and "
     "offering additional services like cloud storage, AI features, and physical retail support.\n\n"
     "In 2025, global mobile phone sales alone reached about $500 billion USD. This means that capturing "
     "just 1% of the combined mobile hardware and software market could translate to a potential $5\xE2\x80\x93"
     "6 billion USD in annual turnover for ULTRA OS."},
    {"ULTRA OS Mobile Desktop", "Mobile-PC", "Mobile PC",
     "There are an estimated 6 billion mobile phones in use worldwide in 2025. Modern smartphones are "
     "20,000 to 100,000 times more powerful than the original ARM-based computers developed by Acorn "
     "Computers Ltd. back in 1987. Remarkably, even the 8 MHz ARM chip in the Acorn A3010 was fast enough "
     "to run the full RISC OS graphical user interface \xE2\x80\x94 proving that today\xE2\x80\x99s phones "
     "easily have the computing power needed for desktop-class applications.\n\n"
     "In reality, at least half of today\xE2\x80\x99s smartphones are technically capable of functioning as "
     "compact desktop PCs. However, they fall short in practice because they lack large displays and "
     "because Android was never designed as a true desktop operating system. While there have been "
     "attempts to bridge this gap, these efforts often came from the same big tech companies that "
     "dominate the mobile market \xE2\x80\x94 and true success would have threatened their business models "
     "more than it helped.\n\n"
     "The truth is: most of us already carry a powerful mini-PC in our pocket every day. So why should "
     "people buy a separate desktop computer when their smartphone could handle the same daily tasks?\n\n"
     "ULTRA OS aims to change this by unlocking the full potential of modern smartphones \xE2\x80\x94 "
     "transforming them into fully capable PCs for everyday work and productivity."},
    {"ULTRA OS INTEL/AMD", "INTEL", "INTEL chip",
     "The majority of PCs and laptops in use today still rely on Intel\xE2\x80\x99s x86 architecture. To "
     "achieve widespread adoption and appeal to general users, ULTRA OS must be fully compatible with x86 "
     "processors \xE2\x80\x94 ensuring smooth performance on the vast installed base of existing desktops "
     "and notebooks. Supporting x86 alongside ARM and RISC-V will make ULTRA OS a truly universal "
     "operating system, ready to run on mainstream hardware without compromise."},
    {"ULTRA OS RiscV", "RiscV-OS", "RISC-V chip",
     "The RISC-V processor architecture was developed as a fully open-source alternative to proprietary "
     "designs like ARM. It delivers comparable performance and cost efficiency while offering developers "
     "and manufacturers complete freedom from licensing restrictions and vendor lock-in.\n\n"
     "Given ULTRA OS\xE2\x80\x99s emphasis on openness, efficiency, and cross-platform flexibility, it is a "
     "natural match for the RISC-V ecosystem. Supporting RISC-V would broaden ULTRA OS\xE2\x80\x99s reach "
     "and help strengthen the adoption of open hardware and software standards \xE2\x80\x94 empowering "
     "innovation without barriers."},
    {"ULTRA OS Raspberry PI", "Raspberry", "Raspberry Pi",
     "The popular Raspberry Pi is an excellent candidate for running ULTRA OS as a lightweight and "
     "efficient operating system. Its popularity stems from its affordability and versatility \xE2\x80\x94 "
     "it can serve as a media center, a development and gaming platform, a web server, a desktop "
     "computer, or even as the controller for robots and IoT projects.\n\n"
     "The Raspberry Pi is also a valuable educational tool, widely used by educators and students alike "
     "to explore computer science, programming, robotics, and electronics in a hands-on way.\n\n"
     "Currently, there are an estimated 23 million Raspberry Pi 3 units, 20 million Pi 4 units, and "
     "5 million Pi 5 units in circulation. If each board owner contributed just 0.05 \xE2\x82\xAC per Pi, it "
     "would generate enough funding to fully support the development of ULTRA OS for this entire ecosystem."},
    {"ULTRA OS Local AI", "Local-AI-OS", "Local AI",
     "Instead of using internet AI services turn your local computer into an AI station.\n\n"
     "Turn your computer into a powerful AI workstation\xE2\x80\x94no internet needed. With local AI, your "
     "data stays private, your research remains secure, and you cut out recurring service fees. Enjoy "
     "faster performance, full control, and total independence from cloud providers.\n\n"
     "Low-cost AI mini computers can start from 300 Euro. Mid-size AI system can be offered from 900 Euro."},
    {"ULTRA OS Kids", "Kids", "Kids",
     "Our vision includes dedicated ULTRA OS versions tailored for families, featuring parental controls, "
     "curated educational content, and games designed for creativity and learning\xE2\x80\x94not violence. "
     "With increasing concerns over social media, messaging apps, cyberbullying, and manipulative "
     "content, ULTRA OS aims to give parents the tools they need to protect their children and ensure a "
     "healthy relationship with technology."},
    {"ULTRA OS Fortress", "Fortress", "Fortress",
     "Your company need a hacker-safe operating system or you don't trust the current mobile phone "
     "suppliers with your data anymore. Then we could developpe an OS for you a special OS version of "
     "ULTRA OS that suits your needs."},
    {"ULTRA OS Smart TV Workstation", "SmartTV", "Smart TV",
     "Suppliers from Smart TVs can turn their TV with ULTRA OS in a real ALL-in-one PC."},
    {"ULTRA OS Smart Home", "SmartHome", "Smart Home",
     "Control your home with a save OS that doesn't share your data with the rest of the world."},
};

struct Purpose { const char* title; const char* picture; const char* caption; const char* text; };
const std::vector<Purpose> kPurposes = {
    {"ULTRA OS for investors", "investor", "Investor",
     "You want to market a computer/mobile solution that is not based on Google Android or Apple IOS or Linux."},
    {"ULTRA for companies", "Factory", "Factory",
     "Your company need a low maintenance cost or a super save (unhackable) OS. Then ULTRA OS is a valid option."},
    {"ULTRA for countries", "countries", "Countries",
     "Your country wants to get independent from the USA or save licensing fees and maintenance cost. "
     "Then ULTRA OS is a valid option."},
    {"ULTRA OS for OEMS", "OEM", "OEM",
     "you want to offer an inexpensive user friendly solution for your PC, laptop, mobiles and tablets or Smart TVs."},
    {"ULTRA OS for administrations", "Administration", "Admin",
     "Your country or city is still not based on computerized solutions? We can built your customized "
     "systems to streamline your administration, increase effectiveness and service quality for your citizens."},
    {"ULTRA OS for education", "education", "Education",
     "With ULTRA OS schools have an affordable choice for their class rooms. ULTRA OS will provide the full "
     "scope of computing technologies to teach their students. This include operating modern computers "
     "user interface, Word processor, spread sheets, database, image processing, vector graphics, Desktop "
     "publishing, programming in C/C++, Python, PHP, Javascript etc. and last but not least AI technologies."},
    {"ULTRA OS for users", "user", "User",
     "User friendly, open and low cost. ULTRA OS for tinkerers: the philosophy that ULTRA OS will provide "
     "all major functionality as OS modules offers programmers and tinkerers the opportunity to "
     "concentrate on the real task that they have planned to solve."},
    {"ULTRA OS for parents", "Parents", "Parents",
     "Family-Friendly Computing. ULTRA OS is committed to creating a safe digital environment for "
     "children\xE2\x80\x94on both desktop and mobile platforms. We believe that children should be able to "
     "explore, learn, and play on their devices without exposure to harmful content."},
};

struct Faq { const char* question; const char* answer; };
const std::vector<Faq> kFaq = {
    {"When ULTRA OS will be available?",
     "The availability of the complete operating system depends on the level of funding and donations, "
     "with an estimated development timeline of 1 to 4 years.\n\n"
     "There will be two stages of implementation:\n\n"
     "Phase One: A Linux-based version featuring the ULTRA OS desktop environment and a suite of "
     "UltraCanvas applications.\n\n"
     "Phase Two: The full ULTRA OS, including a custom kernel and low-level drivers.\n\n"
     "Additionally, the UltraCanvas desktop and its applications will also be made available for "
     "Windows, macOS, and other supported operating systems."},
    {"When can we use UltraCanvas to built cross-platform programs?",
     "You can already experience the first version of UltraCanvas on Linux.\n\n"
     "Support for Windows and macOS is expected to be ready by the end of November 2025, with "
     "approximately 80% of UltraCanvas functionality planned for completion by the end of December.\n\n"
     "A demo application for Linux is available for download now."},
    {"Can we help this open source project?",
     "There are several ways you can support our project:\n\n"
     "Develop with UltraCanvas: Create new applications using UltraCanvas \xE2\x80\x94 they\xE2\x80\x99ll be "
     "automatically compatible with ULTRA OS and can also run on Windows, macOS, and Linux.\n\n"
     "Spread the word: Share information about our project on social media or repost our updates.\n\n"
     "Engage the open-source community: If you\xE2\x80\x99re active in open source, help us by sharing your "
     "knowledge and introducing others to our work.\n\n"
     "Become a donor: Contribute financially to support the ongoing development of ULTRA OS.\n\n"
     "Connect us with supporters: Tell potential donors about our mission whenever you have the opportunity.\n\n"
     "Join our funding initiative: Participate in funding efforts for the commercial branch of the project."},
};

const char* kContactClaim = "UltraCanvas is the most important module of ULTRA OS as it covers all "
                            "interactions between the OS and the user.";
const char* kContactTitle = "ULTRA OS - All You Need. Nothing You Don\xE2\x80\x99t.";
const char* kAddress = "Cloverleaf RISC OS Computer UG\nAn den Klippen 23\n57462 Olpe\nGermany";
const char* kEmail = "info@ultraos.eu";
const char* kPhoneDe = "+491707542291";
const char* kPhoneTh = "+66800723905";

const std::vector<Link> kFooterSocial = {
    {"Facebook", "https://www.facebook.com/profile.php?id=61579912464136"}, {"Twitter", "https://x.com/ULTRAOS2025"},
    {"Youtube", "https://www.youtube.com/@ULTRA-OS-2025"}, {"Github", "https://github.com/ULTRA-OS-Project"},
};
const std::vector<Link> kFooterServices = {
    {"Details", "investor/"}, {"UltraCanvas", "ultracanvas/"}, {"Programs", "program/"},
    {"Contact", "contact-us/"}, {"Donate", "donation/"}, {"Invest", "investor/"},
};
const char* kCopyright = "Copyright \xC2\xA9 2026 UltraOs All Right Reserved";

// ---- element helpers ------------------------------------------------------------
std::shared_ptr<UltraCanvasLabel> Text(const std::string& id, const std::string& text, float size,
                                       const Color& color, FontWeight weight = FontWeight::Normal,
                                       TextAlignment align = TextAlignment::Left) {
    auto label = CreateLabel(id, text);
    label->SetFont(kFont, size, weight);
    label->SetTextColor(color);
    label->SetWrap(TextWrap::WrapWord);
    label->SetAlignment(align, VerticalAlignment::Top);
    label->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    return label;
}

// The small red upper-case line the page puts above a heading.
std::shared_ptr<UltraCanvasLabel> Kicker(const std::string& id, std::string text,
                                         TextAlignment align = TextAlignment::Center) {
    for (auto& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return Text(id, text, 13, kRed, FontWeight::Bold, align);
}

std::shared_ptr<UltraCanvasButton> Flat(const std::string& id, const std::string& text, float size,
                                        const Color& color, const Color& hover) {
    auto button = CreateButton(id, 0, 0, 0, 34, text);
    button->SetFont(kFont, size);
    button->SetColors(Colors::Transparent, Color(hover.r, hover.g, hover.b, 28));
    button->SetTextColors(color, hover);
    button->SetBorder(0, Colors::Transparent);
    button->SetCornerRadius(6);
    button->SetPadding(0, 9);
    button->SetMouseCursor(UCMouseCursor::Hand);
    return button;
}

std::shared_ptr<UltraCanvasButton> Action(const std::string& id, const std::string& text,
                                          const std::string& path, bool primary, bool onDark = false) {
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
    if (!path.empty()) button->onClick = Open(path);
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

// A row of equal boxes that wraps when the window is narrow.
std::shared_ptr<UltraCanvasContainer> Row(const std::string& id, float gap,
                                          CSSLayout::AlignItems align = CSSLayout::AlignItems::Stretch,
                                          CSSLayout::JustifyContent justify = CSSLayout::JustifyContent::Start) {
    auto row = CreateContainer(id);
    row->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    row->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap).SetFlexGap(gap)
        .SetFlexAlignItems(align).SetFlexJustifyContent(justify);
    return row;
}

std::shared_ptr<UltraCanvasContainer> Card(const std::string& id, float minWidth,
                                           const Color& background, bool framed) {
    auto card = CreateContainer(id);
    card->layout.SetDisplay(CSSLayout::DisplayType::Block);
    card->SetBackgroundColor(background);
    card->SetPadding(26);
    if (framed) card->SetBorders(1, kRule, 10);
    else card->SetBorderRadius(10);
    card->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(minWidth));
    return card;
}

// A column that takes the rest of a row (Block inside, so wrapped text measures).
std::shared_ptr<UltraCanvasContainer> Column(const std::string& id, float minWidth = 0) {
    auto column = CreateContainer(id);
    column->layout.SetDisplay(CSSLayout::DisplayType::Block);
    column->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(minWidth));
    return column;
}

std::shared_ptr<UltraCanvasContainer> Gap(float height) {
    auto spacer = CreateContainer("");
    spacer->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(height));
    return spacer;
}

// A picture: media/images/ultraos/<stem>.png when the repository carries it,
// else a tile with the caption of the photo the page shows there.
std::shared_ptr<UltraCanvasUIElement> PictureBox(const std::string& id, float w, float h,
                                                 const std::string& stem, const std::string& caption) {
    if (HasPicture(stem)) {
        auto image = CreateImageFromFile(id, 0, 0, w, h, Picture((stem + ".png").c_str()));
        image->SetFitMode(ImageFitMode::Contain);
        image->SetBorderRadius(10);
        image->layoutItem.SetFlexShrink(0);
        return image;
    }
    auto tile = CreateContainer(id, 0, 0, w, h);
    tile->SetBackgroundColor(kTile);
    tile->SetBorderRadius(10);
    tile->layout.SetFlexRow().SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
        .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    tile->layoutItem.SetFlexShrink(0);
    auto label = CreateLabel(id + "Caption", caption);
    label->SetFont(kFont, 15, FontWeight::Bold);
    label->SetTextColor(kInkSoft);
    tile->AddChild(label);
    return tile;
}

// The newsletter form the page shows twice: e-mail, the privacy checkbox,
// Subscribe. The demo sends nothing; it answers in the line below.
std::shared_ptr<UltraCanvasContainer> NewsletterForm(const std::string& id, bool onDark) {
    auto form = CreateContainer(id);
    form->layout.SetDisplay(CSSLayout::DisplayType::Block);
    form->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());

    auto email = CreateTextInput(id + "Email", 0, 0, 0, 42);
    email->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(42));
    email->SetPlaceholder("Email");
    email->SetFontSize(15);
    form->AddChild(email);
    form->AddChild(Gap(10));

    auto privacy = std::make_shared<UltraCanvasCheckbox>(id + "Privacy", "I accept the privacy policy");
    privacy->SetFont(kFont, 14);
    {
        CheckboxVisualStyle style = privacy->GetVisualStyle();
        style.base.textColor = onDark ? kPaper : kInk;
        style.base.textHoverColor = onDark ? kPaper : kInk;
        privacy->SetVisualStyle(style);
    }
    form->AddChild(privacy);
    form->AddChild(Gap(12));

    auto subscribe = Action(id + "Subscribe", "Subscribe", "", true);
    form->AddChild(subscribe);
    form->AddChild(Gap(10));

    auto reply = Text(id + "Reply", " ", 13, onDark ? kOnDark : kInkSoft);
    form->AddChild(reply);

    auto submit = [email, privacy, reply]() {
        const std::string address = email->GetText();
        if (address.find('@') == std::string::npos) {
            reply->SetText("Please enter your e-mail address.");
        } else if (!privacy->IsChecked()) {
            reply->SetText("Please accept the privacy policy first.");
        } else {
            reply->SetText("Thank you - " + address + " is on the list. (This demo sends nothing.)");
        }
    };
    subscribe->onClick = submit;
    email->onEnterPressed = [submit](const std::string&) { submit(); return true; };
    return form;
}

// ---- the page, top to bottom ----------------------------------------------------
std::vector<std::shared_ptr<UltraCanvasMenu>> gMenus;   // the navigation's drop-downs, kept alive

std::shared_ptr<UltraCanvasContainer> BuildHeader() {
    auto band = MakeBand("Header", kNight, 10);
    band.inner->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center).SetFlexGap(14);

    auto logo = CreateImageFromFile("Logo", 0, 0, 96, 58, Picture("UOS-E.png"));
    logo->SetFitMode(ImageFitMode::Contain);
    logo->SetMouseCursor(UCMouseCursor::Hand);
    logo->SetTooltip("ULTRA OS - Home");
    logo->onClick = []() { GoTo(""); };
    logo->layoutItem.SetFlexShrink(0);
    band.inner->AddChild(logo);

    // The menu wraps onto a second line when the window is narrow, starting
    // under its first item rather than under the logo.
    auto menu = Row("NavRow", 2, CSSLayout::AlignItems::Center);
    menu->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    menu->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
    band.inner->AddChild(menu);

    for (size_t i = 0; i < kNav.size(); ++i) {
        const NavItem& nav = kNav[i];
        const bool dropdown = !nav.menu.empty();
        auto item = Flat("Nav" + std::to_string(i), nav.title, 13.5f,
                         i == 0 ? kRed : (dropdown ? kOnDark : kPaper), kRed);
        if (i == 0) item->SetFont(kFont, 13.5f, FontWeight::Bold);
        item->SetPadding(0, 7);
        item->layoutItem.SetFlexShrink(0);
        if (dropdown) {
            // The site's drop-down: a popup menu under the item, one entry per page.
            auto popup = std::make_shared<UltraCanvasMenu>("NavMenu" + std::to_string(i), 0, 0, 240, 0);
            popup->SetMenuType(MenuType::PopupMenu);
            for (const Link& link : nav.menu) {
                popup->AddItem(MenuItemData::Action(link.title, Open(link.path)));
            }
            gMenus.push_back(popup);
            item->SetTooltip(std::string(nav.title) + ": opens a menu");
            // The button holds the callback, so it is captured raw; the popup
            // is kept alive by gMenus and the capture.
            item->onClick = [popup, anchor = item.get()]() {
                if (auto* window = anchor->GetWindow()) {
                    const Rect2Df b = anchor->GetBoundsInWindow();
                    popup->OpenMenuAnchored(Rect2Di(static_cast<int>(b.x), static_cast<int>(b.y),
                                                    static_cast<int>(b.width), static_cast<int>(b.height)),
                                            *window, PopupElementSettings(), 4);
                }
            };
        } else {
            item->onClick = Open(nav.path);
        }
        menu->AddChild(item);
    }
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildHero() {
    auto band = MakeBand("Hero", kNightSoft, 84);
    band.inner->AddChild(Kicker("HeroKicker", kHeroKicker));
    band.inner->AddChild(Gap(16));
    band.inner->AddChild(Text("HeroTitle", kHeroTitle, 40, kPaper, FontWeight::Bold, TextAlignment::Center));
    band.inner->AddChild(Gap(20));
    auto lead = Text("HeroText", kHeroText, 19, kOnDark, FontWeight::Normal, TextAlignment::Center);
    lead->SetPadding(0, 90);
    lead->box.boxSizing = CSSLayout::BoxSizing::BorderBox;
    band.inner->AddChild(lead);
    band.inner->AddChild(Gap(32));
    auto actions = Row("HeroActions", 14, CSSLayout::AlignItems::Center, CSSLayout::JustifyContent::Center);
    actions->AddChild(Action("HeroLearn", "Learn More", "the-plan/", true));
    actions->AddChild(Action("HeroDownload", "Download", "download-file/", false, true));
    band.inner->AddChild(actions);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildIntro() {
    auto band = MakeBand("Intro", kPaper, 64);
    auto intro = Text("IntroText", kIntroText, 18, kInkSoft, FontWeight::Normal, TextAlignment::Center);
    intro->SetPadding(0, 60);
    intro->box.boxSizing = CSSLayout::BoxSizing::BorderBox;
    band.inner->AddChild(intro);
    band.inner->AddChild(Gap(36));
    auto row = Row("PlatformCards", 28);
    const char* titles[2] = {"For desktops", "For mobile"};
    const char* pictures[2] = {"PC", "Mobile"};
    for (int i = 0; i < 2; ++i) {
        auto card = Card("Platform" + std::to_string(i), 400, kPaperSoft, false);
        auto inner = Row("PlatformRow" + std::to_string(i), 22, CSSLayout::AlignItems::Center);
        inner->AddChild(PictureBox("PlatformPicture" + std::to_string(i), 150, 150, pictures[i], pictures[i]));
        auto title = Text("PlatformTitle" + std::to_string(i), titles[i], 26, kInk, FontWeight::Bold);
        title->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
        title->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
        inner->AddChild(title);
        card->AddChild(inner);
        row->AddChild(card);
    }
    band.inner->AddChild(row);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildReasons() {
    auto band = MakeBand("Reasons", kPaperSoft, 72);
    band.inner->AddChild(Kicker("ReasonsKicker", "Why you should support us"));
    band.inner->AddChild(Gap(10));
    band.inner->AddChild(Text("ReasonsTitle", "The reasons for ULTRA OS", 34, kInk, FontWeight::Bold,
                              TextAlignment::Center));
    band.inner->AddChild(Gap(32));
    auto row = Row("ReasonColumns", 28);
    for (int column = 0; column < 2; ++column) {
        auto list = Column("ReasonColumn" + std::to_string(column), 380);
        for (size_t i = column; i < kReasons.size(); i += 2) {
            auto line = CreateContainer("Reason" + std::to_string(i));
            line->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
            line->layout.SetFlexRow().SetFlexGap(14).SetFlexAlignItems(CSSLayout::AlignItems::Start);
            line->SetPadding(8, 0);
            auto mark = CreateContainer("ReasonMark" + std::to_string(i), 0, 0, 12, 12);
            mark->SetBackgroundColor(kRed);
            mark->SetBorderRadius(6);
            mark->SetMargin(7, 0, 0, 0);
            mark->layoutItem.SetFlexShrink(0);
            line->AddChild(mark);
            auto text = Text("ReasonText" + std::to_string(i), kReasons[i], 16, kInk);
            text->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
            text->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
            line->AddChild(text);
            list->AddChild(line);
        }
        row->AddChild(list);
    }
    band.inner->AddChild(row);
    band.inner->AddChild(Gap(28));
    auto actions = Row("ReasonActions", 14, CSSLayout::AlignItems::Center, CSSLayout::JustifyContent::Center);
    actions->AddChild(Action("ReasonsAbout", "About Us", "the-plan/", true));
    band.inner->AddChild(actions);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildNewsletter() {
    auto band = MakeBand("Newsletter", kNight, 56);
    auto row = Row("NewsletterRow", 48, CSSLayout::AlignItems::Center);
    auto logo = CreateImageFromFile("NewsletterLogo", 0, 0, 360, 220, Picture("UOS-E.png"));
    logo->SetFitMode(ImageFitMode::Contain);
    logo->layoutItem.SetFlexShrink(0);
    row->AddChild(logo);
    auto column = Column("NewsletterColumn", 360);
    column->AddChild(Text("NewsletterTitle", "Newsletter", 30, kPaper, FontWeight::Bold));
    column->AddChild(Gap(10));
    column->AddChild(Text("NewsletterText", kNewsletterText, 16, kOnDark));
    column->AddChild(Gap(18));
    column->AddChild(NewsletterForm("Newsletter", true));
    row->AddChild(column);
    band.inner->AddChild(row);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildStatus() {
    auto band = MakeBand("Status", kPaper, 72);
    auto row = Row("StatusRow", 48, CSSLayout::AlignItems::Center);
    row->AddChild(PictureBox("StatusPicture", 330, 440, "Status", "ULTRA OS"));
    auto column = Column("StatusColumn", 420);
    column->AddChild(Kicker("StatusKicker", "check the current status", TextAlignment::Left));
    column->AddChild(Gap(8));
    column->AddChild(Text("StatusTitle", "Status", 34, kInk, FontWeight::Bold));
    column->AddChild(Gap(14));
    column->AddChild(Text("StatusText", kStatusText, 16, kInkSoft));
    column->AddChild(Gap(14));
    column->AddChild(Text("StatusSupport", "We are looking for your support in different ways", 18, kInk,
                          FontWeight::Bold));
    column->AddChild(Gap(8));
    auto mail = Text("StatusMail", "If you feel that you can assist our effort then please send an email to: "
                     + std::string(kEmail), 16, kInkSoft);
    mail->SetTextLinks({LabelTextLink{static_cast<int>(mail->GetText().size() - std::string(kEmail).size()),
                                      static_cast<int>(mail->GetText().size()), std::string("mailto:") + kEmail}});
    mail->onLinkActivated = [](const std::string& url) { OpenURL(url); };
    column->AddChild(mail);
    column->AddChild(Gap(16));
    auto chips = Row("StatusAudiences", 10, CSSLayout::AlignItems::Center);
    for (size_t i = 0; i < kAudiences.size(); ++i) {
        auto chip = CreateLabel("Audience" + std::to_string(i), kAudiences[i]);
        chip->SetFont(kFont, 14, FontWeight::Bold);
        chip->SetTextColor(kRed);
        chip->SetPadding(6, 14);
        chip->SetBorders(1.5f, kRed, 16);
        chips->AddChild(chip);
    }
    column->AddChild(chips);
    column->AddChild(Gap(22));
    auto actions = Row("StatusActions", 14, CSSLayout::AlignItems::Center);
    actions->AddChild(Action("StatusButton", "current status", "status/", true));
    column->AddChild(actions);
    row->AddChild(column);
    band.inner->AddChild(row);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildOffers() {
    auto band = MakeBand("Offers", kPaperSoft, 72);
    auto row = Row("OfferCards", 28);
    for (size_t i = 0; i < kOffers.size(); ++i) {
        const Offer& offer = kOffers[i];
        const std::string id = "Offer" + std::to_string(i);
        auto card = Card(id, 400, kPaper, true);
        card->AddChild(Text(id + "Name", offer.name, 26, kInk, FontWeight::Bold, TextAlignment::Center));
        card->AddChild(Gap(6));
        card->AddChild(Text(id + "Subtitle", offer.subtitle, 15, kInkSoft, FontWeight::Normal, TextAlignment::Center));
        card->AddChild(Gap(16));
        card->AddChild(Text(id + "Price", offer.price, 44, kRed, FontWeight::Bold, TextAlignment::Center));
        card->AddChild(Gap(16));
        for (size_t k = 0; k < offer.items.size(); ++k) {
            auto line = Text(id + "Item" + std::to_string(k), offer.items[k], 15, kInk, FontWeight::Normal,
                             TextAlignment::Center);
            line->SetPadding(7, 0);
            if (k + 1 < offer.items.size()) line->SetBorderBottom(1, kRule);
            card->AddChild(line);
        }
        card->AddChild(Gap(22));
        auto actions = Row(id + "Actions", 14, CSSLayout::AlignItems::Center, CSSLayout::JustifyContent::Center);
        actions->AddChild(Action(id + "Preorder", "Pre-order", kPreorderUrl, true));
        card->AddChild(actions);
        card->AddChild(Gap(12));
        card->AddChild(Text(id + "Release", offer.release, 14, kInkSoft, FontWeight::Normal, TextAlignment::Center));
        row->AddChild(card);
    }
    band.inner->AddChild(row);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildDonate() {
    auto band = MakeBand("Donate", kNightSoft, 64);
    auto row = Row("DonateRow", 48, CSSLayout::AlignItems::Center);
    auto logo = CreateImageFromFile("DonateLogo", 0, 0, 300, 200, Picture("UOS-E.png"));
    logo->SetFitMode(ImageFitMode::Contain);
    logo->layoutItem.SetFlexShrink(0);
    row->AddChild(logo);
    auto column = Column("DonateColumn", 420);
    auto links = Row("DonateLinks", 12, CSSLayout::AlignItems::Center);
    for (size_t i = 0; i < kDonate.size(); ++i) {
        links->AddChild(Action("Donate" + std::to_string(i), kDonate[i].title, kDonate[i].path, i == 0, true));
    }
    column->AddChild(links);
    column->AddChild(Gap(22));
    column->AddChild(Text("DonateText1", kDonateText1, 16, kOnDark));
    column->AddChild(Gap(12));
    column->AddChild(Text("DonateText2", kDonateText2, 16, kOnDark));
    column->AddChild(Gap(14));
    column->AddChild(Text("DonateText3", kDonateText3, 20, kPaper, FontWeight::Bold));
    row->AddChild(column);
    band.inner->AddChild(row);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildSocial() {
    auto band = MakeBand("Social", kPaper, 48);
    band.inner->AddChild(Text("SocialTitle", "Follow us at our social networks", 30, kInk, FontWeight::Bold,
                              TextAlignment::Center));
    band.inner->AddChild(Gap(22));
    auto row = Row("SocialLinks", 12, CSSLayout::AlignItems::Center, CSSLayout::JustifyContent::Center);
    for (size_t i = 0; i < kSocial.size(); ++i) {
        row->AddChild(Action("Social" + std::to_string(i), kSocial[i].title, kSocial[i].path, false));
    }
    band.inner->AddChild(row);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildVariants() {
    auto band = MakeBand("Variants", kPaperSoft, 72);
    band.inner->AddChild(Text("VariantsTitle", "8 ULTRA OS Variants", 34, kInk, FontWeight::Bold,
                              TextAlignment::Center));
    band.inner->AddChild(Gap(12));
    for (size_t i = 0; i < kVariants.size(); ++i) {
        const Variant& variant = kVariants[i];
        const std::string id = "Variant" + std::to_string(i);
        auto row = Row(id, 40, CSSLayout::AlignItems::Center);
        row->SetPadding(28, 0);
        if (i + 1 < kVariants.size()) row->SetBorderBottom(1, kRule);
        auto picture = PictureBox(id + "Picture", 300, 300, variant.picture, variant.caption);
        auto column = Column(id + "Column", 480);
        column->AddChild(Text(id + "Title", variant.title, 28, kInk, FontWeight::Bold));
        column->AddChild(Gap(12));
        column->AddChild(Text(id + "Text", variant.text, 16, kInkSoft));
        // The page alternates the picture's side.
        if (i % 2 == 0) { row->AddChild(picture); row->AddChild(column); }
        else            { row->AddChild(column); row->AddChild(picture); }
        band.inner->AddChild(row);
    }
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildPurposes() {
    auto band = MakeBand("Purposes", kPaper, 72);
    band.inner->AddChild(Kicker("PurposeKicker", "Designed for Usability, Built for You"));
    band.inner->AddChild(Gap(10));
    band.inner->AddChild(Text("PurposeTitle", "ULTRA OS Purpose based", 34, kInk, FontWeight::Bold,
                              TextAlignment::Center));
    band.inner->AddChild(Gap(32));
    auto row = Row("PurposeCards", 24);
    for (size_t i = 0; i < kPurposes.size(); ++i) {
        const std::string id = "Purpose" + std::to_string(i);
        auto card = Card(id, 240, kPaperSoft, false);
        card->AddChild(PictureBox(id + "Picture", 110, 110, kPurposes[i].picture, kPurposes[i].caption));
        card->AddChild(Gap(14));
        card->AddChild(Text(id + "Title", kPurposes[i].title, 20, kInk, FontWeight::Bold));
        card->AddChild(Gap(8));
        card->AddChild(Text(id + "Text", kPurposes[i].text, 14, kInkSoft));
        row->AddChild(card);
    }
    band.inner->AddChild(row);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildFaq() {
    auto band = MakeBand("Faq", kPaperSoft, 72);
    band.inner->AddChild(Kicker("FaqKicker", "FAQs"));
    band.inner->AddChild(Gap(10));
    band.inner->AddChild(Text("FaqTitle", "FAQ FOR OS", 34, kInk, FontWeight::Bold, TextAlignment::Center));
    band.inner->AddChild(Gap(10));
    band.inner->AddChild(Text("FaqText", "You got curious about ULTRA OS and UltraCanvas? Here some more answers "
                              "that we might have missed to cover.", 16, kInkSoft, FontWeight::Normal,
                              TextAlignment::Center));
    band.inner->AddChild(Gap(28));
    for (size_t i = 0; i < kFaq.size(); ++i) {
        const std::string id = "Faq" + std::to_string(i);
        auto entry = CreateContainer(id);
        entry->layout.SetDisplay(CSSLayout::DisplayType::Block);
        entry->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
        entry->SetBackgroundColor(kPaper);
        entry->SetBorders(1, kRule, 10);
        entry->SetPadding(6, 18);
        entry->SetMargin(0, 0, 12, 0);

        // The page's accordion: the question opens and closes its answer.
        auto question = Flat(id + "Question", std::string("+  ") + kFaq[i].question, 18, kInk, kRed);
        question->SetFont(kFont, 18, FontWeight::Bold);
        question->SetTextAlign(TextAlignment::Left);
        question->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(44));
        entry->AddChild(question);
        auto answer = Text(id + "Answer", kFaq[i].answer, 15, kInkSoft);
        answer->SetPadding(6, 10, 14, 10);
        answer->box.boxSizing = CSSLayout::BoxSizing::BorderBox;
        answer->SetVisible(i == 0);
        entry->AddChild(answer);
        const std::string text = kFaq[i].question;
        // The question holds the callback, so it is captured raw.
        question->onClick = [answer, question = question.get(), text]() {
            const bool open = !answer->IsVisible();
            answer->SetVisible(open);
            question->SetText((open ? "-  " : "+  ") + text);
        };
        if (i == 0) question->SetText(std::string("-  ") + text);
        band.inner->AddChild(entry);
    }
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildContact() {
    auto band = MakeBand("Contact", kPaper, 72);
    auto row = Row("ContactRow", 48, CSSLayout::AlignItems::Start);

    auto column = Column("ContactColumn", 420);
    column->AddChild(Kicker("ContactKicker", "Contact Us", TextAlignment::Left));
    column->AddChild(Gap(14));
    auto claim = Row("ContactClaim", 18, CSSLayout::AlignItems::Center);
    auto ucLogo = CreateImageFromFile("ContactUltraCanvasLogo", 0, 0, 72, 72, Picture("UltraCanvas-logo.png"));
    ucLogo->SetFitMode(ImageFitMode::Contain);
    ucLogo->layoutItem.SetFlexShrink(0);
    claim->AddChild(ucLogo);
    auto claimText = Text("ContactClaimText", kContactClaim, 16, kInkSoft);
    claimText->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    claimText->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
    claimText->SetMouseCursor(UCMouseCursor::Hand);
    claimText->onClick = Open("ultracanvas/");
    claim->AddChild(claimText);
    column->AddChild(claim);
    column->AddChild(Gap(18));
    column->AddChild(Text("ContactTitle", kContactTitle, 30, kInk, FontWeight::Bold));
    column->AddChild(Gap(18));
    for (const char* line : {kEmail, kPhoneDe, kPhoneTh}) {
        auto link = Flat(std::string("ContactLine") + line, line, 16, kRed, kRedHover);
        link->SetPadding(0, 0);
        link->SetTextAlign(TextAlignment::Left);
        const std::string url = (line == kEmail) ? std::string("mailto:") + kEmail : std::string("tel:") + line;
        link->onClick = [url]() { OpenURL(url); };
        auto holder = Row(std::string("ContactLineRow") + line, 0, CSSLayout::AlignItems::Center);
        holder->AddChild(link);
        column->AddChild(holder);
    }
    column->AddChild(Gap(8));
    column->AddChild(Text("ContactAddress", kAddress, 16, kInk));
    column->AddChild(Gap(14));
    column->AddChild(Text("ContactInvite", "If you have question or want to assist us, contact us!", 16, kInkSoft));
    row->AddChild(column);

    auto form = Card("ContactForm", 420, kPaperSoft, false);
    auto field = [&form](const std::string& id, const std::string& caption) {
        form->AddChild(Text(id + "Caption", caption, 14, kInk, FontWeight::Bold));
        form->AddChild(Gap(6));
        auto input = CreateTextInput(id, 0, 0, 0, 40);
        input->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(40));
        input->SetPlaceholder(caption);
        input->SetFontSize(15);
        form->AddChild(input);
        form->AddChild(Gap(12));
        return input;
    };
    auto name = field("ContactName", "Name");
    auto email = field("ContactEmail", "Email");
    field("ContactPhone", "Your Phone");
    form->AddChild(Text("ContactMessageCaption", "Message", 14, kInk, FontWeight::Bold));
    form->AddChild(Gap(6));
    auto message = std::make_shared<UltraCanvasTextArea>("ContactMessage", 0, 0, 0, 120);
    message->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(120));
    message->SetWordWrap(true);
    message->SetFontSize(15);
    form->AddChild(message);
    form->AddChild(Gap(16));
    auto send = Action("ContactSend", "Send", "", true);
    form->AddChild(send);
    form->AddChild(Gap(10));
    auto reply = Text("ContactReply", " ", 13, kInkSoft);
    form->AddChild(reply);
    send->onClick = [name, email, message, reply]() {
        if (name->GetText().empty() || email->GetText().find('@') == std::string::npos) {
            reply->SetText("Please fill in your name and e-mail address.");
        } else if (message->GetText().empty()) {
            reply->SetText("Please write a message.");
        } else {
            reply->SetText("Thank you, " + name->GetText() + ". (This demo sends nothing.)");
        }
    };
    row->AddChild(form);
    band.inner->AddChild(row);
    return band.outer;
}

std::shared_ptr<UltraCanvasContainer> BuildFooter() {
    auto band = MakeBand("Footer", kNight, 48);
    auto row = Row("FooterColumns", 40, CSSLayout::AlignItems::Start);

    auto brand = Column("FooterBrand", 260);
    auto logo = CreateImageFromFile("FooterLogo", 0, 0, 200, 124, Picture("UOS-E.png"));
    logo->SetFitMode(ImageFitMode::Contain);
    brand->AddChild(logo);
    brand->AddChild(Gap(12));
    auto social = Row("FooterSocial", 2, CSSLayout::AlignItems::Center);
    for (size_t i = 0; i < kFooterSocial.size(); ++i) {
        auto link = Flat("FooterSocial" + std::to_string(i), kFooterSocial[i].title, 14, kOnDark, kPaper);
        link->onClick = Open(kFooterSocial[i].path);
        social->AddChild(link);
    }
    brand->AddChild(social);
    row->AddChild(brand);

    auto contact = Column("FooterContact", 220);
    contact->AddChild(Text("FooterContactTitle", "Contact", 20, kPaper, FontWeight::Bold));
    contact->AddChild(Gap(12));
    contact->AddChild(Text("FooterAddress", kAddress, 15, kOnDark));
    contact->AddChild(Gap(10));
    contact->AddChild(Text("FooterEmail", kEmail, 15, kPaper, FontWeight::Bold));
    contact->AddChild(Text("FooterPhone", kPhoneTh, 15, kOnDark));
    row->AddChild(contact);

    auto services = Column("FooterServices", 180);
    services->AddChild(Text("FooterServicesTitle", "Services", 20, kPaper, FontWeight::Bold));
    services->AddChild(Gap(8));
    for (size_t i = 0; i < kFooterServices.size(); ++i) {
        auto link = Flat("FooterService" + std::to_string(i), kFooterServices[i].title, 15, kOnDark, kPaper);
        link->SetPadding(0, 0);
        link->SetTextAlign(TextAlignment::Left);
        link->onClick = Open(kFooterServices[i].path);
        auto holder = Row("FooterServiceRow" + std::to_string(i), 0, CSSLayout::AlignItems::Center);
        holder->AddChild(link);
        services->AddChild(holder);
    }
    row->AddChild(services);

    auto newsletter = Column("FooterNewsletter", 300);
    newsletter->AddChild(Text("FooterNewsletterTitle", "Newsletter", 20, kPaper, FontWeight::Bold));
    newsletter->AddChild(Gap(12));
    newsletter->AddChild(Text("FooterNewsletterText", kNewsletterText, 15, kOnDark));
    newsletter->AddChild(Gap(14));
    newsletter->AddChild(NewsletterForm("FooterNewsletter", true));
    row->AddChild(newsletter);

    band.inner->AddChild(row);
    band.inner->AddChild(Gap(36));
    auto bottom = CreateContainer("FooterBottom");
    bottom->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    bottom->SetBorderTop(1, Color(255, 255, 255, 40));
    bottom->SetPadding(18, 0, 0, 0);
    bottom->layout.SetDisplay(CSSLayout::DisplayType::Block);
    bottom->AddChild(Text("FooterCopyright", kCopyright, 14, kOnDark, FontWeight::Normal, TextAlignment::Center));
    band.inner->AddChild(bottom);
    return band.outer;
}

// What the site calls that path, from the navigation tables.
std::string TitleFor(const std::string& sitePath) {
    for (const NavItem& nav : kNav) {
        if (nav.menu.empty() && sitePath == nav.path) return nav.title;
        for (const Link& link : nav.menu) {
            if (sitePath == link.path) return link.title;
        }
    }
    for (const Link& link : kFooterServices) {
        if (sitePath == link.path) return link.title;
    }
    return sitePath;
}

// A page of the site other than the front page. The demo does not carry the
// site's other pages; what it shows is the address working: the title the
// site gives the path, where the real page is, and the way back.
std::shared_ptr<UltraCanvasContainer> BuildSitePage(const std::string& sitePath) {
    auto band = MakeBand("SitePage", kPaper, 84);
    band.inner->AddChild(Kicker("SitePageKicker", kSiteUrl + sitePath));
    band.inner->AddChild(Gap(16));
    band.inner->AddChild(Text("SitePageTitle", TitleFor(sitePath), 40, kInk, FontWeight::Bold,
                              TextAlignment::Center));
    band.inner->AddChild(Gap(20));
    auto lead = Text("SitePageText",
                     "This page of ultraos.eu is not part of the demo. What the demo shows here is one "
                     "WebAssembly module answering several addresses: the address bar changed to this page, "
                     "the browser\xE2\x80\x99s back button returns to the front page, and a reload opens this "
                     "page again.",
                     19, kInkSoft, FontWeight::Normal, TextAlignment::Center);
    lead->SetPadding(0, 90);
    lead->box.boxSizing = CSSLayout::BoxSizing::BorderBox;
    band.inner->AddChild(lead);
    band.inner->AddChild(Gap(32));
    auto actions = Row("SitePageActions", 14, CSSLayout::AlignItems::Center, CSSLayout::JustifyContent::Center);
    actions->AddChild(Action("SitePageOpen", "Open on ultraos.eu", Url(sitePath), true));   // absolute: the system browser
    auto home = Action("SitePageHome", "Front page", "", false);
    home->onClick = []() { GoTo(""); };
    actions->AddChild(home);
    band.inner->AddChild(actions);
    return band.outer;
}

// Replaces the page's sections with those of `sitePath`: the front page, or
// the site page for any other path. The header and footer are on every page.
void ShowDemoPage(const std::string& sitePath) {
    if (!gPage) return;
    gMenus.clear();
    gPage->ClearChildren();
    gPage->AddChild(BuildHeader());
    if (sitePath.empty()) {
        gPage->AddChild(BuildHero());
        gPage->AddChild(BuildIntro());
        gPage->AddChild(BuildReasons());
        gPage->AddChild(BuildNewsletter());
        gPage->AddChild(BuildStatus());
        gPage->AddChild(BuildOffers());
        gPage->AddChild(BuildDonate());
        gPage->AddChild(BuildSocial());
        gPage->AddChild(BuildVariants());
        gPage->AddChild(BuildPurposes());
        gPage->AddChild(BuildFaq());
        gPage->AddChild(BuildContact());
    } else {
        gPage->AddChild(BuildSitePage(sitePath));
    }
    gPage->AddChild(BuildFooter());
}

} // namespace

// ultraos-frontpage [width height [site path]] - the window size on the
// desktop (the browser build fills the viewport); a tall one shows more of
// the page at once. A site path ("the-plan/") opens that page of the demo
// instead of the front page, as `?page=the-plan/` does in the browser.
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
    cfg.title = "Home - ULTRA OS - Europe's Open Source Operating System # userfriendly # cross-platform API";
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
    gPage = page;
    window->AddChild(page);

#if defined(__EMSCRIPTEN__)
    // The address decides the page: `?page=<site path>` now, and again on
    // every back and forward (the History API's popstate).
    ShowDemoPage(WASMURL::GetQueryParameter("page"));
    WASMURL::SetOnNavigate([](const std::string&) { ShowDemoPage(WASMURL::GetQueryParameter("page")); });
    window->Maximize();   // fill the browser viewport
#else
    ShowDemoPage(argc >= 4 ? argv[3] : "");   // a site path on the command line opens that page
#endif
    window->Show();
    app.Run(); // never returns in the browser; the main loop keeps ticking
    return 0;
}
