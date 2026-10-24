// Apps/DemoApp/UltraCanvasBreadcrumbExamples.cpp
// Breadcrumb navigation component demonstration for main demo app
// Version: 1.3.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  example, breadcrumbs as wide as the card, the indicator and
//                  separator galleries as wrapping tiles, every text sized to fit
// Version: 1.2.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasBreadcrumb.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include <sstream>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBreadcrumbExamples() {
        auto page = DemoKit::CreatePage("BreadcrumbExamples", "UltraCanvas Breadcrumb",
                "Style presets, separators, dropdowns, icons, navigation, and overflow modes");

        // Status label - under the description, a boxed line the callbacks write to.
        auto statusLabel = DemoKit::AddText(page, "BreadcrumbStatus",
                                            "Click on the elements to see callback results");
        statusLabel->SetFontSize(11);
        statusLabel->SetBackgroundColor(Color(245, 245, 245, 255));
        statusLabel->SetBorders(1.0f);
        statusLabel->SetPadding(8.0f);

        auto reportClick = [statusLabel](const std::string& section, int index, const BreadcrumbItem& item) {
            std::ostringstream oss;
            oss << section << ": clicked '" << item.text << "' (index " << index << ")";
            statusLabel->SetText(oss.str());
        };

        // ========================================
        // SECTION 1: DEFAULT STYLE
        // ========================================
        auto card1 = DemoKit::AddCard(page, "BC_Section1", "1. Default style",
                "Chevron separators and bold current item");

        auto bcDefault = CreateBreadcrumb("bc_default", 0, 0, 0, 30);
        bcDefault->AddItem("Home");
        bcDefault->AddItem("Documents");
        bcDefault->AddItem("Projects");
        bcDefault->AddItem("UltraCanvas");
        bcDefault->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Default", idx, item);
        };
        DemoKit::Add(card1, bcDefault);

        // ========================================
        // SECTION 2: COMPACT STYLE
        // ========================================
        auto card2 = DemoKit::AddCard(page, "BC_Section2", "2. Compact style",
                "Smaller font and tighter padding");

        auto bcCompact = CreateBreadcrumb("bc_compact", 0, 0, 0, 24);
        bcCompact->SetStyle(BreadcrumbStyle::Compact());
        bcCompact->AddItem("Home");
        bcCompact->AddItem("Documents");
        bcCompact->AddItem("Projects");
        bcCompact->AddItem("UltraCanvas");
        bcCompact->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Compact", idx, item);
        };
        DemoKit::Add(card2, bcCompact);

        // ========================================
        // SECTION 3: PILLS STYLE
        // ========================================
        auto card3 = DemoKit::AddCard(page, "BC_Section3", "3. Pills style",
                "Rounded backgrounds, no separators, highlighted current");

        auto bcPills = CreateBreadcrumb("bc_pills", 0, 0, 0, 32);
        bcPills->SetStyle(BreadcrumbStyle::Pills());
        bcPills->AddItem("Dashboard");
        bcPills->AddItem("Reports");
        bcPills->AddItem("Q4 2025");
        bcPills->AddItem("Summary");
        bcPills->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Pills", idx, item);
        };
        DemoKit::Add(card3, bcPills);

        // ========================================
        // SECTION 4: NUMBERED STEPS (dark wizard)
        // ========================================
        auto card4 = DemoKit::AddCard(page, "BC_Section15", "4. Numbered steps",
                "Steps() preset: arrow segments + round numbered indicators");

        auto bcSteps = CreateBreadcrumb("bc_steps", 0, 0, 0, 34);
        bcSteps->SetStyle(BreadcrumbStyle::Steps());
        bcSteps->AddItem("Bio");
        bcSteps->AddItem("Links");
        bcSteps->AddItem("Music");
        bcSteps->AddItem("Contact");
        bcSteps->SetCurrentIndex(0);   // highlight the active step
        bcSteps->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Steps", idx, item);
        };
        DemoKit::Add(card4, bcSteps);

        // ========================================
        // SECTION 5: PARALLELOGRAM STYLE
        // ========================================
        auto card5 = DemoKit::AddCard(page, "BC_Section16", "5. Parallelogram",
                "Parallelogram() preset: slanted interlocking segments");

        auto bcPara = CreateBreadcrumb("bc_para", 0, 0, 360, 32);
        bcPara->SetStyle(BreadcrumbStyle::Parallelogram());
        bcPara->AddItem("Level 1");
        bcPara->AddItem("Level 2");
        bcPara->AddItem("Level 3");
        bcPara->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Parallelogram", idx, item);
        };
        DemoKit::AddFixed(card5, bcPara);

        // ========================================
        // SECTION 6: LEVEL INDICATOR BACKGROUNDS
        // ========================================
        auto card6 = DemoKit::AddCard(page, "BC_Section17", "6. Level indicators",
                "Indicator background: round / rectangle / none / bordered");

        struct IndEntry {
            BreadcrumbLevelIndicatorBackground background;
            bool border;
            const char* label;
        };
        const IndEntry indEntries[] = {
            {BreadcrumbLevelIndicatorBackground::Round,        false, "Round"},
            {BreadcrumbLevelIndicatorBackground::Rectangle,    false, "Rectangle"},
            {BreadcrumbLevelIndicatorBackground::NoBackground, false, "None"},
            {BreadcrumbLevelIndicatorBackground::Round,        true,  "Bordered"},
        };

        // One tile per background - its name over its breadcrumb - in a row
        // that wraps.
        auto indGallery = DemoKit::AddRow(card6, "BC_IndGallery", 24);
        for (const auto& entry : indEntries) {
            auto tile = DemoKit::CreateColumn(std::string("BC_IndTile_") + entry.label, 4);
            DemoKit::Add(indGallery, tile);
            DemoKit::AddCaption(tile, std::string("BC_IndLbl_") + entry.label, entry.label);

            BreadcrumbStyle st = BreadcrumbStyle::Default();
            st.showLevelIndicator = true;
            st.levelIndicatorBackground = entry.background;
            st.levelIndicatorBorder = entry.border;
            st.levelIndicatorSize = 18;
            st.levelIndicatorColor = Color(0, 120, 215, 255);
            st.levelIndicatorBorderColor = Color(0, 90, 170, 255);
            // Number only (no fill) needs a dark glyph to read on the page background.
            st.levelIndicatorTextColor =
                (entry.background == BreadcrumbLevelIndicatorBackground::NoBackground)
                    ? Color(40, 40, 40, 255) : Colors::White;

            auto bc = CreateBreadcrumb(std::string("bc_ind_") + entry.label, 0, 0, 0, 26);
            bc->SetStyle(st);
            bc->AddItem("Home");
            bc->AddItem("Library");
            bc->AddItem("Album");
            bc->onItemClicked = [reportClick, name = std::string(entry.label)](int idx, const BreadcrumbItem& item) {
                reportClick(std::string("Indicator/") + name, idx, item);
            };
            DemoKit::Add(tile, bc);
        }

        // ========================================
        // SECTION 4: FILE EXPLORER STYLE
        // ========================================
        auto card7 = DemoKit::AddCard(page, "BC_Section4", "7. File explorer style",
                "Built from a path string via SetPath");

        auto bcExplorer = CreateBreadcrumb("bc_explorer", 0, 0, 0, 28);
        bcExplorer->SetStyle(BreadcrumbStyle::FileExplorer());
        bcExplorer->SetPath("/usr/local/share/applications", '/');
        bcExplorer->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("FileExplorer", idx, item);
        };
        DemoKit::Add(card7, bcExplorer);

        // ========================================
        // SECTION 5: WEB DOCS STYLE
        // ========================================
        auto card8 = DemoKit::AddCard(page, "BC_Section5", "8. Web docs style",
                "Slash separators, underline-on-hover, link colors");

        auto bcWeb = CreateBreadcrumb("bc_web", 0, 0, 0, 26);
        bcWeb->SetStyle(BreadcrumbStyle::WebDocs());
        bcWeb->AddItem("Docs");
        bcWeb->AddItem("Guides");
        bcWeb->AddItem("UI Components");
        bcWeb->AddItem("Breadcrumb");
        bcWeb->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("WebDocs", idx, item);
        };
        DemoKit::Add(card8, bcWeb);


        // ========================================
        // SECTION 7: ITEM WITH DROPDOWN
        // ========================================
        auto card9 = DemoKit::AddCard(page, "BC_Section6", "9. Item with dropdown",
                "Click the small chevron next to 'Projects' to open the menu");

        auto bcDropdown = CreateBreadcrumb("bc_dropdown", 0, 0, 0, 30);
        bcDropdown->AddItem("Workspace");
        std::vector<MenuItemData> projectsMenu = {
            MenuItemData::Action("Open UltraCanvas", [statusLabel]() {
                statusLabel->SetText("Dropdown: opened 'UltraCanvas'");
            }),
            MenuItemData::Action("Open DemoApp", [statusLabel]() {
                statusLabel->SetText("Dropdown: opened 'DemoApp'");
            }),
            MenuItemData::Action("Open Texter", [statusLabel]() {
                statusLabel->SetText("Dropdown: opened 'Texter'");
            }),
            MenuItemData::Separator(),
            MenuItemData::Action("New project...", [statusLabel]() {
                statusLabel->SetText("Dropdown: new project requested");
            }),
        };
        bcDropdown->AddItem(BreadcrumbItem::WithDropdown("Projects", projectsMenu));
        bcDropdown->AddItem("UltraCanvas");
        bcDropdown->AddItem("Source");
        bcDropdown->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Dropdown", idx, item);
        };
        bcDropdown->onItemDropdown = [statusLabel](int idx, const BreadcrumbItem& item) {
            std::ostringstream oss;
            oss << "Dropdown chevron clicked on '" << item.text << "' (index " << idx << ")";
            statusLabel->SetText(oss.str());
        };
        DemoKit::Add(card9, bcDropdown);

        // ========================================
        // SECTION 8: ICONS
        // ========================================
        auto card10 = DemoKit::AddCard(page, "BC_Section7", "10. Icons",
                "Per-item leading icons via WithIcon / IconOnly");

        const std::string iconsRoot = NormalizePath(GetResourcesDir() + "media/icons/");
        auto bcIcons = CreateBreadcrumb("bc_icons", 0, 0, 0, 30);
        bcIcons->AddItem(BreadcrumbItem::IconOnly(iconsRoot + "home-user.svg", "Home"));
        bcIcons->AddItem(BreadcrumbItem::WithIcon("Documents", iconsRoot + "folder.png"));
        bcIcons->AddItem(BreadcrumbItem::WithIcon("Projects", iconsRoot + "folder.png"));
        bcIcons->AddItem(BreadcrumbItem::WithIcon("Report.txt", iconsRoot + "document.png"));
        bcIcons->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Icons", idx, item);
        };
        DemoKit::Add(card10, bcIcons);

        // ========================================
        // SECTION 9: LIVE NAVIGATION
        // ========================================
        auto card11 = DemoKit::AddCard(page, "BC_Section8", "11. Live navigation",
                "Clicking a segment truncates the path — try it!");

        const std::string initialNavPath = "/home/user/projects/ultracanvas/Apps/DemoApp";
        auto bcNav = CreateBreadcrumb("bc_nav", 0, 0, 0, 30);
        bcNav->SetPath(initialNavPath, '/');

        auto resetBtn = std::make_shared<UltraCanvasButton>("BC_NavReset", 0, 0, 0, 28);
        resetBtn->SetText("Reset");
        resetBtn->SetTooltip("Restore the original path");

        // The breadcrumb takes the row's width beside the button; the path
        // it shows goes under them.
        auto navRow = DemoKit::AddRow(card11, "BC_NavRow", 10);
        DemoKit::Add(navRow, bcNav, 1);
        DemoKit::Add(navRow, resetBtn);
        auto navHint = DemoKit::AddCaption(card11, "BC_NavHint", "Path: " + initialNavPath);

        // Capture by weak_ptr to avoid creating reference cycles.
        std::weak_ptr<UltraCanvasBreadcrumb> bcNavWeak = bcNav;
        std::weak_ptr<UltraCanvasLabel> navHintWeak = navHint;
        auto refreshHint = [bcNavWeak, navHintWeak]() {
            auto bc = bcNavWeak.lock();
            auto hint = navHintWeak.lock();
            if (!bc || !hint) return;
            hint->SetText("Path: " + bc->GetPath('/'));
        };

        bcNav->onItemClicked = [bcNavWeak, refreshHint, statusLabel](int idx, const BreadcrumbItem& item) {
            auto bc = bcNavWeak.lock();
            if (!bc) return;
            bc->RemoveItemsAfter(idx);
            refreshHint();
            std::ostringstream oss;
            oss << "Navigation: jumped to '" << item.text << "' (path truncated)";
            statusLabel->SetText(oss.str());
        };

        resetBtn->onClick = [bcNavWeak, refreshHint, statusLabel, initialNavPath]() {
            auto bc = bcNavWeak.lock();
            if (!bc) return;
            bc->SetPath(initialNavPath, '/');
            refreshHint();
            statusLabel->SetText("Navigation: path reset to original");
        };


        // ========================================
        // SECTION 10: COLLAPSE OVERFLOW
        // ========================================
        auto card12 = DemoKit::AddCard(page, "BC_Section9", "12. Collapse overflow",
                "Middle items hidden behind a '...' menu (narrow width)");

        auto bcCollapse = CreateBreadcrumb("bc_collapse", 0, 0, 360, 30);
        bcCollapse->SetOverflowMode(BreadcrumbOverflowMode::Collapse);
        bcCollapse->AddItem("Root");
        bcCollapse->AddItem("Engineering");
        bcCollapse->AddItem("Frameworks");
        bcCollapse->AddItem("Graphics");
        bcCollapse->AddItem("2D");
        bcCollapse->AddItem("Cairo");
        bcCollapse->AddItem("Renderers");
        bcCollapse->AddItem("Linux");
        bcCollapse->AddItem("X11");
        bcCollapse->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Collapse", idx, item);
        };
        bcCollapse->onOverflowOpened = [statusLabel]() {
            statusLabel->SetText("Overflow menu opened — pick any hidden segment");
        };
        DemoKit::AddFixed(card12, bcCollapse);

        // ========================================
        // SECTION 11: ELLIPSIZE OVERFLOW
        // ========================================
        auto card13 = DemoKit::AddCard(page, "BC_Section10", "13. Ellipsize overflow",
                "Per-item text trimmed via maxItemTextWidth=60");

        auto bcEllipsize = CreateBreadcrumb("bc_ellipsize", 0, 0, 0, 30);
        bcEllipsize->SetOverflowMode(BreadcrumbOverflowMode::Ellipsize);
        bcEllipsize->SetMaxItemTextWidth(60);
        bcEllipsize->AddItem("A Very Long Workspace Name");
        bcEllipsize->AddItem("Quarterly Financial Reports");
        bcEllipsize->AddItem("Department of Cross-Platform Engineering");
        bcEllipsize->AddItem("Final Approved Version 2025-Q4");
        bcEllipsize->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Ellipsize", idx, item);
        };
        DemoKit::Add(card13, bcEllipsize);

        // ========================================
        // SECTION 12: SHRINK-TEXT OVERFLOW
        // ========================================
        auto card14 = DemoKit::AddCard(page, "BC_Section11", "14. ShrinkText overflow",
                "Per-item width auto-reduces until everything fits");

        auto bcShrink = CreateBreadcrumb("bc_shrink", 0, 0, 420, 30);
        bcShrink->SetOverflowMode(BreadcrumbOverflowMode::ShrinkText);
        bcShrink->AddItem("Customers");
        bcShrink->AddItem("Acme Corporation");
        bcShrink->AddItem("North America Division");
        bcShrink->AddItem("Quarterly Reports");
        bcShrink->AddItem("2026 Q2 Final");
        bcShrink->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("ShrinkText", idx, item);
        };
        DemoKit::AddFixed(card14, bcShrink);

        // ========================================
        // SECTION 13: ROUNDED STRIP
        // ========================================
        auto card15 = DemoKit::AddCard(page, "BC_Section12", "15. Rounded strip",
                "Light gray rounded background, uniform clickable segments");

        auto bcStrip = CreateBreadcrumb("bc_strip", 0, 0, 420, 32);
        BreadcrumbStyle stripStyle = BreadcrumbStyle::Default();
        stripStyle.backgroundColor       = Color(243, 243, 243, 255);
        stripStyle.cornerRadius          = 4.0f;
        stripStyle.separatorStyle        = BreadcrumbSeparatorStyle::Chevron;
        stripStyle.separatorColor        = Color(140, 140, 140, 255);
        stripStyle.itemTextColor         = Color(60, 60, 60, 255);
        stripStyle.currentItemTextColor  = stripStyle.itemTextColor;
        stripStyle.currentItemBold       = false;
        stripStyle.currentItemClickable  = true;
        stripStyle.itemPaddingHorizontal = 10;
        stripStyle.separatorSpacing      = 8;
        bcStrip->SetStyle(stripStyle);
        bcStrip->AddItem("PlantUML");
        bcStrip->AddItem("Language specification");
        bcStrip->AddItem("Yaml Diagram");
        bcStrip->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("RoundedStrip", idx, item);
        };
        DemoKit::AddFixed(card15, bcStrip);

        // ========================================
        // SECTION 6: SEPARATOR GALLERY
        // ========================================
        auto card16 = DemoKit::AddCard(page, "BC_Section13", "16. Separator gallery",
                "One example per BreadcrumbSeparatorStyle");

        struct SepEntry {
        BreadcrumbSeparatorStyle style;
        const char* label;
        };
        const SepEntry sepEntries[] = {
            {BreadcrumbSeparatorStyle::Chevron,       "Chevron"},
            {BreadcrumbSeparatorStyle::ChevronDouble, "ChevronDouble"},
            {BreadcrumbSeparatorStyle::Slash,         "Slash"},
            {BreadcrumbSeparatorStyle::GreaterThan,   "GreaterThan"},
            {BreadcrumbSeparatorStyle::Arrow,         "Arrow"},
            {BreadcrumbSeparatorStyle::Bullet,        "Bullet"},
            {BreadcrumbSeparatorStyle::Pipe,          "Pipe"},
            {BreadcrumbSeparatorStyle::Dot,           "Dot"},
        };

        // One tile per separator - its name over its breadcrumb - in a row
        // that wraps.
        auto sepGallery = DemoKit::AddRow(card16, "BC_SepGallery", 24);
        for (const auto& entry : sepEntries) {
            auto tile = DemoKit::CreateColumn(std::string("BC_SepTile_") + entry.label, 4);
            DemoKit::Add(sepGallery, tile);
            DemoKit::AddCaption(tile, std::string("BC_SepLbl_") + entry.label, entry.label);

            auto bc = CreateBreadcrumb(std::string("bc_sep_") + entry.label, 0, 0, 0, 26);
            bc->SetSeparatorStyle(entry.style);
            bc->AddItem("Alpha");
            bc->AddItem("Beta");
            bc->AddItem("Gamma");
            bc->AddItem("Delta");
            bc->onItemClicked = [reportClick, name = std::string(entry.label)](int idx, const BreadcrumbItem& item) {
                reportClick(std::string("Separator/") + name, idx, item);
            };
            DemoKit::Add(tile, bc);
        }

        // ========================================
        // SECTION 14: ARROW / STEPPER STYLE
        // ========================================
        auto card17 = DemoKit::AddCard(page, "BC_Section14", "17. Arrow steps",
                "Interlocking arrow segments; current step highlighted");

        auto bcArrow = CreateBreadcrumb("bc_arrow", 0, 0, 0, 30);
        bcArrow->SetStyle(BreadcrumbStyle::Arrow());
        bcArrow->AddItem("PlantUML");
        bcArrow->AddItem("Language specification");
        bcArrow->AddItem("Yaml Diagram");
        bcArrow->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Arrow", idx, item);
        };
        DemoKit::Add(card17, bcArrow);

        return page;
    }

} // namespace UltraCanvas
