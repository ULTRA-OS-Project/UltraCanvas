// Apps/DemoApp/UltraCanvasPertChartExamples.cpp
// Interactive PERT chart demo - software project network with live design,
// palette and connector switching plus schedule statistics.
// Version: 1.0.0
//
// Changelog:
//   1.0.0 - Initial release. One pre-built software project network
//           (design/programming/test phases with team color-coding), a
//           control bar that switches node design, color palette and
//           connector routing live, toggles for critical path / legend /
//           dates / slack, and a status line with the computed project
//           duration, critical path and on-time probability.

#include "UltraCanvasDemo.h"
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

std::string BuildScheduleSummary(const std::shared_ptr<UltraCanvasPertChart>& chart) {
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

} // namespace

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================
std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePertChartExamples() {
    auto container = std::make_shared<UltraCanvasContainer>("PertChartDemo", 0, 0, 1400, 970);
    container->SetBackgroundColor(Color(245, 247, 250, 255));

    auto heading = std::make_shared<UltraCanvasLabel>("PertHeading", 30, 12, 1100, 28);
    heading->SetText("PERT Chart - Project Network with Critical Path Analysis");
    heading->SetFont("Arial", 18.0f, FontWeight::Bold);
    heading->SetTextColor(Color(30, 30, 30, 255));
    container->AddChild(heading);

    auto desc = std::make_shared<UltraCanvasLabel>("PertDesc", 30, 44, 1300, 20);
    desc->SetText("Drag activities, scroll to zoom, drag empty space to pan. "
                  "Switch node design and color palette live; the critical path is computed automatically.");
    desc->SetFontSize(13);
    desc->SetTextColor(Color(100, 100, 100, 255));
    container->AddChild(desc);

    auto chart = CreatePertChart("pertChart", 30, 120, 1340, 700);
    PopulateSoftwareProject(chart);
    chart->SetNodeDesign(PertNodeDesign::Card);
    chart->SetPalette(PertChartPaletteKind::Classic);
    chart->SetGridVisible(true, 20.0);
    container->AddChild(chart);

    auto status = std::make_shared<UltraCanvasLabel>("PertStatus", 30, 830, 1340, 22);
    status->SetFontSize(12);
    status->SetTextColor(Color(70, 76, 88, 255));
    container->AddChild(status);

    // Keep the status line in sync with every schedule recomputation.
    chart->onScheduleComputed = [chart, statusWeak = std::weak_ptr<UltraCanvasLabel>(status)](double) {
        if (auto s = statusWeak.lock()) {
            s->SetText(BuildScheduleSummary(chart));
        }
    };
    chart->ComputeSchedule();

    // ---------------- Control bar ----------------
    int ctrlY = 74;

    auto designLabel = std::make_shared<UltraCanvasLabel>("LblDesign", 30, ctrlY + 5, 50, 20);
    designLabel->SetText("Design:");
    designLabel->SetFontSize(12);
    container->AddChild(designLabel);

    auto designDrop = std::make_shared<UltraCanvasDropdown>("DropDesign", 84, ctrlY, 130, 28);
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
    container->AddChild(designDrop);

    auto paletteLabel = std::make_shared<UltraCanvasLabel>("LblPalette", 232, ctrlY + 5, 52, 20);
    paletteLabel->SetText("Palette:");
    paletteLabel->SetFontSize(12);
    container->AddChild(paletteLabel);

    auto paletteDrop = std::make_shared<UltraCanvasDropdown>("DropPalette", 288, ctrlY, 130, 28);
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
    container->AddChild(paletteDrop);

    auto connLabel = std::make_shared<UltraCanvasLabel>("LblConn", 436, ctrlY + 5, 74, 20);
    connLabel->SetText("Connectors:");
    connLabel->SetFontSize(12);
    container->AddChild(connLabel);

    auto connDrop = std::make_shared<UltraCanvasDropdown>("DropConn", 514, ctrlY, 115, 28);
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
    container->AddChild(connDrop);

    auto chkCritical = std::make_shared<UltraCanvasCheckbox>("ChkCritical", 650, ctrlY + 2, 110, 24);
    chkCritical->SetText("Critical path");
    chkCritical->SetChecked(true);
    chkCritical->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetCriticalPathHighlight(newState == CheckedState::Checked);
    };
    container->AddChild(chkCritical);

    auto chkLegend = std::make_shared<UltraCanvasCheckbox>("ChkLegend", 768, ctrlY + 2, 80, 24);
    chkLegend->SetText("Legend");
    chkLegend->SetChecked(true);
    chkLegend->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetLegendVisible(newState == CheckedState::Checked);
    };
    container->AddChild(chkLegend);

    auto chkDates = std::make_shared<UltraCanvasCheckbox>("ChkDates", 856, ctrlY + 2, 70, 24);
    chkDates->SetText("Dates");
    chkDates->SetChecked(true);
    chkDates->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowDates(newState == CheckedState::Checked);
    };
    container->AddChild(chkDates);

    auto chkSlack = std::make_shared<UltraCanvasCheckbox>("ChkSlack", 934, ctrlY + 2, 70, 24);
    chkSlack->SetText("Slack");
    chkSlack->SetChecked(false);
    chkSlack->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowSlack(newState == CheckedState::Checked);
    };
    container->AddChild(chkSlack);

    auto chkNameCircle = std::make_shared<UltraCanvasCheckbox>("ChkNameCircle", 1012, ctrlY + 2, 122, 24);
    chkNameCircle->SetText("Name in circle");
    chkNameCircle->SetChecked(false);
    chkNameCircle->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetCircleLabelMode(newState == CheckedState::Checked
                                      ? PertCircleLabel::Name : PertCircleLabel::Code);
    };
    container->AddChild(chkNameCircle);

    auto btnLayout = std::make_shared<UltraCanvasButton>("BtnLayout", 1146, ctrlY, 105, 28);
    btnLayout->SetText("Auto Layout");
    btnLayout->SetOnClick([chart]() { chart->AutoLayout(true); });
    container->AddChild(btnLayout);

    auto btnFit = std::make_shared<UltraCanvasButton>("BtnFit", 1263, ctrlY, 100, 28);
    btnFit->SetText("Fit to View");
    btnFit->SetOnClick([chart]() { chart->FitToView(); });
    container->AddChild(btnFit);

    return container;
}

} // namespace UltraCanvas
