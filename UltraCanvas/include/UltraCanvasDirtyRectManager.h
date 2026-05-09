// include/UltraCanvasDirtyRectManager.h
// Dirty rectangle collection and optimization system for partial rendering
// Version: 3.0.0
// Last Modified: 2026-04-27
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include <vector>

namespace UltraCanvas {

    class UltraCanvasDirtyRectManager {
    private:
        std::vector<Rect2Di> dirtyRects;
        std::vector<Rect2Di> optimizedRects;
        bool needsOptimization = false;

        float mergeEfficiencyThreshold = 0.6f;
        int maxRectangles = 20;

    public:
        UltraCanvasDirtyRectManager() = default;

        // Skips zero-size and rects already enclosed by an existing entry.
        void Add(const Rect2Di& rect);

        void OptimizeRectangles();
        void ForceOptimization();
        const std::vector<Rect2Di>& GetOptimizedRectangles();

        void Clear();
        bool HasDirtyRects() const { return !dirtyRects.empty(); }
        size_t GetDirtyRectCount() const { return dirtyRects.size(); }

        void SetMergeEfficiencyThreshold(float threshold) { mergeEfficiencyThreshold = threshold; }
        void SetMaxRectangles(int max) { maxRectangles = max; }

    private:
        void MergeOverlappingRects();
        void RemoveEnclosedRects();
        bool ShouldMergeRects(const Rect2Di& a, const Rect2Di& b) const;
        float CalculateMergeEfficiency(const Rect2Di& a, const Rect2Di& b) const;
        static float Area(const Rect2Di& r) {
            return static_cast<float>(r.width) * static_cast<float>(r.height);
        }
    };

} // namespace UltraCanvas
