// Apps/DemoApp/UltraCanvasBreadcrumbExamples.cpp
// Breadcrumb navigation component demonstration for main demo app
// Version: 1.0.0
// Last Modified: 2026-05-15
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasBreadcrumb.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <sstream>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBreadcrumbExamples() {
        auto mainContainer = std::make_shared<UltraCanvasContainer>("BreadcrumbExamples", 0, 0, 1000, 1100);

        // ===== PAGE TITLE =====
        auto title = std::make_shared<UltraCanvasLabel>("BreadcrumbTitle", 20, 10, 600, 35);
        title->SetText("UltraCanvas Breadcrumb Showcase");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        mainContainer->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("BreadcrumbSubtitle", 20, 45, 800, 25);
        subtitle->SetText("Style presets, separator variants, dropdowns, and overflow strategies");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(subtitle);

        // Shared status label — every breadcrumb writes here on interaction.
        auto statusLabel = std::make_shared<UltraCanvasLabel>("BreadcrumbStatus", 570, 10, 410, 60);
        statusLabel->SetText("Click any segment to see selection feedback");
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

        int yOffset = 90;
        const Color sectionHeaderColor(200, 50, 50, 255);
        const Color descColor(100, 100, 100, 255);

        // ========================================
        // SECTION 1: DEFAULT STYLE
        // ========================================
        auto section1Label = std::make_shared<UltraCanvasLabel>("BC_Section1", 20, yOffset, 960, 25);
        section1Label->SetText("1. Default Style (Chevron separators, bold current item)");
        section1Label->SetFontWeight(FontWeight::Bold);
        section1Label->SetTextColor(sectionHeaderColor);
        mainContainer->AddChild(section1Label);
        yOffset += 30;

        auto bcDefault = CreateBreadcrumb("bc_default", 50, yOffset, 700, 30);
        bcDefault->AddItem("Home");
        bcDefault->AddItem("Documents");
        bcDefault->AddItem("Projects");
        bcDefault->AddItem("UltraCanvas");
        bcDefault->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Default", idx, item);
        };
        mainContainer->AddChild(bcDefault);
        yOffset += 45;

        // ========================================
        // SECTION 2: COMPACT STYLE
        // ========================================
        auto section2Label = std::make_shared<UltraCanvasLabel>("BC_Section2", 20, yOffset, 960, 25);
        section2Label->SetText("2. Compact Style (smaller font, tighter padding)");
        section2Label->SetFontWeight(FontWeight::Bold);
        section2Label->SetTextColor(sectionHeaderColor);
        mainContainer->AddChild(section2Label);
        yOffset += 30;

        auto bcCompact = CreateBreadcrumb("bc_compact", 50, yOffset, 700, 24);
        bcCompact->SetStyle(BreadcrumbStyle::Compact());
        bcCompact->AddItem("Home");
        bcCompact->AddItem("Documents");
        bcCompact->AddItem("Projects");
        bcCompact->AddItem("UltraCanvas");
        bcCompact->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Compact", idx, item);
        };
        mainContainer->AddChild(bcCompact);
        yOffset += 40;

        // ========================================
        // SECTION 3: PILLS STYLE
        // ========================================
        auto section3Label = std::make_shared<UltraCanvasLabel>("BC_Section3", 20, yOffset, 960, 25);
        section3Label->SetText("3. Pills Style (rounded backgrounds, no separators, highlighted current)");
        section3Label->SetFontWeight(FontWeight::Bold);
        section3Label->SetTextColor(sectionHeaderColor);
        mainContainer->AddChild(section3Label);
        yOffset += 30;

        auto bcPills = CreateBreadcrumb("bc_pills", 50, yOffset, 700, 32);
        bcPills->SetStyle(BreadcrumbStyle::Pills());
        bcPills->AddItem("Dashboard");
        bcPills->AddItem("Reports");
        bcPills->AddItem("Q4 2025");
        bcPills->AddItem("Summary");
        bcPills->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("Pills", idx, item);
        };
        mainContainer->AddChild(bcPills);
        yOffset += 50;

        // ========================================
        // SECTION 4: FILE EXPLORER STYLE
        // ========================================
        auto section4Label = std::make_shared<UltraCanvasLabel>("BC_Section4", 20, yOffset, 960, 25);
        section4Label->SetText("4. File Explorer Style (built from path string via SetPath)");
        section4Label->SetFontWeight(FontWeight::Bold);
        section4Label->SetTextColor(sectionHeaderColor);
        mainContainer->AddChild(section4Label);
        yOffset += 30;

        auto bcExplorer = CreateBreadcrumb("bc_explorer", 50, yOffset, 700, 28);
        bcExplorer->SetStyle(BreadcrumbStyle::FileExplorer());
        bcExplorer->SetPath("/usr/local/share/applications", '/');
        bcExplorer->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("FileExplorer", idx, item);
        };
        mainContainer->AddChild(bcExplorer);
        yOffset += 45;

        // ========================================
        // SECTION 5: WEB DOCS STYLE
        // ========================================
        auto section5Label = std::make_shared<UltraCanvasLabel>("BC_Section5", 20, yOffset, 960, 25);
        section5Label->SetText("5. Web Docs Style (slash separators, underline-on-hover, link colors)");
        section5Label->SetFontWeight(FontWeight::Bold);
        section5Label->SetTextColor(sectionHeaderColor);
        mainContainer->AddChild(section5Label);
        yOffset += 30;

        auto bcWeb = CreateBreadcrumb("bc_web", 50, yOffset, 700, 26);
        bcWeb->SetStyle(BreadcrumbStyle::WebDocs());
        bcWeb->AddItem("Docs");
        bcWeb->AddItem("Guides");
        bcWeb->AddItem("UI Components");
        bcWeb->AddItem("Breadcrumb");
        bcWeb->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
            reportClick("WebDocs", idx, item);
        };
        mainContainer->AddChild(bcWeb);
        yOffset += 45;

        // ========================================
        // SECTION 6: SEPARATOR GALLERY
        // ========================================
        auto section6Label = std::make_shared<UltraCanvasLabel>("BC_Section6", 20, yOffset, 960, 25);
        section6Label->SetText("6. Separator Gallery (one row per BreadcrumbSeparatorStyle)");
        section6Label->SetFontWeight(FontWeight::Bold);
        section6Label->SetTextColor(sectionHeaderColor);
        mainContainer->AddChild(section6Label);
        yOffset += 30;

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

        for (const auto& entry : sepEntries) {
            auto lbl = std::make_shared<UltraCanvasLabel>(std::string("BC_SepLbl_") + entry.label,
                                                         50, yOffset + 4, 140, 22);
            lbl->SetText(entry.label);
            lbl->SetFontSize(11);
            lbl->SetTextColor(descColor);
            mainContainer->AddChild(lbl);

            auto bc = CreateBreadcrumb(std::string("bc_sep_") + entry.label,
                                       200, yOffset, 500, 26);
            bc->SetSeparatorStyle(entry.style);
            bc->AddItem("Alpha");
            bc->AddItem("Beta");
            bc->AddItem("Gamma");
            bc->AddItem("Delta");
            bc->onItemClicked = [reportClick, name = std::string(entry.label)](int idx, const BreadcrumbItem& item) {
                reportClick(std::string("Separator/") + name, idx, item);
            };
            mainContainer->AddChild(bc);
            yOffset += 32;
        }
        yOffset += 10;

        // ========================================
        // SECTION 7: ITEM WITH DROPDOWN
        // ========================================
        auto section7Label = std::make_shared<UltraCanvasLabel>("BC_Section7", 20, yOffset, 960, 25);
        section7Label->SetText("7. Item with Dropdown (click the chevron next to 'Projects')");
        section7Label->SetFontWeight(FontWeight::Bold);
        section7Label->SetTextColor(sectionHeaderColor);
        mainContainer->AddChild(section7Label);
        yOffset += 30;

//        auto bcDropdown = CreateBreadcrumb("bc_dropdown", 50, yOffset, 700, 30);
//        bcDropdown->AddItem("Workspace");
//        std::vector<MenuItemData> projectsMenu = {
//            MenuItemData::Action("Open UltraCanvas", [statusLabel]() {
//                statusLabel->SetText("Dropdown: opened 'UltraCanvas'");
//            }),
//            MenuItemData::Action("Open DemoApp", [statusLabel]() {
//                statusLabel->SetText("Dropdown: opened 'DemoApp'");
//            }),
//            MenuItemData::Action("Open Texter", [statusLabel]() {
//                statusLabel->SetText("Dropdown: opened 'Texter'");
//            }),
//            MenuItemData::Action("New project...", [statusLabel]() {
//                statusLabel->SetText("Dropdown: new project requested");
//            }),
//        };
//        bcDropdown->AddItem(BreadcrumbItem::WithDropdown("Projects", projectsMenu));
//        bcDropdown->AddItem("UltraCanvas");
//        bcDropdown->AddItem("Source");
//        bcDropdown->onItemClicked = [reportClick](int idx, const BreadcrumbItem& item) {
//            reportClick("Dropdown", idx, item);
//        };
//        bcDropdown->onItemDropdown = [statusLabel](int idx, const BreadcrumbItem& item) {
//            std::ostringstream oss;
//            oss << "Dropdown chevron clicked on '" << item.text << "' (index " << idx << ")";
//            statusLabel->SetText(oss.str());
//        };
//        mainContainer->AddChild(bcDropdown);
//        yOffset += 50;

        // ========================================
        // SECTION 8: COLLAPSE OVERFLOW
        // ========================================
        auto section8Label = std::make_shared<UltraCanvasLabel>("BC_Section8", 20, yOffset, 960, 25);
        section8Label->SetText("8. Collapse Overflow (middle items hidden behind a '...' menu — narrow width)");
        section8Label->SetFontWeight(FontWeight::Bold);
        section8Label->SetTextColor(sectionHeaderColor);
        mainContainer->AddChild(section8Label);
        yOffset += 30;

        auto bcCollapse = CreateBreadcrumb("bc_collapse", 50, yOffset, 360, 30);
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

        auto collapseDesc = std::make_shared<UltraCanvasLabel>("BC_CollapseDesc", 430, yOffset + 4, 540, 24);
        collapseDesc->SetText("9-segment path in 360px wide control. First + tail kept, middle collapsed.");
        collapseDesc->SetFontSize(10);
        collapseDesc->SetTextColor(descColor);
        mainContainer->AddChild(collapseDesc);
        yOffset += 45;

        // ========================================
        // SECTION 9: ELLIPSIZE OVERFLOW
        // ========================================
        auto section9Label = std::make_shared<UltraCanvasLabel>("BC_Section9", 20, yOffset, 960, 25);
        section9Label->SetText("9. Ellipsize Overflow (per-item text trimmed via maxItemTextWidth)");
        section9Label->SetFontWeight(FontWeight::Bold);
        section9Label->SetTextColor(sectionHeaderColor);
        mainContainer->AddChild(section9Label);
        yOffset += 30;

        auto bcEllipsize = CreateBreadcrumb("bc_ellipsize", 50, yOffset, 700, 30);
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
        yOffset += 45;

        return mainContainer;
    }

} // namespace UltraCanvas
