// include/Plugins/Charts/UltraCanvasGanttChart.h
// Gantt chart element: project schedule visualization with a task table,
// timeline grid, hierarchy, dependencies, milestones, progress tracking,
// critical path analysis, and a preset-based design/palette system.
// Version: 1.0.0
// Last Modified: 2026-07-26
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasChartDataStructures.h"
#include "UltraCanvasCalendarDate.h"
#include "UltraCanvasSmoothScroll.h"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// DATE TYPE
// =============================================================================

    // Calendar date stored as a serial day count since 1970-01-01 (proleptic
    // Gregorian, can be negative). Cheap to copy and compare; conversions use
    // the CalendarDate helpers shared with the calendar heatmap.
    struct GanttDate {
        long serial = 0;

        GanttDate() = default;
        explicit GanttDate(long serialDays) : serial(serialDays) {}
        GanttDate(int year, int month, int day)
                : serial(CalendarDaysFromCivil(year, month, day)) {}

        static GanttDate FromYMD(int year, int month, int day) {
            return GanttDate(year, month, day);
        }

        void ToYMD(int& year, int& month, int& day) const {
            CalendarCivilFromDays(serial, year, month, day);
        }

        int Year() const  { int y, m, d; ToYMD(y, m, d); return y; }
        int Month() const { int y, m, d; ToYMD(y, m, d); return m; }
        int Day() const   { int y, m, d; ToYMD(y, m, d); return d; }

        // Sunday = 0 ... Saturday = 6
        int Weekday() const { return CalendarWeekdaySunday0(serial); }
        bool IsWeekend() const { int wd = Weekday(); return wd == 0 || wd == 6; }

        GanttDate AddDays(long days) const { return GanttDate(serial + days); }
        long DaysUntil(const GanttDate& other) const { return other.serial - serial; }

        bool operator==(const GanttDate& o) const { return serial == o.serial; }
        bool operator!=(const GanttDate& o) const { return serial != o.serial; }
        bool operator<(const GanttDate& o) const  { return serial < o.serial; }
        bool operator<=(const GanttDate& o) const { return serial <= o.serial; }
        bool operator>(const GanttDate& o) const  { return serial > o.serial; }
        bool operator>=(const GanttDate& o) const { return serial >= o.serial; }
    };

    // Date rendering format used by the table columns and tooltips.
    enum class GanttDateFormat {
        DayMonthYearSlash,   // 25/12/26
        MonthDayYearSlash,   // 12/25/26
        ISO,                 // 2026-12-25
        DayMonthShort        // 25 Dec
    };

// =============================================================================
// TASK MODEL
// =============================================================================

    enum class GanttTaskShape {
        Auto,        // Summary when the task has children, milestone when
                     // start == end and flagged, otherwise a normal bar
        Bar,         // Force normal task bar
        Summary,     // Force summary rendering
        Milestone    // Force milestone marker (zero-duration point event)
    };

    enum class GanttPriority {
        NoPriority,
        Low,
        Normal,
        High,
        Urgent
    };

    struct GanttTask {
        int id = -1;                  // Unique id (assigned by the data source)
        int parentId = -1;            // -1 = top-level task
        std::string name;
        GanttDate start;              // First day of work (inclusive)
        GanttDate end;                // Last day of work (inclusive)
        float progress = 0.0f;        // Completion 0.0 .. 1.0
        GanttTaskShape shape = GanttTaskShape::Auto;
        GanttPriority priority = GanttPriority::NoPriority;
        std::string assignee;
        std::string notes;            // Extra line shown in the tooltip
        Color customColor = Colors::Transparent; // Transparent = palette color
        bool expanded = true;         // Children visible (summaries only)

        // Inclusive duration in days (a one-day task returns 1).
        long DurationDays() const { return start.DaysUntil(end) + 1; }
    };

    enum class GanttDependencyType {
        FinishToStart,   // Successor starts after predecessor finishes (default)
        StartToStart,    // Successor starts with predecessor
        FinishToFinish,  // Successor finishes with predecessor
        StartToFinish    // Successor finishes when predecessor starts (rare)
    };

    struct GanttDependency {
        int predecessorId = -1;
        int successorId = -1;
        GanttDependencyType type = GanttDependencyType::FinishToStart;
        int lagDays = 0;             // Positive = wait, negative = overlap
    };

// =============================================================================
// DATA SOURCE
// =============================================================================

    // A flat visible row prepared for rendering: resolved hierarchy order,
    // depth, WBS number ("2.1"), and effective (possibly rolled-up) dates.
    struct GanttRow {
        int taskId = -1;
        int depth = 0;
        bool isSummary = false;
        bool isMilestone = false;
        bool hasChildren = false;
        bool expanded = true;
        bool critical = false;
        std::string wbs;             // "1", "1.2", "1.2.3", ...
        GanttDate start, end;        // Effective dates (summaries roll up)
        float progress = 0.0f;       // Effective progress (summaries roll up)
    };

    class GanttDataSource : public IChartDataSource {
    private:
        std::vector<GanttTask> tasks;                 // Insertion order preserved
        std::vector<GanttDependency> dependencies;
        int nextId = 1;
        uint64_t version = 1;                         // Bumped on every mutation

        // Critical-path results (task ids), valid for the version they were
        // computed at.
        mutable std::vector<int> criticalIds;
        mutable uint64_t criticalVersion = 0;

    public:
        // ===== TASK MANAGEMENT =====

        // Adds a task and returns its id (-1 on invalid input, e.g. an
        // unknown parent or end date before start date).
        int AddTask(const std::string& name, const GanttDate& start,
                    const GanttDate& end, int parentId = -1);
        int AddTask(const GanttTask& task);           // id field is ignored
        int AddMilestone(const std::string& name, const GanttDate& date,
                         int parentId = -1);

        bool RemoveTask(int taskId);                  // Removes children + deps too
        void Clear();

        GanttTask* FindTask(int taskId);
        const GanttTask* FindTask(int taskId) const;

        bool SetTaskDates(int taskId, const GanttDate& start, const GanttDate& end);
        bool SetTaskProgress(int taskId, float progress);
        bool SetTaskColor(int taskId, const Color& color);
        bool SetTaskAssignee(int taskId, const std::string& assignee);
        bool SetTaskPriority(int taskId, GanttPriority priority);
        bool SetTaskNotes(int taskId, const std::string& notes);
        bool SetTaskExpanded(int taskId, bool expanded);
        void SetAllExpanded(bool expanded);

        size_t GetTaskCount() const { return tasks.size(); }
        const std::vector<GanttTask>& GetTasks() const { return tasks; }

        // ===== DEPENDENCIES =====

        // Returns false for unknown ids, self-links, or duplicates.
        bool AddDependency(int predecessorId, int successorId,
                           GanttDependencyType type = GanttDependencyType::FinishToStart,
                           int lagDays = 0);
        bool RemoveDependency(int predecessorId, int successorId);
        const std::vector<GanttDependency>& GetDependencies() const { return dependencies; }

        // Predecessor tasks of `taskId` (used for tooltips).
        std::vector<int> GetPredecessors(int taskId) const;

        // ===== DERIVED DATA =====

        // Flattened list of currently visible rows in display order, with
        // WBS numbers, depth, and rolled-up summary dates/progress.
        // Collapsed subtrees are skipped. Includes critical flags when
        // markCritical is true.
        std::vector<GanttRow> BuildVisibleRows(bool markCritical = false) const;

        // Overall date range across all tasks; false when empty.
        bool GetDateRange(GanttDate& min, GanttDate& max) const;

        // Standard PDM forward/backward pass over the dependency graph.
        // Tasks with zero total slack form the critical path. Results are
        // cached until the next mutation.
        const std::vector<int>& ComputeCriticalPath() const;

        // Effective (rolled-up) values for one task.
        void GetEffectiveTaskSpan(const GanttTask& task, GanttDate& start,
                                  GanttDate& end, float& progress) const;

        bool TaskHasChildren(int taskId) const;
        bool IsSummary(const GanttTask& task) const;
        bool IsMilestone(const GanttTask& task) const;

        // Monotonic change counter so the element can invalidate caches.
        uint64_t GetVersion() const { return version; }

        // ===== IChartDataSource =====
        size_t GetPointCount() const override { return tasks.size(); }
        ChartDataPoint GetPoint(size_t index) override;
        bool SupportsStreaming() const override { return false; }
        void LoadFromCSV(const std::string& filePath) override {}
        void LoadFromArray(const std::vector<ChartDataPoint>& data) override {}

    private:
        void Touch() { ++version; }
        void CollectSubtree(int taskId, std::vector<int>& out) const;
    };

// =============================================================================
// STYLE SYSTEM
// =============================================================================

    enum class GanttTimeScale {
        Auto,        // Chosen from the day width: days / weeks / months / quarters
        Days,        // Minor tier = single days, major tier = months
        Weeks,       // Minor tier = weeks, major tier = months
        Months,      // Minor tier = months, major tier = years
        Quarters     // Minor tier = quarters, major tier = years
    };

    enum class GanttBarStyle {
        Flat,        // Square-cornered solid bars (classic print style)
        Rounded,     // Rounded corners
        Pill,        // Fully rounded ends
        Gradient,    // Vertical gradient fill
        Line         // Thin line with square end caps (report/poster style)
    };

    enum class GanttSummaryStyle {
        Bar,         // Solid bar, optionally with the name inside
        Bracket,     // Thin bar with downward end notches (project-tool style)
        Line         // Thin plain bar
    };

    enum class GanttMilestoneStyle {
        Diamond,
        Circle,
        Square
    };

    enum class GanttProgressStyle {
        Hidden,      // No progress display
        DarkerFill,  // Completed part overlaid in a darker shade
        LightTrack,  // Bar drawn as a light track, completed part solid
        InnerBar     // Thin centered stripe over the bar
    };

    enum class GanttDependencyLineStyle {
        Hidden,
        Elbow,       // Orthogonal connectors with arrowheads
        Straight     // Direct diagonal lines
    };

    enum class GanttLabelPlacement {
        Hidden,
        InsideBar,   // Task name inside the bar (when it fits)
        RightOfBar,  // Task name (and optional extras) after the bar
        AutoPlace    // Inside when it fits, otherwise to the right
    };

    // How bars pick their color from the palette.
    enum class GanttColorMode {
        ByPhase,     // All tasks share their top-level ancestor's palette color
        ByRow,       // Each row cycles through the palette
        ByPriority,  // Color from the task priority
        Single       // First palette entry for everything
    };

    // Columns available in the left task table.
    enum class GanttColumnType {
        Wbs,         // Hierarchical number: 1, 1.1, 1.2 ...
        Name,        // Task name (indented, with expand/collapse control)
        StartDate,
        EndDate,
        Duration,    // "12d"
        Progress,    // "45%"
        Assignee,
        Priority     // Colored chip with the priority name
    };

    struct GanttColumn {
        GanttColumnType type = GanttColumnType::Name;
        std::string title;           // Header caption
        float width = 100.0f;        // Pixels

        GanttColumn() = default;
        GanttColumn(GanttColumnType t, const std::string& caption, float w)
                : type(t), title(caption), width(w) {}
    };

    // Complete visual description of a Gantt chart. Design presets fill this
    // in; every field can be adjusted afterwards.
    struct GanttChartStyle {
        // ----- Layout metrics -----
        float rowHeight = 34.0f;
        float barHeightRatio = 0.58f;      // Bar height as a fraction of the row
        float barCornerRadius = 4.0f;      // Used by Rounded bar style
        float headerTierHeight = 24.0f;    // Height of each header tier
        bool twoTierHeader = true;         // Major + minor tier, or minor only
        float dayWidth = 26.0f;            // Timeline pixels per day (zoom level)
        float tableTimelineGap = 0.0f;     // Gap between table and timeline

        // ----- Task table -----
        bool showTable = true;
        std::vector<GanttColumn> columns;  // Empty = sensible default set
        Color tableBackground = Color(255, 255, 255);
        Color tableHeaderBackground = Color(235, 235, 235);
        Color tableHeaderText = Color(60, 60, 60);
        Color tableText = Color(40, 40, 40);
        Color tableSummaryText = Color(20, 20, 30);
        bool boldSummaryRows = true;
        float tableFontSize = 12.0f;
        float tableIndentPerLevel = 16.0f;

        // ----- Timeline header -----
        Color headerBackground = Color(235, 235, 235);
        Color headerText = Color(60, 60, 60);
        Color headerBorder = Color(200, 200, 200);
        float headerFontSize = 12.0f;
        bool uppercaseHeader = false;      // "WEEK 01" / "OCTOBER" style captions
        bool showWeekdayLetters = true;    // M T W T F S S under day numbers

        // ----- Grid & rows -----
        Color background = Color(255, 255, 255);
        Color rowLineColor = Color(228, 228, 228);
        Color columnLineColor = Color(235, 235, 235);
        Color rowAlternateColor = Colors::Transparent; // Transparent = no striping
        Color summaryRowColor = Colors::Transparent;   // Row band behind summaries
        bool showRowLines = true;
        bool showColumnLines = true;
        bool shadeWeekends = true;
        Color weekendShade = Color(0, 0, 0, 14);

        // ----- Today marker -----
        bool showTodayLine = false;
        Color todayLineColor = Color(230, 74, 69);
        GanttDate today;                   // Set explicitly by the caller

        // ----- Bars -----
        GanttBarStyle barStyle = GanttBarStyle::Rounded;
        GanttSummaryStyle summaryStyle = GanttSummaryStyle::Bar;
        GanttMilestoneStyle milestoneStyle = GanttMilestoneStyle::Diamond;
        Color milestoneColor = Colors::Transparent;    // Transparent = palette
        float milestoneSizeRatio = 0.45f;  // Marker size vs row height
        Color barBorderColor = Colors::Transparent;    // Transparent = none
        float barBorderWidth = 0.0f;

        // ----- Progress -----
        GanttProgressStyle progressStyle = GanttProgressStyle::DarkerFill;
        float progressDarken = 0.28f;      // Shade factor for DarkerFill
        float trackLighten = 0.68f;        // Tint factor for LightTrack
        bool showProgressText = false;     // "45%" after the bar

        // ----- Bar labels -----
        GanttLabelPlacement labelPlacement = GanttLabelPlacement::Hidden;
        bool labelSummariesOnly = false;   // Only summaries carry inside labels
        Color barLabelColor = Colors::White;           // Inside-bar text
        Color outsideLabelColor = Color(60, 60, 60);   // Right-of-bar text
        float barLabelFontSize = 11.0f;
        bool showAssigneeWithLabel = false; // Append assignee to outside labels

        // ----- Dependencies -----
        GanttDependencyLineStyle dependencyStyle = GanttDependencyLineStyle::Elbow;
        bool dependencyDashed = false;
        bool dependencyUsesBarColor = false; // Line takes predecessor bar color
        Color dependencyColor = Color(120, 130, 145);
        float dependencyLineWidth = 1.4f;
        float dependencyArrowSize = 6.0f;

        // ----- Critical path -----
        bool highlightCriticalPath = false;
        Color criticalColor = Color(217, 48, 37);

        // ----- Colors / palette -----
        GanttColorMode colorMode = GanttColorMode::ByPhase;
        std::vector<Color> palette;        // Empty = corporate blue default
        Color summaryBarColor = Colors::Transparent;   // Transparent = palette

        // ----- Selection / hover -----
        Color selectionColor = Color(0, 120, 215, 34);
        Color hoverColor = Color(0, 120, 215, 16);

        // ----- Fonts / misc -----
        std::string fontFamily;            // Empty = system default
        GanttDateFormat dateFormat = GanttDateFormat::DayMonthYearSlash;
        GanttTimeScale timeScale = GanttTimeScale::Auto;

        // Default column set: WBS, Name, Start, End, Duration.
        static std::vector<GanttColumn> DefaultColumns();
        // Wider set used by the Minimal/Professional presets:
        // Name, Assignee, Priority, Start, End, Duration, Progress.
        static std::vector<GanttColumn> ProjectColumns();

        float TotalTableWidth() const {
            float w = 0;
            for (const auto& c : columns) w += c.width;
            return w;
        }
        float HeaderHeight() const {
            return twoTierHeader ? headerTierHeight * 2 : headerTierHeight;
        }
    };

// =============================================================================
// PALETTES & DESIGN PRESETS
// =============================================================================

    enum class GanttPalette {
        CorporateBlue,   // Single indigo-blue family (annual-report style)
        Vibrant,         // Purple / azure / magenta / red / orange / green
        Pastel,          // Soft pink / green / yellow / cyan
        Ocean,           // Teal-blue gradient family
        Sunset,          // Orange-red-pink family
        Forest,          // Green family
        Slate,           // Muted blue-gray professional tones
        Mono             // Grayscale
    };

    namespace GanttPalettes {
        // Task bar color cycles for each named palette.
        const std::vector<Color>& Get(GanttPalette palette);
        // Chip/bar colors for priorities (NoPriority..Urgent).
        const std::vector<Color>& PriorityColors();
    }

    // Ready-made designs modeled on common real-world Gantt styles.
    enum class GanttDesign {
        Classic,       // Print style: flat blue bars, month header, elbow arrows
        Modern,        // Rounded colorful bars, week + weekday header, labels in
                       // summary bars, arrows between phases
        Professional,  // Project-tool look: bracket summaries, progress fills,
                       // names right of bars, diamond milestones
        Soft,          // Pastel report style: thin line bars with square end
                       // caps, dashed dependency arrows, month/week header
        Minimal,       // Light minimal-modern: wide info table (assignee,
                       // priority, % complete), light-track progress bars,
                       // day grid with weekday letters
        Dark           // Dark background variant of Modern
    };

    namespace GanttChartStyles {
        GanttChartStyle CreateClassic();
        GanttChartStyle CreateModern();
        GanttChartStyle CreateProfessional();
        GanttChartStyle CreateSoft();
        GanttChartStyle CreateMinimal();
        GanttChartStyle CreateDark();
        GanttChartStyle Create(GanttDesign design);
    }

// =============================================================================
// GANTT CHART ELEMENT
// =============================================================================

    class UltraCanvasGanttChartElement : public UltraCanvasChartElementBase {
    public:
        UltraCanvasGanttChartElement(const std::string& id, int x, int y,
                                     int width, int height);

        // ===== DATA =====
        void SetGanttDataSource(std::shared_ptr<GanttDataSource> ds);
        std::shared_ptr<GanttDataSource> GetGanttDataSource() const {
            return std::dynamic_pointer_cast<GanttDataSource>(dataSource);
        }

        // ===== STYLE =====
        void SetStyle(const GanttChartStyle& newStyle);
        const GanttChartStyle& GetStyle() const { return style; }
        // Mutate the style in place, then call StyleChanged().
        GanttChartStyle& EditStyle() { return style; }
        void StyleChanged();

        // Replace the whole style with a design preset (data untouched).
        void ApplyDesign(GanttDesign design);
        // Swap only the bar color cycle.
        void SetPalette(GanttPalette palette);
        void SetCustomPalette(const std::vector<Color>& colors);

        // ===== COMMON STYLE SHORTCUTS =====
        void SetTimeScale(GanttTimeScale scale);
        void SetDayWidth(float pixels);          // Zoom level
        void SetShowTable(bool show);
        void SetColumns(const std::vector<GanttColumn>& columns);
        void SetShowDependencies(bool show);
        void SetShowCriticalPath(bool show);
        void SetToday(const GanttDate& date, bool showLine = true);

        // ===== VIEW CONTROL =====
        // Scroll so `date` is at the left edge of the timeline.
        void ScrollToDate(const GanttDate& date);
        void ScrollToTask(int taskId);
        // Choose a day width so the whole project fits the timeline width.
        void FitToRange();

        // ===== SELECTION =====
        int GetSelectedTaskId() const { return selectedTaskId; }
        void SelectTask(int taskId);             // -1 clears

        // ===== CALLBACKS =====
        std::function<void(int taskId)> onTaskClick;
        std::function<void(int taskId)> onTaskDoubleClick;
        std::function<void(int taskId, bool expanded)> onTaskToggled;
        std::function<void(int taskId)> onSelectionChanged; // -1 = cleared

        // ===== FRAMEWORK OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // ----- Resolved per-frame layout -----
        struct Layout {
            Rect2Dd tableHeader, tableBody;    // Left panel
            Rect2Dd timeHeader, timeBody;      // Right panel
            GanttDate viewStart;               // Date at scroll offset 0
            long totalDays = 0;                // Padded project span
            float unitDays = 1.0f;             // Days per minor column
            GanttTimeScale effectiveScale = GanttTimeScale::Days;
            float contentHeight = 0;           // rows * rowHeight
            float contentWidth = 0;            // totalDays * dayWidth
        };

        GanttChartStyle style;
        std::vector<GanttRow> rows;            // Visible rows cache
        uint64_t rowsVersion = 0;              // Data version rows were built at
        bool rowsDirty = true;

        Layout layout;
        float scrollX = 0, scrollY = 0;
        // Wheel scrolling glides to its target (see UltraCanvasSmoothScroll.h);
        // Ctrl-zoom, a pan drag and ScrollTo* reposition the timeline and land
        // at once.
        UltraCanvasSmoothScroll scrollAnimX;
        UltraCanvasSmoothScroll scrollAnimY;
        int selectedTaskId = -1;
        int hoveredRowIndex = -1;
        bool panningTimeline = false;
        Point2Di panAnchor;

        // ----- Data / layout helpers -----
        void EnsureRows();
        void ComputeLayout();
        GanttTimeScale ResolveScale() const;
        double DateToX(const GanttDate& date) const;   // Local timeline coords
        float RowTop(size_t rowIndex) const;
        const GanttRow* RowAt(float localY, int* indexOut = nullptr) const;
        int RowIndexOfTask(int taskId) const;
        Color BarColorForRow(size_t rowIndex, const GanttRow& row) const;
        Color ResolveTaskColor(const GanttTask& task, size_t rowIndex,
                               const GanttRow& row) const;
        int TopLevelAncestorOrdinal(int taskId) const;
        std::string FormatDate(const GanttDate& date) const;
        std::string FormatDuration(long days) const;

        // ----- Rendering -----
        void RenderTable(IRenderContext* ctx);
        void RenderTableHeader(IRenderContext* ctx);
        void RenderTimeHeader(IRenderContext* ctx);
        void RenderTimeGrid(IRenderContext* ctx);
        void RenderRowBands(IRenderContext* ctx);
        void RenderBars(IRenderContext* ctx);
        void RenderDependencies(IRenderContext* ctx);
        void RenderTodayLine(IRenderContext* ctx);
        void DrawTaskBar(IRenderContext* ctx, const Rect2Dd& rect,
                         const Color& color, const GanttRow& row);
        void DrawSummaryBar(IRenderContext* ctx, const Rect2Dd& rect,
                            const Color& color, const GanttRow& row);
        void DrawMilestone(IRenderContext* ctx, const Point2Dd& center,
                           float size, const Color& color);
        void DrawDependencyArrow(IRenderContext* ctx, const Point2Dd& tip,
                                 bool pointsRight, bool pointsDown,
                                 bool horizontal, const Color& color);
        Rect2Dd BarRectForRow(size_t rowIndex) const;  // Timeline-local coords

        // ----- Interaction -----
        void ScrollLimits(float& maxX, float& maxY) const;
        void ClampScroll();
        void BindScrollAnimators();
        void CancelScrollAnimations();
        bool HandleTimelineWheel(const UCEvent& event);
        bool HandleClick(const UCEvent& event, bool doubleClick);
        std::string BuildTaskTooltip(const GanttRow& row) const;
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    inline std::shared_ptr<UltraCanvasGanttChartElement> CreateGanttChartElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasGanttChartElement>(id, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasGanttChartElement> CreateGanttChartWithData(
            const std::string& id, int x, int y, int width, int height,
            std::shared_ptr<GanttDataSource> data,
            GanttDesign design = GanttDesign::Modern,
            const std::string& title = "") {
        auto chart = CreateGanttChartElement(id, x, y, width, height);
        chart->ApplyDesign(design);
        chart->SetGanttDataSource(data);
        if (!title.empty()) {
            chart->SetChartTitle(title);
        }
        return chart;
    }

} // namespace UltraCanvas
