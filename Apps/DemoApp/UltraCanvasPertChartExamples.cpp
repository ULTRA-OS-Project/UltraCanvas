// Apps/DemoApp/UltraCanvasPertChartExamples.cpp
// Interactive PERT chart demo - software project network with live design,
// palette and connector switching plus schedule statistics.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the chart
//                  in a card that fills the area, the control bar a row that
//                  wraps, every label, checkbox and button sized to its text
// Version: 1.0.0
// Last Modified: 2026-10-23
//
// Changelog:
//   1.0.0 - Initial release. One pre-built software project network
//           (design/programming/test phases with team color-coding), a
//           control bar that switches node design, color palette and
//           connector routing live, toggles for critical path / legend /
//           dates / slack, and a status line with the computed project
//           duration, critical path and on-time probability.

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Diagrams/UltraCanvasPertChart.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"

#include <sstream>
#include <iomanip>

namespace UltraCanvas {

namespace {

// Builds the demo project: a software delivery network mirroring the classic
// PERT template (plan -> parallel design tracks -> programming -> merge ->
// test -> UAT) with three-point estimates on the risky activities.
void PopulateSoftwareProject(const std::shared_ptr<UltraCanvasPertChart>& chart) {
    chart->DefineGroup("design", "Design Team");
    chart->DefineGroup("prog", "Programming Team");
    chart->DefineGroup("both", "Both Teams");

    chart->AddMilestone("start", "Project Start");
    chart->SetActivityCode("start", "01");

    chart->AddActivity("plan", "Develop Plan", 2, "02");
    chart->SetActivityDates("plan", "01/03/26", "01/05/26");
    chart->SetActivityResponsible("plan", "PM Office");
    chart->SetActivityGroup("plan", "both");

    chart->AddActivity("designUI", "Design UI", 3, "03");
    chart->SetActivityDates("designUI", "01/05/26", "01/08/26");
    chart->SetActivityResponsible("designUI", "A. Painter");
    chart->SetActivityGroup("designUI", "design");

    chart->AddActivity("designAPI", "Design API", 5, "04");
    chart->SetActivityDates("designAPI", "01/05/26", "01/10/26");
    chart->SetActivityResponsible("designAPI", "B. Architect");
    chart->SetActivityGroup("designAPI", "design");

    chart->AddActivity("designDB", "Design Schema", 4, "05");
    chart->SetActivityDates("designDB", "01/05/26", "01/09/26");
    chart->SetActivityResponsible("designDB", "C. Modeler");
    chart->SetActivityGroup("designDB", "design");

    chart->AddActivity("progUI", "Implement UI", 6, "06");
    chart->SetActivityDates("progUI", "01/08/26", "01/14/26");
    chart->SetActivityResponsible("progUI", "D. Coder");
    chart->SetActivityGroup("progUI", "prog");

    chart->AddActivity("progAPI", "Implement API", 8, "07");
    chart->SetActivityThreePointEstimate("progAPI", 6, 8, 13);
    chart->SetActivityDates("progAPI", "01/10/26", "01/19/26");
    chart->SetActivityResponsible("progAPI", "E. Coder");
    chart->SetActivityGroup("progAPI", "prog");

    chart->AddActivity("progDB", "Implement DB", 5, "08");
    chart->SetActivityDates("progDB", "01/09/26", "01/14/26");
    chart->SetActivityResponsible("progDB", "F. Coder");
    chart->SetActivityGroup("progDB", "prog");

    chart->AddActivity("merge", "Merge Tasks", 2, "09");
    chart->SetActivityDates("merge", "01/19/26", "01/21/26");
    chart->SetActivityResponsible("merge", "Integration");
    chart->SetActivityGroup("merge", "both");

    chart->AddActivity("test", "Test Tasks", 6, "10");
    chart->SetActivityThreePointEstimate("test", 4, 6, 10);
    chart->SetActivityDates("test", "01/21/26", "01/27/26");
    chart->SetActivityResponsible("test", "QA Team");
    chart->SetActivityGroup("test", "both");

    chart->AddMilestone("end", "Project End");
    chart->SetActivityCode("end", "11");

    chart->AddDependency("d1", "start", "plan");
    chart->AddDependency("d2", "plan", "designUI");
    chart->AddDependency("d3", "plan", "designAPI");
    chart->AddDependency("d4", "plan", "designDB");
    chart->AddDependency("d5", "designUI", "progUI");
    chart->AddDependency("d6", "designAPI", "progAPI");
    chart->AddDependency("d7", "designDB", "progDB");
    chart->AddDependency("d8", "progUI", "merge");
    chart->AddDependency("d9", "progAPI", "merge");
    chart->AddDependency("d10", "progDB", "merge");
    // UI implementation informs testing directly (logical link, no duration).
    chart->AddDummyDependency("d11", "progUI", "test");
    chart->AddDependency("d12", "merge", "test");
    chart->AddDependency("d13", "test", "end");
}

// Takes the chart raw: its one caller is a callback the chart itself owns,
// which cannot hold a shared_ptr back to it without making a cycle.
std::string BuildScheduleSummary(const UltraCanvasPertChart* chart) {
    std::ostringstream oss;
    oss << "Project duration: " << std::fixed << std::setprecision(1)
        << chart->GetProjectDuration() << " " << chart->GetDurationUnit();

    auto path = chart->GetCriticalPath();
    oss << "   |   Critical path: ";
    for (size_t i = 0; i < path.size(); ++i) {
        const auto* a = chart->GetActivity(path[i]);
        oss << (a ? a->name : path[i]);
        if (i + 1 < path.size()) oss << " > ";
    }

    double target = chart->GetProjectDuration() + 2.0;
    oss << "   |   P(done in " << std::setprecision(0) << target << " "
        << chart->GetDurationUnit() << "): " << std::setprecision(0)
        << chart->GetCompletionProbability(target) * 100.0 << "%";
    return oss.str();
}

// A button as wide as its text, at least `minWidth` (the width it had).
std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id, const std::string& text,
                                              float minWidth) {
    auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 28, text);
    CSSLayout::BoxConstraints limits;
    limits.minWidth = CSSLayout::Dimension::Px(minWidth);
    button->boxConstraints = limits;
    return button;
}

} // namespace

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================
std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePertChartExamples() {
    auto container = DemoKit::CreatePage("PertChartDemo",
        "PERT Chart - Project Network with Critical Path Analysis",
        "Drag activities, scroll to zoom, drag empty space to pan. "
        "Switch node design and color palette live; the critical path is computed automatically.");
    // The chart pans and zooms itself: the page is the area's height and the
    // chart takes what the controls and the status line leave.
    container->SetFillsArea(true);

    auto card = DemoKit::CreateCard("PertChartCard");
    card->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    DemoKit::Add(container, card, 1);

    // The control bar: wraps onto a second line when the window is narrow.
    auto controls = DemoKit::AddRow(card, "PertControls", 8);
    controls->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);

    auto chart = CreatePertChart("pertChart", 0, 0, 1340, 700);
    PopulateSoftwareProject(chart);
    chart->SetNodeDesign(PertNodeDesign::Card);
    chart->SetPalette(PertChartPaletteKind::Classic);
    chart->SetGridVisible(true, 20.0);
    chart->size.width  = CSSLayout::Dimension::Auto();
    chart->size.height = CSSLayout::Dimension::Auto();
    chart->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    DemoKit::Add(card, chart, 1);

    auto status = DemoKit::AddText(card, "PertStatus", "");
    status->SetTextColor(Color(70, 76, 88, 255));

    // Keep the status line in sync with every schedule recomputation.
    chart->onScheduleComputed = [chart = chart.get(), statusWeak = std::weak_ptr<UltraCanvasLabel>(status)](double) {
        if (auto s = statusWeak.lock()) {
            s->SetText(BuildScheduleSummary(chart));
        }
    };
    chart->ComputeSchedule();

    // ---------------- Control bar ----------------
    DemoKit::Add(controls, DemoKit::CreateText("LblDesign", "Design:"));

    auto designDrop = std::make_shared<UltraCanvasDropdown>("DropDesign", 0, 0, 130, 28);
    designDrop->AddItem("Card");
    designDrop->AddItem("Detailed Card");
    designDrop->AddItem("Compact");
    designDrop->AddItem("Circle (AOA)");
    designDrop->AddItem("CPM Matrix");
    designDrop->SetSelectedIndex(0, false);
    designDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        static const PertNodeDesign kDesigns[] = {
            PertNodeDesign::Card, PertNodeDesign::DetailedCard,
            PertNodeDesign::Compact, PertNodeDesign::Circle,
            PertNodeDesign::Custom // default template = CpmMatrix
        };
        if (index >= 0 && index < 5) chart->SetNodeDesign(kDesigns[index]);
    };
    DemoKit::AddFixed(controls, designDrop);

    DemoKit::Add(controls, DemoKit::CreateText("LblPalette", "Palette:"));

    auto paletteDrop = std::make_shared<UltraCanvasDropdown>("DropPalette", 0, 0, 130, 28);
    paletteDrop->AddItem("Classic");
    paletteDrop->AddItem("Ocean");
    paletteDrop->AddItem("Vibrant");
    paletteDrop->AddItem("Pastel");
    paletteDrop->AddItem("Mint");
    paletteDrop->AddItem("Midnight");
    paletteDrop->AddItem("Dark");
    paletteDrop->AddItem("Monochrome");
    paletteDrop->SetSelectedIndex(0, false);
    paletteDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        static const PertChartPaletteKind kPalettes[] = {
            PertChartPaletteKind::Classic, PertChartPaletteKind::Ocean,
            PertChartPaletteKind::Vibrant, PertChartPaletteKind::Pastel,
            PertChartPaletteKind::Mint, PertChartPaletteKind::Midnight,
            PertChartPaletteKind::Dark, PertChartPaletteKind::Monochrome
        };
        if (index >= 0 && index < 8) chart->SetPalette(kPalettes[index]);
    };
    DemoKit::AddFixed(controls, paletteDrop);

    DemoKit::Add(controls, DemoKit::CreateText("LblConn", "Connectors:"));

    auto connDrop = std::make_shared<UltraCanvasDropdown>("DropConn", 0, 0, 115, 28);
    connDrop->AddItem("Orthogonal");
    connDrop->AddItem("Straight");
    connDrop->AddItem("Curved");
    connDrop->SetSelectedIndex(0, false);
    connDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        static const PertConnectorStyle kStyles[] = {
            PertConnectorStyle::Orthogonal, PertConnectorStyle::Straight,
            PertConnectorStyle::Curved
        };
        if (index >= 0 && index < 3) chart->SetConnectorStyle(kStyles[index]);
    };
    DemoKit::AddFixed(controls, connDrop);

    auto chkCritical = std::make_shared<UltraCanvasCheckbox>("ChkCritical", 0, 0, 0, 24, "Critical path");
    chkCritical->SetChecked(true);
    chkCritical->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetCriticalPathHighlight(newState == CheckedState::Checked);
    };
    DemoKit::Add(controls, chkCritical);

    auto chkLegend = std::make_shared<UltraCanvasCheckbox>("ChkLegend", 0, 0, 0, 24, "Legend");
    chkLegend->SetChecked(true);
    chkLegend->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetLegendVisible(newState == CheckedState::Checked);
    };
    DemoKit::Add(controls, chkLegend);

    auto chkDates = std::make_shared<UltraCanvasCheckbox>("ChkDates", 0, 0, 0, 24, "Dates");
    chkDates->SetChecked(true);
    chkDates->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowDates(newState == CheckedState::Checked);
    };
    DemoKit::Add(controls, chkDates);

    auto chkSlack = std::make_shared<UltraCanvasCheckbox>("ChkSlack", 0, 0, 0, 24, "Slack");
    chkSlack->SetChecked(false);
    chkSlack->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowSlack(newState == CheckedState::Checked);
    };
    DemoKit::Add(controls, chkSlack);

    auto chkNameCircle = std::make_shared<UltraCanvasCheckbox>("ChkNameCircle", 0, 0, 0, 24, "Name in circle");
    chkNameCircle->SetChecked(false);
    chkNameCircle->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetCircleLabelMode(newState == CheckedState::Checked
                                      ? PertCircleLabel::Name : PertCircleLabel::Code);
    };
    DemoKit::Add(controls, chkNameCircle);

    auto btnLayout = MakeButton("BtnLayout", "Auto Layout", 105);
    btnLayout->SetOnClick([chart]() { chart->AutoLayout(true); });
    DemoKit::Add(controls, btnLayout);

    auto btnFit = MakeButton("BtnFit", "Fit to View", 100);
    btnFit->SetOnClick([chart]() { chart->FitToView(); });
    DemoKit::Add(controls, btnFit);

    return container;
}

} // namespace UltraCanvas
