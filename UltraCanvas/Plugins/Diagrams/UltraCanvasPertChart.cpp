// Plugins/Diagrams/UltraCanvasPertChart.cpp
// Interactive PERT chart component implementation
// Version: 1.0.0
//
// Changelog:
//   1.0.0 - Initial release:
//           * CPM forward/backward pass with cycle detection; slack and
//             critical-path flags per activity.
//           * PERT three-point statistics (expected duration, variance,
//             completion probability via the normal approximation).
//           * Four node designs (Card, DetailedCard, Compact, Circle) and
//             eight built-in palettes plus custom palettes.
//           * Layered auto-layout with barycenter ordering; manual position
//             overrides survive relayout.
//           * Straight / orthogonal / curved connectors, dashed dummy
//             dependencies, group color-coding with legend.
//           * Pan/zoom/drag/hover/selection interaction with callbacks.

#include "Plugins/Diagrams/UltraCanvasPertChart.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace UltraCanvas {

namespace {
constexpr double kSlackEpsilon = 1e-6;

// Multi-purpose small helper: does the connector between these two critical
// activities actually lie on the critical path (no slack between them)?
bool IsCriticalEdge(const PertActivity& from, const PertActivity& to) {
    return from.onCriticalPath && to.onCriticalPath &&
           std::fabs(from.earliestFinish - to.earliestStart) < kSlackEpsilon;
}
} // namespace

// =============================================================================
// BUILT-IN PALETTES
// =============================================================================

PertChartPalette PertChartPalette::BuiltIn(PertChartPaletteKind kind) {
    PertChartPalette p; // defaults == Classic
    switch (kind) {
        case PertChartPaletteKind::Classic:
        case PertChartPaletteKind::Custom:
            break;

        case PertChartPaletteKind::Ocean:
            p.backgroundColor = Color(235, 242, 250, 255);
            p.gridColor = Color(220, 230, 242, 255);
            p.nodeHeaderColor = Color(25, 95, 170, 255);
            p.nodeFillColor = Color(250, 253, 255, 255);
            p.nodeBorderColor = Color(25, 95, 170, 255);
            p.milestoneColor = Color(10, 50, 100, 255);
            p.connectorColor = Color(90, 130, 175, 255);
            p.criticalConnectorColor = Color(230, 100, 40, 255);
            p.criticalNodeAccentColor = Color(230, 100, 40, 255);
            p.groupColors = {
                Color(90, 170, 235, 255), Color(25, 95, 170, 255),
                Color(10, 50, 100, 255), Color(60, 190, 200, 255),
                Color(120, 145, 220, 255), Color(50, 130, 150, 255)
            };
            break;

        case PertChartPaletteKind::Vibrant:
            p.backgroundColor = Color(255, 255, 255, 255);
            p.gridColor = Color(240, 240, 240, 255);
            p.nodeHeaderColor = Color(20, 145, 235, 255);
            p.nodeFillColor = Color(255, 255, 255, 255);
            p.nodeBorderColor = Color(20, 145, 235, 255);
            p.milestoneColor = Color(235, 195, 15, 255);
            p.milestoneTextColor = Color(60, 50, 0, 255);
            p.connectorColor = Color(150, 155, 160, 255);
            p.criticalConnectorColor = Color(230, 60, 40, 255);
            p.criticalNodeAccentColor = Color(230, 60, 40, 255);
            p.groupColors = {
                Color(20, 145, 235, 255), Color(250, 90, 25, 255),
                Color(110, 190, 30, 255), Color(235, 195, 15, 255),
                Color(170, 80, 220, 255), Color(0, 175, 170, 255)
            };
            break;

        case PertChartPaletteKind::Pastel:
            p.backgroundColor = Color(252, 250, 248, 255);
            p.gridColor = Color(242, 238, 234, 255);
            p.nodeHeaderColor = Color(150, 170, 220, 255);
            p.nodeFillColor = Color(255, 255, 255, 255);
            p.nodeBorderColor = Color(150, 170, 220, 255);
            p.nodeHeaderTextColor = Color(35, 40, 60, 255);
            p.milestoneColor = Color(190, 165, 215, 255);
            p.milestoneTextColor = Color(50, 35, 70, 255);
            p.connectorColor = Color(170, 175, 185, 255);
            p.criticalConnectorColor = Color(225, 140, 130, 255);
            p.criticalNodeAccentColor = Color(225, 140, 130, 255);
            p.groupColors = {
                Color(165, 200, 235, 255), Color(250, 200, 170, 255),
                Color(185, 225, 185, 255), Color(240, 220, 160, 255),
                Color(210, 185, 230, 255), Color(235, 185, 200, 255)
            };
            break;

        case PertChartPaletteKind::Mint:
            p.backgroundColor = Color(250, 253, 252, 255);
            p.gridColor = Color(235, 244, 241, 255);
            p.nodeHeaderColor = Color(75, 200, 180, 255);
            p.nodeFillColor = Color(255, 255, 255, 255);
            p.nodeBorderColor = Color(60, 175, 160, 255);
            p.nodeHeaderTextColor = Color(255, 255, 255, 255);
            p.milestoneColor = Color(35, 130, 120, 255);
            p.connectorColor = Color(60, 70, 80, 255);
            p.criticalConnectorColor = Color(250, 110, 90, 255);
            p.criticalNodeAccentColor = Color(250, 110, 90, 255);
            p.groupColors = {
                Color(75, 200, 180, 255), Color(35, 130, 120, 255),
                Color(140, 220, 200, 255), Color(90, 165, 200, 255),
                Color(245, 170, 100, 255), Color(230, 120, 160, 255)
            };
            break;

        case PertChartPaletteKind::Midnight:
            p.backgroundColor = Color(250, 251, 253, 255);
            p.gridColor = Color(238, 240, 244, 255);
            p.nodeHeaderColor = Color(18, 35, 60, 255);
            p.nodeFillColor = Color(18, 35, 60, 255);
            p.nodeBorderColor = Color(18, 35, 60, 255);
            p.nodeTextColor = Color(235, 240, 248, 255);
            p.nodeSecondaryTextColor = Color(160, 175, 200, 255);
            p.nodeDividerColor = Color(50, 70, 100, 255);
            p.milestoneColor = Color(8, 18, 35, 255);
            p.connectorColor = Color(60, 75, 100, 255);
            p.criticalConnectorColor = Color(220, 90, 60, 255);
            p.criticalNodeAccentColor = Color(220, 90, 60, 255);
            p.groupColors = {
                Color(18, 35, 60, 255), Color(35, 65, 105, 255),
                Color(60, 100, 150, 255), Color(25, 80, 90, 255),
                Color(80, 55, 110, 255), Color(100, 70, 40, 255)
            };
            break;

        case PertChartPaletteKind::Dark:
            p.backgroundColor = Color(28, 30, 36, 255);
            p.gridColor = Color(42, 45, 54, 255);
            p.nodeHeaderColor = Color(70, 120, 200, 255);
            p.nodeFillColor = Color(44, 48, 58, 255);
            p.nodeBorderColor = Color(90, 130, 195, 255);
            p.nodeHeaderTextColor = Color(240, 244, 250, 255);
            p.nodeTextColor = Color(225, 230, 238, 255);
            p.nodeSecondaryTextColor = Color(150, 158, 172, 255);
            p.nodeDividerColor = Color(70, 76, 90, 255);
            p.milestoneColor = Color(200, 165, 60, 255);
            p.milestoneTextColor = Color(30, 25, 5, 255);
            p.connectorColor = Color(130, 140, 158, 255);
            p.criticalConnectorColor = Color(245, 110, 85, 255);
            p.criticalNodeAccentColor = Color(245, 110, 85, 255);
            p.dummyConnectorColor = Color(100, 108, 122, 255);
            p.connectorLabelColor = Color(210, 216, 226, 255);
            p.legendTextColor = Color(205, 212, 224, 255);
            p.groupColors = {
                Color(70, 120, 200, 255), Color(200, 120, 70, 255),
                Color(90, 175, 110, 255), Color(200, 165, 60, 255),
                Color(160, 100, 210, 255), Color(70, 170, 180, 255)
            };
            break;

        case PertChartPaletteKind::Monochrome:
            p.backgroundColor = Color(252, 252, 252, 255);
            p.gridColor = Color(240, 240, 240, 255);
            p.nodeHeaderColor = Color(70, 70, 70, 255);
            p.nodeFillColor = Color(255, 255, 255, 255);
            p.nodeBorderColor = Color(70, 70, 70, 255);
            p.nodeTextColor = Color(40, 40, 40, 255);
            p.nodeSecondaryTextColor = Color(120, 120, 120, 255);
            p.nodeDividerColor = Color(215, 215, 215, 255);
            p.milestoneColor = Color(30, 30, 30, 255);
            p.connectorColor = Color(110, 110, 110, 255);
            p.criticalConnectorColor = Color(20, 20, 20, 255);
            p.criticalNodeAccentColor = Color(20, 20, 20, 255);
            p.groupColors = {
                Color(90, 90, 90, 255), Color(140, 140, 140, 255),
                Color(50, 50, 50, 255), Color(180, 180, 180, 255),
                Color(110, 110, 110, 255), Color(70, 70, 70, 255)
            };
            break;
    }
    return p;
}

// =============================================================================
// CONSTRUCTION
// =============================================================================

UltraCanvasPertChart::UltraCanvasPertChart(const std::string& id,
                                           int x, int y, int width, int height)
    : UltraCanvasUIElement(id, x, y, width, height) {
}

// =============================================================================
// ACTIVITY MANAGEMENT
// =============================================================================

void UltraCanvasPertChart::AddActivity(const std::string& id, const std::string& name) {
    AddActivity(id, name, 1.0, "");
}

void UltraCanvasPertChart::AddActivity(const std::string& id, const std::string& name,
                                       double duration) {
    AddActivity(id, name, duration, "");
}

void UltraCanvasPertChart::AddActivity(const std::string& id, const std::string& name,
                                       double duration, const std::string& code) {
    if (activities.find(id) != activities.end()) return;

    PertActivity a(id, name);
    a.duration = std::max(0.0, duration);
    a.code = code;
    activities[id] = a;
    MarkGraphChanged();
}

void UltraCanvasPertChart::AddMilestone(const std::string& id, const std::string& name) {
    if (activities.find(id) != activities.end()) return;

    PertActivity a(id, name);
    a.duration = 0.0;
    a.isMilestone = true;
    activities[id] = a;
    MarkGraphChanged();
}

void UltraCanvasPertChart::RemoveActivity(const std::string& id) {
    dependencies.erase(
        std::remove_if(dependencies.begin(), dependencies.end(),
            [&id](const PertDependency& d) {
                return d.fromId == id || d.toId == id;
            }),
        dependencies.end()
    );
    activities.erase(id);

    if (selectedActivityId == id) selectedActivityId.clear();
    if (hoveredActivityId == id) hoveredActivityId.clear();
    MarkGraphChanged();
}

void UltraCanvasPertChart::Clear() {
    activities.clear();
    dependencies.clear();
    groups.clear();
    selectedActivityId.clear();
    hoveredActivityId.clear();
    projectDuration = 0.0;
    MarkGraphChanged();
}

void UltraCanvasPertChart::SetActivityDuration(const std::string& id, double duration) {
    auto* a = GetActivity(id);
    if (a) {
        a->duration = std::max(0.0, duration);
        scheduleDirty = true;
        RequestRedraw();
    }
}

void UltraCanvasPertChart::SetActivityThreePointEstimate(const std::string& id,
                                                         double optimistic,
                                                         double mostLikely,
                                                         double pessimistic) {
    auto* a = GetActivity(id);
    if (a) {
        a->optimistic = optimistic;
        a->mostLikely = mostLikely;
        a->pessimistic = pessimistic;
        a->hasEstimates = true;
        scheduleDirty = true;
        RequestRedraw();
    }
}

void UltraCanvasPertChart::ClearActivityThreePointEstimate(const std::string& id) {
    auto* a = GetActivity(id);
    if (a) {
        a->hasEstimates = false;
        scheduleDirty = true;
        RequestRedraw();
    }
}

void UltraCanvasPertChart::SetActivityDates(const std::string& id,
                                            const std::string& startDate,
                                            const std::string& endDate) {
    auto* a = GetActivity(id);
    if (a) {
        a->startDate = startDate;
        a->endDate = endDate;
        RequestRedraw();
    }
}

void UltraCanvasPertChart::SetActivityResponsible(const std::string& id,
                                                  const std::string& responsible) {
    auto* a = GetActivity(id);
    if (a) {
        a->responsible = responsible;
        RequestRedraw();
    }
}

void UltraCanvasPertChart::SetActivityCode(const std::string& id, const std::string& code) {
    auto* a = GetActivity(id);
    if (a) {
        a->code = code;
        RequestRedraw();
    }
}

void UltraCanvasPertChart::SetActivityGroup(const std::string& id, const std::string& groupId) {
    auto* a = GetActivity(id);
    if (a) {
        a->groupId = groupId;
        RequestRedraw();
    }
}

void UltraCanvasPertChart::SetActivityColors(const std::string& id, const Color& headerColor,
                                             const Color& fillColor, const Color& borderColor,
                                             const Color& textColor) {
    auto* a = GetActivity(id);
    if (a) {
        a->hasCustomColors = true;
        a->customHeaderColor = headerColor;
        a->customFillColor = fillColor;
        a->customBorderColor = borderColor;
        a->customTextColor = textColor;
        RequestRedraw();
    }
}

void UltraCanvasPertChart::ClearActivityColors(const std::string& id) {
    auto* a = GetActivity(id);
    if (a) {
        a->hasCustomColors = false;
        RequestRedraw();
    }
}

void UltraCanvasPertChart::SetActivityPosition(const std::string& id, double x, double y) {
    auto* a = GetActivity(id);
    if (a) {
        a->x = x;
        a->y = y;
        a->hasManualPosition = true;
        RequestRedraw();
    }
}

void UltraCanvasPertChart::SetActivitySize(const std::string& id, double width, double height) {
    auto* a = GetActivity(id);
    if (a) {
        a->width = width;
        a->height = height;
        RequestRedraw();
    }
}

PertActivity* UltraCanvasPertChart::GetActivity(const std::string& id) {
    auto it = activities.find(id);
    return (it != activities.end()) ? &it->second : nullptr;
}

const PertActivity* UltraCanvasPertChart::GetActivity(const std::string& id) const {
    auto it = activities.find(id);
    return (it != activities.end()) ? &it->second : nullptr;
}

std::vector<std::string> UltraCanvasPertChart::GetAllActivityIds() const {
    std::vector<std::string> ids;
    ids.reserve(activities.size());
    for (const auto& pair : activities) {
        ids.push_back(pair.first);
    }
    return ids;
}

// =============================================================================
// DEPENDENCY MANAGEMENT
// =============================================================================

void UltraCanvasPertChart::AddDependency(const std::string& id, const std::string& fromId,
                                         const std::string& toId) {
    if (activities.find(fromId) == activities.end() ||
        activities.find(toId) == activities.end() || fromId == toId) {
        return;
    }
    PertDependency dep(id, fromId, toId);
    dependencies.push_back(dep);
    MarkGraphChanged();
}

void UltraCanvasPertChart::AddDummyDependency(const std::string& id, const std::string& fromId,
                                              const std::string& toId) {
    if (activities.find(fromId) == activities.end() ||
        activities.find(toId) == activities.end() || fromId == toId) {
        return;
    }
    PertDependency dep(id, fromId, toId);
    dep.isDummy = true;
    dependencies.push_back(dep);
    MarkGraphChanged();
}

void UltraCanvasPertChart::RemoveDependency(const std::string& id) {
    dependencies.erase(
        std::remove_if(dependencies.begin(), dependencies.end(),
            [&id](const PertDependency& d) { return d.id == id; }),
        dependencies.end()
    );
    MarkGraphChanged();
}

void UltraCanvasPertChart::SetDependencyLabel(const std::string& id, const std::string& label) {
    auto* dep = GetDependency(id);
    if (dep) {
        dep->label = label;
        RequestRedraw();
    }
}

void UltraCanvasPertChart::SetDependencyColor(const std::string& id, const Color& color) {
    auto* dep = GetDependency(id);
    if (dep) {
        dep->hasCustomColor = true;
        dep->customColor = color;
        RequestRedraw();
    }
}

void UltraCanvasPertChart::ClearDependencyColor(const std::string& id) {
    auto* dep = GetDependency(id);
    if (dep) {
        dep->hasCustomColor = false;
        RequestRedraw();
    }
}

PertDependency* UltraCanvasPertChart::GetDependency(const std::string& id) {
    for (auto& dep : dependencies) {
        if (dep.id == id) return &dep;
    }
    return nullptr;
}

std::vector<std::string> UltraCanvasPertChart::GetAllDependencyIds() const {
    std::vector<std::string> ids;
    ids.reserve(dependencies.size());
    for (const auto& dep : dependencies) {
        ids.push_back(dep.id);
    }
    return ids;
}

// =============================================================================
// GROUPS
// =============================================================================

void UltraCanvasPertChart::DefineGroup(const std::string& groupId, const std::string& name) {
    for (auto& g : groups) {
        if (g.id == groupId) {
            g.name = name;
            RequestRedraw();
            return;
        }
    }
    PertGroup g;
    g.id = groupId;
    g.name = name;
    groups.push_back(g);
    RequestRedraw();
}

void UltraCanvasPertChart::DefineGroup(const std::string& groupId, const std::string& name,
                                       const Color& color) {
    for (auto& g : groups) {
        if (g.id == groupId) {
            g.name = name;
            g.color = color;
            g.colorAssigned = true;
            RequestRedraw();
            return;
        }
    }
    PertGroup g;
    g.id = groupId;
    g.name = name;
    g.color = color;
    g.colorAssigned = true;
    groups.push_back(g);
    RequestRedraw();
}

void UltraCanvasPertChart::RemoveGroup(const std::string& groupId) {
    groups.erase(
        std::remove_if(groups.begin(), groups.end(),
            [&groupId](const PertGroup& g) { return g.id == groupId; }),
        groups.end()
    );
    for (auto& pair : activities) {
        if (pair.second.groupId == groupId) {
            pair.second.groupId.clear();
        }
    }
    RequestRedraw();
}

const PertGroup* UltraCanvasPertChart::GetGroup(const std::string& groupId) const {
    for (const auto& g : groups) {
        if (g.id == groupId) return &g;
    }
    return nullptr;
}

std::vector<std::string> UltraCanvasPertChart::GetAllGroupIds() const {
    std::vector<std::string> ids;
    ids.reserve(groups.size());
    for (const auto& g : groups) {
        ids.push_back(g.id);
    }
    return ids;
}

Color UltraCanvasPertChart::GetGroupColor(const std::string& groupId) const {
    for (size_t i = 0; i < groups.size(); ++i) {
        if (groups[i].id == groupId) {
            if (groups[i].colorAssigned) return groups[i].color;
            if (!palette.groupColors.empty()) {
                return palette.groupColors[i % palette.groupColors.size()];
            }
            return palette.nodeHeaderColor;
        }
    }
    return palette.nodeHeaderColor;
}

void UltraCanvasPertChart::SetLegendVisible(bool visible) {
    showLegend = visible;
    RequestRedraw();
}

// =============================================================================
// SCHEDULE ANALYSIS
// =============================================================================

std::vector<std::string> UltraCanvasPertChart::TopologicalOrder() const {
    std::unordered_map<std::string, int> indegree;
    std::unordered_map<std::string, std::vector<std::string>> successors;
    for (const auto& pair : activities) {
        indegree[pair.first] = 0;
    }
    for (const auto& dep : dependencies) {
        if (indegree.find(dep.fromId) == indegree.end() ||
            indegree.find(dep.toId) == indegree.end()) {
            continue;
        }
        indegree[dep.toId]++;
        successors[dep.fromId].push_back(dep.toId);
    }

    // Sources in map (id) order for deterministic results.
    std::vector<std::string> queue;
    for (const auto& pair : activities) {
        if (indegree[pair.first] == 0) queue.push_back(pair.first);
    }

    std::vector<std::string> order;
    order.reserve(activities.size());
    size_t head = 0;
    while (head < queue.size()) {
        std::string id = queue[head++];
        order.push_back(id);
        auto it = successors.find(id);
        if (it == successors.end()) continue;
        for (const auto& succ : it->second) {
            if (--indegree[succ] == 0) queue.push_back(succ);
        }
    }

    if (order.size() != activities.size()) {
        return {}; // cycle
    }
    return order;
}

bool UltraCanvasPertChart::HasCycle() const {
    if (activities.empty()) return false;
    return TopologicalOrder().empty();
}

bool UltraCanvasPertChart::ComputeSchedule() {
    scheduleDirty = false;
    if (activities.empty()) {
        scheduleValid = true;
        projectDuration = 0.0;
        return true;
    }

    std::vector<std::string> order = TopologicalOrder();
    if (order.empty()) {
        scheduleValid = false;
        return false;
    }

    std::unordered_map<std::string, std::vector<std::string>> preds, succs;
    for (const auto& dep : dependencies) {
        if (activities.find(dep.fromId) == activities.end() ||
            activities.find(dep.toId) == activities.end()) {
            continue;
        }
        preds[dep.toId].push_back(dep.fromId);
        succs[dep.fromId].push_back(dep.toId);
    }

    // Forward pass: earliest start / earliest finish, plus layer depth.
    projectDuration = 0.0;
    for (const auto& id : order) {
        PertActivity& a = activities[id];
        a.earliestStart = 0.0;
        a.layer = 0;
        auto it = preds.find(id);
        if (it != preds.end()) {
            for (const auto& predId : it->second) {
                const PertActivity& p = activities[predId];
                a.earliestStart = std::max(a.earliestStart, p.earliestFinish);
                a.layer = std::max(a.layer, p.layer + 1);
            }
        }
        a.earliestFinish = a.earliestStart + GetExpectedDuration(id);
        projectDuration = std::max(projectDuration, a.earliestFinish);
    }

    // Backward pass: latest finish / latest start / slack.
    for (auto rit = order.rbegin(); rit != order.rend(); ++rit) {
        PertActivity& a = activities[*rit];
        auto it = succs.find(*rit);
        if (it == succs.end() || it->second.empty()) {
            a.latestFinish = projectDuration;
        } else {
            a.latestFinish = std::numeric_limits<double>::max();
            for (const auto& succId : it->second) {
                a.latestFinish = std::min(a.latestFinish, activities[succId].latestStart);
            }
        }
        a.latestStart = a.latestFinish - GetExpectedDuration(*rit);
        a.slack = a.latestStart - a.earliestStart;
        a.onCriticalPath = a.slack < kSlackEpsilon;
    }

    scheduleValid = true;
    if (onScheduleComputed) {
        onScheduleComputed(projectDuration);
    }
    return true;
}

double UltraCanvasPertChart::GetProjectDuration() const {
    return projectDuration;
}

std::vector<std::string> UltraCanvasPertChart::GetCriticalPath() const {
    std::vector<std::string> path;
    if (!scheduleValid) return path;
    for (const auto& id : TopologicalOrder()) {
        auto it = activities.find(id);
        if (it != activities.end() && it->second.onCriticalPath) {
            path.push_back(id);
        }
    }
    return path;
}

bool UltraCanvasPertChart::IsOnCriticalPath(const std::string& id) const {
    const auto* a = GetActivity(id);
    return a && scheduleValid && a->onCriticalPath;
}

double UltraCanvasPertChart::GetSlack(const std::string& id) const {
    const auto* a = GetActivity(id);
    return a ? a->slack : 0.0;
}

double UltraCanvasPertChart::GetExpectedDuration(const std::string& id) const {
    const auto* a = GetActivity(id);
    if (!a) return 0.0;
    if (a->hasEstimates) {
        return (a->optimistic + 4.0 * a->mostLikely + a->pessimistic) / 6.0;
    }
    return a->duration;
}

double UltraCanvasPertChart::GetActivityVariance(const std::string& id) const {
    const auto* a = GetActivity(id);
    if (!a || !a->hasEstimates) return 0.0;
    double sigma = (a->pessimistic - a->optimistic) / 6.0;
    return sigma * sigma;
}

double UltraCanvasPertChart::GetProjectVariance() const {
    double variance = 0.0;
    for (const auto& pair : activities) {
        if (pair.second.onCriticalPath) {
            variance += GetActivityVariance(pair.first);
        }
    }
    return variance;
}

double UltraCanvasPertChart::GetProjectStandardDeviation() const {
    return std::sqrt(GetProjectVariance());
}

double UltraCanvasPertChart::GetCompletionProbability(double targetDuration) const {
    double sigma = GetProjectStandardDeviation();
    if (sigma < kSlackEpsilon) {
        if (targetDuration > projectDuration) return 1.0;
        if (targetDuration < projectDuration) return 0.0;
        return 0.5;
    }
    double z = (targetDuration - projectDuration) / sigma;
    return 0.5 * std::erfc(-z / std::sqrt(2.0));
}

void UltraCanvasPertChart::MarkGraphChanged() {
    scheduleDirty = true;
    layoutDirty = true;
    RequestRedraw();
}

void UltraCanvasPertChart::EnsureScheduleAndLayout() {
    if (scheduleDirty) {
        ComputeSchedule();
    }
    if (layoutDirty) {
        AutoLayout(false);
    }
}

// =============================================================================
// DESIGN OPTIONS
// =============================================================================

void UltraCanvasPertChart::SetNodeDesign(PertNodeDesign design) {
    if (nodeDesign == design) return;
    nodeDesign = design;
    layoutDirty = true; // default node sizes differ per design
    RequestRedraw();
}

void UltraCanvasPertChart::SetConnectorStyle(PertConnectorStyle s) {
    connectorStyle = s;
    RequestRedraw();
}

void UltraCanvasPertChart::SetCriticalPathHighlight(bool enable) {
    highlightCriticalPath = enable;
    RequestRedraw();
}

void UltraCanvasPertChart::SetShowDates(bool show) {
    showDates = show;
    RequestRedraw();
}

void UltraCanvasPertChart::SetShowActivityCodes(bool show) {
    showActivityCodes = show;
    RequestRedraw();
}

void UltraCanvasPertChart::SetShowDurations(bool show) {
    showDurations = show;
    RequestRedraw();
}

void UltraCanvasPertChart::SetShowSlack(bool show) {
    showSlack = show;
    RequestRedraw();
}

void UltraCanvasPertChart::SetDurationUnit(const std::string& unitSuffix) {
    durationUnit = unitSuffix;
    RequestRedraw();
}

void UltraCanvasPertChart::SetFontFamily(const std::string& fontFamily) {
    style.fontFamily = fontFamily;
    RequestRedraw();
}

void UltraCanvasPertChart::SetBackgroundColor(const Color& color) {
    palette.backgroundColor = color;
    RequestRedraw();
}

void UltraCanvasPertChart::SetGridVisible(bool visible, double spacing) {
    style.showGrid = visible;
    if (spacing > 0) {
        style.gridSpacing = spacing;
    }
    RequestRedraw();
}

// =============================================================================
// PALETTE OPTIONS
// =============================================================================

void UltraCanvasPertChart::SetPalette(PertChartPaletteKind kind) {
    paletteKind = kind;
    palette = PertChartPalette::BuiltIn(kind);
    RequestRedraw();
}

void UltraCanvasPertChart::SetCustomPalette(const PertChartPalette& customPalette) {
    paletteKind = PertChartPaletteKind::Custom;
    palette = customPalette;
    RequestRedraw();
}

// =============================================================================
// LAYOUT & VIEW
// =============================================================================

double UltraCanvasPertChart::DefaultNodeWidth() const {
    switch (nodeDesign) {
        case PertNodeDesign::Card:         return 150.0;
        case PertNodeDesign::DetailedCard: return 160.0;
        case PertNodeDesign::Compact:      return 150.0;
        case PertNodeDesign::Circle:       return 56.0;
    }
    return 150.0;
}

double UltraCanvasPertChart::DefaultNodeHeight() const {
    switch (nodeDesign) {
        case PertNodeDesign::Card:         return 66.0;
        case PertNodeDesign::DetailedCard: return 84.0;
        case PertNodeDesign::Compact:      return 54.0;
        case PertNodeDesign::Circle:       return 56.0;
    }
    return 66.0;
}

double UltraCanvasPertChart::NodeWidth(const PertActivity& a) const {
    return (a.width > 0.0) ? a.width : DefaultNodeWidth();
}

double UltraCanvasPertChart::NodeHeight(const PertActivity& a) const {
    return (a.height > 0.0) ? a.height : DefaultNodeHeight();
}

Point2Dd UltraCanvasPertChart::NodeCenter(const PertActivity& a) const {
    return Point2Dd(a.x + NodeWidth(a) / 2.0, a.y + NodeHeight(a) / 2.0);
}

void UltraCanvasPertChart::AutoLayout(bool resetManualPositions) {
    layoutDirty = false;
    if (activities.empty()) return;
    if (scheduleDirty) {
        ComputeSchedule();
    }

    if (resetManualPositions) {
        for (auto& pair : activities) {
            pair.second.hasManualPosition = false;
        }
    }

    // Bucket activities per layer in topological order. When the graph is
    // cyclic (no topo order), fall back to insertion order in one column
    // of rows so the user still sees everything.
    std::vector<std::string> order = TopologicalOrder();
    std::vector<std::vector<std::string>> layers;
    if (!order.empty()) {
        for (const auto& id : order) {
            int layer = activities[id].layer;
            if (static_cast<int>(layers.size()) <= layer) {
                layers.resize(layer + 1);
            }
            layers[layer].push_back(id);
        }
    } else {
        layers.emplace_back();
        for (const auto& pair : activities) {
            layers[0].push_back(pair.first);
        }
    }

    std::unordered_map<std::string, std::vector<std::string>> preds;
    for (const auto& dep : dependencies) {
        preds[dep.toId].push_back(dep.fromId);
    }

    // Order every layer by the barycenter of its predecessors' vertical
    // ranks in the previous layer; keeps chains straight and reduces
    // connector crossings.
    std::unordered_map<std::string, double> rank;
    for (size_t li = 0; li < layers.size(); ++li) {
        auto& layer = layers[li];
        if (li > 0) {
            std::vector<std::pair<double, std::string>> keyed;
            keyed.reserve(layer.size());
            for (size_t i = 0; i < layer.size(); ++i) {
                double sum = 0.0;
                int count = 0;
                auto it = preds.find(layer[i]);
                if (it != preds.end()) {
                    for (const auto& p : it->second) {
                        auto rit = rank.find(p);
                        if (rit != rank.end()) {
                            sum += rit->second;
                            count++;
                        }
                    }
                }
                double key = (count > 0) ? sum / count : static_cast<double>(i);
                keyed.push_back({key, layer[i]});
            }
            std::stable_sort(keyed.begin(), keyed.end(),
                [](const std::pair<double, std::string>& a,
                   const std::pair<double, std::string>& b) {
                    return a.first < b.first;
                });
            for (size_t i = 0; i < keyed.size(); ++i) {
                layer[i] = keyed[i].second;
            }
        }
        for (size_t i = 0; i < layer.size(); ++i) {
            rank[layer[i]] = static_cast<double>(i);
        }
    }

    // Column positions and per-layer stacked heights.
    std::vector<double> layerX(layers.size(), style.marginX);
    std::vector<double> layerHeight(layers.size(), 0.0);
    double x = style.marginX;
    double maxLayerHeight = 0.0;
    for (size_t li = 0; li < layers.size(); ++li) {
        double maxW = 0.0;
        double totalH = 0.0;
        for (const auto& id : layers[li]) {
            const PertActivity& a = activities[id];
            maxW = std::max(maxW, NodeWidth(a));
            totalH += NodeHeight(a) + style.rowSpacing;
        }
        if (!layers[li].empty()) totalH -= style.rowSpacing;
        layerX[li] = x;
        layerHeight[li] = totalH;
        maxLayerHeight = std::max(maxLayerHeight, totalH);
        x += maxW + style.layerSpacing;
    }

    // Place nodes: each layer vertically centered against the tallest layer.
    for (size_t li = 0; li < layers.size(); ++li) {
        double y = style.marginY + (maxLayerHeight - layerHeight[li]) / 2.0;
        for (const auto& id : layers[li]) {
            PertActivity& a = activities[id];
            if (!a.hasManualPosition) {
                a.x = layerX[li];
                a.y = y;
            }
            y += NodeHeight(a) + style.rowSpacing;
        }
    }

    RequestRedraw();
}

void UltraCanvasPertChart::FitToView() {
    EnsureScheduleAndLayout();
    if (activities.empty()) return;

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    for (const auto& pair : activities) {
        const PertActivity& a = pair.second;
        minX = std::min(minX, a.x);
        minY = std::min(minY, a.y);
        maxX = std::max(maxX, a.x + NodeWidth(a));
        maxY = std::max(maxY, a.y + NodeHeight(a));
    }

    double margin = 30.0;
    double contentW = (maxX - minX) + margin * 2.0;
    double contentH = (maxY - minY) + margin * 2.0;
    double viewW = static_cast<double>(finalBounds.width);
    double viewH = static_cast<double>(finalBounds.height);
    if (contentW <= 0 || contentH <= 0 || viewW <= 0 || viewH <= 0) return;

    zoomLevel = std::clamp(std::min(viewW / contentW, viewH / contentH), 0.1, 1.5);
    panOffset.x = (viewW - (maxX - minX) * zoomLevel) / 2.0 - minX * zoomLevel;
    panOffset.y = (viewH - (maxY - minY) * zoomLevel) / 2.0 - minY * zoomLevel;
    RequestRedraw();
}

void UltraCanvasPertChart::SetZoomLevel(double zoom) {
    zoomLevel = std::clamp(zoom, 0.1, 10.0);
    RequestRedraw();
}

void UltraCanvasPertChart::SetPanOffset(double x, double y) {
    panOffset.x = x;
    panOffset.y = y;
    RequestRedraw();
}

// =============================================================================
// SELECTION
// =============================================================================

void UltraCanvasPertChart::SelectActivity(const std::string& id) {
    if (activities.find(id) == activities.end()) return;

    DeselectAll();
    selectedActivityId = id;
    activities[id].isSelected = true;

    if (onActivitySelected) {
        onActivitySelected(id);
    }
    RequestRedraw();
}

void UltraCanvasPertChart::DeselectAll() {
    for (auto& pair : activities) {
        pair.second.isSelected = false;
    }
    for (auto& dep : dependencies) {
        dep.isSelected = false;
    }
    selectedActivityId.clear();
    RequestRedraw();
}

// =============================================================================
// RENDERING
// =============================================================================

void UltraCanvasPertChart::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx || !IsVisible()) return;

    EnsureScheduleAndLayout();

    Rect2Df bounds = GetLocalBounds();
    ctx->SetFillPaint(palette.backgroundColor);
    ctx->FillRectangle(Rect2Dd(bounds.x, bounds.y, bounds.width, bounds.height));

    ctx->PushState();
    ctx->Translate(panOffset.x, panOffset.y);
    ctx->Scale(zoomLevel, zoomLevel);

    if (style.showGrid) {
        RenderGrid(ctx);
    }
    RenderDependencies(ctx);
    RenderActivities(ctx);

    ctx->PopState();

    if (showLegend && !groups.empty()) {
        RenderLegend(ctx);
    }
}

void UltraCanvasPertChart::RenderGrid(IRenderContext* ctx) {
    ctx->SetStrokePaint(palette.gridColor);
    ctx->SetStrokeWidth(0.5);

    double spacing = style.gridSpacing;
    double width = static_cast<double>(finalBounds.width) / zoomLevel;
    double height = static_cast<double>(finalBounds.height) / zoomLevel;

    double startX = -panOffset.x / zoomLevel;
    double startY = -panOffset.y / zoomLevel;
    startX = std::floor(startX / spacing) * spacing;
    startY = std::floor(startY / spacing) * spacing;

    for (double x = startX; x < startX + width + spacing; x += spacing) {
        ctx->DrawLine({x, startY}, {x, startY + height + spacing});
    }
    for (double y = startY; y < startY + height + spacing; y += spacing) {
        ctx->DrawLine({startX, y}, {startX + width + spacing, y});
    }
}

// -----------------------------------------------------------------------------
// Connectors
// -----------------------------------------------------------------------------

Point2Dd UltraCanvasPertChart::ConnectorPoint(const PertActivity& a,
                                              const Point2Dd& toward) const {
    Point2Dd c = NodeCenter(a);
    double dx = toward.x - c.x;
    double dy = toward.y - c.y;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-9) return c;

    if (nodeDesign == PertNodeDesign::Circle) {
        double r = std::min(NodeWidth(a), NodeHeight(a)) / 2.0;
        return Point2Dd(c.x + dx / len * r, c.y + dy / len * r);
    }

    // Intersection of the center->toward ray with the bounding box.
    double hw = NodeWidth(a) / 2.0;
    double hh = NodeHeight(a) / 2.0;
    double tx = (dx != 0.0) ? hw / std::fabs(dx) : std::numeric_limits<double>::max();
    double ty = (dy != 0.0) ? hh / std::fabs(dy) : std::numeric_limits<double>::max();
    double t = std::min(tx, ty);
    return Point2Dd(c.x + dx * t, c.y + dy * t);
}

std::vector<Point2Dd> UltraCanvasPertChart::ComputeConnectorPath(const Point2Dd& start,
                                                                 const Point2Dd& end) const {
    std::vector<Point2Dd> path;
    if (connectorStyle == PertConnectorStyle::Orthogonal &&
        std::fabs(start.y - end.y) > 2.0 && std::fabs(start.x - end.x) > 2.0) {
        double midX = (start.x + end.x) / 2.0;
        path = {start, {midX, start.y}, {midX, end.y}, end};
    } else {
        path = {start, end};
    }
    return path;
}

void UltraCanvasPertChart::RenderDependencies(IRenderContext* ctx) {
    for (const auto& dep : dependencies) {
        RenderDependency(ctx, dep);
    }
}

void UltraCanvasPertChart::RenderDependency(IRenderContext* ctx, const PertDependency& dep) {
    const auto* from = GetActivity(dep.fromId);
    const auto* to = GetActivity(dep.toId);
    if (!from || !to) return;

    Point2Dd fromCenter = NodeCenter(*from);
    Point2Dd toCenter = NodeCenter(*to);
    Point2Dd start = ConnectorPoint(*from, toCenter);
    Point2Dd end = ConnectorPoint(*to, fromCenter);

    bool critical = highlightCriticalPath && scheduleValid && !dep.isDummy &&
                    IsCriticalEdge(*from, *to);

    Color color = palette.connectorColor;
    if (dep.isDummy) color = palette.dummyConnectorColor;
    if (critical) color = palette.criticalConnectorColor;
    if (dep.hasCustomColor) color = dep.customColor;
    if (dep.isSelected) color = palette.selectionColor;

    double width = critical ? style.criticalConnectorWidth : style.connectorWidth;
    if (dep.isSelected) width += 0.7;

    ctx->PushState();
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(width);
    if (dep.isDummy) {
        ctx->SetLineDash(UCDashPattern({6.0, 4.0}));
    }

    Point2Dd labelAnchor((start.x + end.x) / 2.0, (start.y + end.y) / 2.0);
    double arrowAngle = std::atan2(end.y - start.y, end.x - start.x);

    if (connectorStyle == PertConnectorStyle::Curved) {
        double bend = (end.x - start.x) * 0.5;
        Point2Dd cp1(start.x + bend, start.y);
        Point2Dd cp2(end.x - bend, end.y);
        ctx->DrawBezierCurve(start, cp1, cp2, end);
        arrowAngle = std::atan2(end.y - cp2.y, end.x - cp2.x);
        if (std::fabs(arrowAngle) < 1e-9 && std::fabs(end.x - cp2.x) < 1e-9) {
            arrowAngle = std::atan2(end.y - start.y, end.x - start.x);
        }
    } else {
        std::vector<Point2Dd> path = ComputeConnectorPath(start, end);
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            ctx->DrawLine(path[i], path[i + 1]);
        }
        const Point2Dd& p0 = path[path.size() - 2];
        const Point2Dd& p1 = path.back();
        arrowAngle = std::atan2(p1.y - p0.y, p1.x - p0.x);

        // Anchor the label on the longest segment so it never sits on a corner.
        double best = -1.0;
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            double segLen = std::hypot(path[i + 1].x - path[i].x, path[i + 1].y - path[i].y);
            if (segLen > best) {
                best = segLen;
                labelAnchor = Point2Dd((path[i].x + path[i + 1].x) / 2.0,
                                       (path[i].y + path[i + 1].y) / 2.0);
            }
        }
    }

    if (dep.isDummy) {
        ctx->SetLineDash(UCDashPattern::EMPTY);
    }
    RenderArrowHead(ctx, end, arrowAngle, color);
    ctx->PopState();

    if (!dep.label.empty()) {
        RenderConnectorLabel(ctx, dep, labelAnchor);
    }
}

void UltraCanvasPertChart::RenderArrowHead(IRenderContext* ctx, const Point2Dd& tip,
                                           double angle, const Color& color) {
    double size = style.arrowSize;
    double spread = 0.42; // half-angle of the arrow head
    Point2Dd p1(tip.x - size * std::cos(angle - spread),
                tip.y - size * std::sin(angle - spread));
    Point2Dd p2(tip.x - size * std::cos(angle + spread),
                tip.y - size * std::sin(angle + spread));
    ctx->SetFillPaint(color);
    ctx->FillLinePath({tip, p1, p2});
}

void UltraCanvasPertChart::RenderConnectorLabel(IRenderContext* ctx,
                                                const PertDependency& dep,
                                                const Point2Dd& anchor) {
    ctx->PushState();
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.secondaryFontSize);
    Size2Di dim = ctx->GetTextLineDimensions(dep.label);

    double padX = 4.0, padY = 2.0;
    Rect2Dd bg(anchor.x - dim.width / 2.0 - padX, anchor.y - dim.height / 2.0 - padY,
               dim.width + padX * 2.0, dim.height + padY * 2.0);
    Color bgColor = palette.backgroundColor;
    bgColor.a = 225;
    ctx->SetFillPaint(bgColor);
    ctx->FillRoundedRectangle(bg, 3.0);

    ctx->SetTextPaint(palette.connectorLabelColor);
    ctx->DrawText(dep.label, {anchor.x - dim.width / 2.0, anchor.y - dim.height / 2.0});
    ctx->PopState();
}

// -----------------------------------------------------------------------------
// Nodes
// -----------------------------------------------------------------------------

void UltraCanvasPertChart::ResolveNodeColors(const PertActivity& a, Color& header,
                                             Color& fill, Color& border,
                                             Color& headerText, Color& bodyText) const {
    if (a.isMilestone) {
        header = palette.milestoneColor;
        fill = palette.milestoneColor;
        border = palette.milestoneColor;
        headerText = palette.milestoneTextColor;
        bodyText = palette.milestoneTextColor;
    } else {
        header = palette.nodeHeaderColor;
        fill = palette.nodeFillColor;
        border = palette.nodeBorderColor;
        headerText = palette.nodeHeaderTextColor;
        bodyText = palette.nodeTextColor;
        if (!a.groupId.empty() && GetGroup(a.groupId)) {
            Color gc = GetGroupColor(a.groupId);
            header = gc;
            border = gc;
        }
    }

    if (a.hasCustomColors) {
        header = a.customHeaderColor;
        fill = a.customFillColor;
        border = a.customBorderColor;
        bodyText = a.customTextColor;
    }

    if (highlightCriticalPath && scheduleValid && a.onCriticalPath && !a.hasCustomColors) {
        border = palette.criticalNodeAccentColor;
    }
}

void UltraCanvasPertChart::RenderActivities(IRenderContext* ctx) {
    for (const auto& pair : activities) {
        const PertActivity& a = pair.second;
        ctx->PushState();
        RenderActivity(ctx, a);
        ctx->PopState();
        if (a.isSelected) {
            RenderSelectionHighlight(ctx, a);
        }
    }
}

void UltraCanvasPertChart::RenderActivity(IRenderContext* ctx, const PertActivity& a) {
    switch (nodeDesign) {
        case PertNodeDesign::Card:
            RenderCardNode(ctx, a, false);
            break;
        case PertNodeDesign::DetailedCard:
            RenderCardNode(ctx, a, true);
            break;
        case PertNodeDesign::Compact:
            RenderCompactNode(ctx, a);
            break;
        case PertNodeDesign::Circle:
            RenderCircleNode(ctx, a);
            break;
    }

    if (showSlack && scheduleValid && !a.onCriticalPath && !a.isMilestone) {
        ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(style.secondaryFontSize);
        std::string slackText = "slack: " + FormatDuration(a.slack);
        Size2Di dim = ctx->GetTextLineDimensions(slackText);
        ctx->SetTextPaint(palette.nodeSecondaryTextColor);
        ctx->DrawText(slackText, {a.x + (NodeWidth(a) - dim.width) / 2.0,
                                  a.y + NodeHeight(a) + 3.0});
    }
}

void UltraCanvasPertChart::RenderCardNode(IRenderContext* ctx, const PertActivity& a,
                                          bool detailed) {
    double w = NodeWidth(a);
    double h = NodeHeight(a);
    Rect2Dd rect(a.x, a.y, w, h);
    double r = style.cornerRadius;

    Color header, fill, border, headerText, bodyText;
    ResolveNodeColors(a, header, fill, border, headerText, bodyText);

    if (a.isMilestone) {
        // Milestones are a single solid bar with the name centered.
        ctx->DrawFilledRectangle(rect, fill, style.borderWidth, border, r);
        ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
        ctx->SetFontSize(style.headerFontSize);
        Size2Di dim = ctx->GetTextLineDimensions(a.name);
        ctx->SetTextPaint(headerText);
        ctx->DrawText(a.name, {a.x + (w - dim.width) / 2.0, a.y + (h - dim.height) / 2.0});
        return;
    }

    int rowCount = 1 + (showDates ? 1 : 0) + (detailed ? 1 : 0);
    double headerH = std::clamp(h * 0.34, 20.0, 30.0);
    double rowH = (h - headerH) / rowCount;

    ctx->PushState();
    ctx->ClipRoundedRectangle(rect, r, r, r, r);

    ctx->SetFillPaint(fill);
    ctx->FillRectangle(rect);
    ctx->SetFillPaint(header);
    ctx->FillRectangle(Rect2Dd(a.x, a.y, w, headerH));

    // Row separators + vertical cell divider for the code/duration and
    // dates rows.
    ctx->SetStrokePaint(palette.nodeDividerColor);
    ctx->SetStrokeWidth(1.0);
    for (int i = 1; i < rowCount; ++i) {
        double yy = a.y + headerH + rowH * i;
        ctx->DrawLine({a.x, yy}, {a.x + w, yy});
    }
    double midX = a.x + w / 2.0;
    double dividedRows = showDates ? 2.0 : 1.0; // code/dur row + optional dates row
    ctx->DrawLine({midX, a.y + headerH}, {midX, a.y + headerH + rowH * dividedRows});

    // Header: activity name.
    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(style.headerFontSize);
    Size2Di nameDim = ctx->GetTextLineDimensions(a.name);
    ctx->SetTextPaint(headerText);
    ctx->DrawText(a.name, {a.x + (w - nameDim.width) / 2.0,
                           a.y + (headerH - nameDim.height) / 2.0});

    // Row 1: code | duration.
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.baseFontSize);
    ctx->SetTextPaint(bodyText);
    double row1Y = a.y + headerH;
    std::string codeText = showActivityCodes ? (a.code.empty() ? a.id : a.code) : "";
    if (!codeText.empty()) {
        Size2Di dim = ctx->GetTextLineDimensions(codeText);
        ctx->DrawText(codeText, {a.x + (w / 2.0 - dim.width) / 2.0,
                                 row1Y + (rowH - dim.height) / 2.0});
    }
    if (showDurations) {
        std::string durText = FormatDuration(GetExpectedDuration(a.id));
        Size2Di dim = ctx->GetTextLineDimensions(durText);
        ctx->DrawText(durText, {midX + (w / 2.0 - dim.width) / 2.0,
                                row1Y + (rowH - dim.height) / 2.0});
    }

    // Row 2: start date | end date.
    ctx->SetFontSize(style.secondaryFontSize);
    ctx->SetTextPaint(palette.nodeSecondaryTextColor);
    if (showDates) {
        double row2Y = row1Y + rowH;
        if (!a.startDate.empty()) {
            Size2Di dim = ctx->GetTextLineDimensions(a.startDate);
            ctx->DrawText(a.startDate, {a.x + (w / 2.0 - dim.width) / 2.0,
                                        row2Y + (rowH - dim.height) / 2.0});
        }
        if (!a.endDate.empty()) {
            Size2Di dim = ctx->GetTextLineDimensions(a.endDate);
            ctx->DrawText(a.endDate, {midX + (w / 2.0 - dim.width) / 2.0,
                                      row2Y + (rowH - dim.height) / 2.0});
        }
    }

    // Row 3 (DetailedCard): responsible.
    if (detailed) {
        double row3Y = a.y + headerH + rowH * (rowCount - 1);
        std::string who = a.responsible.empty() ? "-" : a.responsible;
        Size2Di dim = ctx->GetTextLineDimensions(who);
        ctx->DrawText(who, {a.x + (w - dim.width) / 2.0,
                            row3Y + (rowH - dim.height) / 2.0});
    }

    ctx->PopState();

    bool critical = highlightCriticalPath && scheduleValid && a.onCriticalPath;
    ctx->SetStrokePaint(border);
    ctx->SetStrokeWidth(critical ? style.borderWidth + 0.8 : style.borderWidth);
    ctx->DrawRoundedRectangle(rect, r);
}

void UltraCanvasPertChart::RenderCompactNode(IRenderContext* ctx, const PertActivity& a) {
    double w = NodeWidth(a);
    double h = NodeHeight(a);
    Rect2Dd rect(a.x, a.y, w, h);
    double r = style.cornerRadius;

    Color header, fill, border, headerText, bodyText;
    ResolveNodeColors(a, header, fill, border, headerText, bodyText);

    // Compact boxes are solid: the header color IS the box color (dark box,
    // light text), matching the minimalist PERT presentation.
    Color boxColor = a.hasCustomColors ? a.customFillColor : header;
    Color textColor = a.isMilestone ? palette.milestoneTextColor
                                    : (a.hasCustomColors ? a.customTextColor
                                                         : palette.nodeHeaderTextColor);
    ctx->DrawFilledRectangle(rect, boxColor, style.borderWidth,
                             (highlightCriticalPath && scheduleValid && a.onCriticalPath)
                                 ? palette.criticalNodeAccentColor : boxColor,
                             r);

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(style.baseFontSize);
    Size2Di nameDim = ctx->GetTextLineDimensions(a.name);
    bool withDuration = showDurations && !a.isMilestone;
    double nameY = withDuration ? a.y + h / 2.0 - nameDim.height - 1.0
                                : a.y + (h - nameDim.height) / 2.0;
    ctx->SetTextPaint(textColor);
    ctx->DrawText(a.name, {a.x + (w - nameDim.width) / 2.0, nameY});

    if (withDuration) {
        std::string durText = "(" + FormatDuration(GetExpectedDuration(a.id)) + ")";
        ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(style.secondaryFontSize);
        Size2Di durDim = ctx->GetTextLineDimensions(durText);
        ctx->DrawText(durText, {a.x + (w - durDim.width) / 2.0, a.y + h / 2.0 + 2.0});
    }
}

void UltraCanvasPertChart::RenderCircleNode(IRenderContext* ctx, const PertActivity& a) {
    double w = NodeWidth(a);
    double h = NodeHeight(a);
    Point2Dd c = NodeCenter(a);
    double radius = std::min(w, h) / 2.0;

    Color header, fill, border, headerText, bodyText;
    ResolveNodeColors(a, header, fill, border, headerText, bodyText);
    Color circleColor = a.hasCustomColors ? a.customFillColor : header;

    ctx->SetFillPaint(circleColor);
    ctx->FillCircle(c, radius);
    if (highlightCriticalPath && scheduleValid && a.onCriticalPath) {
        ctx->SetStrokePaint(palette.criticalNodeAccentColor);
        ctx->SetStrokeWidth(style.borderWidth + 0.8);
        ctx->DrawCircle(c, radius);
    }

    // Inside the circle: the activity code (or the id as fallback).
    std::string codeText = showActivityCodes ? (a.code.empty() ? a.id : a.code) : "";
    if (!codeText.empty()) {
        ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
        ctx->SetFontSize(style.headerFontSize);
        Size2Di dim = ctx->GetTextLineDimensions(codeText);
        ctx->SetTextPaint(headerText);
        ctx->DrawText(codeText, {c.x - dim.width / 2.0, c.y - dim.height / 2.0});
    }

    // Above the circle: name; below: duration (AOA-style annotations).
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.baseFontSize);
    ctx->SetTextPaint(palette.nodeTextColor);
    if (!a.name.empty()) {
        Size2Di dim = ctx->GetTextLineDimensions(a.name);
        ctx->DrawText(a.name, {c.x - dim.width / 2.0, a.y - dim.height - 4.0});
    }
    if (showDurations && !a.isMilestone) {
        std::string durText = FormatDuration(GetExpectedDuration(a.id));
        ctx->SetFontSize(style.secondaryFontSize);
        ctx->SetTextPaint(palette.nodeSecondaryTextColor);
        Size2Di dim = ctx->GetTextLineDimensions(durText);
        ctx->DrawText(durText, {c.x - dim.width / 2.0, a.y + h + 4.0});
    }
}

void UltraCanvasPertChart::RenderSelectionHighlight(IRenderContext* ctx, const PertActivity& a) {
    double pad = 4.0;
    ctx->PushState();
    ctx->SetStrokePaint(palette.selectionColor);
    ctx->SetStrokeWidth(style.selectionWidth);
    if (nodeDesign == PertNodeDesign::Circle) {
        double radius = std::min(NodeWidth(a), NodeHeight(a)) / 2.0 + pad;
        ctx->DrawCircle(NodeCenter(a), radius);
    } else {
        ctx->DrawRoundedRectangle(Rect2Dd(a.x - pad, a.y - pad,
                                          NodeWidth(a) + pad * 2.0,
                                          NodeHeight(a) + pad * 2.0),
                                  style.cornerRadius + pad / 2.0);
    }
    ctx->PopState();
}

void UltraCanvasPertChart::RenderLegend(IRenderContext* ctx) {
    Rect2Df bounds = GetLocalBounds();

    ctx->PushState();
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.baseFontSize);

    double swatchW = 26.0, swatchH = 12.0, gap = 8.0, lineH = 22.0, pad = 12.0;
    double maxTextW = 0.0;
    for (const auto& g : groups) {
        Size2Di dim = ctx->GetTextLineDimensions(g.name);
        maxTextW = std::max(maxTextW, static_cast<double>(dim.width));
    }
    double boxW = swatchW + gap + maxTextW + pad * 2.0;
    double boxH = groups.size() * lineH + pad * 2.0 - (lineH - swatchH);
    double boxX = bounds.width - boxW - 14.0;
    double boxY = bounds.height - boxH - 14.0;

    Color legendBg = palette.backgroundColor;
    legendBg.a = 235;
    ctx->SetFillPaint(legendBg);
    ctx->FillRoundedRectangle(Rect2Dd(boxX, boxY, boxW, boxH), 6.0);
    ctx->SetStrokePaint(palette.nodeDividerColor);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRoundedRectangle(Rect2Dd(boxX, boxY, boxW, boxH), 6.0);

    double y = boxY + pad;
    for (size_t i = 0; i < groups.size(); ++i) {
        Color gc = GetGroupColor(groups[i].id);
        ctx->SetFillPaint(gc);
        ctx->FillRoundedRectangle(Rect2Dd(boxX + pad, y, swatchW, swatchH), 5.0);

        Size2Di dim = ctx->GetTextLineDimensions(groups[i].name);
        ctx->SetTextPaint(palette.legendTextColor);
        ctx->DrawText(groups[i].name,
                      {boxX + pad + swatchW + gap, y + (swatchH - dim.height) / 2.0});
        y += lineH;
    }
    ctx->PopState();
}

std::string UltraCanvasPertChart::FormatDuration(double d) const {
    std::ostringstream oss;
    double rounded = std::round(d * 10.0) / 10.0;
    if (std::fabs(rounded - std::round(rounded)) < 1e-9) {
        oss << static_cast<long long>(std::llround(rounded));
    } else {
        oss.precision(1);
        oss << std::fixed << rounded;
    }
    if (!durationUnit.empty()) {
        oss << " " << durationUnit;
    }
    return oss.str();
}

// =============================================================================
// HIT TESTING
// =============================================================================

Point2Dd UltraCanvasPertChart::ScreenToWorld(const Point2Di& screenPos) const {
    return Point2Dd((screenPos.x - panOffset.x) / zoomLevel,
                    (screenPos.y - panOffset.y) / zoomLevel);
}

bool UltraCanvasPertChart::PointInActivity(const PertActivity& a, const Point2Dd& p) const {
    if (nodeDesign == PertNodeDesign::Circle) {
        Point2Dd c = NodeCenter(a);
        double r = std::min(NodeWidth(a), NodeHeight(a)) / 2.0;
        double dx = p.x - c.x, dy = p.y - c.y;
        return dx * dx + dy * dy <= r * r;
    }
    return p.x >= a.x && p.x <= a.x + NodeWidth(a) &&
           p.y >= a.y && p.y <= a.y + NodeHeight(a);
}

std::string UltraCanvasPertChart::FindActivityAt(const Point2Dd& worldPos) const {
    for (const auto& pair : activities) {
        if (PointInActivity(pair.second, worldPos)) {
            return pair.first;
        }
    }
    return "";
}

double UltraCanvasPertChart::DistanceToSegment(const Point2Dd& p, const Point2Dd& a,
                                               const Point2Dd& b) const {
    double dx = b.x - a.x, dy = b.y - a.y;
    double lenSq = dx * dx + dy * dy;
    if (lenSq < 1e-12) return std::hypot(p.x - a.x, p.y - a.y);
    double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lenSq, 0.0, 1.0);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

std::string UltraCanvasPertChart::FindDependencyAt(const Point2Dd& worldPos) const {
    double tolerance = 6.0 / zoomLevel;
    for (const auto& dep : dependencies) {
        const auto* from = GetActivity(dep.fromId);
        const auto* to = GetActivity(dep.toId);
        if (!from || !to) continue;
        Point2Dd start = ConnectorPoint(*from, NodeCenter(*to));
        Point2Dd end = ConnectorPoint(*to, NodeCenter(*from));
        std::vector<Point2Dd> path = ComputeConnectorPath(start, end);
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            if (DistanceToSegment(worldPos, path[i], path[i + 1]) <= tolerance) {
                return dep.id;
            }
        }
    }
    return "";
}

// =============================================================================
// EVENT HANDLING
// =============================================================================

bool UltraCanvasPertChart::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseDown:
            return HandleMouseDown(event);
        case UCEventType::MouseUp:
            return HandleMouseUp(event);
        case UCEventType::MouseMove:
            return HandleMouseMove(event);
        case UCEventType::MouseWheel:
            return HandleMouseWheel(event);
        case UCEventType::MouseDoubleClick:
            return HandleDoubleClick(event);
        default:
            break;
    }
    return UltraCanvasUIElement::OnEvent(event);
}

bool UltraCanvasPertChart::HandleMouseDown(const UCEvent& event) {
    Point2Dd world = ScreenToWorld(event.pointer);

    std::string activityId = FindActivityAt(world);
    if (!activityId.empty()) {
        SelectActivity(activityId);
        const PertActivity& a = activities[activityId];
        isDraggingNode = true;
        dragOffsetX = world.x - a.x;
        dragOffsetY = world.y - a.y;
        if (onActivityClick) {
            onActivityClick(activityId);
        }
        return true;
    }

    std::string depId = FindDependencyAt(world);
    if (!depId.empty()) {
        DeselectAll();
        auto* dep = GetDependency(depId);
        if (dep) dep->isSelected = true;
        if (onDependencyClick) {
            onDependencyClick(depId);
        }
        RequestRedraw();
        return true;
    }

    DeselectAll();
    isPanning = true;
    panStartPos = event.pointer;
    panStartOffset = panOffset;
    return true;
}

bool UltraCanvasPertChart::HandleMouseUp(const UCEvent& event) {
    isDraggingNode = false;
    isPanning = false;
    return true;
}

bool UltraCanvasPertChart::HandleMouseMove(const UCEvent& event) {
    Point2Dd world = ScreenToWorld(event.pointer);

    if (isDraggingNode && !selectedActivityId.empty()) {
        auto* a = GetActivity(selectedActivityId);
        if (a) {
            a->x = world.x - dragOffsetX;
            a->y = world.y - dragOffsetY;
            a->hasManualPosition = true;
            if (onActivityDragged) {
                onActivityDragged(selectedActivityId, a->x, a->y);
            }
            RequestRedraw();
        }
        return true;
    }

    if (isPanning) {
        panOffset.x = panStartOffset.x + (event.pointer.x - panStartPos.x);
        panOffset.y = panStartOffset.y + (event.pointer.y - panStartPos.y);
        RequestRedraw();
        return true;
    }

    std::string hovered = FindActivityAt(world);
    if (hovered != hoveredActivityId) {
        if (!hoveredActivityId.empty()) {
            auto* prev = GetActivity(hoveredActivityId);
            if (prev) prev->isHovered = false;
        }
        hoveredActivityId = hovered;
        if (!hovered.empty()) {
            activities[hovered].isHovered = true;
        }
        RequestRedraw();
    }
    return !hovered.empty();
}

bool UltraCanvasPertChart::HandleMouseWheel(const UCEvent& event) {
    double zoomFactor = (event.wheelDelta > 0) ? 1.1 : 0.9;
    Point2Di mousePos = event.pointer;
    Point2Dd worldBefore = ScreenToWorld(mousePos);

    double newZoom = std::clamp(zoomLevel * zoomFactor, 0.1, 10.0);
    if (newZoom != zoomLevel) {
        zoomLevel = newZoom;
        Point2Dd worldAfter = ScreenToWorld(mousePos);
        panOffset.x += (worldAfter.x - worldBefore.x) * zoomLevel;
        panOffset.y += (worldAfter.y - worldBefore.y) * zoomLevel;
        RequestRedraw();
    }
    return true;
}

bool UltraCanvasPertChart::HandleDoubleClick(const UCEvent& event) {
    Point2Dd world = ScreenToWorld(event.pointer);
    std::string activityId = FindActivityAt(world);
    if (!activityId.empty() && onActivityDoubleClick) {
        onActivityDoubleClick(activityId);
        return true;
    }
    return false;
}

} // namespace UltraCanvas
