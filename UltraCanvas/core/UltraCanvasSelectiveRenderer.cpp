// UltraCanvasSelectiveRenderer.cpp
// Simple implementation using only isDirty flags
// Version: 1.0.0
// Last Modified: 2025-09-07
// Author: UltraCanvas Framework

#include "UltraCanvasSelectiveRenderer.h"
#include "UltraCanvasBaseWindow.h"
#include "UltraCanvasRenderInterface.h"
#include <algorithm>
#include <iostream>

namespace UltraCanvas {

    UltraCanvasSelectiveRenderer::UltraCanvasSelectiveRenderer(UltraCanvasBaseWindow* win)
            : window(win) {
    }

// ===== DIRTY TRACKING =====

//    void UltraCanvasSelectiveRenderer::MarkElementDirty(UltraCanvasElement* element) {
//        if (!element) return;
//
//        // Add element to dirty set
//        dirtyElements.insert(element);
//
//        // Calculate element bounds and add to dirty regions
//        Rect2Di bounds = element->GetBoundsInWindow();
//
//        // Check if this is an overlay element (menu, dropdown, tooltip)
//        std::string typeName = typeid(*element).name();
//        bool isOverlay = (typeName.find("Menu") != std::string::npos) ||
//                         (typeName.find("Dropdown") != std::string::npos) ||
//                         (typeName.find("Tooltip") != std::string::npos);
//
//        MarkRegionDirty(bounds, isOverlay);
//    }

    void UltraCanvasSelectiveRenderer::MarkRegionDirty(const Rect2Di& region, bool isOverlay) {
        dirtyRegions.emplace_back(region, isOverlay);
    }

    void UltraCanvasSelectiveRenderer::MarkFullRedraw() {
        if (!window) return;

        Rect2D windowRect(0, 0, window->GetWidth(), window->GetHeight());
        dirtyRegions.clear();
        //dirtyElements.clear();
        MarkRegionDirty(windowRect, false);
    }

    void UltraCanvasSelectiveRenderer::ClearDirtyRegions() {
        dirtyRegions.clear();
        //dirtyElements.clear();
    }

// ===== SIMPLE RENDERING =====

    void UltraCanvasSelectiveRenderer::RenderFrame() {
        if (!window) return;

        // Only render if there are dirty regions
        if (HasDirtyRegions()) {
            RenderDirtyRegions();
        }
    }

    void UltraCanvasSelectiveRenderer::RenderDirtyRegions() {
        // Optimize dirty regions by merging overlapping ones
        OptimizeDirtyRegions();

        // Process each dirty region
        for (const auto& region : dirtyRegions) {
            // Set clipping to dirty region to avoid drawing outside
            SetClippingRegion(region.bounds);

            if (region.isOverlay) {
                // Handle overlay elements (menus, dropdowns) - they need special treatment
                auto overlayElements = window->GetActivePopups();
                for (auto* element : overlayElements) {
                    if (element && element->IsVisible() &&
                        element->GetActualBoundsInWindow().Intersects(region.bounds)) {
                        element->Render();
                    }
                }
            } else {
                // Clear the dirty region first (paint background)
//                auto renderContext = RenderContextManager::GetCurrent();
//                if (renderContext) {
//                    // Fill with window background color
//                    renderContext->SetFillColor(window->GetBackgroundColor());
//                    renderContext->FillRectangle(region.bounds.x, region.bounds.y,
//                                                 region.bounds.width, region.bounds.height);
//                }

                // Get all elements that intersect with this dirty region
                auto elementsInRegion = GetElementsInRegion(region.bounds);

                // Sort by z-index for proper rendering order
                std::sort(elementsInRegion.begin(), elementsInRegion.end(),
                          [](UltraCanvasElement* a, UltraCanvasElement* b) {
                              return a->GetZIndex() < b->GetZIndex();
                          });

                // Render each element in the region
                for (auto* element : elementsInRegion) {
                    if (element && element->IsVisible()) {
                        element->Render();

                        // Mark element as clean if it was dirty
//                        if (element->IsDirty()) {
//                            element->MarkClean();
//                        }
                    }
                }
            }

            ClearClippingRegion();
        }

        // Clear dirty regions after rendering
        ClearDirtyRegions();
    }

// ===== OVERLAY SUPPORT =====

    void UltraCanvasSelectiveRenderer::SaveBackgroundForOverlay(UltraCanvasElement* overlayElement) {
        if (!overlayElement || !window) return;

        Rect2Di bounds = overlayElement->GetActualBoundsInWindow();

        // Simple background saving - in a real implementation, this would
        // capture the actual pixels from the framebuffer
        savedBackgroundRegion = bounds;
        hasOverlayBackground = true;

        // Mark that we need to save background
        std::cout << "Saving background for overlay at (" << bounds.x << ", " << bounds.y
                  << ", " << bounds.width << ", " << bounds.height << ")" << std::endl;
    }

    void UltraCanvasSelectiveRenderer::RestoreBackgroundFromOverlay() {
        if (!hasOverlayBackground) return;

        // Mark the overlay region as dirty to redraw background
        MarkRegionDirty(savedBackgroundRegion, false);
        hasOverlayBackground = false;

        std::cout << "Restoring background from overlay" << std::endl;
    }

// ===== OPTIMIZATION =====

    void UltraCanvasSelectiveRenderer::OptimizeDirtyRegions() {
        if (dirtyRegions.size() <= 1) return;

        MergeOverlappingRegions();
    }

    void UltraCanvasSelectiveRenderer::MergeOverlappingRegions() {
        bool merged = true;

        while (merged && dirtyRegions.size() > 1) {
            merged = false;

            for (auto it1 = dirtyRegions.begin(); it1 != dirtyRegions.end() && !merged; ++it1) {
                for (auto it2 = it1 + 1; it2 != dirtyRegions.end(); ++it2) {
                    if (it1->Intersects(*it2)) {
                        // Calculate merge efficiency to avoid creating huge regions
                        float area1 = it1->bounds.width * it1->bounds.height;
                        float area2 = it2->bounds.width * it2->bounds.height;
                        Rect2D unionRect = it1->bounds.Union(it2->bounds);
                        float unionArea = unionRect.width * unionRect.height;

                        float efficiency = (area1 + area2) / unionArea;

                        if (efficiency >= 0.5f) { // Only merge if reasonably efficient
                            // Merge regions
                            it1->Merge(*it2);
                            dirtyRegions.erase(it2);
                            merged = true;
                            break;
                        }
                    }
                }
            }
        }
    }

// ===== HELPER METHODS =====

    std::vector<UltraCanvasElement*> UltraCanvasSelectiveRenderer::GetElementsInRegion(const Rect2Di& region) {
        std::vector<UltraCanvasElement*> elements;

        if (!window) return elements;

        // Collect all elements from window's containers
        CollectElementsFromContainer(window, region, elements);

        return elements;
    }

    void UltraCanvasSelectiveRenderer::CollectElementsFromContainer(UltraCanvasContainer* container,
                                                                 const Rect2Di& region,
                                                                 std::vector<UltraCanvasElement*>& elements) {
        if (!container) return;

        // Get all elements from container that intersect with region
        auto containerElements = container->GetChildren();
        for (auto element : containerElements) {
            if (element && element->IsVisible()) {
                Rect2D elementBounds = element->GetActualBoundsInWindow();
                if (elementBounds.Intersects(region)) {
                    elements.push_back(element.get());
                }
            }

            // If element is also a container, search recursively
            auto* childContainer = dynamic_cast<UltraCanvasContainer*>(element.get());
            if (childContainer) {
                CollectElementsFromContainer(childContainer, region, elements);
            }
        }
    }

    void UltraCanvasSelectiveRenderer::SetClippingRegion(const Rect2Di& clipRect) {
        auto renderContext = RenderContextManager::GetCurrent();
        if (renderContext) {
            renderContext->SetClipRect(clipRect.x, clipRect.y, clipRect.width, clipRect.height);
        }
    }

    void UltraCanvasSelectiveRenderer::ClearClippingRegion() {
        auto renderContext = RenderContextManager::GetCurrent();
        if (renderContext) {
            renderContext->ClearClipRect();
        }
    }

} // namespace UltraCanvas