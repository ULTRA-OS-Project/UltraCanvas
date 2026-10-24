// Apps/DemoApp/UltraCanvasMatrixDiagramExamples.cpp
// Matrix diagram examples: L, T, X, Y and House arrangements with live presentation controls
// Version: 1.7.0 - the shared page look (UltraCanvasDemoPageKit): sidebar and
//                  diagram cards, every caption sized to its text, file
//                  buttons as wide as their text
// Version: 1.6.0 - C-Shaped (Cube) tab; the read-out names a cube cell's slice and a clicked slice's total
// Version: 1.5.0 - Attribute columns switch (the Improvement Tools departments carry two)
// Version: 1.4.0 - Pan & zoom and Editable switches; Open JSON... and Save JSON...
// Version: 1.3.0 - Import table... button: a spreadsheet into an Imported tab
// Version: 1.2.0 - House of Quality tab; numbers-or-bars roll-up control
// Version: 1.1.0 - X and Y tabs; hub, header band and cell tint controls
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Diagrams/UltraCanvasMatrixDiagram.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"     // GetResourcesDir
#include "UltraCanvasUtils.h"      // NormalizePath
#include "UltraCanvasPathUtf8.h"   // PathToUtf8 / PathFromUtf8 for the file name
#include "DataFormats/UltraCanvasJSON.h"
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMatrixDiagramExamples() {
        const int SIDEBAR_W = 170;
        // The sidebar's card width before it wraps under the diagram: room
        // for the 170 px controls and the card's padding.
        const float SIDEBAR_CARD_W = SIDEBAR_W + 2 * DemoKit::kCardPadding + 2;

        auto mainContainer = DemoKit::CreatePage("matrixMain", "Matrix Diagram",
                "Relationships between two to four ordered lists, marked from a named "
                "scale and rolled up into weighted row and column totals");

        // Controls | diagram tabs and read-out. The sidebar keeps its width;
        // on a narrow window the cards wrap under each other.
        auto contentContainer = DemoKit::AddRow(mainContainer, "matrixContent");

        // The small upper-case heading of a panel.
        auto makeSectionTitle = [](const std::string& id, const std::string& text) {
            auto title = DemoKit::CreateText(id, text);
            title->SetFontSize(11);
            title->SetFontWeight(FontWeight::Bold);
            title->SetTextColor(Color(73, 80, 87, 255));
            return title;
        };

        // ===== LEFT SIDEBAR (controls) =====
        auto leftSidebar = DemoKit::AddCard(contentContainer, "matrixLeft", "", "", SIDEBAR_CARD_W);
        leftSidebar->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

        DemoKit::Add(leftSidebar, makeSectionTitle("matrixControlsTitle", "CONTROLS"));

        auto addDropdown = [&](const std::string& id, const std::string& caption) {
            auto label = DemoKit::CreateText(id + "Label", caption);
            label->SetFontSize(10);
            label->SetFontWeight(FontWeight::Bold);
            DemoKit::Add(leftSidebar, label);

            auto dropdown = std::make_shared<UltraCanvasDropdown>(id, 0, 0, SIDEBAR_W, 26);
            DemoKit::AddStretched(leftSidebar, dropdown);
            return dropdown;
        };

        // The caption takes the row's width and wraps; the switch keeps its
        // size at the right.
        auto addSwitch = [&](const std::string& id, const std::string& caption) {
            auto row = DemoKit::CreateRow(id + "Row", 8, false);
            DemoKit::Add(leftSidebar, row);

            auto label = DemoKit::CreateText(id + "Label", caption);
            label->SetFontSize(11);
            DemoKit::Add(row, label, 1);

            auto toggle = std::make_shared<UltraCanvasSwitch>(id, 0, 0, 42, 22);
            DemoKit::AddFixed(row, toggle);
            return toggle;
        };

        auto themeSelector = addDropdown("matrixTheme", "Theme");
        themeSelector->AddItem("Light", "light");
        themeSelector->AddItem("Dark", "dark");
        themeSelector->SetSelectedIndex(0);

        auto totalsSelector = addDropdown("matrixTotals", "Roll-up totals");
        totalsSelector->AddItem("Both axes", "both");
        totalsSelector->AddItem("Rows only", "rows");
        totalsSelector->AddItem("Columns only", "columns");
        totalsSelector->AddItem("None", "none");
        totalsSelector->SetSelectedIndex(0);

        auto headerSelector = addDropdown("matrixHeaderFit", "Column headers");
        headerSelector->AddItem("Auto", "auto");
        headerSelector->AddItem("Horizontal", "horizontal");
        headerSelector->AddItem("Wrapped", "wrapped");
        headerSelector->AddItem("Rotated 45", "r45");
        headerSelector->AddItem("Rotated 90", "r90");
        headerSelector->SetSelectedIndex(0);

        auto legendSelector = addDropdown("matrixLegendPos", "Legend");
        legendSelector->AddItem("Bottom", "bottom");
        legendSelector->AddItem("Top", "top");
        legendSelector->AddItem("Right", "right");
        legendSelector->SetSelectedIndex(0);

        auto hubSelector = addDropdown("matrixHub", "X hub");
        hubSelector->AddItem("Captions", "captions");
        hubSelector->AddItem("Blank", "blank");
        hubSelector->SetSelectedIndex(0);

        auto rollUpSelector = addDropdown("matrixRollUp", "Column totals as");
        rollUpSelector->AddItem("Bars with ranks", "bars");
        rollUpSelector->AddItem("Numbers", "numbers");
        rollUpSelector->SetSelectedIndex(0);

        auto swBands = addSwitch("matrixBands", "Row banding");
        auto swLabelFill = addSwitch("matrixLabelFill", "Filled label band");
        auto swValues = addSwitch("matrixValues", "Cell weights");
        auto swAxisTitles = addSwitch("matrixAxisTitles", "Axis titles");
        auto swTint = addSwitch("matrixTint", "Tint cells");
        auto swHeaderBand = addSwitch("matrixHeaderBand", "Form header");
        auto swPanZoom = addSwitch("matrixPanZoom", "Pan & zoom");
        auto swEditable = addSwitch("matrixEditable", "Editable");
        auto swAttributes = addSwitch("matrixAttributes", "Attribute columns");

        // ===== DIAGRAM TABS =====
        // The diagrams have a fixed size; the card is at least as wide as the
        // tabs and takes the width the sidebar leaves.
        auto centerCard = DemoKit::AddCard(contentContainer, "matrixCenter", "", "",
                                           800 + 2 * DemoKit::kCardPadding + 2);
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>(
                "matrixTabs", 0, 0, 800, 566);
        DemoKit::AddFixed(centerCard, tabbedContainer);

        struct TabSpec {
            const char* id;
            const char* tabTitle;
            MatrixModel (*build)();
        };
        const std::vector<TabSpec> specs = {
                {"matrixTools",     "Improvement Tools", &MatrixSamples::ImprovementTools},
                {"matrixEmployees", "T-Shaped",          &MatrixSamples::EmployeeAllocation},
                {"matrixQfd",       "Weighted (QFD)",    &MatrixSamples::QualityFunctionDeployment},
                {"matrixSupply",    "X-Shaped",          &MatrixSamples::SupplyChain},
                {"matrixTrace",     "Y-Shaped",          &MatrixSamples::DesignTraceability},
                {"matrixHouse",     "House of Quality",  &MatrixSamples::HouseOfQuality},
                {"matrixCube",      "C-Shaped (Cube)",   &MatrixSamples::DefectCube},
        };

        // Shared, because an import adds a diagram after the controls below
        // have been wired, and they must reach it too.
        auto allDiagrams = std::make_shared<std::vector<std::shared_ptr<UltraCanvasMatrixDiagram>>>();

        for (const TabSpec& spec : specs) {
            auto diagram = CreateMatrixDiagramElement(spec.id, 0, 0, 780, 506, spec.build());
            diagram->SetTotals(MatrixTotals::Both);
            diagram->SetHubMode(MatrixHubMode::Captions);
            diagram->SetRollUp(MatrixRollUp::Bars);

            auto tabContent = std::make_shared<UltraCanvasContainer>(
                    std::string(spec.id) + "Tab", 0, 0, 790, 516);
            tabContent->SetBackgroundColor(Color(255, 255, 255, 255));
            tabContent->AddChild(diagram);
            tabbedContainer->AddTab(spec.tabTitle, tabContent);
            allDiagrams->push_back(diagram);
        }

        // ===== STATUS / READ-OUT =====
        // Under the tabs: the heading with the file buttons beside it (they
        // wrap under it when the texts grow), then the read-out lines, each as
        // tall as its wrapped text.
        auto readoutPanel = DemoKit::CreateColumn("matrixReadout", 6);
        DemoKit::Add(centerCard, readoutPanel);

        auto readoutHeader = DemoKit::CreateRow("matrixReadoutHeader", 8);
        readoutHeader->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        DemoKit::Add(readoutPanel, readoutHeader);

        auto readoutTitle = makeSectionTitle("matrixReadoutTitle", "SELECTION");
        DemoKit::Add(readoutHeader, readoutTitle, 1);

        auto readoutText = DemoKit::CreateText("matrixReadoutText",
                "Click a cell to read its relationship, a row or column header for its total, "
                "or a legend entry to filter the marks.");
        readoutText->SetTextColor(Color(73, 80, 87, 255));
        DemoKit::Add(readoutPanel, readoutText);

        auto rankingText = DemoKit::CreateText("matrixRanking", "");
        rankingText->SetTextColor(Color(33, 37, 41, 255));
        DemoKit::Add(readoutPanel, rankingText);

        // A file button: as wide as its text, at least the old 145 px.
        auto makeFileButton = [](const std::string& id, const std::string& text) {
            auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 26, text);
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(145);
            button->boxConstraints = limits;
            return button;
        };

        auto activeDiagram = [tabbedContainer, allDiagrams]() -> std::shared_ptr<UltraCanvasMatrixDiagram> {
            int index = tabbedContainer->GetActiveTab();
            if (index < 0 || index >= static_cast<int>(allDiagrams->size())) return nullptr;
            return (*allDiagrams)[static_cast<size_t>(index)];
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

        // The read-out callbacks every diagram gets - the samples now, an
        // imported sheet later.
        auto wireDiagram = [readoutText](const std::shared_ptr<UltraCanvasMatrixDiagram>& diagram) {
            auto weak = std::weak_ptr<UltraCanvasMatrixDiagram>(diagram);
            diagram->onCellClick = [weak, readoutText](const MatrixRef& ref) {
                auto d = weak.lock();
                if (!d) return;
                const MatrixModel& model = d->GetModel();
                const MatrixItemSet* rows = model.RowSet(ref.panel);
                const MatrixItemSet* cols = model.ColSet(ref.panel);
                if (!rows || !cols) return;
                if (ref.row >= rows->Count() || ref.col >= cols->Count()) return;

                const std::string level = model.LevelAt(ref.panel, ref.row, ref.col, ref.depth);
                std::ostringstream out;
                out << rows->items[static_cast<size_t>(ref.row)].label << "  x  "
                    << cols->items[static_cast<size_t>(ref.col)].label;
                const MatrixItemSet* depths = model.DepthSet(ref.panel);
                if (depths && ref.depth >= 0 && ref.depth < depths->Count()) {
                    out << "  x  " << depths->items[static_cast<size_t>(ref.depth)].label;
                }
                out << "   ->   " << (level.empty() ? std::string("no relationship") : level);
                readoutText->SetText(out.str());
            };

            diagram->onSliceClick = [weak, readoutText](int panel, int depth, const MatrixItem& item) {
                auto d = weak.lock();
                if (!d) return;
                std::ostringstream out;
                out << "Slice '" << item.label << "' scores "
                    << static_cast<long>(d->GetModel().DepthScore(panel, depth));
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
        };
        for (auto& diagram : *allDiagrams) wireDiagram(diagram);

        // ===== CONTROL WIRING =====
        themeSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            const bool dark = (item.value == "dark");
            for (auto& diagram : *allDiagrams) diagram->SetDarkTheme(dark);
        };

        totalsSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            MatrixTotals which = MatrixTotals::Both;
            if (item.value == "rows")         which = MatrixTotals::Rows;
            else if (item.value == "columns") which = MatrixTotals::Columns;
            else if (item.value == "none")    which = MatrixTotals::NoTotals;
            for (auto& diagram : *allDiagrams) diagram->SetTotals(which);
        };

        headerSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            MatrixHeaderFit fit = MatrixHeaderFit::Auto;
            if (item.value == "horizontal")   fit = MatrixHeaderFit::Horizontal;
            else if (item.value == "wrapped") fit = MatrixHeaderFit::Wrapped;
            else if (item.value == "r45")     fit = MatrixHeaderFit::Rotated45;
            else if (item.value == "r90")     fit = MatrixHeaderFit::Rotated90;
            for (auto& diagram : *allDiagrams) diagram->SetHeaderFit(fit);
        };

        legendSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            ChartLegendPosition position = ChartLegendPosition::BottomCenter;
            if (item.value == "top")        position = ChartLegendPosition::TopCenter;
            else if (item.value == "right") position = ChartLegendPosition::RightCenter;
            for (auto& diagram : *allDiagrams) diagram->SetLegendPosition(position);
        };

        swBands->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            const bool on = (newState == CheckedState::Checked);
            for (auto& diagram : *allDiagrams) {
                MatrixDiagramStyle style = diagram->GetStyle();
                style.alternateRowBands = on;
                diagram->SetStyle(style);
            }
        };

        swLabelFill->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            const bool on = (newState == CheckedState::Checked);
            for (auto& diagram : *allDiagrams) {
                MatrixDiagramStyle style = diagram->GetStyle();
                style.fillLabelBands = on;
                diagram->SetStyle(style);
            }
        };

        swValues->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            for (auto& diagram : *allDiagrams) {
                diagram->SetShowCellValues(newState == CheckedState::Checked);
            }
        };

        swAxisTitles->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            for (auto& diagram : *allDiagrams) {
                diagram->SetShowAxisTitles(newState == CheckedState::Checked);
            }
        };

        rollUpSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            const MatrixRollUp presentation = (item.value == "numbers") ? MatrixRollUp::Numbers
                                                                         : MatrixRollUp::Bars;
            for (auto& diagram : *allDiagrams) diagram->SetRollUp(presentation);
        };

        hubSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            const MatrixHubMode mode = (item.value == "blank") ? MatrixHubMode::Blank
                                                               : MatrixHubMode::Captions;
            for (auto& diagram : *allDiagrams) diagram->SetHubMode(mode);
        };

        swPanZoom->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            const bool on = (newState == CheckedState::Checked);
            for (auto& diagram : *allDiagrams) {
                diagram->SetPanZoomEnabled(on);
                diagram->Viewport().SetControlsPosition(DiagramPanelPosition::TopRight);
                diagram->Viewport().SetControlsVisible(on);
            }
        };

        swEditable->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            for (auto& diagram : *allDiagrams) {
                diagram->SetEditable(newState == CheckedState::Checked);
            }
        };

        swTint->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            const bool on = (newState == CheckedState::Checked);
            for (auto& diagram : *allDiagrams) {
                MatrixDiagramStyle style = diagram->GetStyle();
                style.tintCellsByLevel = on;
                diagram->SetStyle(style);
            }
        };

        swHeaderBand->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            for (auto& diagram : *allDiagrams) {
                diagram->SetShowHeaderBand(newState == CheckedState::Checked);
            }
        };

        swAttributes->onStateChanged = [allDiagrams](CheckedState, CheckedState newState) {
            for (auto& diagram : *allDiagrams) {
                diagram->SetShowAttributes(newState == CheckedState::Checked);
            }
        };

        swBands->SetChecked(false);
        swLabelFill->SetChecked(true);
        swValues->SetChecked(false);
        swAxisTitles->SetChecked(true);
        swTint->SetChecked(false);
        swHeaderBand->SetChecked(true);
        swPanZoom->SetChecked(false);
        swEditable->SetChecked(false);
        swAttributes->SetChecked(true);

        // ===== IMPORT =====
        // A spreadsheet's tabular form - column labels across the top, row
        // labels down the side, a mark in each cell - read by
        // ImportMatrixTable into an "Imported" tab. Two sample sheets ship in
        // media/diagrams/matrix/, where the dialog opens.
        // Added to the header after Open JSON / Save JSON, as it was placed.
        auto importButton = makeFileButton("matrixImport", "Import table");

        // Puts an imported model in the "Imported" tab - added on the first
        // import, dressed like the other tabs, reused after.
        auto importedDiagram = std::make_shared<std::shared_ptr<UltraCanvasMatrixDiagram>>();
        auto showImported = [tabbedContainer, allDiagrams, importedDiagram, wireDiagram,
                             updateRanking, readoutText](const MatrixModel& model,
                                                         const std::string& message) {
            std::shared_ptr<UltraCanvasMatrixDiagram>& target = *importedDiagram;
            if (!target) {
                target = CreateMatrixDiagramElement("matrixImported", 0, 0, 780, 506, model);
                const auto& first = allDiagrams->front();
                target->SetDarkTheme(first->GetDarkTheme());
                target->SetStyle(first->GetStyle());
                target->SetTotals(first->GetTotals());
                target->SetRollUp(first->GetRollUp());
                target->SetHeaderFit(first->GetHeaderFit());
                target->SetHubMode(first->GetHubMode());
                target->SetEditable(first->GetEditable());
                target->SetPanZoomEnabled(first->GetPanZoomEnabled());
                target->Viewport().SetControlsPosition(DiagramPanelPosition::TopRight);
                target->Viewport().SetControlsVisible(first->GetPanZoomEnabled());

                auto tabContent = std::make_shared<UltraCanvasContainer>(
                        "matrixImportedTab", 0, 0, 790, 516);
                tabContent->SetBackgroundColor(Color(255, 255, 255, 255));
                tabContent->AddChild(target);
                tabbedContainer->AddTab("Imported", tabContent);
                allDiagrams->push_back(target);
                wireDiagram(target);
            } else {
                target->SetModel(model);
            }
            tabbedContainer->SetActiveTab(static_cast<int>(allDiagrams->size()) - 1);
            readoutText->SetText(message);
            updateRanking();
        };

        importButton->onClick = [showImported, readoutText]() {
            FileDialogOptions opts;
            opts.SetTitle("Import a matrix table")
                .AddFilter("Comma-separated values (*.csv)", "csv")
                .AddFilter("Text (*.txt)", "txt")
                .AddFilter("All files", "*")
                .SetInitialDirectory(NormalizePath(GetResourcesDir() + "media/diagrams/matrix/"));

            UltraCanvasFileLoader::OpenFileDialog(opts,
                    [showImported, readoutText](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;

                // Read through the loader, as every user file is: it also
                // takes a gzipped sheet or a URL.
                const FileBytesResult loaded = UltraCanvasFileLoader::LoadFile(path);
                if (!loaded.success) {
                    readoutText->SetText("Could not open " + path + ": " + loaded.error);
                    return;
                }
                const std::string text(loaded.bytes.begin(), loaded.bytes.end());

                // The sheet does not say which scale its marks belong to, so
                // try the presets in turn and keep the first that reads every
                // mark: numbers 9/3/1 land on QFD, names like "medium" on 5/3/1.
                struct Candidate { const char* name; MatrixScale scale; };
                const Candidate candidates[] = {
                        {"QFD 9/3/1", MatrixScale::QFD()},
                        {"strong/medium/weak 5/3/1", MatrixScale::Strength531()},
                        {"high/medium/low", MatrixScale::HighMediumLow()},
                        {"presence", MatrixScale::Presence()},
                };
                MatrixModel model;
                std::string error, firstError;
                const char* used = nullptr;
                for (const Candidate& c : candidates) {
                    if (ImportMatrixTable(text, c.scale, model, "Rows", "Columns", &error)) {
                        used = c.name;
                        break;
                    }
                    if (firstError.empty()) firstError = error;
                }
                const std::string fileName = PathToUtf8(PathFromUtf8(path).filename());
                if (!used) {
                    readoutText->SetText("Could not import " + fileName + ": " + firstError);
                    return;
                }

                model.title = fileName;
                model.subtitle = std::to_string(model.sets[0].Count()) + " rows x "
                                 + std::to_string(model.sets[1].Count()) + " columns, read on the "
                                 + used + " scale";

                showImported(model, "Imported " + fileName + " - " + model.subtitle + ".");
            });
        };

        // ===== JSON =====
        // The whole model - every panel, scale, cell and header field - where
        // the table import carries one panel. Open JSON... shares the
        // Imported tab; Save JSON... writes whichever tab is showing.
        auto openJsonButton = makeFileButton("matrixOpenJson", "Open JSON");
        DemoKit::Add(readoutHeader, openJsonButton);
        auto saveJsonButton = makeFileButton("matrixSaveJson", "Save JSON");
        DemoKit::Add(readoutHeader, saveJsonButton);
        DemoKit::Add(readoutHeader, importButton);

        openJsonButton->onClick = [showImported, readoutText]() {
            FileDialogOptions opts;
            opts.SetTitle("Open a matrix diagram")
                .AddFilter("Matrix diagram (*.json)", "json")
                .AddFilter("All files", "*");
            UltraCanvasFileLoader::OpenFileDialog(opts,
                    [showImported, readoutText](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                const FileBytesResult loaded = UltraCanvasFileLoader::LoadFile(path);
                if (!loaded.success) {
                    readoutText->SetText("Could not open " + path + ": " + loaded.error);
                    return;
                }
                MatrixModel model;
                std::string error;
                const std::string fileName = PathToUtf8(PathFromUtf8(path).filename());
                if (!ImportMatrixJson(std::string(loaded.bytes.begin(), loaded.bytes.end()), model, &error)) {
                    readoutText->SetText("Could not read " + fileName + ": " + error);
                    return;
                }
                showImported(model, "Opened " + fileName + ".");
            });
        };

        saveJsonButton->onClick = [activeDiagram, readoutText]() {
            auto diagram = activeDiagram();
            if (!diagram) return;
            FileDialogOptions opts;
            opts.SetTitle("Save the matrix diagram")
                .AddFilter("Matrix diagram (*.json)", "json")
                .SetDefaultExtension("json")
                .SetDefaultFileName("matrix.json");
            // The model is copied now: the dialog names a file later, and an
            // edit made meanwhile belongs to the next save, not this one.
            const MatrixModel model = diagram->GetModel();
            UltraCanvasFileLoader::SaveFileDialog(opts,
                    [model, readoutText](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                JSONSerializeOptions pretty;
                pretty.pretty = true;
                pretty.indentWidth = 2;
                const bool saved = JSON::SerializeToFile(path, MatrixModelToJson(model), pretty);
                readoutText->SetText(saved ? "Saved " + path : "Could not write " + path);
            });
        };

        // ===== ABOUT =====
        // Under the switches (it used to overlap the last one).
        DemoKit::Add(leftSidebar, makeSectionTitle("matrixAboutTitle", "ABOUT"));

        auto aboutText = DemoKit::CreateCaption("matrixAbout",
                "Click a cell, then use the arrow keys and Tab. With Editable on, "
                "Space or a click steps a cell's level; Delete clears it.");
        aboutText->SetWrap(TextWrap::WrapWordChar);
        aboutText->SetTextColor(Color(108, 117, 125, 255));
        DemoKit::Add(leftSidebar, aboutText);

        // The ranking belongs to whichever tab is showing, so recompute it on
        // every tab change as well as at construction.
        tabbedContainer->onTabChange = [updateRanking](int, int) { updateRanking(); };

        updateRanking();
        return mainContainer;
    }

} // namespace UltraCanvas
