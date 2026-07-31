// Apps/DemoApp/UltraCanvasTimelineDiagramExamples.cpp
// Timeline diagram examples: one tab per design preset, with live controls
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasTimelineDiagram.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace UltraCanvas {

    // Short phrases used by the "Add Random Item" button
    static TimelineItem RandomTimelineItem(int ordinal) {
        static const char* titles[] = {
                "New office", "Award won", "Team doubled", "Patent filed",
                "Partner signed", "Record quarter"};
        static const char* bodies[] = {
                "A second engineering site opens and hiring starts immediately.",
                "The product wins its category at the annual industry awards.",
                "Headcount doubles after the funding round closes.",
                "The core compression method is filed for a patent.",
                "A distribution partner is signed for three new regions.",
                "Revenue passes the previous best quarter by a wide margin."};

        const int pick = rand() % 6;
        TimelineItem item(std::to_string(2021 + ordinal), titles[pick], bodies[pick]);
        item.iconGlyph = std::to_string(ordinal + 1);
        item.SetDate(2021 + ordinal, 6, 1);
        return item;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTimelineDiagramExamples() {
        // Children placed directly inside a sidebar/panel container are inset
        // explicitly instead of using SetPadding() - the same pattern the
        // SWOT/Quadrant/Venn demos use - so absolutely-positioned children are
        // not clipped by the container's content area.
        const int PAD = 15;

        auto mainContainer = std::make_shared<UltraCanvasContainer>("tlMainContainer", 0, 0, 1140, 800);
        mainContainer->SetBackgroundColor(Color(248, 249, 250, 255));

        auto headerContainer = std::make_shared<UltraCanvasContainer>("tlHeader", 0, 0, 1140, 90);
        headerContainer->SetBackgroundColor(Color(255, 255, 255, 255));
        mainContainer->AddChild(headerContainer);

        auto mainTitle = std::make_shared<UltraCanvasLabel>("tlMainTitle", 30, 15, 1080, 35);
        mainTitle->SetText("Timeline Diagram Infographic");
        mainTitle->SetFontSize(22);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainTitle->SetAlignment(TextAlignment::Left);
        mainTitle->SetTextColor(Color(33, 37, 41, 255));
        headerContainer->AddChild(mainTitle);

        auto subtitleLabel = std::make_shared<UltraCanvasLabel>("tlSubtitle", 30, 52, 1000, 30);
        subtitleLabel->SetText("Narrative timeline with nine design presets, palettes, even/proportional placement and interactive items");
        subtitleLabel->SetFontSize(13);
        subtitleLabel->SetAlignment(TextAlignment::Left);
        subtitleLabel->SetTextColor(Color(108, 117, 125, 255));
        headerContainer->AddChild(subtitleLabel);

        auto contentContainer = std::make_shared<UltraCanvasContainer>("tlContent", 0, 90, 1140, 710);
        mainContainer->AddChild(contentContainer);

        // ===== LEFT SIDEBAR (controls) =====
        auto leftSidebar = std::make_shared<UltraCanvasContainer>("tlLeftSidebar", 20, 20, 200, 670);
        leftSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(leftSidebar);

        const int SIDEBAR_W = 170;

        auto controlsTitle = std::make_shared<UltraCanvasLabel>("tlControlsTitle", PAD, PAD, SIDEBAR_W, 22);
        controlsTitle->SetText("CONTROLS");
        controlsTitle->SetFontSize(11);
        controlsTitle->SetFontWeight(FontWeight::Bold);
        controlsTitle->SetTextColor(Color(73, 80, 87, 255));
        controlsTitle->SetAlignment(TextAlignment::Left);
        leftSidebar->AddChild(controlsTitle);

        auto themeLabel = std::make_shared<UltraCanvasLabel>("tlThemeLabel", PAD, 50, SIDEBAR_W, 18);
        themeLabel->SetText("Theme");
        themeLabel->SetFontSize(10);
        themeLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(themeLabel);

        auto themeSelector = std::make_shared<UltraCanvasDropdown>("tlThemeSelector", PAD, 70, SIDEBAR_W, 28);
        themeSelector->AddItem("Light", "light");
        themeSelector->AddItem("Dark", "dark");
        themeSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(themeSelector);

        auto paletteLabel = std::make_shared<UltraCanvasLabel>("tlPaletteLabel", PAD, 106, SIDEBAR_W, 18);
        paletteLabel->SetText("Palette");
        paletteLabel->SetFontSize(10);
        paletteLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(paletteLabel);

        auto paletteSelector = std::make_shared<UltraCanvasDropdown>("tlPaletteSelector", PAD, 126, SIDEBAR_W, 28);
        paletteSelector->AddItem("Vibrant", "vibrant");
        paletteSelector->AddItem("Corporate Blue", "corporate");
        paletteSelector->AddItem("Pastel", "pastel");
        paletteSelector->AddItem("Ocean", "ocean");
        paletteSelector->AddItem("Sunset", "sunset");
        paletteSelector->AddItem("Forest", "forest");
        paletteSelector->AddItem("Slate", "slate");
        paletteSelector->AddItem("Mono", "mono");
        paletteSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(paletteSelector);

        auto sideLabel = std::make_shared<UltraCanvasLabel>("tlSideLabel", PAD, 162, SIDEBAR_W, 18);
        sideLabel->SetText("Side Policy");
        sideLabel->SetFontSize(10);
        sideLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(sideLabel);

        auto sideSelector = std::make_shared<UltraCanvasDropdown>("tlSideSelector", PAD, 182, SIDEBAR_W, 28);
        sideSelector->AddItem("Alternate", "alternate");
        sideSelector->AddItem("All Above / Left", "above");
        sideSelector->AddItem("All Below / Right", "below");
        sideSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(sideSelector);

        auto placementLabel = std::make_shared<UltraCanvasLabel>("tlPlacementLabel", PAD, 218, SIDEBAR_W, 18);
        placementLabel->SetText("Placement");
        placementLabel->SetFontSize(10);
        placementLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(placementLabel);

        auto placementSelector = std::make_shared<UltraCanvasDropdown>("tlPlacementSelector", PAD, 238, SIDEBAR_W, 28);
        placementSelector->AddItem("Even (infographic)", "even");
        placementSelector->AddItem("Proportional (dates)", "proportional");
        placementSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(placementSelector);

        auto colorLabel = std::make_shared<UltraCanvasLabel>("tlColorLabel", PAD, 274, SIDEBAR_W, 18);
        colorLabel->SetText("Color Mode");
        colorLabel->SetFontSize(10);
        colorLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(colorLabel);

        auto colorSelector = std::make_shared<UltraCanvasDropdown>("tlColorSelector", PAD, 294, SIDEBAR_W, 28);
        colorSelector->AddItem("Per Item", "peritem");
        colorSelector->AddItem("Single", "single");
        colorSelector->AddItem("Gradient Along Path", "gradient");
        colorSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(colorSelector);

        auto btnAddItem = std::make_shared<UltraCanvasButton>("tlBtnAddItem", PAD, 336, SIDEBAR_W, 32);
        btnAddItem->SetText("Add Random Item");
        btnAddItem->SetBackgroundColor(Color(13, 110, 253, 255));
        leftSidebar->AddChild(btnAddItem);

        auto btnRemove = std::make_shared<UltraCanvasButton>("tlBtnRemove", PAD, 373, SIDEBAR_W, 32);
        btnRemove->SetText("Remove Selected");
        btnRemove->SetBackgroundColor(Color(220, 53, 69, 255));
        leftSidebar->AddChild(btnRemove);

        auto btnReverse = std::make_shared<UltraCanvasButton>("tlBtnReverse", PAD, 410, SIDEBAR_W, 32);
        btnReverse->SetText("Reverse Direction");
        btnReverse->SetBackgroundColor(Color(111, 66, 193, 255));
        leftSidebar->AddChild(btnReverse);

        auto btnReload = std::make_shared<UltraCanvasButton>("tlBtnReload", PAD, 447, SIDEBAR_W, 32);
        btnReload->SetText("Reload Sample Data");
        btnReload->SetBackgroundColor(Color(25, 135, 84, 255));
        leftSidebar->AddChild(btnReload);

        auto btnReset = std::make_shared<UltraCanvasButton>("tlBtnReset", PAD, 484, SIDEBAR_W, 32);
        btnReset->SetText("Reset View");
        btnReset->SetBackgroundColor(Color(108, 117, 125, 255));
        leftSidebar->AddChild(btnReset);

        auto helpBox = std::make_shared<UltraCanvasLabel>("tlHelpBox", PAD, 530, SIDEBAR_W, 120);
        helpBox->SetText("INTERACTION TIPS\n\n"
                         "- Click an item to select\n"
                         "- Click empty space to\n"
                         "  clear the selection\n"
                         "- Hover for the full text\n"
                         "- Double-click an item");
        helpBox->SetFontSize(9);
        helpBox->SetAlignment(TextAlignment::Left);
        helpBox->SetBackgroundColor(Color(255, 243, 205, 255));
        helpBox->SetPadding(10);
        leftSidebar->AddChild(helpBox);

        // ===== TABS =====
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("tlTabs", 240, 20, 660, 580);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        contentContainer->AddChild(tabbedContainer);

        // ===== RIGHT SIDEBAR (stats + about) =====
        auto rightSidebar = std::make_shared<UltraCanvasContainer>("tlRightSidebar", 920, 20, 200, 580);
        rightSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(rightSidebar);

        auto statsTitle = std::make_shared<UltraCanvasLabel>("tlStatsTitle", PAD, PAD, SIDEBAR_W, 24);
        statsTitle->SetText("STATISTICS");
        statsTitle->SetFontSize(11);
        statsTitle->SetFontWeight(FontWeight::Bold);
        statsTitle->SetTextColor(Color(73, 80, 87, 255));
        statsTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(statsTitle);

        auto statsPanel = std::make_shared<UltraCanvasLabel>("tlStatsPanel", PAD, 48, SIDEBAR_W, 180);
        statsPanel->SetFontSize(10);
        statsPanel->SetAlignment(TextAlignment::Left);
        statsPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        statsPanel->SetPadding(12);
        rightSidebar->AddChild(statsPanel);

        auto aboutTitle = std::make_shared<UltraCanvasLabel>("tlAboutTitle", PAD, 240, SIDEBAR_W, 24);
        aboutTitle->SetText("ABOUT THIS DESIGN");
        aboutTitle->SetFontSize(11);
        aboutTitle->SetFontWeight(FontWeight::Bold);
        aboutTitle->SetTextColor(Color(73, 80, 87, 255));
        aboutTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(aboutTitle);

        auto aboutPanel = std::make_shared<UltraCanvasLabel>("tlAboutPanel", PAD, 272, SIDEBAR_W, 250);
        aboutPanel->SetFontSize(9);
        aboutPanel->SetAlignment(TextAlignment::Left);
        aboutPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        aboutPanel->SetPadding(12);
        rightSidebar->AddChild(aboutPanel);

        auto statusBar = std::make_shared<UltraCanvasLabel>("tlStatusBar", 240, 610, 660, 60);
        statusBar->SetText("Ready - click items to select them, hover for tooltips");
        statusBar->SetFontSize(11);
        statusBar->SetAlignment(TextAlignment::Left);
        statusBar->SetBackgroundColor(Color(233, 236, 239, 255));
        statusBar->SetPadding(15);
        statusBar->SetTextColor(Color(73, 80, 87, 255));
        contentContainer->AddChild(statusBar);

        // Diagram geometry inside each tab
        const int DIAG_X = 20, DIAG_Y = 15, DIAG_W = 620, DIAG_H = 510;

        struct TabSpec {
            std::string tabTitle;
            TimelineDesign design;
            bool useYearData;     // Company history (6 items) or project year (12)
            std::string about;
        };
        const std::vector<TabSpec> tabSpecs = {
                {"Bar", TimelineDesign::Bar, true,
                 "Bar\n\nA thick spine split into\none colored segment per\nitem, with circular nodes\nalternating on the top and\nbottom edges and the\nperiod caption on the\nopposite side."},
                {"Line", TimelineDesign::Line, true,
                 "Line\n\nA thin axis with small\nnodes on it and a short\nstem to each text block.\nThe most restrained of\nthe horizontal designs."},
                {"Alternating", TimelineDesign::Alternating, true,
                 "Alternating\n\nThe classic zigzag: cards\nabove and below a central\nspine, connected by stems.\nGood when each item has a\nparagraph of text."},
                {"Cards", TimelineDesign::Cards, true,
                 "Cards\n\nA row of separate cards\nwith a numbered badge on\nthe top edge. No spine -\nreads as independent\ntiles rather than a\ncontinuous path."},
                {"Vertical", TimelineDesign::Vertical, true,
                 "Vertical\n\nTop-down spine with cards\nleft and right. The right\nchoice for long lists and\nportrait layouts."},
                {"Serpentine", TimelineDesign::Serpentine, false,
                 "Serpentine\n\nA snaking ribbon that\nwraps over several runs,\nwith numbered nodes on\nthe path. Fits twelve or\nmore items into a wide\nframe."},
                {"Hanging", TimelineDesign::Hanging, false,
                 "Hanging\n\nA month axis with stems\ndropping to bubbles of\nvarying size. Bubble text\nis wrapped to the circle,\nand bubbles are staggered\nover two tiers so they\nnever collide."},
                {"Chevron", TimelineDesign::Chevron, true,
                 "Chevron\n\nArrow blocks pointing\nforward - the process /\nphase presentation where\nthe direction of travel\nmatters more than the\ndates."},
                {"Steps", TimelineDesign::Steps, true,
                 "Steps\n\nAscending staircase\nblocks. Suits growth\nnarratives where each\nstage builds on the\nprevious one."}};

        std::vector<std::shared_ptr<UltraCanvasTimelineDiagram>> allDiagrams;
        for (const auto& spec : tabSpecs) {
            auto diagram = CreateTimelineDiagramElement(
                    "tlDiagram" + std::to_string(allDiagrams.size()),
                    DIAG_X, DIAG_Y, DIAG_W, DIAG_H, spec.design);
            diagram->SetTitle(spec.useYearData ? "Company History" : "Project Year 2026");
            diagram->SetSubtitle(spec.useYearData
                                 ? "Six milestones from founding to one million users"
                                 : "Twelve monthly milestones");
            diagram->SetItems(spec.useYearData
                              ? TimelineDiagramSamples::CompanyHistory()
                              : TimelineDiagramSamples::ProjectYear(2026));
            diagram->SetPalette(TimelinePalette::Vibrant);

            if (spec.design == TimelineDesign::Hanging) {
                // The independent scale row is what makes the month axis read
                // as a calendar rather than a list of items
                diagram->SetScaleLabels({"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"});
                diagram->EditStyle().showScaleRow = true;
                // The scale row already labels every month, so the per-item
                // caption would just repeat it
                diagram->EditStyle().showCaptions = false;
                diagram->StyleChanged();
            }
            if (spec.design == TimelineDesign::Serpentine) {
                diagram->SetColorMode(TimelineColorMode::GradientAlongPath);
            }

            auto tabContent = std::make_shared<UltraCanvasContainer>(
                    diagram->GetIdentifier() + "Tab", 0, 0, 660, 542);
            tabContent->SetBackgroundColor(Color(255, 255, 255, 255));
            tabContent->AddChild(diagram);
            tabbedContainer->AddTab(spec.tabTitle, tabContent);
            allDiagrams.push_back(diagram);
        }

        // Returns the diagram belonging to the currently active tab
        auto activeDiagram = [tabbedContainer, allDiagrams]() -> std::shared_ptr<UltraCanvasTimelineDiagram> {
            int index = tabbedContainer->GetActiveTab();
            if (index < 0 || index >= static_cast<int>(allDiagrams.size())) return nullptr;
            return allDiagrams[index];
        };

        auto updateStats = [activeDiagram, statsPanel]() {
            auto diagram = activeDiagram();
            if (!diagram) return;

            std::ostringstream oss;
            oss << "Items: " << diagram->CountItems() << "\n";
            oss << "Direction: " << (diagram->GetReverseOrder() ? "reversed" : "forward") << "\n";
            oss << "Placement: "
                << (diagram->GetPlacement() == TimelinePlacement::Proportional ? "by date" : "even")
                << "\n\n";

            int selected = diagram->GetSelectedIndex();
            oss << "Selected: ";
            if (selected >= 0) {
                const TimelineItem& item = diagram->GetItem(static_cast<size_t>(selected));
                oss << "#" << (selected + 1) << "\n" << item.caption;
                if (!item.title.empty()) oss << "\n" << item.title;
            } else {
                oss << "none";
            }
            statsPanel->SetText(oss.str());
        };

        std::vector<std::string> aboutTexts;
        for (const auto& spec : tabSpecs) aboutTexts.push_back(spec.about);
        auto updateAbout = [aboutTexts, tabbedContainer, aboutPanel]() {
            int index = tabbedContainer->GetActiveTab();
            if (index >= 0 && index < static_cast<int>(aboutTexts.size())) {
                aboutPanel->SetText(aboutTexts[index]);
            }
        };

        // ===== DIAGRAM CALLBACKS =====
        for (auto& diagram : allDiagrams) {
            diagram->onItemSelect = [statusBar, updateStats](size_t index, const TimelineItem& item) {
                statusBar->SetText("Selected #" + std::to_string(index + 1) + " [" +
                                   item.caption + "] " + item.title);
                updateStats();
            };
            diagram->onItemHover = [statusBar](size_t index, const TimelineItem& item) {
                statusBar->SetText("Hovering #" + std::to_string(index + 1) + " [" +
                                   item.caption + "] " + item.title);
            };
            diagram->onItemDoubleClick = [statusBar](size_t index, const TimelineItem& item) {
                statusBar->SetText("Double-clicked #" + std::to_string(index + 1) + " " + item.body);
            };
            diagram->onSelectionChange = updateStats;
        }

        // ===== CONTROL CALLBACKS =====
        tabbedContainer->onTabChange = [updateStats, updateAbout](int, int) {
            updateStats();
            updateAbout();
        };

        themeSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            bool dark = (item.value == "dark");
            for (auto& diagram : allDiagrams) diagram->SetDarkTheme(dark);
        };

        paletteSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            TimelinePalette palette = TimelinePalette::Vibrant;
            if (item.value == "corporate") palette = TimelinePalette::CorporateBlue;
            else if (item.value == "pastel") palette = TimelinePalette::Pastel;
            else if (item.value == "ocean") palette = TimelinePalette::Ocean;
            else if (item.value == "sunset") palette = TimelinePalette::Sunset;
            else if (item.value == "forest") palette = TimelinePalette::Forest;
            else if (item.value == "slate") palette = TimelinePalette::Slate;
            else if (item.value == "mono") palette = TimelinePalette::Mono;
            for (auto& diagram : allDiagrams) diagram->SetPalette(palette);
        };

        sideSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            TimelineSidePolicy policy = TimelineSidePolicy::Alternate;
            if (item.value == "above") policy = TimelineSidePolicy::AllAbove;
            else if (item.value == "below") policy = TimelineSidePolicy::AllBelow;
            for (auto& diagram : allDiagrams) diagram->SetSidePolicy(policy);
        };

        placementSelector->onSelectionChanged =
                [allDiagrams, updateStats](int, const DropdownItem& item) {
            TimelinePlacement mode = (item.value == "proportional")
                                     ? TimelinePlacement::Proportional
                                     : TimelinePlacement::Even;
            for (auto& diagram : allDiagrams) diagram->SetPlacement(mode);
            updateStats();
        };

        colorSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            TimelineColorMode mode = TimelineColorMode::PerItem;
            if (item.value == "single") mode = TimelineColorMode::Single;
            else if (item.value == "gradient") mode = TimelineColorMode::GradientAlongPath;
            for (auto& diagram : allDiagrams) diagram->SetColorMode(mode);
        };

        btnAddItem->onClick = [activeDiagram, statusBar, updateStats]() {
            auto diagram = activeDiagram();
            if (!diagram) return;

            TimelineItem item = RandomTimelineItem(static_cast<int>(diagram->CountItems()) - 5);
            diagram->AddItem(item);
            statusBar->SetText("Added [" + item.caption + "] " + item.title);
            updateStats();
        };

        btnRemove->onClick = [activeDiagram, statusBar, updateStats]() {
            auto diagram = activeDiagram();
            if (!diagram) return;

            int selected = diagram->GetSelectedIndex();
            if (selected < 0) {
                statusBar->SetText("Nothing selected - click an item first");
                return;
            }
            diagram->RemoveItem(static_cast<size_t>(selected));
            statusBar->SetText("Removed the selected item");
            updateStats();
        };

        btnReverse->onClick = [activeDiagram, statusBar, updateStats]() {
            auto diagram = activeDiagram();
            if (!diagram) return;
            diagram->SetReverseOrder(!diagram->GetReverseOrder());
            statusBar->SetText(diagram->GetReverseOrder()
                               ? "Sequence reversed - the timeline now runs backwards"
                               : "Sequence restored to forward order");
            updateStats();
        };

        btnReload->onClick = [activeDiagram, statusBar, updateStats]() {
            auto diagram = activeDiagram();
            if (!diagram) return;
            diagram->LoadSampleData();
            statusBar->SetText("Sample data reloaded for the active diagram");
            updateStats();
        };

        btnReset->onClick = [themeSelector, paletteSelector, sideSelector, placementSelector,
                             colorSelector, allDiagrams, tabSpecs, statusBar, updateStats]() {
            themeSelector->SetSelectedIndex(0);
            paletteSelector->SetSelectedIndex(0);
            sideSelector->SetSelectedIndex(0);
            placementSelector->SetSelectedIndex(0);
            colorSelector->SetSelectedIndex(0);

            for (size_t i = 0; i < allDiagrams.size(); ++i) {
                auto& diagram = allDiagrams[i];
                diagram->SetDarkTheme(false);
                diagram->SetPalette(TimelinePalette::Vibrant);
                diagram->SetSidePolicy(TimelineSidePolicy::Alternate);
                diagram->SetPlacement(TimelinePlacement::Even);
                diagram->SetColorMode(i < tabSpecs.size() &&
                                      tabSpecs[i].design == TimelineDesign::Serpentine
                                      ? TimelineColorMode::GradientAlongPath
                                      : TimelineColorMode::PerItem);
                diagram->SetReverseOrder(false);
                diagram->ClearSelection();
                diagram->SetItems(i < tabSpecs.size() && tabSpecs[i].useYearData
                                  ? TimelineDiagramSamples::CompanyHistory()
                                  : TimelineDiagramSamples::ProjectYear(2026));
            }

            statusBar->SetText("View reset - all diagrams restored to their defaults");
            updateStats();
        };

        updateStats();
        updateAbout();
        return mainContainer;
    }

} // namespace UltraCanvas
