// Apps/DemoApp/UltraCanvasTernaryChartExamples.cpp
// Ternary chart examples: three-party vote shares by region (read
// anticlockwise), vote swings as paths, soil samples over the USDA texture
// classes (read clockwise) and a De Finetti diagram
// Version: 1.2.0 - Phase 3: the Density tab, a density control, drag-to-zoom
//   on every tab with a button and a read-out of the zoom
// Version: 1.1.0 - Phase 2: the Vote swings, Soil texture and De Finetti tabs,
//   the reading guide, region clicks, Open / Save JSON and Import / Export CSV
//   through UltraCanvasFileLoader
// Version: 1.0.0
// Last Modified: 2026-11-05
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasTernaryChart.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"   // PathToUtf8 / PathFromUtf8 for the file name
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTernaryChartExamples() {
        auto page = DemoKit::CreatePage("ternaryMainContainer", Tr("Ternary Chart"),
                Tr("Things made of three parts - votes, soil, alloys - as points in a triangle: each "
                "corner is all of one part, and the three shares of every point add up to 100%"));

        // The chart beside its controls; a narrow window puts the controls under it.
        auto row = DemoKit::AddRow(page, "ternaryContent");

        // ===== TABS =====
        auto chartCard = DemoKit::AddCard(row, "ternaryChartCard");
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("ternaryTabs", 0, 0, 760, 640);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        DemoKit::AddFixed(chartCard, tabbedContainer);

        struct Example {
            std::string tab;
            TernaryModel model;
            bool density = false;   // the feldspar cloud: counted, not marked
        };
        TernaryModel soil = TernarySamples::SoilTexture();
        soil.title = Tr("Soil samples over the USDA texture classes");
        soil.subtitle = Tr("Invented samples; click a class to count them");
        soil.series = TernarySamples::SoilSamples().series;
        const std::vector<Example> examples = {
            {Tr("Votes"), TernarySamples::ThreePartyVotes()},
            {Tr("Vote swings"), TernarySamples::VoteTrajectories()},
            {Tr("Soil texture"), soil},
            {Tr("De Finetti"), TernarySamples::DeFinetti()},
            {Tr("Density"), TernarySamples::FeldsparCloud(), true},
        };
        std::vector<std::shared_ptr<UltraCanvasTernaryChart>> charts;
        for (size_t i = 0; i < examples.size(); ++i) {
            auto chart = CreateTernaryChart("ternaryChart" + std::to_string(i), 10, 10, 740, 580);
            chart->SetModel(examples[i].model);
            if (examples[i].density) {
                // 2,400 markers would hide one another: count them instead.
                chart->SetDensity(15);
                chart->SetDensityContours(4);
                chart->SetShowPoints(false);
            }
            auto tabContent = std::make_shared<UltraCanvasContainer>("ternaryTab" + std::to_string(i), 0, 0, 760, 602);
            tabContent->SetBackgroundColor(Color(255, 255, 255, 255));
            tabContent->AddChild(chart);
            tabbedContainer->AddTab(examples[i].tab, tabContent);
            charts.push_back(chart);
        }

        // ===== SIDEBAR =====
        auto sidebar = DemoKit::AddCard(row, "ternarySidebar", "", "", 240);
        auto addCaption = [&](const std::string& id, const std::string& text) {
            DemoKit::AddCaption(sidebar, id, text)->SetFontWeight(FontWeight::Bold);
        };

        addCaption("ternaryReadingCaption", Tr("Reading direction"));
        auto readingSelector = std::make_shared<UltraCanvasDropdown>("ternaryReadingSelector", 0, 0, 190, 28);
        readingSelector->AddItem(Tr("As the sample reads"), "sample");
        readingSelector->AddItem(Tr("Clockwise"), "clockwise");
        readingSelector->AddItem(Tr("Anticlockwise"), "anticlockwise");
        readingSelector->SetSelectedIndex(0);
        DemoKit::AddStretched(sidebar, readingSelector);

        addCaption("ternaryLabelsCaption", Tr("Point names"));
        auto labelsSelector = std::make_shared<UltraCanvasDropdown>("ternaryLabelsSelector", 0, 0, 190, 28);
        labelsSelector->AddItem(Tr("Hidden (hover for them)"), "hidden");
        labelsSelector->AddItem(Tr("Beside the points"), "shown");
        labelsSelector->SetSelectedIndex(0);
        DemoKit::AddStretched(sidebar, labelsSelector);

        addCaption("ternaryGridCaption", Tr("Grid and titles"));
        auto gridSelector = std::make_shared<UltraCanvasDropdown>("ternaryGridSelector", 0, 0, 190, 28);
        gridSelector->AddItem(Tr("Grid and edge titles"), "both");
        gridSelector->AddItem(Tr("Grid only"), "grid");
        gridSelector->AddItem(Tr("Edge titles only"), "titles");
        gridSelector->SetSelectedIndex(0);
        DemoKit::AddStretched(sidebar, gridSelector);

        addCaption("ternaryGuideCaption", Tr("Reading guide"));
        auto guideSelector = std::make_shared<UltraCanvasDropdown>("ternaryGuideSelector", 0, 0, 190, 28);
        guideSelector->AddItem(Tr("Off"), "off");
        guideSelector->AddItem(Tr("Follow the pointer"), "on");
        guideSelector->SetSelectedIndex(0);
        DemoKit::AddStretched(sidebar, guideSelector);

        addCaption("ternaryDensityCaption", Tr("Density"));
        auto densitySelector = std::make_shared<UltraCanvasDropdown>("ternaryDensitySelector", 0, 0, 190, 28);
        densitySelector->AddItem(Tr("As the tab shows it"), "tab");
        densitySelector->AddItem(Tr("Off: the points"), "off");
        densitySelector->AddItem(Tr("Cells"), "cells");
        densitySelector->AddItem(Tr("Cells and contours"), "contours");
        densitySelector->SetSelectedIndex(0);
        DemoKit::AddStretched(sidebar, densitySelector);

        addCaption("ternaryZoomCaption", Tr("Zoom"));
        auto zoomOutButton = std::make_shared<UltraCanvasButton>("ternaryZoomOut", 0, 0, 190, 28,
                                                                 Tr("Show the whole triangle"));
        DemoKit::AddStretched(sidebar, zoomOutButton);

        addCaption("ternaryFilesCaption", Tr("Files"));
        auto makeButton = [&sidebar](const std::string& id, const std::string& text) {
            auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 190, 28, text);
            DemoKit::AddStretched(sidebar, button);
            return button;
        };
        auto openJsonButton = makeButton("ternaryOpenJson", Tr("Open JSON\xE2\x80\xA6"));
        auto saveJsonButton = makeButton("ternarySaveJson", Tr("Save JSON\xE2\x80\xA6"));
        auto importCsvButton = makeButton("ternaryImportCsv", Tr("Import points (CSV)\xE2\x80\xA6"));
        auto exportCsvButton = makeButton("ternaryExportCsv", Tr("Export points (CSV)\xE2\x80\xA6"));

        addCaption("ternarySelectionCaption", Tr("SELECTED"));
        auto selection = DemoKit::AddCaption(sidebar, "ternarySelection",
                Tr("Click a point for its three shares, or a soil class to count the samples in "
                "it. Drag across the triangle to zoom in; double-click to zoom out. Click a "
                "legend entry to hide a series."));
        selection->SetTextColor(DemoKit::BodyText());
        selection->SetBackgroundColor(Color(248, 249, 250, 255));
        selection->SetPadding(10);

        // ===== CALLBACKS (raw pointers: no cycle through the elements) =====
        std::vector<UltraCanvasTernaryChart*> chartPtrs;
        for (auto& chart : charts) chartPtrs.push_back(chart.get());
        UltraCanvasLabel* selectionPtr = selection.get();
        std::vector<TernaryReading> sampleReadings;
        for (const Example& example : examples) sampleReadings.push_back(example.model.reading);

        UltraCanvasTabbedContainer* tabsPtr = tabbedContainer.get();
        auto active = [chartPtrs, tabsPtr]() -> UltraCanvasTernaryChart* {
            const int index = tabsPtr->GetActiveTab();
            return index >= 0 && index < static_cast<int>(chartPtrs.size()) ? chartPtrs[static_cast<size_t>(index)]
                                                                            : nullptr;
        };

        for (size_t i = 0; i < chartPtrs.size(); ++i) {
            charts[i]->onPointClick = [chartPtrs, i, selectionPtr](size_t series, size_t point) {
                selectionPtr->SetText(chartPtrs[i]->BuildPointTooltip(series, point));
            };
            // A class and how many samples fall in it - the reading a soil
            // scientist takes from the triangle.
            charts[i]->onRegionClick = [chartPtrs, i, selectionPtr](int region) {
                const TernaryModel& model = chartPtrs[i]->GetModel();
                int count = 0;
                std::string names;
                for (const TernarySeries& series : model.series) {
                    for (const TernaryPoint& point : series.points) {
                        if (model.RegionOf(point) != region) continue;
                        ++count;
                        names += (names.empty() ? "" : ", ") + point.label;
                    }
                }
                selectionPtr->SetText(chartPtrs[i]->BuildRegionTooltip(region) + "\n" +
                                      (names.empty() ? TrN("{n} sample", "{n} samples", count)
                                                     : LocalisationDetail::Substitute(
                                                           TrN("{n} sample: {0}", "{n} samples: {0}", count), {names})));
            };
        }
        guideSelector->onSelectionChanged = [chartPtrs](int, const DropdownItem& item) {
            for (auto* chart : chartPtrs) chart->SetReadingGuide(item.value == "on");
        };
        densitySelector->onSelectionChanged = [chartPtrs](int, const DropdownItem& item) {
            for (auto* chart : chartPtrs) {
                // Each tab's own setting: on for the feldspar cloud only.
                const bool cloud = chart->GetModel().PointCount() > 1000;
                const bool on = item.value == "cells" || item.value == "contours" || (item.value == "tab" && cloud);
                chart->SetDensity(on ? 15 : 0);
                chart->SetDensityContours(item.value == "contours" || (item.value == "tab" && cloud) ? 4 : 0);
                chart->SetShowPoints(!on);
            }
        };
        zoomOutButton->onClick = [active]() {
            if (UltraCanvasTernaryChart* chart = active()) chart->ResetZoom();
        };
        // The zoom in words: what each part is shown from and to.
        for (size_t i = 0; i < chartPtrs.size(); ++i) {
            charts[i]->onZoomChanged = [chartPtrs, i, selectionPtr]() {
                const TernaryModel& model = chartPtrs[i]->GetModel();
                if (!model.IsZoomed()) {
                    selectionPtr->SetText(Tr("The whole triangle. Drag across it to zoom in."));
                    return;
                }
                std::string text = Tr("Zoomed in:");
                for (const TernaryAxis& axis : model.axes) {
                    text += "\n" + TrF("{0} {1} to {2}", axis.title, FormatTernaryShare(axis.minimum),
                                       FormatTernaryShare(axis.minimum + model.ZoomSpan()));
                }
                selectionPtr->SetText(text + "\n" + Tr("Double-click to zoom out."));
            };
        }

        // ===== FILES (through UltraCanvasFileLoader, as every user file) =====
        auto fileName = [](const std::string& path) { return PathToUtf8(PathFromUtf8(path).filename()); };
        auto readText = [selectionPtr, fileName](const std::string& path, std::string& text) {
            const FileBytesResult loaded = UltraCanvasFileLoader::LoadFile(path);
            if (!loaded.success) {
                selectionPtr->SetText(TrF("Could not open {0}: {1}", fileName(path), loaded.error));
                return false;
            }
            text.assign(loaded.bytes.begin(), loaded.bytes.end());
            return true;
        };
        auto writeText = [selectionPtr, fileName](const std::string& path, const std::string& text) {
            std::string error;
            if (UltraCanvasFileLoader::SaveFile(path, std::vector<uint8_t>(text.begin(), text.end()), error)) {
                selectionPtr->SetText(TrF("Saved {0}.", fileName(path)));
            } else {
                selectionPtr->SetText(TrF("Could not write {0}: {1}", fileName(path), error));
            }
        };

        // Open JSON... replaces the chart of the tab shown.
        openJsonButton->onClick = [active, selectionPtr, readText, fileName]() {
            UltraCanvasTernaryChart* chart = active();
            if (!chart) return;
            FileDialogOptions opts;
            opts.SetTitle(Tr("Open a ternary chart"))
                .AddFilter(Tr("Ternary chart (*.json)"), "json")
                .AddFilter(Tr("All files"), "*");
            UltraCanvasFileLoader::OpenFileDialog(opts,
                    [chart, selectionPtr, readText, fileName](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                std::string text, error;
                if (!readText(path, text)) return;
                TernaryModel model;
                if (!ImportTernaryJson(text, model, &error)) {
                    selectionPtr->SetText(TrF("Could not read {0}: {1}", fileName(path), error));
                    return;
                }
                chart->SetModel(model);
                selectionPtr->SetText(TrF("Opened {0}: {1}, {2}.", fileName(path),
                                          TrN("{n} point", "{n} points", static_cast<long long>(model.PointCount())),
                                          TrN("{n} region", "{n} regions", static_cast<long long>(model.regions.size()))));
            });
        };
        saveJsonButton->onClick = [active, writeText]() {
            UltraCanvasTernaryChart* chart = active();
            if (!chart) return;
            FileDialogOptions opts;
            opts.SetTitle(Tr("Save the ternary chart"))
                .AddFilter(Tr("Ternary chart (*.json)"), "json")
                .SetDefaultExtension("json")
                .SetDefaultFileName("ternary.json");
            const std::string text = ExportTernaryJson(chart->GetModel());
            UltraCanvasFileLoader::SaveFileDialog(opts, [text, writeText](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                writeText(path, text);
            });
        };
        // Import points (CSV)... replaces the points of the tab shown, on its
        // axes and over its regions.
        importCsvButton->onClick = [active, selectionPtr, readText, fileName]() {
            UltraCanvasTernaryChart* chart = active();
            if (!chart) return;
            FileDialogOptions opts;
            opts.SetTitle(Tr("Import points"))
                .AddFilter(Tr("Comma-separated values (*.csv)"), "csv")
                .AddFilter(Tr("All files"), "*");
            UltraCanvasFileLoader::OpenFileDialog(opts,
                    [chart, selectionPtr, readText, fileName](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                std::string text, error;
                if (!readText(path, text)) return;
                TernaryModel model = chart->GetModel();
                if (!ImportTernaryCsv(text, model, &error)) {
                    selectionPtr->SetText(TrF("Could not import {0}: {1}", fileName(path), error));
                    return;
                }
                chart->SetModel(model);
                selectionPtr->SetText(LocalisationDetail::Substitute(
                        TrN("Imported {n} point from {0}.", "Imported {n} points from {0}.",
                            static_cast<long long>(model.PointCount())), {fileName(path)}));
            });
        };
        exportCsvButton->onClick = [active, writeText]() {
            UltraCanvasTernaryChart* chart = active();
            if (!chart) return;
            FileDialogOptions opts;
            opts.SetTitle(Tr("Export the points"))
                .AddFilter(Tr("Comma-separated values (*.csv)"), "csv")
                .SetDefaultExtension("csv")
                .SetDefaultFileName("points.csv");
            const std::string text = ExportTernaryCsv(chart->GetModel());
            UltraCanvasFileLoader::SaveFileDialog(opts, [text, writeText](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                writeText(path, text);
            });
        };
        readingSelector->onSelectionChanged = [chartPtrs, sampleReadings](int, const DropdownItem& item) {
            for (size_t i = 0; i < chartPtrs.size(); ++i) {
                if (item.value == "clockwise") chartPtrs[i]->SetReading(TernaryReading::Clockwise);
                else if (item.value == "anticlockwise") chartPtrs[i]->SetReading(TernaryReading::Anticlockwise);
                else chartPtrs[i]->SetReading(sampleReadings[i]);
            }
        };
        labelsSelector->onSelectionChanged = [chartPtrs](int, const DropdownItem& item) {
            for (auto* chart : chartPtrs) chart->SetShowPointLabels(item.value == "shown");
        };
        gridSelector->onSelectionChanged = [chartPtrs](int, const DropdownItem& item) {
            for (auto* chart : chartPtrs) {
                chart->SetShowGrid(item.value != "titles");
                chart->SetShowEdgeTitles(item.value != "grid");
            }
        };

        return page;
    }

} // namespace UltraCanvas
