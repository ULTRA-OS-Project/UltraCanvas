// Apps/DemoApp/UltraCanvasBreadcrumbExamples.cpp
// Breadcrumb navigation component demonstration for main demo app
// Version: 1.1.0
// Last Modified: 2026-05-16
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasBreadcrumb.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <sstream>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBreadcrumbExamples() {
        auto mainContainer = std::make_shared<UltraCanvasContainer>("BreadcrumbExamples", 0, 0, 1000, 1100);

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
        subtitle->SetText("Style presets, separator variants, dropdowns, and overflow strategies");
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
        // SECTION 6: SEPARATOR GALLERY
        // ========================================
        addDescription("BC_Section6", yOffset,
                       "6. Separator gallery",
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
        // SECTION 7: COLLAPSE OVERFLOW
        // ========================================
        addDescription("BC_Section7", yOffset,
                       "7. Collapse overflow",
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
        // SECTION 8: ELLIPSIZE OVERFLOW
        // ========================================
        addDescription("BC_Section8", yOffset,
                       "8. Ellipsize overflow",
                       "Per-item text trimmed via maxItemTextWidth");

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

        return mainContainer;
    }

} // namespace UltraCanvas
