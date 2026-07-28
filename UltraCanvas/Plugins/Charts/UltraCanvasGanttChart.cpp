// Plugins/Charts/UltraCanvasGanttChart.cpp
// Gantt chart element: project schedule visualization with a task table,
// timeline grid, hierarchy, dependencies, milestones, progress tracking,
// critical path analysis, and a preset-based design/palette system.
// Version: 1.0.0
// Last Modified: 2026-07-26
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasGanttChart.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>

namespace UltraCanvas {

namespace {
    const char* kMonthShort[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const char* kMonthFull[] = {"January", "February", "March", "April",
                                "May", "June", "July", "August", "September",
                                "October", "November", "December"};
    // Sunday = 0 ... Saturday = 6
    const char* kWeekdayLetter[] = {"S", "M", "T", "W", "T", "F", "S"};

    std::string ToUpper(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }

    // Draw a single line of text clipped to `rect`, horizontally aligned and
    // vertically centered. Uses the context's current font and text paint.
    void DrawCellText(IRenderContext* ctx, const std::string& text,
                      const Rect2Dd& rect, TextAlignment align,
                      float padding = 4.0f) {
        if (text.empty() || rect.width <= 2 * padding) return;
        Size2Di sz = ctx->GetTextLineDimensions(text);
        double x = rect.x + padding;
        if (align == TextAlignment::Center) {
            x = rect.x + (rect.width - sz.width) / 2.0;
            x = std::max(x, rect.x + padding);
        } else if (align == TextAlignment::Right) {
            x = rect.x + rect.width - padding - sz.width;
            x = std::max(x, rect.x + padding);
        }
        double y = rect.y + (rect.height - sz.height) / 2.0;
        ctx->PushState();
        ctx->ClipRect(rect);
        ctx->DrawText(text, Point2Dd(x, y));
        ctx->PopState();
    }
}

// =============================================================================
// DATA SOURCE
// =============================================================================

int GanttDataSource::AddTask(const std::string& name, const GanttDate& start,
                             const GanttDate& end, int parentId) {
    GanttTask task;
    task.name = name;
    task.start = start;
    task.end = end;
    task.parentId = parentId;
    return AddTask(task);
}

int GanttDataSource::AddTask(const GanttTask& task) {
    if (task.end < task.start) return -1;
    if (task.parentId >= 0 && !FindTask(task.parentId)) return -1;
    GanttTask copy = task;
    copy.id = nextId++;
    copy.progress = std::clamp(copy.progress, 0.0f, 1.0f);
    tasks.push_back(copy);
    Touch();
    return copy.id;
}

int GanttDataSource::AddMilestone(const std::string& name, const GanttDate& date,
                                  int parentId) {
    GanttTask task;
    task.name = name;
    task.start = task.end = date;
    task.parentId = parentId;
    task.shape = GanttTaskShape::Milestone;
    return AddTask(task);
}

void GanttDataSource::CollectSubtree(int taskId, std::vector<int>& out) const {
    out.push_back(taskId);
    for (const auto& t : tasks) {
        if (t.parentId == taskId) CollectSubtree(t.id, out);
    }
}

bool GanttDataSource::RemoveTask(int taskId) {
    if (!FindTask(taskId)) return false;
    std::vector<int> doomed;
    CollectSubtree(taskId, doomed);
    std::set<int> doomedSet(doomed.begin(), doomed.end());
    tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                               [&](const GanttTask& t) { return doomedSet.count(t.id) > 0; }),
                tasks.end());
    dependencies.erase(std::remove_if(dependencies.begin(), dependencies.end(),
                                      [&](const GanttDependency& d) {
                                          return doomedSet.count(d.predecessorId) > 0 ||
                                                 doomedSet.count(d.successorId) > 0;
                                      }),
                       dependencies.end());
    Touch();
    return true;
}

void GanttDataSource::Clear() {
    tasks.clear();
    dependencies.clear();
    nextId = 1;
    Touch();
}

GanttTask* GanttDataSource::FindTask(int taskId) {
    for (auto& t : tasks) {
        if (t.id == taskId) return &t;
    }
    return nullptr;
}

const GanttTask* GanttDataSource::FindTask(int taskId) const {
    for (const auto& t : tasks) {
        if (t.id == taskId) return &t;
    }
    return nullptr;
}

bool GanttDataSource::SetTaskDates(int taskId, const GanttDate& start, const GanttDate& end) {
    GanttTask* t = FindTask(taskId);
    if (!t || end < start) return false;
    t->start = start;
    t->end = end;
    Touch();
    return true;
}

bool GanttDataSource::SetTaskProgress(int taskId, float progress) {
    GanttTask* t = FindTask(taskId);
    if (!t) return false;
    t->progress = std::clamp(progress, 0.0f, 1.0f);
    Touch();
    return true;
}

bool GanttDataSource::SetTaskColor(int taskId, const Color& color) {
    GanttTask* t = FindTask(taskId);
    if (!t) return false;
    t->customColor = color;
    Touch();
    return true;
}

bool GanttDataSource::SetTaskAssignee(int taskId, const std::string& assignee) {
    GanttTask* t = FindTask(taskId);
    if (!t) return false;
    t->assignee = assignee;
    Touch();
    return true;
}

bool GanttDataSource::SetTaskPriority(int taskId, GanttPriority priority) {
    GanttTask* t = FindTask(taskId);
    if (!t) return false;
    t->priority = priority;
    Touch();
    return true;
}

bool GanttDataSource::SetTaskNotes(int taskId, const std::string& notes) {
    GanttTask* t = FindTask(taskId);
    if (!t) return false;
    t->notes = notes;
    Touch();
    return true;
}

bool GanttDataSource::SetTaskExpanded(int taskId, bool expanded) {
    GanttTask* t = FindTask(taskId);
    if (!t) return false;
    t->expanded = expanded;
    Touch();
    return true;
}

void GanttDataSource::SetAllExpanded(bool expanded) {
    for (auto& t : tasks) t.expanded = expanded;
    Touch();
}

bool GanttDataSource::AddDependency(int predecessorId, int successorId,
                                    GanttDependencyType type, int lagDays) {
    if (predecessorId == successorId) return false;
    if (!FindTask(predecessorId) || !FindTask(successorId)) return false;
    for (const auto& d : dependencies) {
        if (d.predecessorId == predecessorId && d.successorId == successorId) return false;
    }
    dependencies.push_back({predecessorId, successorId, type, lagDays});
    Touch();
    return true;
}

bool GanttDataSource::RemoveDependency(int predecessorId, int successorId) {
    auto it = std::remove_if(dependencies.begin(), dependencies.end(),
                             [&](const GanttDependency& d) {
                                 return d.predecessorId == predecessorId &&
                                        d.successorId == successorId;
                             });
    if (it == dependencies.end()) return false;
    dependencies.erase(it, dependencies.end());
    Touch();
    return true;
}

std::vector<int> GanttDataSource::GetPredecessors(int taskId) const {
    std::vector<int> result;
    for (const auto& d : dependencies) {
        if (d.successorId == taskId) result.push_back(d.predecessorId);
    }
    return result;
}

bool GanttDataSource::TaskHasChildren(int taskId) const {
    for (const auto& t : tasks) {
        if (t.parentId == taskId) return true;
    }
    return false;
}

bool GanttDataSource::IsSummary(const GanttTask& task) const {
    if (task.shape == GanttTaskShape::Summary) return true;
    if (task.shape == GanttTaskShape::Auto) return TaskHasChildren(task.id);
    return false;
}

bool GanttDataSource::IsMilestone(const GanttTask& task) const {
    return task.shape == GanttTaskShape::Milestone;
}

void GanttDataSource::GetEffectiveTaskSpan(const GanttTask& task, GanttDate& start,
                                           GanttDate& end, float& progress) const {
    start = task.start;
    end = task.end;
    progress = task.progress;
    if (!IsSummary(task)) return;

    bool any = false;
    double weightedProgress = 0.0;
    double totalWeight = 0.0;
    for (const auto& child : tasks) {
        if (child.parentId != task.id) continue;
        GanttDate cs, ce;
        float cp;
        GetEffectiveTaskSpan(child, cs, ce, cp);
        if (!any) {
            start = cs;
            end = ce;
            any = true;
        } else {
            if (cs < start) start = cs;
            if (end < ce) end = ce;
        }
        double weight = static_cast<double>(cs.DaysUntil(ce) + 1);
        weightedProgress += cp * weight;
        totalWeight += weight;
    }
    if (any && totalWeight > 0.0) {
        progress = static_cast<float>(weightedProgress / totalWeight);
    }
}

std::vector<GanttRow> GanttDataSource::BuildVisibleRows(bool markCritical) const {
    std::vector<GanttRow> rows;
    std::set<int> criticalSet;
    if (markCritical) {
        const auto& ids = ComputeCriticalPath();
        criticalSet.insert(ids.begin(), ids.end());
    }

    // parent id -> ordered child indices
    std::map<int, std::vector<size_t>> children;
    std::set<int> knownIds;
    for (const auto& t : tasks) knownIds.insert(t.id);
    for (size_t i = 0; i < tasks.size(); ++i) {
        int parent = tasks[i].parentId;
        if (parent >= 0 && knownIds.count(parent) == 0) parent = -1; // orphan → root
        children[parent].push_back(i);
    }

    std::function<void(int, int, const std::string&)> emit =
            [&](int parentId, int depth, const std::string& wbsPrefix) {
        auto it = children.find(parentId);
        if (it == children.end()) return;
        int ordinal = 1;
        for (size_t idx : it->second) {
            const GanttTask& task = tasks[idx];
            GanttRow row;
            row.taskId = task.id;
            row.depth = depth;
            row.hasChildren = children.count(task.id) > 0;
            row.isSummary = IsSummary(task);
            row.isMilestone = IsMilestone(task);
            row.expanded = task.expanded;
            row.critical = criticalSet.count(task.id) > 0;
            row.wbs = wbsPrefix.empty() ? std::to_string(ordinal)
                                        : wbsPrefix + "." + std::to_string(ordinal);
            GetEffectiveTaskSpan(task, row.start, row.end, row.progress);
            rows.push_back(row);
            if (row.hasChildren && task.expanded) {
                emit(task.id, depth + 1, row.wbs);
            }
            ++ordinal;
        }
    };
    emit(-1, 0, "");
    return rows;
}

bool GanttDataSource::GetDateRange(GanttDate& min, GanttDate& max) const {
    bool any = false;
    for (const auto& t : tasks) {
        if (!any) {
            min = t.start;
            max = t.end;
            any = true;
        } else {
            if (t.start < min) min = t.start;
            if (max < t.end) max = t.end;
        }
    }
    return any;
}

const std::vector<int>& GanttDataSource::ComputeCriticalPath() const {
    if (criticalVersion == version) return criticalIds;
    criticalIds.clear();
    criticalVersion = version;
    if (tasks.empty() || dependencies.empty()) return criticalIds;

    // Work on leaf tasks only (summaries derive from children).
    std::map<int, long> dur, es, ls;
    for (const auto& t : tasks) {
        if (IsSummary(t)) continue;
        dur[t.id] = t.DurationDays();
        es[t.id] = 0;
    }

    // Constraint on earliest start of the successor, in days relative to the
    // predecessor's earliest start.
    auto forwardOffset = [&](const GanttDependency& d) -> long {
        switch (d.type) {
            case GanttDependencyType::FinishToStart:
                return dur[d.predecessorId] + d.lagDays;
            case GanttDependencyType::StartToStart:
                return d.lagDays;
            case GanttDependencyType::FinishToFinish:
                return dur[d.predecessorId] - dur[d.successorId] + d.lagDays;
            case GanttDependencyType::StartToFinish:
                return d.lagDays - dur[d.successorId];
        }
        return 0;
    };

    // Forward pass by relaxation (dependency graphs are small; the iteration
    // cap guards against cycles).
    size_t maxIter = tasks.size() + 2;
    for (size_t iter = 0; iter < maxIter; ++iter) {
        bool changed = false;
        for (const auto& d : dependencies) {
            if (!dur.count(d.predecessorId) || !dur.count(d.successorId)) continue;
            long candidate = es[d.predecessorId] + forwardOffset(d);
            if (candidate > es[d.successorId]) {
                es[d.successorId] = candidate;
                changed = true;
            }
        }
        if (!changed) break;
    }

    long projectEnd = 0;
    for (const auto& kv : es) {
        projectEnd = std::max(projectEnd, kv.second + dur[kv.first]);
    }

    // Backward pass.
    for (const auto& kv : es) ls[kv.first] = projectEnd - dur[kv.first];
    for (size_t iter = 0; iter < maxIter; ++iter) {
        bool changed = false;
        for (const auto& d : dependencies) {
            if (!dur.count(d.predecessorId) || !dur.count(d.successorId)) continue;
            long candidate = ls[d.successorId] - forwardOffset(d);
            if (candidate < ls[d.predecessorId]) {
                ls[d.predecessorId] = candidate;
                changed = true;
            }
        }
        if (!changed) break;
    }

    // Zero-slack tasks that take part in at least one dependency.
    std::set<int> linked;
    for (const auto& d : dependencies) {
        linked.insert(d.predecessorId);
        linked.insert(d.successorId);
    }
    for (const auto& kv : es) {
        if (linked.count(kv.first) && ls[kv.first] == kv.second) {
            criticalIds.push_back(kv.first);
        }
    }
    return criticalIds;
}

ChartDataPoint GanttDataSource::GetPoint(size_t index) {
    if (index >= tasks.size()) return ChartDataPoint(0, 0);
    const GanttTask& t = tasks[index];
    return ChartDataPoint(static_cast<double>(index),
                          static_cast<double>(t.DurationDays()), 0.0,
                          t.name, t.progress);
}

// =============================================================================
// PALETTES
// =============================================================================

namespace GanttPalettes {

    const std::vector<Color>& Get(GanttPalette palette) {
        static const std::vector<Color> corporateBlue = {
                Color(48, 84, 150), Color(68, 114, 196), Color(91, 155, 213),
                Color(31, 56, 100), Color(142, 170, 219)};
        static const std::vector<Color> vibrant = {
                Color(121, 112, 214), Color(66, 165, 245), Color(233, 30, 140),
                Color(229, 77, 66), Color(244, 156, 66), Color(38, 166, 154)};
        static const std::vector<Color> pastel = {
                Color(244, 143, 177), Color(129, 199, 132), Color(255, 213, 79),
                Color(77, 208, 225), Color(179, 157, 219), Color(255, 171, 145)};
        static const std::vector<Color> ocean = {
                Color(0, 105, 148), Color(0, 150, 199), Color(0, 180, 216),
                Color(72, 202, 228), Color(2, 62, 100)};
        static const std::vector<Color> sunset = {
                Color(238, 108, 77), Color(243, 146, 55), Color(250, 192, 94),
                Color(214, 64, 69), Color(154, 38, 89)};
        static const std::vector<Color> forest = {
                Color(45, 106, 79), Color(64, 145, 108), Color(82, 183, 136),
                Color(27, 67, 50), Color(116, 198, 157)};
        static const std::vector<Color> slate = {
                Color(84, 110, 138), Color(120, 144, 168), Color(66, 84, 102),
                Color(151, 169, 188), Color(52, 64, 80)};
        static const std::vector<Color> mono = {
                Color(70, 70, 70), Color(110, 110, 110), Color(150, 150, 150),
                Color(45, 45, 45), Color(180, 180, 180)};

        switch (palette) {
            case GanttPalette::CorporateBlue: return corporateBlue;
            case GanttPalette::Vibrant:       return vibrant;
            case GanttPalette::Pastel:        return pastel;
            case GanttPalette::Ocean:         return ocean;
            case GanttPalette::Sunset:        return sunset;
            case GanttPalette::Forest:        return forest;
            case GanttPalette::Slate:         return slate;
            case GanttPalette::Mono:          return mono;
        }
        return corporateBlue;
    }

    const std::vector<Color>& PriorityColors() {
        // NoPriority, Low, Normal, High, Urgent
        static const std::vector<Color> colors = {
                Color(158, 158, 158), Color(96, 125, 139), Color(63, 81, 181),
                Color(230, 145, 20), Color(183, 28, 28)};
        return colors;
    }

} // namespace GanttPalettes

// =============================================================================
// DESIGN PRESETS
// =============================================================================

std::vector<GanttColumn> GanttChartStyle::DefaultColumns() {
    return {
            GanttColumn(GanttColumnType::Wbs, "ID", 40),
            GanttColumn(GanttColumnType::Name, "Task", 200),
            GanttColumn(GanttColumnType::StartDate, "Start", 74),
            GanttColumn(GanttColumnType::EndDate, "End", 74),
            GanttColumn(GanttColumnType::Duration, "Days", 48),
    };
}

std::vector<GanttColumn> GanttChartStyle::ProjectColumns() {
    return {
            GanttColumn(GanttColumnType::Name, "Task", 190),
            GanttColumn(GanttColumnType::Assignee, "Assignee", 100),
            GanttColumn(GanttColumnType::Priority, "Priority", 72),
            GanttColumn(GanttColumnType::StartDate, "Start", 74),
            GanttColumn(GanttColumnType::EndDate, "Finish", 74),
            GanttColumn(GanttColumnType::Duration, "Days", 46),
            GanttColumn(GanttColumnType::Progress, "%", 46),
    };
}

namespace GanttChartStyles {

    // Print style: flat single-color bars, month header, ID + Task table,
    // solid elbow dependency arrows. Modeled on classic business-plan charts.
    GanttChartStyle CreateClassic() {
        GanttChartStyle s;
        s.columns = {GanttColumn(GanttColumnType::Wbs, "ID", 40),
                     GanttColumn(GanttColumnType::Name, "Task", 240)};
        s.rowHeight = 30.0f;
        s.barHeightRatio = 0.52f;
        s.barStyle = GanttBarStyle::Flat;
        s.summaryStyle = GanttSummaryStyle::Bar;
        s.twoTierHeader = false;
        s.timeScale = GanttTimeScale::Months;
        s.dayWidth = 2.6f;
        s.showWeekdayLetters = false;
        s.colorMode = GanttColorMode::Single;
        s.palette = {Color(52, 78, 154)};
        s.background = Color(245, 246, 248);
        s.tableBackground = Color(245, 246, 248);
        s.headerBackground = Color(245, 246, 248);
        s.headerText = Color(35, 40, 55);
        s.rowLineColor = Color(180, 185, 195);
        s.columnLineColor = Color(205, 210, 218);
        s.shadeWeekends = false;
        s.progressStyle = GanttProgressStyle::Hidden;
        s.labelPlacement = GanttLabelPlacement::Hidden;
        s.dependencyStyle = GanttDependencyLineStyle::Elbow;
        s.dependencyColor = Color(52, 78, 154);
        s.tableFontSize = 12.0f;
        s.headerFontSize = 12.0f;
        return s;
    }

    // Rounded colorful bars, week + weekday-letter header, phase names inside
    // summary bars, arrows linking phases. Modeled on event-planning charts.
    GanttChartStyle CreateModern() {
        GanttChartStyle s;
        s.columns = GanttChartStyle::DefaultColumns();
        s.rowHeight = 32.0f;
        s.barHeightRatio = 0.62f;
        s.barStyle = GanttBarStyle::Pill;
        s.barCornerRadius = 8.0f;
        s.summaryStyle = GanttSummaryStyle::Bar;
        s.twoTierHeader = true;
        s.timeScale = GanttTimeScale::Days;
        s.dayWidth = 22.0f;
        s.uppercaseHeader = true;
        s.showWeekdayLetters = true;
        s.colorMode = GanttColorMode::ByPhase;
        s.palette = GanttPalettes::Get(GanttPalette::Vibrant);
        s.background = Colors::White;
        s.headerBackground = Color(238, 239, 241);
        s.tableHeaderBackground = Color(238, 239, 241);
        s.rowLineColor = Color(236, 237, 239);
        s.columnLineColor = Color(238, 239, 241);
        s.shadeWeekends = true;
        s.weekendShade = Color(0, 0, 0, 10);
        s.progressStyle = GanttProgressStyle::Hidden;
        s.labelPlacement = GanttLabelPlacement::InsideBar;
        s.labelSummariesOnly = true;
        s.dependencyStyle = GanttDependencyLineStyle::Elbow;
        s.dependencyUsesBarColor = true;
        return s;
    }

    // Project-management-tool look: bracket summary bars, in-bar progress,
    // task names to the right of bars, diamond milestones, week header.
    GanttChartStyle CreateProfessional() {
        GanttChartStyle s;
        s.columns = GanttChartStyle::ProjectColumns();
        s.rowHeight = 34.0f;
        s.barHeightRatio = 0.5f;
        s.barStyle = GanttBarStyle::Rounded;
        s.barCornerRadius = 3.0f;
        s.summaryStyle = GanttSummaryStyle::Bracket;
        s.twoTierHeader = true;
        s.timeScale = GanttTimeScale::Weeks;
        s.dayWidth = 8.0f;
        s.showWeekdayLetters = false;
        s.colorMode = GanttColorMode::ByPhase;
        s.palette = {Color(41, 182, 246), Color(102, 187, 106),
                     Color(158, 158, 158), Color(255, 167, 38),
                     Color(171, 71, 188)};
        s.background = Colors::White;
        s.headerBackground = Color(250, 250, 250);
        s.tableHeaderBackground = Color(250, 250, 250);
        s.rowLineColor = Color(238, 238, 238);
        s.columnLineColor = Color(242, 242, 242);
        s.rowAlternateColor = Color(0, 0, 0, 6);
        s.shadeWeekends = false;
        s.progressStyle = GanttProgressStyle::DarkerFill;
        s.showProgressText = true;
        s.labelPlacement = GanttLabelPlacement::RightOfBar;
        s.showAssigneeWithLabel = true;
        s.dependencyStyle = GanttDependencyLineStyle::Elbow;
        s.dependencyColor = Color(130, 130, 130);
        s.highlightCriticalPath = false;
        s.summaryBarColor = Color(97, 97, 97);
        return s;
    }

    // Pastel report style: thin line bars with square end caps, dashed
    // dependency arrows, weeks grouped under month bands.
    GanttChartStyle CreateSoft() {
        GanttChartStyle s;
        s.columns = {GanttColumn(GanttColumnType::Name, "Activity", 220),
                     GanttColumn(GanttColumnType::Duration, "Days", 56)};
        s.rowHeight = 36.0f;
        s.barHeightRatio = 0.5f;
        s.barStyle = GanttBarStyle::Line;
        s.summaryStyle = GanttSummaryStyle::Bar;
        s.twoTierHeader = true;
        s.timeScale = GanttTimeScale::Weeks;
        s.dayWidth = 12.0f;
        s.showWeekdayLetters = false;
        s.colorMode = GanttColorMode::ByPhase;
        s.palette = GanttPalettes::Get(GanttPalette::Pastel);
        s.background = Colors::White;
        s.headerBackground = Color(120, 120, 124);
        s.headerText = Colors::White;
        s.headerBorder = Color(150, 150, 154);
        s.tableHeaderBackground = Color(52, 48, 46);
        s.tableHeaderText = Colors::White;
        s.rowAlternateColor = Color(0, 0, 0, 12);
        s.rowLineColor = Color(222, 222, 222);
        s.columnLineColor = Color(228, 228, 228);
        s.shadeWeekends = false;
        s.progressStyle = GanttProgressStyle::Hidden;
        s.labelPlacement = GanttLabelPlacement::Hidden;
        s.dependencyStyle = GanttDependencyLineStyle::Elbow;
        s.dependencyDashed = true;
        s.dependencyColor = Color(235, 110, 60);
        s.milestoneStyle = GanttMilestoneStyle::Diamond;
        s.milestoneColor = Color(150, 150, 150);
        return s;
    }

    // Minimal-modern template: wide info table (assignee, priority chips,
    // % complete), light-track progress bars, day grid with weekday letters.
    GanttChartStyle CreateMinimal() {
        GanttChartStyle s;
        s.columns = GanttChartStyle::ProjectColumns();
        s.rowHeight = 30.0f;
        s.barHeightRatio = 0.68f;
        s.barStyle = GanttBarStyle::Flat;
        s.summaryStyle = GanttSummaryStyle::Bar;
        s.twoTierHeader = true;
        s.timeScale = GanttTimeScale::Days;
        s.dayWidth = 18.0f;
        s.showWeekdayLetters = true;
        s.colorMode = GanttColorMode::ByPhase;
        s.palette = {Color(214, 84, 112), Color(140, 129, 226),
                     Color(124, 179, 66), Color(246, 191, 38),
                     Color(63, 106, 216)};
        s.background = Colors::White;
        s.headerBackground = Colors::White;
        s.headerText = Color(70, 70, 76);
        s.headerBorder = Color(228, 228, 232);
        s.tableBackground = Colors::White;
        s.tableHeaderBackground = Color(248, 246, 243);
        s.rowLineColor = Color(238, 238, 240);
        s.columnLineColor = Color(242, 242, 244);
        s.shadeWeekends = false;
        s.progressStyle = GanttProgressStyle::LightTrack;
        s.trackLighten = 0.72f;
        s.labelPlacement = GanttLabelPlacement::Hidden;
        s.dependencyStyle = GanttDependencyLineStyle::Hidden;
        s.tableFontSize = 11.0f;
        s.headerFontSize = 11.0f;
        return s;
    }

    // Dark variant of Modern.
    GanttChartStyle CreateDark() {
        GanttChartStyle s = CreateModern();
        s.background = Color(30, 33, 40);
        s.tableBackground = Color(30, 33, 40);
        s.tableHeaderBackground = Color(42, 46, 55);
        s.tableHeaderText = Color(210, 214, 220);
        s.tableText = Color(210, 214, 220);
        s.tableSummaryText = Color(240, 242, 246);
        s.headerBackground = Color(42, 46, 55);
        s.headerText = Color(210, 214, 220);
        s.headerBorder = Color(58, 63, 74);
        s.rowLineColor = Color(46, 50, 60);
        s.columnLineColor = Color(42, 46, 55);
        s.weekendShade = Color(255, 255, 255, 10);
        s.outsideLabelColor = Color(200, 204, 212);
        s.rowAlternateColor = Color(255, 255, 255, 6);
        s.selectionColor = Color(88, 166, 255, 40);
        s.hoverColor = Color(88, 166, 255, 20);
        s.todayLineColor = Color(255, 99, 90);
        return s;
    }

    GanttChartStyle Create(GanttDesign design) {
        switch (design) {
            case GanttDesign::Classic:      return CreateClassic();
            case GanttDesign::Modern:       return CreateModern();
            case GanttDesign::Professional: return CreateProfessional();
            case GanttDesign::Soft:         return CreateSoft();
            case GanttDesign::Minimal:      return CreateMinimal();
            case GanttDesign::Dark:         return CreateDark();
        }
        return CreateModern();
    }

} // namespace GanttChartStyles

// =============================================================================
// ELEMENT: SETUP & STYLE
// =============================================================================

UltraCanvasGanttChartElement::UltraCanvasGanttChartElement(
        const std::string& id, int x, int y, int width, int height)
        : UltraCanvasChartElementBase(id, x, y, width, height) {
    enableTooltips = true;
    enableZoom = false;   // The element handles wheel zoom itself
    enablePan = false;
    animationEnabled = false;
    style = GanttChartStyles::CreateModern();
    if (style.columns.empty()) style.columns = GanttChartStyle::DefaultColumns();
}

void UltraCanvasGanttChartElement::SetGanttDataSource(std::shared_ptr<GanttDataSource> ds) {
    dataSource = ds;
    rowsDirty = true;
    scrollX = scrollY = 0;
    selectedTaskId = -1;
    hoveredRowIndex = -1;
    RequestRedraw();
}

void UltraCanvasGanttChartElement::SetStyle(const GanttChartStyle& newStyle) {
    style = newStyle;
    if (style.columns.empty()) style.columns = GanttChartStyle::DefaultColumns();
    StyleChanged();
}

void UltraCanvasGanttChartElement::StyleChanged() {
    rowsDirty = true;  // Critical-path marking depends on the style
    RequestRedraw();
}

void UltraCanvasGanttChartElement::ApplyDesign(GanttDesign design) {
    // Keep caller-supplied schedule context across the preset swap.
    GanttDate keepToday = style.today;
    bool keepShowToday = style.showTodayLine;
    style = GanttChartStyles::Create(design);
    style.today = keepToday;
    style.showTodayLine = keepShowToday;
    StyleChanged();
}

void UltraCanvasGanttChartElement::SetPalette(GanttPalette palette) {
    style.palette = GanttPalettes::Get(palette);
    RequestRedraw();
}

void UltraCanvasGanttChartElement::SetCustomPalette(const std::vector<Color>& colors) {
    if (!colors.empty()) style.palette = colors;
    RequestRedraw();
}

void UltraCanvasGanttChartElement::SetTimeScale(GanttTimeScale scale) {
    style.timeScale = scale;
    RequestRedraw();
}

void UltraCanvasGanttChartElement::SetDayWidth(float pixels) {
    style.dayWidth = std::clamp(pixels, 0.5f, 200.0f);
    RequestRedraw();
}

void UltraCanvasGanttChartElement::SetShowTable(bool show) {
    style.showTable = show;
    RequestRedraw();
}

void UltraCanvasGanttChartElement::SetColumns(const std::vector<GanttColumn>& columns) {
    style.columns = columns.empty() ? GanttChartStyle::DefaultColumns() : columns;
    RequestRedraw();
}

void UltraCanvasGanttChartElement::SetShowDependencies(bool show) {
    style.dependencyStyle = show ? GanttDependencyLineStyle::Elbow
                                 : GanttDependencyLineStyle::Hidden;
    RequestRedraw();
}

void UltraCanvasGanttChartElement::SetShowCriticalPath(bool show) {
    style.highlightCriticalPath = show;
    StyleChanged();
}

void UltraCanvasGanttChartElement::SetToday(const GanttDate& date, bool showLine) {
    style.today = date;
    style.showTodayLine = showLine;
    RequestRedraw();
}

void UltraCanvasGanttChartElement::ScrollToDate(const GanttDate& date) {
    ComputeLayout();
    scrollX = static_cast<float>((date.serial - layout.viewStart.serial) * style.dayWidth);
    ClampScroll();
    RequestRedraw();
}

void UltraCanvasGanttChartElement::ScrollToTask(int taskId) {
    EnsureRows();
    int index = RowIndexOfTask(taskId);
    if (index < 0) return;
    ComputeLayout();
    scrollY = index * style.rowHeight - layout.timeBody.height / 2;
    scrollX = static_cast<float>((rows[index].start.serial - layout.viewStart.serial) *
                                 style.dayWidth) - 40.0f;
    ClampScroll();
    RequestRedraw();
}

void UltraCanvasGanttChartElement::FitToRange() {
    auto ds = GetGanttDataSource();
    if (!ds) return;
    GanttDate min, max;
    if (!ds->GetDateRange(min, max)) return;
    long span = min.DaysUntil(max) + 1;
    if (span < 1) return;
    // ComputeLayout() pads the grid by one scale unit before the first task and
    // two after it, so the width to divide is the padded span, not the raw one
    // — dividing by the raw span pushes the tail of the project off-screen.
    // Two passes because with GanttTimeScale::Auto the resolved scale (and with
    // it the padding) depends on the day width chosen here.
    for (int pass = 0; pass < 2; ++pass) {
        ComputeLayout();
        if (layout.timeBody.width <= 0) return;
        long pad = std::max<long>(2, static_cast<long>(layout.unitDays));
        style.dayWidth = std::clamp(
                static_cast<float>(layout.timeBody.width) /
                static_cast<float>(span + pad * 3),
                0.5f, 200.0f);
    }
    scrollX = 0;
    scrollY = 0;
    RequestRedraw();
}

void UltraCanvasGanttChartElement::SelectTask(int taskId) {
    if (selectedTaskId == taskId) return;
    selectedTaskId = taskId;
    if (onSelectionChanged) onSelectionChanged(taskId);
    RequestRedraw();
}

// =============================================================================
// ELEMENT: LAYOUT
// =============================================================================

void UltraCanvasGanttChartElement::EnsureRows() {
    auto ds = GetGanttDataSource();
    if (!ds) {
        rows.clear();
        return;
    }
    if (rowsDirty || rowsVersion != ds->GetVersion()) {
        rows = ds->BuildVisibleRows(style.highlightCriticalPath);
        rowsVersion = ds->GetVersion();
        rowsDirty = false;
    }
}

GanttTimeScale UltraCanvasGanttChartElement::ResolveScale() const {
    if (style.timeScale != GanttTimeScale::Auto) return style.timeScale;
    if (style.dayWidth >= 13.0f) return GanttTimeScale::Days;
    if (style.dayWidth >= 3.5f) return GanttTimeScale::Weeks;
    if (style.dayWidth >= 1.1f) return GanttTimeScale::Months;
    return GanttTimeScale::Quarters;
}

void UltraCanvasGanttChartElement::ComputeLayout() {
    Rect2Df local = GetLocalBounds();
    float headerH = style.HeaderHeight();
    float tableW = 0;
    if (style.showTable) {
        tableW = std::min(style.TotalTableWidth(),
                          local.width * 0.65f);
    }

    layout.tableHeader = Rect2Dd(0, 0, tableW, headerH);
    layout.tableBody = Rect2Dd(0, headerH, tableW,
                               std::max(0.0f, local.height - headerH));
    double tx = tableW + style.tableTimelineGap;
    layout.timeHeader = Rect2Dd(tx, 0, std::max(0.0, local.width - tx), headerH);
    layout.timeBody = Rect2Dd(tx, headerH, std::max(0.0, local.width - tx),
                              std::max(0.0f, local.height - headerH));

    layout.effectiveScale = ResolveScale();
    switch (layout.effectiveScale) {
        case GanttTimeScale::Days:     layout.unitDays = 1.0f; break;
        case GanttTimeScale::Weeks:    layout.unitDays = 7.0f; break;
        case GanttTimeScale::Months:   layout.unitDays = 30.44f; break;
        case GanttTimeScale::Quarters: layout.unitDays = 91.31f; break;
        default:                       layout.unitDays = 1.0f; break;
    }

    GanttDate min, max;
    auto ds = GetGanttDataSource();
    if (ds && ds->GetDateRange(min, max)) {
        long pad = std::max<long>(2, static_cast<long>(layout.unitDays));
        layout.viewStart = min.AddDays(-pad);
        layout.totalDays = min.DaysUntil(max) + 1 + pad * 3;
        // Make sure the grid always covers the viewport.
        long viewportDays = static_cast<long>(layout.timeBody.width / style.dayWidth) + 1;
        layout.totalDays = std::max(layout.totalDays, viewportDays);
    } else {
        layout.viewStart = GanttDate(0);
        layout.totalDays = 0;
    }
    layout.contentWidth = static_cast<float>(layout.totalDays) * style.dayWidth;
    layout.contentHeight = static_cast<float>(rows.size()) * style.rowHeight;
}

double UltraCanvasGanttChartElement::DateToX(const GanttDate& date) const {
    return layout.timeBody.x +
           static_cast<double>(date.serial - layout.viewStart.serial) * style.dayWidth -
           scrollX;
}

float UltraCanvasGanttChartElement::RowTop(size_t rowIndex) const {
    return static_cast<float>(layout.timeBody.y) +
           static_cast<float>(rowIndex) * style.rowHeight - scrollY;
}

const GanttRow* UltraCanvasGanttChartElement::RowAt(float localY, int* indexOut) const {
    if (localY < layout.timeBody.y) return nullptr;
    int index = static_cast<int>((localY - layout.timeBody.y + scrollY) / style.rowHeight);
    if (index < 0 || index >= static_cast<int>(rows.size())) return nullptr;
    if (indexOut) *indexOut = index;
    return &rows[index];
}

int UltraCanvasGanttChartElement::RowIndexOfTask(int taskId) const {
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].taskId == taskId) return static_cast<int>(i);
    }
    return -1;
}

int UltraCanvasGanttChartElement::TopLevelAncestorOrdinal(int taskId) const {
    auto ds = GetGanttDataSource();
    if (!ds) return 0;
    const GanttTask* t = ds->FindTask(taskId);
    while (t && t->parentId >= 0) {
        const GanttTask* parent = ds->FindTask(t->parentId);
        if (!parent) break;
        t = parent;
    }
    if (!t) return 0;
    int ordinal = 0;
    for (const auto& task : ds->GetTasks()) {
        if (task.parentId >= 0 && ds->FindTask(task.parentId)) continue;
        if (task.id == t->id) return ordinal;
        ++ordinal;
    }
    return 0;
}

Color UltraCanvasGanttChartElement::ResolveTaskColor(const GanttTask& task,
                                                     size_t rowIndex,
                                                     const GanttRow& row) const {
    if (task.customColor.a != 0) return task.customColor;
    if (style.highlightCriticalPath && row.critical) return style.criticalColor;

    const std::vector<Color>& palette =
            style.palette.empty() ? GanttPalettes::Get(GanttPalette::CorporateBlue)
                                  : style.palette;
    switch (style.colorMode) {
        case GanttColorMode::ByRow:
            return palette[rowIndex % palette.size()];
        case GanttColorMode::ByPriority: {
            const auto& colors = GanttPalettes::PriorityColors();
            return colors[static_cast<size_t>(task.priority) % colors.size()];
        }
        case GanttColorMode::Single:
            return palette[0];
        case GanttColorMode::ByPhase:
        default:
            return palette[static_cast<size_t>(TopLevelAncestorOrdinal(task.id)) %
                           palette.size()];
    }
}

Color UltraCanvasGanttChartElement::BarColorForRow(size_t rowIndex,
                                                   const GanttRow& row) const {
    auto ds = GetGanttDataSource();
    const GanttTask* task = ds ? ds->FindTask(row.taskId) : nullptr;
    if (!task) return Color(120, 120, 120);
    if (row.isSummary && style.summaryBarColor.a != 0 &&
        task->customColor.a == 0) {
        return style.summaryBarColor;
    }
    return ResolveTaskColor(*task, rowIndex, row);
}

std::string UltraCanvasGanttChartElement::FormatDate(const GanttDate& date) const {
    int y, m, d;
    date.ToYMD(y, m, d);
    char buf[24];
    switch (style.dateFormat) {
        case GanttDateFormat::MonthDayYearSlash:
            snprintf(buf, sizeof(buf), "%d/%d/%02d", m, d, y % 100);
            break;
        case GanttDateFormat::ISO:
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
            break;
        case GanttDateFormat::DayMonthShort:
            snprintf(buf, sizeof(buf), "%d %s", d, kMonthShort[m - 1]);
            break;
        case GanttDateFormat::DayMonthYearSlash:
        default:
            snprintf(buf, sizeof(buf), "%d/%d/%02d", d, m, y % 100);
            break;
    }
    return buf;
}

std::string UltraCanvasGanttChartElement::FormatDuration(long days) const {
    return std::to_string(days) + "d";
}

// =============================================================================
// ELEMENT: RENDERING
// =============================================================================

void UltraCanvasGanttChartElement::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx) return;
    EnsureRows();
    ComputeLayout();

    Rect2Df local = GetLocalBounds();
    ctx->PushState();
    if (!style.fontFamily.empty()) ctx->SetFontFamily(style.fontFamily);
    ctx->DrawFilledRectangle(Rect2Dd(local.x, local.y, local.width, local.height),
                             style.background);

    if (rows.empty()) {
        ctx->SetTextPaint(style.tableText.WithAlpha(160));
        ctx->SetFontSize(13.0f);
        DrawCellText(ctx, "No tasks",
                     Rect2Dd(local.x, local.y, local.width, local.height),
                     TextAlignment::Center);
        ctx->PopState();
        return;
    }

    RenderChart(ctx);
    ctx->PopState();
}

void UltraCanvasGanttChartElement::RenderChart(IRenderContext* ctx) {
    // Timeline body: bands, grid, bars, dependency lines, today marker.
    ctx->PushState();
    ctx->ClipRect(layout.timeBody);
    RenderRowBands(ctx);
    RenderTimeGrid(ctx);
    RenderDependencies(ctx);
    RenderBars(ctx);
    RenderTodayLine(ctx);
    ctx->PopState();

    RenderTimeHeader(ctx);

    if (style.showTable && layout.tableBody.width > 0) {
        RenderTable(ctx);
        RenderTableHeader(ctx);
        // Separator between table and timeline.
        ctx->SetStrokePaint(style.headerBorder);
        ctx->SetStrokeWidth(1.0f);
        double sepX = layout.tableBody.x + layout.tableBody.width;
        ctx->DrawLine(Point2Dd(sepX, 0),
                      Point2Dd(sepX, layout.tableBody.y + layout.tableBody.height));
    }
}

void UltraCanvasGanttChartElement::RenderRowBands(IRenderContext* ctx) {
    const Rect2Dd& body = layout.timeBody;
    for (size_t i = 0; i < rows.size(); ++i) {
        float top = RowTop(i);
        if (top + style.rowHeight < body.y || top > body.y + body.height) continue;
        Rect2Dd band(body.x, top, body.width, style.rowHeight);

        if (style.rowAlternateColor.a != 0 && (i % 2) == 1) {
            ctx->DrawFilledRectangle(band, style.rowAlternateColor);
        }
        if (style.summaryRowColor.a != 0 && rows[i].isSummary) {
            ctx->DrawFilledRectangle(band, style.summaryRowColor);
        }
        if (static_cast<int>(i) == hoveredRowIndex && style.hoverColor.a != 0) {
            ctx->DrawFilledRectangle(band, style.hoverColor);
        }
        if (rows[i].taskId == selectedTaskId && style.selectionColor.a != 0) {
            ctx->DrawFilledRectangle(band, style.selectionColor);
        }
    }

    // Weekend shading (only meaningful at day resolution).
    if (style.shadeWeekends && layout.effectiveScale == GanttTimeScale::Days) {
        long firstDay = layout.viewStart.serial +
                        static_cast<long>(scrollX / style.dayWidth) - 1;
        long lastDay = firstDay + static_cast<long>(body.width / style.dayWidth) + 2;
        for (long s = firstDay; s <= lastDay; ++s) {
            GanttDate d(s);
            if (!d.IsWeekend()) continue;
            double x = DateToX(d);
            ctx->DrawFilledRectangle(
                    Rect2Dd(x, body.y, style.dayWidth, body.height),
                    style.weekendShade);
        }
    }
}

// Iterate minor-tier boundaries (day/week/month/quarter starts) that overlap
// the visible timeline and invoke `fn(cellStart, cellEndExclusive)`.
static void ForEachPeriod(GanttTimeScale scale, const GanttDate& from, long spanDays,
                          const std::function<void(GanttDate, GanttDate)>& fn) {
    GanttDate end = from.AddDays(spanDays);
    if (scale == GanttTimeScale::Days) {
        for (long s = from.serial; s < end.serial; ++s) {
            fn(GanttDate(s), GanttDate(s + 1));
        }
    } else if (scale == GanttTimeScale::Weeks) {
        // Weeks start on Monday.
        long start = from.serial;
        int wd = CalendarWeekdaySunday0(start);           // Sun=0..Sat=6
        long mondayShift = (wd == 0) ? 6 : (wd - 1);
        start -= mondayShift;
        for (long s = start; s < end.serial; s += 7) {
            fn(GanttDate(s), GanttDate(s + 7));
        }
    } else if (scale == GanttTimeScale::Months) {
        int y, m, d;
        from.ToYMD(y, m, d);
        GanttDate cell(y, m, 1);
        while (cell < end) {
            int ny = (m == 12) ? y + 1 : y;
            int nm = (m == 12) ? 1 : m + 1;
            GanttDate next(ny, nm, 1);
            fn(cell, next);
            cell = next;
            y = ny;
            m = nm;
        }
    } else { // Quarters
        int y, m, d;
        from.ToYMD(y, m, d);
        m = ((m - 1) / 3) * 3 + 1;
        GanttDate cell(y, m, 1);
        while (cell < end) {
            int ny = (m >= 10) ? y + 1 : y;
            int nm = (m >= 10) ? 1 : m + 3;
            GanttDate next(ny, nm, 1);
            fn(cell, next);
            cell = next;
            y = ny;
            m = nm;
        }
    }
}

void UltraCanvasGanttChartElement::RenderTimeGrid(IRenderContext* ctx) {
    const Rect2Dd& body = layout.timeBody;

    if (style.showColumnLines) {
        ctx->SetStrokePaint(style.columnLineColor);
        ctx->SetStrokeWidth(1.0f);
        long firstVisible = static_cast<long>(scrollX / style.dayWidth) - 1;
        long visibleSpan = static_cast<long>(body.width / style.dayWidth) + 3;
        ForEachPeriod(layout.effectiveScale,
                      layout.viewStart.AddDays(std::max<long>(0, firstVisible)),
                      visibleSpan + static_cast<long>(layout.unitDays) * 2,
                      [&](GanttDate cell, GanttDate) {
                          double x = DateToX(cell);
                          if (x < body.x - 1 || x > body.x + body.width + 1) return;
                          ctx->DrawLine(Point2Dd(x, body.y),
                                        Point2Dd(x, body.y + body.height));
                      });
    }

    if (style.showRowLines) {
        ctx->SetStrokePaint(style.rowLineColor);
        ctx->SetStrokeWidth(1.0f);
        for (size_t i = 0; i <= rows.size(); ++i) {
            double y = RowTop(i);
            if (y < body.y - 1 || y > body.y + body.height + 1) continue;
            ctx->DrawLine(Point2Dd(body.x, y), Point2Dd(body.x + body.width, y));
        }
    }
}

void UltraCanvasGanttChartElement::RenderTimeHeader(IRenderContext* ctx) {
    const Rect2Dd& header = layout.timeHeader;
    if (header.width <= 0) return;
    ctx->PushState();
    ctx->ClipRect(header);
    ctx->DrawFilledRectangle(header, style.headerBackground);
    ctx->SetFontSize(style.headerFontSize);
    ctx->SetTextPaint(style.headerText);
    ctx->SetStrokePaint(style.headerBorder);
    ctx->SetStrokeWidth(1.0f);

    float tierH = style.headerTierHeight;
    double minorY = style.twoTierHeader ? header.y + tierH : header.y;
    double minorH = style.twoTierHeader ? tierH : header.height;

    long firstVisible = static_cast<long>(scrollX / style.dayWidth) -
                        static_cast<long>(layout.unitDays) * 2;
    long visibleSpan = static_cast<long>(header.width / style.dayWidth) +
                       static_cast<long>(layout.unitDays) * 4;
    GanttDate iterFrom = layout.viewStart.AddDays(std::max<long>(0, firstVisible));

    // ----- Minor tier -----
    // Monday on/before the project view start anchors the week numbering so
    // "WEEK 01" stays stable while scrolling.
    long viewStartMonday = layout.viewStart.serial;
    {
        int wd = CalendarWeekdaySunday0(viewStartMonday);
        viewStartMonday -= (wd == 0) ? 6 : (wd - 1);
    }
    ForEachPeriod(layout.effectiveScale, iterFrom, visibleSpan,
                  [&](GanttDate cell, GanttDate next) {
        double x1 = DateToX(cell);
        double x2 = DateToX(next);
        if (x2 < header.x || x1 > header.x + header.width) return;
        Rect2Dd cellRect(x1, minorY, x2 - x1, minorH);
        ctx->DrawLine(Point2Dd(x1, minorY), Point2Dd(x1, minorY + minorH));

        std::string caption;
        switch (layout.effectiveScale) {
            case GanttTimeScale::Days: {
                caption = std::to_string(cell.Day());
                if (style.showWeekdayLetters && style.dayWidth >= 13.0f) {
                    // Day number in the upper part, weekday letter below.
                    Rect2Dd upper(cellRect.x, cellRect.y, cellRect.width,
                                  cellRect.height * 0.55);
                    Rect2Dd lower(cellRect.x, cellRect.y + cellRect.height * 0.5,
                                  cellRect.width, cellRect.height * 0.5);
                    DrawCellText(ctx, caption, upper, TextAlignment::Center, 0.0f);
                    ctx->PushState();
                    ctx->SetFontSize(style.headerFontSize - 2.0f);
                    ctx->SetTextPaint(style.headerText.WithAlpha(170));
                    DrawCellText(ctx, kWeekdayLetter[cell.Weekday()], lower,
                                 TextAlignment::Center, 0.0f);
                    ctx->PopState();
                    return;
                }
                break;
            }
            case GanttTimeScale::Weeks: {
                if (style.uppercaseHeader) {
                    long weekIndex = (cell.serial - viewStartMonday) / 7;
                    char buf[16];
                    snprintf(buf, sizeof(buf), "WEEK %02ld", (weekIndex % 52) + 1);
                    caption = buf;
                } else {
                    caption = std::to_string(cell.Day());
                }
                break;
            }
            case GanttTimeScale::Months:
                caption = kMonthShort[cell.Month() - 1];
                if (style.uppercaseHeader) caption = ToUpper(caption);
                break;
            case GanttTimeScale::Quarters:
                caption = "Q" + std::to_string((cell.Month() - 1) / 3 + 1);
                break;
            default:
                break;
        }
        DrawCellText(ctx, caption, cellRect, TextAlignment::Center, 1.0f);
    });

    // ----- Major tier -----
    if (style.twoTierHeader) {
        GanttTimeScale majorScale =
                (layout.effectiveScale == GanttTimeScale::Days ||
                 layout.effectiveScale == GanttTimeScale::Weeks)
                        ? GanttTimeScale::Months
                        : GanttTimeScale::Quarters;
        bool majorIsYear = layout.effectiveScale == GanttTimeScale::Months ||
                           layout.effectiveScale == GanttTimeScale::Quarters;

        ctx->SetFontSize(style.headerFontSize);
        ForEachPeriod(majorScale, iterFrom, visibleSpan,
                      [&](GanttDate cell, GanttDate next) {
            double x1 = DateToX(cell);
            double x2 = DateToX(next);
            if (x2 < header.x || x1 > header.x + header.width) return;
            Rect2Dd cellRect(x1, header.y, x2 - x1, tierH);
            ctx->DrawLine(Point2Dd(x1, header.y), Point2Dd(x1, header.y + tierH));
            std::string caption;
            if (majorIsYear) {
                // Group months/quarters by year: caption on January only.
                if (cell.Month() == 1 || x1 <= header.x) {
                    caption = std::to_string(cell.Year());
                }
            } else {
                caption = kMonthFull[cell.Month() - 1];
                if ((x2 - x1) < 72) caption = kMonthShort[cell.Month() - 1];
                if (style.uppercaseHeader) caption = ToUpper(caption);
            }
            ctx->PushState();
            ctx->SetFontWeight(FontWeight::Bold);
            DrawCellText(ctx, caption, cellRect, TextAlignment::Center, 1.0f);
            ctx->PopState();
        });
        ctx->DrawLine(Point2Dd(header.x, header.y + tierH),
                      Point2Dd(header.x + header.width, header.y + tierH));
    }

    ctx->DrawLine(Point2Dd(header.x, header.y + header.height),
                  Point2Dd(header.x + header.width, header.y + header.height));
    ctx->PopState();
}

void UltraCanvasGanttChartElement::RenderTableHeader(IRenderContext* ctx) {
    const Rect2Dd& header = layout.tableHeader;
    ctx->PushState();
    ctx->ClipRect(header);
    ctx->DrawFilledRectangle(header, style.tableHeaderBackground);
    ctx->SetFontSize(style.tableFontSize);
    ctx->SetTextPaint(style.tableHeaderText);
    ctx->SetFontWeight(FontWeight::Bold);

    double x = header.x;
    for (const auto& col : style.columns) {
        Rect2Dd cell(x, header.y, col.width, header.height);
        std::string caption = style.uppercaseHeader ? ToUpper(col.title) : col.title;
        DrawCellText(ctx, caption, cell,
                     col.type == GanttColumnType::Name ? TextAlignment::Left
                                                       : TextAlignment::Center);
        x += col.width;
    }
    ctx->PopState();

    ctx->SetStrokePaint(style.headerBorder);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawLine(Point2Dd(header.x, header.y + header.height),
                  Point2Dd(header.x + header.width, header.y + header.height));
}

void UltraCanvasGanttChartElement::RenderTable(IRenderContext* ctx) {
    const Rect2Dd& body = layout.tableBody;
    auto ds = GetGanttDataSource();
    if (!ds) return;

    ctx->PushState();
    ctx->ClipRect(body);
    ctx->DrawFilledRectangle(body, style.tableBackground);
    ctx->SetFontSize(style.tableFontSize);

    const auto& priorityColors = GanttPalettes::PriorityColors();
    static const char* priorityNames[] = {"-", "Low", "Normal", "High", "Urgent"};

    for (size_t i = 0; i < rows.size(); ++i) {
        float top = RowTop(i);
        if (top + style.rowHeight < body.y || top > body.y + body.height) continue;
        const GanttRow& row = rows[i];
        const GanttTask* task = ds->FindTask(row.taskId);
        if (!task) continue;
        Rect2Dd band(body.x, top, body.width, style.rowHeight);

        if (style.rowAlternateColor.a != 0 && (i % 2) == 1) {
            ctx->DrawFilledRectangle(band, style.rowAlternateColor);
        }
        if (style.summaryRowColor.a != 0 && row.isSummary) {
            ctx->DrawFilledRectangle(band, style.summaryRowColor);
        }
        if (static_cast<int>(i) == hoveredRowIndex && style.hoverColor.a != 0) {
            ctx->DrawFilledRectangle(band, style.hoverColor);
        }
        if (row.taskId == selectedTaskId && style.selectionColor.a != 0) {
            ctx->DrawFilledRectangle(band, style.selectionColor);
        }

        bool bold = row.isSummary && style.boldSummaryRows;
        ctx->SetFontWeight(bold ? FontWeight::Bold : FontWeight::Normal);
        ctx->SetTextPaint(row.isSummary ? style.tableSummaryText : style.tableText);

        double x = body.x;
        for (const auto& col : style.columns) {
            Rect2Dd cell(x, top, col.width, style.rowHeight);
            switch (col.type) {
                case GanttColumnType::Wbs:
                    DrawCellText(ctx, row.wbs, cell, TextAlignment::Center);
                    break;
                case GanttColumnType::Name: {
                    double indent = row.depth * style.tableIndentPerLevel;
                    if (row.hasChildren) {
                        // Expand / collapse triangle.
                        double cx = cell.x + 6 + indent;
                        double cy = top + style.rowHeight / 2.0;
                        ctx->SetFillPaint(row.isSummary ? style.tableSummaryText
                                                        : style.tableText);
                        std::vector<Point2Dd> tri;
                        if (row.expanded) {
                            tri = {{cx - 4, cy - 2}, {cx + 4, cy - 2}, {cx, cy + 3}};
                        } else {
                            tri = {{cx - 2, cy - 4}, {cx + 3, cy}, {cx - 2, cy + 4}};
                        }
                        ctx->FillLinePath(tri);
                    }
                    Rect2Dd nameRect(cell.x + indent + (row.hasChildren ? 14 : 4),
                                     cell.y,
                                     cell.width - indent - (row.hasChildren ? 14 : 4),
                                     cell.height);
                    DrawCellText(ctx, task->name, nameRect, TextAlignment::Left);
                    break;
                }
                case GanttColumnType::StartDate:
                    DrawCellText(ctx, FormatDate(row.start), cell, TextAlignment::Center);
                    break;
                case GanttColumnType::EndDate:
                    DrawCellText(ctx, FormatDate(row.end), cell, TextAlignment::Center);
                    break;
                case GanttColumnType::Duration:
                    DrawCellText(ctx, FormatDuration(row.start.DaysUntil(row.end) + 1),
                                 cell, TextAlignment::Center);
                    break;
                case GanttColumnType::Progress: {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "%d%%",
                             static_cast<int>(std::lround(row.progress * 100)));
                    DrawCellText(ctx, buf, cell, TextAlignment::Center);
                    break;
                }
                case GanttColumnType::Assignee:
                    DrawCellText(ctx, task->assignee, cell, TextAlignment::Left);
                    break;
                case GanttColumnType::Priority: {
                    if (task->priority != GanttPriority::NoPriority) {
                        Color chip = priorityColors[static_cast<size_t>(task->priority)];
                        double chipH = std::min(18.0, style.rowHeight - 8.0);
                        Rect2Dd chipRect(cell.x + 4, top + (style.rowHeight - chipH) / 2,
                                         col.width - 8, chipH);
                        ctx->DrawFilledRectangle(chipRect, chip, 0, Colors::Transparent,
                                                 chipH / 2);
                        ctx->PushState();
                        ctx->SetTextPaint(Colors::White);
                        ctx->SetFontSize(style.tableFontSize - 1.0f);
                        DrawCellText(ctx,
                                     priorityNames[static_cast<size_t>(task->priority)],
                                     chipRect, TextAlignment::Center, 2.0f);
                        ctx->PopState();
                    }
                    break;
                }
            }
            x += col.width;
        }

        if (style.showRowLines) {
            ctx->SetStrokePaint(style.rowLineColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(Point2Dd(body.x, top + style.rowHeight),
                          Point2Dd(body.x + body.width, top + style.rowHeight));
        }
    }

    // Column separators.
    if (style.showColumnLines) {
        ctx->SetStrokePaint(style.columnLineColor);
        ctx->SetStrokeWidth(1.0f);
        double x = body.x;
        for (size_t c = 0; c + 1 < style.columns.size(); ++c) {
            x += style.columns[c].width;
            ctx->DrawLine(Point2Dd(x, body.y), Point2Dd(x, body.y + body.height));
        }
    }
    ctx->PopState();
}

Rect2Dd UltraCanvasGanttChartElement::BarRectForRow(size_t rowIndex) const {
    const GanttRow& row = rows[rowIndex];
    double x1 = DateToX(row.start);
    double x2 = DateToX(row.end.AddDays(1));       // End date is inclusive
    float top = RowTop(rowIndex);
    double barH = style.rowHeight * style.barHeightRatio;
    if (row.isMilestone) {
        double size = style.rowHeight * style.milestoneSizeRatio;
        double cx = x1 + style.dayWidth * 0.5;
        double cy = top + style.rowHeight / 2.0;
        return Rect2Dd(cx - size, cy - size, size * 2, size * 2);
    }
    return Rect2Dd(x1, top + (style.rowHeight - barH) / 2.0,
                   std::max(2.0, x2 - x1), barH);
}

void UltraCanvasGanttChartElement::DrawMilestone(IRenderContext* ctx,
                                                 const Point2Dd& center,
                                                 float size, const Color& color) {
    switch (style.milestoneStyle) {
        case GanttMilestoneStyle::Diamond: {
            std::vector<Point2Dd> pts = {
                    {center.x, center.y - size}, {center.x + size, center.y},
                    {center.x, center.y + size}, {center.x - size, center.y}};
            ctx->SetFillPaint(color);
            ctx->FillLinePath(pts);
            break;
        }
        case GanttMilestoneStyle::Circle:
            ctx->DrawFilledCircle(center, size, color);
            break;
        case GanttMilestoneStyle::Square:
            ctx->DrawFilledRectangle(
                    Rect2Dd(center.x - size * 0.8, center.y - size * 0.8,
                            size * 1.6, size * 1.6),
                    color);
            break;
    }
}

void UltraCanvasGanttChartElement::DrawTaskBar(IRenderContext* ctx,
                                               const Rect2Dd& rect,
                                               const Color& color,
                                               const GanttRow& row) {
    double radius = 0.0;
    switch (style.barStyle) {
        case GanttBarStyle::Rounded: radius = style.barCornerRadius; break;
        case GanttBarStyle::Pill:    radius = rect.height / 2.0; break;
        default: break;
    }
    radius = std::min(radius, rect.height / 2.0);

    if (style.barStyle == GanttBarStyle::Line) {
        // Thin line with square end caps (report style).
        double midY = rect.y + rect.height / 2.0;
        double capH = rect.height * 0.55;
        ctx->DrawFilledRectangle(
                Rect2Dd(rect.x, midY - 1.5, rect.width, 3.0), color);
        ctx->DrawFilledRectangle(
                Rect2Dd(rect.x - 2, midY - capH / 2, 5, capH), color);
        ctx->DrawFilledRectangle(
                Rect2Dd(rect.x + rect.width - 3, midY - capH / 2, 5, capH), color);
        // Progress on line bars: solid overlay on the completed span.
        if (style.progressStyle != GanttProgressStyle::Hidden && row.progress > 0) {
            ctx->DrawFilledRectangle(
                    Rect2Dd(rect.x, midY - 3.0, rect.width * row.progress, 6.0),
                    color.WithAlpha(90));
        }
        return;
    }

    Color base = color;
    Color track = color;
    if (style.progressStyle == GanttProgressStyle::LightTrack) {
        track = color.Lighten(style.trackLighten);
        base = track;
    }

    if (style.barStyle == GanttBarStyle::Gradient) {
        auto pattern = ctx->CreateLinearGradientPattern(
                rect.x, rect.y, rect.x, rect.y + rect.height,
                {GradientStop(0.0, base.Lighten(0.18)), GradientStop(1.0, base.Darken(0.1))});
        ctx->PushState();
        if (radius > 0) {
            ctx->RoundedRect(rect.x, rect.y, rect.width, rect.height, radius);
        } else {
            ctx->Rect(rect.x, rect.y, rect.width, rect.height);
        }
        ctx->SetFillPaint(pattern);
        ctx->Fill();
        ctx->PopState();
    } else {
        ctx->DrawFilledRectangle(rect, base, 0, Colors::Transparent, radius);
    }

    // Progress overlays.
    if (row.progress > 0 && style.progressStyle != GanttProgressStyle::Hidden) {
        Rect2Dd done(rect.x, rect.y, rect.width * row.progress, rect.height);
        switch (style.progressStyle) {
            case GanttProgressStyle::DarkerFill:
                ctx->DrawFilledRectangle(done, color.Darken(style.progressDarken),
                                         0, Colors::Transparent, radius);
                break;
            case GanttProgressStyle::LightTrack:
                ctx->DrawFilledRectangle(done, color, 0, Colors::Transparent, radius);
                break;
            case GanttProgressStyle::InnerBar: {
                double stripeH = std::max(3.0, rect.height * 0.28);
                ctx->DrawFilledRectangle(
                        Rect2Dd(rect.x + 2, rect.y + (rect.height - stripeH) / 2,
                                std::max(0.0, (rect.width - 4) * row.progress), stripeH),
                        color.Darken(0.45), 0, Colors::Transparent, stripeH / 2);
                break;
            }
            default:
                break;
        }
    }

    if (style.barBorderWidth > 0 && style.barBorderColor.a != 0) {
        ctx->PushState();
        ctx->SetStrokePaint(style.barBorderColor);
        ctx->SetStrokeWidth(style.barBorderWidth);
        if (radius > 0) {
            ctx->DrawRoundedRectangle(rect, radius);
        } else {
            ctx->DrawRectangle(rect);
        }
        ctx->PopState();
    }
}

void UltraCanvasGanttChartElement::DrawSummaryBar(IRenderContext* ctx,
                                                  const Rect2Dd& rect,
                                                  const Color& color,
                                                  const GanttRow& row) {
    switch (style.summaryStyle) {
        case GanttSummaryStyle::Bar:
            DrawTaskBar(ctx, rect, color, row);
            return;
        case GanttSummaryStyle::Line: {
            double h = std::max(4.0, rect.height * 0.3);
            ctx->DrawFilledRectangle(
                    Rect2Dd(rect.x, rect.y + (rect.height - h) / 2, rect.width, h),
                    color, 0, Colors::Transparent, h / 2);
            return;
        }
        case GanttSummaryStyle::Bracket: {
            // Thin top bar with downward notches at both ends.
            double h = std::max(5.0, rect.height * 0.38);
            double notch = h + 4.0;
            Rect2Dd top(rect.x, rect.y, rect.width, h);
            ctx->DrawFilledRectangle(top, color);
            std::vector<Point2Dd> left = {
                    {rect.x, rect.y + h}, {rect.x + 6, rect.y + h},
                    {rect.x, rect.y + h + notch * 0.6}};
            std::vector<Point2Dd> right = {
                    {rect.x + rect.width - 6, rect.y + h},
                    {rect.x + rect.width, rect.y + h},
                    {rect.x + rect.width, rect.y + h + notch * 0.6}};
            ctx->SetFillPaint(color);
            ctx->FillLinePath(left);
            ctx->FillLinePath(right);
            return;
        }
    }
}

void UltraCanvasGanttChartElement::RenderBars(IRenderContext* ctx) {
    const Rect2Dd& body = layout.timeBody;
    auto ds = GetGanttDataSource();
    if (!ds) return;

    for (size_t i = 0; i < rows.size(); ++i) {
        float top = RowTop(i);
        if (top + style.rowHeight < body.y || top > body.y + body.height) continue;
        const GanttRow& row = rows[i];
        Rect2Dd rect = BarRectForRow(i);
        if (rect.x + rect.width < body.x || rect.x > body.x + body.width) continue;

        Color color = BarColorForRow(i, row);
        const GanttTask* task = ds->FindTask(row.taskId);

        if (row.isMilestone) {
            Color mcolor = (style.milestoneColor.a != 0 &&
                            (!task || task->customColor.a == 0))
                                   ? style.milestoneColor
                                   : color;
            DrawMilestone(ctx,
                          Point2Dd(rect.x + rect.width / 2, rect.y + rect.height / 2),
                          rect.width / 2, mcolor);
        } else if (row.isSummary) {
            DrawSummaryBar(ctx, rect, color, row);
        } else {
            DrawTaskBar(ctx, rect, color, row);
        }

        // ----- Labels -----
        bool wantLabel = style.labelPlacement != GanttLabelPlacement::Hidden &&
                         (!style.labelSummariesOnly || row.isSummary) && task;
        if (wantLabel) {
            std::string label = task->name;
            if (style.showAssigneeWithLabel && !task->assignee.empty() &&
                !row.isSummary) {
                label += "  ·  " + task->assignee;
            }
            if (style.showProgressText && !row.isMilestone) {
                char buf[16];
                snprintf(buf, sizeof(buf), "  %d%%",
                         static_cast<int>(std::lround(row.progress * 100)));
                label += buf;
            }
            ctx->PushState();
            ctx->SetFontSize(style.barLabelFontSize);
            Size2Di sz = ctx->GetTextLineDimensions(label);
            bool fitsInside = !row.isMilestone && sz.width + 12 < rect.width;
            GanttLabelPlacement placement = style.labelPlacement;
            if (placement == GanttLabelPlacement::AutoPlace) {
                placement = fitsInside ? GanttLabelPlacement::InsideBar
                                       : GanttLabelPlacement::RightOfBar;
            }
            if (placement == GanttLabelPlacement::InsideBar && fitsInside) {
                ctx->SetTextPaint(style.barLabelColor);
                DrawCellText(ctx, label, rect, TextAlignment::Left, 8.0f);
            } else if (placement == GanttLabelPlacement::RightOfBar) {
                ctx->SetTextPaint(style.outsideLabelColor);
                Rect2Dd out(rect.x + rect.width + 6, top,
                            body.x + body.width - (rect.x + rect.width) - 6,
                            style.rowHeight);
                DrawCellText(ctx, label, out, TextAlignment::Left, 0.0f);
            }
            ctx->PopState();
        } else if (style.showProgressText && !row.isMilestone && !row.isSummary) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d%%",
                     static_cast<int>(std::lround(row.progress * 100)));
            ctx->PushState();
            ctx->SetFontSize(style.barLabelFontSize);
            ctx->SetTextPaint(style.outsideLabelColor);
            Rect2Dd out(rect.x + rect.width + 6, top, 60, style.rowHeight);
            DrawCellText(ctx, buf, out, TextAlignment::Left, 0.0f);
            ctx->PopState();
        }
    }
}

void UltraCanvasGanttChartElement::DrawDependencyArrow(IRenderContext* ctx,
                                                       const Point2Dd& tip,
                                                       bool pointsRight,
                                                       bool pointsDown,
                                                       bool horizontal,
                                                       const Color& color) {
    float a = style.dependencyArrowSize;
    std::vector<Point2Dd> tri;
    if (horizontal) {
        double dir = pointsRight ? -1.0 : 1.0;
        tri = {{tip.x, tip.y},
               {tip.x + dir * a, tip.y - a * 0.6},
               {tip.x + dir * a, tip.y + a * 0.6}};
    } else {
        double dir = pointsDown ? -1.0 : 1.0;
        tri = {{tip.x, tip.y},
               {tip.x - a * 0.6, tip.y + dir * a},
               {tip.x + a * 0.6, tip.y + dir * a}};
    }
    ctx->SetFillPaint(color);
    ctx->FillLinePath(tri);
}

void UltraCanvasGanttChartElement::RenderDependencies(IRenderContext* ctx) {
    if (style.dependencyStyle == GanttDependencyLineStyle::Hidden) return;
    auto ds = GetGanttDataSource();
    if (!ds) return;

    std::map<int, size_t> rowOf;
    for (size_t i = 0; i < rows.size(); ++i) rowOf[rows[i].taskId] = i;

    ctx->PushState();
    ctx->SetStrokeWidth(style.dependencyLineWidth);
    if (style.dependencyDashed) {
        ctx->SetLineDash(UCDashPattern({4.0, 3.0}));
    }

    for (const auto& dep : ds->GetDependencies()) {
        auto pIt = rowOf.find(dep.predecessorId);
        auto sIt = rowOf.find(dep.successorId);
        if (pIt == rowOf.end() || sIt == rowOf.end()) continue;
        size_t pi = pIt->second, si = sIt->second;

        Rect2Dd pr = BarRectForRow(pi);
        Rect2Dd sr = BarRectForRow(si);
        double pCy = pr.y + pr.height / 2.0;
        double sCy = sr.y + sr.height / 2.0;

        Color lineColor = style.dependencyColor;
        if (style.dependencyUsesBarColor) {
            lineColor = BarColorForRow(pi, rows[pi]);
        }
        if (style.highlightCriticalPath && rows[pi].critical && rows[si].critical) {
            lineColor = style.criticalColor;
        }
        ctx->SetStrokePaint(lineColor);

        bool fromStart = dep.type == GanttDependencyType::StartToStart ||
                         dep.type == GanttDependencyType::StartToFinish;
        bool intoEnd = dep.type == GanttDependencyType::FinishToFinish ||
                       dep.type == GanttDependencyType::StartToFinish;
        double exitX = fromStart ? pr.x : pr.x + pr.width;
        double entryX = intoEnd ? sr.x + sr.width : sr.x;

        if (style.dependencyStyle == GanttDependencyLineStyle::Straight) {
            ctx->DrawLine(Point2Dd(exitX, pCy), Point2Dd(entryX, sCy));
            DrawDependencyArrow(ctx, Point2Dd(entryX, sCy), !intoEnd, sCy > pCy,
                                true, lineColor);
            continue;
        }

        // Elbow routing.
        std::vector<Point2Dd> pts;
        double stub = 8.0;
        if (!fromStart && !intoEnd) {
            // Finish -> Start
            double xKnee = exitX + stub;
            if (xKnee <= entryX - stub) {
                pts = {{exitX, pCy}, {xKnee, pCy}, {xKnee, sCy},
                       {entryX, sCy}};
            } else {
                // Backtracking: drop between the rows and come in from the left.
                double midY = (sCy > pCy) ? sr.y - 3 : sr.y + sr.height + 3;
                pts = {{exitX, pCy}, {xKnee, pCy}, {xKnee, midY},
                       {entryX - stub, midY}, {entryX - stub, sCy}, {entryX, sCy}};
            }
        } else if (fromStart && !intoEnd) {
            // Start -> Start
            double xKnee = std::min(exitX, entryX) - stub;
            pts = {{exitX, pCy}, {xKnee, pCy}, {xKnee, sCy}, {entryX, sCy}};
        } else if (!fromStart && intoEnd) {
            // Finish -> Finish
            double xKnee = std::max(exitX, entryX) + stub;
            pts = {{exitX, pCy}, {xKnee, pCy}, {xKnee, sCy}, {entryX, sCy}};
        } else {
            // Start -> Finish
            double xKnee = std::max(entryX + stub, exitX - stub);
            pts = {{exitX, pCy}, {exitX - stub, pCy}, {exitX - stub, (pCy + sCy) / 2},
                   {xKnee, (pCy + sCy) / 2}, {xKnee, sCy}, {entryX, sCy}};
        }
        ctx->DrawLinePath(pts, false);
        DrawDependencyArrow(ctx, Point2Dd(entryX, sCy), !intoEnd, sCy > pCy,
                            true, lineColor);
    }
    ctx->PopState();
}

void UltraCanvasGanttChartElement::RenderTodayLine(IRenderContext* ctx) {
    if (!style.showTodayLine) return;
    double x = DateToX(style.today) + style.dayWidth * 0.5;
    const Rect2Dd& body = layout.timeBody;
    if (x < body.x || x > body.x + body.width) return;
    ctx->PushState();
    ctx->SetStrokePaint(style.todayLineColor);
    ctx->SetStrokeWidth(1.6f);
    ctx->SetLineDash(UCDashPattern({5.0, 3.0}));
    ctx->DrawLine(Point2Dd(x, body.y), Point2Dd(x, body.y + body.height));
    ctx->PopState();
}

// =============================================================================
// ELEMENT: INTERACTION
// =============================================================================

void UltraCanvasGanttChartElement::ClampScroll() {
    float maxX = std::max(0.0f, layout.contentWidth -
                                static_cast<float>(layout.timeBody.width));
    float maxY = std::max(0.0f, layout.contentHeight -
                                static_cast<float>(layout.timeBody.height));
    scrollX = std::clamp(scrollX, 0.0f, maxX);
    scrollY = std::clamp(scrollY, 0.0f, maxY);
}

std::string UltraCanvasGanttChartElement::BuildTaskTooltip(const GanttRow& row) const {
    auto ds = GetGanttDataSource();
    const GanttTask* task = ds ? ds->FindTask(row.taskId) : nullptr;
    if (!task) return "";

    std::ostringstream out;
    out << task->name;
    if (row.isMilestone) {
        out << "\nMilestone: " << FormatDate(row.start);
    } else {
        out << "\n" << FormatDate(row.start) << " - " << FormatDate(row.end)
            << "  (" << (row.start.DaysUntil(row.end) + 1) << " days)";
        out << "\nProgress: "
            << static_cast<int>(std::lround(row.progress * 100)) << "%";
    }
    if (!task->assignee.empty()) out << "\nAssignee: " << task->assignee;
    if (style.highlightCriticalPath && row.critical) out << "\nCritical path";
    if (!task->notes.empty()) out << "\n" << task->notes;

    auto preds = ds->GetPredecessors(task->id);
    if (!preds.empty()) {
        out << "\nDepends on: ";
        for (size_t i = 0; i < preds.size(); ++i) {
            const GanttTask* p = ds->FindTask(preds[i]);
            if (!p) continue;
            if (i) out << ", ";
            out << p->name;
        }
    }
    return out.str();
}

bool UltraCanvasGanttChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
    int index = -1;
    const GanttRow* row = RowAt(static_cast<float>(mousePos.y), &index);
    bool inBody = mousePos.y >= layout.timeBody.y &&
                  (mousePos.x >= layout.timeBody.x ||
                   (style.showTable && mousePos.x < layout.tableBody.x +
                                                    layout.tableBody.width));
    if (!inBody) {
        row = nullptr;
        index = -1;
    }

    if (index != hoveredRowIndex) {
        hoveredRowIndex = index;
        RequestRedraw();
    }

    if (row && enableTooltips) {
        std::string content = BuildTaskTooltip(*row);
        if (!content.empty()) {
            auto windowMousePos = MapFromLocal(mousePos, nullptr);
            UltraCanvasTooltipManager::UpdateAndShowTooltip(this->window, content,
                                                            windowMousePos);
            isTooltipActive = true;
            return true;
        }
    }
    if (isTooltipActive) HideTooltip();
    return row != nullptr;
}

bool UltraCanvasGanttChartElement::HandleTimelineWheel(const UCEvent& event) {
    if (event.ctrl) {
        // Zoom around the cursor date.
        double mouseX = event.pointer.x;
        double dateAtCursor = (mouseX - layout.timeBody.x + scrollX) / style.dayWidth;
        float factor = event.wheelDelta > 0 ? 1.15f : (1.0f / 1.15f);
        style.dayWidth = std::clamp(style.dayWidth * factor, 0.5f, 200.0f);
        ComputeLayout();
        scrollX = static_cast<float>(dateAtCursor * style.dayWidth -
                                     (mouseX - layout.timeBody.x));
        ClampScroll();
        RequestRedraw();
        return true;
    }

    float step = style.rowHeight * 3;
    if (event.shift) {
        scrollX += (event.wheelDelta > 0 ? -step : step);
    } else if (layout.contentHeight > layout.timeBody.height) {
        scrollY += (event.wheelDelta > 0 ? -step : step);
    } else {
        scrollX += (event.wheelDelta > 0 ? -step : step);
    }
    ClampScroll();
    RequestRedraw();
    return true;
}

bool UltraCanvasGanttChartElement::HandleClick(const UCEvent& event, bool doubleClick) {
    int index = -1;
    const GanttRow* row = RowAt(static_cast<float>(event.pointer.y), &index);
    if (!row) {
        SelectTask(-1);
        return false;
    }

    auto ds = GetGanttDataSource();

    // Expand / collapse triangle in the Name column.
    if (!doubleClick && ds && row->hasChildren && style.showTable &&
        event.pointer.x < layout.tableBody.x + layout.tableBody.width) {
        double colX = layout.tableBody.x;
        for (const auto& col : style.columns) {
            if (col.type == GanttColumnType::Name) {
                double triX = colX + 6 + row->depth * style.tableIndentPerLevel;
                if (event.pointer.x >= triX - 8 && event.pointer.x <= triX + 10) {
                    bool expanded = !row->expanded;
                    int taskId = row->taskId;   // `row` invalidates on rebuild
                    ds->SetTaskExpanded(taskId, expanded);
                    rowsDirty = true;
                    if (onTaskToggled) onTaskToggled(taskId, expanded);
                    RequestRedraw();
                    return true;
                }
                break;
            }
            colX += col.width;
        }
    }

    SelectTask(row->taskId);
    if (doubleClick) {
        if (ds && row->hasChildren) {
            int taskId = row->taskId;
            bool expanded = !row->expanded;
            ds->SetTaskExpanded(taskId, expanded);
            rowsDirty = true;
            if (onTaskToggled) onTaskToggled(taskId, expanded);
            RequestRedraw();
        }
        if (onTaskDoubleClick) onTaskDoubleClick(row->taskId);
    } else {
        if (onTaskClick) onTaskClick(row->taskId);
    }
    return true;
}

bool UltraCanvasGanttChartElement::OnEvent(const UCEvent& event) {
    if (IsDisabled() || !IsVisible()) return false;
    EnsureRows();
    ComputeLayout();

    switch (event.type) {
        case UCEventType::MouseDown:
            if (event.button == UCMouseButton::Left) {
                if (event.pointer.x >= layout.timeBody.x &&
                    event.pointer.y >= layout.timeBody.y) {
                    panningTimeline = true;
                    panAnchor = event.pointer;
                }
                return HandleClick(event, false);
            }
            return false;

        case UCEventType::MouseDoubleClick:
            return HandleClick(event, true);

        case UCEventType::MouseUp:
            panningTimeline = false;
            return true;

        case UCEventType::MouseMove:
            // Drag-pan; short drags still behave like clicks because
            // selection already ran on MouseDown.
            if (panningTimeline) {
                int dx = event.pointer.x - panAnchor.x;
                int dy = event.pointer.y - panAnchor.y;
                if (dx != 0 || dy != 0) {
                    scrollX -= dx;
                    scrollY -= dy;
                    panAnchor = event.pointer;
                    ClampScroll();
                    RequestRedraw();
                }
            }
            return HandleChartMouseMove(event.pointer);

        case UCEventType::MouseWheel:
            return HandleTimelineWheel(event);

        case UCEventType::MouseLeave:
            panningTimeline = false;
            if (hoveredRowIndex != -1) {
                hoveredRowIndex = -1;
                RequestRedraw();
            }
            if (isTooltipActive) HideTooltip();
            return true;

        default:
            return false;
    }
}

} // namespace UltraCanvas
