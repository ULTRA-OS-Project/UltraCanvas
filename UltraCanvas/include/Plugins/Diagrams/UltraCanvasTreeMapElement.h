// include/Plugins/Charts/UltraCanvasTreeMapElement.h
// Interactive treemap component with hierarchical data visualization and drill-down navigation
// Version: 1.0.1
// Last Modified: 2025-04-01
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasApplication.h"
#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "Plugins/Charts/UltraCanvasChartDataStructures.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <map>
#include <algorithm>
#include <sstream>

namespace UltraCanvas {

// =============================================================================
// TREEMAP SPECIFIC DATA STRUCTURES
// =============================================================================

    struct TreeMapNode {
        std::string name;
        std::string description;
        double value;
        double secondaryValue;
        Color backgroundColor;
        Color textColor;
        std::string iconPath;
        std::string backgroundImagePath;
        bool hasBackgroundImage;
        bool isTransparent;
        std::vector<std::shared_ptr<TreeMapNode>> children;
        TreeMapNode* parent;
        
        std::vector<std::string> displayTexts;
        std::vector<std::string> iconPaths;
        Rect2Dd bounds;
        bool isLeaf;
        int depth;
        bool isVisible;
        
        TreeMapNode(const std::string& n = "", double val = 0.0, double secVal = 0.0)
            : name(n), value(val), secondaryValue(secVal), parent(nullptr), 
              isLeaf(true), depth(0), isVisible(true), hasBackgroundImage(false),
              isTransparent(false), backgroundColor(Colors::White), textColor(Colors::Black) {}
              
        void AddChild(std::shared_ptr<TreeMapNode> child) {
            child->parent = this;
            child->depth = depth + 1;
            children.push_back(child);
            isLeaf = false;
        }
        
        double GetTotalValue() const {
            if (isLeaf) return value;
            double total = 0.0;
            for (const auto& child : children) {
                total += child->GetTotalValue();
            }
            return total;
        }
    };

    enum class TreeMapAlgorithm {
        Squarified,
        SliceAndDice,
        Strip,
        Spiral,
        Binary
    };

    enum class TreeMapStyle {
        Flat,
        Raised,
        Sunken,
        Gradient,
        Textured,
        Minimal
    };

    enum class TreeMapColorScheme {
        Sequential,
        Diverging,
        Categorical,
        Random,
        Monochrome,
        Custom
    };

    class UltraCanvasTreeMapElement : public UltraCanvasChartElementBase {
    private:
        std::shared_ptr<TreeMapNode> rootNode;
        std::shared_ptr<TreeMapNode> currentNode;
        std::vector<std::shared_ptr<TreeMapNode>> navigationHistory;
        
        TreeMapAlgorithm algorithm = TreeMapAlgorithm::Squarified;
        TreeMapStyle visualStyle = TreeMapStyle::Flat;
        TreeMapColorScheme colorScheme = TreeMapColorScheme::Sequential;
        
        double borderWidth = 2.0;
        Color borderColor = Color(255, 255, 255, 255);
        double padding = 4.0;
        double minRectSize = 10.0;
        bool showLabels = true;
        bool showValues = true;
        bool showPercentages = false;
        bool showIcons = true;
        
        std::vector<Color> colorPalette = {
            Color(26, 188, 156, 255),
            Color(52, 152, 219, 255),
            Color(155, 89, 182, 255),
            Color(241, 196, 15, 255),
            Color(230, 126, 34, 255),
            Color(231, 76, 60, 255),
            Color(46, 204, 113, 255),
            Color(149, 165, 166, 255)
        };
        
        bool enableAnimations = true;
        double animationDuration = 0.5f;
        double currentAnimationProgress = 1.0;
        bool isAnimating = false;
        
        std::map<std::string, Rect2Dd> oldBoundsMap;
        std::string animationType;
        
        std::shared_ptr<TreeMapNode> hoveredNode = nullptr;
        std::shared_ptr<TreeMapNode> selectedNode = nullptr;
        bool enableDrillDown = true;
        
        std::string fontFamily = "Arial";
        float fontSize = 12.0;
        float titleFontSize = 14.0;
        bool autoScaleText = true;
        
        Color backgroundColor = Color(240, 240, 240, 255);
        std::string backgroundImagePath;
        bool hasGlobalBackground = false;
        bool globalTransparent = false;

    public:
        UltraCanvasTreeMapElement(const std::string& id, int x, int y, int width, int height)
            : UltraCanvasChartElementBase(id, x, y, width, height) {
            enableTooltips = true;
            enableSelection = true;
            enableZoom = true;
            enablePan = true;
            rootNode = std::make_shared<TreeMapNode>("Root", 0.0);
            currentNode = rootNode;
            fontFamily = UltraCanvasApplication::GetInstance()->GetSystemFontStyle().fontFamily;
        }

        void SetRootNode(std::shared_ptr<TreeMapNode> root) {
            rootNode = root;
            currentNode = root;
            navigationHistory.clear();
            RecalculateLayout();
            RequestRedraw();
        }
        
        std::shared_ptr<TreeMapNode> GetRootNode() const { return rootNode; }
        std::shared_ptr<TreeMapNode> GetCurrentNode() const { return currentNode; }
        
        void SetDataFromHierarchy(const std::vector<std::tuple<std::string, std::string, double, double>>& data);

        void SetLayoutAlgorithm(TreeMapAlgorithm algo) {
            algorithm = algo;
            RecalculateLayout();
            RequestRedraw();
        }
        
        void SetVisualStyle(TreeMapStyle style) {
            visualStyle = style;
            RequestRedraw();
        }
        
        void SetColorScheme(TreeMapColorScheme scheme) {
            colorScheme = scheme;
            UpdateColors();
            RequestRedraw();
        }
        
        void SetColorPalette(const std::vector<Color>& palette) {
            colorPalette = palette;
            colorScheme = TreeMapColorScheme::Custom;
            UpdateColors();
            RequestRedraw();
        }

        void SetBorderProperties(double width, const Color& color) {
            borderWidth = width;
            borderColor = color;
            RequestRedraw();
        }
        
        void SetPadding(double pad) {
            padding = std::max(0.0, pad);
            RecalculateLayout();
            RequestRedraw();
        }
        
        void SetMinimumRectangleSize(double minSize) {
            minRectSize = std::max(1.0, minSize);
            RecalculateLayout();
            RequestRedraw();
        }
        
        void SetTextProperties(const std::string& font, double size, double titleSize, bool autoScale) {
            fontFamily = font;
            fontSize = size;
            titleFontSize = titleSize;
            autoScaleText = autoScale;
            RequestRedraw();
        }
        
        void SetDisplayOptions(bool labels, bool values, bool percentages, bool icons) {
            showLabels = labels;
            showValues = values;
            showPercentages = percentages;
            showIcons = icons;
            RequestRedraw();
        }

        void SetBackgroundColor(const Color& color) {
            backgroundColor = color;
            hasGlobalBackground = true;
            globalTransparent = false;
            RequestRedraw();
        }

        void SetAnimationEnabled(bool enabled) {
            enableAnimations = enabled;
        }

        bool DrillDown(std::shared_ptr<TreeMapNode> node);
        bool DrillUp();
        void ResetToRoot();
        std::vector<std::string> GetNavigationPath() const;

        void SetInteractionOptions(bool drillDown, bool zoom, bool pan) {
            enableDrillDown = drillDown;
            enableZoom = zoom;
            enablePan = pan;
        }

        void SelectNode(std::shared_ptr<TreeMapNode> node);
        void ClearSelection();
        std::shared_ptr<TreeMapNode> GetSelectedNode() const { return selectedNode; }

        std::shared_ptr<TreeMapNode> FindNodeByName(const std::string& name) const;
        std::vector<std::shared_ptr<TreeMapNode>> FindNodesByValue(double minValue, double maxValue) const;
        void HighlightNode(std::shared_ptr<TreeMapNode> node);

        std::function<void(const std::string&)> onNodeDrillDown;
        std::function<void(const std::string&)> onNodeDrillUp;
        std::function<void()> onNodeReset;
        std::function<void(const std::string&, double)> onNodeSelect;
        std::function<void()> onNodeDeselect;
        std::function<void(const std::string&, const Point2Di&)> onNodeHover;
        std::function<void(const std::string&, double, double)> onNodeDoubleClick;
        std::function<void(const std::string&)> onNodeRightClick;

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool HandleChartMouseWheel(double delta);
        bool HandleChartPan(int dx, int dy);

    private:
        void RecalculateLayout();
        void CalculateSquarifiedLayout(std::vector<std::shared_ptr<TreeMapNode>>& nodes, const Rect2Dd& area);
        void SquarifyRecursive(std::vector<std::shared_ptr<TreeMapNode>>& nodes, size_t start, const Rect2Dd& area, double length);
        void CalculateSliceAndDiceLayout(std::vector<std::shared_ptr<TreeMapNode>>& nodes, const Rect2Dd& area, int depth);
        void CalculateStripLayout(std::vector<std::shared_ptr<TreeMapNode>>& nodes, const Rect2Dd& area);
        void CalculateSpiralLayout(std::vector<std::shared_ptr<TreeMapNode>>& nodes, const Rect2Dd& area);
        void CalculateBinaryLayout(std::vector<std::shared_ptr<TreeMapNode>>& nodes, const Rect2Dd& area);
        void BinaryLayoutRecursive(std::vector<std::shared_ptr<TreeMapNode>>& nodes, size_t start, size_t end, const Rect2Dd& area);

        void DrawGlobalBackground(IRenderContext* ctx);
        void DrawTreeMapRectangle(IRenderContext* ctx, std::shared_ptr<TreeMapNode> node);
        void DrawFlatRectangle(IRenderContext* ctx, std::shared_ptr<TreeMapNode> node, const Rect2Dd& rect);
        void DrawRaisedRectangle(IRenderContext* ctx, std::shared_ptr<TreeMapNode> node, const Rect2Dd& rect);
        void DrawSunkenRectangle(IRenderContext* ctx, std::shared_ptr<TreeMapNode> node, const Rect2Dd& rect);
        void DrawGradientRectangle(IRenderContext* ctx, std::shared_ptr<TreeMapNode> node, const Rect2Dd& rect);
        void DrawTexturedRectangle(IRenderContext* ctx, std::shared_ptr<TreeMapNode> node, const Rect2Dd& rect);
        void DrawMinimalRectangle(IRenderContext* ctx, std::shared_ptr<TreeMapNode> node, const Rect2Dd& rect);
        void DrawRectangleContent(IRenderContext* ctx, std::shared_ptr<TreeMapNode> node, const Rect2Dd& rect);
        void DrawSelectionIndicator(IRenderContext* ctx, const Rect2Dd& rect);
        void DrawHoverEffect(IRenderContext* ctx, const Rect2Dd& rect);
        void DrawNavigationBreadcrumbs(IRenderContext* ctx);
        void DrawLeafNodeDetails(IRenderContext* ctx);

        std::shared_ptr<TreeMapNode> GetNodeAtPosition(const Point2Di& pos) const;
        std::shared_ptr<TreeMapNode> FindNodeAtPositionRecursive(std::shared_ptr<TreeMapNode> node, const Point2Dd& pos) const;
        Color GetNodeColor(std::shared_ptr<TreeMapNode> node) const;
        void UpdateColors();
        void UpdateNodeColorsRecursive(std::shared_ptr<TreeMapNode> node);
        void ShowNodeTooltip(const Point2Di& mousePos, std::shared_ptr<TreeMapNode> node);
        
        void StartDrillDownAnimation();
        void StartDrillUpAnimation();
        void StartResetAnimation();
        void ApplyAnimationTransform(IRenderContext* ctx);

        std::vector<std::string> SplitPath(const std::string& path) const;
        std::shared_ptr<TreeMapNode> FindNodeRecursive(std::shared_ptr<TreeMapNode> node, const std::string& name) const;
        void FindNodesByValueRecursive(std::shared_ptr<TreeMapNode> node, double minValue, double maxValue, std::vector<std::shared_ptr<TreeMapNode>>& results) const;
        double GetTotalValueForNodes(const std::vector<std::shared_ptr<TreeMapNode>>& nodes, size_t start) const;
        double CalculateWorstRatio(const std::vector<std::shared_ptr<TreeMapNode>>& row, double totalValue, double length) const;
        void LayoutRow(const std::vector<std::shared_ptr<TreeMapNode>>& row, const Rect2Dd& area, double totalValue);
        Rect2Dd CalculateRemainingArea(const Rect2Dd& area, const std::vector<std::shared_ptr<TreeMapNode>>& row, double totalValue) const;
        size_t FindBalancedSplit(const std::vector<std::shared_ptr<TreeMapNode>>& nodes, size_t start, size_t end) const;
        double GetValueRatio(const std::vector<std::shared_ptr<TreeMapNode>>& nodes, size_t start, size_t mid, size_t end) const;
        double GetMaxSecondaryValue() const;
        void GetMaxSecondaryValueRecursive(std::shared_ptr<TreeMapNode> node, double& maxVal) const;
        Color LightenColor(const Color& color, double factor) const;
        Color DarkenColor(const Color& color, double factor) const;
        Color InterpolateColor(const Color& start, const Color& end, double ratio) const;
        Color GenerateRandomColor(const std::string& seed) const;
        double CalculateOptimalFontSize(const std::string& text, double availableWidth, double maxSize) const;
        std::string FormatValue(double value) const;
        std::string FormatPercentage(double percentage) const;
        bool PointInRect(const Point2Dd& point, const Rect2Dd& rect) const;
        Rect2Dd GetPlotArea() const;
    };

    inline std::shared_ptr<UltraCanvasTreeMapElement> CreateTreeMap(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasTreeMapElement>(id, x, y, width, height);
    }

} // namespace UltraCanvas
