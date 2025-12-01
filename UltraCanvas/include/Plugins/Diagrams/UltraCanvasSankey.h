// include/Plogins/Diagrams/UltraCanvasSankey.h
// Interactive Sankey diagram plugin for data flow visualization
// Version: 1.3.0
// Last Modified: 2025-10-16
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <memory>
#include <fstream>
#include <sstream>
#include <cmath>
#include <functional>

namespace UltraCanvas {

// ===== DATA STRUCTURES =====
    struct SankeyNode {
        std::string id;
        std::string label;
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float value = 0.0f;
        int depth = 0;
        int ordering = 0;
        Color color = Colors::Blue;
        bool isDragging = false;
        std::vector<std::string> sourceLinks;
        std::vector<std::string> targetLinks;
    };

    struct SankeyLink {
        std::string source;
        std::string target;
        float value = 0.0f;
        float sourceY = 0.0f;
        float targetY = 0.0f;
        float sourceWidth = 0.0f;  // Width at source node
        float targetWidth = 0.0f;  // Width at target node
        Color color = Colors::LightBlue;
        float opacity = 0.7f;
    };

// ===== ALIGNMENT OPTIONS =====
    enum class SankeyAlignment {
        Left,
        Right,
        Center,
        Justify
    };

// ===== THEME OPTIONS =====
    enum class SankeyTheme {
        Default,
        Energy,
        Finance,
        WebTraffic,
        Custom
    };

// ===== SANKEY RENDERER CLASS =====
    class UltraCanvasSankeyDiagram : public UltraCanvasUIElement {
    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasSankeyDiagram(const std::string& id, long uid, long x, long y, long w, long h);
        bool AcceptsFocus() const override { return true; }

        // ===== NODE MANAGEMENT =====
        void AddNode(const std::string& id, const std::string& label = "", const Color& tgtColor = Colors::Transparent);
        void RemoveNode(const std::string& id);

        // ===== LINK MANAGEMENT =====
        void AddLink(const std::string& source, const std::string& target, float value, const Color& tgtColor = Colors::Transparent);
        void RemoveLink(const std::string& source, const std::string& target);
        void ClearAll();

        // ===== DATA LOADING =====
        bool LoadFromCSV(const std::string& filePath);
        bool SaveToSVG(const std::string& filePath);

        // ===== LAYOUT ALGORITHM =====
        void PerformLayout();

        // ===== RENDERING =====
        void Render(IRenderContext* ctx) override;

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;

        // ===== CONFIGURATION =====
        void SetAlignment(SankeyAlignment align);
        void SetTheme(SankeyTheme t);
        void SetNodeWidth(float width);
        void SetNodePadding(float padding);
        void SetLinkCurvature(float curvature);
        void SetIterations(int iter);
        void SetFontSize(float size);
        void SetFontFamily(const std::string& family);
        void SetMaxLabelWidth(float width);
        float GetMaxLabelWidth() const;

        void SetManualOrderMode(bool enabled);
        bool GetManualOrderMode() const;
        void SetNodeOrdering(const std::string& nodeId, int ordering);
        int GetNodeOrdering(const std::string& nodeId) const;

        // ===== CALLBACKS =====
        std::function<void(const std::string&)> onNodeClick;
        std::function<void(const std::string&, const std::string&)> onLinkClick;
        std::function<void(const std::string&)> onNodeHover;
        std::function<void(const std::string&, const std::string&)> onLinkHover;

    private:
        // ===== MEMBER VARIABLES =====
        std::map<std::string, SankeyNode> nodes;
        std::vector<SankeyLink> links;

        float nodeWidth;
        float nodePadding;
        float linkCurvature;
        int iterations;
        SankeyAlignment alignment;
        SankeyTheme theme;
        float maxLabelWidth;

        bool manualOrderMode = false;
        bool needsLayout = true;
        bool enableAnimation;
        bool enableTooltips;
        std::string hoveredNodeId;
        int hoveredLinkIndex = -1;
        std::string draggedNodeId;
        Point2Di dragOffset;

        struct {
            bool hasBackground = true;
            Color backgroundColor = Color(245, 245, 245);
            Color nodeStrokeColor = Colors::DarkGray;
            float nodeStrokeWidth = 1.0f;
            Color textColor = Colors::Black;
            std::string fontFamily = "Arial";
            float fontSize = 12.0f;
            Color tooltipBackground = Color(255, 255, 255, 230);
            Color tooltipBorder = Colors::Gray;
            float tooltipPadding = 8.0f;
        } style;

        // ===== LAYOUT METHODS =====
        void ComputeNodeDepths();
        void AssignDepth(const std::string& nodeId, int depth);
        void ComputeNodeValues();
        void ComputeNodeBreadths();
        void ComputeLinkBreadths();
        void RelaxLeftToRight();
        void RelaxRightToLeft();
        void ResolveCollisions(const std::vector<std::string>& nodeIds);

        // ===== DRAWING METHODS =====
        void DrawNode(IRenderContext* ctx, const SankeyNode& node);
        void DrawLink(IRenderContext* ctx, const SankeyLink& link);
        void DrawCurvedLink(IRenderContext* ctx, float x0, float y0,
                            float x1, float y1, float sourceWidth, float targetWidth,
                            const Color& color);

        void DrawTooltip(IRenderContext* ctx);

        // ===== EVENT HANDLERS =====
        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);

        // ===== HELPER METHODS =====
        Color GetNodeColor(size_t index);
        void ApplyTheme(SankeyTheme t);
    };

//// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasSankeyDiagram> CreateSankeyRenderer(
            const std::string& id, long uid, long x, long y, long w, long h
    ) {
        return std::make_shared<UltraCanvasSankeyDiagram>(id, uid, x, y, w, h);
    }
} // namespace UltraCanvas