// Apps/DemoApp/UltraCanvasPerformanceMatrixExamples.cpp
// Performance matrix examples: the 9-box, performance vs potential tiles,
// the performance management matrix with header tabs (vivid and in one hue),
// talent review cards, skill / will, and a calibration with a team in the boxes
// Version: 1.2.0 - calibration: people moved by drag or Ctrl+arrows with a veto
//   on the top box, the "In the box" list behind "+N more", Open / Save JSON
//   and Import / Export of the people as CSV through UltraCanvasFileLoader
// Version: 1.1.0 - the Calibration tab: an invented team as counts, shares
//   against a target, dots or names, a group legend, an anonymised mode
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasPerformanceMatrix.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasListModel.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"   // PathToUtf8 / PathFromUtf8 for the file name
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePerformanceMatrixExamples() {
        const int PAD = 15;

        auto mainContainer = std::make_shared<UltraCanvasContainer>("pmMainContainer", 0, 0, 1140, 800);
        mainContainer->SetBackgroundColor(Color(248, 249, 250, 255));

        auto headerContainer = std::make_shared<UltraCanvasContainer>("pmHeader", 0, 0, 1140, 90);
        headerContainer->SetBackgroundColor(Color(255, 255, 255, 255));
        mainContainer->AddChild(headerContainer);

        auto mainTitle = std::make_shared<UltraCanvasLabel>("pmMainTitle", 30, 15, 640, 35);
        mainTitle->SetText("Performance Matrix");
        mainTitle->SetFontSize(22);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainTitle->SetAlignment(TextAlignment::Left);
        mainTitle->SetTextColor(Color(33, 37, 41, 255));
        headerContainer->AddChild(mainTitle);

        auto subtitle = std::make_shared<UltraCanvasLabel>("pmSubtitle", 30, 52, 640, 30);
        subtitle->SetText("Named boxes where two rated axes cross");
        subtitle->SetFontSize(13);
        subtitle->SetWrap(TextWrap::WrapWord);
        subtitle->SetAlignment(TextAlignment::Left);
        subtitle->SetTextColor(Color(108, 117, 125, 255));
        headerContainer->AddChild(subtitle);

        // Files: the whole model as JSON, the people alone as CSV. Every file
        // goes through UltraCanvasFileLoader.
        auto makeFileButton = [&headerContainer](const std::string& id, const std::string& text,
                                                 const std::string& tip, int x, int y) {
            auto button = std::make_shared<UltraCanvasButton>(id, x, y, 190, 28, text);
            button->SetTooltip(tip);
            headerContainer->AddChild(button);
            return button;
        };
        auto openJsonButton = makeFileButton("pmOpenJson", "Open JSON\xE2\x80\xA6",
                                             "Open a whole matrix into the Calibration tab", 720, 15);
        auto saveJsonButton = makeFileButton("pmSaveJson", "Save JSON\xE2\x80\xA6",
                                             "Save the matrix shown, people included", 925, 15);
        auto importCsvButton = makeFileButton("pmImportCsv", "Import CSV\xE2\x80\xA6",
                                              "Replace the people of the matrix shown", 720, 52);
        auto exportCsvButton = makeFileButton("pmExportCsv", "Export CSV\xE2\x80\xA6",
                                              "Save the people of the matrix shown", 925, 52);

        auto contentContainer = std::make_shared<UltraCanvasContainer>("pmContent", 0, 90, 1140, 710);
        mainContainer->AddChild(contentContainer);

        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("pmTabs", 20, 20, 860, 670);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        contentContainer->AddChild(tabbedContainer);

        // ===== THE EXAMPLES =====
        struct Example {
            std::string tab;
            PerformanceMatrixModel model;
            PerformanceMatrixDesign design;
            PerformanceCellColoring coloring;
            PerformanceMatrixPalette palette;
            bool panel;
        };
        PerformanceMatrixModel management = PerformanceMatrixSamples::PerformanceManagement();
        const char* columnIcons[] = {"rating-heart-on.svg", "chart.svg", "user-plus.svg"};
        const char* rowIcons[] = {"warning.png", "check.png", "arrow-up.svg"};
        for (size_t i = 0; i < 3; ++i) {
            management.xAxis.levels[i].iconPath = DemoIconPath(columnIcons[i]);
            management.yAxis.levels[i].iconPath = DemoIconPath(rowIcons[i]);
        }
        // The calibration: the 9-box with an invented team placed in it.
        PerformanceMatrixModel calibration = PerformanceMatrixSamples::NineBox();
        calibration.title = "Calibration: 36 people (invented)";
        calibration.items = PerformanceMatrixSamples::SampleTeam();
        calibration.targetDistribution = PerformanceMatrixSamples::SampleTargetDistribution();
        std::vector<Example> examples = {
            {"Calibration", calibration, PerformanceMatrixDesign::Grid, PerformanceCellColoring::DiagonalBands,
             PerformanceMatrixPalette::TrafficLight(), false},
            {"9-box", PerformanceMatrixSamples::NineBox(), PerformanceMatrixDesign::Grid,
             PerformanceCellColoring::DiagonalBands, PerformanceMatrixPalette::TrafficLight(), false},
            {"Performance vs potential", PerformanceMatrixSamples::PerformanceVsPotential(),
             PerformanceMatrixDesign::Tiles, PerformanceCellColoring::DiagonalBands,
             PerformanceMatrixPalette::TrafficLight(), false},
            {"Management", management, PerformanceMatrixDesign::HeaderTabs, PerformanceCellColoring::Plain,
             PerformanceMatrixPalette::Vivid(), false},
            {"One hue", management, PerformanceMatrixDesign::HeaderTabs, PerformanceCellColoring::Plain,
             PerformanceMatrixPalette::Monochrome(Color(36, 104, 178, 255)), true},
            {"Talent review", PerformanceMatrixSamples::TalentReview(), PerformanceMatrixDesign::Cards,
             PerformanceCellColoring::ByRow, PerformanceMatrixPalette::Pastel(), false},
            {"Skill / will", PerformanceMatrixSamples::SkillWill(), PerformanceMatrixDesign::Grid,
             PerformanceCellColoring::DiagonalBands, PerformanceMatrixPalette::TrafficLight(), false},
        };

        std::vector<std::shared_ptr<UltraCanvasPerformanceMatrix>> matrices;
        for (size_t i = 0; i < examples.size(); ++i) {
            const Example& example = examples[i];
            auto matrix = CreatePerformanceMatrix("pmMatrix" + std::to_string(i), 10, 10, 840, 612);
            matrix->SetModel(example.model);
            matrix->SetDesign(example.design);
            matrix->SetCellColoring(example.coloring);
            matrix->SetPalette(example.palette);
            matrix->SetShowContainerPanel(example.panel);
            matrix->SetShowTotals(!example.model.items.empty());

            auto tabContent = std::make_shared<UltraCanvasContainer>("pmTab" + std::to_string(i), 0, 0, 860, 632);
            tabContent->SetBackgroundColor(Color(255, 255, 255, 255));
            tabContent->AddChild(matrix);
            tabbedContainer->AddTab(example.tab, tabContent);
            matrices.push_back(matrix);
        }

        // ===== SIDEBAR =====
        auto sidebar = std::make_shared<UltraCanvasContainer>("pmSidebar", 900, 20, 220, 670);
        sidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(sidebar);
        const int SIDEBAR_W = 190;

        auto addCaption = [&](const std::string& id, const std::string& text, int y) {
            auto caption = std::make_shared<UltraCanvasLabel>(id, PAD, y, SIDEBAR_W, 20);
            caption->SetText(text);
            caption->SetFontSize(10);
            caption->SetFontWeight(FontWeight::Bold);
            caption->SetTextColor(Color(73, 80, 87, 255));
            caption->SetAlignment(TextAlignment::Left);
            sidebar->AddChild(caption);
        };

        addCaption("pmDesignCaption", "Design", PAD);
        auto designSelector = std::make_shared<UltraCanvasDropdown>("pmDesignSelector", PAD, PAD + 23, SIDEBAR_W, 28);
        designSelector->AddItem("Grid", "grid");
        designSelector->AddItem("Tiles", "tiles");
        designSelector->AddItem("Header tabs", "tabs");
        designSelector->AddItem("Cards", "cards");
        designSelector->SetSelectedIndex(0);
        sidebar->AddChild(designSelector);

        addCaption("pmColoringCaption", "Cell colours", PAD + 63);
        auto coloringSelector = std::make_shared<UltraCanvasDropdown>("pmColoringSelector", PAD, PAD + 86, SIDEBAR_W, 28);
        coloringSelector->AddItem("By rank from the best corner", "rank");
        coloringSelector->AddItem("By row", "row");
        coloringSelector->AddItem("By column", "column");
        coloringSelector->AddItem("By how many people", "count");
        coloringSelector->AddItem("Plain", "plain");
        coloringSelector->SetSelectedIndex(0);
        sidebar->AddChild(coloringSelector);

        addCaption("pmPaletteCaption", "Palette", PAD + 126);
        auto paletteSelector = std::make_shared<UltraCanvasDropdown>("pmPaletteSelector", PAD, PAD + 149, SIDEBAR_W, 28);
        paletteSelector->AddItem("Traffic light", "traffic");
        paletteSelector->AddItem("Vivid", "vivid");
        paletteSelector->AddItem("One hue (blue)", "mono");
        paletteSelector->AddItem("Pastel", "pastel");
        paletteSelector->SetSelectedIndex(0);
        sidebar->AddChild(paletteSelector);

        addCaption("pmPeopleCaption", "People", PAD + 189);
        auto peopleSelector = std::make_shared<UltraCanvasDropdown>("pmPeopleSelector", PAD, PAD + 212, SIDEBAR_W, 28);
        peopleSelector->AddItem("Count per box", "count");
        peopleSelector->AddItem("Share and target", "percent");
        peopleSelector->AddItem("Dots", "dots");
        peopleSelector->AddItem("Names", "names");
        peopleSelector->AddItem("Names, anonymised", "anonymised");
        peopleSelector->AddItem("Hidden", "hidden");
        peopleSelector->SetSelectedIndex(0);
        sidebar->AddChild(peopleSelector);

        addCaption("pmSelectionCaption", "SELECTED", PAD + 259);
        auto selection = std::make_shared<UltraCanvasLabel>("pmSelection", PAD, PAD + 283, SIDEBAR_W, 150);
        selection->SetFontSize(10);
        selection->SetWrap(TextWrap::WrapWord);   // tooltips and the hint are sentences, not lines
        selection->SetAlignment(TextAlignment::Left);
        selection->SetBackgroundColor(Color(248, 249, 250, 255));
        selection->SetPadding(10);
        selection->SetText("Click a box or a person. On Calibration, tick Allow moves to drag "
                           "people, or move one with Ctrl+arrows.");
        sidebar->AddChild(selection);

        // Everyone in the box last clicked, or behind its "+N more".
        addCaption("pmBoxCaption", "IN THE BOX", PAD + 443);
        auto boxModel = std::make_shared<UltraCanvasSimpleListModel>();
        auto boxList = std::make_shared<UltraCanvasListView>("pmBoxList", PAD, PAD + 466, SIDEBAR_W, 120);
        boxList->SetModel(boxModel);
        boxList->SetRowHeight(20);
        sidebar->AddChild(boxList);

        auto allowMoves = UltraCanvasCheckbox::CreateCheckbox("pmAllowMoves", static_cast<float>(PAD),
                                                              static_cast<float>(PAD + 596),
                                                              static_cast<float>(SIDEBAR_W), 24.0f,
                                                              "Allow moves", false);
        allowMoves->SetFontSize(11.0f);
        sidebar->AddChild(allowMoves);

        // ===== CALLBACKS (raw pointers: no cycle through the elements) =====
        std::vector<UltraCanvasPerformanceMatrix*> matrixPtrs;
        for (auto& matrix : matrices) matrixPtrs.push_back(matrix.get());
        UltraCanvasLabel* selectionPtr = selection.get();
        UltraCanvasTabbedContainer* tabsPtr = tabbedContainer.get();
        auto active = [matrixPtrs, tabsPtr]() -> UltraCanvasPerformanceMatrix* {
            const int index = tabsPtr->GetActiveTab();
            return (index >= 0 && index < static_cast<int>(matrixPtrs.size())) ? matrixPtrs[static_cast<size_t>(index)]
                                                                               : nullptr;
        };

        // The list's rows, as indices into the items of the matrix they came from.
        struct Listed {
            UltraCanvasPerformanceMatrix* matrix = nullptr;
            std::vector<size_t> items;
        };
        auto listed = std::make_shared<Listed>();
        auto showBox = [boxModel, listed](UltraCanvasPerformanceMatrix* matrix, const PerformanceCellRef& ref) {
            listed->matrix = matrix;
            listed->items = matrix->GetCellItems(ref);
            std::vector<ListItem> rows;
            const std::vector<PerformanceMatrixItem>& items = matrix->GetModel().items;
            // Names only when the matrix shows them.
            if (!matrix->IsAnonymised()) {
                for (size_t k : listed->items) {
                    const PerformanceMatrixItem& item = items[k];
                    rows.emplace_back(item.group.empty() ? item.label : item.label + "  \xC2\xB7  " + item.group);
                }
            } else {
                listed->items.clear();
            }
            boxModel->SetItems(rows);
        };
        boxList->onItemClicked = [listed, selectionPtr](int row) {
            UltraCanvasPerformanceMatrix* matrix = listed->matrix;
            if (!matrix || row < 0 || row >= static_cast<int>(listed->items.size())) return;
            const size_t index = listed->items[static_cast<size_t>(row)];
            // Selected here, a person hidden behind "+N more" can still be
            // moved with Ctrl+arrows.
            matrix->SetSelectedItem(static_cast<int>(index));
            selectionPtr->SetText(matrix->BuildItemTooltip(index));
        };

        for (size_t i = 0; i < matrixPtrs.size(); ++i) {
            matrices[i]->onCellClick = [matrixPtrs, i, selectionPtr, showBox](const PerformanceCellRef& ref) {
                UltraCanvasPerformanceMatrix* clicked = matrixPtrs[i];
                selectionPtr->SetText(clicked->GetSelectedCell().IsValid() ? clicked->BuildCellTooltip(ref)
                                                                           : std::string("(nothing selected)"));
                if (!clicked->GetModel().items.empty()) showBox(clicked, ref);
            };
            matrices[i]->onOverflowClick = [matrixPtrs, i, selectionPtr, showBox](const PerformanceCellRef& ref,
                                                                                 const std::vector<size_t>& items) {
                showBox(matrixPtrs[i], ref);
                selectionPtr->SetText(std::to_string(items.size()) + " people in this box: all of them are "
                                      "in the list below.");
            };
            // The veto: a calibration holds the top box to its target share,
            // as a forced distribution would.
            matrices[i]->onItemMove = [matrixPtrs, i, selectionPtr](const PerformanceMatrixItem& item,
                                                                   const PerformanceCellRef& from,
                                                                   const PerformanceCellRef& to) {
                const UltraCanvasPerformanceMatrix* matrix = matrixPtrs[i];
                const PerformanceMatrixModel& model = matrix->GetModel();
                const PerformanceCellRef top(model.Rows() - 1, model.Columns() - 1);
                const size_t cells = static_cast<size_t>(model.Rows() * model.Columns());
                if (to == top && model.targetDistribution.size() == cells) {
                    const std::vector<int> counts = model.CountsPerCell();
                    int total = 0;
                    for (int n : counts) total += n;
                    const int cap = static_cast<int>(std::lround(model.targetDistribution.back() * total));
                    if (counts.back() >= cap) {
                        selectionPtr->SetText("Refused: the top box already holds " + std::to_string(counts.back()) +
                                              ", its target is " + std::to_string(cap) + ". " + item.label +
                                              " stays where they are.");
                        return false;
                    }
                }
                auto name = [&model](const PerformanceCellRef& ref) {
                    const PerformanceCell* cell = model.FindCell(ref.row, ref.col);
                    return cell && !cell->title.empty() ? cell->title
                                                        : model.yAxis.levels[static_cast<size_t>(ref.row)].label + " / " +
                                                          model.xAxis.levels[static_cast<size_t>(ref.col)].label;
                };
                selectionPtr->SetText("Moved " + item.label + " from " + name(from) + " to " + name(to) +
                                      ". The scores stay in the data beside the new box.");
                return true;
            };
            matrices[i]->onItemClick = [matrixPtrs, i, selectionPtr](const PerformanceMatrixItem& item) {
                const UltraCanvasPerformanceMatrix* clicked = matrixPtrs[i];
                const std::vector<PerformanceMatrixItem>& items = clicked->GetModel().items;
                for (size_t k = 0; k < items.size(); ++k) {
                    if (&items[k] == &item) selectionPtr->SetText(clicked->BuildItemTooltip(k));
                }
            };
        }
        designSelector->onSelectionChanged = [active](int, const DropdownItem& item) {
            UltraCanvasPerformanceMatrix* matrix = active();
            if (!matrix) return;
            if (item.value == "tiles") matrix->SetDesign(PerformanceMatrixDesign::Tiles);
            else if (item.value == "tabs") matrix->SetDesign(PerformanceMatrixDesign::HeaderTabs);
            else if (item.value == "cards") matrix->SetDesign(PerformanceMatrixDesign::Cards);
            else matrix->SetDesign(PerformanceMatrixDesign::Grid);
        };
        coloringSelector->onSelectionChanged = [active](int, const DropdownItem& item) {
            UltraCanvasPerformanceMatrix* matrix = active();
            if (!matrix) return;
            if (item.value == "row") matrix->SetCellColoring(PerformanceCellColoring::ByRow);
            else if (item.value == "column") matrix->SetCellColoring(PerformanceCellColoring::ByColumn);
            else if (item.value == "plain") matrix->SetCellColoring(PerformanceCellColoring::Plain);
            else if (item.value == "count") matrix->SetCellColoring(PerformanceCellColoring::ByCount);
            else matrix->SetCellColoring(PerformanceCellColoring::DiagonalBands);
        };
        paletteSelector->onSelectionChanged = [active](int, const DropdownItem& item) {
            UltraCanvasPerformanceMatrix* matrix = active();
            if (!matrix) return;
            if (item.value == "vivid") matrix->SetPalette(PerformanceMatrixPalette::Vivid());
            else if (item.value == "mono") matrix->SetPalette(PerformanceMatrixPalette::Monochrome(Color(36, 104, 178, 255)));
            else if (item.value == "pastel") matrix->SetPalette(PerformanceMatrixPalette::Pastel());
            else matrix->SetPalette(PerformanceMatrixPalette::TrafficLight());
        };

        peopleSelector->onSelectionChanged = [active](int, const DropdownItem& item) {
            UltraCanvasPerformanceMatrix* matrix = active();
            if (!matrix) return;
            matrix->SetAnonymised(item.value == "anonymised");
            if (item.value == "percent") matrix->SetPopulationDisplay(PerformancePopulationDisplay::Percent);
            else if (item.value == "dots") matrix->SetPopulationDisplay(PerformancePopulationDisplay::Dots);
            else if (item.value == "names" || item.value == "anonymised") {
                matrix->SetPopulationDisplay(PerformancePopulationDisplay::Names);
            } else if (item.value == "hidden") matrix->SetPopulationDisplay(PerformancePopulationDisplay::Hidden);
            else matrix->SetPopulationDisplay(PerformancePopulationDisplay::Count);
        };

        allowMoves->onStateChanged = [matrixPtrs](CheckedState, CheckedState state) {
            for (UltraCanvasPerformanceMatrix* matrix : matrixPtrs) {
                matrix->SetAllowItemDrag(state == CheckedState::Checked);
            }
        };

        // ===== FILES =====
        auto fileName = [](const std::string& path) { return PathToUtf8(PathFromUtf8(path).filename()); };
        auto readText = [selectionPtr, fileName](const std::string& path, std::string& text) {
            const FileBytesResult loaded = UltraCanvasFileLoader::LoadFile(path);
            if (!loaded.success) {
                selectionPtr->SetText("Could not open " + fileName(path) + ": " + loaded.error);
                return false;
            }
            text.assign(loaded.bytes.begin(), loaded.bytes.end());
            return true;
        };
        auto writeText = [selectionPtr, fileName](const std::string& path, const std::string& text,
                                                  const std::string& what) {
            std::string error;
            if (UltraCanvasFileLoader::SaveFile(path, std::vector<uint8_t>(text.begin(), text.end()), error)) {
                selectionPtr->SetText("Saved " + what + " to " + fileName(path) + ".");
            } else {
                selectionPtr->SetText("Could not write " + fileName(path) + ": " + error);
            }
        };

        // Open JSON... replaces the Calibration tab's model, which shows the
        // people; the tab is brought forward.
        openJsonButton->onClick = [matrixPtrs, tabsPtr, selectionPtr, readText, fileName]() {
            FileDialogOptions opts;
            opts.SetTitle("Open a performance matrix")
                .AddFilter("Performance matrix (*.json)", "json")
                .AddFilter("All files", "*");
            UltraCanvasFileLoader::OpenFileDialog(opts,
                    [matrixPtrs, tabsPtr, selectionPtr, readText, fileName](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                std::string text, error;
                if (!readText(path, text)) return;
                PerformanceMatrixModel model;
                if (!ImportPerformanceMatrixJson(text, model, &error)) {
                    selectionPtr->SetText("Could not read " + fileName(path) + ": " + error);
                    return;
                }
                matrixPtrs.front()->SetModel(model);
                tabsPtr->SetActiveTab(0);
                selectionPtr->SetText("Opened " + fileName(path) + ": " + std::to_string(model.items.size()) +
                                      " people in a " + std::to_string(model.Rows()) + "x" +
                                      std::to_string(model.Columns()) + " grid.");
            });
        };

        saveJsonButton->onClick = [active, writeText]() {
            UltraCanvasPerformanceMatrix* matrix = active();
            if (!matrix) return;
            FileDialogOptions opts;
            opts.SetTitle("Save the performance matrix")
                .AddFilter("Performance matrix (*.json)", "json")
                .SetDefaultExtension("json")
                .SetDefaultFileName("performance-matrix.json");
            // The model is copied now: a move made while the dialog is open
            // belongs to the next save.
            const std::string text = ExportPerformanceMatrixJson(matrix->GetModel());
            UltraCanvasFileLoader::SaveFileDialog(opts, [text, writeText](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                writeText(path, text, "the matrix");
            });
        };

        // Import people (CSV)... places them on the axes of the tab shown.
        importCsvButton->onClick = [active, selectionPtr, readText, fileName]() {
            UltraCanvasPerformanceMatrix* matrix = active();
            if (!matrix) return;
            FileDialogOptions opts;
            opts.SetTitle("Import people")
                .AddFilter("Comma-separated values (*.csv)", "csv")
                .AddFilter("All files", "*");
            UltraCanvasFileLoader::OpenFileDialog(opts,
                    [matrix, selectionPtr, readText, fileName](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                std::string text, error;
                if (!readText(path, text)) return;
                PerformanceMatrixModel model = matrix->GetModel();
                if (!ImportPerformanceItemsCsv(text, model, &error)) {
                    selectionPtr->SetText("Could not import " + fileName(path) + ": " + error);
                    return;
                }
                matrix->SetModel(model);
                matrix->SetShowTotals(true);
                selectionPtr->SetText("Imported " + std::to_string(model.items.size()) + " people from " +
                                      fileName(path) + ".");
            });
        };

        exportCsvButton->onClick = [active, selectionPtr, writeText]() {
            UltraCanvasPerformanceMatrix* matrix = active();
            if (!matrix) return;
            if (matrix->GetModel().items.empty()) {
                selectionPtr->SetText("This tab has no people to export. Calibration has.");
                return;
            }
            FileDialogOptions opts;
            opts.SetTitle("Export the people")
                .AddFilter("Comma-separated values (*.csv)", "csv")
                .SetDefaultExtension("csv")
                .SetDefaultFileName("people.csv");
            const std::string text = ExportPerformanceItemsCsv(matrix->GetModel());
            UltraCanvasFileLoader::SaveFileDialog(opts, [text, writeText](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                writeText(path, text, "the people");
            });
        };

        return mainContainer;
    }

} // namespace UltraCanvas
