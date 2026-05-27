// include/UltraCanvasSplitPane.h
// Split pane container that divides content into N draggable panes along one axis
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include <vector>
#include <memory>
#include <functional>
#include <string>

namespace UltraCanvas {

    class UltraCanvasSplitPane;

    enum class SplitOrientation {
        Horizontal, // panes laid out left-to-right; splitter is a vertical strip
        Vertical    // panes laid out top-to-bottom; splitter is a horizontal strip
    };

    struct SplitPaneStyle {
        int splitterThickness = 4;
        int splitterHitMargin = 0;
        Color splitterColor       = Color(220, 220, 220, 255);
        Color splitterHoverColor  = Color(180, 200, 240, 255);
        Color splitterActiveColor = Color(120, 160, 230, 255);
        bool  showSplitterBackground = true;
    };

    class UltraCanvasSplitter : public UltraCanvasUIElement {
    public:
        UltraCanvasSplitter(const std::string& id, SplitOrientation orient, UltraCanvasSplitPane* owner);

        void SetIndex(size_t i) { index = i; }
        size_t GetIndex() const { return index; }

        void SetStyle(const SplitPaneStyle& s) { style = s; }

        bool IsDragging() const { return dragging; }

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        SplitOrientation orientation;
        UltraCanvasSplitPane* owner = nullptr;
        size_t index = 0;
        SplitPaneStyle style;

        bool dragging = false;
        int dragStartGlobalAxis = 0;
    };

    class UltraCanvasSplitPane : public UltraCanvasContainer {
    public:
        UltraCanvasSplitPane(const std::string& id, float x, float y, float w, float h,
                             SplitOrientation orient);

        // ===== PANE MANAGEMENT =====
        std::shared_ptr<UltraCanvasContainer> AddPane(double weight = 1.0);
        std::shared_ptr<UltraCanvasContainer> InsertPane(size_t index, double weight = 1.0);
        void RemovePane(size_t index);
        void RemovePane(UltraCanvasContainer* pane);

        size_t PaneCount() const { return panes.size(); }
        std::shared_ptr<UltraCanvasContainer> GetPane(size_t index) const;
        int GetPaneIndex(UltraCanvasContainer* pane) const;

        // ===== SIZING =====
        void   SetPaneWeight(size_t index, double weight);
        double GetPaneWeight(size_t index) const;
        void   SetPaneMinSize(size_t index, int px);
        void   SetPaneMaxSize(size_t index, int px);
        void   SetPaneSizes(const std::vector<int>& pixelSizes);

        // ===== STYLE / ORIENTATION =====
        SplitOrientation GetOrientation() const { return orientation; }
        void SetSplitPaneStyle(const SplitPaneStyle& s);
        const SplitPaneStyle& GetSplitPaneStyle() const { return splitStyle; }

        // ===== CALLBACKS =====
        std::function<void(size_t splitterIndex)>       onSplitterDragStart;
        std::function<void(size_t splitterIndex)>       onSplitterDragEnd;
        std::function<void(const std::vector<double>&)> onWeightsChanged;

        // ===== OVERRIDES =====
        void UpdateGeometry(IRenderContext* ctx) override;
        void SetBounds(const Rect2Df& b) override;

        // ===== INTERNAL (called by UltraCanvasSplitter) =====
        void BeginSplitterDrag(size_t splitterIndex);
        void UpdateSplitterDrag(size_t splitterIndex, int deltaAxisPixels);
        void EndSplitterDrag(size_t splitterIndex);

    private:
        struct PaneSlot {
            std::shared_ptr<UltraCanvasContainer> pane;
            double weight  = 1.0;
            int    minSize = 0;
            int    maxSize = 0; // 0 = unlimited
        };

        SplitOrientation orientation;
        SplitPaneStyle splitStyle;
        std::vector<PaneSlot> panes;
        std::vector<std::shared_ptr<UltraCanvasSplitter>> splitters;

        // Drag-start snapshot (axis pixel sizes of all panes at MouseDown)
        std::vector<int> dragStartPaneSizes;

        void EnsureSplitterCountMatches();
        void RebindSplitterIndices();
        void PerformLayout();
        std::vector<int> ComputePaneSizes(int availableAxis) const;
        int AxisLength() const;
    };

    // ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasSplitPane> CreateHorizontalSplitPane(
            const std::string& id, float x, float y, float w, float h) {
        return std::make_shared<UltraCanvasSplitPane>(id, x, y, w, h, SplitOrientation::Horizontal);
    }

    inline std::shared_ptr<UltraCanvasSplitPane> CreateVerticalSplitPane(
            const std::string& id, float x, float y, float w, float h) {
        return std::make_shared<UltraCanvasSplitPane>(id, x, y, w, h, SplitOrientation::Vertical);
    }

} // namespace UltraCanvas
