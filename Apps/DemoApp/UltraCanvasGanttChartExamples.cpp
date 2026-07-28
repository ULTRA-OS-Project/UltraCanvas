// Apps/DemoApp/UltraCanvasGanttChartExamples.cpp
// Gantt chart demo: one project schedule rendered through several design
// presets and palettes, showing hierarchy, dependencies, milestones,
// progress, critical path, and the interactive timeline.
// Version: 1.0.0
// Last Modified: 2026-07-26
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasGanttChart.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDemo.h"
#include <memory>

namespace UltraCanvas {

namespace {

    // Product-launch schedule with two phase levels, milestones,
    // dependencies, progress, assignees, and priorities. Each chart gets its
    // own instance because expand/collapse state lives on the tasks.
    std::shared_ptr<GanttDataSource> BuildLaunchProject() {
        auto ds = std::make_shared<GanttDataSource>();

        int planning = ds->AddTask("Planning", GanttDate(2026, 2, 2), GanttDate(2026, 2, 13));
        int t11 = ds->AddTask("Vision & strategy", GanttDate(2026, 2, 2), GanttDate(2026, 2, 5), planning);
        int t12 = ds->AddTask("Market research", GanttDate(2026, 2, 4), GanttDate(2026, 2, 10), planning);
        int t13 = ds->AddTask("Budget approval", GanttDate(2026, 2, 11), GanttDate(2026, 2, 13), planning);
        ds->SetTaskProgress(t11, 1.0f);
        ds->SetTaskProgress(t12, 1.0f);
        ds->SetTaskProgress(t13, 0.8f);
        ds->SetTaskAssignee(t11, "Alex Moro");
        ds->SetTaskAssignee(t12, "Dana Reyes");
        ds->SetTaskAssignee(t13, "Alex Moro");
        ds->SetTaskPriority(t13, GanttPriority::High);

        int design = ds->AddTask("Design", GanttDate(2026, 2, 16), GanttDate(2026, 3, 6));
        int t21 = ds->AddTask("Concept sketches", GanttDate(2026, 2, 16), GanttDate(2026, 2, 20), design);
        int t22 = ds->AddTask("Prototype", GanttDate(2026, 2, 23), GanttDate(2026, 3, 3), design);
        int t23 = ds->AddTask("Design review", GanttDate(2026, 3, 4), GanttDate(2026, 3, 6), design);
        ds->SetTaskProgress(t21, 1.0f);
        ds->SetTaskProgress(t22, 0.55f);
        ds->SetTaskAssignee(t21, "Iris Chen");
        ds->SetTaskAssignee(t22, "Iris Chen");
        ds->SetTaskAssignee(t23, "Sam Ortiz");
        ds->SetTaskPriority(t22, GanttPriority::Normal);
        int msDesign = ds->AddMilestone("Design freeze", GanttDate(2026, 3, 6), design);

        int build = ds->AddTask("Implementation", GanttDate(2026, 3, 9), GanttDate(2026, 4, 10));
        int t31 = ds->AddTask("Core engine", GanttDate(2026, 3, 9), GanttDate(2026, 3, 27), build);
        int t32 = ds->AddTask("Integrations", GanttDate(2026, 3, 23), GanttDate(2026, 4, 3), build);
        int t33 = ds->AddTask("QA & bugfixing", GanttDate(2026, 3, 30), GanttDate(2026, 4, 10), build);
        ds->SetTaskProgress(t31, 0.35f);
        ds->SetTaskAssignee(t31, "Priya Nair");
        ds->SetTaskAssignee(t32, "Leo Brandt");
        ds->SetTaskAssignee(t33, "QA Team");
        ds->SetTaskPriority(t31, GanttPriority::Urgent);
        ds->SetTaskPriority(t33, GanttPriority::High);
        ds->SetTaskNotes(t31, "Includes the rendering pipeline rewrite");

        int launch = ds->AddTask("Launch", GanttDate(2026, 4, 13), GanttDate(2026, 4, 24));
        int t41 = ds->AddTask("Marketing campaign", GanttDate(2026, 4, 13), GanttDate(2026, 4, 24), launch);
        int t42 = ds->AddTask("Release packaging", GanttDate(2026, 4, 13), GanttDate(2026, 4, 17), launch);
        ds->SetTaskAssignee(t41, "Dana Reyes");
        ds->SetTaskAssignee(t42, "Leo Brandt");
        int msGa = ds->AddMilestone("GA release", GanttDate(2026, 4, 24), launch);

        ds->AddDependency(t11, t12);
        ds->AddDependency(t12, t13);
        ds->AddDependency(t13, t21);
        ds->AddDependency(t21, t22);
        ds->AddDependency(t22, t23);
        ds->AddDependency(t23, msDesign);
        ds->AddDependency(msDesign, t31);
        ds->AddDependency(t31, t32, GanttDependencyType::StartToStart, 10);
        ds->AddDependency(t31, t33, GanttDependencyType::FinishToFinish, 10);
        ds->AddDependency(t33, t41);
        ds->AddDependency(t33, t42);
        ds->AddDependency(t41, msGa, GanttDependencyType::FinishToFinish);
        return ds;
    }

    std::shared_ptr<UltraCanvasLabel> MakeCaption(const std::string& id,
                                                  float x, float y, float w,
                                                  const std::string& text) {
        auto label = std::make_shared<UltraCanvasLabel>(id, x, y, w, 22);
        label->SetText(text);
        label->SetFontSize(12);
        label->SetFontWeight(FontWeight::Bold);
        return label;
    }

} // namespace

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGanttChartExamples() {
    auto container = std::make_shared<UltraCanvasContainer>(
            "GanttChartDemo", 0, 0, 1180, 900);

    auto titleLabel = std::make_shared<UltraCanvasLabel>(
            "GanttTitle", 20, 8, 1140, 26);
    titleLabel->SetText("Gantt Chart Examples - wheel scrolls, Ctrl+wheel zooms, "
                        "click selects, triangles collapse phases");
    titleLabel->SetFontSize(15);
    titleLabel->SetFontWeight(FontWeight::Bold);
    container->AddChild(titleLabel);

    GanttDate today(2026, 3, 18);

    // ===== 1. Modern design: colorful pill bars, week/day header ============
    container->AddChild(MakeCaption("GanttCapModern", 20, 38, 560,
                                    "Modern design - Vibrant palette"));
    auto modern = CreateGanttChartWithData("gantt_modern", 10, 60, 575, 300,
                                           BuildLaunchProject(), GanttDesign::Modern);
    modern->SetToday(today);
    modern->EditStyle().dayWidth = 14.0f;
    modern->StyleChanged();
    container->AddChild(modern);

    // ===== 2. Professional design: progress fills, bracket summaries ========
    container->AddChild(MakeCaption("GanttCapPro", 605, 38, 560,
                                    "Professional design - progress + critical path"));
    auto pro = CreateGanttChartWithData("gantt_pro", 595, 60, 575, 300,
                                        BuildLaunchProject(), GanttDesign::Professional);
    pro->SetToday(today);
    pro->SetShowCriticalPath(true);
    container->AddChild(pro);

    // ===== 3. Classic design: flat single-color print style ================
    container->AddChild(MakeCaption("GanttCapClassic", 20, 372, 560,
                                    "Classic design - CorporateBlue, month scale"));
    auto classic = CreateGanttChartWithData("gantt_classic", 10, 394, 575, 240,
                                            BuildLaunchProject(), GanttDesign::Classic);
    classic->EditStyle().timeScale = GanttTimeScale::Weeks;
    classic->EditStyle().dayWidth = 5.5f;
    classic->StyleChanged();
    container->AddChild(classic);

    // ===== 4. Soft design: pastel line bars, dashed dependencies ===========
    container->AddChild(MakeCaption("GanttCapSoft", 605, 372, 560,
                                    "Soft design - Pastel palette, dashed arrows"));
    auto soft = CreateGanttChartWithData("gantt_soft", 595, 394, 575, 240,
                                         BuildLaunchProject(), GanttDesign::Soft);
    soft->EditStyle().dayWidth = 7.0f;
    soft->StyleChanged();
    container->AddChild(soft);

    // ===== 5. Minimal design: wide project table, light-track progress =====
    container->AddChild(MakeCaption("GanttCapMinimal", 20, 646, 560,
                                    "Minimal design - assignees, priorities, % complete"));
    auto minimal = CreateGanttChartWithData("gantt_minimal", 10, 668, 575, 220,
                                            BuildLaunchProject(), GanttDesign::Minimal);
    minimal->SetToday(today);
    minimal->EditStyle().dayWidth = 9.0f;
    minimal->StyleChanged();
    container->AddChild(minimal);

    // ===== 6. Dark design with a swapped palette ===========================
    container->AddChild(MakeCaption("GanttCapDark", 605, 646, 560,
                                    "Dark design - Ocean palette"));
    auto dark = CreateGanttChartWithData("gantt_dark", 595, 668, 575, 220,
                                         BuildLaunchProject(), GanttDesign::Dark);
    dark->SetPalette(GanttPalette::Ocean);
    dark->SetToday(today);
    dark->EditStyle().dayWidth = 12.0f;
    dark->StyleChanged();
    container->AddChild(dark);

    return container;
}

} // namespace UltraCanvas
