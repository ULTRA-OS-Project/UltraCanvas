// Apps/DemoApp/UltraCanvasGanttChartExamples.cpp
// Gantt chart demo. Every example lives on its own tab so it gets the full
// display area: a "Design Studio" tab whose control panel exercises the whole
// GanttChartStyle surface (design presets, palettes, bar/summary/milestone
// shapes, progress + dependency rendering, header and grid options, metrics),
// one tab per built-in design preset, and a palette gallery tab that shows the
// same schedule through all eight named palettes.
// Version: 2.1.0 - the shared page look (UltraCanvasDemoPageKit): kit title and
//                  description, the tabs in a card that fills the area, the
//                  tab descriptions and the studio panel's captions wrap; the
//                  panel is a flex grid that scrolls when its options outgrow it
// Version: 2.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasGanttChart.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

// =============================================================================
// SAMPLE PROJECT
// =============================================================================

    // Product-launch schedule with two phase levels, milestones, dependencies,
    // progress, assignees, and priorities. Each chart gets its own instance
    // because expand/collapse state lives on the tasks.
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
        ds->SetTaskProgress(t41, 0.1f);
        ds->SetTaskAssignee(t41, "Dana Reyes");
        ds->SetTaskAssignee(t42, "Leo Brandt");
        ds->SetTaskPriority(t41, GanttPriority::Low);
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

    const GanttDate kToday(2026, 3, 18);

// =============================================================================
// MENU TABLES (keep dropdown order <-> enum mapping in one place)
// =============================================================================

    const std::vector<std::pair<std::string, GanttDesign>>& DesignMenu() {
        static const std::vector<std::pair<std::string, GanttDesign>> menu = {
                {"Modern",       GanttDesign::Modern},
                {"Professional", GanttDesign::Professional},
                {"Classic",      GanttDesign::Classic},
                {"Soft",         GanttDesign::Soft},
                {"Minimal",      GanttDesign::Minimal},
                {"Dark",         GanttDesign::Dark},
        };
        return menu;
    }

    const std::vector<std::pair<std::string, GanttPalette>>& PaletteMenu() {
        static const std::vector<std::pair<std::string, GanttPalette>> menu = {
                {"Corporate Blue", GanttPalette::CorporateBlue},
                {"Vibrant",        GanttPalette::Vibrant},
                {"Pastel",         GanttPalette::Pastel},
                {"Ocean",          GanttPalette::Ocean},
                {"Sunset",         GanttPalette::Sunset},
                {"Forest",         GanttPalette::Forest},
                {"Slate",          GanttPalette::Slate},
                {"Mono",           GanttPalette::Mono},
        };
        return menu;
    }

    const std::vector<std::pair<std::string, GanttColorMode>>& ColorModeMenu() {
        static const std::vector<std::pair<std::string, GanttColorMode>> menu = {
                {"By phase",    GanttColorMode::ByPhase},
                {"By row",      GanttColorMode::ByRow},
                {"By priority", GanttColorMode::ByPriority},
                {"Single",      GanttColorMode::Single},
        };
        return menu;
    }

    const std::vector<std::pair<std::string, GanttBarStyle>>& BarStyleMenu() {
        static const std::vector<std::pair<std::string, GanttBarStyle>> menu = {
                {"Flat",     GanttBarStyle::Flat},
                {"Rounded",  GanttBarStyle::Rounded},
                {"Pill",     GanttBarStyle::Pill},
                {"Gradient", GanttBarStyle::Gradient},
                {"Line",     GanttBarStyle::Line},
        };
        return menu;
    }

    const std::vector<std::pair<std::string, GanttSummaryStyle>>& SummaryStyleMenu() {
        static const std::vector<std::pair<std::string, GanttSummaryStyle>> menu = {
                {"Bar",     GanttSummaryStyle::Bar},
                {"Bracket", GanttSummaryStyle::Bracket},
                {"Line",    GanttSummaryStyle::Line},
        };
        return menu;
    }

    const std::vector<std::pair<std::string, GanttMilestoneStyle>>& MilestoneStyleMenu() {
        static const std::vector<std::pair<std::string, GanttMilestoneStyle>> menu = {
                {"Diamond", GanttMilestoneStyle::Diamond},
                {"Circle",  GanttMilestoneStyle::Circle},
                {"Square",  GanttMilestoneStyle::Square},
        };
        return menu;
    }

    const std::vector<std::pair<std::string, GanttProgressStyle>>& ProgressStyleMenu() {
        static const std::vector<std::pair<std::string, GanttProgressStyle>> menu = {
                {"Hidden",      GanttProgressStyle::Hidden},
                {"Darker fill", GanttProgressStyle::DarkerFill},
                {"Light track", GanttProgressStyle::LightTrack},
                {"Inner bar",   GanttProgressStyle::InnerBar},
        };
        return menu;
    }

    const std::vector<std::pair<std::string, GanttDependencyLineStyle>>& DependencyStyleMenu() {
        static const std::vector<std::pair<std::string, GanttDependencyLineStyle>> menu = {
                {"Hidden",   GanttDependencyLineStyle::Hidden},
                {"Elbow",    GanttDependencyLineStyle::Elbow},
                {"Straight", GanttDependencyLineStyle::Straight},
        };
        return menu;
    }

    const std::vector<std::pair<std::string, GanttLabelPlacement>>& LabelPlacementMenu() {
        static const std::vector<std::pair<std::string, GanttLabelPlacement>> menu = {
                {"Hidden",       GanttLabelPlacement::Hidden},
                {"Inside bar",   GanttLabelPlacement::InsideBar},
                {"Right of bar", GanttLabelPlacement::RightOfBar},
                {"Auto place",   GanttLabelPlacement::AutoPlace},
        };
        return menu;
    }

    const std::vector<std::pair<std::string, GanttTimeScale>>& TimeScaleMenu() {
        static const std::vector<std::pair<std::string, GanttTimeScale>> menu = {
                {"Auto",     GanttTimeScale::Auto},
                {"Days",     GanttTimeScale::Days},
                {"Weeks",    GanttTimeScale::Weeks},
                {"Months",   GanttTimeScale::Months},
                {"Quarters", GanttTimeScale::Quarters},
        };
        return menu;
    }

    const std::vector<std::pair<std::string, GanttDateFormat>>& DateFormatMenu() {
        static const std::vector<std::pair<std::string, GanttDateFormat>> menu = {
                {"25/12/26",   GanttDateFormat::DayMonthYearSlash},
                {"12/25/26",   GanttDateFormat::MonthDayYearSlash},
                {"2026-12-25", GanttDateFormat::ISO},
                {"25 Dec",     GanttDateFormat::DayMonthShort},
        };
        return menu;
    }

    // Column presets offered by the "Table columns" dropdown.
    std::vector<GanttColumn> ColumnPreset(int index) {
        switch (index) {
            case 1: return GanttChartStyle::ProjectColumns();
            case 2: return {GanttColumn(GanttColumnType::Name, "Task", 240)};
            case 3: return {GanttColumn(GanttColumnType::Name, "Activity", 200),
                            GanttColumn(GanttColumnType::Duration, "Days", 56)};
            case 4: return {GanttColumn(GanttColumnType::Wbs, "ID", 40),
                            GanttColumn(GanttColumnType::Name, "Task", 180),
                            GanttColumn(GanttColumnType::Assignee, "Owner", 100),
                            GanttColumn(GanttColumnType::Progress, "%", 46)};
            case 5: return {GanttColumn(GanttColumnType::Wbs, "ID", 40),
                            GanttColumn(GanttColumnType::Name, "Task", 240)};
            default: return GanttChartStyle::DefaultColumns();
        }
    }

    const int kColumnPresetCount = 6;

    // Index of the column preset that matches `columns`, so the dropdown can
    // follow a design-preset swap. Falls back to the default set.
    int ColumnPresetIndexFor(const std::vector<GanttColumn>& columns) {
        auto sameShape = [&](const std::vector<GanttColumn>& other) {
            if (columns.size() != other.size()) return false;
            for (size_t i = 0; i < columns.size(); ++i) {
                if (columns[i].type != other[i].type) return false;
            }
            return true;
        };
        for (int i = 0; i < kColumnPresetCount; ++i) {
            if (sameShape(ColumnPreset(i))) return i;
        }
        return 0;
    }

    // Index of `value` inside a menu table; 0 when it is not listed.
    template <typename T>
    int MenuIndexOf(const std::vector<std::pair<std::string, T>>& menu, T value) {
        for (size_t i = 0; i < menu.size(); ++i) {
            if (menu[i].second == value) return static_cast<int>(i);
        }
        return 0;
    }

// =============================================================================
// SMALL UI HELPERS
// =============================================================================

    // Stretch a flex child on the cross axis and give it the requested grow factor.
    void AddFlexChild(const std::shared_ptr<UltraCanvasContainer>& parent,
                      const std::shared_ptr<UltraCanvasUIElement>& child, float grow) {
        child->layoutItem.SetFlexGrow(grow).SetFlexShrink(grow > 0 ? 1.f : 0.f)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        parent->AddChild(child);
    }

    std::shared_ptr<UltraCanvasLabel> MakeTabDescription(const std::string& id,
                                                         const std::string& text) {
        auto lbl = DemoKit::CreateText(id, text);
        lbl->SetFontSize(11);
        lbl->SetTextColor(Color(70, 70, 80, 255));
        lbl->SetMargin(8, 12, 4, 12);   // top, right, bottom, left
        return lbl;
    }

    std::string FormatFloat(float value, int decimals) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
        return buf;
    }

// =============================================================================
// DESIGN STUDIO CONTROL PANEL
// =============================================================================

    // The option panel's grid: each section is a heading over a wrapping row
    // of fixed-width cells, two to a line. A cell is a caption over its
    // control, as tall as its wrapped caption.
    class PanelLayout {
    public:
        PanelLayout(std::shared_ptr<UltraCanvasContainer> target, float columnWidth,
                    float columnGap)
                : panel(std::move(target)), colW(columnWidth), colGap(columnGap) {}

        float ColumnWidth() const { return colW; }

        // Starts a section: its heading, then a fresh grid for its controls.
        void Heading(const std::string& id, const std::string& text) {
            auto lbl = DemoKit::CreateHeading(id, text);
            lbl->SetFontSize(12);
            lbl->SetTextColor(Color(40, 50, 120, 255));
            if (grid) lbl->SetMargin(6, 0, 0, 0);
            DemoKit::Add(panel, lbl);
            grid = DemoKit::CreateRow(id + "Grid", colGap);
            grid->layout.SetFlexGap(4, colGap);
            DemoKit::Add(panel, grid);
        }

        // A cell of the current grid, for a caption and its control.
        std::shared_ptr<UltraCanvasContainer> Cell(const std::string& id) {
            auto cell = DemoKit::CreateColumn(id, 2);
            cell->size.width = CSSLayout::Dimension::Px(colW);
            DemoKit::Add(grid, cell);
            return cell;
        }

        // A control that is a cell on its own (a checkbox).
        void Place(const std::shared_ptr<UltraCanvasUIElement>& element) {
            DemoKit::AddFixed(grid, element);
        }

        std::shared_ptr<UltraCanvasLabel> Caption(const std::shared_ptr<UltraCanvasContainer>& cell,
                                                  const std::string& id,
                                                  const std::string& text) {
            auto lbl = DemoKit::AddCaption(cell, id, text);
            lbl->SetTextColor(Color(80, 80, 90, 255));
            return lbl;
        }

    private:
        std::shared_ptr<UltraCanvasContainer> panel;
        std::shared_ptr<UltraCanvasContainer> grid;
        float colW = 0;
        float colGap = 0;
    };

    // Every control writes into the chart's style and every control can be
    // pushed back from the style (after a design-preset swap). `suppress` stops
    // the programmatic sync from re-triggering the change callbacks.
    struct StudioControls {
        std::shared_ptr<UltraCanvasGanttChartElement> chart;
        std::shared_ptr<bool> suppress = std::make_shared<bool>(false);
        std::vector<std::function<void()>> syncers;

        void SyncFromStyle() {
            *suppress = true;
            for (auto& s : syncers) s();
            *suppress = false;
        }
    };

    // Adds a labeled dropdown bound to a menu table; `apply` receives the enum
    // value, `read` pulls the current value back out of the style.
    template <typename T>
    void AddEnumDropdown(PanelLayout& lay, StudioControls& sc, const std::string& id,
                         const std::string& caption,
                         const std::vector<std::pair<std::string, T>>& menu,
                         std::function<void(GanttChartStyle&, T)> apply,
                         std::function<T(const GanttChartStyle&)> read) {
        auto cell = lay.Cell(id + "Cell");
        lay.Caption(cell, id + "Lbl", caption);
        auto dd = std::make_shared<UltraCanvasDropdown>(id, 0, 0, lay.ColumnWidth(), 23);
        for (const auto& entry : menu) dd->AddItem(entry.first);
        dd->SetSelectedIndex(MenuIndexOf(menu, read(sc.chart->GetStyle())), false);

        auto chart = sc.chart;
        auto suppress = sc.suppress;
        const auto* table = &menu;   // menu tables are function-local statics
        dd->onSelectionChanged = [chart, suppress, table, apply](int index, const DropdownItem&) {
            if (*suppress) return;
            if (index < 0 || index >= static_cast<int>(table->size())) return;
            apply(chart->EditStyle(), (*table)[index].second);
            chart->StyleChanged();
        };
        DemoKit::AddStretched(cell, dd);
        sc.syncers.push_back([dd, chart, table, read]() {
            dd->SetSelectedIndex(MenuIndexOf(*table, read(chart->GetStyle())), false);
        });
    }

    void AddCheck(PanelLayout& lay, StudioControls& sc, const std::string& id, const std::string& text,
                  std::function<void(GanttChartStyle&, bool)> apply,
                  std::function<bool(const GanttChartStyle&)> read) {
        auto cb = std::make_shared<UltraCanvasCheckbox>(id, 0, 0, lay.ColumnWidth(), 19);
        cb->SetText(text);
        cb->SetFontSize(10);
        cb->SetBoxSize(13.0f);
        cb->SetChecked(read(sc.chart->GetStyle()));

        auto chart = sc.chart;
        auto suppress = sc.suppress;
        cb->onStateChanged = [chart, suppress, apply](CheckedState, CheckedState ns) {
            if (*suppress) return;
            apply(chart->EditStyle(), ns == CheckedState::Checked);
            chart->StyleChanged();
        };
        lay.Place(cb);
        sc.syncers.push_back([cb, chart, read]() {
            cb->SetChecked(read(chart->GetStyle()));
        });
    }

    void AddSlider(PanelLayout& lay, StudioControls& sc, const std::string& id, const std::string& caption,
                   float minValue, float maxValue, float step, int decimals,
                   std::function<void(GanttChartStyle&, float)> apply,
                   std::function<float(const GanttChartStyle&)> read) {
        const float current = read(sc.chart->GetStyle());
        auto cell = lay.Cell(id + "Cell");
        auto lbl = lay.Caption(cell, id + "Lbl", caption + ": " + FormatFloat(current, decimals));

        auto sl = std::make_shared<UltraCanvasSlider>(id, 0, 0, lay.ColumnWidth(), 20);
        sl->SetRange(minValue, maxValue);
        sl->SetStep(step);
        sl->SetValueDisplay(SliderValueDisplay::NoDisplay);
        sl->SetValue(current);

        auto chart = sc.chart;
        auto suppress = sc.suppress;
        sl->onValueChanged = [chart, suppress, lbl, caption, decimals, apply](float v) {
            lbl->SetText(caption + ": " + FormatFloat(v, decimals));
            if (*suppress) return;
            apply(chart->EditStyle(), v);
            chart->StyleChanged();
        };
        DemoKit::AddStretched(cell, sl);
        sc.syncers.push_back([sl, chart, read]() { sl->SetValue(read(chart->GetStyle())); });
    }

    // Fill `panel` with the full option set for `chart`.
    void BuildStudioPanel(const std::shared_ptr<UltraCanvasContainer>& panel,
                          const std::shared_ptr<UltraCanvasGanttChartElement>& chart) {
        const float COL_W = 166.0f, COL_GAP = 12.0f;

        auto sc = std::make_shared<StudioControls>();
        sc->chart = chart;
        PanelLayout lay(panel, COL_W, COL_GAP);

        // ---- Design & colour -------------------------------------------------
        lay.Heading("GsHeadDesign", "Design & colour");

        // The design preset replaces the whole style, so it drives every other
        // control back into sync instead of writing a single field.
        auto designCell = lay.Cell("GsDesignCell");
        lay.Caption(designCell, "GsDesignLbl", "Design preset:");
        auto designDd = std::make_shared<UltraCanvasDropdown>("GsDesign", 0, 0,
                                                               lay.ColumnWidth(), 23);
        for (const auto& entry : DesignMenu()) designDd->AddItem(entry.first);
        designDd->SetSelectedIndex(0, false);
        {
            auto controls = sc;
            designDd->onSelectionChanged = [controls](int index, const DropdownItem&) {
                if (*controls->suppress) return;
                const auto& menu = DesignMenu();
                if (index < 0 || index >= static_cast<int>(menu.size())) return;
                controls->chart->ApplyDesign(menu[index].second);
                controls->SyncFromStyle();
            };
        }
        DemoKit::AddStretched(designCell, designDd);

        // Palette: swaps only the bar colour cycle. Entry 0 restores whatever
        // palette the selected design preset ships with, so a design swap can
        // reset this dropdown to a truthful value instead of a stale name.
        auto paletteCell = lay.Cell("GsPaletteCell");
        lay.Caption(paletteCell, "GsPaletteLbl", "Palette:");
        auto paletteDd = std::make_shared<UltraCanvasDropdown>("GsPalette", 0, 0,
                                                               lay.ColumnWidth(), 23);
        paletteDd->AddItem("Design default");
        for (const auto& entry : PaletteMenu()) paletteDd->AddItem(entry.first);
        paletteDd->SetSelectedIndex(0, false);
        {
            auto controls = sc;
            paletteDd->onSelectionChanged = [controls, designDd](int index, const DropdownItem&) {
                if (*controls->suppress) return;
                if (index <= 0) {
                    const auto& designs = DesignMenu();
                    int d = std::max(0, std::min(designDd->GetSelectedIndex(),
                                                 static_cast<int>(designs.size()) - 1));
                    controls->chart->SetCustomPalette(
                            GanttChartStyles::Create(designs[d].second).palette);
                    return;
                }
                const auto& menu = PaletteMenu();
                if (index - 1 >= static_cast<int>(menu.size())) return;
                controls->chart->SetPalette(menu[index - 1].second);
            };
        }
        DemoKit::AddStretched(paletteCell, paletteDd);
        // After a preset swap the chart really is showing the design's own
        // palette again. Held weakly: this dropdown's own callback owns `sc`,
        // and a strong capture here would close the cycle.
        sc->syncers.push_back([weakPalette = std::weak_ptr<UltraCanvasDropdown>(paletteDd)]() {
            if (auto dd = weakPalette.lock()) dd->SetSelectedIndex(0, false);
        });

        AddEnumDropdown<GanttColorMode>(lay, *sc, "GsColorMode", "Colour mode:",
                ColorModeMenu(),
                [](GanttChartStyle& s, GanttColorMode v) { s.colorMode = v; },
                [](const GanttChartStyle& s) { return s.colorMode; });

        AddEnumDropdown<GanttBarStyle>(lay, *sc, "GsBarStyle", "Task bars:",
                BarStyleMenu(),
                [](GanttChartStyle& s, GanttBarStyle v) { s.barStyle = v; },
                [](const GanttChartStyle& s) { return s.barStyle; });

        AddEnumDropdown<GanttSummaryStyle>(lay, *sc, "GsSummary", "Summary bars:",
                SummaryStyleMenu(),
                [](GanttChartStyle& s, GanttSummaryStyle v) { s.summaryStyle = v; },
                [](const GanttChartStyle& s) { return s.summaryStyle; });

        AddEnumDropdown<GanttMilestoneStyle>(lay, *sc, "GsMilestone", "Milestones:",
                MilestoneStyleMenu(),
                [](GanttChartStyle& s, GanttMilestoneStyle v) { s.milestoneStyle = v; },
                [](const GanttChartStyle& s) { return s.milestoneStyle; });

        AddEnumDropdown<GanttProgressStyle>(lay, *sc, "GsProgress", "Progress:",
                ProgressStyleMenu(),
                [](GanttChartStyle& s, GanttProgressStyle v) { s.progressStyle = v; },
                [](const GanttChartStyle& s) { return s.progressStyle; });

        AddEnumDropdown<GanttDependencyLineStyle>(lay, *sc, "GsDeps", "Dependencies:",
                DependencyStyleMenu(),
                [](GanttChartStyle& s, GanttDependencyLineStyle v) { s.dependencyStyle = v; },
                [](const GanttChartStyle& s) { return s.dependencyStyle; });

        AddEnumDropdown<GanttLabelPlacement>(lay, *sc, "GsLabels", "Bar labels:",
                LabelPlacementMenu(),
                [](GanttChartStyle& s, GanttLabelPlacement v) { s.labelPlacement = v; },
                [](const GanttChartStyle& s) { return s.labelPlacement; });

        AddEnumDropdown<GanttTimeScale>(lay, *sc, "GsScale", "Time scale:",
                TimeScaleMenu(),
                [](GanttChartStyle& s, GanttTimeScale v) { s.timeScale = v; },
                [](const GanttChartStyle& s) { return s.timeScale; });

        AddEnumDropdown<GanttDateFormat>(lay, *sc, "GsDateFmt", "Date format:",
                DateFormatMenu(),
                [](GanttChartStyle& s, GanttDateFormat v) { s.dateFormat = v; },
                [](const GanttChartStyle& s) { return s.dateFormat; });

        // Table columns: a preset list rather than a single style field.
        auto columnsCell = lay.Cell("GsColumnsCell");
        lay.Caption(columnsCell, "GsColumnsLbl", "Table columns:");
        auto columnsDd = std::make_shared<UltraCanvasDropdown>("GsColumns", 0, 0,
                                                               lay.ColumnWidth(), 23);
        for (const char* n : {"ID / task / dates", "Project (owner, priority, %)",
                              "Task name only", "Activity + days", "ID / task / owner / %",
                              "ID / task"})
            columnsDd->AddItem(n);
        columnsDd->SetSelectedIndex(ColumnPresetIndexFor(chart->GetStyle().columns), false);
        {
            auto controls = sc;
            columnsDd->onSelectionChanged = [controls](int index, const DropdownItem&) {
                if (*controls->suppress) return;
                controls->chart->SetColumns(ColumnPreset(index));
                controls->chart->StyleChanged();
            };
        }
        DemoKit::AddStretched(columnsCell, columnsDd);
        sc->syncers.push_back([weakColumns = std::weak_ptr<UltraCanvasDropdown>(columnsDd),
                               chart]() {
            if (auto dd = weakColumns.lock())
                dd->SetSelectedIndex(ColumnPresetIndexFor(chart->GetStyle().columns), false);
        });

        // ---- Elements --------------------------------------------------------
        lay.Heading("GsHeadElements", "Chart elements");

        AddCheck(lay, *sc, "GsShowTable", "Task table",
                 [](GanttChartStyle& s, bool v) { s.showTable = v; },
                 [](const GanttChartStyle& s) { return s.showTable; });
        AddCheck(lay, *sc, "GsTwoTier", "Two-tier header",
                 [](GanttChartStyle& s, bool v) { s.twoTierHeader = v; },
                 [](const GanttChartStyle& s) { return s.twoTierHeader; });
        AddCheck(lay, *sc, "GsWeekdays", "Weekday letters",
                 [](GanttChartStyle& s, bool v) { s.showWeekdayLetters = v; },
                 [](const GanttChartStyle& s) { return s.showWeekdayLetters; });
        AddCheck(lay, *sc, "GsUpper", "Uppercase header",
                 [](GanttChartStyle& s, bool v) { s.uppercaseHeader = v; },
                 [](const GanttChartStyle& s) { return s.uppercaseHeader; });
        AddCheck(lay, *sc, "GsRowLines", "Row lines",
                 [](GanttChartStyle& s, bool v) { s.showRowLines = v; },
                 [](const GanttChartStyle& s) { return s.showRowLines; });
        AddCheck(lay, *sc, "GsColLines", "Column lines",
                 [](GanttChartStyle& s, bool v) { s.showColumnLines = v; },
                 [](const GanttChartStyle& s) { return s.showColumnLines; });
        AddCheck(lay, *sc, "GsWeekend", "Shade weekends",
                 [](GanttChartStyle& s, bool v) { s.shadeWeekends = v; },
                 [](const GanttChartStyle& s) { return s.shadeWeekends; });
        AddCheck(lay, *sc, "GsStripes", "Row striping",
                 [](GanttChartStyle& s, bool v) {
                     s.rowAlternateColor = v ? Color(0, 0, 0, 12) : Colors::Transparent;
                 },
                 [](const GanttChartStyle& s) { return s.rowAlternateColor.a != 0; });
        AddCheck(lay, *sc, "GsBoldSummary", "Bold summary rows",
                 [](GanttChartStyle& s, bool v) { s.boldSummaryRows = v; },
                 [](const GanttChartStyle& s) { return s.boldSummaryRows; });
        AddCheck(lay, *sc, "GsToday", "Today marker",
                 [](GanttChartStyle& s, bool v) { s.showTodayLine = v; },
                 [](const GanttChartStyle& s) { return s.showTodayLine; });
        AddCheck(lay, *sc, "GsCritical", "Critical path",
                 [](GanttChartStyle& s, bool v) { s.highlightCriticalPath = v; },
                 [](const GanttChartStyle& s) { return s.highlightCriticalPath; });
        AddCheck(lay, *sc, "GsDashed", "Dashed arrows",
                 [](GanttChartStyle& s, bool v) { s.dependencyDashed = v; },
                 [](const GanttChartStyle& s) { return s.dependencyDashed; });
        AddCheck(lay, *sc, "GsDepColor", "Arrows use bar colour",
                 [](GanttChartStyle& s, bool v) { s.dependencyUsesBarColor = v; },
                 [](const GanttChartStyle& s) { return s.dependencyUsesBarColor; });
        AddCheck(lay, *sc, "GsProgressText", "Progress % text",
                 [](GanttChartStyle& s, bool v) { s.showProgressText = v; },
                 [](const GanttChartStyle& s) { return s.showProgressText; });
        AddCheck(lay, *sc, "GsAssignee", "Assignee in labels",
                 [](GanttChartStyle& s, bool v) { s.showAssigneeWithLabel = v; },
                 [](const GanttChartStyle& s) { return s.showAssigneeWithLabel; });
        AddCheck(lay, *sc, "GsBarBorder", "Bar outline",
                 [](GanttChartStyle& s, bool v) {
                     s.barBorderColor = v ? Color(0, 0, 0, 70) : Colors::Transparent;
                     s.barBorderWidth = v ? 1.0f : 0.0f;
                 },
                 [](const GanttChartStyle& s) { return s.barBorderWidth > 0.0f; });

        // ---- Metrics ---------------------------------------------------------
        lay.Heading("GsHeadMetrics", "Metrics");

        AddSlider(lay, *sc, "GsDayWidth", "Day width", 1.5f, 40.0f, 0.5f, 1,
                  [](GanttChartStyle& s, float v) { s.dayWidth = v; },
                  [](const GanttChartStyle& s) { return s.dayWidth; });
        AddSlider(lay, *sc, "GsRowHeight", "Row height", 18.0f, 48.0f, 1.0f, 0,
                  [](GanttChartStyle& s, float v) { s.rowHeight = v; },
                  [](const GanttChartStyle& s) { return s.rowHeight; });
        AddSlider(lay, *sc, "GsBarRatio", "Bar height", 0.25f, 0.9f, 0.02f, 2,
                  [](GanttChartStyle& s, float v) { s.barHeightRatio = v; },
                  [](const GanttChartStyle& s) { return s.barHeightRatio; });
        AddSlider(lay, *sc, "GsCorner", "Corner radius", 0.0f, 14.0f, 1.0f, 0,
                  [](GanttChartStyle& s, float v) { s.barCornerRadius = v; },
                  [](const GanttChartStyle& s) { return s.barCornerRadius; });
        AddSlider(lay, *sc, "GsMsSize", "Milestone size", 0.2f, 0.9f, 0.05f, 2,
                  [](GanttChartStyle& s, float v) { s.milestoneSizeRatio = v; },
                  [](const GanttChartStyle& s) { return s.milestoneSizeRatio; });
        AddSlider(lay, *sc, "GsDepWidth", "Arrow width", 0.5f, 4.0f, 0.1f, 1,
                  [](GanttChartStyle& s, float v) { s.dependencyLineWidth = v; },
                  [](const GanttChartStyle& s) { return s.dependencyLineWidth; });

        // `sc` (and with it the syncer list) stays alive because every control
        // callback registered above captured it, or the shared suppress flag,
        // by value.
    }

// =============================================================================
// TAB BUILDERS
// =============================================================================

    // A full-page chart tab: description line on top, chart filling the rest.
    std::shared_ptr<UltraCanvasContainer> MakeDesignTab(
            const std::string& id, const std::string& description,
            const std::function<void(const std::shared_ptr<UltraCanvasGanttChartElement>&)>& configure,
            GanttDesign design, int innerW, int innerH) {
        auto tab = std::make_shared<UltraCanvasContainer>(id, 0, 0, innerW, innerH);
        tab->SetBackgroundColor(Color(255, 255, 255, 255));
        tab->layout.SetFlexColumn().SetFlexGap(4)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        AddFlexChild(tab, MakeTabDescription(id + "Desc", description), 0);

        auto chart = CreateGanttChartWithData(id + "Chart", 0, 0, innerW, innerH - 50,
                                              BuildLaunchProject(), design);
        chart->SetToday(kToday);
        if (configure) configure(chart);
        chart->StyleChanged();
        AddFlexChild(tab, chart, 1);
        return tab;
    }

    // One gallery cell: palette name over a compact chart using that palette.
    // The phases stay collapsed so each cell shows one bar per palette colour
    // across the whole schedule instead of a clipped task list.
    std::shared_ptr<UltraCanvasContainer> MakePaletteCell(size_t index, int cellW, int cellH) {
        const auto& menu = PaletteMenu();
        const std::string suffix = std::to_string(index);

        auto cell = std::make_shared<UltraCanvasContainer>("GanttPalCell" + suffix,
                                                           0, 0, cellW, cellH);
        cell->layout.SetFlexColumn().SetFlexGap(2)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        auto caption = DemoKit::CreateText("GanttPalCap" + suffix, menu[index].first);
        caption->SetFontSize(11);
        caption->SetFontWeight(FontWeight::Bold);
        caption->SetTextColor(Color(50, 55, 70, 255));
        AddFlexChild(cell, caption, 0);

        auto data = BuildLaunchProject();
        data->SetAllExpanded(false);

        auto chart = CreateGanttChartWithData("GanttPal" + suffix, 0, 0, cellW, cellH - 17,
                                              data, GanttDesign::Professional);
        chart->SetPalette(menu[index].second);
        chart->SetColumns({GanttColumn(GanttColumnType::Name, "Phase", 124)});
        auto& st = chart->EditStyle();
        st.rowHeight = 24.0f;
        st.barHeightRatio = 0.6f;
        st.summaryStyle = GanttSummaryStyle::Bar;   // solid bars carry the colour
        st.summaryBarColor = Colors::Transparent;   // ... taken from the palette
        st.timeScale = GanttTimeScale::Months;
        st.tableFontSize = 10.0f;
        st.headerFontSize = 10.0f;
        st.headerTierHeight = 17.0f;
        st.labelPlacement = GanttLabelPlacement::Hidden;
        st.showProgressText = false;
        st.rowAlternateColor = Colors::Transparent;
        chart->StyleChanged();
        chart->FitToRange();
        AddFlexChild(cell, chart, 1);
        return cell;
    }

    // Palette gallery: the same schedule rendered eight times, one per named
    // palette, in a 2 x 4 grid that follows the page size.
    std::shared_ptr<UltraCanvasContainer> MakePaletteTab(int innerW, int innerH) {
        auto tab = std::make_shared<UltraCanvasContainer>("GanttTabPalettes", 0, 0, innerW, innerH);
        tab->SetBackgroundColor(Color(255, 255, 255, 255));
        tab->layout.SetFlexColumn().SetFlexGap(4)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        AddFlexChild(tab, MakeTabDescription("GanttPalDesc",
                "The eight named palettes from GanttPalettes::Get(), applied to one shared design "
                "with the phases collapsed so each bar is one palette entry. SetPalette() swaps "
                "only the bar colour cycle - layout, header and progress rendering stay exactly "
                "as the design preset left them."), 0);

        const int cols = 2, rows = 4;
        const int cellW = innerW / cols;
        const int cellH = (innerH - 50) / rows;

        for (int r = 0; r < rows; ++r) {
            auto band = std::make_shared<UltraCanvasContainer>(
                    "GanttPalRow" + std::to_string(r), 0, 0, innerW, cellH);
            band->layout.SetFlexRow().SetFlexGap(14)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
            band->SetMargin(0, 12, 0, 12);
            for (int c = 0; c < cols; ++c) {
                AddFlexChild(band, MakePaletteCell(static_cast<size_t>(r * cols + c),
                                                   cellW, cellH), 1);
            }
            AddFlexChild(tab, band, 1);
        }
        return tab;
    }

} // namespace

// =============================================================================
// PAGE
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGanttChartExamples() {
    // Tab content is built at a deliberately conservative width: the tabs
    // take the display area's width, which may be narrower than the page was
    // built for. FitToRange() reads the bounds it is called with, and
    // under-filling the timeline is harmless where overflowing clips bars.
    const int INNER_W = 1000;
    const int INNER_H = 640;

    auto page = DemoKit::CreatePage("GanttRoot", "Gantt Charts - Design Studio, Presets & Palettes",
            "Every example has its own tab. In all charts the wheel scrolls, "
            "Ctrl+wheel zooms the timeline, clicking selects a task and the "
            "triangles collapse phases.");
    // The page is the display area's height and the tabs take what the title
    // and description leave; the charts scroll themselves.
    page->SetFillsArea(true);

    auto card = DemoKit::CreateCard("GanttTabsCard");
    card->SetPadding(4);
    DemoKit::Add(page, card, 1);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("GanttTabs", 0, 0, 0, 0);
    tabs->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    tabs->SetTabStyle(TabStyle::Rounded);
    tabs->SetTabHeight(30);
    tabs->SetTabCornerRadius(8.0f);
    tabs->SetTabMaxWidth(180);

    // ---- Tab 1: Design Studio (all appearance options) ----------------------
    auto studio = std::make_shared<UltraCanvasContainer>("GanttTabStudio", 0, 0, INNER_W, INNER_H);
    studio->SetBackgroundColor(Color(255, 255, 255, 255));
    studio->layout.SetFlexRow().SetFlexGap(10)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // Two 166 px columns with a 12 px gap, the padding, and room for the
    // vertical scrollbar the panel shows when its options are taller than
    // the tab - with it the two columns still fit side by side.
    const float PANEL_W = 390.0f;
    auto panel = std::make_shared<UltraCanvasContainer>("GanttStudioPanel", 0, 0,
                                                        static_cast<int>(PANEL_W), INNER_H);
    panel->SetBackgroundColor(Color(247, 247, 250, 255));
    panel->size.width = CSSLayout::Dimension::Px(PANEL_W);
    panel->SetPadding(8, 12, 8, 12);
    panel->layout.SetFlexColumn().SetFlexGap(4)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    {
        ContainerStyle panelStyle;
        panelStyle.autoShowScrollbars = true;
        panelStyle.autoShowHorizontalScrollbar = false;
        panel->SetContainerStyle(panelStyle);
    }

    // Built at exactly the width the flex row will hand it, so FitToRange()
    // below measures the timeline it actually gets.
    const int CHART_W = INNER_W - static_cast<int>(PANEL_W) - 10;
    auto studioChart = CreateGanttChartWithData("GanttStudioChart", 0, 0, CHART_W, INNER_H,
                                                BuildLaunchProject(), GanttDesign::Modern);
    studioChart->SetToday(kToday);
    // The option panel takes a third of the page, so start from the narrow
    // column set and a week scale that stays readable once the whole project is
    // fitted. Switching "Table columns" or "Time scale" shows the trade-off.
    studioChart->SetColumns(ColumnPreset(5));
    studioChart->SetTimeScale(GanttTimeScale::Weeks);
    // Modern's uppercase captions ("WEEK 06") do not survive a narrow week
    // column; the "Uppercase header" checkbox turns them back on.
    studioChart->EditStyle().uppercaseHeader = false;
    studioChart->StyleChanged();
    studioChart->FitToRange();

    BuildStudioPanel(panel, studioChart);

    AddFlexChild(studio, panel, 0);         // fixed-width option panel
    AddFlexChild(studio, studioChart, 1);   // chart fills the remaining width
    tabs->AddTab("Design Studio", studio);

    // ---- Tabs 2..7: one per built-in design preset --------------------------
    tabs->AddTab("Modern", MakeDesignTab("GanttTabModern",
            "Modern preset: pill bars on a vibrant palette, two-tier day header with weekday "
            "letters, phase names inside the summary bars, and elbow arrows that take the "
            "colour of their predecessor bar. Opens on the day scale - Ctrl+wheel to zoom out "
            "to the whole schedule.",
            [](const std::shared_ptr<UltraCanvasGanttChartElement>& c) {
                c->EditStyle().dayWidth = 11.0f;
            },
            GanttDesign::Modern, INNER_W, INNER_H));

    tabs->AddTab("Professional", MakeDesignTab("GanttTabPro",
            "Professional preset: the project-tool look - bracket summary bars, darker "
            "progress fills with a % readout, task name plus assignee to the right of each "
            "bar, and the critical path highlighted in red.",
            [](const std::shared_ptr<UltraCanvasGanttChartElement>& c) {
                c->SetShowCriticalPath(true);
                c->FitToRange();
            },
            GanttDesign::Professional, INNER_W, INNER_H));

    tabs->AddTab("Classic", MakeDesignTab("GanttTabClassic",
            "Classic preset: the flat single-colour print style used in business plans - one "
            "corporate blue for every bar, a single-tier month header, no weekend shading and "
            "solid elbow dependency arrows. FitToRange() picks the zoom so the whole project "
            "lands on one screen.",
            [](const std::shared_ptr<UltraCanvasGanttChartElement>& c) {
                c->SetTimeScale(GanttTimeScale::Weeks);
                c->FitToRange();
            },
            GanttDesign::Classic, INNER_W, INNER_H));

    tabs->AddTab("Soft", MakeDesignTab("GanttTabSoft",
            "Soft preset: a pastel report style - thin line bars with square end caps, striped "
            "rows under a dark table header, and dashed orange dependency arrows between the "
            "activities.",
            [](const std::shared_ptr<UltraCanvasGanttChartElement>& c) { c->FitToRange(); },
            GanttDesign::Soft, INNER_W, INNER_H));

    tabs->AddTab("Minimal", MakeDesignTab("GanttTabMinimal",
            "Minimal preset: a wide project table carrying assignee, priority chips and "
            "% complete, with light-track progress bars. Dependency arrows are off so the "
            "table stays the focus; the scale is dropped to weeks to leave the wide table room.",
            [](const std::shared_ptr<UltraCanvasGanttChartElement>& c) {
                c->SetTimeScale(GanttTimeScale::Weeks);
                c->FitToRange();
            },
            GanttDesign::Minimal, INNER_W, INNER_H));

    tabs->AddTab("Dark", MakeDesignTab("GanttTabDark",
            "Dark preset: the Modern layout on a dark surface, here combined with the Ocean "
            "palette and a today marker. Every colour in GanttChartStyle is independent, so a "
            "dark theme is a palette plus a handful of surface colours.",
            [](const std::shared_ptr<UltraCanvasGanttChartElement>& c) {
                c->SetPalette(GanttPalette::Ocean);
                c->EditStyle().dayWidth = 11.0f;
            },
            GanttDesign::Dark, INNER_W, INNER_H));

    // ---- Tab 8: palette gallery ---------------------------------------------
    tabs->AddTab("Palette Gallery", MakePaletteTab(INNER_W, INNER_H));

    tabs->SetActiveTab(0);
    DemoKit::Add(card, tabs, 1);
    return page;
}

} // namespace UltraCanvas
