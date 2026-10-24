// Apps/DemoApp/UltraCanvasTimelineDiagramExamples.cpp
// Timeline diagram examples: one tab per design preset, with live controls
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): controls,
//                  diagram and statistics in cards that wrap, every text sized
//                  to fit, the about texts wrap instead of carrying line breaks
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
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

    namespace {
        // A button in the controls card: stretched across it, and as wide as
        // its text when that is wider.
        std::shared_ptr<UltraCanvasButton> AddTimelineDiagramButton(const std::shared_ptr<UltraCanvasContainer>& card,
                                                                    const std::string& id, const std::string& text,
                                                                    const Color& background) {
            auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 32, text);
            button->SetBackgroundColor(background);
            DemoKit::Add(card, button);
            return button;
        }

        // A caption over a dropdown, the dropdown stretched across the card.
        std::shared_ptr<UltraCanvasDropdown> AddTimelineDiagramSelector(const std::shared_ptr<UltraCanvasContainer>& card,
                                                                        const std::string& id, const std::string& labelId,
                                                                        const std::string& label) {
            auto caption = DemoKit::AddCaption(card, labelId, label);
            caption->SetFontWeight(FontWeight::Bold);
            auto selector = std::make_shared<UltraCanvasDropdown>(id, 0, 0, 170, 28);
            DemoKit::AddStretched(card, selector);
            return selector;
        }

        // A side card keeps its width (the sidebar's 200 px) and lets the
        // diagram card take the rest of the row.
        std::shared_ptr<UltraCanvasContainer> AddTimelineDiagramSideCard(const std::shared_ptr<UltraCanvasContainer>& row,
                                                                         const std::string& id, const std::string& titleId,
                                                                         const std::string& title) {
            auto card = DemoKit::AddCard(row, id, "", "", 200);
            card->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            DemoKit::AddHeading(card, titleId, title);
            return card;
        }
    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTimelineDiagramExamples() {
        auto mainContainer = DemoKit::CreatePage("tlMainContainer", "Timeline Diagram Infographic",
                "Narrative timeline with nine design presets, palettes, even/proportional placement and interactive items");

        // Controls | tabs with the status line | statistics and about. The
        // side cards wrap under the diagram in a narrow window.
        auto contentContainer = DemoKit::AddRow(mainContainer, "tlContent");

        // ===== LEFT SIDEBAR (controls) =====
        auto leftSidebar = AddTimelineDiagramSideCard(contentContainer, "tlLeftSidebar", "tlControlsTitle", "CONTROLS");

        auto themeSelector = AddTimelineDiagramSelector(leftSidebar, "tlThemeSelector", "tlThemeLabel", "Theme");
        themeSelector->AddItem("Light", "light");
        themeSelector->AddItem("Dark", "dark");
        themeSelector->SetSelectedIndex(0);

        auto paletteSelector = AddTimelineDiagramSelector(leftSidebar, "tlPaletteSelector", "tlPaletteLabel", "Palette");
        paletteSelector->AddItem("Vibrant", "vibrant");
        paletteSelector->AddItem("Corporate Blue", "corporate");
        paletteSelector->AddItem("Pastel", "pastel");
        paletteSelector->AddItem("Ocean", "ocean");
        paletteSelector->AddItem("Sunset", "sunset");
        paletteSelector->AddItem("Forest", "forest");
        paletteSelector->AddItem("Slate", "slate");
        paletteSelector->AddItem("Mono", "mono");
        paletteSelector->SetSelectedIndex(0);

        auto sideSelector = AddTimelineDiagramSelector(leftSidebar, "tlSideSelector", "tlSideLabel", "Side Policy");
        sideSelector->AddItem("Alternate", "alternate");
        sideSelector->AddItem("All Above / Left", "above");
        sideSelector->AddItem("All Below / Right", "below");
        sideSelector->SetSelectedIndex(0);

        auto placementSelector = AddTimelineDiagramSelector(leftSidebar, "tlPlacementSelector", "tlPlacementLabel", "Placement");
        placementSelector->AddItem("Even (infographic)", "even");
        placementSelector->AddItem("Proportional (dates)", "proportional");
        placementSelector->SetSelectedIndex(0);

        auto colorSelector = AddTimelineDiagramSelector(leftSidebar, "tlColorSelector", "tlColorLabel", "Color Mode");
        colorSelector->AddItem("Per Item", "peritem");
        colorSelector->AddItem("Single", "single");
        colorSelector->AddItem("Gradient Along Path", "gradient");
        colorSelector->SetSelectedIndex(0);

        auto btnAddItem = AddTimelineDiagramButton(leftSidebar, "tlBtnAddItem", "Add Random Item", Color(13, 110, 253, 255));
        auto btnRemove = AddTimelineDiagramButton(leftSidebar, "tlBtnRemove", "Remove Selected", Color(220, 53, 69, 255));
        auto btnReverse = AddTimelineDiagramButton(leftSidebar, "tlBtnReverse", "Reverse Direction", Color(111, 66, 193, 255));
        auto btnReload = AddTimelineDiagramButton(leftSidebar, "tlBtnReload", "Reload Sample Data", Color(25, 135, 84, 255));
        auto btnReset = AddTimelineDiagramButton(leftSidebar, "tlBtnReset", "Reset View", Color(108, 117, 125, 255));

        auto helpBox = DemoKit::AddCaption(leftSidebar, "tlHelpBox",
                "INTERACTION TIPS\n\n"
                "- Click an item to select\n"
                "- Click empty space to clear the selection\n"
                "- Hover for the full text\n"
                "- Double-click an item");
        helpBox->SetTextColor(DemoKit::BodyText());
        helpBox->SetBackgroundColor(Color(255, 243, 205, 255));
        helpBox->SetPadding(10);

        // ===== TABS =====
        auto diagramCard = DemoKit::AddCard(contentContainer, "tlDiagramCard", "", "", 690);
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("tlTabs", 0, 0, 660, 580);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        DemoKit::AddFixed(diagramCard, tabbedContainer);

        auto statusBar = DemoKit::AddText(diagramCard, "tlStatusBar",
                "Ready - click items to select them, hover for tooltips");
        statusBar->SetFontSize(11);
        statusBar->SetBackgroundColor(Color(233, 236, 239, 255));
        statusBar->SetPadding(15);
        statusBar->SetTextColor(Color(73, 80, 87, 255));

        // ===== RIGHT SIDEBAR (stats + about) =====
        auto rightSidebar = AddTimelineDiagramSideCard(contentContainer, "tlRightSidebar", "tlStatsTitle", "STATISTICS");

        auto statsPanel = DemoKit::AddText(rightSidebar, "tlStatsPanel", "");
        statsPanel->SetFontSize(10);
        statsPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        statsPanel->SetPadding(12);

        DemoKit::AddHeading(rightSidebar, "tlAboutTitle", "ABOUT THIS DESIGN");

        auto aboutPanel = DemoKit::AddText(rightSidebar, "tlAboutPanel", "");
        aboutPanel->SetFontSize(9);
        aboutPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        aboutPanel->SetPadding(12);

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
                 "Bar\n\nA thick spine split into one colored segment per item, with circular nodes alternating on the top and bottom edges and the period caption on the opposite side."},
                {"Line", TimelineDesign::Line, true,
                 "Line\n\nA thin axis with small nodes on it and a short stem to each text block. The most restrained of the horizontal designs."},
                {"Alternating", TimelineDesign::Alternating, true,
                 "Alternating\n\nThe classic zigzag: cards above and below a central spine, connected by stems. Good when each item has a paragraph of text."},
                {"Cards", TimelineDesign::Cards, true,
                 "Cards\n\nA row of separate cards with a numbered badge on the top edge. No spine - reads as independent tiles rather than a continuous path."},
                {"Vertical", TimelineDesign::Vertical, true,
                 "Vertical\n\nTop-down spine with cards left and right. The right choice for long lists and portrait layouts."},
                {"Serpentine", TimelineDesign::Serpentine, false,
                 "Serpentine\n\nA snaking ribbon that wraps over several runs, with numbered nodes on the path. Fits twelve or more items into a wide frame."},
                {"Hanging", TimelineDesign::Hanging, false,
                 "Hanging\n\nA month axis with stems dropping to bubbles of varying size. Bubble text is wrapped to the circle, and bubbles are staggered over two tiers so they never collide."},
                {"Chevron", TimelineDesign::Chevron, true,
                 "Chevron\n\nArrow blocks pointing forward - the process / phase presentation where the direction of travel matters more than the dates."},
                {"Steps", TimelineDesign::Steps, true,
                 "Steps\n\nAscending staircase blocks. Suits growth narratives where each stage builds on the previous one."}};

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
