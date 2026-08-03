// include/Plugins/Charts/UltraCanvasParallelAxisModel.h
// Data model for the parallel coordinate chart: records, dimensions, display
// order, normalisation (per-axis or common-scale, optionally standardised),
// brushes and nearest-line hit testing - all in normalised chart space, free
// of UI dependencies, so it is unit-testable without a window.
//
// The element (UltraCanvasParallelCoordinateChart) owns one of these and maps
// its normalised output through the engine projection.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartAxis.h"
#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// DATA TYPES
// =============================================================================

struct PCPRecord {
    std::vector<double> values;      // one per dimension; NaN = missing
    std::string label;               // tooltip / pin caption
    std::string group;               // categorical hue key
    double weight = 1.0;
    Color colorOverride = Color(0, 0, 0, 0);   // alpha 0 = use the colour rules
    uint64_t id = 0;                 // stable id for selection; 0 = index-based
};

// How columns map onto their axes.
enum class PCPNormalization {
    PerAxis,       // each axis spans its own min..max (the classic look)
    CommonScale    // all axes share one value range and one shared value axis
};

// Optional per-column standardisation applied before the common scale, so
// columns with different units become comparable (the Iris z-score look).
enum class PCPStandardize { Off, ZScore };   // Off, not None: X11 #defines None

// A range filter on one dimension, in data space. A record passes the brush
// set when it satisfies every brushed dimension (AND across dimensions) and
// any brush on a given dimension (OR within a dimension).
struct PCPBrush {
    size_t dimension = 0;
    double low = 0.0, high = 0.0;
};

// =============================================================================
// MODEL
// =============================================================================

class ParallelAxisModel {
public:
    // ---- data -----------------------------------------------------------
    size_t AddDimension(const std::string& name);
    // Column-oriented input: appends/extends records with one column of values.
    void AddDimensionColumn(const std::string& name, const std::vector<double>& column);
    void SetRecords(std::vector<PCPRecord> newRecords);
    void SetRecordGroups(const std::vector<std::string>& groups);
    void Clear();

    size_t DimensionCount() const { return dimensionNames.size(); }
    size_t RecordCount() const { return records.size(); }
    const std::string& DimensionName(size_t dim) const { return dimensionNames[dim]; }
    const PCPRecord& Record(size_t index) const { return records[index]; }
    PCPRecord& Record(size_t index) { return records[index]; }

    // ---- display order and per-dimension state --------------------------
    // Visible dimensions in display order (indices into the dimension list).
    std::vector<size_t> DisplayOrder() const;
    void MoveAxis(size_t fromDisplayIndex, size_t toDisplayIndex);
    void SetAxisVisible(size_t dim, bool visible);
    bool IsAxisVisible(size_t dim) const { return dim < visibleFlags.size() && visibleFlags[dim]; }
    void SetAxisInverted(size_t dim, bool inverted);
    bool IsAxisInverted(size_t dim) const { return dim < invertedFlags.size() && invertedFlags[dim]; }

    // Normalised domain position of a display slot: k / (n-1), 0.5 for n == 1.
    double AxisU(size_t displayIndex) const;

    // ---- normalisation ---------------------------------------------------
    void SetNormalization(PCPNormalization mode);
    PCPNormalization Normalization() const { return normalization; }
    void SetStandardize(PCPStandardize mode);

    // Builds one ChartAxis per visible dimension (in display order), each
    // flagged inPlot at its AxisU position, honouring the normalisation and
    // standardisation modes. CommonScale gives every axis a range that maps
    // the same (possibly standardised) value band onto [0,1].
    void ConfigureAxes(ChartAxisSet& axes) const;

    // The shared value axis range for CommonScale (what the engine's edge
    // axis displays). Meaningless under PerAxis.
    void SharedRange(double& low, double& high) const;

    // Rebuilds the normalised cache from the axes ConfigureAxes produced.
    // Call after data, order, visibility, inversion or normalisation changes.
    // Finalizes the axes first, so a caller cannot accidentally normalise
    // against unresolved ranges.
    void BuildCache(ChartAxisSet& axes);

    // Normalised value of a record at a display slot; NaN when missing.
    double NormalizedValue(size_t record, size_t displayIndex) const;
    bool HasValue(size_t record, size_t displayIndex) const;

    // ---- brushes ---------------------------------------------------------
    void AddBrush(size_t dim, double low, double high);
    void ClearBrushes();
    void ClearBrushes(size_t dim);
    const std::vector<PCPBrush>& Brushes() const { return brushes; }
    bool HasBrushes() const { return !brushes.empty(); }
    bool RecordPasses(size_t record) const;
    size_t PassingCount() const;

    // ---- groups ----------------------------------------------------------
    // Distinct group names in first-seen order (the legend order).
    std::vector<std::string> Groups() const;

    // ---- hit testing -----------------------------------------------------
    // Nearest record polyline to (u, v) in normalised space, restricted to the
    // segment span the point falls into. Returns -1 when nothing is within
    // maxDistance (normalised units). Missing values break the line.
    int NearestRecord(double u, double v, double maxDistance) const;

private:
    std::vector<std::string> dimensionNames;
    std::vector<PCPRecord> records;
    std::vector<bool> visibleFlags;
    std::vector<bool> invertedFlags;
    std::vector<size_t> order;                 // all dimensions, display order
    PCPNormalization normalization = PCPNormalization::PerAxis;
    PCPStandardize standardize = PCPStandardize::Off;
    std::vector<PCPBrush> brushes;

    // Cache: records x visible display slots, row-major; NaN = missing.
    std::vector<double> cache;
    size_t cachedSlots = 0;

    void EnsureDimensionState();
    // Per-column mean/stddev for the standardised common scale.
    void ColumnStats(size_t dim, double& mean, double& stddev) const;
};

} // namespace UltraCanvas
