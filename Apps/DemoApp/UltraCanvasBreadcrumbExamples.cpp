// Apps/DemoApp/UltraCanvasBreadcrumbExamples.cpp
// Breadcrumb navigation component demonstration for main demo app
// Version: 1.2.0
// Last Modified: 2026-05-16
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasBreadcrumb.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include <sstream>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBreadcrumbExamples() {
        auto mainContainer = std::make_shared<UltraCanvasContainer>("BreadcrumbExamples", 0, 0, 1000, 1620);

        // Shared palette — matches the other demo pages.
        const Color titleColor(50, 50, 150, 255);
        const Color sectionHeaderColor(0, 100, 200, 255);
        const Color descColor(100, 100, 100, 255);

        // Two-column layout coordinates.
        const int leftX = 40;
        const int leftWidth = 320;
        const int rightX = 380;
        const int rightWidth = 580;

        // ===== PAGE TITLE =====
        auto title = std::make_shared<UltraCanvasLabel>("BreadcrumbTitle", 20, 10, 600, 35);
        title->SetText("UltraCanvas Breadcrumb");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(titleColor);
        mainContainer->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("BreadcrumbSubtitle", 20, 45, 600, 25);
        subtitle->SetText("Style presets, separators, dropdowns, icons, navigation, and overflow modes");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(descColor);
        mainContainer->AddChild(subtitle);

        // Status label — pinned top-right, sized so it doesn't reach the section area below.
        auto statusLabel = std::make_shared<UltraCanvasLabel>("BreadcrumbStatus", 660, 10, 320, 60);
        statusLabel->SetText("Click on the elements to see callback results");
        statusLabel->SetFontSize(11);
        statusLabel->SetBackgroundColor(Color(245, 245, 245, 255));
        statusLabel->SetBorders(1.0f);
        statusLabel->SetPadding(8.0f);
        mainContainer->AddChild(statusLabel);

        auto reportClick = [statusLabel](const std::string& section, int index, const BreadcrumbItem& item) {
            std::ostringstream oss;
            oss << section << ": clicked '" << item.text << "' (index " << index << ")";
            statusLabel->SetText(oss.str());
        };

        // Helper for the left-column "number + bold title + small description" block.
        auto addDescription = [&](const std::string& id, int y,
                                  const std::string& heading, const std::string& body) {
            auto headingLbl = std::make_shared<UltraCanvasLabel>(id + "_h", leftX, y, leftWidth, 22);
            headingLbl->SetText(heading);
            headingLbl->SetFontSize(13);
            headingLbl->SetFontWeight(FontWeight::Bold);
            headingLbl->SetTextColor(sectionHeaderColor);
            mainContainer->AddChild(headingLbl);

            auto bodyLbl = std::make_shared<UltraCanvasLabel>(id + "_b", leftX, y + 22, leftWidth, 36);
            bodyLbl->SetText(body);
            bodyLbl->SetFontSize(11);
            bodyLbl->SetTextColor(descColor);
            mainContainer->AddChild(bodyLbl);
        };

        // Start below the status label so it never overlaps.
        int yOffset = 90;
        const int rowStep = 60;

        // ========================================
        // SECTION 1: DEFAULT STYLE
        // ========================================
        addDescription("BC_Section1", yOffset,
                       "1. Default style",
                       "Chevron separators and bold current item");

        auto bcDefault = CreateBreadcrumb("bc_default", rightX, yOffset + 4, rightWidth, 30);
        bcDefault->AddItem("Home");
        bcDefault->AddItem("Documents");
        bcDefault->AddItem("Projects");
        bcDefault->AddItem("UltraCanvas");
        bcDefault->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Default", idx, item);
        };
        mainContainer->AddChild(bcDefault);
        yOffset += rowStep;

        // ========================================
        // SECTION 2: COMPACT STYLE
        // ========================================
        addDescription("BC_Section2", yOffset,
                       "2. Compact style",
                       "Smaller font and tighter padding");

        auto bcCompact = CreateBreadcrumb("bc_compact", rightX, yOffset + 6, rightWidth, 24);
        bcCompact->SetStyle(BreadcrumbStyle::Compact());
        bcCompact->AddItem("Home");
        bcCompact->AddItem("Documents");
        bcCompact->AddItem("Projects");
        bcCompact->AddItem("UltraCanvas");
        bcCompact->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Compact", idx, item);
        };
        mainContainer->AddChild(bcCompact);
        yOffset += rowStep;

        // ========================================
        // SECTION 3: PILLS STYLE
        // ========================================
        addDescription("BC_Section3", yOffset,
                       "3. Pills style",
                       "Rounded backgrounds, no separators, highlighted current");

        auto bcPills = CreateBreadcrumb("bc_pills", rightX, yOffset + 4, rightWidth, 32);
        bcPills->SetStyle(BreadcrumbStyle::Pills());
        bcPills->AddItem("Dashboard");
        bcPills->AddItem("Reports");
        bcPills->AddItem("Q4 2025");
        bcPills->AddItem("Summary");
        bcPills->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Pills", idx, item);
        };
        mainContainer->AddChild(bcPills);
        yOffset += rowStep;

        // ========================================
        // SECTION 4: FILE EXPLORER STYLE
        // ========================================
        addDescription("BC_Section4", yOffset,
                       "4. File explorer style",
                       "Built from a path string via SetPath");

        auto bcExplorer = CreateBreadcrumb("bc_explorer", rightX, yOffset + 4, rightWidth, 28);
        bcExplorer->SetStyle(BreadcrumbStyle::FileExplorer());
        bcExplorer->SetPath("/usr/local/share/applications", '/');
        bcExplorer->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("FileExplorer", idx, item);
        };
        mainContainer->AddChild(bcExplorer);
        yOffset += rowStep;

        // ========================================
        // SECTION 5: WEB DOCS STYLE
        // ========================================
        addDescription("BC_Section5", yOffset,
                       "5. Web docs style",
                       "Slash separators, underline-on-hover, link colors");

        auto bcWeb = CreateBreadcrumb("bc_web", rightX, yOffset + 4, rightWidth, 26);
        bcWeb->SetStyle(BreadcrumbStyle::WebDocs());
        bcWeb->AddItem("Docs");
        bcWeb->AddItem("Guides");
        bcWeb->AddItem("UI Components");
        bcWeb->AddItem("Breadcrumb");
        bcWeb->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("WebDocs", idx, item);
        };
        mainContainer->AddChild(bcWeb);
        yOffset += rowStep;


        // ========================================
        // SECTION 7: ITEM WITH DROPDOWN
        // ========================================
        addDescription("BC_Section6", yOffset,
                       "6. Item with dropdown",
                       "Click the small chevron next to 'Projects' to open the menu");

        auto bcDropdown = CreateBreadcrumb("bc_dropdown", rightX, yOffset + 4, rightWidth, 30);
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
        mainContainer->AddChild(bcDropdown);
        yOffset += rowStep;

        // ========================================
        // SECTION 8: ICONS
        // ========================================
        addDescription("BC_Section7", yOffset,
                       "7. Icons",
                       "Per-item leading icons via WithIcon / IconOnly");

        const std::string iconsRoot = NormalizePath(GetResourcesDir() + "media/icons/");
        auto bcIcons = CreateBreadcrumb("bc_icons", rightX, yOffset + 4, rightWidth, 30);
        bcIcons->AddItem(BreadcrumbItem::IconOnly(iconsRoot + "home-icon.png", "Home"));
        bcIcons->AddItem(BreadcrumbItem::WithIcon("Documents", iconsRoot + "folder.png"));
        bcIcons->AddItem(BreadcrumbItem::WithIcon("Projects", iconsRoot + "folder.png"));
        bcIcons->AddItem(BreadcrumbItem::WithIcon("Report.txt", iconsRoot + "document.png"));
        bcIcons->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Icons", idx, item);
        };
        mainContainer->AddChild(bcIcons);
        yOffset += rowStep;

        // ========================================
        // SECTION 9: LIVE NAVIGATION
        // ========================================
        addDescription("BC_Section8", yOffset,
                       "8. Live navigation",
                       "Clicking a segment truncates the path — try it!");

        const std::string initialNavPath = "/home/user/projects/ultracanvas/Apps/DemoApp";
        auto bcNav = CreateBreadcrumb("bc_nav", rightX, yOffset + 4, rightWidth - 90, 30);
        bcNav->SetPath(initialNavPath, '/');

        auto resetBtn = std::make_shared<UltraCanvasButton>("BC_NavReset", rightX + rightWidth - 80, yOffset + 4, 80, 28);
        resetBtn->SetText("Reset");
        resetBtn->SetTooltip("Restore the original path");

        auto navHint = std::make_shared<UltraCanvasLabel>("BC_NavHint", rightX, yOffset + 38, rightWidth, 18);
        navHint->SetText("Path: " + initialNavPath);
        navHint->SetFontSize(10);
        navHint->SetTextColor(descColor);
        mainContainer->AddChild(navHint);

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

        mainContainer->AddChild(bcNav);
        mainContainer->AddChild(resetBtn);
        yOffset += rowStep + 10; // a bit extra for the path hint line

        // ========================================
        // SECTION 10: COLLAPSE OVERFLOW
        // ========================================
        addDescription("BC_Section9", yOffset,
                       "9. Collapse overflow",
                       "Middle items hidden behind a '...' menu (narrow width)");

        auto bcCollapse = CreateBreadcrumb("bc_collapse", rightX, yOffset + 4, 360, 30);
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
        mainContainer->AddChild(bcCollapse);
        yOffset += rowStep;

        // ========================================
        // SECTION 11: ELLIPSIZE OVERFLOW
        // ========================================
        addDescription("BC_Section10", yOffset,
                       "10. Ellipsize overflow",
                       "Per-item text trimmed via maxItemTextWidth=60");

        auto bcEllipsize = CreateBreadcrumb("bc_ellipsize", rightX, yOffset + 4, rightWidth, 30);
        bcEllipsize->SetOverflowMode(BreadcrumbOverflowMode::Ellipsize);
        bcEllipsize->SetMaxItemTextWidth(60);
        bcEllipsize->AddItem("A Very Long Workspace Name");
        bcEllipsize->AddItem("Quarterly Financial Reports");
        bcEllipsize->AddItem("Department of Cross-Platform Engineering");
        bcEllipsize->AddItem("Final Approved Version 2025-Q4");
        bcEllipsize->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Ellipsize", idx, item);
        };
        mainContainer->AddChild(bcEllipsize);
        yOffset += rowStep;

        // ========================================
        // SECTION 12: SHRINK-TEXT OVERFLOW
        // ========================================
        addDescription("BC_Section11", yOffset,
                       "11. ShrinkText overflow",
                       "Per-item width auto-reduces until everything fits");

        auto bcShrink = CreateBreadcrumb("bc_shrink", rightX, yOffset + 4, 420, 30);
        bcShrink->SetOverflowMode(BreadcrumbOverflowMode::ShrinkText);
        bcShrink->AddItem("Customers");
        bcShrink->AddItem("Acme Corporation");
        bcShrink->AddItem("North America Division");
        bcShrink->AddItem("Quarterly Reports");
        bcShrink->AddItem("2026 Q2 Final");
        bcShrink->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("ShrinkText", idx, item);
        };
        mainContainer->AddChild(bcShrink);
        yOffset += rowStep;

        // ========================================
        // SECTION 13: ROUNDED STRIP
        // ========================================
        addDescription("BC_Section12", yOffset,
                       "12. Rounded strip",
                       "Light gray rounded background, uniform clickable segments");

        auto bcStrip = CreateBreadcrumb("bc_strip", rightX, yOffset + 4, 420, 32);
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
        mainContainer->AddChild(bcStrip);
        yOffset += rowStep;

        // ========================================
        // SECTION 6: SEPARATOR GALLERY
        // ========================================
        addDescription("BC_Section13", yOffset,
                   "13. Separator gallery",
                   "One row per BreadcrumbSeparatorStyle");

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

        int sepY = yOffset;
        for (const auto& entry : sepEntries) {
            auto lbl = std::make_shared<UltraCanvasLabel>(std::string("BC_SepLbl_") + entry.label,
                                                          rightX, sepY + 4, 100, 22);
            lbl->SetText(entry.label);
            lbl->SetFontSize(11);
            lbl->SetTextColor(descColor);
            mainContainer->AddChild(lbl);

            auto bc = CreateBreadcrumb(std::string("bc_sep_") + entry.label,
                                       rightX + 110, sepY, rightWidth - 110, 26);
            bc->SetSeparatorStyle(entry.style);
            bc->AddItem("Alpha");
            bc->AddItem("Beta");
            bc->AddItem("Gamma");
            bc->AddItem("Delta");
            bc->onItemClicked = [reportClick, name = std::string(entry.label)](int idx, const BreadcrumbItem& item) {
                reportClick(std::string("Separator/") + name, idx, item);
            };
            mainContainer->AddChild(bc);
            sepY += 30;
        }
        yOffset = sepY + 15;

        // ========================================
        // SECTION 14: ARROW / STEPPER STYLE
        // ========================================
        addDescription("BC_Section14", yOffset,
                       "14. Arrow steps",
                       "Interlocking arrow segments; current step highlighted");

        auto bcArrow = CreateBreadcrumb("bc_arrow", rightX, yOffset + 4, rightWidth, 30);
        bcArrow->SetStyle(BreadcrumbStyle::Arrow());
        bcArrow->AddItem("PlantUML");
        bcArrow->AddItem("Language specification");
        bcArrow->AddItem("Yaml Diagram");
        bcArrow->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Arrow", idx, item);
        };
        mainContainer->AddChild(bcArrow);
        yOffset += rowStep;

        // ========================================
        // SECTION 15: NUMBERED STEPS (dark wizard)
        // ========================================
        addDescription("BC_Section15", yOffset,
                       "15. Numbered steps",
                       "Steps() preset: arrow segments + round numbered indicators");

        auto bcSteps = CreateBreadcrumb("bc_steps", rightX, yOffset + 4, rightWidth, 34);
        bcSteps->SetStyle(BreadcrumbStyle::Steps());
        bcSteps->AddItem("Bio");
        bcSteps->AddItem("Links");
        bcSteps->AddItem("Music");
        bcSteps->AddItem("Contact");
        bcSteps->SetCurrentIndex(0);   // highlight the active step
        bcSteps->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Steps", idx, item);
        };
        mainContainer->AddChild(bcSteps);
        yOffset += rowStep;

        // ========================================
        // SECTION 16: PARALLELOGRAM STYLE
        // ========================================
        addDescription("BC_Section16", yOffset,
                       "16. Parallelogram",
                       "Parallelogram() preset: slanted interlocking segments");

        auto bcPara = CreateBreadcrumb("bc_para", rightX, yOffset + 4, 360, 32);
        bcPara->SetStyle(BreadcrumbStyle::Parallelogram());
        bcPara->AddItem("Level 1");
        bcPara->AddItem("Level 2");
        bcPara->AddItem("Level 3");
        bcPara->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Parallelogram", idx, item);
        };
        mainContainer->AddChild(bcPara);
        yOffset += rowStep;

        // ========================================
        // SECTION 17: LEVEL INDICATOR BACKGROUNDS
        // ========================================
        addDescription("BC_Section17", yOffset,
                       "17. Level indicators",
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

        int indY = yOffset;
        for (const auto& entry : indEntries) {
            auto lbl = std::make_shared<UltraCanvasLabel>(std::string("BC_IndLbl_") + entry.label,
                                                          rightX, indY + 4, 90, 22);
            lbl->SetText(entry.label);
            lbl->SetFontSize(11);
            lbl->SetTextColor(descColor);
            mainContainer->AddChild(lbl);

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

            auto bc = CreateBreadcrumb(std::string("bc_ind_") + entry.label,
                                       rightX + 100, indY, rightWidth - 100, 26);
            bc->SetStyle(st);
            bc->AddItem("Home");
            bc->AddItem("Library");
            bc->AddItem("Album");
            bc->onItemClicked = [reportClick, name = std::string(entry.label)](int idx, const BreadcrumbItem& item) {
                reportClick(std::string("Indicator/") + name, idx, item);
            };
            mainContainer->AddChild(bc);
            indY += 32;
        }
        yOffset = indY + 15;

        return mainContainer;
    }

} // namespace UltraCanvas
