// include/UltraCanvasSankey.h
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
        float width = 0.0f;
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
    class UltraCanvasSankeyRenderer : public UltraCanvasUIElement {
    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasSankeyRenderer(const std::string& id, long uid, long x, long y, long w, long h)
                : UltraCanvasUIElement(id, uid, x, y, w, h) {
            nodeWidth = 20.0f;
            nodePadding = 10.0f;
            linkCurvature = 0.5f;
            iterations = 32;
            alignment = SankeyAlignment::Justify;
            theme = SankeyTheme::Default;
            enableAnimation = true;
            enableTooltips = true;
            ApplyTheme(theme);
        }

        bool AcceptsFocus() const override { return true; }

        // ===== NODE MANAGEMENT =====
        void AddNode(const std::string& id, const std::string& label = "") {
            if (nodes.find(id) == nodes.end()) {
                SankeyNode node;
                node.id = id;
                node.label = label.empty() ? id : label;
                node.color = GetNodeColor(nodes.size());
                nodes[id] = node;
                needsLayout = true;
            }
        }

        void RemoveNode(const std::string& id) {
            auto it = nodes.find(id);
            if (it != nodes.end()) {
                // Remove associated links
                links.erase(
                        std::remove_if(links.begin(), links.end(),
                                       [&id](const SankeyLink& link) {
                                           return link.source == id || link.target == id;
                                       }),
                        links.end()
                );
                nodes.erase(it);
                needsLayout = true;
            }
        }

        // ===== LINK MANAGEMENT =====
        void AddLink(const std::string& source, const std::string& target, float value) {
            // Auto-create nodes if they don't exist
            if (nodes.find(source) == nodes.end()) {
                AddNode(source);
            }
            if (nodes.find(target) == nodes.end()) {
                AddNode(target);
            }

            SankeyLink link;
            link.source = source;
            link.target = target;
            link.value = value;
            link.color = nodes[source].color.WithAlpha(180); // Semi-transparent
            links.push_back(link);

            // Update node connections
            nodes[source].sourceLinks.push_back(target);
            nodes[target].targetLinks.push_back(source);

            needsLayout = true;
        }

        void RemoveLink(const std::string& source, const std::string& target) {
            links.erase(
                    std::remove_if(links.begin(), links.end(),
                                   [&source, &target](const SankeyLink& link) {
                                       return link.source == source && link.target == target;
                                   }),
                    links.end()
            );
            needsLayout = true;
        }

        void ClearAll() {
            nodes.clear();
            links.clear();
            needsLayout = true;
            RequestRedraw();
        }

        // ===== DATA LOADING =====
        bool LoadFromCSV(const std::string& filePath) {
            std::ifstream file(filePath);
            if (!file.is_open()) return false;

            ClearAll();
            std::string line;
            bool firstLine = true;

            while (std::getline(file, line)) {
                if (firstLine) {
                    firstLine = false;
                    continue; // Skip header
                }

                std::stringstream ss(line);
                std::string source, target, valueStr;

                std::getline(ss, source, ',');
                std::getline(ss, target, ',');
                std::getline(ss, valueStr, ',');

                if (!source.empty() && !target.empty() && !valueStr.empty()) {
                    try {
                        float value = std::stof(valueStr);
                        AddLink(source, target, value);
                    } catch (...) {
                        // Skip invalid lines
                    }
                }
            }

            file.close();
            return !links.empty();
        }

        bool SaveToSVG(const std::string& filePath) {
            std::ofstream file(filePath);
            if (!file.is_open()) return false;
            auto bounds = GetBounds();

            file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            file << "<svg width=\"" << bounds.width << "\" height=\"" << bounds.height
                 << "\" xmlns=\"http://www.w3.org/2000/svg\">\n";

            // Export links
            for (const auto& link : links) {
                auto sourceNode = nodes.find(link.source);
                auto targetNode = nodes.find(link.target);
                if (sourceNode != nodes.end() && targetNode != nodes.end()) {
                    file << "  <path d=\"";
                    file << GetLinkPath(
                            sourceNode->second.x + nodeWidth, link.sourceY,
                            targetNode->second.x, link.targetY, link.width
                    );
                    file << "\" fill=\"" << ColorToHex(link.color) << "\" opacity=\""
                         << (link.opacity) << "\"/>\n";
                }
            }

            // Export nodes
            for (const auto& [id, node] : nodes) {
                file << "  <rect x=\"" << node.x << "\" y=\"" << node.y
                     << "\" width=\"" << nodeWidth << "\" height=\"" << node.height
                     << "\" fill=\"" << ColorToHex(node.color) << "\"/>\n";
                file << "  <text x=\"" << (node.x + nodeWidth + 5) << "\" y=\""
                     << (node.y + node.height/2) << "\" alignment-baseline=\"middle\">"
                     << node.label << "</text>\n";
            }

            file << "</svg>\n";
            file.close();
            return true;
        }

        // ===== LAYOUT ALGORITHM =====
        void PerformLayout() {
            if (nodes.empty() || links.empty()) return;

            ComputeNodeDepths();
            ComputeNodeBreadths();
            ComputeNodeValues();
            ComputeLinkBreadths();

            // Iterative relaxation
            for (int i = 0; i < iterations; ++i) {
                RelaxRightToLeft();
                RelaxLeftToRight();
            }

            needsLayout = false;
        }

        // ===== RENDERING =====
        void Render() override {
            IRenderContext* ctx = GetRenderContext();
            if (!IsVisible()) return;

            auto bounds = GetBounds();

            if (needsLayout) {
                PerformLayout();
            }

            // Draw background if enabled
            if (style.hasBackground) {
                ctx->SetFillPaint(style.backgroundColor);
                ctx->FillRectangle(bounds.x, bounds.y, bounds.width, bounds.height);
            }

            // Draw links
            for (const auto& link : links) {
                DrawLink(ctx, link);
            }

            // Draw nodes
            for (const auto& [id, node] : nodes) {
                DrawNode(ctx, node);
            }

            // Draw tooltips
            if (enableTooltips && !hoveredNodeId.empty()) {
                DrawTooltip(ctx);
            }
        }

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override {
            switch (event.type) {
                case UCEventType::MouseMove:
                    return HandleMouseMove(event);
                case UCEventType::MouseDown:
                    return HandleMouseDown(event);
                case UCEventType::MouseUp:
                    return HandleMouseUp(event);
                case UCEventType::MouseLeave:
                    hoveredNodeId.clear();
                    hoveredLinkIndex = -1;
                    RequestRedraw();
                    return true;
                default:
                    break;
            }
            return false;
        }

        // ===== CONFIGURATION =====
        void SetAlignment(SankeyAlignment align) {
            alignment = align;
            needsLayout = true;
            RequestRedraw();
        }

        void SetTheme(SankeyTheme t) {
            theme = t;
            ApplyTheme(theme);
            RequestRedraw();
        }

        void SetNodeWidth(float width) {
            nodeWidth = std::max(1.0f, width);
            needsLayout = true;
            RequestRedraw();
        }

        void SetNodePadding(float padding) {
            nodePadding = std::max(0.0f, padding);
            needsLayout = true;
            RequestRedraw();
        }

        void SetLinkCurvature(float curvature) {
            linkCurvature = std::clamp(curvature, 0.0f, 1.0f);
            RequestRedraw();
        }

        void SetIterations(int iter) {
            iterations = std::max(1, iter);
            needsLayout = true;
            RequestRedraw();
        }

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
        void ComputeNodeDepths() {
            // Reset depths
            for (auto& [id, node] : nodes) {
                node.depth = -1;
            }

            // Find nodes with no targets (sources)
            std::vector<std::string> sources;
            for (const auto& [id, node] : nodes) {
                if (node.targetLinks.empty()) {
                    sources.push_back(id);
                }
            }

            // Breadth-first traversal
            for (const auto& source : sources) {
                AssignDepth(source, 0);
            }

            // Assign remaining nodes
            int maxDepth = 0;
            for (auto& [id, node] : nodes) {
                if (node.depth == -1) {
                    node.depth = 0;
                }
                maxDepth = std::max(maxDepth, node.depth);
            }
        }

        void AssignDepth(const std::string& nodeId, int depth) {
            auto it = nodes.find(nodeId);
            if (it == nodes.end()) return;

            if (it->second.depth < depth) {
                it->second.depth = depth;
            }

            for (const auto& target : it->second.sourceLinks) {
                AssignDepth(target, depth + 1);
            }
        }

        void ComputeNodeBreadths() {
            auto bounds = GetBounds();
            int maxDepth = 0;
            for (const auto& [id, node] : nodes) {
                maxDepth = std::max(maxDepth, node.depth);
            }

            float xStep = (bounds.width - nodeWidth - 2 * nodePadding) / std::max(1, maxDepth);

            // Group nodes by depth
            std::map<int, std::vector<std::string>> nodesByDepth;
            for (const auto& [id, node] : nodes) {
                nodesByDepth[node.depth].push_back(id);
            }

            // Position nodes
            for (auto& [depth, nodeIds] : nodesByDepth) {
                float x = bounds.x + nodePadding + depth * xStep;
                float y = bounds.y + nodePadding;

                for (const auto& id : nodeIds) {
                    nodes[id].x = x;
                    nodes[id].y = y;
                    y += nodes[id].height + nodePadding;
                }
            }
        }

        void ComputeNodeValues() {
            for (auto& [id, node] : nodes) {
                node.value = 0;

                // Sum incoming values
                for (const auto& link : links) {
                    if (link.target == id) {
                        node.value += link.value;
                    }
                }

                // Sum outgoing values if no incoming
                if (node.value == 0) {
                    for (const auto& link : links) {
                        if (link.source == id) {
                            node.value += link.value;
                        }
                    }
                }

                // Convert value to height
                float totalValue = 0;
                for (const auto& [nid, n] : nodes) {
                    if (n.depth == node.depth) {
                        totalValue += n.value;
                    }
                }

                if (totalValue > 0) {
                    float availableHeight = GetHeight() - 2 * nodePadding -
                                            (GetNodeCountAtDepth(node.depth) - 1) * nodePadding;
                    node.height = (node.value / totalValue) * availableHeight;
                    node.height = std::max(1.0f, node.height);
                } else {
                    node.height = 30.0f; // Default height
                }
            }
        }

        int GetNodeCountAtDepth(int depth) {
            int count = 0;
            for (const auto& [id, node] : nodes) {
                if (node.depth == depth) count++;
            }
            return count;
        }

        void ComputeLinkBreadths() {
            // Initialize link positions
            for (auto& link : links) {
                auto sourceIt = nodes.find(link.source);
                auto targetIt = nodes.find(link.target);

                if (sourceIt != nodes.end() && targetIt != nodes.end()) {
                    // Calculate link width based on value
                    float maxValue = 0;
                    for (const auto& l : links) {
                        maxValue = std::max(maxValue, l.value);
                    }

                    if (maxValue > 0) {
                        link.width = (link.value / maxValue) *
                                     std::min(sourceIt->second.height, targetIt->second.height) * 0.9f;
                    } else {
                        link.width = 10.0f;
                    }

                    link.sourceY = sourceIt->second.y + sourceIt->second.height / 2.0f;
                    link.targetY = targetIt->second.y + targetIt->second.height / 2.0f;
                }
            }

            // Adjust link positions to avoid overlaps
            AdjustLinkPositions();
        }

        void AdjustLinkPositions() {
            // Group links by source node
            std::map<std::string, std::vector<SankeyLink*>> linksBySource;
            for (auto& link : links) {
                linksBySource[link.source].push_back(&link);
            }

            // Adjust source positions
            for (auto& [nodeId, nodeLinks] : linksBySource) {
                auto nodeIt = nodes.find(nodeId);
                if (nodeIt == nodes.end()) continue;

                float totalWidth = 0;
                for (const auto* link : nodeLinks) {
                    totalWidth += link->width;
                }

                float y = nodeIt->second.y + (nodeIt->second.height - totalWidth) / 2.0f;
                for (auto* link : nodeLinks) {
                    link->sourceY = y + link->width / 2.0f;
                    y += link->width;
                }
            }

            // Group links by target node
            std::map<std::string, std::vector<SankeyLink*>> linksByTarget;
            for (auto& link : links) {
                linksByTarget[link.target].push_back(&link);
            }

            // Adjust target positions
            for (auto& [nodeId, nodeLinks] : linksByTarget) {
                auto nodeIt = nodes.find(nodeId);
                if (nodeIt == nodes.end()) continue;

                float totalWidth = 0;
                for (const auto* link : nodeLinks) {
                    totalWidth += link->width;
                }

                float y = nodeIt->second.y + (nodeIt->second.height - totalWidth) / 2.0f;
                for (auto* link : nodeLinks) {
                    link->targetY = y + link->width / 2.0f;
                    y += link->width;
                }
            }
        }

        void RelaxLeftToRight() {
            // Implement node position relaxation algorithm
            // This is simplified - full implementation would include collision detection
            for (auto& [id, node] : nodes) {
                float targetY = 0;
                float weightSum = 0;

                for (const auto& link : links) {
                    if (link.target == id) {
                        targetY += link.sourceY * link.value;
                        weightSum += link.value;
                    }
                }

                if (weightSum > 0) {
                    float newY = targetY / weightSum - node.height / 2.0f;
                    node.y = std::clamp(newY, GetY() + nodePadding,
                                        GetY() + GetHeight() - node.height - nodePadding);
                }
            }
        }

        void RelaxRightToLeft() {
            // Similar to RelaxLeftToRight but in reverse
            for (auto& [id, node] : nodes) {
                float targetY = 0;
                float weightSum = 0;

                for (const auto& link : links) {
                    if (link.source == id) {
                        targetY += link.targetY * link.value;
                        weightSum += link.value;
                    }
                }

                if (weightSum > 0) {
                    float newY = targetY / weightSum - node.height / 2.0f;
                    node.y = std::clamp(newY, GetY() + nodePadding,
                                        GetY() + GetHeight() - node.height - nodePadding);
                }
            }
        }

        // ===== DRAWING METHODS =====
        void DrawNode(IRenderContext* ctx, const SankeyNode& node) {
            // Draw node rectangle
            ctx->SetFillPaint(node.color);
            ctx->FillRectangle(node.x, node.y, nodeWidth, node.height);

            // Draw node border
            if (style.nodeStrokeWidth > 0) {
                ctx->SetStrokePaint(style.nodeStrokeColor);
                ctx->SetStrokeWidth(style.nodeStrokeWidth);
                ctx->DrawRectangle(node.x, node.y, nodeWidth, node.height);
            }

            // Draw label
            ctx->SetFillPaint(style.textColor);
            ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
            ctx->SetFontSize(style.fontSize);

            float labelX = node.x;
            float labelY = node.y + node.height / 2.0f;

            // Position label based on alignment
            if (alignment == SankeyAlignment::Left || node.depth == 0) {
                labelX = node.x - 5;
                ctx->DrawText(node.label, labelX - ctx->GetTextWidth(node.label), labelY);
            } else {
                labelX = node.x + nodeWidth + 5;
                ctx->DrawText(node.label, labelX, labelY);
            }
        }

        void DrawLink(IRenderContext* ctx, const SankeyLink& link) {
            auto sourceIt = nodes.find(link.source);
            auto targetIt = nodes.find(link.target);

            if (sourceIt == nodes.end() || targetIt == nodes.end()) return;

            float x0 = sourceIt->second.x + nodeWidth;
            float y0 = link.sourceY;
            float x1 = targetIt->second.x;
            float y1 = link.targetY;

            // Draw curved link using bezier curve
            DrawCurvedLink(ctx, x0, y0, x1, y1, link.width, link.color.WithAlpha(
                    static_cast<uint8_t>(link.opacity * 255)
            ));
        }

        void DrawCurvedLink(IRenderContext* ctx, float x0, float y0,
                            float x1, float y1, float width, const Color& color) {
            ctx->SetFillPaint(color);
            ctx->ClearPath();

            // Create path for link with thickness
            float midX = x0 + (x1 - x0) * linkCurvature;

            // Top edge of link
            ctx->MoveTo(x0, y0 - width/2);
            ctx->BezierCurveTo(midX, y0 - width/2, midX, y1 - width/2, x1, y1 - width/2);

            // Bottom edge of link
            ctx->LineTo(x1, y1 + width/2);
            ctx->BezierCurveTo(midX, y1 + width/2, midX, y0 + width/2, x0, y0 + width/2);

            ctx->ClosePath();
            ctx->Fill();
        }

        void DrawTooltip(IRenderContext* ctx) {
            if (hoveredNodeId.empty()) return;

            auto nodeIt = nodes.find(hoveredNodeId);
            if (nodeIt == nodes.end()) return;

            const auto& node = nodeIt->second;
            std::string text = node.label + "\nValue: " + std::to_string(static_cast<int>(node.value));

            float textWidth = ctx->GetTextWidth(text);
            float textHeight = style.fontSize * 2.5f;

            float tooltipX = node.x + nodeWidth + 10;
            float tooltipY = node.y + node.height / 2 - textHeight / 2;

            // Draw tooltip background
            ctx->SetFillPaint(style.tooltipBackground);
            ctx->FillRoundedRectangle(tooltipX, tooltipY,
                                      textWidth + style.tooltipPadding * 2,
                                      textHeight + style.tooltipPadding * 2, 4);

            // Draw tooltip border
            ctx->SetStrokePaint(style.tooltipBorder);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRoundedRectangle(tooltipX, tooltipY,
                                      textWidth + style.tooltipPadding * 2,
                                      textHeight + style.tooltipPadding * 2, 4);

            // Draw tooltip text
            ctx->SetFillPaint(style.textColor);
            ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
            ctx->SetFontSize(style.fontSize);
            ctx->DrawText(text, tooltipX + style.tooltipPadding,
                          tooltipY + style.tooltipPadding + style.fontSize);
        }

        // ===== EVENT HANDLERS =====
        bool HandleMouseMove(const UCEvent& event) {
            Point2D mousePos(event.x, event.y);

            // Check for dragging
            if (!draggedNodeId.empty()) {
                auto nodeIt = nodes.find(draggedNodeId);
                if (nodeIt != nodes.end()) {
                    nodeIt->second.y = mousePos.y - dragOffset.y;
                    nodeIt->second.y = std::clamp(nodeIt->second.y,
                                                  GetY() + nodePadding,
                                                  GetY() + GetHeight() - nodeIt->second.height - nodePadding);
                    ComputeLinkBreadths();
                    RequestRedraw();
                    return true;
                }
            }

            // Check for hover
            std::string oldHoveredId = hoveredNodeId;
            hoveredNodeId.clear();

            for (const auto& [id, node] : nodes) {
                if (mousePos.x >= node.x && mousePos.x <= node.x + nodeWidth &&
                    mousePos.y >= node.y && mousePos.y <= node.y + node.height) {
                    hoveredNodeId = id;
                    if (onNodeHover) onNodeHover(id);
                    break;
                }
            }

            if (oldHoveredId != hoveredNodeId) {
                RequestRedraw();
            }

            return !hoveredNodeId.empty();
        }

        bool HandleMouseDown(const UCEvent& event) {
            if (!hoveredNodeId.empty()) {
                draggedNodeId = hoveredNodeId;
                auto nodeIt = nodes.find(draggedNodeId);
                if (nodeIt != nodes.end()) {
                    dragOffset.x = event.x - nodeIt->second.x;
                    dragOffset.y = event.y - nodeIt->second.y;
                }
                return true;
            }
            return false;
        }

        bool HandleMouseUp(const UCEvent& event) {
            if (!draggedNodeId.empty()) {
                draggedNodeId.clear();
                needsLayout = false; // Keep custom position
                return true;
            }

            // Check for click
            if (!hoveredNodeId.empty() && onNodeClick) {
                onNodeClick(hoveredNodeId);
                return true;
            }

            return false;
        }

        // ===== THEME MANAGEMENT =====
        void ApplyTheme(SankeyTheme t) {
            switch (t) {
                case SankeyTheme::Energy:
                    style.backgroundColor = Color(240, 248, 255);
                    style.nodeStrokeColor = Color(70, 130, 180);
                    style.textColor = Color(25, 25, 112);
                    break;

                case SankeyTheme::Finance:
                    style.backgroundColor = Color(245, 255, 250);
                    style.nodeStrokeColor = Color(34, 139, 34);
                    style.textColor = Color(0, 100, 0);
                    break;

                case SankeyTheme::WebTraffic:
                    style.backgroundColor = Color(255, 248, 240);
                    style.nodeStrokeColor = Color(255, 140, 0);
                    style.textColor = Color(139, 69, 19);
                    break;

                case SankeyTheme::Default:
                case SankeyTheme::Custom:
                default:
                    // Use default style values
                    break;
            }
        }

        // ===== UTILITY METHODS =====
        Color GetNodeColor(size_t index) {
            static const std::vector<Color> palette = {
                    Color(31, 119, 180),   // Blue
                    Color(255, 127, 14),   // Orange
                    Color(44, 160, 44),    // Green
                    Color(214, 39, 40),    // Red
                    Color(148, 103, 189),  // Purple
                    Color(140, 86, 75),    // Brown
                    Color(227, 119, 194),  // Pink
                    Color(127, 127, 127),  // Gray
                    Color(188, 189, 34),   // Olive
                    Color(23, 190, 207)    // Cyan
            };

            return palette[index % palette.size()];
        }

        std::string GetLinkPath(float x0, float y0, float x1, float y1, float width) {
            std::stringstream ss;
            float midX = x0 + (x1 - x0) * linkCurvature;

            ss << "M " << x0 << " " << (y0 - width/2);
            ss << " C " << midX << " " << (y0 - width/2) << ", ";
            ss << midX << " " << (y1 - width/2) << ", ";
            ss << x1 << " " << (y1 - width/2);
            ss << " L " << x1 << " " << (y1 + width/2);
            ss << " C " << midX << " " << (y1 + width/2) << ", ";
            ss << midX << " " << (y0 + width/2) << ", ";
            ss << x0 << " " << (y0 + width/2);
            ss << " Z";

            return ss.str();
        }

        std::string ColorToHex(const Color& color) {
            char buffer[8];
            snprintf(buffer, sizeof(buffer), "#%02X%02X%02X",
                     color.r, color.g, color.b);
            return std::string(buffer);
        }
    };

// ===== SANKEY PLUGIN CLASS =====
//    class UltraCanvasSankey : public IGraphicsPlugin {
//    public:
//        // ===== PLUGIN INFORMATION =====
//        std::string GetPluginName() const override {
//            return "SankeyDiagram";
//        }
//
//        std::string GetPluginVersion() const override {
//            return "1.3.0";
//        }
//
//        std::vector<std::string> GetSupportedExtensions() const override {
//            return {"sankey", "flow", "csv"};
//        }
//
//        // ===== FILE HANDLING =====
//        bool CanHandle(const std::string& filePath) const override {
//            std::string ext = GetFileExtension(filePath);
//            auto extensions = GetSupportedExtensions();
//            return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
//        }
//
//        bool CanHandle(const GraphicsFileInfo& fileInfo) const override {
//            return CanHandle(fileInfo.filename);
//        }
//
//        // ===== GRAPHICS OPERATIONS =====
//        std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const std::string& filePath) override {
//            auto renderer = std::make_shared<UltraCanvasSankeyRenderer>(
//                    "sankey", GenerateUID(), 0, 0, 800, 600
//            );
//
//            if (renderer->LoadFromCSV(filePath)) {
//                return renderer;
//            }
//
//            return nullptr;
//        }
//
//        std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const GraphicsFileInfo& fileInfo) override {
//            return LoadGraphics(fileInfo.filename);
//        }
//
//        std::shared_ptr<UltraCanvasUIElement> CreateGraphics(int width, int height, GraphicsFormatType type) override {
//            return std::make_shared<UltraCanvasSankeyRenderer>(
//                    "sankey", GenerateUID(), 0, 0, width, height
//            );
//        }
//
//        // ===== CAPABILITIES =====
//        GraphicsManipulation GetSupportedManipulations() const override {
//            return GraphicsManipulation::Move |
//                   GraphicsManipulation::Resize |
//                   GraphicsManipulation::Select;
//        }
//
//        GraphicsFileInfo GetFileInfo(const std::string& filePath) override {
//            GraphicsFileInfo info;
//            info.filename = filePath;
//            info.extension = GetFileExtension(filePath);
//            info.formatType = GraphicsFormatType::Data;
//            return info;
//        }
//
//        bool ValidateFile(const std::string& filePath) override {
//            std::ifstream file(filePath);
//            return file.is_open();
//        }
//
//    private:
//        std::string GetFileExtension(const std::string& filePath) const {
//            size_t dotPos = filePath.find_last_of('.');
//            if (dotPos == std::string::npos) return "";
//            std::string ext = filePath.substr(dotPos + 1);
//            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
//            return ext;
//        }
//
//        long GenerateUID() const {
//            static long uid = 100000;
//            return ++uid;
//        }
//    };
//
//// ===== FACTORY FUNCTIONS =====
//    inline std::shared_ptr<UltraCanvasSankeyRenderer> CreateSankeyRenderer(
//            const std::string& id, long uid, long x, long y, long w, long h
//    ) {
//        return std::make_shared<UltraCanvasSankeyRenderer>(id, uid, x, y, w, h);
//    }
//
//    inline void RegisterSankeyPlugin() {
//        UltraCanvasGraphicsPluginRegistry::RegisterPlugin(
//                std::make_shared<UltraCanvasSankey>()
//        );
//    }

// ===== EXAMPLE DATA GENERATORS =====
    inline void GenerateEnergySankeyData(UltraCanvasSankeyRenderer* renderer) {
        // Energy flow example
        renderer->AddLink("Coal", "Electricity", 45.0f);
        renderer->AddLink("Natural Gas", "Electricity", 35.0f);
        renderer->AddLink("Nuclear", "Electricity", 20.0f);
//        renderer->AddLink("Solar", "Electricity", 5.0f);
//        renderer->AddLink("Wind", "Electricity", 8.0f);
//        renderer->AddLink("Hydro", "Electricity", 7.0f);

        renderer->AddLink("Electricity", "Residential", 40.0f);
        renderer->AddLink("Electricity", "Commercial", 35.0f);
        renderer->AddLink("Electricity", "Industrial", 45.0f);

        renderer->AddLink("Natural Gas", "Residential Heating", 15.0f);
        renderer->AddLink("Natural Gas", "Commercial Heating", 10.0f);

        renderer->SetTheme(SankeyTheme::Energy);
    }

    inline void GenerateFinanceSankeyData(UltraCanvasSankeyRenderer* renderer) {
        // Financial flow example
//        renderer->AddLink("Revenue", "Product Sales", 65.0f);
//        renderer->AddLink("Revenue", "Services", 35.0f);
//
//        renderer->AddLink("Product Sales", "Profit", 20.0f);
//        renderer->AddLink("Product Sales", "Manufacturing", 30.0f);
//        renderer->AddLink("Product Sales", "Marketing", 15.0f);
//
//        renderer->AddLink("Services", "Profit", 15.0f);
//        renderer->AddLink("Services", "Operations", 10.0f);
//        renderer->AddLink("Services", "Support", 10.0f);
//
//        renderer->AddLink("Profit", "Dividends", 15.0f);
//        renderer->AddLink("Profit", "R&D", 10.0f);
//        renderer->AddLink("Profit", "Reserves", 10.0f);
//
//        renderer->SetTheme(SankeyTheme::Finance);
        renderer->AddLink("Coal", "Electricity", 45.0f);
        renderer->AddLink("Natural Gas", "Electricity", 35.0f);
        renderer->AddLink("Nuclear", "Electricity", 20.0f);

        renderer->AddLink("Natural Gas", "Residential Heating", 15.0f);
        renderer->AddLink("Natural Gas", "Commercial Heating", 10.0f);
//        renderer->AddLink("Solar", "Electricity", 5.0f);
//        renderer->AddLink("Wind", "Electricity", 8.0f);
//        renderer->AddLink("Hydro", "Electricity", 7.0f);

        renderer->AddLink("Electricity", "Residential", 40.0f);
        renderer->AddLink("Electricity", "Commercial", 35.0f);
        renderer->AddLink("Electricity", "Industrial", 45.0f);

        renderer->SetTheme(SankeyTheme::Energy);
    }

    inline void GenerateWebTrafficSankeyData(UltraCanvasSankeyRenderer* renderer) {
        // Web traffic flow example

        // Clear existing data
        renderer->ClearAll();

        // 2020 Energy Sources
        renderer->AddLink("2020 Coal", "Fossil Phase-Out", 3800.0f);
        renderer->AddLink("2020 Natural Gas", "Transition Fuel", 3200.0f);
        renderer->AddLink("2020 Oil", "Fossil Phase-Out", 4100.0f);
        renderer->AddLink("2020 Nuclear", "Stable Nuclear", 700.0f);
        renderer->AddLink("2020 Hydro", "Renewable Growth", 1200.0f);
        renderer->AddLink("2020 Wind", "Renewable Growth", 600.0f);
        renderer->AddLink("2020 Solar", "Renewable Growth", 400.0f);
        renderer->AddLink("2020 Other Renewables", "Renewable Growth", 300.0f);

        // Transition pathways
        renderer->AddLink("Fossil Phase-Out", "2030 Reduced Fossil", 4500.0f);
        renderer->AddLink("Fossil Phase-Out", "Carbon Capture", 2400.0f);
        renderer->AddLink("Fossil Phase-Out", "Decommissioned", 1000.0f);

        renderer->AddLink("Transition Fuel", "2030 Natural Gas", 2800.0f);
        renderer->AddLink("Transition Fuel", "Hydrogen Production", 400.0f);

        renderer->AddLink("Renewable Growth", "2030 Wind Expansion", 2000.0f);
        renderer->AddLink("Renewable Growth", "2030 Solar Expansion", 1800.0f);
        renderer->AddLink("Renewable Growth", "2030 Hydro Stable", 1200.0f);
        renderer->AddLink("Renewable Growth", "2030 New Tech", 500.0f);

        renderer->AddLink("Stable Nuclear", "2030 Nuclear", 700.0f);

        // 2030 to 2040 transition
        renderer->AddLink("2030 Reduced Fossil", "2040 Minimal Fossil", 2000.0f);
        renderer->AddLink("2030 Reduced Fossil", "CCS Equipped", 2500.0f);

        renderer->AddLink("2030 Natural Gas", "2040 Green Hydrogen", 1500.0f);
        renderer->AddLink("2030 Natural Gas", "2040 Gas with CCS", 1300.0f);

        renderer->AddLink("2030 Wind Expansion", "2040 Wind Dominant", 3500.0f);
        renderer->AddLink("2030 Solar Expansion", "2040 Solar Dominant", 3200.0f);
        renderer->AddLink("2030 Hydro Stable", "2040 Hydro", 1200.0f);
        renderer->AddLink("2030 New Tech", "2040 Advanced Storage", 1000.0f);
        renderer->AddLink("2030 Nuclear", "2040 Next Gen Nuclear", 900.0f);

        // 2040 to 2050 final transition
        renderer->AddLink("2040 Minimal Fossil", "2050 Zero Carbon", 500.0f);
        renderer->AddLink("2040 Minimal Fossil", "2050 CCS Only", 1500.0f);

        renderer->AddLink("2040 Green Hydrogen", "2050 Hydrogen Economy", 2000.0f);
        renderer->AddLink("2040 Gas with CCS", "2050 CCS Only", 1300.0f);

        renderer->AddLink("2040 Wind Dominant", "2050 Wind Power", 4500.0f);
        renderer->AddLink("2040 Solar Dominant", "2050 Solar Power", 4200.0f);
        renderer->AddLink("2040 Hydro", "2050 Hydro Power", 1200.0f);
        renderer->AddLink("2040 Advanced Storage", "2050 Grid Storage", 1500.0f);
        renderer->AddLink("2040 Next Gen Nuclear", "2050 Nuclear Fusion", 1200.0f);

        renderer->AddLink("CCS Equipped", "2050 CCS Only", 2500.0f);
        renderer->AddLink("Carbon Capture", "2050 CCS Only", 2400.0f);
        renderer->AddLink("Hydrogen Production", "2050 Hydrogen Economy", 400.0f);

        // Final distribution to sectors
        renderer->AddLink("2050 Wind Power", "Industry 2050", 2000.0f);
        renderer->AddLink("2050 Wind Power", "Residential 2050", 1500.0f);
        renderer->AddLink("2050 Wind Power", "Transport 2050", 1000.0f);

        renderer->AddLink("2050 Solar Power", "Residential 2050", 2000.0f);
        renderer->AddLink("2050 Solar Power", "Industry 2050", 1500.0f);
        renderer->AddLink("2050 Solar Power", "Commercial 2050", 700.0f);

        renderer->AddLink("2050 Hydrogen Economy", "Transport 2050", 1500.0f);
        renderer->AddLink("2050 Hydrogen Economy", "Industry 2050", 900.0f);

        renderer->AddLink("2050 Nuclear Fusion", "Industry 2050", 800.0f);
        renderer->AddLink("2050 Nuclear Fusion", "Commercial 2050", 400.0f);

        renderer->AddLink("2050 Hydro Power", "Residential 2050", 600.0f);
        renderer->AddLink("2050 Hydro Power", "Industry 2050", 600.0f);

        renderer->AddLink("2050 Grid Storage", "All Sectors 2050", 1500.0f);
        renderer->AddLink("2050 CCS Only", "Industry 2050", 5700.0f);

        renderer->SetTheme(SankeyTheme::Energy);
        renderer->SetAlignment(SankeyAlignment::Justify);
        renderer->SetLinkCurvature(0.5f);
        renderer->SetNodeWidth(20.0f);
        renderer->SetIterations(50); // More iterations for complex flow

//        renderer->AddLink("Search", "Homepage", 45.0f);
//        renderer->AddLink("Social Media", "Homepage", 30.0f);
//        renderer->AddLink("Direct", "Homepage", 25.0f);
//        renderer->AddLink("Referral", "Homepage", 15.0f);
//
//        renderer->AddLink("Homepage", "Product Page", 50.0f);
//        renderer->AddLink("Homepage", "About", 20.0f);
//        renderer->AddLink("Homepage", "Blog", 25.0f);
//        renderer->AddLink("Homepage", "Exit", 20.0f);
//
//        renderer->AddLink("Product Page", "Checkout", 30.0f);
//        renderer->AddLink("Product Page", "Exit", 20.0f);
//
//        renderer->AddLink("Checkout", "Purchase", 25.0f);
//        renderer->AddLink("Checkout", "Exit", 5.0f);
//
//        renderer->SetTheme(SankeyTheme::WebTraffic);
    }



} // namespace UltraCanvas