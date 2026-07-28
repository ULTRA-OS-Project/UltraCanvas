// Apps/DemoApp/UltraCanvasGanttChartExamples.cpp
// Gantt chart demo. Every example lives on its own tab so it gets the full
// display area: a "Design Studio" tab whose control panel exercises the whole
// GanttChartStyle surface (design presets, palettes, bar/summary/milestone
// shapes, progress + dependency rendering, header and grid options, metrics),
// one tab per built-in design preset, and a palette gallery tab that shows the
// same schedule through all eight named palettes.
// Version: 2.0.0
// Last Modified: 2026-07-28
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasGanttChart.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDemo.h"

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
        auto lbl = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 44);
        lbl->SetText(text);
        lbl->SetFontSize(11);
        lbl->SetWrap(TextWrap::WrapWord);
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

    // Two-column cursor for the option panel: controls are placed left/right in
    // pairs, sections start on a fresh row with a heading.
    class PanelLayout {
    public:
        PanelLayout(std::shared_ptr<UltraCanvasContainer> target, float padding,
                    float columnWidth, float columnGap)
                : panel(std::move(target)), colW(columnWidth) {
            colX[0] = padding;
            colX[1] = padding + columnWidth + columnGap;
        }

        float ColumnWidth() const { return colW; }
        float X() const { return colX[col]; }
        float Y() const { return y; }

        // Reserve one cell of height `h`; the caller places its widget(s) at
        // (X(), Y()) before calling this.
        void Advance(float h) {
            rowHeight = h;
            if (++col >= 2) { col = 0; y += h; }
        }

        void EndRow() {
            if (col != 0) { col = 0; y += rowHeight; }
        }

        void Heading(const std::string& id, const std::string& text, float width) {
            EndRow();
            auto lbl = std::make_shared<UltraCanvasLabel>(id, colX[0], y, width, 18);
            lbl->SetText(text);
            lbl->SetFontSize(12);
            lbl->SetFontWeight(FontWeight::Bold);
            lbl->SetTextColor(Color(40, 50, 120, 255));
            panel->AddChild(lbl);
            y += 20;
        }

        std::shared_ptr<UltraCanvasLabel> Caption(const std::string& id,
                                                  const std::string& text) {
            auto lbl = std::make_shared<UltraCanvasLabel>(id, colX[col], y, colW, 14);
            lbl->SetText(text);
            lbl->SetFontSize(10);
            lbl->SetTextColor(Color(80, 80, 90, 255));
            panel->AddChild(lbl);
            return lbl;
        }

    private:
        std::shared_ptr<UltraCanvasContainer> panel;
        float colX[2] = {0, 0};
        float colW = 0;
        float y = 8;
        float rowHeight = 0;
        int col = 0;
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
    void AddEnumDropdown(PanelLayout& lay,
                         const std::shared_ptr<UltraCanvasContainer>& panel,
                         StudioControls& sc, const std::string& id,
                         const std::string& caption,
                         const std::vector<std::pair<std::string, T>>& menu,
                         std::function<void(GanttChartStyle&, T)> apply,
                         std::function<T(const GanttChartStyle&)> read) {
        lay.Caption(id + "Lbl", caption);
        auto dd = std::make_shared<UltraCanvasDropdown>(id, lay.X(), lay.Y() + 15,
                                                        lay.ColumnWidth(), 23);
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
        panel->AddChild(dd);
        sc.syncers.push_back([dd, chart, table, read]() {
            dd->SetSelectedIndex(MenuIndexOf(*table, read(chart->GetStyle())), false);
        });
        lay.Advance(44);
    }

    void AddCheck(PanelLayout& lay, const std::shared_ptr<UltraCanvasContainer>& panel,
                  StudioControls& sc, const std::string& id, const std::string& text,
                  std::function<void(GanttChartStyle&, bool)> apply,
                  std::function<bool(const GanttChartStyle&)> read) {
        auto cb = std::make_shared<UltraCanvasCheckbox>(id, lay.X(), lay.Y(),
                                                        lay.ColumnWidth(), 19);
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
        panel->AddChild(cb);
        sc.syncers.push_back([cb, chart, read]() {
            cb->SetChecked(read(chart->GetStyle()));
        });
        lay.Advance(21);
    }

    void AddSlider(PanelLayout& lay, const std::shared_ptr<UltraCanvasContainer>& panel,
                   StudioControls& sc, const std::string& id, const std::string& caption,
                   float minValue, float maxValue, float step, int decimals,
                   std::function<void(GanttChartStyle&, float)> apply,
                   std::function<float(const GanttChartStyle&)> read) {
        const float current = read(sc.chart->GetStyle());
        auto lbl = lay.Caption(id + "Lbl", caption + ": " + FormatFloat(current, decimals));

        auto sl = std::make_shared<UltraCanvasSlider>(id, lay.X(), lay.Y() + 15,
                                                      lay.ColumnWidth(), 20);
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
        panel->AddChild(sl);
        sc.syncers.push_back([sl, chart, read]() { sl->SetValue(read(chart->GetStyle())); });
        lay.Advance(41);
    }

    // Fill `panel` with the full option set for `chart`.
    void BuildStudioPanel(const std::shared_ptr<UltraCanvasContainer>& panel,
                          const std::shared_ptr<UltraCanvasGanttChartElement>& chart) {
        const float PAD = 12.0f, COL_W = 166.0f, COL_GAP = 12.0f;
        const float FULL_W = COL_W * 2 + COL_GAP;

        auto sc = std::make_shared<StudioControls>();
        sc->chart = chart;
        PanelLayout lay(panel, PAD, COL_W, COL_GAP);

        // ---- Design & colour -------------------------------------------------
        lay.Heading("GsHeadDesign", "Design & colour", FULL_W);

        // The design preset replaces the whole style, so it drives every other
        // control back into sync instead of writing a single field.
        lay.Caption("GsDesignLbl", "Design preset:");
        auto designDd = std::make_shared<UltraCanvasDropdown>("GsDesign", lay.X(), lay.Y() + 15,
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
        panel->AddChild(designDd);
        lay.Advance(44);

        // Palette: swaps only the bar colour cycle. Entry 0 restores whatever
        // palette the selected design preset ships with, so a design swap can
        // reset this dropdown to a truthful value instead of a stale name.
        lay.Caption("GsPaletteLbl", "Palette:");
        auto paletteDd = std::make_shared<UltraCanvasDropdown>("GsPalette", lay.X(), lay.Y() + 15,
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
        panel->AddChild(paletteDd);
        // After a preset swap the chart really is showing the design's own
        // palette again. Held weakly: this dropdown's own callback owns `sc`,
        // and a strong capture here would close the cycle.
        sc->syncers.push_back([weakPalette = std::weak_ptr<UltraCanvasDropdown>(paletteDd)]() {
            if (auto dd = weakPalette.lock()) dd->SetSelectedIndex(0, false);
        });
        lay.Advance(44);

        AddEnumDropdown<GanttColorMode>(lay, panel, *sc, "GsColorMode", "Colour mode:",
                ColorModeMenu(),
                [](GanttChartStyle& s, GanttColorMode v) { s.colorMode = v; },
                [](const GanttChartStyle& s) { return s.colorMode; });

        AddEnumDropdown<GanttBarStyle>(lay, panel, *sc, "GsBarStyle", "Task bars:",
                BarStyleMenu(),
                [](GanttChartStyle& s, GanttBarStyle v) { s.barStyle = v; },
                [](const GanttChartStyle& s) { return s.barStyle; });

        AddEnumDropdown<GanttSummaryStyle>(lay, panel, *sc, "GsSummary", "Summary bars:",
                SummaryStyleMenu(),
                [](GanttChartStyle& s, GanttSummaryStyle v) { s.summaryStyle = v; },
                [](const GanttChartStyle& s) { return s.summaryStyle; });

        AddEnumDropdown<GanttMilestoneStyle>(lay, panel, *sc, "GsMilestone", "Milestones:",
                MilestoneStyleMenu(),
                [](GanttChartStyle& s, GanttMilestoneStyle v) { s.milestoneStyle = v; },
                [](const GanttChartStyle& s) { return s.milestoneStyle; });

        AddEnumDropdown<GanttProgressStyle>(lay, panel, *sc, "GsProgress", "Progress:",
                ProgressStyleMenu(),
                [](GanttChartStyle& s, GanttProgressStyle v) { s.progressStyle = v; },
                [](const GanttChartStyle& s) { return s.progressStyle; });

        AddEnumDropdown<GanttDependencyLineStyle>(lay, panel, *sc, "GsDeps", "Dependencies:",
                DependencyStyleMenu(),
                [](GanttChartStyle& s, GanttDependencyLineStyle v) { s.dependencyStyle = v; },
                [](const GanttChartStyle& s) { return s.dependencyStyle; });

        AddEnumDropdown<GanttLabelPlacement>(lay, panel, *sc, "GsLabels", "Bar labels:",
                LabelPlacementMenu(),
                [](GanttChartStyle& s, GanttLabelPlacement v) { s.labelPlacement = v; },
                [](const GanttChartStyle& s) { return s.labelPlacement; });

        AddEnumDropdown<GanttTimeScale>(lay, panel, *sc, "GsScale", "Time scale:",
                TimeScaleMenu(),
                [](GanttChartStyle& s, GanttTimeScale v) { s.timeScale = v; },
                [](const GanttChartStyle& s) { return s.timeScale; });

        AddEnumDropdown<GanttDateFormat>(lay, panel, *sc, "GsDateFmt", "Date format:",
                DateFormatMenu(),
                [](GanttChartStyle& s, GanttDateFormat v) { s.dateFormat = v; },
                [](const GanttChartStyle& s) { return s.dateFormat; });

        // Table columns: a preset list rather than a single style field.
        lay.Caption("GsColumnsLbl", "Table columns:");
        auto columnsDd = std::make_shared<UltraCanvasDropdown>("GsColumns", lay.X(), lay.Y() + 15,
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
        panel->AddChild(columnsDd);
        sc->syncers.push_back([weakColumns = std::weak_ptr<UltraCanvasDropdown>(columnsDd),
                               chart]() {
            if (auto dd = weakColumns.lock())
                dd->SetSelectedIndex(ColumnPresetIndexFor(chart->GetStyle().columns), false);
        });
        lay.Advance(44);

        // ---- Elements --------------------------------------------------------
        lay.Heading("GsHeadElements", "Chart elements", FULL_W);

        AddCheck(lay, panel, *sc, "GsShowTable", "Task table",
                 [](GanttChartStyle& s, bool v) { s.showTable = v; },
                 [](const GanttChartStyle& s) { return s.showTable; });
        AddCheck(lay, panel, *sc, "GsTwoTier", "Two-tier header",
                 [](GanttChartStyle& s, bool v) { s.twoTierHeader = v; },
                 [](const GanttChartStyle& s) { return s.twoTierHeader; });
        AddCheck(lay, panel, *sc, "GsWeekdays", "Weekday letters",
                 [](GanttChartStyle& s, bool v) { s.showWeekdayLetters = v; },
                 [](const GanttChartStyle& s) { return s.showWeekdayLetters; });
        AddCheck(lay, panel, *sc, "GsUpper", "Uppercase header",
                 [](GanttChartStyle& s, bool v) { s.uppercaseHeader = v; },
                 [](const GanttChartStyle& s) { return s.uppercaseHeader; });
        AddCheck(lay, panel, *sc, "GsRowLines", "Row lines",
                 [](GanttChartStyle& s, bool v) { s.showRowLines = v; },
                 [](const GanttChartStyle& s) { return s.showRowLines; });
        AddCheck(lay, panel, *sc, "GsColLines", "Column lines",
                 [](GanttChartStyle& s, bool v) { s.showColumnLines = v; },
                 [](const GanttChartStyle& s) { return s.showColumnLines; });
        AddCheck(lay, panel, *sc, "GsWeekend", "Shade weekends",
                 [](GanttChartStyle& s, bool v) { s.shadeWeekends = v; },
                 [](const GanttChartStyle& s) { return s.shadeWeekends; });
        AddCheck(lay, panel, *sc, "GsStripes", "Row striping",
                 [](GanttChartStyle& s, bool v) {
                     s.rowAlternateColor = v ? Color(0, 0, 0, 12) : Colors::Transparent;
                 },
                 [](const GanttChartStyle& s) { return s.rowAlternateColor.a != 0; });
        AddCheck(lay, panel, *sc, "GsBoldSummary", "Bold summary rows",
                 [](GanttChartStyle& s, bool v) { s.boldSummaryRows = v; },
                 [](const GanttChartStyle& s) { return s.boldSummaryRows; });
        AddCheck(lay, panel, *sc, "GsToday", "Today marker",
                 [](GanttChartStyle& s, bool v) { s.showTodayLine = v; },
                 [](const GanttChartStyle& s) { return s.showTodayLine; });
        AddCheck(lay, panel, *sc, "GsCritical", "Critical path",
                 [](GanttChartStyle& s, bool v) { s.highlightCriticalPath = v; },
                 [](const GanttChartStyle& s) { return s.highlightCriticalPath; });
        AddCheck(lay, panel, *sc, "GsDashed", "Dashed arrows",
                 [](GanttChartStyle& s, bool v) { s.dependencyDashed = v; },
                 [](const GanttChartStyle& s) { return s.dependencyDashed; });
        AddCheck(lay, panel, *sc, "GsDepColor", "Arrows use bar colour",
                 [](GanttChartStyle& s, bool v) { s.dependencyUsesBarColor = v; },
                 [](const GanttChartStyle& s) { return s.dependencyUsesBarColor; });
        AddCheck(lay, panel, *sc, "GsProgressText", "Progress % text",
                 [](GanttChartStyle& s, bool v) { s.showProgressText = v; },
                 [](const GanttChartStyle& s) { return s.showProgressText; });
        AddCheck(lay, panel, *sc, "GsAssignee", "Assignee in labels",
                 [](GanttChartStyle& s, bool v) { s.showAssigneeWithLabel = v; },
                 [](const GanttChartStyle& s) { return s.showAssigneeWithLabel; });
        AddCheck(lay, panel, *sc, "GsBarBorder", "Bar outline",
                 [](GanttChartStyle& s, bool v) {
                     s.barBorderColor = v ? Color(0, 0, 0, 70) : Colors::Transparent;
                     s.barBorderWidth = v ? 1.0f : 0.0f;
                 },
                 [](const GanttChartStyle& s) { return s.barBorderWidth > 0.0f; });
        lay.EndRow();

        // ---- Metrics ---------------------------------------------------------
        lay.Heading("GsHeadMetrics", "Metrics", FULL_W);

        AddSlider(lay, panel, *sc, "GsDayWidth", "Day width", 1.5f, 40.0f, 0.5f, 1,
                  [](GanttChartStyle& s, float v) { s.dayWidth = v; },
                  [](const GanttChartStyle& s) { return s.dayWidth; });
        AddSlider(lay, panel, *sc, "GsRowHeight", "Row height", 18.0f, 48.0f, 1.0f, 0,
                  [](GanttChartStyle& s, float v) { s.rowHeight = v; },
                  [](const GanttChartStyle& s) { return s.rowHeight; });
        AddSlider(lay, panel, *sc, "GsBarRatio", "Bar height", 0.25f, 0.9f, 0.02f, 2,
                  [](GanttChartStyle& s, float v) { s.barHeightRatio = v; },
                  [](const GanttChartStyle& s) { return s.barHeightRatio; });
        AddSlider(lay, panel, *sc, "GsCorner", "Corner radius", 0.0f, 14.0f, 1.0f, 0,
                  [](GanttChartStyle& s, float v) { s.barCornerRadius = v; },
                  [](const GanttChartStyle& s) { return s.barCornerRadius; });
        AddSlider(lay, panel, *sc, "GsMsSize", "Milestone size", 0.2f, 0.9f, 0.05f, 2,
                  [](GanttChartStyle& s, float v) { s.milestoneSizeRatio = v; },
                  [](const GanttChartStyle& s) { return s.milestoneSizeRatio; });
        AddSlider(lay, panel, *sc, "GsDepWidth", "Arrow width", 0.5f, 4.0f, 0.1f, 1,
                  [](GanttChartStyle& s, float v) { s.dependencyLineWidth = v; },
                  [](const GanttChartStyle& s) { return s.dependencyLineWidth; });
        lay.EndRow();

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
    std::shared_ptr<UltraCanvasContainer> MakePaletteCell(size_t index, int cellW, int cellH) {
        const auto& menu = PaletteMenu();
        const std::string suffix = std::to_string(index);

        auto cell = std::make_shared<UltraCanvasContainer>("GanttPalCell" + suffix,
                                                           0, 0, cellW, cellH);
        cell->layout.SetFlexColumn().SetFlexGap(2)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        auto caption = std::make_shared<UltraCanvasLabel>("GanttPalCap" + suffix, 0, 0, 0, 16);
        caption->SetText(menu[index].first);
        caption->SetFontSize(11);
        caption->SetFontWeight(FontWeight::Bold);
        caption->SetTextColor(Color(50, 55, 70, 255));
        AddFlexChild(cell, caption, 0);

        auto chart = CreateGanttChartWithData("GanttPal" + suffix, 0, 0, cellW, cellH - 18,
                                              BuildLaunchProject(), GanttDesign::Professional);
        chart->SetPalette(menu[index].second);
        chart->SetColumns({GanttColumn(GanttColumnType::Name, "Task", 112)});
        chart->SetToday(kToday);
        auto& st = chart->EditStyle();
        st.rowHeight = 18.0f;
        st.dayWidth = 2.6f;
        st.timeScale = GanttTimeScale::Months;
        st.tableFontSize = 9.0f;
        st.headerFontSize = 9.0f;
        st.headerTierHeight = 16.0f;
        st.tableIndentPerLevel = 9.0f;
        st.labelPlacement = GanttLabelPlacement::Hidden;
        st.showProgressText = false;
        st.summaryBarColor = Colors::Transparent;   // summaries follow the palette
        chart->StyleChanged();
        AddFlexChild(cell, chart, 1);
        return cell;
    }

    // Palette gallery: the same schedule rendered eight times, one per named
    // palette, in a 4 x 2 grid that follows the page size.
    std::shared_ptr<UltraCanvasContainer> MakePaletteTab(int innerW, int innerH) {
        auto tab = std::make_shared<UltraCanvasContainer>("GanttTabPalettes", 0, 0, innerW, innerH);
        tab->SetBackgroundColor(Color(255, 255, 255, 255));
        tab->layout.SetFlexColumn().SetFlexGap(6)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        AddFlexChild(tab, MakeTabDescription("GanttPalDesc",
                "The eight named palettes from GanttPalettes::Get(), applied to one shared design. "
                "SetPalette() swaps only the bar colour cycle - layout, header and progress "
                "rendering stay exactly as the design preset left them."), 0);

        const int cols = 4, rows = 2;
        const int cellW = innerW / cols;
        const int cellH = (innerH - 50) / rows;

        for (int r = 0; r < rows; ++r) {
            auto band = std::make_shared<UltraCanvasContainer>(
                    "GanttPalRow" + std::to_string(r), 0, 0, innerW, cellH);
            band->layout.SetFlexRow().SetFlexGap(8)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
            band->SetMargin(0, 10, 0, 10);
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
    const int CONTAINER_W = 1180;
    const int CONTAINER_H = 800;
    const int PAD = 16;
    const int INNER_W = CONTAINER_W - 2 * PAD - 8;
    const int INNER_H = 660;

    auto root = std::make_shared<UltraCanvasContainer>("GanttRoot", 0, 0,
                                                       CONTAINER_W, CONTAINER_H);
    root->SetBackgroundColor(Color(250, 250, 252, 255));
    root->SetPadding(PAD);
    root->layout.SetFlexColumn().SetFlexGap(8)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto title = std::make_shared<UltraCanvasLabel>("GanttTitle", 0, 0, 0, 28);
    title->SetText("Gantt Charts - Design Studio, Presets & Palettes");
    title->SetFontSize(18);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(40, 50, 120, 255));
    AddFlexChild(root, title, 0);

    auto desc = std::make_shared<UltraCanvasLabel>("GanttDesc", 0, 0, 0, 20);
    desc->SetText("Every example has its own tab. In all charts the wheel scrolls, "
                  "Ctrl+wheel zooms the timeline, clicking selects a task and the "
                  "triangles collapse phases.");
    desc->SetFontSize(11);
    desc->SetWrap(TextWrap::WrapWord);
    desc->SetTextColor(Color(90, 90, 100, 255));
    AddFlexChild(root, desc, 0);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("GanttTabs", 0, 0,
                                                             CONTAINER_W - 2 * PAD, INNER_H + 40);
    tabs->SetTabStyle(TabStyle::Rounded);
    tabs->SetTabHeight(30);
    tabs->SetTabCornerRadius(8.0f);
    tabs->SetTabMaxWidth(180);

    // ---- Tab 1: Design Studio (all appearance options) ----------------------
    auto studio = std::make_shared<UltraCanvasContainer>("GanttTabStudio", 0, 0, INNER_W, INNER_H);
    studio->SetBackgroundColor(Color(255, 255, 255, 255));
    studio->layout.SetFlexRow().SetFlexGap(10)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    const float PANEL_W = 372.0f;
    auto panel = std::make_shared<UltraCanvasContainer>("GanttStudioPanel", 0, 0,
                                                        static_cast<int>(PANEL_W), INNER_H);
    panel->SetBackgroundColor(Color(247, 247, 250, 255));
    panel->size.width = CSSLayout::Dimension::Px(PANEL_W);

    auto studioChart = CreateGanttChartWithData("GanttStudioChart", 0, 0, 740, INNER_H,
                                                BuildLaunchProject(), GanttDesign::Modern);
    studioChart->SetToday(kToday);
    studioChart->EditStyle().dayWidth = 14.0f;
    studioChart->StyleChanged();

    BuildStudioPanel(panel, studioChart);

    AddFlexChild(studio, panel, 0);         // fixed-width option panel
    AddFlexChild(studio, studioChart, 1);   // chart fills the remaining width
    tabs->AddTab("Design Studio", studio);

    // ---- Tabs 2..7: one per built-in design preset --------------------------
    tabs->AddTab("Modern", MakeDesignTab("GanttTabModern",
            "Modern preset: pill bars on a vibrant palette, two-tier day header with weekday "
            "letters, phase names inside the summary bars, and elbow arrows that take the "
            "colour of their predecessor bar.",
            [](const std::shared_ptr<UltraCanvasGanttChartElement>& c) {
                c->EditStyle().dayWidth = 16.0f;
            },
            GanttDesign::Modern, INNER_W, INNER_H));

    tabs->AddTab("Professional", MakeDesignTab("GanttTabPro",
            "Professional preset: the project-tool look - bracket summary bars, darker "
            "progress fills with a % readout, task name plus assignee to the right of each "
            "bar, and the critical path highlighted in red.",
            [](const std::shared_ptr<UltraCanvasGanttChartElement>& c) {
                c->SetShowCriticalPath(true);
                c->EditStyle().dayWidth = 10.0f;
            },
            GanttDesign::Professional, INNER_W, INNER_H));

    tabs->AddTab("Classic", MakeDesignTab("GanttTabClassic",
            "Classic preset: the flat single-colour print style used in business plans - one "
            "corporate blue for every bar, a single-tier month header, no weekend shading and "
            "solid elbow dependency arrows.",
            [](const std::shared_ptr<UltraCanvasGanttChartElement>& c) {
                c->EditStyle().timeScale = GanttTimeScale::Weeks;
                c->EditStyle().dayWidth = 7.0f;
            },
            GanttDesign::Classic, INNER_W, INNER_H));

    tabs->AddTab("Soft", MakeDesignTab("GanttTabSoft",
            "Soft preset: a pastel report style - thin line bars with square end caps, striped "
            "rows under a dark table header, and dashed orange dependency arrows between the "
            "activities.",
            [](const std::shared_ptr<UltraCanvasGanttChartElement>& c) {
                c->EditStyle().dayWidth = 11.0f;
            },
            GanttDesign::Soft, INNER_W, INNER_H));

    tabs->AddTab("Minimal", MakeDesignTab("GanttTabMinimal",
            "Minimal preset: a wide project table carrying assignee, priority chips and "
            "% complete, with light-track progress bars on a day grid. Dependency arrows are "
            "off so the table stays the focus.",
            [](const std::shared_ptr<UltraCanvasGanttChartElement>& c) {
                c->EditStyle().dayWidth = 11.0f;
            },
            GanttDesign::Minimal, INNER_W, INNER_H));

    tabs->AddTab("Dark", MakeDesignTab("GanttTabDark",
            "Dark preset: the Modern layout on a dark surface, here combined with the Ocean "
            "palette and a today marker. Every colour in GanttChartStyle is independent, so a "
            "dark theme is a palette plus a handful of surface colours.",
            [](const std::shared_ptr<UltraCanvasGanttChartElement>& c) {
                c->SetPalette(GanttPalette::Ocean);
                c->EditStyle().dayWidth = 15.0f;
            },
            GanttDesign::Dark, INNER_W, INNER_H));

    // ---- Tab 8: palette gallery ---------------------------------------------
    tabs->AddTab("Palette Gallery", MakePaletteTab(INNER_W, INNER_H));

    tabs->SetActiveTab(0);
    AddFlexChild(root, tabs, 1);
    return root;
}

} // namespace UltraCanvas
