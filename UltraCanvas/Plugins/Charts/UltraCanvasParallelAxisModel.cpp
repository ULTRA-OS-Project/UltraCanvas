// Plugins/Charts/UltraCanvasParallelAxisModel.cpp
// Parallel coordinate data model
// Version: 1.0.0
// Last Modified: 2026-08-06
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasParallelAxisModel.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace UltraCanvas {

namespace {
const double kNaN = std::numeric_limits<double>::quiet_NaN();
}

// =============================================================================
// DATA
// =============================================================================

void ParallelAxisModel::EnsureDimensionState() {
    visibleFlags.resize(dimensionNames.size(), true);
    invertedFlags.resize(dimensionNames.size(), false);
    while (order.size() < dimensionNames.size()) order.push_back(order.size());
}

size_t ParallelAxisModel::AddDimension(const std::string& name) {
    dimensionNames.push_back(name);
    EnsureDimensionState();
    for (PCPRecord& record : records) record.values.resize(dimensionNames.size(), kNaN);
    cache.clear();
    return dimensionNames.size() - 1;
}

void ParallelAxisModel::AddDimensionColumn(const std::string& name,
                                           const std::vector<double>& column) {
    const size_t dim = AddDimension(name);
    if (records.size() < column.size()) {
        const size_t oldCount = records.size();
        records.resize(column.size());
        for (size_t i = oldCount; i < records.size(); ++i) {
            records[i].values.resize(dimensionNames.size(), kNaN);
        }
    }
    for (size_t i = 0; i < records.size(); ++i) {
        records[i].values.resize(dimensionNames.size(), kNaN);
        records[i].values[dim] = (i < column.size()) ? column[i] : kNaN;
    }
    cache.clear();
}

void ParallelAxisModel::SetRecords(std::vector<PCPRecord> newRecords) {
    records = std::move(newRecords);
    for (PCPRecord& record : records) record.values.resize(dimensionNames.size(), kNaN);
    cache.clear();
}

void ParallelAxisModel::SetRecordGroups(const std::vector<std::string>& groups) {
    for (size_t i = 0; i < records.size() && i < groups.size(); ++i) {
        records[i].group = groups[i];
    }
}

void ParallelAxisModel::Clear() {
    dimensionNames.clear();
    records.clear();
    visibleFlags.clear();
    invertedFlags.clear();
    order.clear();
    brushes.clear();
    cache.clear();
    cachedSlots = 0;
}

// =============================================================================
// ORDER AND PER-DIMENSION STATE
// =============================================================================

std::vector<size_t> ParallelAxisModel::DisplayOrder() const {
    std::vector<size_t> visible;
    for (size_t dim : order) {
        if (dim < visibleFlags.size() && visibleFlags[dim]) visible.push_back(dim);
    }
    return visible;
}

void ParallelAxisModel::MoveAxis(size_t fromDisplayIndex, size_t toDisplayIndex) {
    // Display indices address the *visible* sequence; translate them onto the
    // full order so hidden dimensions keep their place.
    std::vector<size_t> visiblePositions;
    for (size_t i = 0; i < order.size(); ++i) {
        if (visibleFlags[order[i]]) visiblePositions.push_back(i);
    }
    if (fromDisplayIndex >= visiblePositions.size() ||
        toDisplayIndex >= visiblePositions.size() ||
        fromDisplayIndex == toDisplayIndex) return;

    const size_t from = visiblePositions[fromDisplayIndex];
    const size_t to = visiblePositions[toDisplayIndex];
    const size_t moved = order[from];
    order.erase(order.begin() + static_cast<long>(from));
    order.insert(order.begin() + static_cast<long>(to), moved);
    cache.clear();
}

void ParallelAxisModel::SetAxisVisible(size_t dim, bool visible) {
    if (dim >= visibleFlags.size()) return;
    visibleFlags[dim] = visible;
    cache.clear();
}

void ParallelAxisModel::SetAxisInverted(size_t dim, bool inverted) {
    if (dim >= invertedFlags.size()) return;
    invertedFlags[dim] = inverted;
    cache.clear();
}

double ParallelAxisModel::AxisU(size_t displayIndex) const {
    const size_t n = DisplayOrder().size();
    if (n <= 1) return 0.5;
    return static_cast<double>(displayIndex) / static_cast<double>(n - 1);
}

// =============================================================================
// NORMALISATION
// =============================================================================

void ParallelAxisModel::SetNormalization(PCPNormalization mode) {
    normalization = mode;
    cache.clear();
}

void ParallelAxisModel::SetStandardize(PCPStandardize mode) {
    standardize = mode;
    cache.clear();
}

void ParallelAxisModel::ColumnStats(size_t dim, double& mean, double& stddev) const {
    double sum = 0.0;
    size_t count = 0;
    for (const PCPRecord& record : records) {
        const double v = record.values[dim];
        if (std::isfinite(v)) { sum += v; ++count; }
    }
    mean = count ? sum / static_cast<double>(count) : 0.0;
    double variance = 0.0;
    for (const PCPRecord& record : records) {
        const double v = record.values[dim];
        if (std::isfinite(v)) variance += (v - mean) * (v - mean);
    }
    stddev = count ? std::sqrt(variance / static_cast<double>(count)) : 1.0;
    if (stddev < 1e-12) stddev = 1.0;      // constant column
}

void ParallelAxisModel::SharedRange(double& low, double& high) const {
    low = std::numeric_limits<double>::infinity();
    high = -std::numeric_limits<double>::infinity();
    for (size_t dim : DisplayOrder()) {
        double mean = 0.0, stddev = 1.0;
        if (standardize == PCPStandardize::ZScore) ColumnStats(dim, mean, stddev);
        for (const PCPRecord& record : records) {
            double v = record.values[dim];
            if (!std::isfinite(v)) continue;
            if (standardize == PCPStandardize::ZScore) v = (v - mean) / stddev;
            low = std::min(low, v);
            high = std::max(high, v);
        }
    }
    if (!(high > low)) { low = 0.0; high = 1.0; }
}

void ParallelAxisModel::ConfigureAxes(ChartAxisSet& axes) const {
    const std::vector<size_t> display = DisplayOrder();

    double sharedLow = 0.0, sharedHigh = 1.0;
    if (normalization == PCPNormalization::CommonScale) SharedRange(sharedLow, sharedHigh);

    for (size_t k = 0; k < display.size(); ++k) {
        const size_t dim = display[k];
        ChartAxis axis(dimensionNames[dim]);
        axis.title = dimensionNames[dim];
        axis.inPlot = true;
        axis.plotPosition = AxisU(k);
        axis.inverted = invertedFlags[dim];
        // Exactly one value-labelling system per axis. Per-axis normalisation
        // gets the integrated axis (tick labels along the rule); the common
        // scale reads from the shared edge axis, so its in-plot axes carry
        // only the original-unit min/max at the ends.
        axis.showTickLabels = (normalization == PCPNormalization::PerAxis);
        axis.showEndpointLabels = (normalization == PCPNormalization::CommonScale);

        if (normalization == PCPNormalization::CommonScale) {
            // Every axis maps the same (standardised) band onto [0,1]. For a
            // z-scored column the shared band is translated back into the
            // column's own units, so Normalize stays a plain linear map and
            // the shared edge axis shows the band itself.
            if (standardize == PCPStandardize::ZScore) {
                double mean = 0.0, stddev = 1.0;
                ColumnStats(dim, mean, stddev);
                axis.SetRange(mean + sharedLow * stddev, mean + sharedHigh * stddev);
            } else {
                axis.SetRange(sharedLow, sharedHigh);
            }
        } else {
            for (const PCPRecord& record : records) axis.Observe(record.values[dim]);
        }
        axes.Add(axis);
    }
}

void ParallelAxisModel::BuildCache(ChartAxisSet& axes) {
    axes.FinalizeAll();
    const std::vector<size_t> display = DisplayOrder();
    cachedSlots = display.size();
    cache.assign(records.size() * cachedSlots, kNaN);

    for (size_t r = 0; r < records.size(); ++r) {
        for (size_t k = 0; k < display.size() && k < axes.Count(); ++k) {
            const double v = records[r].values[display[k]];
            if (!std::isfinite(v)) continue;
            cache[r * cachedSlots + k] = axes.At(k).Normalize(v);
        }
    }
}

double ParallelAxisModel::NormalizedValue(size_t record, size_t displayIndex) const {
    if (record >= records.size() || displayIndex >= cachedSlots) return kNaN;
    return cache[record * cachedSlots + displayIndex];
}

bool ParallelAxisModel::HasValue(size_t record, size_t displayIndex) const {
    return std::isfinite(NormalizedValue(record, displayIndex));
}

// =============================================================================
// BRUSHES
// =============================================================================

void ParallelAxisModel::AddBrush(size_t dim, double low, double high) {
    if (dim >= dimensionNames.size()) return;
    PCPBrush brush;
    brush.dimension = dim;
    brush.low = std::min(low, high);
    brush.high = std::max(low, high);
    brushes.push_back(brush);
}

void ParallelAxisModel::ClearBrushes() { brushes.clear(); }

void ParallelAxisModel::ClearBrushes(size_t dim) {
    brushes.erase(std::remove_if(brushes.begin(), brushes.end(),
                                 [dim](const PCPBrush& b) { return b.dimension == dim; }),
                  brushes.end());
}

bool ParallelAxisModel::RecordPasses(size_t record) const {
    if (record >= records.size()) return false;
    if (brushes.empty()) return true;

    // AND across dimensions, OR within one: collect the brushed dimensions,
    // then require a satisfied brush on each.
    for (const PCPBrush& brush : brushes) {
        bool dimensionSatisfied = false;
        bool checkedThisDimension = false;
        for (const PCPBrush& other : brushes) {
            if (other.dimension != brush.dimension) continue;
            checkedThisDimension = true;
            const double v = records[record].values[other.dimension];
            if (std::isfinite(v) && v >= other.low && v <= other.high) {
                dimensionSatisfied = true;
                break;
            }
        }
        if (checkedThisDimension && !dimensionSatisfied) return false;
    }
    return true;
}

size_t ParallelAxisModel::PassingCount() const {
    size_t count = 0;
    for (size_t r = 0; r < records.size(); ++r) {
        if (RecordPasses(r)) ++count;
    }
    return count;
}

// =============================================================================
// GROUPS
// =============================================================================

std::vector<std::string> ParallelAxisModel::Groups() const {
    std::vector<std::string> groups;
    for (const PCPRecord& record : records) {
        if (record.group.empty()) continue;
        if (std::find(groups.begin(), groups.end(), record.group) == groups.end()) {
            groups.push_back(record.group);
        }
    }
    return groups;
}

// =============================================================================
// HIT TESTING
// =============================================================================

int ParallelAxisModel::NearestRecord(double u, double v, double maxDistance) const {
    const size_t slots = cachedSlots;
    if (slots < 2 || records.empty()) return -1;

    // The segment span the query point falls into.
    const double span = 1.0 / static_cast<double>(slots - 1);
    const double slot = u / span;
    const long k = std::clamp(static_cast<long>(std::floor(slot)),
                              0L, static_cast<long>(slots) - 2L);
    const double u0 = static_cast<double>(k) * span;
    const double u1 = u0 + span;

    double best = maxDistance;
    int bestRecord = -1;
    for (size_t r = 0; r < records.size(); ++r) {
        const double v0 = NormalizedValue(r, static_cast<size_t>(k));
        const double v1 = NormalizedValue(r, static_cast<size_t>(k) + 1);
        if (!std::isfinite(v0) || !std::isfinite(v1)) continue;   // missing = broken line

        // Point-to-segment distance for (u0,v0)-(u1,v1).
        const double dx = u1 - u0, dy = v1 - v0;
        const double lengthSq = dx * dx + dy * dy;
        double t = lengthSq > 0.0 ? ((u - u0) * dx + (v - v0) * dy) / lengthSq : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const double px = u0 + t * dx - u;
        const double py = v0 + t * dy - v;
        const double distance = std::sqrt(px * px + py * py);
        if (distance < best) {
            best = distance;
            bestRecord = static_cast<int>(r);
        }
    }
    return bestRecord;
}

} // namespace UltraCanvas
