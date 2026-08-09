// Apps/DemoApp/UltraCanvasFishboneDiagramExamples.cpp
// Fishbone (Ishikawa) diagram examples: eight design presets with live controls
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasFishboneDiagram.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasSwitch.h"
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace UltraCanvas {

    // Short phrases used by the "Add Random Cause" button
    static std::string RandomCauseText() {
        static const char* phrases[] = {
                "Undocumented procedure", "Ambiguous ownership", "Missing checklist step",
                "Ageing equipment", "Unclear acceptance criteria", "Handover gaps between shifts",
                "No preventive schedule", "Supplier substitution", "Insufficient sampling",
                "Manual data entry"};
        return phrases[rand() % 10];
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateFishboneDiagramExamples() {
        // Children placed directly inside a sidebar/panel container are inset
        // explicitly instead of using SetPadding() - the same pattern the
        // SWOT/Quadrant/Venn demos use - so absolutely-positioned children are
        // not clipped by the container's content area.
        const int PAD = 15;
        const int SIDEBAR_W = 170;

        auto mainContainer = std::make_shared<UltraCanvasContainer>("fishboneMain", 0, 0, 1140, 800);
        mainContainer->SetBackgroundColor(Color(248, 249, 250, 255));

        auto headerContainer = std::make_shared<UltraCanvasContainer>("fishboneHeader", 0, 0, 1140, 90);
        headerContainer->SetBackgroundColor(Color(255, 255, 255, 255));
        mainContainer->AddChild(headerContainer);

        auto mainTitle = std::make_shared<UltraCanvasLabel>("fishboneTitle", 30, 15, 1080, 35);
        mainTitle->SetText("Fishbone (Ishikawa) Diagram");
        mainTitle->SetFontSize(22);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainTitle->SetAlignment(TextAlignment::Left);
        mainTitle->SetTextColor(Color(33, 37, 41, 255));
        headerContainer->AddChild(mainTitle);

        auto subtitle = std::make_shared<UltraCanvasLabel>("fishboneSubtitle", 30, 52, 1000, 30);
        subtitle->SetText("Cause-and-effect root cause analysis with eight design presets, "
                          "category checklists (6M / 8P / 4S) and Mermaid ishikawa-beta export");
        subtitle->SetFontSize(13);
        subtitle->SetAlignment(TextAlignment::Left);
        subtitle->SetTextColor(Color(108, 117, 125, 255));
        headerContainer->AddChild(subtitle);

        auto contentContainer = std::make_shared<UltraCanvasContainer>("fishboneContent", 0, 90, 1140, 710);
        mainContainer->AddChild(contentContainer);

        // ===== LEFT SIDEBAR (controls) =====
        auto leftSidebar = std::make_shared<UltraCanvasContainer>("fishboneLeft", 20, 20, 200, 670);
        leftSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(leftSidebar);

        auto controlsTitle = std::make_shared<UltraCanvasLabel>("fishboneControlsTitle", PAD, PAD, SIDEBAR_W, 22);
        controlsTitle->SetText("CONTROLS");
        controlsTitle->SetFontSize(11);
        controlsTitle->SetFontWeight(FontWeight::Bold);
        controlsTitle->SetTextColor(Color(73, 80, 87, 255));
        controlsTitle->SetAlignment(TextAlignment::Left);
        leftSidebar->AddChild(controlsTitle);

        // Builds a "label + dropdown" pair at y and returns the dropdown
        auto addDropdown = [&](const std::string& id, const std::string& caption, int y) {
            auto label = std::make_shared<UltraCanvasLabel>(id + "Label", PAD, y, SIDEBAR_W, 18);
            label->SetText(caption);
            label->SetFontSize(10);
            label->SetFontWeight(FontWeight::Bold);
            label->SetAlignment(TextAlignment::Left);
            leftSidebar->AddChild(label);

            auto dropdown = std::make_shared<UltraCanvasDropdown>(id, PAD, y + 19, SIDEBAR_W, 26);
            leftSidebar->AddChild(dropdown);
            return dropdown;
        };

        auto themeSelector = addDropdown("fishboneTheme", "Theme", 44);
        themeSelector->AddItem("Light", "light");
        themeSelector->AddItem("Dark", "dark");
        themeSelector->SetSelectedIndex(0);

        auto paletteSelector = addDropdown("fishbonePalette", "Palette", 96);
        paletteSelector->AddItem("Vibrant", "vibrant");
        paletteSelector->AddItem("Corporate Blue", "corporate");
        paletteSelector->AddItem("Pastel", "pastel");
        paletteSelector->AddItem("Ocean", "ocean");
        paletteSelector->AddItem("Sunset", "sunset");
        paletteSelector->AddItem("Forest", "forest");
        paletteSelector->AddItem("Slate", "slate");
        paletteSelector->AddItem("Mono", "mono");
        paletteSelector->SetSelectedIndex(0);

        auto sideSelector = addDropdown("fishboneSides", "Rib Sides", 148);
        sideSelector->AddItem("Alternate", "alternate");
        sideSelector->AddItem("All Above", "above");
        sideSelector->AddItem("All Below", "below");
        sideSelector->AddItem("Per Category", "per");
        sideSelector->SetSelectedIndex(0);

        auto markerSelector = addDropdown("fishboneMarker", "Cause Marker", 200);
        markerSelector->AddItem("Dot", "dot");
        markerSelector->AddItem("Bullet", "bullet");
        markerSelector->AddItem("Numbered", "numbered");
        markerSelector->AddItem("Hidden", "hidden");
        markerSelector->SetSelectedIndex(0);

        auto headSelector = addDropdown("fishboneHead", "Head Shape", 252);
        headSelector->AddItem("Triangle", "triangle");
        headSelector->AddItem("Arrow", "arrow");
        headSelector->AddItem("Fish Head", "fish");
        headSelector->AddItem("Effect Box", "box");
        headSelector->AddItem("Hidden", "hidden");
        headSelector->SetSelectedIndex(0);

        auto presetSelector = addDropdown("fishbonePreset", "Category Checklist", 304);
        presetSelector->AddItem("(keep current)", "keep");
        presetSelector->AddItem("6M - manufacturing", "6m");
        presetSelector->AddItem("8P - marketing", "8p");
        presetSelector->AddItem("4S - service", "4s");
        presetSelector->AddItem("PEMPEM", "pempem");
        presetSelector->AddItem("Software delivery", "software");
        presetSelector->SetSelectedIndex(0);

        auto swIcons = UltraCanvasSwitch::Create("fishboneSwIcons", PAD, 360, "Icons", true);
        leftSidebar->AddChild(swIcons);

        auto swSubCauses = UltraCanvasSwitch::Create("fishboneSwSubs", PAD, 390, "Sub-causes", true);
        leftSidebar->AddChild(swSubCauses);

        auto swWeights = UltraCanvasSwitch::Create("fishboneSwWeights", PAD, 420, "Weights", false);
        leftSidebar->AddChild(swWeights);

        auto swCaption = UltraCanvasSwitch::Create("fishboneSwCaption", PAD, 450, "Spine caption", false);
        leftSidebar->AddChild(swCaption);

        auto btnAddCause = std::make_shared<UltraCanvasButton>("fishboneBtnAdd", PAD, 486, SIDEBAR_W, 30);
        btnAddCause->SetText("Add Random Cause");
        btnAddCause->SetBackgroundColor(Color(13, 110, 253, 255));
        leftSidebar->AddChild(btnAddCause);

        auto btnRemove = std::make_shared<UltraCanvasButton>("fishboneBtnRemove", PAD, 521, SIDEBAR_W, 30);
        btnRemove->SetText("Remove Selected");
        btnRemove->SetBackgroundColor(Color(220, 53, 69, 255));
        leftSidebar->AddChild(btnRemove);

        auto btnMermaid = std::make_shared<UltraCanvasButton>("fishboneBtnMermaid", PAD, 556, SIDEBAR_W, 30);
        btnMermaid->SetText("Show Mermaid Export");
        btnMermaid->SetBackgroundColor(Color(25, 135, 84, 255));
        leftSidebar->AddChild(btnMermaid);

        auto btnReset = std::make_shared<UltraCanvasButton>("fishboneBtnReset", PAD, 591, SIDEBAR_W, 30);
        btnReset->SetText("Reset View");
        btnReset->SetBackgroundColor(Color(108, 117, 125, 255));
        leftSidebar->AddChild(btnReset);

        // ===== TABS =====
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("fishboneTabs", 240, 20, 660, 580);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        contentContainer->AddChild(tabbedContainer);

        // ===== RIGHT SIDEBAR =====
        auto rightSidebar = std::make_shared<UltraCanvasContainer>("fishboneRight", 920, 20, 200, 580);
        rightSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(rightSidebar);

        auto statsTitle = std::make_shared<UltraCanvasLabel>("fishboneStatsTitle", PAD, PAD, SIDEBAR_W, 22);
        statsTitle->SetText("STATISTICS");
        statsTitle->SetFontSize(11);
        statsTitle->SetFontWeight(FontWeight::Bold);
        statsTitle->SetTextColor(Color(73, 80, 87, 255));
        statsTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(statsTitle);

        auto statsPanel = std::make_shared<UltraCanvasLabel>("fishboneStats", PAD, 44, SIDEBAR_W, 150);
        statsPanel->SetFontSize(10);
        statsPanel->SetAlignment(TextAlignment::Left);
        statsPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        statsPanel->SetPadding(10);
        rightSidebar->AddChild(statsPanel);

        auto aboutTitle = std::make_shared<UltraCanvasLabel>("fishboneAboutTitle", PAD, 206, SIDEBAR_W, 22);
        aboutTitle->SetText("ABOUT THIS DESIGN");
        aboutTitle->SetFontSize(11);
        aboutTitle->SetFontWeight(FontWeight::Bold);
        aboutTitle->SetTextColor(Color(73, 80, 87, 255));
        aboutTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(aboutTitle);

        auto aboutPanel = std::make_shared<UltraCanvasLabel>("fishboneAbout", PAD, 234, SIDEBAR_W, 220);
        aboutPanel->SetFontSize(9);
        aboutPanel->SetAlignment(TextAlignment::Left);
        aboutPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        aboutPanel->SetPadding(10);
        rightSidebar->AddChild(aboutPanel);

        auto tipsPanel = std::make_shared<UltraCanvasLabel>("fishboneTips", PAD, 466, SIDEBAR_W, 98);
        tipsPanel->SetText("INTERACTION TIPS\n\n"
                           "- Click a cause to select\n"
                           "- Click a category chip\n"
                           "- Double-click a chip to\n"
                           "  collapse its causes\n"
                           "- Hover for tooltips");
        tipsPanel->SetFontSize(9);
        tipsPanel->SetAlignment(TextAlignment::Left);
        tipsPanel->SetBackgroundColor(Color(255, 243, 205, 255));
        tipsPanel->SetPadding(10);
        rightSidebar->AddChild(tipsPanel);

        auto statusBar = std::make_shared<UltraCanvasLabel>("fishboneStatus", 240, 610, 660, 60);
        statusBar->SetText("Ready - click a cause to select it, hover for tooltips");
        statusBar->SetFontSize(11);
        statusBar->SetAlignment(TextAlignment::Left);
        statusBar->SetBackgroundColor(Color(233, 236, 239, 255));
        statusBar->SetPadding(15);
        statusBar->SetTextColor(Color(73, 80, 87, 255));
        contentContainer->AddChild(statusBar);

        // ===== TABS: ONE PER DESIGN =====
        const int DIAG_X = 15, DIAG_Y = 10, DIAG_W = 630, DIAG_H = 520;

        struct TabSpec {
            std::string tabTitle;
            FishboneDesign design;
            int sample;          // 0 = quality control, 1 = churn, 2 = software
            std::string about;
        };
        const std::vector<TabSpec> tabSpecs = {
                {"Spine Chips", FishboneDesign::SpineChips, 0,
                 "Spine Chips\n\nA solid tapered spine\nwith category pills\nsitting on it, each\nending in a circular\nicon badge. Causes hang\noff short ribs and end\nin a colored dot."},
                {"Classic", FishboneDesign::Classic, 0,
                 "Classic\n\nThe textbook herringbone:\na thin spine, straight\nangled ribs and category\nboxes at the rib tips.\n\nThe form QA and\nengineering expect."},
                {"Bracket", FishboneDesign::Bracket, 2,
                 "Bracket\n\nParallelogram bones - a\nslanted edge plus a\nhorizontal shelf. Causes\nattach by horizontal\nleader lines, so the\ntext stays level however\nsteep the rib is."},
                {"Crossed Ribs", FishboneDesign::CrossedRibs, 0,
                 "Crossed Ribs\n\nOne stroke crosses the\nspine and serves two\ncategories at once - one\nabove, one below.\n\nCause text runs toward\nthe head."},
                {"Chevron Spine", FishboneDesign::ChevronSpine, 2,
                 "Chevron Spine\n\nThe spine becomes a chain\nof arrow blocks, one per\ncategory, each with its\nown pictogram. Hairline\nleaders connect them to\nfloating pills."},
                {"Columns", FishboneDesign::Columns, 1,
                 "Columns\n\nNo ribs at all: each\ncategory is a tinted\npanel straddling the\nspine, with an icon\nbadge at the far end and\nits causes listed\ninside."},
                {"Vertical", FishboneDesign::Vertical, 1,
                 "Vertical\n\nThe spine runs top to\nbottom with ribs left\nand right - the portrait\nform for tall frames\nand slides."},
                {"Compact", FishboneDesign::Compact, 0,
                 "Compact\n\nEvery category on one\nside of a low spine.\nHalves the height at the\ncost of length - suits\nwide dashboard tiles."}};

        std::vector<std::shared_ptr<UltraCanvasFishboneDiagram>> allDiagrams;
        for (size_t i = 0; i < tabSpecs.size(); ++i) {
            const TabSpec& spec = tabSpecs[i];

            auto diagram = CreateFishboneDiagramElement(
                    "fishboneDiagram" + std::to_string(i), DIAG_X, DIAG_Y, DIAG_W, DIAG_H,
                    spec.design);

            switch (spec.sample) {
                case 1:  diagram->SetDocument(FishboneSamples::CustomerChurn()); break;
                case 2:  diagram->SetDocument(FishboneSamples::SoftwareDelivery()); break;
                default: diagram->SetDocument(FishboneSamples::QualityControl()); break;
            }

            // Design-specific touches that show the options off
            if (spec.design == FishboneDesign::Bracket) {
                diagram->SetShowSpineCaption(true);
                diagram->SetSpineCaption("Potential Causes");
            }
            if (spec.design == FishboneDesign::Columns) {
                diagram->SetTailShape(FishboneTailShape::FishTail);
                diagram->SetShowWaypoints(true);
            }
            if (spec.design == FishboneDesign::Classic) {
                diagram->SetEffectPlacement(FishboneEffectPlacement::HeadBox);
                diagram->SetHeadShape(FishboneHeadShape::Box);
            }
            if (spec.design == FishboneDesign::ChevronSpine) {
                diagram->SetHeadShape(FishboneHeadShape::FishHead);
            }

            auto tabContent = std::make_shared<UltraCanvasContainer>(
                    diagram->GetIdentifier() + "Tab", 0, 0, 660, 542);
            tabContent->SetBackgroundColor(Color(255, 255, 255, 255));
            tabContent->AddChild(diagram);
            tabbedContainer->AddTab(spec.tabTitle, tabContent);
            allDiagrams.push_back(diagram);
        }

        // Returns the diagram belonging to the currently active tab
        auto activeDiagram = [tabbedContainer, allDiagrams]() -> std::shared_ptr<UltraCanvasFishboneDiagram> {
            int index = tabbedContainer->GetActiveTab();
            if (index < 0 || index >= static_cast<int>(allDiagrams.size())) return nullptr;
            return allDiagrams[index];
        };

        auto updateStats = [activeDiagram, statsPanel]() {
            auto diagram = activeDiagram();
            if (!diagram) return;

            std::ostringstream oss;
            oss << "Effect: " << diagram->GetEffect() << "\n\n"
                << "Categories: " << diagram->CountCategories() << "\n"
                << "Causes: " << diagram->CountAllCauses(false) << "\n"
                << "With sub-causes: " << diagram->CountAllCauses(true) << "\n"
                << "Depth: " << FishboneDepth(diagram->GetDocument()) << "\n\nSelected: ";

            auto selection = diagram->GetSelectedItem();
            if (!selection.IsValid()) {
                oss << "none";
            } else if (selection.IsCategory()) {
                oss << "category "
                    << diagram->GetCategories()[selection.category].title;
            } else {
                const FishboneCause* cause = diagram->FindCause(selection);
                oss << (cause ? cause->text : std::string("cause"));
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
            diagram->onCauseSelect = [statusBar, updateStats](const FishboneRef& ref,
                                                              const FishboneCause& cause) {
                statusBar->SetText(std::string(ref.IsSubCause() ? "Selected sub-cause: "
                                                                : "Selected cause: ") + cause.text);
                updateStats();
            };
            diagram->onCategorySelect = [statusBar, updateStats](size_t, const FishboneCategory& category) {
                statusBar->SetText("Selected category: " + category.title + " (" +
                                   std::to_string(category.causes.size()) + " causes) - "
                                   "double-click the chip to collapse it");
                updateStats();
            };
            diagram->onCauseHover = [statusBar](const FishboneRef&, const FishboneCause& cause) {
                statusBar->SetText("Hovering: " + cause.text);
            };
            diagram->onCauseDoubleClick = [statusBar](const FishboneRef&, const FishboneCause& cause) {
                statusBar->SetText("Double-clicked: " + cause.text);
            };
            diagram->onSelectionChange = updateStats;
        }

        // ===== CONTROL CALLBACKS =====
        tabbedContainer->onTabChange = [updateStats, updateAbout](int, int) {
            updateStats();
            updateAbout();
        };

        themeSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            const bool dark = (item.value == "dark");
            for (auto& diagram : allDiagrams) diagram->SetDarkTheme(dark);
        };

        paletteSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            FishbonePalette palette = FishbonePalette::Vibrant;
            if (item.value == "corporate") palette = FishbonePalette::CorporateBlue;
            else if (item.value == "pastel") palette = FishbonePalette::Pastel;
            else if (item.value == "ocean")  palette = FishbonePalette::Ocean;
            else if (item.value == "sunset") palette = FishbonePalette::Sunset;
            else if (item.value == "forest") palette = FishbonePalette::Forest;
            else if (item.value == "slate")  palette = FishbonePalette::Slate;
            else if (item.value == "mono")   palette = FishbonePalette::Mono;
            for (auto& diagram : allDiagrams) diagram->SetPalette(palette);
        };

        sideSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            FishboneSidePolicy policy = FishboneSidePolicy::Alternate;
            if (item.value == "above") policy = FishboneSidePolicy::AllAbove;
            else if (item.value == "below") policy = FishboneSidePolicy::AllBelow;
            else if (item.value == "per") policy = FishboneSidePolicy::PerCategory;
            for (auto& diagram : allDiagrams) diagram->SetSidePolicy(policy);
        };

        markerSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            FishboneCauseMarker marker = FishboneCauseMarker::Dot;
            if (item.value == "bullet") marker = FishboneCauseMarker::Bullet;
            else if (item.value == "numbered") marker = FishboneCauseMarker::Numbered;
            else if (item.value == "hidden") marker = FishboneCauseMarker::Hidden;
            for (auto& diagram : allDiagrams) diagram->SetCauseMarker(marker);
        };

        headSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            FishboneHeadShape shape = FishboneHeadShape::Triangle;
            FishboneEffectPlacement placement = FishboneEffectPlacement::Title;
            if (item.value == "arrow") shape = FishboneHeadShape::Arrow;
            else if (item.value == "fish") shape = FishboneHeadShape::FishHead;
            else if (item.value == "hidden") shape = FishboneHeadShape::Hidden;
            else if (item.value == "box") {
                shape = FishboneHeadShape::Box;
                placement = FishboneEffectPlacement::HeadBox;
            }
            for (auto& diagram : allDiagrams) {
                diagram->SetHeadShape(shape);
                diagram->SetEffectPlacement(placement);
            }
        };

        presetSelector->onSelectionChanged =
                [activeDiagram, statusBar, updateStats](int, const DropdownItem& item) {
            auto diagram = activeDiagram();
            if (!diagram || item.value == "keep") return;

            FishboneCategoryPreset preset = FishboneCategoryPreset::SixM;
            if (item.value == "8p") preset = FishboneCategoryPreset::EightP;
            else if (item.value == "4s") preset = FishboneCategoryPreset::FourS;
            else if (item.value == "pempem") preset = FishboneCategoryPreset::PEMPEM;
            else if (item.value == "software") preset = FishboneCategoryPreset::Software;

            diagram->LoadCategoryPreset(preset);
            statusBar->SetText(std::string("Loaded the ") + FishboneCategoryPresetName(preset) +
                               " checklist - the categories are empty, add causes to fill them");
            updateStats();
        };

        swIcons->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            const bool on = (newState == CheckedState::Checked);
            for (auto& diagram : allDiagrams) diagram->SetShowCategoryIcons(on);
        };

        swSubCauses->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            const bool on = (newState == CheckedState::Checked);
            for (auto& diagram : allDiagrams) diagram->SetShowSubCauses(on);
        };

        swWeights->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            const bool on = (newState == CheckedState::Checked);
            for (auto& diagram : allDiagrams) diagram->SetShowWeights(on);
        };

        swCaption->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            const bool on = (newState == CheckedState::Checked);
            for (auto& diagram : allDiagrams) diagram->SetShowSpineCaption(on);
        };

        // Adds a cause to the category that currently has the fewest
        btnAddCause->onClick = [activeDiagram, statusBar, updateStats]() {
            auto diagram = activeDiagram();
            if (!diagram || diagram->CountCategories() == 0) return;

            size_t target = 0;
            size_t fewest = diagram->CountCauses(0);
            for (size_t i = 1; i < diagram->CountCategories(); ++i) {
                if (diagram->CountCauses(i) < fewest) {
                    fewest = diagram->CountCauses(i);
                    target = i;
                }
            }

            const std::string text = RandomCauseText();
            diagram->AddCause(target, text);
            statusBar->SetText("Added \"" + text + "\" to " +
                               diagram->GetCategories()[target].title);
            updateStats();
        };

        btnRemove->onClick = [activeDiagram, statusBar, updateStats]() {
            auto diagram = activeDiagram();
            if (!diagram) return;

            auto selection = diagram->GetSelectedItem();
            if (!selection.IsValid()) {
                statusBar->SetText("Nothing selected - click a cause or a category chip first");
                return;
            }
            if (selection.IsCategory()) {
                diagram->RemoveCategory(static_cast<size_t>(selection.category));
                statusBar->SetText("Removed the selected category");
            } else if (selection.IsCause()) {
                diagram->RemoveCause(static_cast<size_t>(selection.category),
                                     static_cast<size_t>(selection.cause));
                statusBar->SetText("Removed the selected cause");
            } else {
                statusBar->SetText("Sub-causes are removed with their parent cause");
                return;
            }
            diagram->ClearSelection();
            updateStats();
        };

        btnMermaid->onClick = [activeDiagram, aboutPanel, aboutTitle, statusBar]() {
            auto diagram = activeDiagram();
            if (!diagram) return;

            // Show the first lines of the export; the panel is small, so the
            // rest is summarised.
            const std::string mermaid = diagram->ExportMermaid();
            std::istringstream stream(mermaid);
            std::ostringstream shown;
            std::string line;
            int count = 0;
            int total = 0;
            while (std::getline(stream, line)) {
                ++total;
                if (count < 14) {
                    shown << line << "\n";
                    ++count;
                }
            }
            if (total > count) shown << "... (" << (total - count) << " more lines)";

            aboutTitle->SetText("MERMAID EXPORT");
            aboutPanel->SetText(shown.str());
            statusBar->SetText("Exported " + std::to_string(total) +
                               " lines of Mermaid ishikawa-beta - switch tabs to restore the panel");
        };

        btnReset->onClick = [themeSelector, paletteSelector, sideSelector, markerSelector,
                             headSelector, presetSelector, swIcons, swSubCauses, swWeights,
                             swCaption, aboutTitle, allDiagrams, tabSpecs, statusBar,
                             updateStats, updateAbout]() {
            themeSelector->SetSelectedIndex(0);
            paletteSelector->SetSelectedIndex(0);
            sideSelector->SetSelectedIndex(0);
            markerSelector->SetSelectedIndex(0);
            headSelector->SetSelectedIndex(0);
            presetSelector->SetSelectedIndex(0);
            swIcons->SetChecked(true);
            swSubCauses->SetChecked(true);
            swWeights->SetChecked(false);
            swCaption->SetChecked(false);

            for (size_t i = 0; i < allDiagrams.size() && i < tabSpecs.size(); ++i) {
                auto& diagram = allDiagrams[i];
                diagram->SetDarkTheme(false);
                diagram->SetPalette(FishbonePalette::Vibrant);
                diagram->SetSidePolicy(FishboneSidePolicy::Alternate);
                diagram->SetCauseMarker(FishboneCauseMarker::Dot);
                diagram->SetShowCategoryIcons(true);
                diagram->SetShowSubCauses(true);
                diagram->SetShowWeights(false);
                diagram->ClearSelection();

                const bool classic = (tabSpecs[i].design == FishboneDesign::Classic);
                diagram->SetHeadShape(classic ? FishboneHeadShape::Box
                                              : (tabSpecs[i].design == FishboneDesign::ChevronSpine
                                                         ? FishboneHeadShape::FishHead
                                                         : FishboneHeadShape::Triangle));
                diagram->SetEffectPlacement(classic ? FishboneEffectPlacement::HeadBox
                                                    : FishboneEffectPlacement::Title);
                const bool bracket = (tabSpecs[i].design == FishboneDesign::Bracket);
                diagram->SetShowSpineCaption(bracket);

                switch (tabSpecs[i].sample) {
                    case 1:  diagram->SetDocument(FishboneSamples::CustomerChurn()); break;
                    case 2:  diagram->SetDocument(FishboneSamples::SoftwareDelivery()); break;
                    default: diagram->SetDocument(FishboneSamples::QualityControl()); break;
                }
            }

            aboutTitle->SetText("ABOUT THIS DESIGN");
            statusBar->SetText("View reset - all diagrams restored to their defaults");
            updateStats();
            updateAbout();
        };

        // Initial panel contents
        updateStats();
        updateAbout();

        return mainContainer;
    }

} // namespace UltraCanvas
