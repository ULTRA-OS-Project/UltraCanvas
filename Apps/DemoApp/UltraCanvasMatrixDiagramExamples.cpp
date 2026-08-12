// Apps/DemoApp/UltraCanvasMatrixDiagramExamples.cpp
// Matrix diagram examples: L and T arrangements with live presentation controls
// Version: 1.0.0
// Last Modified: 2026-08-11
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasMatrixDiagram.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasSwitch.h"
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMatrixDiagramExamples() {
        // Children placed directly inside a sidebar/panel container are inset
        // explicitly instead of using SetPadding() - the same pattern the
        // fishbone/SWOT/Quadrant demos use - so absolutely-positioned children
        // are not clipped by the container's content area.
        const int PAD = 15;
        const int SIDEBAR_W = 170;

        auto mainContainer = std::make_shared<UltraCanvasContainer>("matrixMain", 0, 0, 1140, 800);
        mainContainer->SetBackgroundColor(Color(248, 249, 250, 255));

        auto headerContainer = std::make_shared<UltraCanvasContainer>("matrixHeader", 0, 0, 1140, 90);
        headerContainer->SetBackgroundColor(Color(255, 255, 255, 255));
        mainContainer->AddChild(headerContainer);

        auto mainTitle = std::make_shared<UltraCanvasLabel>("matrixTitle", 30, 15, 1080, 35);
        mainTitle->SetText("Matrix Diagram");
        mainTitle->SetFontSize(22);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainTitle->SetAlignment(TextAlignment::Left);
        mainTitle->SetTextColor(Color(33, 37, 41, 255));
        headerContainer->AddChild(mainTitle);

        auto subtitle = std::make_shared<UltraCanvasLabel>("matrixSubtitle", 30, 52, 1000, 30);
        subtitle->SetText("Relationships between two or three ordered lists, marked from a named "
                          "scale and rolled up into weighted row and column totals");
        subtitle->SetFontSize(13);
        subtitle->SetAlignment(TextAlignment::Left);
        subtitle->SetTextColor(Color(108, 117, 125, 255));
        headerContainer->AddChild(subtitle);

        auto contentContainer = std::make_shared<UltraCanvasContainer>("matrixContent", 0, 90, 1140, 710);
        mainContainer->AddChild(contentContainer);

        // ===== LEFT SIDEBAR (controls) =====
        auto leftSidebar = std::make_shared<UltraCanvasContainer>("matrixLeft", 20, 20, 200, 670);
        leftSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(leftSidebar);

        auto controlsTitle = std::make_shared<UltraCanvasLabel>("matrixControlsTitle", PAD, PAD, SIDEBAR_W, 22);
        controlsTitle->SetText("CONTROLS");
        controlsTitle->SetFontSize(11);
        controlsTitle->SetFontWeight(FontWeight::Bold);
        controlsTitle->SetTextColor(Color(73, 80, 87, 255));
        controlsTitle->SetAlignment(TextAlignment::Left);
        leftSidebar->AddChild(controlsTitle);

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

        auto addSwitch = [&](const std::string& id, const std::string& caption, int y) {
            auto label = std::make_shared<UltraCanvasLabel>(id + "Label", PAD, y, SIDEBAR_W - 50, 22);
            label->SetText(caption);
            label->SetFontSize(11);
            label->SetAlignment(TextAlignment::Left);
            leftSidebar->AddChild(label);

            auto toggle = std::make_shared<UltraCanvasSwitch>(id, PAD + SIDEBAR_W - 46, y, 42, 22);
            leftSidebar->AddChild(toggle);
            return toggle;
        };

        auto themeSelector = addDropdown("matrixTheme", "Theme", 44);
        themeSelector->AddItem("Light", "light");
        themeSelector->AddItem("Dark", "dark");
        themeSelector->SetSelectedIndex(0);

        auto totalsSelector = addDropdown("matrixTotals", "Roll-up totals", 96);
        totalsSelector->AddItem("Both axes", "both");
        totalsSelector->AddItem("Rows only", "rows");
        totalsSelector->AddItem("Columns only", "columns");
        totalsSelector->AddItem("None", "none");
        totalsSelector->SetSelectedIndex(0);

        auto headerSelector = addDropdown("matrixHeaderFit", "Column headers", 148);
        headerSelector->AddItem("Auto", "auto");
        headerSelector->AddItem("Horizontal", "horizontal");
        headerSelector->AddItem("Wrapped", "wrapped");
        headerSelector->AddItem("Rotated 45", "r45");
        headerSelector->AddItem("Rotated 90", "r90");
        headerSelector->SetSelectedIndex(0);

        auto legendSelector = addDropdown("matrixLegendPos", "Legend", 200);
        legendSelector->AddItem("Bottom", "bottom");
        legendSelector->AddItem("Top", "top");
        legendSelector->AddItem("Right", "right");
        legendSelector->SetSelectedIndex(0);

        auto swBands = addSwitch("matrixBands", "Row banding", 258);
        auto swLabelFill = addSwitch("matrixLabelFill", "Filled label band", 288);
        auto swValues = addSwitch("matrixValues", "Cell weights", 318);
        auto swAxisTitles = addSwitch("matrixAxisTitles", "Axis titles", 348);

        // ===== DIAGRAM TABS =====
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>(
                "matrixTabs", 240, 20, 800, 566);
        contentContainer->AddChild(tabbedContainer);

        struct TabSpec {
            const char* id;
            const char* tabTitle;
            MatrixModel (*build)();
        };
        const std::vector<TabSpec> specs = {
                {"matrixTools",     "Improvement Tools", &MatrixSamples::ImprovementTools},
                {"matrixEmployees", "T-Shaped",          &MatrixSamples::EmployeeAllocation},
                {"matrixQfd",       "Weighted (QFD)",    &MatrixSamples::QualityFunctionDeployment},
        };

        std::vector<std::shared_ptr<UltraCanvasMatrixDiagram>> allDiagrams;

        for (const TabSpec& spec : specs) {
            auto diagram = CreateMatrixDiagramElement(spec.id, 0, 0, 780, 506, spec.build());
            diagram->SetTotals(MatrixTotals::Both);

            auto tabContent = std::make_shared<UltraCanvasContainer>(
                    std::string(spec.id) + "Tab", 0, 0, 790, 516);
            tabContent->SetBackgroundColor(Color(255, 255, 255, 255));
            tabContent->AddChild(diagram);
            tabbedContainer->AddTab(spec.tabTitle, tabContent);
            allDiagrams.push_back(diagram);
        }

        // ===== STATUS / READ-OUT =====
        auto readoutPanel = std::make_shared<UltraCanvasContainer>("matrixReadout", 240, 602, 800, 88);
        readoutPanel->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(readoutPanel);

        auto readoutTitle = std::make_shared<UltraCanvasLabel>("matrixReadoutTitle", PAD, 10, 400, 20);
        readoutTitle->SetText("SELECTION");
        readoutTitle->SetFontSize(11);
        readoutTitle->SetFontWeight(FontWeight::Bold);
        readoutTitle->SetTextColor(Color(73, 80, 87, 255));
        readoutTitle->SetAlignment(TextAlignment::Left);
        readoutPanel->AddChild(readoutTitle);

        auto readoutText = std::make_shared<UltraCanvasLabel>("matrixReadoutText", PAD, 34, 770, 24);
        readoutText->SetText("Click a cell to read its relationship, a row or column header for its total, "
                             "or a legend entry to filter the marks.");
        readoutText->SetFontSize(12);
        readoutText->SetAlignment(TextAlignment::Left);
        readoutText->SetTextColor(Color(73, 80, 87, 255));
        readoutPanel->AddChild(readoutText);

        auto rankingText = std::make_shared<UltraCanvasLabel>("matrixRanking", PAD, 58, 770, 26);
        rankingText->SetFontSize(12);
        rankingText->SetAlignment(TextAlignment::Left);
        rankingText->SetTextColor(Color(33, 37, 41, 255));
        readoutPanel->AddChild(rankingText);

        auto activeDiagram = [tabbedContainer, allDiagrams]() -> std::shared_ptr<UltraCanvasMatrixDiagram> {
            int index = tabbedContainer->GetActiveTab();
            if (index < 0 || index >= static_cast<int>(allDiagrams.size())) return nullptr;
            return allDiagrams[index];
        };

        // The ranking is the payoff of the weighted roll-up: it is what a QFD
        // exercise is run to produce, and it comes straight off the model.
        auto updateRanking = [activeDiagram, rankingText]() {
            auto diagram = activeDiagram();
            if (!diagram) return;

            const MatrixModel& model = diagram->GetModel();
            const MatrixItemSet* cols = model.ColSet(0);
            if (!cols) return;

            std::vector<int> ranked = model.RankColumns(0);
            std::ostringstream out;
            out << "Top columns by weighted score: ";
            for (size_t i = 0; i < ranked.size() && i < 4; ++i) {
                if (i > 0) out << "   ";
                out << (i + 1) << ". " << cols->items[static_cast<size_t>(ranked[i])].label
                    << " (" << static_cast<long>(model.ColumnScore(0, ranked[i])) << ")";
            }
            rankingText->SetText(out.str());
        };

        for (auto& diagram : allDiagrams) {
            auto weak = std::weak_ptr<UltraCanvasMatrixDiagram>(diagram);
            diagram->onCellClick = [weak, readoutText](const MatrixRef& ref) {
                auto d = weak.lock();
                if (!d) return;
                const MatrixModel& model = d->GetModel();
                const MatrixItemSet* rows = model.RowSet(ref.panel);
                const MatrixItemSet* cols = model.ColSet(ref.panel);
                if (!rows || !cols) return;
                if (ref.row >= rows->Count() || ref.col >= cols->Count()) return;

                const std::string level = model.LevelAt(ref.panel, ref.row, ref.col);
                std::ostringstream out;
                out << rows->items[static_cast<size_t>(ref.row)].label << "  x  "
                    << cols->items[static_cast<size_t>(ref.col)].label << "   ->   "
                    << (level.empty() ? std::string("no relationship") : level);
                readoutText->SetText(out.str());
            };

            diagram->onColumnClick = [weak, readoutText](int panel, int index, const MatrixItem& item) {
                auto d = weak.lock();
                if (!d) return;
                std::ostringstream out;
                out << "Column '" << item.label << "' scores "
                    << static_cast<long>(d->GetModel().ColumnScore(panel, index));
                readoutText->SetText(out.str());
            };

            diagram->onRowClick = [weak, readoutText](int panel, int index, const MatrixItem& item) {
                auto d = weak.lock();
                if (!d) return;
                std::ostringstream out;
                out << "Row '" << item.label << "' scores "
                    << static_cast<long>(d->GetModel().RowScore(panel, index))
                    << "   (importance " << item.importance << ")";
                readoutText->SetText(out.str());
            };
        }

        // ===== CONTROL WIRING =====
        themeSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            const bool dark = (item.value == "dark");
            for (auto& diagram : allDiagrams) diagram->SetDarkTheme(dark);
        };

        totalsSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            MatrixTotals which = MatrixTotals::Both;
            if (item.value == "rows")         which = MatrixTotals::Rows;
            else if (item.value == "columns") which = MatrixTotals::Columns;
            else if (item.value == "none")    which = MatrixTotals::NoTotals;
            for (auto& diagram : allDiagrams) diagram->SetTotals(which);
        };

        headerSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            MatrixHeaderFit fit = MatrixHeaderFit::Auto;
            if (item.value == "horizontal")   fit = MatrixHeaderFit::Horizontal;
            else if (item.value == "wrapped") fit = MatrixHeaderFit::Wrapped;
            else if (item.value == "r45")     fit = MatrixHeaderFit::Rotated45;
            else if (item.value == "r90")     fit = MatrixHeaderFit::Rotated90;
            for (auto& diagram : allDiagrams) diagram->SetHeaderFit(fit);
        };

        legendSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            ChartLegendPosition position = ChartLegendPosition::BottomCenter;
            if (item.value == "top")        position = ChartLegendPosition::TopCenter;
            else if (item.value == "right") position = ChartLegendPosition::RightCenter;
            for (auto& diagram : allDiagrams) diagram->SetLegendPosition(position);
        };

        swBands->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            const bool on = (newState == CheckedState::Checked);
            for (auto& diagram : allDiagrams) {
                MatrixDiagramStyle style = diagram->GetStyle();
                style.alternateRowBands = on;
                diagram->SetStyle(style);
            }
        };

        swLabelFill->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            const bool on = (newState == CheckedState::Checked);
            for (auto& diagram : allDiagrams) {
                MatrixDiagramStyle style = diagram->GetStyle();
                style.fillLabelBands = on;
                diagram->SetStyle(style);
            }
        };

        swValues->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            for (auto& diagram : allDiagrams) {
                diagram->SetShowCellValues(newState == CheckedState::Checked);
            }
        };

        swAxisTitles->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            for (auto& diagram : allDiagrams) {
                diagram->SetShowAxisTitles(newState == CheckedState::Checked);
            }
        };

        swBands->SetChecked(false);
        swLabelFill->SetChecked(true);
        swValues->SetChecked(false);
        swAxisTitles->SetChecked(true);

        // ===== ABOUT =====
        auto aboutTitle = std::make_shared<UltraCanvasLabel>("matrixAboutTitle", PAD, 392, SIDEBAR_W, 20);
        aboutTitle->SetText("ABOUT");
        aboutTitle->SetFontSize(11);
        aboutTitle->SetFontWeight(FontWeight::Bold);
        aboutTitle->SetTextColor(Color(73, 80, 87, 255));
        aboutTitle->SetAlignment(TextAlignment::Left);
        leftSidebar->AddChild(aboutTitle);

        auto aboutText = std::make_shared<UltraCanvasLabel>("matrixAbout", PAD, 414, SIDEBAR_W, 240);
        aboutText->SetText("One of the Seven Management and Planning Tools.\n\n"
                           "Cells hold a named level from a small scale, not a number - "
                           "for continuous values use the Heatmap chart instead.\n\n"
                           "Each level carries a weight, so rows and columns roll up into "
                           "totals. The 'Improvement Tools' tab uses a 5/3/1 scale; "
                           "'Weighted (QFD)' uses 9/3/1 and real row importances.\n\n"
                           "Room adjacency matrices belong to the Adjacency Diagram, "
                           "which owns that data and draws its own matrix view.");
        aboutText->SetFontSize(10);
        aboutText->SetAlignment(TextAlignment::Left);
        aboutText->SetWrap(TextWrap::WrapWordChar);
        aboutText->SetTextColor(Color(108, 117, 125, 255));
        leftSidebar->AddChild(aboutText);

        // The ranking belongs to whichever tab is showing, so recompute it on
        // every tab change as well as at construction.
        tabbedContainer->onTabChange = [updateRanking](int, int) { updateRanking(); };

        updateRanking();
        return mainContainer;
    }

} // namespace UltraCanvas
