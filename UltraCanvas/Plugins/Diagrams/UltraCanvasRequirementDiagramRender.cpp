// Plugins/Diagrams/UltraCanvasRequirementDiagramRender.cpp
// UltraCanvasRequirementDiagram - rendering, events, editing and serialization
// Version: 3.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasRequirementDiagram.h"
#include "DataFormats/UltraCanvasJSON.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// FRAME ENTRY POINT
// =============================================================================

void UltraCanvasRequirementDiagram::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx || !IsVisible()) return;

    // Text metrics are only available with a live context, so the measurement
    // and any pending layout run at the start of the frame.
    if (measurementDirty) {
        MeasureAllNodes(ctx);
        if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
    }
    if (layoutDirty) {
        if (layoutMode == RequirementLayoutMode::ContainmentTree) {
            ApplyContainmentTreeLayout();
            if (autoFitOnLayout) fitPending = true;
        } else if (layoutMode == RequirementLayoutMode::Layered) {
            ApplyLayeredLayout();
            if (autoFitOnLayout) fitPending = true;
        } else if (layoutMode == RequirementLayoutMode::ForceDirected) {
            ApplyForceDirectedLayout();
            if (autoFitOnLayout) fitPending = true;
        }
    }
    layoutDirty = false;
    if (fitPending) {
        fitPending = false;   // cleared first: FitView() re-arms it only while
        FitView();            // the measurement is still dirty, which it isn't
    }

    // A copy goes stale when its master is edited, and nothing tells the view
    // that happened - so the flags are refreshed here rather than relying on
    // the caller having run Validate().
    if (suspectBadge) model.RefreshSuspectFlags();

    ctx->SetFillPaint(palette.backgroundColor);
    ctx->FillRectangle(GetLocalBounds());

    ctx->PushState();
    ctx->Translate(panOffset.x, panOffset.y);
    ctx->Scale(zoomLevel, zoomLevel);

    if (style.showGrid) RenderGrid(ctx);
    if (packageRegionConfig.visible) RenderPackageRegions(ctx);

    RenderContainmentBuses(ctx);
    RenderRelations(ctx);
    RenderNoteLeaders(ctx);
    RenderNodes(ctx);
    RenderCollapseToggles(ctx);
    if (coverageOverlay) RenderCoverageBadges(ctx);
    if (rationaleNotes) RenderRationaleNotes(ctx);
    RenderCallouts(ctx);
    if (isConnecting) RenderConnectionPreview(ctx);
    if (isSelectingBox) RenderSelectionBox(ctx);
    if (!renamingNodeId.empty()) RenderRenameEditor(ctx);

    ctx->PopState();

    // Screen-space overlays.
    if (frameConfig.visible) RenderFrame(ctx);
    if (titleConfig.visible) RenderTitle(ctx);
    if (legendConfig.visible) RenderLegend(ctx);
    if (minimapConfig.visible) RenderMinimap(ctx);
    if (controlsConfig.visible) RenderControls(ctx);
    (void)dirtyRect;
}

void UltraCanvasRequirementDiagram::RenderGrid(IRenderContext* ctx) {
    ctx->SetStrokePaint(palette.gridColor);
    ctx->SetStrokeWidth(1.0 / std::max(0.0001, zoomLevel));

    const double spacing = style.gridSpacing;
    if (spacing <= 0.0) return;

    const Point2Dd topLeft = ScreenToWorld(Point2Di(0, 0));
    const Point2Dd bottomRight = ScreenToWorld(
        Point2Di(static_cast<int>(GetWidth()), static_cast<int>(GetHeight())));

    for (double x = std::floor(topLeft.x / spacing) * spacing; x <= bottomRight.x; x += spacing) {
        ctx->DrawLine(Point2Dd(x, topLeft.y), Point2Dd(x, bottomRight.y));
    }
    for (double y = std::floor(topLeft.y / spacing) * spacing; y <= bottomRight.y; y += spacing) {
        ctx->DrawLine(Point2Dd(topLeft.x, y), Point2Dd(bottomRight.x, y));
    }
}

// =============================================================================
// RELATIONSHIPS
// =============================================================================

void UltraCanvasRequirementDiagram::RenderContainmentBuses(IRenderContext* ctx) {
    // All children of one parent share a single spine - one drop from the
    // parent, one horizontal run, one riser per child. This is the SysML
    // containment notation and is why containment is not drawn per relation.
    const bool vertical = (orientation == RequirementOrientation::TopDown ||
                           orientation == RequirementOrientation::BottomUp);

    std::vector<std::string> parents;
    std::map<std::string, std::vector<const RequirementRelation*>> byParent;
    for (const auto& r : model.GetRelations()) {
        if (r.kind != RequirementRelationKind::Containment || !r.visible) continue;
        const RequirementNode* parent = model.GetNode(r.sourceId);
        const RequirementNode* child = model.GetNode(r.targetId);
        if (!parent || !child || !IsDisplayed(*parent) || !IsDisplayed(*child)) continue;
        if (byParent.find(r.sourceId) == byParent.end()) parents.push_back(r.sourceId);
        byParent[r.sourceId].push_back(&r);
    }

    for (const auto& parentId : parents) {
        const RequirementNode* parent = model.GetNode(parentId);
        if (!parent) continue;

        // Selected buses are stroked in the selection colour on a second pass.
        for (bool selectedPass : {false, true}) {
            std::vector<const RequirementRelation*> group;
            for (const auto* r : byParent[parentId]) {
                if (r->isSelected == selectedPass) group.push_back(r);
            }
            if (group.empty()) continue;

            const bool dimmed = parent->dimmed && !selectedPass;
            const Color color = ApplyDim(
                selectedPass ? palette.selectionColor
                             : palette.ColorForRelation(RequirementRelationKind::Containment),
                dimmed);
            const double width = selectedPass ? style.selectionWidth : style.relationLineWidth;

            ctx->SetLineDash(UCDashPattern::EMPTY);
            ctx->SetStrokePaint(color);
            ctx->SetStrokeWidth(width);

            double spine = 0.0;
            bool haveSpine = false;
            std::vector<std::pair<Point2Dd, Point2Dd>> risers;   // (spine point, child anchor)

            for (const auto* r : group) {
                const RequirementNode* child = model.GetNode(r->targetId);
                if (!child) continue;

                if (vertical) {
                    const bool below = child->y >= parent->y;
                    const Point2Dd parentAnchor(parent->x + parent->width / 2.0,
                                                below ? parent->y + parent->height : parent->y);
                    const Point2Dd childAnchor(child->x + child->width / 2.0,
                                               below ? child->y : child->y + child->height);
                    if (!haveSpine) {
                        spine = (parentAnchor.y + childAnchor.y) / 2.0;
                        haveSpine = true;
                    }
                    risers.emplace_back(Point2Dd(childAnchor.x, spine), childAnchor);
                } else {
                    const bool right = child->x >= parent->x;
                    const Point2Dd parentAnchor(right ? parent->x + parent->width : parent->x,
                                                parent->y + parent->height / 2.0);
                    const Point2Dd childAnchor(right ? child->x : child->x + child->width,
                                               child->y + child->height / 2.0);
                    if (!haveSpine) {
                        spine = (parentAnchor.x + childAnchor.x) / 2.0;
                        haveSpine = true;
                    }
                    risers.emplace_back(Point2Dd(spine, childAnchor.y), childAnchor);
                }
            }
            if (!haveSpine || risers.empty()) continue;

            const Point2Dd parentCentre = vertical
                ? Point2Dd(parent->x + parent->width / 2.0,
                           spine >= parent->y + parent->height ? parent->y + parent->height
                                                                : parent->y)
                : Point2Dd(spine >= parent->x + parent->width ? parent->x + parent->width
                                                              : parent->x,
                           parent->y + parent->height / 2.0);
            const Point2Dd busStart = vertical ? Point2Dd(parentCentre.x, spine)
                                                : Point2Dd(spine, parentCentre.y);
            ctx->DrawLine(parentCentre, busStart);

            double minCross = vertical ? busStart.x : busStart.y;
            double maxCross = minCross;
            for (const auto& [spinePoint, childAnchor] : risers) {
                (void)childAnchor;
                const double cross = vertical ? spinePoint.x : spinePoint.y;
                minCross = std::min(minCross, cross);
                maxCross = std::max(maxCross, cross);
            }
            if (vertical) {
                ctx->DrawLine(Point2Dd(minCross, spine), Point2Dd(maxCross, spine));
            } else {
                ctx->DrawLine(Point2Dd(spine, minCross), Point2Dd(spine, maxCross));
            }
            for (const auto& [spinePoint, childAnchor] : risers) {
                ctx->DrawLine(spinePoint, childAnchor);
            }

            // The crosshair ⊕ always sits at the PARENT end of the containment.
            if (!selectedPass) DrawCrosshair(ctx, parentCentre, color);
        }
    }
}

void UltraCanvasRequirementDiagram::RenderRelations(IRenderContext* ctx) {
    for (const auto& relation : model.GetRelations()) {
        if (relation.kind == RequirementRelationKind::Containment) continue;
        if (!relation.visible) continue;
        const RequirementNode* source = model.GetNode(relation.sourceId);
        const RequirementNode* target = model.GetNode(relation.targetId);
        if (!source || !target || !IsDisplayed(*source) || !IsDisplayed(*target)) continue;
        RenderRelation(ctx, relation);
    }
}

void UltraCanvasRequirementDiagram::RenderRelation(IRenderContext* ctx,
                                                    const RequirementRelation& relation) {
    std::vector<Point2Dd> path;
    BuildRelationPath(relation, path);
    if (path.size() < 2) return;

    Color color = relation.hasColor ? relation.color
                                    : palette.ColorForRelation(relation.kind);
    double width = relation.lineWidth > 0.0 ? relation.lineWidth : style.relationLineWidth;
    if (relation.isSelected) {
        color = palette.selectionColor;
        width = style.selectionWidth;
    } else if (relation.isHovered) {
        color = palette.hoverColor;
    } else if (relation.illegal) {
        // Lenient mode keeps the relation but marks it, so the modelling error
        // is visible without the diagram refusing to draw.
        color = palette.warningColor;
    }
    color = ApplyDim(color, relation.dimmed);

    // Generalisation is a solid line; every requirement dependency is dashed.
    const bool dashed = (relation.kind != RequirementRelationKind::Generalization);
    ctx->SetLineDash(dashed ? style.relationDash : UCDashPattern::EMPTY);
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(width);
    ctx->DrawLinePath(path, false);
    ctx->SetLineDash(UCDashPattern::EMPTY);

    // Arrowhead oriented by the last segment, not by the overall vector.
    const Point2Dd tip = path.back();
    const Point2Dd prev = path[path.size() - 2];
    double dirX = tip.x - prev.x, dirY = tip.y - prev.y;
    const double length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length > 1e-6) {
        dirX /= length;
        dirY /= length;
        if (relation.kind == RequirementRelationKind::Generalization) {
            DrawHollowTriangle(ctx, tip, dirX, dirY, color,
                               ApplyDim(palette.backgroundColor, relation.dimmed));
        } else {
            DrawOpenArrowHead(ctx, tip, dirX, dirY, color);
        }
    }

    if (relation.showLabel) RenderRelationLabel(ctx, relation, path);
}

void UltraCanvasRequirementDiagram::RenderRelationLabel(IRenderContext* ctx,
                                                         const RequirementRelation& relation,
                                                         const std::vector<Point2Dd>& path) {
    std::string label = relation.label;
    if (label.empty()) {
        const char* defaultLabel = RequirementRelationLabel(relation.kind);
        if (!defaultLabel || !*defaultLabel) return;
        label = std::string("«") + defaultLabel + "»";
    }
    if (path.size() < 2) return;

    // Anchor to the middle of the longest segment: on an orthogonal route that
    // is the long central run, where a reader expects the keyword.
    size_t bestIndex = 1;
    double bestLength = -1.0;
    for (size_t i = 1; i < path.size(); ++i) {
        const double dx = path[i].x - path[i - 1].x;
        const double dy = path[i].y - path[i - 1].y;
        const double length = dx * dx + dy * dy;
        if (length > bestLength) { bestLength = length; bestIndex = i; }
    }
    const double midX = (path[bestIndex].x + path[bestIndex - 1].x) / 2.0;
    const double midY = (path[bestIndex].y + path[bestIndex - 1].y) / 2.0;

    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.relationLabelFontSize);
    const Size2Di dim = ctx->GetTextLineDimensions(label);
    const double padding = 3.0;

    ctx->SetFillPaint(ApplyDim(palette.relationLabelBackground, relation.dimmed));
    ctx->FillRoundedRectangle(
        Rect2Dd(midX - dim.width / 2.0 - padding, midY - dim.height / 2.0 - padding,
                dim.width + padding * 2.0, dim.height + padding * 2.0),
        3.0);

    ctx->SetTextPaint(ApplyDim(relation.isSelected ? palette.selectionColor
                                                   : palette.relationLabelColor,
                               relation.dimmed));
    ctx->DrawText(label, Point2Dd(midX - dim.width / 2.0, midY - dim.height / 2.0));
}

void UltraCanvasRequirementDiagram::RenderNoteLeaders(IRenderContext* ctx) {
    // A «rationale» / «problem» / note anchored to an element or a relation is
    // joined to it with a dashed leader, the SysML note notation.
    for (const auto& id : model.GetNodeOrder()) {
        const RequirementNode* note = model.GetNode(id);
        if (!note || note->anchorId.empty() || !IsDisplayed(*note)) continue;

        Point2Dd anchorPoint;
        bool haveAnchor = false;

        if (const RequirementNode* target = model.GetNode(note->anchorId)) {
            if (!IsDisplayed(*target)) continue;
            anchorPoint = Point2Dd(target->x + target->width / 2.0,
                                   target->y + target->height / 2.0);
            haveAnchor = true;
        } else if (const RequirementRelation* rel = model.GetRelation(note->anchorId)) {
            std::vector<Point2Dd> path;
            BuildRelationPath(*rel, path);
            if (!path.empty()) {
                anchorPoint = path[path.size() / 2];
                haveAnchor = true;
            }
        }
        if (!haveAnchor) continue;

        ctx->SetLineDash(style.leaderDash);
        ctx->SetStrokePaint(ApplyDim(palette.noteBorder, note->dimmed));
        ctx->SetStrokeWidth(style.relationLineWidth);
        ctx->DrawLine(Point2Dd(note->x + note->width / 2.0, note->y + note->height / 2.0),
                      anchorPoint);
        ctx->SetLineDash(UCDashPattern::EMPTY);
    }
}

// =============================================================================
// NODES
// =============================================================================

void UltraCanvasRequirementDiagram::RenderNodes(IRenderContext* ctx) {
    for (const auto& id : model.GetNodeOrder()) {
        const RequirementNode* node = model.GetNode(id);
        if (!node || !IsDisplayed(*node)) continue;
        RenderNode(ctx, *node);
    }
}

void UltraCanvasRequirementDiagram::RenderNodeShape(IRenderContext* ctx,
                                                     const RequirementNode& node,
                                                     const Color& fill, const Color& border,
                                                     double borderWidth) {
    const Rect2Dd rect = NodeRect(node);
    ctx->SetLineDash(UCDashPattern::EMPTY);

    switch (ResolveShape(node)) {
        case RequirementNodeShape::Oval:
            ctx->SetFillPaint(fill);
            ctx->FillEllipse(rect);
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(borderWidth);
            ctx->DrawEllipse(rect);
            break;

        case RequirementNodeShape::RoundedRectangle:
            ctx->DrawFilledRectangle(rect, fill, static_cast<float>(borderWidth), border,
                                     static_cast<float>(style.nodeCornerRadius));
            break;

        case RequirementNodeShape::Folder: {
            // UML package: a small tab on the top-left over the body.
            const double tabWidth = std::min(rect.width * 0.45, 70.0);
            const double tabHeight = 12.0;
            ctx->DrawFilledRectangle(Rect2Dd(rect.x, rect.y, tabWidth, tabHeight), fill,
                                     static_cast<float>(borderWidth), border, 0.0f);
            ctx->DrawFilledRectangle(
                Rect2Dd(rect.x, rect.y + tabHeight, rect.width, rect.height - tabHeight),
                fill, static_cast<float>(borderWidth), border, 0.0f);
            break;
        }
        case RequirementNodeShape::FoldedNote: {
            // Note: rectangle with the top-right corner folded down.
            const double fold = 12.0;
            const std::vector<Point2Dd> outline = {
                Point2Dd(rect.x, rect.y),
                Point2Dd(rect.Right() - fold, rect.y),
                Point2Dd(rect.Right(), rect.y + fold),
                Point2Dd(rect.Right(), rect.Bottom()),
                Point2Dd(rect.x, rect.Bottom())
            };
            ctx->SetFillPaint(fill);
            ctx->FillLinePath(outline);
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(borderWidth);
            ctx->DrawLinePath(outline, true);
            // The fold itself.
            ctx->DrawLine(Point2Dd(rect.Right() - fold, rect.y),
                          Point2Dd(rect.Right() - fold, rect.y + fold));
            ctx->DrawLine(Point2Dd(rect.Right() - fold, rect.y + fold),
                          Point2Dd(rect.Right(), rect.y + fold));
            break;
        }
        case RequirementNodeShape::StickFigure: {
            // Actor: the figure sits above the label block.
            const double centreX = rect.x + rect.width / 2.0;
            const double headRadius = 6.0;
            const double top = rect.y + 3.0;
            ctx->DrawFilledCircle(Point2Dd(centreX, top + headRadius),
                                  static_cast<float>(headRadius), fill, border,
                                  static_cast<float>(borderWidth));
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(borderWidth);
            const double bodyTop = top + headRadius * 2.0;
            const double bodyBottom = bodyTop + 10.0;
            ctx->DrawLine(Point2Dd(centreX, bodyTop), Point2Dd(centreX, bodyBottom));
            ctx->DrawLine(Point2Dd(centreX - 8.0, bodyTop + 4.0),
                          Point2Dd(centreX + 8.0, bodyTop + 4.0));
            ctx->DrawLine(Point2Dd(centreX, bodyBottom),
                          Point2Dd(centreX - 6.0, bodyBottom + 7.0));
            ctx->DrawLine(Point2Dd(centreX, bodyBottom),
                          Point2Dd(centreX + 6.0, bodyBottom + 7.0));
            break;
        }
        case RequirementNodeShape::Rectangle:
        case RequirementNodeShape::Auto:
        default:
            ctx->DrawFilledRectangle(rect, fill, static_cast<float>(borderWidth), border, 0.0f);
            break;
    }
}

void UltraCanvasRequirementDiagram::RenderNode(IRenderContext* ctx,
                                                const RequirementNode& node) {
    if (node.width <= 0.0 || node.height <= 0.0) return;

    const Color fill = ApplyDim(ResolveFillColor(node), node.dimmed);
    Color border = ResolveBorderColor(node);
    double borderWidth = node.borderWidth > 0.0 ? node.borderWidth : style.borderWidth;

    if (node.isSelected) {
        border = palette.selectionColor;
        borderWidth = style.selectionWidth;
    } else if (node.isHovered) {
        border = palette.hoverColor;
        borderWidth = std::max(borderWidth, style.borderWidth + 0.6);
    }
    border = ApplyDim(border, node.dimmed && !node.isSelected);

    RenderNodeShape(ctx, node, fill, border, borderWidth);

    // Risk as a coloured left edge stripe (opt-in).
    if (showRiskStripe && node.risk != RequirementRisk::Unspecified) {
        ctx->SetFillPaint(ApplyDim(palette.ColorForRisk(node.risk), node.dimmed));
        ctx->FillRectangle(Rect2Dd(node.x, node.y, 4.0, node.height));
    }

    auto measuredIt = measuredNodes.find(node.id);
    if (measuredIt != measuredNodes.end()) RenderNodeBody(ctx, node, measuredIt->second);

    RenderPorts(ctx, node);
    RenderStatusBadge(ctx, node);
    if (suspectBadge) RenderSuspectBadge(ctx, node);
}

void UltraCanvasRequirementDiagram::DrawTextLine(IRenderContext* ctx, const MeasuredLine& line,
                                                  double x, double y, double boxWidth) {
    ctx->SetFontFace(style.fontFamily,
                     line.bold ? FontWeight::Bold : FontWeight::Normal,
                     FontSlant::Normal);
    ctx->SetFontSize(line.fontSize);
    ctx->SetTextPaint(line.color);

    double drawX = x;
    if (line.centered) {
        const int textWidth = ctx->GetTextLineWidth(line.text);
        drawX = x + (boxWidth - static_cast<double>(textWidth)) / 2.0;
        if (drawX < x) drawX = x;
    }
    // Y is the TOP of the text box in this framework, not the baseline.
    ctx->DrawText(line.text, Point2Dd(drawX, y));
}

void UltraCanvasRequirementDiagram::RenderNodeBody(IRenderContext* ctx,
                                                    const RequirementNode& node,
                                                    const MeasuredNode& measured) {
    const double contentX = node.x + style.nodePadding;
    const double contentWidth = node.width - style.nodePadding * 2.0;
    double y = node.y + style.nodePadding;
    // The stick figure occupies the top of the box; the label starts below it.
    if (ResolveShape(node) == RequirementNodeShape::StickFigure) y += 26.0;

    if (palette.headerBandColor.a > 0 && measured.hasDivider) {
        ctx->SetFillPaint(ApplyDim(palette.headerBandColor, node.dimmed));
        ctx->FillRectangle(Rect2Dd(node.x + 1.0, node.y + 1.0, node.width - 2.0,
                                   measured.headerHeight + style.nodePadding * 2.0 - 2.0));
    }

    for (const auto& line : measured.headerLines) {
        ctx->SetFontFace(style.fontFamily,
                         line.bold ? FontWeight::Bold : FontWeight::Normal,
                         FontSlant::Normal);
        ctx->SetFontSize(line.fontSize);
        const double lineHeight = ctx->GetTextLineHeight(line.text);
        MeasuredLine dimmedLine = line;
        dimmedLine.color = ApplyDim(line.color, node.dimmed);
        // The inline editor paints the name itself.
        if (!(renamingNodeId == node.id && line.bold)) {
            DrawTextLine(ctx, dimmedLine, contentX, y, contentWidth);
        }
        y += lineHeight + style.rowSpacing;
    }

    if (!measured.hasDivider) return;

    y += style.headerGap - style.rowSpacing;
    ctx->SetLineDash(UCDashPattern::EMPTY);
    ctx->SetStrokePaint(ApplyDim(palette.dividerColor, node.dimmed));
    ctx->SetStrokeWidth(1.0);
    ctx->DrawLine(Point2Dd(node.x, y), Point2Dd(node.x + node.width, y));
    y += style.headerGap;

    for (const auto& line : measured.bodyLines) {
        ctx->SetFontFace(style.fontFamily,
                         line.bold ? FontWeight::Bold : FontWeight::Normal,
                         FontSlant::Normal);
        ctx->SetFontSize(line.fontSize);
        const double lineHeight = ctx->GetTextLineHeight(line.text);
        // A compartment heading gets a hairline above it so the lists read as
        // separate blocks, matching the SysML compartment notation.
        if (line.heading && y > node.y + style.nodePadding) {
            ctx->SetStrokePaint(ApplyDim(palette.dividerColor, node.dimmed));
            ctx->SetStrokeWidth(0.7);
            ctx->DrawLine(Point2Dd(node.x, y - style.rowSpacing),
                          Point2Dd(node.x + node.width, y - style.rowSpacing));
        }
        MeasuredLine dimmedLine = line;
        dimmedLine.color = ApplyDim(line.color, node.dimmed);
        DrawTextLine(ctx, dimmedLine, contentX, y, contentWidth);
        y += lineHeight + style.rowSpacing;
    }
}

void UltraCanvasRequirementDiagram::RenderCollapseToggles(IRenderContext* ctx) {
    for (const auto& id : model.GetNodeOrder()) {
        const RequirementNode* node = model.GetNode(id);
        if (!node || !IsDisplayed(*node)) continue;
        const std::vector<std::string> children = model.GetChildIds(id);
        if (children.empty()) continue;

        const Point2Dd centre = CollapseTogglePosition(*node);
        const double r = style.toggleRadius;
        const Color color = ApplyDim(
            hoveredToggleId == id ? palette.hoverColor
                                  : palette.ColorForRelation(RequirementRelationKind::Containment),
            node->dimmed);

        ctx->SetLineDash(UCDashPattern::EMPTY);
        ctx->DrawFilledCircle(centre, static_cast<float>(r),
                              ApplyDim(palette.backgroundColor, node->dimmed), color,
                              static_cast<float>(style.relationLineWidth));
        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(style.relationLineWidth);
        // Minus always; the vertical stroke makes it a plus when collapsed.
        ctx->DrawLine(Point2Dd(centre.x - r * 0.6, centre.y),
                      Point2Dd(centre.x + r * 0.6, centre.y));
        if (node->collapsed) {
            ctx->DrawLine(Point2Dd(centre.x, centre.y - r * 0.6),
                          Point2Dd(centre.x, centre.y + r * 0.6));

            // Badge with the number of hidden descendants.
            const size_t hidden = model.GetDescendants(id).size();
            if (hidden > 0) {
                const std::string badge = std::to_string(hidden);
                ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
                ctx->SetFontSize(style.baseFontSize - 1.0);
                const Size2Di dim = ctx->GetTextLineDimensions(badge);
                const Point2Dd badgeCentre(centre.x + r + dim.width / 2.0 + 4.0, centre.y);
                ctx->DrawFilledCircle(badgeCentre,
                                      static_cast<float>(dim.height / 2.0 + 2.0),
                                      color, color, 0.0f);
                ctx->SetTextPaint(palette.backgroundColor);
                ctx->DrawText(badge, Point2Dd(badgeCentre.x - dim.width / 2.0,
                                              badgeCentre.y - dim.height / 2.0));
            }
        }
    }
}

void UltraCanvasRequirementDiagram::RenderCoverageBadges(IRenderContext* ctx) {
    // A corner dot per requirement: red = nothing satisfies it, amber = no
    // test case verifies it, green = both covered.
    const std::vector<std::string> uncovered = model.GetUncoveredRequirements();
    const std::vector<std::string> unverified = model.GetUnverifiedRequirements();
    const std::set<std::string> uncoveredSet(uncovered.begin(), uncovered.end());
    const std::set<std::string> unverifiedSet(unverified.begin(), unverified.end());

    for (const auto& id : model.GetNodeOrder()) {
        const RequirementNode* node = model.GetNode(id);
        if (!node || node->kind != RequirementNodeKind::Requirement) continue;
        if (!IsDisplayed(*node)) continue;

        Color color = palette.coveredColor;
        if (uncoveredSet.count(id))       color = palette.uncoveredColor;
        else if (unverifiedSet.count(id)) color = palette.unverifiedColor;

        const double radius = 4.5;
        ctx->DrawFilledCircle(Point2Dd(node->x + node->width - radius - 3.0,
                                       node->y + radius + 3.0),
                              static_cast<float>(radius),
                              ApplyDim(color, node->dimmed),
                              ApplyDim(palette.backgroundColor, node->dimmed), 1.0f);
    }
}

void UltraCanvasRequirementDiagram::RenderCallouts(IRenderContext* ctx) {
    for (const auto& callout : model.GetCallouts()) {
        const RequirementNode* target = model.GetNode(callout.targetNodeId);
        if (!target || !IsDisplayed(*target)) continue;

        const Rect2Dd rect(callout.x, callout.y, callout.width, callout.height);
        const Color fill = callout.hasFillColor ? callout.fillColor : palette.calloutFill;
        const Color border = callout.isSelected ? palette.selectionColor
                                                 : palette.calloutBorder;

        // Dashed leader first, so the box covers the stub.
        ctx->SetLineDash(style.leaderDash);
        ctx->SetStrokePaint(palette.calloutBorder);
        ctx->SetStrokeWidth(style.relationLineWidth);
        ctx->DrawLine(Point2Dd(rect.x + rect.width / 2.0, rect.y + rect.height / 2.0),
                      Point2Dd(target->x + target->width / 2.0,
                               target->y + target->height / 2.0));
        ctx->SetLineDash(UCDashPattern::EMPTY);

        ctx->DrawFilledRectangle(rect, fill,
                                 static_cast<float>(callout.isSelected ? style.selectionWidth
                                                                        : style.borderWidth),
                                 border, 3.0f);

        auto it = measuredCallouts.find(callout.id);
        if (it == measuredCallouts.end()) continue;
        const MeasuredNode& measured = it->second;

        const double contentX = rect.x + style.nodePadding;
        const double contentWidth = rect.width - style.nodePadding * 2.0;
        double y = rect.y + style.nodePadding;

        for (const auto& line : measured.headerLines) {
            ctx->SetFontFace(style.fontFamily,
                             line.bold ? FontWeight::Bold : FontWeight::Normal,
                             FontSlant::Normal);
            ctx->SetFontSize(line.fontSize);
            const double lineHeight = ctx->GetTextLineHeight(line.text);
            DrawTextLine(ctx, line, contentX, y, contentWidth);
            y += lineHeight + style.rowSpacing;
        }
        if (measured.hasDivider) {
            y += style.headerGap - style.rowSpacing;
            ctx->SetStrokePaint(palette.dividerColor);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawLine(Point2Dd(rect.x, y), Point2Dd(rect.x + rect.width, y));
            y += style.headerGap;
        }
        for (const auto& line : measured.bodyLines) {
            ctx->SetFontFace(style.fontFamily,
                             line.bold ? FontWeight::Bold : FontWeight::Normal,
                             FontSlant::Normal);
            ctx->SetFontSize(line.fontSize);
            const double lineHeight = ctx->GetTextLineHeight(line.text);
            DrawTextLine(ctx, line, contentX, y, contentWidth);
            y += lineHeight + style.rowSpacing;
        }
    }
}

// =============================================================================
// DECORATIONS
// =============================================================================

void UltraCanvasRequirementDiagram::DrawCrosshair(IRenderContext* ctx, const Point2Dd& center,
                                                   const Color& color) {
    const double r = style.crosshairRadius;
    ctx->SetLineDash(UCDashPattern::EMPTY);
    ctx->DrawFilledCircle(center, static_cast<float>(r), palette.backgroundColor, color,
                          static_cast<float>(style.relationLineWidth));
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(style.relationLineWidth);
    ctx->DrawLine(Point2Dd(center.x - r, center.y), Point2Dd(center.x + r, center.y));
    ctx->DrawLine(Point2Dd(center.x, center.y - r), Point2Dd(center.x, center.y + r));
}

void UltraCanvasRequirementDiagram::DrawOpenArrowHead(IRenderContext* ctx, const Point2Dd& tip,
                                                       double dirX, double dirY,
                                                       const Color& color) {
    // SysML dependencies use an OPEN (unfilled) arrowhead: two strokes, no fill.
    const double angle = 0.42;
    const double size = style.arrowSize;
    const double cosA = std::cos(angle), sinA = std::sin(angle);
    const double backX = -dirX, backY = -dirY;

    ctx->SetLineDash(UCDashPattern::EMPTY);
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(style.relationLineWidth);
    ctx->DrawLine(Point2Dd(tip.x + size * (backX * cosA - backY * sinA),
                           tip.y + size * (backX * sinA + backY * cosA)), tip);
    ctx->DrawLine(Point2Dd(tip.x + size * (backX * cosA + backY * sinA),
                           tip.y + size * (-backX * sinA + backY * cosA)), tip);
}

void UltraCanvasRequirementDiagram::DrawHollowTriangle(IRenderContext* ctx, const Point2Dd& tip,
                                                        double dirX, double dirY,
                                                        const Color& color, const Color& fill) {
    const double size = style.trianglSize;
    const double halfWidth = size * 0.5;
    const double baseX = tip.x - dirX * size;
    const double baseY = tip.y - dirY * size;
    const double px = -dirY, py = dirX;   // perpendicular

    const std::vector<Point2Dd> triangle = {
        tip,
        Point2Dd(baseX + px * halfWidth, baseY + py * halfWidth),
        Point2Dd(baseX - px * halfWidth, baseY - py * halfWidth)
    };

    ctx->SetLineDash(UCDashPattern::EMPTY);
    ctx->SetFillPaint(fill);
    ctx->FillLinePath(triangle);
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(style.relationLineWidth);
    ctx->DrawLinePath(triangle, true);
}

void UltraCanvasRequirementDiagram::RenderSelectionBox(IRenderContext* ctx) {
    const double x = std::min(selectionBoxStart.x, selectionBoxEnd.x);
    const double y = std::min(selectionBoxStart.y, selectionBoxEnd.y);
    const double w = std::abs(selectionBoxEnd.x - selectionBoxStart.x);
    const double h = std::abs(selectionBoxEnd.y - selectionBoxStart.y);

    ctx->SetLineDash(UCDashPattern::EMPTY);
    ctx->SetFillPaint(style.selectionBoxFill);
    ctx->FillRectangle(Rect2Dd(x, y, w, h));
    ctx->SetStrokePaint(style.selectionBoxStroke);
    ctx->SetStrokeWidth(1.0 / std::max(0.0001, zoomLevel));
    ctx->DrawRectangle(Rect2Dd(x, y, w, h));
}

void UltraCanvasRequirementDiagram::RenderConnectionPreview(IRenderContext* ctx) {
    const RequirementNode* source = model.GetNode(connectionSourceId);
    if (!source) return;

    const Color color = palette.ColorForRelation(pendingRelationKind);
    ctx->SetLineDash(style.relationDash);
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(style.relationLineWidth);
    const Point2Dd start(source->x + source->width / 2.0, source->y + source->height / 2.0);
    ctx->DrawLine(start, connectionEndPoint);
    ctx->SetLineDash(UCDashPattern::EMPTY);

    double dirX = connectionEndPoint.x - start.x;
    double dirY = connectionEndPoint.y - start.y;
    const double length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length > 1e-6) {
        DrawOpenArrowHead(ctx, connectionEndPoint, dirX / length, dirY / length, color);
    }
}

void UltraCanvasRequirementDiagram::RenderRenameEditor(IRenderContext* ctx) {
    const RequirementNode* node = model.GetNode(renamingNodeId);
    if (!node) return;

    auto measuredIt = measuredNodes.find(renamingNodeId);
    if (measuredIt == measuredNodes.end()) return;

    // The editor replaces the bold name line, wherever the header put it.
    double y = node->y + style.nodePadding;
    if (ResolveShape(*node) == RequirementNodeShape::StickFigure) y += 26.0;
    for (const auto& line : measuredIt->second.headerLines) {
        ctx->SetFontFace(style.fontFamily,
                         line.bold ? FontWeight::Bold : FontWeight::Normal,
                         FontSlant::Normal);
        ctx->SetFontSize(line.fontSize);
        const double lineHeight = ctx->GetTextLineHeight(line.text);
        if (line.bold) break;
        y += lineHeight + style.rowSpacing;
    }

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(style.nameFontSize);
    const std::string shown = renameBuffer + "|";
    const Size2Di dim = ctx->GetTextLineDimensions(shown);
    const double boxX = node->x + style.nodePadding;
    const double boxWidth = node->width - style.nodePadding * 2.0;

    ctx->SetFillPaint(Color(255, 255, 255, 245));
    ctx->FillRectangle(Rect2Dd(boxX, y - 1.0, boxWidth, dim.height + 2.0));
    ctx->SetStrokePaint(palette.selectionColor);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRectangle(Rect2Dd(boxX, y - 1.0, boxWidth, dim.height + 2.0));

    ctx->SetTextPaint(palette.headerTextColor);
    ctx->DrawText(shown, Point2Dd(boxX + 2.0, y));
}

// =============================================================================
// SCREEN-SPACE OVERLAYS
// =============================================================================

void UltraCanvasRequirementDiagram::RenderTitle(IRenderContext* ctx) {
    ctx->SetFillPaint(titleConfig.backgroundColor);
    ctx->FillRectangle(Rect2Dd(0.0, 0.0, GetWidth(), titleConfig.height));

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(titleConfig.fontSize);
    ctx->SetTextPaint(titleConfig.textColor);

    const Size2Di titleDim = ctx->GetTextLineDimensions(titleConfig.title);
    const bool hasSubtitle = !titleConfig.subtitle.empty();
    const double totalHeight = hasSubtitle
        ? titleDim.height + titleConfig.subtitleFontSize * 1.4
        : titleDim.height;
    double y = (titleConfig.height - totalHeight) / 2.0;

    auto alignedX = [this](int textWidth) {
        if (titleConfig.alignment == TextAlignment::Center) {
            return (GetWidth() - static_cast<double>(textWidth)) / 2.0;
        }
        if (titleConfig.alignment == TextAlignment::Right) {
            return GetWidth() - textWidth - 12.0;
        }
        return 12.0;
    };

    ctx->DrawText(titleConfig.title, Point2Dd(alignedX(titleDim.width), y));
    y += titleDim.height;

    if (hasSubtitle) {
        ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(titleConfig.subtitleFontSize);
        const Size2Di subDim = ctx->GetTextLineDimensions(titleConfig.subtitle);
        ctx->DrawText(titleConfig.subtitle, Point2Dd(alignedX(subDim.width), y));
    }
}

void UltraCanvasRequirementDiagram::RenderFrame(IRenderContext* ctx) {
    // SysML frame: `req [Package] Name [Diagram Name]` in a pentagon tab over
    // a border enclosing the whole diagram area.
    std::string header = frameConfig.diagramKind;
    if (!frameConfig.elementType.empty()) header += " [" + frameConfig.elementType + "]";
    if (!frameConfig.elementName.empty()) header += " " + frameConfig.elementName;
    if (!frameConfig.diagramName.empty()) header += " [" + frameConfig.diagramName + "]";

    const double top = (titleConfig.visible ? titleConfig.height : 0.0) + frameConfig.margin;
    const double left = frameConfig.margin;
    const double right = GetWidth() - frameConfig.margin;
    const double bottom = GetHeight() - frameConfig.margin;
    if (right <= left || bottom <= top) return;

    ctx->SetLineDash(UCDashPattern::EMPTY);
    ctx->SetStrokePaint(frameConfig.borderColor);
    ctx->SetStrokeWidth(frameConfig.borderWidth);
    ctx->DrawRectangle(Rect2Dd(left, top, right - left, bottom - top));

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(frameConfig.fontSize);
    const Size2Di dim = ctx->GetTextLineDimensions(header);
    const double tabWidth = dim.width + 20.0 + frameConfig.tabNotch;
    const double tabHeight = std::max(frameConfig.tabHeight,
                                      static_cast<double>(dim.height) + 6.0);

    const std::vector<Point2Dd> pentagon = {
        Point2Dd(left, top),
        Point2Dd(left + tabWidth, top),
        Point2Dd(left + tabWidth, top + tabHeight - frameConfig.tabNotch),
        Point2Dd(left + tabWidth - frameConfig.tabNotch, top + tabHeight),
        Point2Dd(left, top + tabHeight)
    };
    ctx->SetFillPaint(frameConfig.tabFillColor);
    ctx->FillLinePath(pentagon);
    ctx->SetStrokePaint(frameConfig.borderColor);
    ctx->SetStrokeWidth(frameConfig.borderWidth);
    ctx->DrawLinePath(pentagon, true);

    ctx->SetTextPaint(frameConfig.textColor);
    ctx->DrawText(header, Point2Dd(left + 10.0,
                                   top + (tabHeight - static_cast<double>(dim.height)) / 2.0));
}

std::vector<RequirementLegendEntry> UltraCanvasRequirementDiagram::BuildLegendEntries() const {
    std::vector<RequirementLegendEntry> entries;

    switch (legendConfig.source) {
        case RequirementLegendSource::Custom:
            return customLegendEntries;

        case RequirementLegendSource::Categories: {
            // Only categories actually present, in registration order, so the
            // legend never advertises an unused colour.
            std::set<std::string> used;
            for (const auto& id : model.GetNodeOrder()) {
                const RequirementNode* node = model.GetNode(id);
                if (node && !node->category.empty()) used.insert(node->category);
            }
            for (const auto& name : model.GetCategoryOrder()) {
                if (used.find(name) == used.end()) continue;
                RequirementLegendEntry entry;
                entry.label = name;
                const auto* category = model.GetCategory(name);
                entry.color = category ? category->fillColor : Color(220, 220, 220, 255);
                entries.push_back(entry);
            }
            break;
        }
        case RequirementLegendSource::NodeKinds: {
            std::vector<RequirementNodeKind> seen;
            for (const auto& id : model.GetNodeOrder()) {
                const RequirementNode* node = model.GetNode(id);
                if (!node) continue;
                if (std::find(seen.begin(), seen.end(), node->kind) != seen.end()) continue;
                seen.push_back(node->kind);
                RequirementLegendEntry entry;
                entry.label = std::string("«") + RequirementNodeKindToString(node->kind) + "»";
                entry.color = palette.FillForKind(node->kind);
                entries.push_back(entry);
            }
            break;
        }
        case RequirementLegendSource::RelationKinds: {
            std::vector<RequirementRelationKind> seen;
            for (const auto& r : model.GetRelations()) {
                if (!r.visible) continue;
                if (std::find(seen.begin(), seen.end(), r.kind) != seen.end()) continue;
                seen.push_back(r.kind);
                RequirementLegendEntry entry;
                entry.label = RequirementRelationKindToString(r.kind);
                entry.color = palette.ColorForRelation(r.kind);
                entry.isLine = true;
                entry.dashed = (r.kind != RequirementRelationKind::Containment &&
                                r.kind != RequirementRelationKind::Generalization);
                entries.push_back(entry);
            }
            break;
        }
        case RequirementLegendSource::Coverage: {
            const RequirementCoverage coverage = model.GetCoverage();
            const int uncovered = coverage.requirementCount - coverage.satisfiedCount;
            const int unverified = coverage.requirementCount - coverage.verifiedCount;
            entries.push_back({"satisfied + verified: " + std::to_string(
                                   std::min(coverage.satisfiedCount, coverage.verifiedCount)),
                               palette.coveredColor, false, false});
            entries.push_back({"not verified: " + std::to_string(unverified),
                               palette.unverifiedColor, false, false});
            entries.push_back({"not satisfied: " + std::to_string(uncovered),
                               palette.uncoveredColor, false, false});
            break;
        }
    }
    return entries;
}

void UltraCanvasRequirementDiagram::RenderLegend(IRenderContext* ctx) {
    const std::vector<RequirementLegendEntry> entries = BuildLegendEntries();
    if (entries.empty()) return;

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(legendConfig.fontSize + 1.0);
    const Size2Di titleDim = ctx->GetTextLineDimensions(legendConfig.title);

    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(legendConfig.fontSize);

    double contentWidth = legendConfig.title.empty() ? 0.0 : titleDim.width;
    double rowHeight = 0.0;
    for (const auto& entry : entries) {
        const Size2Di dim = ctx->GetTextLineDimensions(entry.label);
        contentWidth = std::max(contentWidth,
                                legendConfig.swatchSize + 6.0 + static_cast<double>(dim.width));
        rowHeight = std::max(rowHeight,
                             std::max(static_cast<double>(dim.height), legendConfig.swatchSize));
    }
    if (rowHeight <= 0.0) rowHeight = legendConfig.swatchSize;

    const double titleHeight = legendConfig.title.empty()
                                   ? 0.0 : titleDim.height + legendConfig.rowGap;
    const double panelWidth = contentWidth + legendConfig.innerPadding * 2.0;
    const double panelHeight = titleHeight +
        static_cast<double>(entries.size()) * (rowHeight + legendConfig.rowGap) -
        legendConfig.rowGap + legendConfig.innerPadding * 2.0;

    const double topInset = (titleConfig.visible ? titleConfig.height : 0.0) +
                            (frameConfig.visible ? frameConfig.tabHeight : 0.0);
    double px = legendConfig.padding;
    double py = legendConfig.padding + topInset;
    switch (legendConfig.position) {
        case RequirementPanelPosition::TopLeft:
            break;
        case RequirementPanelPosition::TopRight:
            px = GetWidth() - panelWidth - legendConfig.padding;
            break;
        case RequirementPanelPosition::BottomLeft:
            py = GetHeight() - panelHeight - legendConfig.padding;
            break;
        case RequirementPanelPosition::BottomRight:
            px = GetWidth() - panelWidth - legendConfig.padding;
            py = GetHeight() - panelHeight - legendConfig.padding;
            break;
    }

    ctx->SetLineDash(UCDashPattern::EMPTY);
    ctx->DrawFilledRectangle(Rect2Dd(px, py, panelWidth, panelHeight),
                             legendConfig.backgroundColor, 1.0f,
                             legendConfig.borderColor, 4.0f);

    double y = py + legendConfig.innerPadding;
    if (!legendConfig.title.empty()) {
        ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
        ctx->SetFontSize(legendConfig.fontSize + 1.0);
        ctx->SetTextPaint(legendConfig.textColor);
        ctx->DrawText(legendConfig.title, Point2Dd(px + legendConfig.innerPadding, y));
        y += titleDim.height + legendConfig.rowGap;
    }

    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(legendConfig.fontSize);
    for (const auto& entry : entries) {
        const double swatchX = px + legendConfig.innerPadding;
        const double swatchY = y + (rowHeight - legendConfig.swatchSize) / 2.0;

        if (entry.isLine) {
            ctx->SetLineDash(entry.dashed ? style.relationDash : UCDashPattern::EMPTY);
            ctx->SetStrokePaint(entry.color);
            ctx->SetStrokeWidth(2.0);
            ctx->DrawLine(Point2Dd(swatchX, swatchY + legendConfig.swatchSize / 2.0),
                          Point2Dd(swatchX + legendConfig.swatchSize,
                                   swatchY + legendConfig.swatchSize / 2.0));
            ctx->SetLineDash(UCDashPattern::EMPTY);
        } else {
            ctx->DrawFilledRectangle(
                Rect2Dd(swatchX, swatchY, legendConfig.swatchSize, legendConfig.swatchSize),
                entry.color, 1.0f, legendConfig.borderColor, 2.0f);
        }

        ctx->SetTextPaint(legendConfig.textColor);
        const Size2Di dim = ctx->GetTextLineDimensions(entry.label);
        ctx->DrawText(entry.label,
                      Point2Dd(swatchX + legendConfig.swatchSize + 6.0,
                               y + (rowHeight - static_cast<double>(dim.height)) / 2.0));
        y += rowHeight + legendConfig.rowGap;
    }
}

// =============================================================================
// SELECTION
// =============================================================================

void UltraCanvasRequirementDiagram::SelectNode(const std::string& id, bool addToSelection) {
    if (!addToSelection) {
        selectedNodes.clear();
        selectedRelations.clear();
    }
    if (model.HasNode(id)) selectedNodes.insert(id);
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SelectRelation(const std::string& id, bool addToSelection) {
    if (!addToSelection) {
        selectedNodes.clear();
        selectedRelations.clear();
    }
    if (model.GetRelation(id)) selectedRelations.insert(id);
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SelectAll() {
    for (const auto& id : model.GetNodeOrder()) {
        if (IsNodeDisplayed(id)) selectedNodes.insert(id);
    }
    for (const auto& r : model.GetRelations()) selectedRelations.insert(r.id);
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::DeselectAll() {
    selectedNodes.clear();
    selectedRelations.clear();
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::DeleteSelected() {
    const std::vector<std::string> relationIds(selectedRelations.begin(),
                                                selectedRelations.end());
    const std::vector<std::string> nodeIds(selectedNodes.begin(), selectedNodes.end());
    for (const auto& id : relationIds) RemoveRelation(id);
    for (const auto& id : nodeIds) RemoveNode(id);
    selectedNodes.clear();
    selectedRelations.clear();
    NotifySelectionChange();
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetSelectedNodeIds() const {
    return std::vector<std::string>(selectedNodes.begin(), selectedNodes.end());
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetSelectedRelationIds() const {
    return std::vector<std::string>(selectedRelations.begin(), selectedRelations.end());
}

bool UltraCanvasRequirementDiagram::IsNodeSelected(const std::string& id) const {
    return selectedNodes.find(id) != selectedNodes.end();
}

bool UltraCanvasRequirementDiagram::IsRelationSelected(const std::string& id) const {
    return selectedRelations.find(id) != selectedRelations.end();
}

void UltraCanvasRequirementDiagram::NotifySelectionChange() {
    for (const auto& id : model.GetNodeOrder()) {
        if (RequirementNode* node = model.GetNode(id)) {
            node->isSelected = selectedNodes.count(id) > 0;
        }
    }
    for (auto& r : model.GetRelations()) {
        r.isSelected = selectedRelations.count(r.id) > 0;
    }
    if (onSelectionChange) {
        onSelectionChange(GetSelectedNodeIds(), GetSelectedRelationIds());
    }
}

void UltraCanvasRequirementDiagram::NotifyViewportChange() {
    if (onViewportChange) onViewportChange(zoomLevel, panOffset.x, panOffset.y);
}

// =============================================================================
// EDITING
// =============================================================================

void UltraCanvasRequirementDiagram::SetEditMode(RequirementEditMode mode) {
    editMode = mode;
    isConnecting = false;
    connectionSourceId.clear();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::BeginRename(const std::string& nodeId) {
    const RequirementNode* node = model.GetNode(nodeId);
    if (!node) return;
    renamingNodeId = nodeId;
    renameBuffer = node->name;
    renameFresh = true;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::CommitRename() {
    if (renamingNodeId.empty()) return;
    const std::string id = renamingNodeId;
    if (RequirementNode* node = model.GetNode(id)) {
        node->name = renameBuffer;
        if (onNodeRenamed) onNodeRenamed(id, renameBuffer);
    }
    renamingNodeId.clear();
    renameBuffer.clear();
    renameFresh = false;
    InvalidateMeasurement();
}

void UltraCanvasRequirementDiagram::CancelRename() {
    renamingNodeId.clear();
    renameBuffer.clear();
    renameFresh = false;
    RequestRedraw();
}

// =============================================================================
// EVENTS
// =============================================================================

bool UltraCanvasRequirementDiagram::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;

    switch (event.type) {
        case UCEventType::MouseDown:  return HandleMouseDown(event);
        case UCEventType::MouseUp:    return HandleMouseUp(event);
        case UCEventType::MouseMove:  return HandleMouseMove(event);
        case UCEventType::MouseWheel: return HandleMouseWheel(event);
        case UCEventType::MouseDoubleClick: {
            if (!Contains(Point2Di(event.pointer.x, event.pointer.y))) return false;
            const std::string nodeId = FindNodeAt(Point2Di(event.pointer.x, event.pointer.y));
            if (nodeId.empty()) return false;

            switch (doubleClickAction) {
                case RequirementDoubleClickAction::ToggleDetail: {
                    if (RequirementNode* node = model.GetNode(nodeId)) {
                        node->detail = (node->detail == RequirementDetailLevel::Collapsed)
                                           ? RequirementDetailLevel::Full
                                           : RequirementDetailLevel::Collapsed;
                        InvalidateMeasurement();
                    }
                    break;
                }
                case RequirementDoubleClickAction::ToggleCollapse:
                    ToggleNodeCollapsed(nodeId);
                    break;
                case RequirementDoubleClickAction::Rename:
                    BeginRename(nodeId);
                    break;
                case RequirementDoubleClickAction::NoAction:
                    break;
            }
            if (onNodeDoubleClick) onNodeDoubleClick(nodeId);
            return true;
        }
        case UCEventType::KeyDown:   return HandleKeyDown(event);
        case UCEventType::TextInput: return HandleTextInput(event);
        default: break;
    }
    return UltraCanvasUIElement::OnEvent(event);
}

bool UltraCanvasRequirementDiagram::HandleMouseDown(const UCEvent& event) {
    const Point2Di mousePos(event.pointer.x, event.pointer.y);
    if (!Contains(mousePos)) return false;
    if (!isInteractive && event.button != UCMouseButton::Middle) return false;

    lastMousePos = mousePos;
    dragStartPos = mousePos;

    // Take keyboard focus, or Delete / Ctrl+A / F2 and the inline rename
    // editor never see a key event.
    SetFocus(true);

    // An open inline editor commits when the user clicks away.
    if (!renamingNodeId.empty()) CommitRename();

    if (event.button == UCMouseButton::Right) {
        if (FindNodeAt(mousePos).empty() && FindCalloutAt(mousePos).empty() &&
            FindRelationAt(mousePos).empty() && onCanvasRightClick) {
            const Point2Dd world = ScreenToWorld(mousePos);
            onCanvasRightClick(world.x, world.y);
        }
        return true;
    }

    // ---- controls overlay (screen space, above everything) ---------------
    const int controlIndex = FindControlButtonAt(mousePos);
    if (controlIndex >= 0) {
        int index = 0;
        if (controlsConfig.showZoom) {
            if (controlIndex == index++) { ZoomIn(); return true; }
            if (controlIndex == index++) { ZoomOut(); return true; }
        }
        if (controlsConfig.showFit && controlIndex == index++) { FitView(); return true; }
        if (controlsConfig.showLock && controlIndex == index) {
            isInteractive = !isInteractive;
            RequestRedraw();
            return true;
        }
        return true;
    }

    if (PointInMinimap(mousePos)) {
        if (minimapConfig.pannable) {
            isDraggingMinimap = true;
            HandleMouseMove(event);   // jump to the clicked spot immediately
        }
        return true;
    }

    if (event.button == UCMouseButton::Middle || editMode == RequirementEditMode::Pan) {
        isDraggingViewport = true;
        return true;
    }

    // Expand/collapse toggles sit above everything else.
    const std::string toggleId = FindCollapseToggleAt(mousePos);
    if (!toggleId.empty()) {
        ToggleNodeCollapsed(toggleId);
        return true;
    }

    // ---- CreateNode: click empty canvas -----------------------------------
    if (editMode == RequirementEditMode::CreateNode && FindNodeAt(mousePos).empty()) {
        const Point2Dd world = SnapPoint(ScreenToWorld(mousePos));
        const std::string newId = model.GenerateNodeId(
            pendingNodeKind == RequirementNodeKind::Requirement ? "R" : "E");
        RequirementNode node(newId, newId);
        node.kind = pendingNodeKind;
        node.detail = (pendingNodeKind == RequirementNodeKind::Requirement)
                          ? defaultDetail : RequirementDetailLevel::Collapsed;
        node.x = world.x;
        node.y = world.y;
        node.pinned = true;
        if (AddNode(node)) {
            SelectNode(newId);
            if (onNodeCreated) onNodeCreated(newId);
            BeginRename(newId);
        }
        return true;
    }

    // ---- CreateRelation: drag from one box to another ---------------------
    if (editMode == RequirementEditMode::CreateRelation) {
        const std::string nodeId = FindNodeAt(mousePos);
        if (!nodeId.empty()) {
            isConnecting = true;
            connectionSourceId = nodeId;
            connectionEndPoint = ScreenToWorld(mousePos);
            return true;
        }
    }

    // Callouts sit above the nodes and drag independently.
    const std::string calloutId = FindCalloutAt(mousePos);
    if (!calloutId.empty()) {
        for (auto& c : model.GetCallouts()) c.isSelected = (c.id == calloutId);
        if (auto* callout = model.GetCallout(calloutId)) {
            isDraggingCallout = true;
            draggedCalloutId = calloutId;
            calloutDragStart = Point2Dd(callout->x, callout->y);
        }
        RequestRedraw();
        return true;
    }
    for (auto& c : model.GetCallouts()) c.isSelected = false;

    const std::string nodeId = FindNodeAt(mousePos);
    if (!nodeId.empty()) {
        if (event.shift) {
            if (selectedNodes.count(nodeId)) selectedNodes.erase(nodeId);
            else selectedNodes.insert(nodeId);
        } else if (!selectedNodes.count(nodeId)) {
            selectedNodes.clear();
            selectedRelations.clear();
            selectedNodes.insert(nodeId);
        }
        NotifySelectionChange();

        if (nodesDraggable) {
            isDraggingNode = true;
            dragStartPositions.clear();
            for (const auto& id : selectedNodes) {
                if (const auto* n = model.GetNode(id)) {
                    dragStartPositions[id] = Point2Dd(n->x, n->y);
                }
            }
        }
        if (onNodeClick) onNodeClick(nodeId);
        RequestRedraw();
        return true;
    }

    const std::string relationId = FindRelationAt(mousePos);
    if (!relationId.empty()) {
        SelectRelation(relationId, event.shift);
        if (onRelationClick) onRelationClick(relationId);
        return true;
    }

    // Empty canvas: rubber-band select, or pan.
    if (event.shift || !panOnDrag) {
        isSelectingBox = true;
        selectionBoxStart = ScreenToWorld(mousePos);
        selectionBoxEnd = selectionBoxStart;
        if (!event.shift) {
            selectedNodes.clear();
            selectedRelations.clear();
            NotifySelectionChange();
        }
    } else {
        isDraggingViewport = true;
        selectedNodes.clear();
        selectedRelations.clear();
        NotifySelectionChange();
    }
    RequestRedraw();
    return true;
}

bool UltraCanvasRequirementDiagram::HandleMouseMove(const UCEvent& event) {
    const Point2Di mousePos(event.pointer.x, event.pointer.y);

    if (isDraggingMinimap) {
        // Map the cursor back through the minimap's transform and centre there.
        const Rect2Dd panel = MinimapBounds();
        const Rect2Dd content = GetContentBounds();
        if (content.width > 0.0 && content.height > 0.0) {
            const double inset = 6.0;
            const double scale = std::min((panel.width - inset * 2.0) / content.width,
                                          (panel.height - inset * 2.0) / content.height);
            const double originX = panel.x + inset +
                ((panel.width - inset * 2.0) - content.width * scale) / 2.0;
            const double originY = panel.y + inset +
                ((panel.height - inset * 2.0) - content.height * scale) / 2.0;
            CenterOn(content.x + (mousePos.x - originX) / scale,
                     content.y + (mousePos.y - originY) / scale);
        }
        return true;
    }

    if (controlsConfig.visible) {
        const int hovered = FindControlButtonAt(mousePos);
        if (hovered != hoveredControlButton) {
            hoveredControlButton = hovered;
            RequestRedraw();
        }
        if (hovered >= 0) {
            SetMouseCursor(UCMouseCursor::Hand);
            return true;
        }
    }

    if (isDraggingViewport) {
        panOffset.x += mousePos.x - lastMousePos.x;
        panOffset.y += mousePos.y - lastMousePos.y;
        lastMousePos = mousePos;
        NotifyViewportChange();
        RequestRedraw();
        return true;
    }

    if (isConnecting) {
        connectionEndPoint = ScreenToWorld(mousePos);
        RequestRedraw();
        return true;
    }

    if (isDraggingCallout) {
        if (auto* callout = model.GetCallout(draggedCalloutId)) {
            const Point2Dd delta((mousePos.x - dragStartPos.x) / zoomLevel,
                                 (mousePos.y - dragStartPos.y) / zoomLevel);
            const Point2Dd moved = SnapPoint(Point2Dd(calloutDragStart.x + delta.x,
                                                      calloutDragStart.y + delta.y));
            callout->x = moved.x;
            callout->y = moved.y;
            RequestRedraw();
        }
        return true;
    }

    if (isDraggingNode) {
        const Point2Dd delta((mousePos.x - dragStartPos.x) / zoomLevel,
                             (mousePos.y - dragStartPos.y) / zoomLevel);
        for (const auto& [id, startPos] : dragStartPositions) {
            RequirementNode* node = model.GetNode(id);
            if (!node) continue;
            const Point2Dd moved = SnapPoint(Point2Dd(startPos.x + delta.x,
                                                      startPos.y + delta.y));
            node->x = moved.x;
            node->y = moved.y;
            // A dragged node keeps its position through the next auto-layout.
            node->pinned = true;
            if (onNodeDrag) onNodeDrag(id, node->x, node->y);
        }
        InvalidateRouting();
        return true;
    }

    if (isSelectingBox) {
        selectionBoxEnd = ScreenToWorld(mousePos);
        RequestRedraw();
        return true;
    }

    if (!Contains(mousePos)) {
        if (!hoveredNodeId.empty() || !hoveredRelationId.empty() || !hoveredToggleId.empty()) {
            hoveredNodeId.clear();
            hoveredRelationId.clear();
            hoveredToggleId.clear();
            for (const auto& id : model.GetNodeOrder()) {
                if (RequirementNode* node = model.GetNode(id)) node->isHovered = false;
            }
            for (auto& r : model.GetRelations()) r.isHovered = false;
            RequestRedraw();
        }
        return false;
    }

    // Hover feedback + tooltip.
    const std::string toggleId = FindCollapseToggleAt(mousePos);
    const std::string nodeId = toggleId.empty() ? FindNodeAt(mousePos) : std::string();
    const std::string relationId = (toggleId.empty() && nodeId.empty())
                                       ? FindRelationAt(mousePos) : std::string();

    if (nodeId != hoveredNodeId || relationId != hoveredRelationId ||
        toggleId != hoveredToggleId) {
        hoveredNodeId = nodeId;
        hoveredRelationId = relationId;
        hoveredToggleId = toggleId;
        for (const auto& id : model.GetNodeOrder()) {
            if (RequirementNode* node = model.GetNode(id)) {
                node->isHovered = (id == hoveredNodeId);
            }
        }
        for (auto& r : model.GetRelations()) r.isHovered = (r.id == hoveredRelationId);

        if (tooltipsEnabled) {
            std::string tooltip;
            if (const auto* node = model.GetNode(nodeId)) {
                tooltip = node->name;
                if (!node->text.empty()) tooltip += "\n" + node->text;
            } else if (const auto* relation = model.GetRelation(relationId)) {
                tooltip = std::string(RequirementRelationKindToString(relation->kind)) +
                          ": " + relation->sourceId + " -> " + relation->targetId;
                if (!relation->rationale.empty()) tooltip += "\n" + relation->rationale;
            } else if (!toggleId.empty()) {
                tooltip = IsNodeCollapsed(toggleId) ? "Expand sub-tree" : "Collapse sub-tree";
            }
            SetTooltip(tooltip);
        }
        if (!nodeId.empty() && onNodeHover) onNodeHover(nodeId);
        SetMouseCursor((nodeId.empty() && toggleId.empty()) ? UCMouseCursor::Default
                                                             : UCMouseCursor::Hand);
        RequestRedraw();
    }
    lastMousePos = mousePos;
    return false;
}

bool UltraCanvasRequirementDiagram::HandleMouseUp(const UCEvent& event) {
    const Point2Di mousePos(event.pointer.x, event.pointer.y);

    if (isConnecting) {
        const std::string targetId = FindNodeAt(mousePos);
        isConnecting = false;
        if (!targetId.empty() && targetId != connectionSourceId) {
            const std::string relationId =
                AddRelation(pendingRelationKind, connectionSourceId, targetId);
            if (!relationId.empty() && onRelationCreated) {
                if (const auto* created = model.GetRelation(relationId)) {
                    onRelationCreated(*created);
                }
            }
        }
        connectionSourceId.clear();
        RequestRedraw();
        return true;
    }

    if (isSelectingBox) {
        const Rect2Dd box(std::min(selectionBoxStart.x, selectionBoxEnd.x),
                          std::min(selectionBoxStart.y, selectionBoxEnd.y),
                          std::abs(selectionBoxEnd.x - selectionBoxStart.x),
                          std::abs(selectionBoxEnd.y - selectionBoxStart.y));
        for (const auto& id : model.GetNodeOrder()) {
            const RequirementNode* node = model.GetNode(id);
            if (!node || !IsDisplayed(*node)) continue;
            if (box.Intersects(NodeRect(*node))) selectedNodes.insert(id);
        }
        isSelectingBox = false;
        NotifySelectionChange();
        RequestRedraw();
        return true;
    }

    const bool wasInteracting = isDraggingNode || isDraggingViewport ||
                                isDraggingCallout || isDraggingMinimap;
    isDraggingNode = false;
    isDraggingViewport = false;
    isDraggingCallout = false;
    isDraggingMinimap = false;
    draggedCalloutId.clear();
    dragStartPositions.clear();
    return wasInteracting;
}

bool UltraCanvasRequirementDiagram::HandleMouseWheel(const UCEvent& event) {
    const Point2Di mousePos(event.pointer.x, event.pointer.y);
    if (!Contains(mousePos) || !zoomOnScroll) return false;
    if (PointInMinimap(mousePos) || FindControlButtonAt(mousePos) >= 0) return true;

    if (!zoomAnim.IsBound()) {
        zoomAnim.Bind([this](double f) { ApplyZoomFactorAtCursor(f, zoomCursor); },
                      [this] { NotifyViewportChange(); RequestRedraw(); });
    }
    zoomCursor = mousePos;
    zoomAnim.ZoomBy((event.wheelDelta > 0) ? 1.1 : (1.0 / 1.1),
                    zoomLevel, minZoom, maxZoom);
    return true;
}

// One zoom step about the cursor: the point under it stays fixed. A wheel notch
// is eased in as a run of these (UltraCanvasSmoothZoom), and applying them in a
// row about the same cursor is exactly applying their product once.
void UltraCanvasRequirementDiagram::ApplyZoomFactorAtCursor(double factor,
                                                            const Point2Di& cursor) {
    const double oldZoom = zoomLevel;
    zoomLevel *= factor;
    ClampZoom();
    if (oldZoom == zoomLevel) return;
    panOffset.x = cursor.x - (cursor.x - panOffset.x) * (zoomLevel / oldZoom);
    panOffset.y = cursor.y - (cursor.y - panOffset.y) * (zoomLevel / oldZoom);
}

bool UltraCanvasRequirementDiagram::HandleTextInput(const UCEvent& event) {
    if (renamingNodeId.empty() || event.text.empty()) return false;
    if (renameFresh) { renameBuffer.clear(); renameFresh = false; }
    renameBuffer += event.text;
    RequestRedraw();
    return true;
}

bool UltraCanvasRequirementDiagram::HandleKeyDown(const UCEvent& event) {
    // The inline editor swallows keys while it is open.
    if (!renamingNodeId.empty()) {
        switch (event.virtualKey) {
            case UCKeys::Return:
                CommitRename();
                return true;
            case UCKeys::Escape:
                CancelRename();
                return true;
            case UCKeys::Backspace:
                renameFresh = false;   // editing, not replacing
                if (!renameBuffer.empty()) {
                    // Step back over a whole UTF-8 sequence, not one byte.
                    size_t cut = renameBuffer.size() - 1;
                    while (cut > 0 &&
                           (static_cast<unsigned char>(renameBuffer[cut]) & 0xC0) == 0x80) {
                        cut--;
                    }
                    renameBuffer.resize(cut);
                    RequestRedraw();
                }
                return true;
            default:
                if (event.character >= 32 && event.character != 127) {
                    if (renameFresh) { renameBuffer.clear(); renameFresh = false; }
                    renameBuffer += event.character;
                    RequestRedraw();
                    return true;
                }
                return true;
        }
    }

    switch (event.virtualKey) {
        case UCKeys::Escape:
            if (highlightActive) ClearHighlight();
            DeselectAll();
            isSelectingBox = false;
            isConnecting = false;
            return true;
        case UCKeys::Delete:
            if (!selectedNodes.empty() || !selectedRelations.empty()) {
                DeleteSelected();
                return true;
            }
            return false;
        case UCKeys::A:
            if (event.ctrl) { SelectAll(); return true; }
            break;
        case UCKeys::F2:
            if (selectedNodes.size() == 1) { BeginRename(*selectedNodes.begin()); return true; }
            break;
        default:
            break;
    }
    return false;
}

// =============================================================================
// SERIALIZATION
// =============================================================================

std::string UltraCanvasRequirementDiagram::ToJson(bool pretty) const {
    JSONValue root = JSONValue::MakeObject();
    root.Set("version", JSONValue("2.0"));

    JSONValue viewport = JSONValue::MakeObject();
    viewport.Set("zoom", JSONValue(zoomLevel));
    viewport.Set("panX", JSONValue(panOffset.x));
    viewport.Set("panY", JSONValue(panOffset.y));
    root.Set("viewport", std::move(viewport));

    JSONValue diagram = JSONValue::MakeObject();
    diagram.Set("palette", JSONValue(static_cast<int>(paletteKind)));
    diagram.Set("colorSource", JSONValue(static_cast<int>(colorSource)));
    diagram.Set("layoutMode", JSONValue(static_cast<int>(layoutMode)));
    diagram.Set("orientation", JSONValue(static_cast<int>(orientation)));
    diagram.Set("defaultRouting", JSONValue(static_cast<int>(defaultRouting)));
    diagram.Set("semantics", JSONValue(static_cast<int>(model.GetSemanticsMode())));
    diagram.Set("obstacleAvoidance", JSONValue(obstacleAvoidance));
    diagram.Set("coverageOverlay", JSONValue(coverageOverlay));
    diagram.Set("riskStripe", JSONValue(showRiskStripe));
    diagram.Set("title", JSONValue(titleConfig.title));
    diagram.Set("subtitle", JSONValue(titleConfig.subtitle));
    diagram.Set("titleVisible", JSONValue(titleConfig.visible));
    diagram.Set("legendVisible", JSONValue(legendConfig.visible));
    diagram.Set("legendSource", JSONValue(static_cast<int>(legendConfig.source)));
    diagram.Set("frameVisible", JSONValue(frameConfig.visible));
    diagram.Set("frameElementType", JSONValue(frameConfig.elementType));
    diagram.Set("frameElementName", JSONValue(frameConfig.elementName));
    diagram.Set("frameDiagramName", JSONValue(frameConfig.diagramName));
    root.Set("diagram", std::move(diagram));

    JSONValue nodeArray = JSONValue::MakeArray();
    for (const auto& id : model.GetNodeOrder()) {
        const RequirementNode* node = model.GetNode(id);
        if (!node) continue;

        JSONValue value = JSONValue::MakeObject();
        value.Set("id", JSONValue(node->id));
        value.Set("name", JSONValue(node->name));
        value.Set("kind", JSONValue(RequirementNodeKindToString(node->kind)));
        if (!node->stereotype.empty()) value.Set("stereotype", JSONValue(node->stereotype));
        if (!node->externalId.empty()) value.Set("externalId", JSONValue(node->externalId));
        if (!node->text.empty())       value.Set("text", JSONValue(node->text));
        if (!node->source.empty())     value.Set("source", JSONValue(node->source));
        if (node->risk != RequirementRisk::Unspecified) {
            value.Set("risk", JSONValue(RequirementRiskToString(node->risk)));
        }
        if (node->verifyMethod != RequirementVerifyMethod::Unspecified) {
            value.Set("verifyMethod",
                      JSONValue(RequirementVerifyMethodToString(node->verifyMethod)));
        }
        if (!node->status.empty())   value.Set("status", JSONValue(node->status));
        if (!node->owner.empty())    value.Set("owner", JSONValue(node->owner));
        if (!node->priority.empty()) value.Set("priority", JSONValue(node->priority));
        if (!node->docRef.empty())   value.Set("docRef", JSONValue(node->docRef));
        if (!node->category.empty()) value.Set("category", JSONValue(node->category));
        if (!node->anchorId.empty()) value.Set("anchorId", JSONValue(node->anchorId));
        value.Set("detail", JSONValue(static_cast<int>(node->detail)));
        value.Set("shape", JSONValue(static_cast<int>(node->shape)));
        value.Set("x", JSONValue(node->x));
        value.Set("y", JSONValue(node->y));
        value.Set("width", JSONValue(node->width));
        value.Set("height", JSONValue(node->height));
        value.Set("pinned", JSONValue(node->pinned));
        if (node->collapsed) value.Set("collapsed", JSONValue(true));
        if (node->hasFillColor)   value.Set("fillColor", JSON::FromColor(node->fillColor));
        if (node->hasBorderColor) value.Set("borderColor", JSON::FromColor(node->borderColor));

        if (!node->customProperties.empty()) {
            JSONValue custom = JSONValue::MakeObject();
            for (const auto& [key, propertyValue] : node->customProperties) {
                custom.Set(key, JSONValue(propertyValue));
            }
            value.Set("customProperties", std::move(custom));
        }
        nodeArray.Append(std::move(value));
    }
    root.Set("nodes", std::move(nodeArray));

    JSONValue relationArray = JSONValue::MakeArray();
    for (const auto& relation : model.GetRelations()) {
        JSONValue value = JSONValue::MakeObject();
        value.Set("id", JSONValue(relation.id));
        value.Set("kind", JSONValue(RequirementRelationKindToString(relation.kind)));
        value.Set("source", JSONValue(relation.sourceId));
        value.Set("target", JSONValue(relation.targetId));
        if (!relation.label.empty())     value.Set("label", JSONValue(relation.label));
        if (!relation.rationale.empty()) value.Set("rationale", JSONValue(relation.rationale));
        if (relation.hasColor)           value.Set("color", JSON::FromColor(relation.color));
        if (!relation.visible)           value.Set("visible", JSONValue(false));
        if (!relation.useDefaultRouting) {
            value.Set("routing", JSONValue(static_cast<int>(relation.routing)));
        }
        relationArray.Append(std::move(value));
    }
    root.Set("relations", std::move(relationArray));

    JSONValue calloutArray = JSONValue::MakeArray();
    for (const auto& callout : model.GetCallouts()) {
        JSONValue value = JSONValue::MakeObject();
        value.Set("id", JSONValue(callout.id));
        value.Set("target", JSONValue(callout.targetNodeId));
        value.Set("x", JSONValue(callout.x));
        value.Set("y", JSONValue(callout.y));
        value.Set("width", JSONValue(callout.width));
        if (!callout.headerText.empty()) value.Set("header", JSONValue(callout.headerText));
        JSONValue fieldArray = JSONValue::MakeArray();
        for (RequirementField field : callout.fields) {
            fieldArray.Append(JSONValue(static_cast<int>(field)));
        }
        value.Set("fields", std::move(fieldArray));
        calloutArray.Append(std::move(value));
    }
    root.Set("callouts", std::move(calloutArray));

    JSONValue categoryArray = JSONValue::MakeArray();
    for (const auto& name : model.GetCategoryOrder()) {
        const RequirementCategory* category = model.GetCategory(name);
        if (!category) continue;
        JSONValue value = JSONValue::MakeObject();
        value.Set("name", JSONValue(category->name));
        value.Set("fillColor", JSON::FromColor(category->fillColor));
        value.Set("borderColor", JSON::FromColor(category->borderColor));
        categoryArray.Append(std::move(value));
    }
    root.Set("categories", std::move(categoryArray));

    JSONSerializeOptions options;
    options.pretty = pretty;
    options.indentWidth = 2;
    return JSON::Serialize(root, options);
}

bool UltraCanvasRequirementDiagram::FromJson(const std::string& json) {
    JSONParseResult result;
    const JSONValue root = JSON::Parse(json, &result);
    if (!result.success || !root.IsObject()) return false;

    Clear();

    const JSONValue& diagram = root.Get("diagram");
    if (diagram.IsObject()) {
        SetPalette(static_cast<RequirementPaletteKind>(
            diagram.Get("palette").GetInteger(static_cast<int>(RequirementPaletteKind::Classic))));
        colorSource = static_cast<RequirementColorSource>(
            diagram.Get("colorSource").GetInteger(static_cast<int>(RequirementColorSource::ByKind)));
        layoutMode = static_cast<RequirementLayoutMode>(
            diagram.Get("layoutMode").GetInteger(static_cast<int>(RequirementLayoutMode::Manual)));
        orientation = static_cast<RequirementOrientation>(
            diagram.Get("orientation").GetInteger(static_cast<int>(RequirementOrientation::TopDown)));
        defaultRouting = static_cast<RequirementRouting>(
            diagram.Get("defaultRouting").GetInteger(static_cast<int>(RequirementRouting::Orthogonal)));
        model.SetSemanticsMode(static_cast<RequirementSemanticsMode>(
            diagram.Get("semantics").GetInteger(
                static_cast<int>(RequirementSemanticsMode::Lenient))));
        obstacleAvoidance = diagram.Get("obstacleAvoidance").GetBoolean(false);
        coverageOverlay = diagram.Get("coverageOverlay").GetBoolean(false);
        showRiskStripe = diagram.Get("riskStripe").GetBoolean(false);
        titleConfig.title = diagram.Get("title").GetString();
        titleConfig.subtitle = diagram.Get("subtitle").GetString();
        titleConfig.visible = diagram.Get("titleVisible").GetBoolean(false);
        legendConfig.visible = diagram.Get("legendVisible").GetBoolean(false);
        legendConfig.source = static_cast<RequirementLegendSource>(
            diagram.Get("legendSource").GetInteger(
                static_cast<int>(RequirementLegendSource::Categories)));
        frameConfig.visible = diagram.Get("frameVisible").GetBoolean(false);
        frameConfig.elementType = diagram.Get("frameElementType").GetString("Package");
        frameConfig.elementName = diagram.Get("frameElementName").GetString();
        frameConfig.diagramName = diagram.Get("frameDiagramName").GetString();
    }

    const JSONValue& categoryArray = root.Get("categories");
    for (size_t i = 0; i < categoryArray.GetSize(); ++i) {
        const JSONValue& value = categoryArray.At(i);
        RequirementCategory category;
        category.name = value.Get("name").GetString();
        JSON::ToColor(value.Get("fillColor"), category.fillColor);
        JSON::ToColor(value.Get("borderColor"), category.borderColor);
        AddCategory(category);
    }

    const JSONValue& nodeArray = root.Get("nodes");
    for (size_t i = 0; i < nodeArray.GetSize(); ++i) {
        const JSONValue& value = nodeArray.At(i);
        RequirementNode node;
        node.id = value.Get("id").GetString();
        node.name = value.Get("name").GetString();
        node.kind = RequirementNodeKindFromString(value.Get("kind").GetString("requirement"));
        node.stereotype = value.Get("stereotype").GetString();
        node.externalId = value.Get("externalId").GetString();
        node.text = value.Get("text").GetString();
        node.source = value.Get("source").GetString();
        node.risk = RequirementRiskFromString(value.Get("risk").GetString());
        node.verifyMethod =
            RequirementVerifyMethodFromString(value.Get("verifyMethod").GetString());
        node.status = value.Get("status").GetString();
        node.owner = value.Get("owner").GetString();
        node.priority = value.Get("priority").GetString();
        node.docRef = value.Get("docRef").GetString();
        node.category = value.Get("category").GetString();
        node.anchorId = value.Get("anchorId").GetString();
        node.detail = static_cast<RequirementDetailLevel>(
            value.Get("detail").GetInteger(static_cast<int>(RequirementDetailLevel::Full)));
        node.shape = static_cast<RequirementNodeShape>(
            value.Get("shape").GetInteger(static_cast<int>(RequirementNodeShape::Auto)));
        node.x = value.Get("x").GetNumber(0.0);
        node.y = value.Get("y").GetNumber(0.0);
        node.width = value.Get("width").GetNumber(0.0);
        node.height = value.Get("height").GetNumber(0.0);
        node.pinned = value.Get("pinned").GetBoolean(false);
        node.collapsed = value.Get("collapsed").GetBoolean(false);
        node.hasFillColor = JSON::ToColor(value.Get("fillColor"), node.fillColor);
        node.hasBorderColor = JSON::ToColor(value.Get("borderColor"), node.borderColor);

        const JSONValue& custom = value.Get("customProperties");
        if (custom.IsObject()) {
            for (const auto& [key, propertyValue] : custom.GetMembers()) {
                node.customProperties[key] = propertyValue.GetString();
            }
        }
        // Straight through the model: AddNode() here would re-apply the
        // detail-level defaulting and change what was saved.
        model.AddNode(node);
    }

    const JSONValue& relationArray = root.Get("relations");
    for (size_t i = 0; i < relationArray.GetSize(); ++i) {
        const JSONValue& value = relationArray.At(i);
        RequirementRelation relation;
        relation.id = value.Get("id").GetString();
        relation.kind = RequirementRelationKindFromString(value.Get("kind").GetString("trace"));
        relation.sourceId = value.Get("source").GetString();
        relation.targetId = value.Get("target").GetString();
        relation.label = value.Get("label").GetString();
        relation.rationale = value.Get("rationale").GetString();
        relation.hasColor = JSON::ToColor(value.Get("color"), relation.color);
        relation.visible = value.Get("visible").GetBoolean(true);
        if (value.Contains("routing")) {
            relation.routing = static_cast<RequirementRouting>(
                value.Get("routing").GetInteger(static_cast<int>(RequirementRouting::Orthogonal)));
            relation.useDefaultRouting = false;
        }
        model.AddRelation(relation);
    }

    const JSONValue& calloutArray = root.Get("callouts");
    for (size_t i = 0; i < calloutArray.GetSize(); ++i) {
        const JSONValue& value = calloutArray.At(i);
        RequirementCallout callout;
        callout.id = value.Get("id").GetString();
        callout.targetNodeId = value.Get("target").GetString();
        callout.x = value.Get("x").GetNumber(0.0);
        callout.y = value.Get("y").GetNumber(0.0);
        callout.width = value.Get("width").GetNumber(220.0);
        callout.headerText = value.Get("header").GetString();
        const JSONValue& fieldArray = value.Get("fields");
        for (size_t f = 0; f < fieldArray.GetSize(); ++f) {
            callout.fields.push_back(
                static_cast<RequirementField>(fieldArray.At(f).GetInteger(0)));
        }
        model.AddCallout(callout);
    }

    const JSONValue& viewport = root.Get("viewport");
    if (viewport.IsObject()) {
        zoomLevel = viewport.Get("zoom").GetNumber(1.0);
        panOffset.x = viewport.Get("panX").GetNumber(0.0);
        panOffset.y = viewport.Get("panY").GetNumber(0.0);
        ClampZoom();
        // An explicit viewport in the file wins over the auto-fit a
        // ContainmentTree layout would otherwise apply.
        autoFitOnLayout = false;
    }

    InvalidateMeasurement();
    return true;
}

std::string UltraCanvasRequirementDiagram::ToMermaid(const std::string& direction) const {
    return model.ToMermaid(direction);
}

bool UltraCanvasRequirementDiagram::FromMermaid(const std::string& text,
                                                 std::string* outError) {
    std::string direction;
    // Clear the view state first; the model clears itself inside FromMermaid.
    selectedNodes.clear();
    selectedRelations.clear();
    measuredNodes.clear();
    measuredCallouts.clear();
    routeCache.clear();
    highlightedNodes.clear();
    highlightActive = false;

    if (!model.FromMermaid(text, outError, &direction)) return false;

    // Mermaid's direction directive maps onto the layout orientation.
    if (direction == "BT")      orientation = RequirementOrientation::BottomUp;
    else if (direction == "LR") orientation = RequirementOrientation::LeftRight;
    else if (direction == "RL") orientation = RequirementOrientation::RightLeft;
    else if (direction == "TB" || direction == "TD") orientation = RequirementOrientation::TopDown;

    layoutMode = RequirementLayoutMode::ContainmentTree;
    autoFitOnLayout = true;
    InvalidateMeasurement();
    RunLayout();
    return true;
}

std::string UltraCanvasRequirementDiagram::ToCsv(const RequirementCsvSchema& schema) const {
    return model.ToCsv(schema);
}

bool UltraCanvasRequirementDiagram::FromCsv(const std::string& text,
                                             const RequirementCsvSchema& schema,
                                             std::string* outError) {
    selectedNodes.clear();
    selectedRelations.clear();
    measuredNodes.clear();
    measuredCallouts.clear();
    routeCache.clear();
    highlightedNodes.clear();
    highlightActive = false;

    if (!model.FromCsv(text, schema, outError)) return false;

    layoutMode = RequirementLayoutMode::ContainmentTree;
    autoFitOnLayout = true;
    InvalidateMeasurement();
    RunLayout();
    return true;
}


// =============================================================================
// PHASE 3 DECORATIONS
// =============================================================================

void UltraCanvasRequirementDiagram::RenderStatusBadge(IRenderContext* ctx,
                                                       const RequirementNode& node) {
    if (node.status.empty()) return;

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(std::max(7.0, style.baseFontSize - 1.5));
    const Size2Di dim = ctx->GetTextLineDimensions(node.status);
    const double padding = 4.0;
    const double badgeWidth = dim.width + padding * 2.0;
    const double badgeHeight = dim.height + 2.0;
    // Hangs ABOVE the top edge, right-aligned: inside the box it would sit on
    // the centred «stereotype» and name lines, and the top-right corner
    // already carries the coverage badge.
    const double x = node.x + node.width - badgeWidth - 2.0;
    const double y = node.y - badgeHeight - 2.0;

    // A status the caller gave a colour to uses it; otherwise the neutral chip.
    Color fill = palette.statusBadgeColor;
    auto colorIt = statusColors.find(node.status);
    if (colorIt != statusColors.end()) fill = colorIt->second;

    ctx->SetLineDash(UCDashPattern::EMPTY);
    ctx->DrawFilledRectangle(Rect2Dd(x, y, badgeWidth, badgeHeight),
                             ApplyDim(fill, node.dimmed), 0.0f, Colors::Transparent, 3.0f);
    ctx->SetTextPaint(ApplyDim(palette.statusBadgeTextColor, node.dimmed));
    ctx->DrawText(node.status, Point2Dd(x + padding, y + 1.0));
}

void UltraCanvasRequirementDiagram::RenderPorts(IRenderContext* ctx,
                                                 const RequirementNode& node) {
    auto it = nodePorts.find(node.id);
    if (it == nodePorts.end() || it->second <= 0) return;

    const int ports = it->second;
    const double size = 7.0;
    for (int i = 0; i < ports; ++i) {
        const double fraction = (static_cast<double>(i) + 1.0) /
                                (static_cast<double>(ports) + 1.0);
        const double cx = node.x + node.width * fraction;
        ctx->DrawFilledRectangle(
            Rect2Dd(cx - size / 2.0, node.y - size / 2.0, size, size),
            ApplyDim(palette.backgroundColor, node.dimmed), 1.0f,
            ApplyDim(palette.portColor, node.dimmed), 0.0f);
    }
}

void UltraCanvasRequirementDiagram::RenderSuspectBadge(IRenderContext* ctx,
                                                        const RequirementNode& node) {
    if (!node.suspect) return;
    // A copy whose master moved on: an exclamation disc on the bottom-left,
    // clear of the coverage badge and the collapse toggle.
    const double radius = 6.0;
    const Point2Dd centre(node.x + radius + 3.0, node.y + node.height - radius - 3.0);
    const Color color = ApplyDim(palette.suspectColor, node.dimmed);

    ctx->SetLineDash(UCDashPattern::EMPTY);
    ctx->DrawFilledCircle(centre, static_cast<float>(radius), color,
                          ApplyDim(palette.backgroundColor, node.dimmed), 1.0f);
    ctx->SetStrokePaint(ApplyDim(palette.backgroundColor, node.dimmed));
    ctx->SetStrokeWidth(1.6);
    ctx->DrawLine(Point2Dd(centre.x, centre.y - radius * 0.55),
                  Point2Dd(centre.x, centre.y + radius * 0.15));
    ctx->DrawLine(Point2Dd(centre.x, centre.y + radius * 0.45),
                  Point2Dd(centre.x, centre.y + radius * 0.55));
}

void UltraCanvasRequirementDiagram::RenderPackageRegions(IRenderContext* ctx) {
    // A dashed translucent band around everything a «package» contains - the
    // SysML package grouping, drawn behind the boxes.
    for (const auto& id : model.GetNodeOrder()) {
        const RequirementNode* package = model.GetNode(id);
        if (!package || package->kind != RequirementNodeKind::Package) continue;
        if (!IsDisplayed(*package)) continue;

        const std::vector<std::string> members = model.GetDescendants(id);
        if (members.empty()) continue;

        bool any = false;
        double minX = 0, minY = 0, maxX = 0, maxY = 0;
        for (const auto& memberId : members) {
            const RequirementNode* member = model.GetNode(memberId);
            if (!member || !IsDisplayed(*member)) continue;
            if (!any) {
                minX = member->x; minY = member->y;
                maxX = member->x + member->width; maxY = member->y + member->height;
                any = true;
            } else {
                minX = std::min(minX, member->x);
                minY = std::min(minY, member->y);
                maxX = std::max(maxX, member->x + member->width);
                maxY = std::max(maxY, member->y + member->height);
            }
        }
        if (!any) continue;

        const double m = packageRegionConfig.margin;
        const Rect2Dd region(minX - m, minY - m - packageRegionConfig.titleHeight,
                             (maxX - minX) + m * 2.0,
                             (maxY - minY) + m * 2.0 + packageRegionConfig.titleHeight);

        Color fill = ResolveFillColor(*package);
        fill.a = static_cast<uint8_t>(std::max(0.0, std::min(255.0,
                     255.0 * packageRegionConfig.fillAlpha)));
        ctx->SetFillPaint(fill);
        ctx->FillRoundedRectangle(region, packageRegionConfig.cornerRadius);

        ctx->SetLineDash(style.relationDash);
        ctx->SetStrokePaint(packageRegionConfig.borderColor);
        ctx->SetStrokeWidth(1.2);
        ctx->DrawRoundedRectangle(region, packageRegionConfig.cornerRadius);
        ctx->SetLineDash(UCDashPattern::EMPTY);

        ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
        ctx->SetFontSize(packageRegionConfig.fontSize);
        ctx->SetTextPaint(packageRegionConfig.textColor);
        ctx->DrawText("«package» " + package->name,
                      Point2Dd(region.x + 8.0, region.y + 3.0));
    }
}

void UltraCanvasRequirementDiagram::RenderRationaleNotes(IRenderContext* ctx) {
    // SysML lets a rationale hang off a relationship; drawn as a small note at
    // the middle of the route with a dashed leader.
    for (const auto& relation : model.GetRelations()) {
        if (relation.rationale.empty() || !relation.visible) continue;
        const RequirementNode* source = model.GetNode(relation.sourceId);
        const RequirementNode* target = model.GetNode(relation.targetId);
        if (!source || !target || !IsDisplayed(*source) || !IsDisplayed(*target)) continue;

        std::vector<Point2Dd> path;
        BuildRelationPath(relation, path);
        if (path.empty()) continue;
        const Point2Dd anchor = path[path.size() / 2];

        ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Italic);
        ctx->SetFontSize(style.baseFontSize - 0.5);
        const double maxWidth = 150.0;
        const std::vector<std::string> lines =
            WrapText(ctx, relation.rationale, maxWidth - 10.0, 3);
        if (lines.empty()) continue;

        double textWidth = 0.0, lineHeight = 0.0;
        for (const auto& line : lines) {
            textWidth = std::max(textWidth, static_cast<double>(ctx->GetTextLineWidth(line)));
            lineHeight = std::max(lineHeight, static_cast<double>(ctx->GetTextLineHeight(line)));
        }
        const double boxWidth = textWidth + 12.0;
        const double boxHeight = lineHeight * static_cast<double>(lines.size()) + 10.0;

        // Try a few placements around the anchor and take the first that does
        // not land on a box; a note drawn over a requirement is worse than a
        // note slightly further from its line.
        static const Point2Dd kOffsets[] = {
            {18.0, 12.0}, {18.0, -12.0}, {-18.0, 12.0}, {-18.0, -12.0},
            {0.0, 34.0}, {0.0, -34.0}, {44.0, 0.0}, {-44.0, 0.0}
        };
        Rect2Dd box(anchor.x + kOffsets[0].x, anchor.y + kOffsets[0].y, boxWidth, boxHeight);
        for (const auto& offset : kOffsets) {
            Rect2Dd candidate(offset.x >= 0.0 ? anchor.x + offset.x
                                              : anchor.x + offset.x - boxWidth,
                              offset.y >= 0.0 ? anchor.y + offset.y
                                              : anchor.y + offset.y - boxHeight,
                              boxWidth, boxHeight);
            bool clear = true;
            for (const auto& id : model.GetNodeOrder()) {
                const RequirementNode* other = model.GetNode(id);
                if (!other || !IsDisplayed(*other)) continue;
                if (candidate.Intersects(NodeRect(*other))) { clear = false; break; }
            }
            box = candidate;
            if (clear) break;
        }

        ctx->SetLineDash(style.leaderDash);
        ctx->SetStrokePaint(ApplyDim(palette.noteBorder, relation.dimmed));
        ctx->SetStrokeWidth(style.relationLineWidth);
        ctx->DrawLine(anchor, Point2Dd(box.x, box.y + box.height / 2.0));
        ctx->SetLineDash(UCDashPattern::EMPTY);

        ctx->DrawFilledRectangle(box, ApplyDim(palette.noteFill, relation.dimmed), 1.0f,
                                 ApplyDim(palette.noteBorder, relation.dimmed), 3.0f);

        double y = box.y + 5.0;
        ctx->SetTextPaint(ApplyDim(palette.propertyTextColor, relation.dimmed));
        for (const auto& line : lines) {
            ctx->DrawText(line, Point2Dd(box.x + 6.0, y));
            y += lineHeight;
        }
        ctx->SetFontSlant(FontSlant::Normal);
    }
}

// =============================================================================
// MINIMAP & CONTROLS OVERLAYS
// =============================================================================

Rect2Dd UltraCanvasRequirementDiagram::MinimapBounds() const {
    const double topInset = (titleConfig.visible ? titleConfig.height : 0.0);
    double x = minimapConfig.padding;
    double y = minimapConfig.padding + topInset;
    switch (minimapConfig.position) {
        case RequirementPanelPosition::TopLeft: break;
        case RequirementPanelPosition::TopRight:
            x = GetWidth() - minimapConfig.width - minimapConfig.padding;
            break;
        case RequirementPanelPosition::BottomLeft:
            y = GetHeight() - minimapConfig.height - minimapConfig.padding;
            break;
        case RequirementPanelPosition::BottomRight:
            x = GetWidth() - minimapConfig.width - minimapConfig.padding;
            y = GetHeight() - minimapConfig.height - minimapConfig.padding;
            break;
    }
    return Rect2Dd(x, y, minimapConfig.width, minimapConfig.height);
}

bool UltraCanvasRequirementDiagram::PointInMinimap(const Point2Di& screenPos) const {
    if (!minimapConfig.visible) return false;
    return MinimapBounds().Contains(Point2Dd(screenPos.x, screenPos.y));
}

void UltraCanvasRequirementDiagram::RenderMinimap(IRenderContext* ctx) {
    const Rect2Dd panel = MinimapBounds();
    ctx->SetLineDash(UCDashPattern::EMPTY);
    ctx->DrawFilledRectangle(panel, minimapConfig.backgroundColor, 1.0f,
                             minimapConfig.borderColor, 3.0f);

    const Rect2Dd content = GetContentBounds();
    if (content.width <= 0.0 || content.height <= 0.0) return;

    const double inset = 6.0;
    const double scale = std::min((panel.width - inset * 2.0) / content.width,
                                  (panel.height - inset * 2.0) / content.height);
    const double originX = panel.x + inset +
                           ((panel.width - inset * 2.0) - content.width * scale) / 2.0;
    const double originY = panel.y + inset +
                           ((panel.height - inset * 2.0) - content.height * scale) / 2.0;

    for (const auto& id : model.GetNodeOrder()) {
        const RequirementNode* node = model.GetNode(id);
        if (!node || !IsDisplayed(*node)) continue;
        const Rect2Dd box(originX + (node->x - content.x) * scale,
                          originY + (node->y - content.y) * scale,
                          std::max(1.0, node->width * scale),
                          std::max(1.0, node->height * scale));
        ctx->SetFillPaint(node->isSelected ? palette.selectionColor : minimapConfig.nodeColor);
        ctx->FillRectangle(box);
    }

    // The viewport rectangle: the world region currently on screen.
    const Point2Dd topLeft = ScreenToWorld(Point2Di(0, 0));
    const Point2Dd bottomRight = ScreenToWorld(
        Point2Di(static_cast<int>(GetWidth()), static_cast<int>(GetHeight())));
    const Rect2Dd viewport(originX + (topLeft.x - content.x) * scale,
                           originY + (topLeft.y - content.y) * scale,
                           (bottomRight.x - topLeft.x) * scale,
                           (bottomRight.y - topLeft.y) * scale);
    ctx->SetFillPaint(minimapConfig.viewportFill);
    ctx->FillRectangle(viewport);
    ctx->SetStrokePaint(minimapConfig.viewportStroke);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRectangle(viewport);
}

int UltraCanvasRequirementDiagram::FindControlButtonAt(const Point2Di& screenPos) const {
    if (!controlsConfig.visible) return -1;

    int buttonCount = 0;
    if (controlsConfig.showZoom) buttonCount += 2;
    if (controlsConfig.showFit)  buttonCount += 1;
    if (controlsConfig.showLock) buttonCount += 1;
    if (buttonCount == 0) return -1;

    const double size = controlsConfig.buttonSize;
    const double gap = controlsConfig.gap;
    const double totalHeight = buttonCount * size + (buttonCount - 1) * gap;
    const double topInset = (titleConfig.visible ? titleConfig.height : 0.0);

    double x = controlsConfig.padding;
    double y = controlsConfig.padding + topInset;
    switch (controlsConfig.position) {
        case RequirementPanelPosition::TopLeft: break;
        case RequirementPanelPosition::TopRight:
            x = GetWidth() - size - controlsConfig.padding;
            break;
        case RequirementPanelPosition::BottomLeft:
            y = GetHeight() - totalHeight - controlsConfig.padding;
            break;
        case RequirementPanelPosition::BottomRight:
            x = GetWidth() - size - controlsConfig.padding;
            y = GetHeight() - totalHeight - controlsConfig.padding;
            break;
    }

    for (int i = 0; i < buttonCount; ++i) {
        const Rect2Dd button(x, y + i * (size + gap), size, size);
        if (button.Contains(Point2Dd(screenPos.x, screenPos.y))) return i;
    }
    return -1;
}

void UltraCanvasRequirementDiagram::RenderControls(IRenderContext* ctx) {
    int buttonCount = 0;
    if (controlsConfig.showZoom) buttonCount += 2;
    if (controlsConfig.showFit)  buttonCount += 1;
    if (controlsConfig.showLock) buttonCount += 1;
    if (buttonCount == 0) return;

    const double size = controlsConfig.buttonSize;
    const double gap = controlsConfig.gap;
    const double totalHeight = buttonCount * size + (buttonCount - 1) * gap;
    const double topInset = (titleConfig.visible ? titleConfig.height : 0.0);

    double x = controlsConfig.padding;
    double y = controlsConfig.padding + topInset;
    switch (controlsConfig.position) {
        case RequirementPanelPosition::TopLeft: break;
        case RequirementPanelPosition::TopRight:
            x = GetWidth() - size - controlsConfig.padding;
            break;
        case RequirementPanelPosition::BottomLeft:
            y = GetHeight() - totalHeight - controlsConfig.padding;
            break;
        case RequirementPanelPosition::BottomRight:
            x = GetWidth() - size - controlsConfig.padding;
            y = GetHeight() - totalHeight - controlsConfig.padding;
            break;
    }

    // Glyphs are drawn as vector primitives, not text characters: at 28px a
    // glyph font renders tiny and off-centre (NodeDiagram 2.0.1's lesson).
    ctx->SetLineDash(UCDashPattern::EMPTY);
    int index = 0;
    auto drawButton = [&](int which) {
        const Rect2Dd button(x, y + index * (size + gap), size, size);
        ctx->DrawFilledRectangle(button,
                                 hoveredControlButton == index ? controlsConfig.hoverColor
                                                               : controlsConfig.backgroundColor,
                                 1.0f, controlsConfig.borderColor, 3.0f);
        ctx->SetStrokePaint(controlsConfig.iconColor);
        ctx->SetStrokeWidth(1.8);
        const double cx = button.x + button.width / 2.0;
        const double cy = button.y + button.height / 2.0;
        const double arm = button.width * 0.22;

        switch (which) {
            case 0:                                    // zoom in: +
                ctx->DrawLine(Point2Dd(cx - arm, cy), Point2Dd(cx + arm, cy));
                ctx->DrawLine(Point2Dd(cx, cy - arm), Point2Dd(cx, cy + arm));
                break;
            case 1:                                    // zoom out: -
                ctx->DrawLine(Point2Dd(cx - arm, cy), Point2Dd(cx + arm, cy));
                break;
            case 2:                                    // fit: corner brackets
                ctx->DrawRectangle(Rect2Dd(cx - arm, cy - arm, arm * 2.0, arm * 2.0));
                break;
            case 3: {                                  // lock: shackle + body
                ctx->DrawRectangle(Rect2Dd(cx - arm, cy - arm * 0.2,
                                           arm * 2.0, arm * 1.4));
                if (!isInteractive) {
                    ctx->DrawLine(Point2Dd(cx - arm * 0.5, cy - arm * 0.2),
                                  Point2Dd(cx - arm * 0.5, cy - arm));
                    ctx->DrawLine(Point2Dd(cx - arm * 0.5, cy - arm),
                                  Point2Dd(cx + arm * 0.5, cy - arm));
                    ctx->DrawLine(Point2Dd(cx + arm * 0.5, cy - arm),
                                  Point2Dd(cx + arm * 0.5, cy - arm * 0.2));
                }
                break;
            }
            default: break;
        }
        index++;
    };

    if (controlsConfig.showZoom) { drawButton(0); drawButton(1); }
    if (controlsConfig.showFit)  drawButton(2);
    if (controlsConfig.showLock) drawButton(3);
}

// =============================================================================
// PHASE 3 API
// =============================================================================

int UltraCanvasRequirementDiagram::SyncCopies() {
    const int updated = model.SyncCopies();
    if (updated > 0) InvalidateMeasurement();
    return updated;
}

bool UltraCanvasRequirementDiagram::RefreshCopy(const std::string& nodeId) {
    if (!model.RefreshCopy(nodeId)) return false;
    InvalidateMeasurement();
    return true;
}

void UltraCanvasRequirementDiagram::SetSuspectBadgeVisible(bool visible) {
    suspectBadge = visible;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::FocusOnNode(const std::string& nodeId, bool select) {
    const RequirementNode* node = model.GetNode(nodeId);
    if (!node) return;

    // Expand every collapsed ancestor, or the "focused" node stays hidden.
    std::string current = model.GetParentId(nodeId);
    int guard = 0;
    while (!current.empty() && guard++ < 1000) {
        if (RequirementNode* parent = model.GetNode(current)) parent->collapsed = false;
        current = model.GetParentId(current);
    }
    if (guard > 0 && layoutMode != RequirementLayoutMode::Manual) {
        layoutDirty = true;
        InvalidateRouting();
    }

    if (select) SelectNode(nodeId);
    CenterOn(node->x + node->width / 2.0, node->y + node->height / 2.0);
}

std::string UltraCanvasRequirementDiagram::FindAndFocus(const std::string& query) {
    const std::vector<RequirementSearchHit> hits = model.FindNodes(query);
    if (hits.empty()) return "";
    FocusOnNode(hits.front().nodeId, true);
    return hits.front().nodeId;
}

std::string UltraCanvasRequirementDiagram::ToTraceMatrixCsv(char delimiter) const {
    return model.ToTraceMatrixCsv(model.BuildTraceMatrix(), delimiter);
}

void UltraCanvasRequirementDiagram::SetPackageRegionsVisible(bool visible) {
    packageRegionConfig.visible = visible;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetPackageRegionConfig(
        const RequirementPackageRegionConfig& config) {
    packageRegionConfig = config;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetMinimapVisible(bool visible) {
    minimapConfig.visible = visible;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetMinimapConfig(const RequirementMinimapConfig& config) {
    minimapConfig = config;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetControlsVisible(bool visible) {
    controlsConfig.visible = visible;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetControlsConfig(
        const RequirementControlsConfig& config) {
    controlsConfig = config;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetRationaleNotesVisible(bool visible) {
    rationaleNotes = visible;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetRelationRationale(const std::string& relationId,
                                                          const std::string& rationale) {
    if (auto* relation = model.GetRelation(relationId)) {
        relation->rationale = rationale;
        RequestRedraw();
    }
}

void UltraCanvasRequirementDiagram::SetNodePortCount(const std::string& nodeId, int ports) {
    if (ports <= 0) nodePorts.erase(nodeId);
    else nodePorts[nodeId] = ports;
    RequestRedraw();
}

bool UltraCanvasRequirementDiagram::FromReqIf(const std::string& xml, std::string* outError) {
    selectedNodes.clear();
    selectedRelations.clear();
    measuredNodes.clear();
    measuredCallouts.clear();
    routeCache.clear();
    highlightedNodes.clear();
    highlightActive = false;

    if (!model.FromReqIf(xml, outError)) return false;

    layoutMode = RequirementLayoutMode::ContainmentTree;
    autoFitOnLayout = true;
    InvalidateMeasurement();
    RunLayout();
    return true;
}

} // namespace UltraCanvas
