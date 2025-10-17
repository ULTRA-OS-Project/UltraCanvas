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
            nodeWidth = 15.0f;  // Slightly thinner to give more space for labels
            nodePadding = 8.0f;
            linkCurvature = 0.5f;
            iterations = 32;
            alignment = SankeyAlignment::Justify;
            theme = SankeyTheme::Default;
            enableAnimation = true;
            enableTooltips = true;
            maxLabelWidth = 200.0f;  // Maximum width for labels
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

                if (std::getline(ss, source, ',') &&
                    std::getline(ss, target, ',') &&
                    std::getline(ss, valueStr, ',')) {

                    try {
                        float value = std::stof(valueStr);
                        AddLink(source, target, value);
                    } catch (...) {
                        // Skip malformed lines
                    }
                }
            }

            file.close();
            RequestRedraw();
            return true;
        }

        bool SaveToSVG(const std::string& filePath) {
            std::ofstream file(filePath);
            if (!file.is_open()) return false;

            auto bounds = GetBounds();
            file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            file << "<svg xmlns=\"http://www.w3.org/2000/svg\" ";
            file << "width=\"" << bounds.width << "\" ";
            file << "height=\"" << bounds.height << "\">\n";

            // Write links
            for (const auto& link : links) {
                auto sourceIt = nodes.find(link.source);
                auto targetIt = nodes.find(link.target);
                if (sourceIt != nodes.end() && targetIt != nodes.end()) {
                    float x0 = sourceIt->second.x + nodeWidth;
                    float y0 = link.sourceY;
                    float x1 = targetIt->second.x;
                    float y1 = link.targetY;
                    float midX = x0 + (x1 - x0) * linkCurvature;

                    file << "<path d=\"M" << x0 << "," << (y0 - link.width/2);
                    file << " C" << midX << "," << (y0 - link.width/2);
                    file << " " << midX << "," << (y1 - link.width/2);
                    file << " " << x1 << "," << (y1 - link.width/2);
                    file << " L" << x1 << "," << (y1 + link.width/2);
                    file << " C" << midX << "," << (y1 + link.width/2);
                    file << " " << midX << "," << (y0 + link.width/2);
                    file << " " << x0 << "," << (y0 + link.width/2);
                    file << " Z\" ";
                    file << "fill=\"" << link.color.ToHexString() << "\" ";
                    file << "opacity=\"" << link.opacity << "\"/>\n";
                }
            }

            // Write nodes
            for (const auto& [id, node] : nodes) {
                file << "<rect x=\"" << node.x << "\" ";
                file << "y=\"" << node.y << "\" ";
                file << "width=\"" << nodeWidth << "\" ";
                file << "height=\"" << node.height << "\" ";
                file << "fill=\"" << node.color.ToHexString() << "\"/>\n";

                // Write labels
                float labelY = node.y + node.height / 2.0f;
                std::string anchor;
                float labelX;

                // Check if this is a terminal node
                bool isTerminal = true;
                for (const auto& link : links) {
                    if (link.source == node.id) {
                        isTerminal = false;
                        break;
                    }
                }

                if (node.depth == 0) {
                    labelX = node.x - 8;
                    anchor = "end";
                } else if (isTerminal) {
                    labelX = node.x + nodeWidth + 8;
                    anchor = "start";
                } else {
                    // Intermediate nodes
                    labelX = (alignment == SankeyAlignment::Left) ?
                             node.x - 8 : node.x + nodeWidth + 8;
                    anchor = (alignment == SankeyAlignment::Left) ? "end" : "start";
                }

                file << "<text x=\"" << labelX << "\" ";
                file << "y=\"" << labelY << "\" ";
                file << "text-anchor=\"" << anchor << "\" ";
                file << "dominant-baseline=\"middle\" ";
                file << "font-family=\"Arial\" font-size=\"12\">";
                file << node.label << "</text>\n";
            }

            file << "</svg>\n";
            file.close();
            return true;
        }

        // ===== LAYOUT ALGORITHM =====
        void PerformLayout() {
            if (nodes.empty() || links.empty()) return;

            ComputeNodeDepths();
            ComputeNodeValues();
            ComputeNodeBreadths();
            ComputeLinkBreadths();

            // Iterative relaxation
            for (int i = 0; i < iterations; ++i) {
                RelaxRightToLeft();
                RelaxLeftToRight();
                ComputeLinkBreadths();  // Recompute after relaxation
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

        void SetFontSize(float size) {
            style.fontSize = size;
            needsLayout = true;  // Need to recalculate padding
            RequestRedraw();
        }

        void SetFontFamily(const std::string& family) {
            style.fontFamily = family;
            needsLayout = true;  // Need to recalculate padding
            RequestRedraw();
        }

        void SetMaxLabelWidth(float width) {
            maxLabelWidth = width;
            needsLayout = true;
            RequestRedraw();
        }

        float GetMaxLabelWidth() const {
            return maxLabelWidth;
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
        float maxLabelWidth;

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

        void ComputeNodeValues() {
            // Calculate node values based on incoming and outgoing flows
            for (auto& [id, node] : nodes) {
                float incomingValue = 0;
                float outgoingValue = 0;

                for (const auto& link : links) {
                    if (link.target == id) {
                        incomingValue += link.value;
                    }
                    if (link.source == id) {
                        outgoingValue += link.value;
                    }
                }

                // Node value is the maximum of incoming or outgoing flow
                node.value = std::max(incomingValue, outgoingValue);
                if (node.value == 0) {
                    node.value = 10.0f; // Default minimum value
                }
            }
        }

        void ComputeNodeBreadths() {
            auto bounds = GetBounds();

            // Find max depth
            int maxDepth = 0;
            for (const auto& [id, node] : nodes) {
                maxDepth = std::max(maxDepth, node.depth);
            }

            // Group nodes by depth
            std::map<int, std::vector<std::string>> nodesByDepth;
            for (const auto& [id, node] : nodes) {
                nodesByDepth[node.depth].push_back(id);
            }

            // Calculate required padding for labels
            float leftPadding = nodePadding;
            float rightPadding = nodePadding;

            // Get render context for text measurement
            IRenderContext* ctx = GetRenderContext();
            if (ctx) {
                ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
                ctx->SetFontSize(style.fontSize);

                // Measure left side labels (source nodes at depth 0)
                for (const auto& [id, node] : nodes) {
                    if (node.depth == 0) {
                        float textWidth = ctx->GetTextWidth(node.label);
                        textWidth = std::min(textWidth, maxLabelWidth);
                        leftPadding = std::max(leftPadding, textWidth + 15.0f);
                    }
                }

                // Measure right side labels (terminal nodes)
                for (const auto& [id, node] : nodes) {
                    bool isTerminal = true;
                    for (const auto& link : links) {
                        if (link.source == id) {
                            isTerminal = false;
                            break;
                        }
                    }
                    if (isTerminal) {
                        float textWidth = ctx->GetTextWidth(node.label);
                        textWidth = std::min(textWidth, maxLabelWidth);
                        rightPadding = std::max(rightPadding, textWidth + 15.0f);
                    }
                }
            } else {
                // Fallback if no context available
                leftPadding = 100.0f;
                rightPadding = 100.0f;
            }

            // Calculate horizontal spacing between columns
            float availableWidth = bounds.width - nodeWidth - leftPadding - rightPadding;
            float xStep = availableWidth / std::max(1, maxDepth);

            // Ensure minimum spacing between columns
            xStep = std::max(xStep, nodeWidth + 20.0f);

            // Calculate the maximum total value across all columns
            float maxColumnValue = 0;
            for (const auto& [depth, nodeIds] : nodesByDepth) {
                float columnValue = 0;
                for (const auto& id : nodeIds) {
                    columnValue += nodes[id].value;
                }
                maxColumnValue = std::max(maxColumnValue, columnValue);
            }

            // Use available height minus padding for nodes
            float availableHeight = bounds.height - 2 * nodePadding;

            // Position nodes at each depth
            for (auto& [depth, nodeIds] : nodesByDepth) {
                float x = bounds.x + leftPadding + depth * xStep;

                // Calculate total value for this depth
                float totalValue = 0;
                for (const auto& id : nodeIds) {
                    totalValue += nodes[id].value;
                }

                // Calculate padding between nodes in this column
                float columnPadding = (nodeIds.size() > 1) ?
                                      nodePadding * (nodeIds.size() - 1) : 0;

                // Available height for actual nodes (excluding padding between them)
                float nodeAreaHeight = availableHeight - columnPadding;

                // Scaling factor - use global maximum for consistent scaling
                float scale = nodeAreaHeight / maxColumnValue;

                // Calculate actual height used by this column
                float columnHeight = totalValue * scale + columnPadding;

                // Center the column vertically
                float y = bounds.y + nodePadding + (availableHeight - columnHeight) / 2.0f;

                // Position each node in the column
                for (const auto& id : nodeIds) {
                    auto& node = nodes[id];
                    node.x = x;
                    node.y = y;
                    node.width = nodeWidth;
                    node.height = std::max(1.0f, node.value * scale);
                    y += node.height + nodePadding;
                }
            }
        }

        void ComputeLinkBreadths() {
            // Reset link widths based on their values
            float maxLinkValue = 0;
            for (const auto& link : links) {
                maxLinkValue = std::max(maxLinkValue, link.value);
            }

            // Group links by source and target nodes
            std::map<std::string, std::vector<SankeyLink*>> linksBySource;
            std::map<std::string, std::vector<SankeyLink*>> linksByTarget;

            for (auto& link : links) {
                linksBySource[link.source].push_back(&link);
                linksByTarget[link.target].push_back(&link);
            }

            // Calculate link widths and positions for each node
            for (auto& [nodeId, nodeLinks] : linksBySource) {
                auto nodeIt = nodes.find(nodeId);
                if (nodeIt == nodes.end()) continue;

                const auto& node = nodeIt->second;

                // Sort links by target depth for better visual flow
                std::sort(nodeLinks.begin(), nodeLinks.end(),
                          [this](const SankeyLink* a, const SankeyLink* b) {
                              auto targetA = nodes.find(a->target);
                              auto targetB = nodes.find(b->target);
                              if (targetA != nodes.end() && targetB != nodes.end()) {
                                  return targetA->second.y < targetB->second.y;
                              }
                              return false;
                          });

                // Calculate total outgoing flow
                float totalFlow = 0;
                for (const auto* link : nodeLinks) {
                    totalFlow += link->value;
                }

                // Scale factor for link widths
                float scale = (totalFlow > 0) ? node.height / totalFlow : 0;

                // Position links along the source node
                float y = node.y;
                for (auto* link : nodeLinks) {
                    link->width = link->value * scale;
                    link->sourceY = y + link->width / 2.0f;
                    y += link->width;
                }
            }

            // Position links along target nodes
            for (auto& [nodeId, nodeLinks] : linksByTarget) {
                auto nodeIt = nodes.find(nodeId);
                if (nodeIt == nodes.end()) continue;

                const auto& node = nodeIt->second;

                // Sort links by source depth
                std::sort(nodeLinks.begin(), nodeLinks.end(),
                          [this](const SankeyLink* a, const SankeyLink* b) {
                              auto sourceA = nodes.find(a->source);
                              auto sourceB = nodes.find(b->source);
                              if (sourceA != nodes.end() && sourceB != nodes.end()) {
                                  return sourceA->second.y < sourceB->second.y;
                              }
                              return false;
                          });

                // Calculate total incoming flow
                float totalFlow = 0;
                for (const auto* link : nodeLinks) {
                    totalFlow += link->value;
                }

                // Scale factor for link widths
                float scale = (totalFlow > 0) ? node.height / totalFlow : 0;

                // Position links along the target node
                float y = node.y;
                for (auto* link : nodeLinks) {
                    // Update width to match target scaling if needed
                    float targetWidth = link->value * scale;
                    link->width = std::min(link->width, targetWidth);
                    link->targetY = y + link->width / 2.0f;
                    y += link->width;
                }
            }
        }

        void RelaxLeftToRight() {
            // Group nodes by depth
            std::map<int, std::vector<std::string>> nodesByDepth;
            for (const auto& [id, node] : nodes) {
                nodesByDepth[node.depth].push_back(id);
            }

            // Relax from left to right
            for (auto& [depth, nodeIds] : nodesByDepth) {
                if (depth == 0) continue; // Skip source nodes

                for (const auto& nodeId : nodeIds) {
                    auto& node = nodes[nodeId];

                    float targetY = 0;
                    float weightSum = 0;

                    // Calculate weighted center based on incoming links
                    for (const auto& link : links) {
                        if (link.target == nodeId) {
                            targetY += link.sourceY * link.value;
                            weightSum += link.value;
                        }
                    }

                    if (weightSum > 0) {
                        float newY = targetY / weightSum - node.height / 2.0f;
                        auto bounds = GetBounds();
                        node.y = std::clamp(newY,
                                            bounds.y + nodePadding,
                                            bounds.y + bounds.height - node.height - nodePadding);
                    }
                }

                // Resolve collisions within this depth
                ResolveCollisions(nodeIds);
            }
        }

        void RelaxRightToLeft() {
            // Group nodes by depth
            std::map<int, std::vector<std::string>> nodesByDepth;
            for (const auto& [id, node] : nodes) {
                nodesByDepth[node.depth].push_back(id);
            }

            // Find max depth
            int maxDepth = 0;
            for (const auto& [depth, nodeIds] : nodesByDepth) {
                maxDepth = std::max(maxDepth, depth);
            }

            // Relax from right to left
            for (int depth = maxDepth - 1; depth >= 0; --depth) {
                auto it = nodesByDepth.find(depth);
                if (it == nodesByDepth.end()) continue;

                for (const auto& nodeId : it->second) {
                    auto& node = nodes[nodeId];

                    float targetY = 0;
                    float weightSum = 0;

                    // Calculate weighted center based on outgoing links
                    for (const auto& link : links) {
                        if (link.source == nodeId) {
                            targetY += link.targetY * link.value;
                            weightSum += link.value;
                        }
                    }

                    if (weightSum > 0) {
                        float newY = targetY / weightSum - node.height / 2.0f;
                        auto bounds = GetBounds();
                        node.y = std::clamp(newY,
                                            bounds.y + nodePadding,
                                            bounds.y + bounds.height - node.height - nodePadding);
                    }
                }

                // Resolve collisions within this depth
                ResolveCollisions(it->second);
            }
        }

        void ResolveCollisions(const std::vector<std::string>& nodeIds) {
            if (nodeIds.size() <= 1) return;

            // Sort nodes by Y position
            std::vector<std::string> sortedIds = nodeIds;
            std::sort(sortedIds.begin(), sortedIds.end(),
                      [this](const std::string& a, const std::string& b) {
                          return nodes[a].y < nodes[b].y;
                      });

            // Push overlapping nodes apart
            for (size_t i = 1; i < sortedIds.size(); ++i) {
                auto& prevNode = nodes[sortedIds[i-1]];
                auto& currNode = nodes[sortedIds[i]];

                float minY = prevNode.y + prevNode.height + nodePadding;
                if (currNode.y < minY) {
                    currNode.y = minY;
                }
            }

            // Ensure nodes stay within bounds
            auto bounds = GetBounds();
            float maxY = bounds.y + bounds.height - nodePadding;

            for (auto it = sortedIds.rbegin(); it != sortedIds.rend(); ++it) {
                auto& node = nodes[*it];
                if (node.y + node.height > maxY) {
                    node.y = maxY - node.height;
                    maxY = node.y - nodePadding;
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

            float labelY = node.y + node.height / 2.0f;

            // Position label based on node depth
            if (node.depth == 0) {
                // Left-aligned labels for source nodes
                float labelX = node.x - 8;
                float textWidth = ctx->GetTextWidth(node.label);
                ctx->DrawText(node.label, labelX - textWidth, labelY);
            } else {
                // Check if this is a terminal node (no outgoing links)
                bool isTerminal = true;
                for (const auto& link : links) {
                    if (link.source == node.id) {
                        isTerminal = false;
                        break;
                    }
                }

                if (isTerminal) {
                    // Right-aligned labels for terminal nodes
                    float labelX = node.x + nodeWidth + 8;
                    ctx->DrawText(node.label, labelX, labelY);
                } else {
                    // For intermediate nodes, position based on alignment preference
                    if (alignment == SankeyAlignment::Left) {
                        float labelX = node.x - 8;
                        float textWidth = ctx->GetTextWidth(node.label);
                        ctx->DrawText(node.label, labelX - textWidth, labelY);
                    } else {
                        float labelX = node.x + nodeWidth + 8;
                        ctx->DrawText(node.label, labelX, labelY);
                    }
                }
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
                    auto bounds = GetBounds();
                    nodeIt->second.y = std::clamp(nodeIt->second.y,
                                                  bounds.y + nodePadding,
                                                  bounds.y + bounds.height - nodeIt->second.height - nodePadding);
                    ComputeLinkBreadths();
                    RequestRedraw();
                }
                return true;
            }

            // Check for node hover
            std::string newHoveredNodeId;
            for (const auto& [id, node] : nodes) {
                if (mousePos.x >= node.x && mousePos.x <= node.x + nodeWidth &&
                    mousePos.y >= node.y && mousePos.y <= node.y + node.height) {
                    newHoveredNodeId = id;
                    break;
                }
            }

            if (newHoveredNodeId != hoveredNodeId) {
                hoveredNodeId = newHoveredNodeId;
                if (!hoveredNodeId.empty() && onNodeHover) {
                    onNodeHover(hoveredNodeId);
                }
                RequestRedraw();
            }

            return !hoveredNodeId.empty();
        }

        bool HandleMouseDown(const UCEvent& event) {
            Point2D mousePos(event.x, event.y);

            // Check if clicking on a node
            for (const auto& [id, node] : nodes) {
                if (mousePos.x >= node.x && mousePos.x <= node.x + nodeWidth &&
                    mousePos.y >= node.y && mousePos.y <= node.y + node.height) {

                    if (onNodeClick) {
                        onNodeClick(id);
                    }

                    // Start dragging
                    draggedNodeId = id;
                    dragOffset.x = mousePos.x - node.x;
                    dragOffset.y = mousePos.y - node.y;

                    return true;
                }
            }

            return false;
        }

        bool HandleMouseUp(const UCEvent& event) {
            if (!draggedNodeId.empty()) {
                draggedNodeId.clear();
                needsLayout = true; // Trigger full layout after drag
                RequestRedraw();
                return true;
            }
            return false;
        }

        // ===== HELPER METHODS =====
        Color GetNodeColor(size_t index) {
            static const std::vector<Color> palette = {
                    Color(141, 211, 199),  // Teal
                    Color(255, 255, 179),  // Light Yellow
                    Color(190, 186, 218),  // Lavender
                    Color(251, 128, 114),  // Salmon
                    Color(128, 177, 211),  // Sky Blue
                    Color(253, 180, 98),   // Orange
                    Color(179, 222, 105),  // Light Green
                    Color(252, 205, 229),  // Pink
                    Color(217, 217, 217),  // Light Gray
                    Color(188, 128, 189),  // Purple
                    Color(204, 235, 197)   // Mint
            };
            return palette[index % palette.size()];
        }

        void ApplyTheme(SankeyTheme t) {
            switch (t) {
                case SankeyTheme::Energy:
                    style.backgroundColor = Color(240, 248, 255);
                    style.nodeStrokeColor = Colors::DarkBlue;
                    style.textColor = Colors::DarkBlue;
                    break;
                case SankeyTheme::Finance:
                    style.backgroundColor = Color(245, 245, 240);
                    style.nodeStrokeColor = Colors::DarkGreen;
                    style.textColor = Colors::DarkGreen;
                    break;
                case SankeyTheme::WebTraffic:
                    style.backgroundColor = Color(250, 250, 250);
                    style.nodeStrokeColor = Colors::Gray;
                    style.textColor = Colors::DarkGray;
                    break;
                default:
                    // Keep default theme
                    break;
            }
        }
    };

//// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasSankeyRenderer> CreateSankeyRenderer(
            const std::string& id, long uid, long x, long y, long w, long h
    ) {
        return std::make_shared<UltraCanvasSankeyRenderer>(id, uid, x, y, w, h);
    }

// ===== EXAMPLE DATA GENERATORS =====
    inline void GenerateEnergySankeyData(UltraCanvasSankeyRenderer* renderer) {
        // Energy flow example
        renderer->AddLink("Coal", "Electricity", 35.0f);
        renderer->AddLink("Natural Gas", "Electricity", 35.0f);
        renderer->AddLink("Nuclear", "Electricity", 10.0f);
        renderer->AddLink("Solar", "Electricity", 3.0f);
        renderer->AddLink("Wind", "Electricity", 2.0f);
        renderer->AddLink("Hydro", "Electricity", 5.0f);

        renderer->AddLink("Electricity", "Residential", 40.0f);
        renderer->AddLink("Electricity", "Commercial", 35.0f);
        renderer->AddLink("Electricity", "Industrial", 45.0f);

        renderer->AddLink("Natural Gas", "Residential Heating", 15.0f);
        renderer->AddLink("Natural Gas", "Commercial Heating", 10.0f);

        renderer->SetTheme(SankeyTheme::Energy);
    }

    inline void GenerateFinanceSankeyData(UltraCanvasSankeyRenderer* renderer) {
        // Financial flow example
        renderer->AddLink("Revenue", "Product Sales", 65.0f);
        renderer->AddLink("Revenue", "Services", 35.0f);

        renderer->AddLink("Product Sales", "Profit", 20.0f);
        renderer->AddLink("Product Sales", "Manufacturing", 30.0f);
        renderer->AddLink("Product Sales", "Marketing", 15.0f);

        renderer->AddLink("Services", "Profit", 15.0f);
        renderer->AddLink("Services", "Operations", 10.0f);
        renderer->AddLink("Services", "Support", 10.0f);

        renderer->AddLink("Profit", "Dividends", 15.0f);
        renderer->AddLink("Profit", "R&D", 10.0f);
        renderer->AddLink("Profit", "Reserves", 10.0f);

        renderer->SetTheme(SankeyTheme::Finance);
    }

    inline void GenerateWebTrafficSankeyData(UltraCanvasSankeyRenderer* renderer) {
        // Web traffic flow example

        // Clear existing data
        renderer->ClearAll();

        renderer->AddLink("Search", "Homepage", 30.0f);
        renderer->AddLink("Social Media", "Homepage", 30.0f);
        renderer->AddLink("Direct", "Homepage", 25.0f);
        renderer->AddLink("Referral", "Homepage", 25.0f);

        renderer->AddLink("Homepage", "Product Page", 50.0f);
        renderer->AddLink("Homepage", "About", 20.0f);
        renderer->AddLink("Homepage", "Blog", 20.0f);
        renderer->AddLink("Homepage", "Exit", 20.0f);

        renderer->AddLink("Product Page", "Checkout", 30.0f);
        renderer->AddLink("Product Page", "Exit", 20.0f);

        renderer->AddLink("Checkout", "Purchase", 25.0f);
        renderer->AddLink("Checkout", "Exit", 5.0f);

        renderer->SetTheme(SankeyTheme::WebTraffic);
    }
} // namespace UltraCanvas