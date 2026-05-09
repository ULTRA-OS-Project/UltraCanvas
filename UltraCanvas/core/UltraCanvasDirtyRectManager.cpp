// UltraCanvasDirtyRectManager.cpp
// Implementation of dirty rectangle collection and optimization system
// Version: 3.0.0
// Last Modified: 2026-04-27
// Author: UltraCanvas Framework

#include "UltraCanvasDirtyRectManager.h"
#include <algorithm>

namespace UltraCanvas {

    static bool RectContainsRect(const Rect2Di& outer, const Rect2Di& inner) {
        return inner.x >= outer.x &&
               inner.y >= outer.y &&
               inner.x + inner.width  <= outer.x + outer.width &&
               inner.y + inner.height <= outer.y + outer.height;
    }

    void UltraCanvasDirtyRectManager::Add(const Rect2Di& rect) {
        if (rect.width <= 0 || rect.height <= 0) return;

        for (const auto& existing : dirtyRects) {
            if (RectContainsRect(existing, rect)) return;
        }

        dirtyRects.push_back(rect);
        needsOptimization = true;

        if (dirtyRects.size() >= static_cast<size_t>(maxRectangles)) {
            ForceOptimization();
        }
    }

    void UltraCanvasDirtyRectManager::OptimizeRectangles() {
        if (!needsOptimization || dirtyRects.empty()) return;

        optimizedRects = dirtyRects;
        RemoveEnclosedRects();
        MergeOverlappingRects();

        needsOptimization = false;
    }

    void UltraCanvasDirtyRectManager::ForceOptimization() {
        needsOptimization = true;
        OptimizeRectangles();
        // Collapse the input list onto the optimized result so subsequent Add()
        // calls dedup against the merged set.
        dirtyRects = optimizedRects;
    }

    const std::vector<Rect2Di>& UltraCanvasDirtyRectManager::GetOptimizedRectangles() {
        if (needsOptimization) OptimizeRectangles();
        return optimizedRects;
    }

    void UltraCanvasDirtyRectManager::Clear() {
        dirtyRects.clear();
        optimizedRects.clear();
        needsOptimization = false;
    }

    void UltraCanvasDirtyRectManager::RemoveEnclosedRects() {
        if (optimizedRects.size() <= 1) return;

        std::sort(optimizedRects.begin(), optimizedRects.end(),
                  [](const Rect2Di& a, const Rect2Di& b) {
                      return Area(a) > Area(b);
                  });

        for (auto it = optimizedRects.begin(); it != optimizedRects.end(); ) {
            bool enclosed = false;
            for (auto check = optimizedRects.begin(); check != it; ++check) {
                if (RectContainsRect(*check, *it)) { enclosed = true; break; }
            }
            if (enclosed) it = optimizedRects.erase(it);
            else ++it;
        }
    }

    void UltraCanvasDirtyRectManager::MergeOverlappingRects() {
        bool merged = true;
        while (merged && optimizedRects.size() > 1) {
            merged = false;
            for (auto it1 = optimizedRects.begin(); it1 != optimizedRects.end() && !merged; ++it1) {
                for (auto it2 = it1 + 1; it2 != optimizedRects.end(); ++it2) {
                    if (ShouldMergeRects(*it1, *it2)) {
                        *it1 = it1->Union(*it2);
                        optimizedRects.erase(it2);
                        merged = true;
                        break;
                    }
                }
            }
        }
    }

    bool UltraCanvasDirtyRectManager::ShouldMergeRects(const Rect2Di& a, const Rect2Di& b) const {
        if (!a.Intersects(b)) return false;
        return CalculateMergeEfficiency(a, b) >= mergeEfficiencyThreshold;
    }

    float UltraCanvasDirtyRectManager::CalculateMergeEfficiency(const Rect2Di& a, const Rect2Di& b) const {
        float unionArea = Area(a.Union(b));
        if (unionArea <= 0.0f) return 0.0f;
        return (Area(a) + Area(b)) / unionArea;
    }

} // namespace UltraCanvas
