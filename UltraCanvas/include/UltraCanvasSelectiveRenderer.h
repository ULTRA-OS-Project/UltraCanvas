// UltraCanvasSelectiveRenderer.h
// Simple selective rendering system using only isDirty flags
// Version: 1.0.0
// Last Modified: 2025-09-07
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include <vector>
#include <unordered_set>

namespace UltraCanvas {

// Forward declarations
    class UltraCanvasBaseWindow;
    class UltraCanvasContainer;

// ===== SIMPLE DIRTY REGION =====
    struct DirtyRegion {
        Rect2Di bounds;
        bool isOverlay; // For menus, dropdowns, tooltips

        DirtyRegion(const Rect2Di& rect, bool overlay = false)
                : bounds(rect), isOverlay(overlay) {}

        bool Intersects(const DirtyRegion& other) const {
            return bounds.Intersects(other.bounds);
        }

        void Merge(const DirtyRegion& other) {
            bounds = bounds.Union(other.bounds);
        }
    };

// ===== SIMPLE SELECTIVE RENDERER =====
    class UltraCanvasSelectiveRenderer {
    private:
        UltraCanvasBaseWindow* window;

        // Simple dirty tracking
        std::vector<DirtyRegion> dirtyRegions;
//        std::unordered_set<UltraCanvasElement*> dirtyElements;

        // Overlay background storage for menus/dropdowns
        std::vector<uint32_t> savedBackground;
        Rect2Di savedBackgroundRegion;
        bool hasOverlayBackground = false;

    public:
        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasSelectiveRenderer(UltraCanvasBaseWindow* win);
        ~UltraCanvasSelectiveRenderer() = default;

        // ===== DIRTY TRACKING =====
        void MarkRegionDirty(const Rect2Di& region, bool isOverlay = false);
        void MarkFullRedraw();
        void ClearDirtyRegions();

        bool HasDirtyRegions() const { return !dirtyRegions.empty(); }

        // ===== SIMPLE RENDERING =====
        void RenderFrame();
        void RenderDirtyRegions();

        // ===== OVERLAY SUPPORT =====
        void SaveBackgroundForOverlay(UltraCanvasElement* overlayElement);
        void RestoreBackgroundFromOverlay();

    private:
        // ===== INTERNAL HELPERS =====
        void OptimizeDirtyRegions();
        void MergeOverlappingRegions();
        std::vector<UltraCanvasElement*> GetElementsInRegion(const Rect2Di& region);
        void CollectElementsFromContainer(UltraCanvasContainer* container,
                                          const Rect2Di& region,
                                          std::vector<UltraCanvasElement*>& elements);
        void SetClippingRegion(const Rect2Di& clipRect);
        void ClearClippingRegion();
    };

} // namespace UltraCanvas
