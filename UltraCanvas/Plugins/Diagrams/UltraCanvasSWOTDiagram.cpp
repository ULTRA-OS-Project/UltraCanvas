// Plugins/Diagrams/UltraCanvasSWOTDiagram.cpp
// Classic four-panel SWOT analysis infographic with multiple design presets
// Version: 1.0.0
// Last Modified: 2026-07-28
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasSWOTDiagram.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace UltraCanvas {

// =============================================================================
// LOCAL COLOR HELPERS
// =============================================================================

    static Color MixColors(const Color& a, const Color& b, float t) {
        auto mix = [t](int ca, int cb) {
            return static_cast<uint8_t>(std::clamp(
                    static_cast<int>(ca + (cb - ca) * t + 0.5f), 0, 255));
        };
        return Color(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), 255);
    }

    static Color DarkenColor(const Color& c, float t) {
        return MixColors(c, Color(0, 0, 0, 255), t);
    }

// =============================================================================
// CONSTRUCTION & DEFAULTS
// =============================================================================

    UltraCanvasSWOTDiagram::UltraCanvasSWOTDiagram(const std::string& id, int x, int y, int w, int h)
            : UltraCanvasChartElementBase(id, x, y, w, h) {
        enableSelection = true;
        enableTooltips = true;
        showGrid = false;
        backgroundColor = Color(250, 250, 252, 255);

        quadrants[0] = SWOTQuadrantConfig("Strengths", "S", Color(39, 174, 96, 255));
        quadrants[1] = SWOTQuadrantConfig("Weaknesses", "W", Color(231, 76, 60, 255));
        quadrants[2] = SWOTQuadrantConfig("Opportunities", "O", Color(41, 128, 185, 255));
        quadrants[3] = SWOTQuadrantConfig("Threats", "T", Color(230, 126, 34, 255));
    }

// =============================================================================
// DESIGN, THEME & CONFIGURATION
// =============================================================================

    void UltraCanvasSWOTDiagram::SetDesign(SWOTDesign d) {
        if (design == d) return;
        design = d;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasSWOTDiagram::SetDarkTheme(bool dark) {
        if (darkTheme == dark) return;
        darkTheme = dark;
        backgroundColor = dark ? Color(32, 35, 42, 255) : Color(250, 250, 252, 255);
        RequestRedraw();
    }

    void UltraCanvasSWOTDiagram::SetQuadrantConfig(SWOTQuadrant q, const SWOTQuadrantConfig& config) {
        quadrants[static_cast<size_t>(q)] = config;
        RequestRedraw();
    }

    void UltraCanvasSWOTDiagram::SetQuadrantTitle(SWOTQuadrant q, const std::string& title) {
        quadrants[static_cast<size_t>(q)].title = title;
        RequestRedraw();
    }

    void UltraCanvasSWOTDiagram::SetQuadrantAccentColor(SWOTQuadrant q, const Color& color) {
        quadrants[static_cast<size_t>(q)].accentColor = color;
        RequestRedraw();
    }

// =============================================================================
// DATA MANAGEMENT
// =============================================================================

    void UltraCanvasSWOTDiagram::AddItem(SWOTQuadrant q, const std::string& text) {
        items[static_cast<size_t>(q)].emplace_back(text);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasSWOTDiagram::AddItem(SWOTQuadrant q, const SWOTItem& item) {
        items[static_cast<size_t>(q)].push_back(item);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasSWOTDiagram::SetItems(SWOTQuadrant q, const std::vector<SWOTItem>& list) {
        items[static_cast<size_t>(q)] = list;
        hoveredItem = SWOTItemRef();
        selectedItem = SWOTItemRef();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasSWOTDiagram::RemoveItem(SWOTQuadrant q, size_t index) {
        auto& list = items[static_cast<size_t>(q)];
        if (index >= list.size()) return;
        list.erase(list.begin() + index);

        // Keep the selection/hover consistent with the shifted indices
        auto fix = [&](SWOTItemRef& ref) {
            if (ref.quadrant != static_cast<int>(q)) return;
            if (ref.item == static_cast<int>(index)) ref = SWOTItemRef();
            else if (ref.item > static_cast<int>(index)) ref.item--;
        };
        fix(selectedItem);
        fix(hoveredItem);

        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasSWOTDiagram::ClearItems(SWOTQuadrant q) {
        items[static_cast<size_t>(q)].clear();
        if (selectedItem.quadrant == static_cast<int>(q)) selectedItem = SWOTItemRef();
        if (hoveredItem.quadrant == static_cast<int>(q)) hoveredItem = SWOTItemRef();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasSWOTDiagram::ClearAllItems() {
        for (auto& list : items) list.clear();
        hoveredItem = SWOTItemRef();
        selectedItem = SWOTItemRef();
        InvalidateLayout();
        RequestRedraw();
    }

    size_t UltraCanvasSWOTDiagram::CountAllItems() const {
        size_t total = 0;
        for (const auto& list : items) total += list.size();
        return total;
    }

    void UltraCanvasSWOTDiagram::LoadSampleData() {
        auto sample = SWOTDiagramSamples::BusinessExample();
        for (size_t q = 0; q < kSWOTQuadrantCount; ++q) {
            items[q] = sample[q];
        }
        hoveredItem = SWOTItemRef();
        selectedItem = SWOTItemRef();
        InvalidateLayout();
        RequestRedraw();
    }

// =============================================================================
// VISUAL TOGGLES
// =============================================================================

    void UltraCanvasSWOTDiagram::SetShowBadges(bool show) {
        if (showBadges == show) return;
        showBadges = show;
        RequestRedraw();
    }

    void UltraCanvasSWOTDiagram::SetShowCenterElement(bool show) {
        if (showCenterElement == show) return;
        showCenterElement = show;
        RequestRedraw();
    }

    void UltraCanvasSWOTDiagram::SetShowItemBullets(bool show) {
        if (showItemBullets == show) return;
        showItemBullets = show;
        RequestRedraw();
    }

    void UltraCanvasSWOTDiagram::SetShowAxisCaptions(bool show) {
        if (showAxisCaptions == show) return;
        showAxisCaptions = show;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasSWOTDiagram::SetCenterText(const std::string& text) {
        centerText = text;
        RequestRedraw();
    }

// =============================================================================
// SELECTION
// =============================================================================

    void UltraCanvasSWOTDiagram::ClearSelection() {
        if (!selectedItem.IsValid()) return;
        selectedItem = SWOTItemRef();
        RequestRedraw();
    }

// =============================================================================
// THEME HELPERS
// =============================================================================

    Color UltraCanvasSWOTDiagram::PanelFillColor(size_t q) const {
        const Color& accent = quadrants[q].accentColor;
        if (design == SWOTDesign::CornerBadges) return accent;  // Vivid panels
        if (darkTheme) return MixColors(accent, Color(32, 35, 42, 255), 0.72f);
        return MixColors(accent, Color(255, 255, 255, 255), 0.88f);
    }

    Color UltraCanvasSWOTDiagram::BodyTextColor(size_t) const {
        if (design == SWOTDesign::CornerBadges) return Color(255, 255, 255, 255);
        return darkTheme ? Color(224, 227, 232, 255) : Color(55, 58, 64, 255);
    }

    Color UltraCanvasSWOTDiagram::DiagramTextColor() const {
        return darkTheme ? Color(235, 238, 242, 255) : Color(40, 44, 52, 255);
    }

// =============================================================================
// TEXT HELPERS
// =============================================================================

    std::vector<std::string> UltraCanvasSWOTDiagram::WrapText(
            IRenderContext* ctx, const std::string& text, double maxWidth) const {
        std::vector<std::string> lines;

        std::stringstream paragraphs(text);
        std::string paragraph;
        while (std::getline(paragraphs, paragraph, '\n')) {
            std::stringstream words(paragraph);
            std::string word, current;
            while (words >> word) {
                std::string candidate = current.empty() ? word : current + " " + word;
                if (!current.empty() &&
                    ctx->GetTextLineDimensions(candidate).width > maxWidth) {
                    lines.push_back(current);
                    current = word;
                } else {
                    current = candidate;
                }
            }
            lines.push_back(current);  // May be empty for blank paragraphs
        }
        if (lines.empty()) lines.push_back("");
        return lines;
    }

    void UltraCanvasSWOTDiagram::DrawMultilineCentered(
            IRenderContext* ctx, const std::string& text,
            const Point2Dd& center, float lineHeight) {
        std::vector<std::string> lines;
        std::stringstream ss(text);
        std::string line;
        while (std::getline(ss, line, '\n')) lines.push_back(line);
        if (lines.empty()) return;

        double y = center.y - lines.size() * lineHeight / 2.0;
        for (const auto& l : lines) {
            Size2Di s = ctx->GetTextLineDimensions(l);
            ctx->DrawText(l, Point2Dd(center.x - s.width / 2.0, y));
            y += lineHeight;
        }
    }

// =============================================================================
// ITEM LIST RENDERING (shared by all designs)
// =============================================================================

    double UltraCanvasSWOTDiagram::DrawItemList(
            IRenderContext* ctx, size_t q, const Rect2Dd& rect,
            const Color& textColor, const Color& bulletColor) {
        const auto& list = items[q];
        if (rect.width < 20 || rect.height < 10) return rect.y;

        ctx->SetFontSize(itemFontSize);
        ctx->SetFontWeight(FontWeight::Normal);

        const double lineHeight = itemFontSize * 1.35;
        const double itemGap = 5.0;
        const double bulletIndent = showItemBullets ? 12.0 : 0.0;
        const double textWidth = rect.width - bulletIndent;
        const bool onVividPanel = (design == SWOTDesign::CornerBadges);

        double y = rect.y;
        for (size_t i = 0; i < list.size(); ++i) {
            std::vector<std::string> lines = WrapText(ctx, list[i].text, textWidth);
            double itemHeight = lines.size() * lineHeight;

            if (y + itemHeight > rect.y + rect.height) {
                // Not enough room - indicate how many items are hidden
                size_t hidden = list.size() - i;
                if (y + lineHeight <= rect.y + rect.height + 2) {
                    ctx->SetTextPaint(Color(textColor.r, textColor.g, textColor.b, 150));
                    ctx->DrawText("+" + std::to_string(hidden) + " more...",
                                  Point2Dd(rect.x + bulletIndent, y));
                }
                break;
            }

            SWOTItemRef ref{static_cast<int>(q), static_cast<int>(i)};
            Rect2Dd itemRect(rect.x, y - 2, rect.width, itemHeight + 4);
            layout.itemRects.push_back({ref, itemRect});

            // Hover / selection highlight behind the text
            if (ref == selectedItem || ref == hoveredItem) {
                bool isSelected = (ref == selectedItem);
                Color highlight = onVividPanel
                        ? Color(255, 255, 255, isSelected ? 80 : 45)
                        : Color(quadrants[q].accentColor.r, quadrants[q].accentColor.g,
                                quadrants[q].accentColor.b, isSelected ? 60 : 30);
                ctx->SetFillPaint(highlight);
                ctx->FillRoundedRectangle(itemRect, 4.0);
                if (isSelected) {
                    ctx->SetStrokePaint(onVividPanel ? Color(255, 255, 255, 200)
                                                     : quadrants[q].accentColor);
                    ctx->SetStrokeWidth(1.0f);
                    ctx->DrawRoundedRectangle(itemRect, 4.0);
                }
            }

            if (showItemBullets) {
                const Color& itemBullet = list[i].bulletColor.a > 0
                        ? list[i].bulletColor : bulletColor;
                ctx->SetFillPaint(itemBullet);
                ctx->FillCircle(Point2Dd(rect.x + 4.0, y + lineHeight / 2.0), 2.5);
            }

            ctx->SetTextPaint(textColor);
            double lineY = y;
            for (const auto& l : lines) {
                ctx->DrawText(l, Point2Dd(rect.x + bulletIndent, lineY));
                lineY += lineHeight;
            }

            y += itemHeight + itemGap;
        }
        return y;
    }

// =============================================================================
// SHARED DECORATIONS
// =============================================================================

    void UltraCanvasSWOTDiagram::DrawBadgeCircle(
            IRenderContext* ctx, size_t q, const Point2Dd& center, double radius) {
        ctx->SetFillPaint(DarkenColor(quadrants[q].accentColor, 0.18f));
        ctx->FillCircle(center, radius);
        ctx->SetStrokePaint(darkTheme ? Color(32, 35, 42, 255) : Color(255, 255, 255, 255));
        ctx->SetStrokeWidth(2.5f);
        ctx->DrawCircle(center, radius);

        ctx->SetFontSize(static_cast<float>(radius * 1.05));
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(Color(255, 255, 255, 255));
        Size2Di s = ctx->GetTextLineDimensions(quadrants[q].badgeLetter);
        ctx->DrawText(quadrants[q].badgeLetter,
                      Point2Dd(center.x - s.width / 2.0, center.y - s.height / 2.0));
        ctx->SetFontWeight(FontWeight::Normal);
    }

    void UltraCanvasSWOTDiagram::DrawCenterCircle(
            IRenderContext* ctx, const Point2Dd& center, double radius) {
        ctx->SetFillPaint(darkTheme ? Color(45, 49, 58, 255) : Color(255, 255, 255, 255));
        ctx->FillCircle(center, radius);
        ctx->SetStrokePaint(darkTheme ? Color(72, 78, 90, 255) : Color(215, 218, 224, 255));
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawCircle(center, radius);

        ctx->SetFontSize(centerFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(DiagramTextColor());
        DrawMultilineCentered(ctx, centerText, center, centerFontSize * 1.25f);
        ctx->SetFontWeight(FontWeight::Normal);
    }

// =============================================================================
// LAYOUT
// =============================================================================

    void UltraCanvasSWOTDiagram::UpdateLayout(IRenderContext*) {
        double top = chartTitle.empty() ? 12.0 : 46.0;
        layout.diagramArea = Rect2Dd(
                14.0, top,
                std::max(40.0, GetWidth() - 28.0),
                std::max(40.0, GetHeight() - top - 14.0));
        layout.itemRects.clear();
        layout.valid = true;
    }

    SWOTItemRef UltraCanvasSWOTDiagram::FindItemAt(const Point2Di& pos) const {
        Point2Dd p(pos.x, pos.y);
        // Iterate backwards so the item drawn last wins on overlap
        for (auto it = layout.itemRects.rbegin(); it != layout.itemRects.rend(); ++it) {
            if (it->rect.Contains(p)) return it->ref;
        }
        return SWOTItemRef();
    }

// =============================================================================
// RENDERING - TOP LEVEL
// =============================================================================

    void UltraCanvasSWOTDiagram::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (!ctx) return;

        UpdateLayout(ctx);

        ctx->DrawFilledRectangle(GetLocalBounds(), backgroundColor);

        if (!chartTitle.empty()) {
            ctx->SetTextPaint(DiagramTextColor());
            ctx->SetFontSize(titleFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            Size2Di s = ctx->GetTextLineDimensions(chartTitle);
            ctx->DrawText(chartTitle, Point2Dd(
                    GetWidth() / 2.0 - s.width / 2.0, (46.0 - s.height) / 2.0));
            ctx->SetFontWeight(FontWeight::Normal);
        }

        RenderChart(ctx);
    }

    void UltraCanvasSWOTDiagram::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;
        if (!layout.valid) UpdateLayout(ctx);

        switch (design) {
            case SWOTDesign::Matrix:        RenderMatrix(ctx); break;
            case SWOTDesign::Cards:         RenderCards(ctx); break;
            case SWOTDesign::CornerBadges:  RenderCornerBadges(ctx); break;
            case SWOTDesign::CenterDiamond: RenderCenterDiamond(ctx); break;
            case SWOTDesign::Rows:          RenderRows(ctx); break;
            case SWOTDesign::Columns:       RenderColumns(ctx); break;
        }
    }

// =============================================================================
// DESIGN: CLASSIC 2x2 MATRIX
// =============================================================================

    void UltraCanvasSWOTDiagram::RenderMatrix(IRenderContext* ctx) {
        Rect2Dd area = layout.diagramArea;

        // Axis captions reserve a strip above and left of the grid
        if (showAxisCaptions) {
            area = Rect2Dd(area.x + 22, area.y + 20, area.width - 22, area.height - 20);
        }

        double cx = area.x + area.width / 2.0;
        double cy = area.y + area.height / 2.0;

        std::array<Rect2Dd, 4> cells = {
                Rect2Dd(area.x, area.y, area.width / 2.0, area.height / 2.0),  // S
                Rect2Dd(cx, area.y, area.width / 2.0, area.height / 2.0),      // W
                Rect2Dd(area.x, cy, area.width / 2.0, area.height / 2.0),      // O
                Rect2Dd(cx, cy, area.width / 2.0, area.height / 2.0)};         // T

        for (size_t q = 0; q < kSWOTQuadrantCount; ++q) {
            ctx->SetFillPaint(PanelFillColor(q));
            ctx->FillRectangle(cells[q]);
        }

        // Separator cross + outer border in the element background color
        ctx->SetStrokePaint(backgroundColor);
        ctx->SetStrokeWidth(3.0f);
        ctx->DrawLine({cx, area.y}, {cx, area.y + area.height});
        ctx->DrawLine({area.x, cy}, {area.x + area.width, cy});

        ctx->SetStrokePaint(darkTheme ? Color(60, 65, 76, 255) : Color(220, 223, 229, 255));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(area);

        if (showAxisCaptions) {
            ctx->SetFontSize(9.0f);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(darkTheme ? Color(150, 156, 166, 255) : Color(130, 135, 144, 255));

            auto captionAt = [&](const std::string& text, double centerX, double y) {
                Size2Di s = ctx->GetTextLineDimensions(text);
                ctx->DrawText(text, Point2Dd(centerX - s.width / 2.0, y));
            };
            captionAt("HELPFUL", area.x + area.width * 0.25, area.y - 16);
            captionAt("HARMFUL", area.x + area.width * 0.75, area.y - 16);

            auto rotatedCaption = [&](const std::string& text, double centerY) {
                Size2Di s = ctx->GetTextLineDimensions(text);
                ctx->PushState();
                ctx->Translate(area.x - 8, centerY);
                ctx->Rotate(-M_PI / 2.0);
                ctx->DrawText(text, Point2Dd(-s.width / 2.0, -s.height));
                ctx->PopState();
            };
            rotatedCaption("INTERNAL", area.y + area.height * 0.25);
            rotatedCaption("EXTERNAL", area.y + area.height * 0.75);
            ctx->SetFontWeight(FontWeight::Normal);
        }

        for (size_t q = 0; q < kSWOTQuadrantCount; ++q) {
            const double pad = 14.0;
            double headerX = cells[q].x + pad;

            if (showBadges) {
                DrawBadgeCircle(ctx, q, Point2Dd(headerX + 10, cells[q].y + pad + 9), 10.0);
                headerX += 28;
            }

            ctx->SetFontSize(quadrantTitleFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(darkTheme
                    ? MixColors(quadrants[q].accentColor, Color(255, 255, 255, 255), 0.35f)
                    : DarkenColor(quadrants[q].accentColor, 0.15f));
            ctx->DrawText(quadrants[q].title, Point2Dd(headerX, cells[q].y + pad + 2));
            ctx->SetFontWeight(FontWeight::Normal);

            Rect2Dd body(cells[q].x + pad, cells[q].y + pad + 28,
                         cells[q].width - pad * 2, cells[q].height - pad * 2 - 28);
            layout.panels[q] = cells[q];
            layout.bodies[q] = body;
            DrawItemList(ctx, q, body, BodyTextColor(q), quadrants[q].accentColor);
        }
    }

// =============================================================================
// DESIGN: SEPARATED CARDS WITH HEADER BARS
// =============================================================================

    void UltraCanvasSWOTDiagram::RenderCards(IRenderContext* ctx) {
        const Rect2Dd& area = layout.diagramArea;
        const double gutter = 14.0;
        const double cardW = (area.width - gutter) / 2.0;
        const double cardH = (area.height - gutter) / 2.0;
        const double radius = 10.0;
        const double headerH = 32.0;

        std::array<Rect2Dd, 4> cards = {
                Rect2Dd(area.x, area.y, cardW, cardH),
                Rect2Dd(area.x + cardW + gutter, area.y, cardW, cardH),
                Rect2Dd(area.x, area.y + cardH + gutter, cardW, cardH),
                Rect2Dd(area.x + cardW + gutter, area.y + cardH + gutter, cardW, cardH)};

        for (size_t q = 0; q < kSWOTQuadrantCount; ++q) {
            const Rect2Dd& card = cards[q];

            ctx->SetFillPaint(PanelFillColor(q));
            ctx->FillRoundedRectangle(card, radius);

            // Header bar: rounded on top, squared where it meets the body
            ctx->SetFillPaint(quadrants[q].accentColor);
            ctx->FillRoundedRectangle(Rect2Dd(card.x, card.y, card.width, headerH), radius);
            ctx->FillRectangle(Rect2Dd(card.x, card.y + radius, card.width, headerH - radius));

            ctx->SetStrokePaint(darkTheme ? Color(60, 65, 76, 255) : Color(222, 225, 231, 255));
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRoundedRectangle(card, radius);

            double titleX = card.x + 14;
            if (showBadges) {
                DrawBadgeCircle(ctx, q, Point2Dd(card.x + 20, card.y + headerH / 2.0), 11.0);
                titleX = card.x + 38;
            }

            ctx->SetFontSize(quadrantTitleFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(Color(255, 255, 255, 255));
            Size2Di ts = ctx->GetTextLineDimensions(quadrants[q].title);
            ctx->DrawText(quadrants[q].title,
                          Point2Dd(titleX, card.y + (headerH - ts.height) / 2.0));
            ctx->SetFontWeight(FontWeight::Normal);

            Rect2Dd body(card.x + 14, card.y + headerH + 10,
                         card.width - 28, card.height - headerH - 20);
            layout.panels[q] = card;
            layout.bodies[q] = body;
            DrawItemList(ctx, q, body, BodyTextColor(q), quadrants[q].accentColor);
        }
    }

// =============================================================================
// DESIGN: CORNER BADGES + CENTRAL CIRCLE (vivid reference style)
// =============================================================================

    void UltraCanvasSWOTDiagram::RenderCornerBadges(IRenderContext* ctx) {
        const Rect2Dd& area = layout.diagramArea;
        const double gutter = 12.0;
        const double badgeR = 19.0;
        // Panels are inset so the corner badges have room at the diagram corners
        const double inset = badgeR * 0.7;

        Rect2Dd inner(area.x + inset, area.y + inset,
                      area.width - inset * 2, area.height - inset * 2);
        const double panelW = (inner.width - gutter) / 2.0;
        const double panelH = (inner.height - gutter) / 2.0;
        const double radius = 12.0;

        std::array<Rect2Dd, 4> panels = {
                Rect2Dd(inner.x, inner.y, panelW, panelH),
                Rect2Dd(inner.x + panelW + gutter, inner.y, panelW, panelH),
                Rect2Dd(inner.x, inner.y + panelH + gutter, panelW, panelH),
                Rect2Dd(inner.x + panelW + gutter, inner.y + panelH + gutter, panelW, panelH)};

        for (size_t q = 0; q < kSWOTQuadrantCount; ++q) {
            const Rect2Dd& panel = panels[q];

            ctx->SetFillPaint(quadrants[q].accentColor);
            ctx->FillRoundedRectangle(panel, radius);

            ctx->SetFontSize(quadrantTitleFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(Color(255, 255, 255, 255));
            Size2Di ts = ctx->GetTextLineDimensions(quadrants[q].title);
            ctx->DrawText(quadrants[q].title,
                          Point2Dd(panel.x + panel.width / 2.0 - ts.width / 2.0,
                                   panel.y + 12));
            ctx->SetFontWeight(FontWeight::Normal);

            // The center circle overlaps the inner corners - keep the item area
            // clear of it by insetting the inner-facing side a little
            Rect2Dd body(panel.x + 16, panel.y + 38,
                         panel.width - 32, panel.height - 52);
            layout.panels[q] = panel;
            layout.bodies[q] = body;
            DrawItemList(ctx, q, body, BodyTextColor(q), Color(255, 255, 255, 230));
        }

        if (showCenterElement) {
            double centerR = std::min(area.width, area.height) * 0.155;
            DrawCenterCircle(ctx,
                             Point2Dd(inner.x + inner.width / 2.0, inner.y + inner.height / 2.0),
                             centerR);
        }

        if (showBadges) {
            DrawBadgeCircle(ctx, 0, Point2Dd(panels[0].x + 4, panels[0].y + 4), badgeR);
            DrawBadgeCircle(ctx, 1, Point2Dd(panels[1].x + panels[1].width - 4, panels[1].y + 4), badgeR);
            DrawBadgeCircle(ctx, 2, Point2Dd(panels[2].x + 4, panels[2].y + panels[2].height - 4), badgeR);
            DrawBadgeCircle(ctx, 3, Point2Dd(panels[3].x + panels[3].width - 4,
                                             panels[3].y + panels[3].height - 4), badgeR);
        }
    }

// =============================================================================
// DESIGN: CENTRAL DIAMOND WITH CORNER TEXT BLOCKS
// =============================================================================

    void UltraCanvasSWOTDiagram::RenderCenterDiamond(IRenderContext* ctx) {
        const Rect2Dd& area = layout.diagramArea;
        double cx = area.x + area.width / 2.0;
        double cy = area.y + area.height / 2.0;
        double d = std::min(area.width, area.height) * 0.21;

        if (showCenterElement) {
            Point2Dd n(cx, cy - d), e(cx + d, cy), s(cx, cy + d), w(cx - d, cy), c(cx, cy);

            // Triangles point at their quadrant: NW=S, NE=W, SW=O, SE=T
            struct Tri { Point2Dd a, b; size_t q; };
            std::array<Tri, 4> tris = {{{w, n, 0}, {n, e, 1}, {s, w, 2}, {e, s, 3}}};

            for (const auto& tri : tris) {
                std::vector<Point2Dd> pts = {tri.a, tri.b, c};
                ctx->SetFillPaint(quadrants[tri.q].accentColor);
                ctx->FillLinePath(pts);
                ctx->SetStrokePaint(backgroundColor);
                ctx->SetStrokeWidth(2.0f);
                ctx->DrawLinePath(pts, true);

                // Letter at the triangle centroid
                Point2Dd centroid((tri.a.x + tri.b.x + c.x) / 3.0,
                                  (tri.a.y + tri.b.y + c.y) / 3.0);
                ctx->SetFontSize(badgeFontSize);
                ctx->SetFontWeight(FontWeight::Bold);
                ctx->SetTextPaint(Color(255, 255, 255, 255));
                Size2Di ls = ctx->GetTextLineDimensions(quadrants[tri.q].badgeLetter);
                ctx->DrawText(quadrants[tri.q].badgeLetter,
                              Point2Dd(centroid.x - ls.width / 2.0, centroid.y - ls.height / 2.0));
                ctx->SetFontWeight(FontWeight::Normal);
            }
        }

        // Corner text blocks around the diamond
        double blockW = (area.width - 2 * d) / 2.0 - 24;
        double blockH = area.height / 2.0 - 10;

        std::array<Rect2Dd, 4> blocks = {
                Rect2Dd(area.x, area.y, blockW, blockH),
                Rect2Dd(area.x + area.width - blockW, area.y, blockW, blockH),
                Rect2Dd(area.x, cy + 10, blockW, blockH),
                Rect2Dd(area.x + area.width - blockW, cy + 10, blockW, blockH)};

        for (size_t q = 0; q < kSWOTQuadrantCount; ++q) {
            const Rect2Dd& block = blocks[q];
            double headerX = block.x;

            if (showBadges) {
                DrawBadgeCircle(ctx, q, Point2Dd(block.x + 10, block.y + 10), 10.0);
                headerX += 28;
            }

            ctx->SetFontSize(quadrantTitleFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(darkTheme
                    ? MixColors(quadrants[q].accentColor, Color(255, 255, 255, 255), 0.35f)
                    : DarkenColor(quadrants[q].accentColor, 0.15f));
            ctx->DrawText(quadrants[q].title, Point2Dd(headerX, block.y + 3));
            ctx->SetFontWeight(FontWeight::Normal);

            Rect2Dd body(block.x, block.y + 28, block.width, block.height - 28);
            layout.panels[q] = block;
            layout.bodies[q] = body;
            DrawItemList(ctx, q, body, BodyTextColor(q), quadrants[q].accentColor);
        }
    }

// =============================================================================
// DESIGN: STACKED HORIZONTAL ROWS
// =============================================================================

    void UltraCanvasSWOTDiagram::RenderRows(IRenderContext* ctx) {
        const Rect2Dd& area = layout.diagramArea;
        const double gap = 10.0;
        const double bandH = (area.height - 3 * gap) / 4.0;
        const double letterW = 46.0;
        const double radius = 8.0;

        for (size_t q = 0; q < kSWOTQuadrantCount; ++q) {
            Rect2Dd band(area.x, area.y + q * (bandH + gap), area.width, bandH);

            ctx->SetFillPaint(PanelFillColor(q));
            ctx->FillRoundedRectangle(band, radius);

            // Big letter block, rounded on the left and squared on the right
            ctx->SetFillPaint(quadrants[q].accentColor);
            ctx->FillRoundedRectangle(Rect2Dd(band.x, band.y, letterW, band.height), radius);
            ctx->FillRectangle(Rect2Dd(band.x + radius, band.y, letterW - radius, band.height));

            ctx->SetFontSize(std::min(26.0, bandH * 0.42));
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(Color(255, 255, 255, 255));
            Size2Di ls = ctx->GetTextLineDimensions(quadrants[q].badgeLetter);
            ctx->DrawText(quadrants[q].badgeLetter,
                          Point2Dd(band.x + letterW / 2.0 - ls.width / 2.0,
                                   band.y + band.height / 2.0 - ls.height / 2.0));

            ctx->SetFontSize(quadrantTitleFontSize);
            ctx->SetTextPaint(darkTheme
                    ? MixColors(quadrants[q].accentColor, Color(255, 255, 255, 255), 0.35f)
                    : DarkenColor(quadrants[q].accentColor, 0.15f));
            ctx->DrawText(quadrants[q].title, Point2Dd(band.x + letterW + 14, band.y + 8));
            ctx->SetFontWeight(FontWeight::Normal);

            Rect2Dd body(band.x + letterW + 14, band.y + 30,
                         band.width - letterW - 28, band.height - 38);
            layout.panels[q] = band;
            layout.bodies[q] = body;
            DrawItemList(ctx, q, body, BodyTextColor(q), quadrants[q].accentColor);
        }
    }

// =============================================================================
// DESIGN: FOUR VERTICAL COLUMNS
// =============================================================================

    void UltraCanvasSWOTDiagram::RenderColumns(IRenderContext* ctx) {
        const Rect2Dd& area = layout.diagramArea;
        const double gutter = 12.0;
        const double colW = (area.width - 3 * gutter) / 4.0;
        const double headerH = showBadges ? 44.0 : 32.0;
        const double radius = 8.0;

        for (size_t q = 0; q < kSWOTQuadrantCount; ++q) {
            Rect2Dd col(area.x + q * (colW + gutter), area.y, colW, area.height);

            // Body below the header chip
            Rect2Dd bodyPanel(col.x, col.y + headerH + 8, col.width, col.height - headerH - 8);
            ctx->SetFillPaint(PanelFillColor(q));
            ctx->FillRoundedRectangle(bodyPanel, radius);

            // Header chip
            ctx->SetFillPaint(quadrants[q].accentColor);
            ctx->FillRoundedRectangle(Rect2Dd(col.x, col.y, col.width, headerH), radius);

            double titleY = col.y + 9;
            if (showBadges) {
                DrawBadgeCircle(ctx, q, Point2Dd(col.x + col.width / 2.0, col.y + 15), 11.0);
                titleY = col.y + 27;
            }

            ctx->SetFontSize(std::min(quadrantTitleFontSize, static_cast<float>(colW * 0.115)));
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(Color(255, 255, 255, 255));
            Size2Di ts = ctx->GetTextLineDimensions(quadrants[q].title);
            ctx->DrawText(quadrants[q].title,
                          Point2Dd(col.x + col.width / 2.0 - ts.width / 2.0, titleY));
            ctx->SetFontWeight(FontWeight::Normal);

            Rect2Dd body(bodyPanel.x + 10, bodyPanel.y + 10,
                         bodyPanel.width - 20, bodyPanel.height - 20);
            layout.panels[q] = col;
            layout.bodies[q] = body;
            DrawItemList(ctx, q, body, BodyTextColor(q), quadrants[q].accentColor);
        }
    }

// =============================================================================
// EVENT HANDLING
// =============================================================================

    std::string UltraCanvasSWOTDiagram::BuildItemTooltip(const SWOTItemRef& ref) const {
        const auto& quadrant = quadrants[ref.quadrant];
        const auto& item = items[ref.quadrant][ref.item];
        return quadrant.title + "\n" + item.text;
    }

    bool UltraCanvasSWOTDiagram::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!layout.valid) return false;

        SWOTItemRef found = FindItemAt(mousePos);
        if (found != hoveredItem) {
            hoveredItem = found;
            if (hoveredItem.IsValid() && onItemHover) {
                onItemHover(static_cast<SWOTQuadrant>(hoveredItem.quadrant),
                            static_cast<size_t>(hoveredItem.item),
                            items[hoveredItem.quadrant][hoveredItem.item]);
            }
            RequestRedraw();
        }

        if (enableTooltips) {
            if (hoveredItem.IsValid()) {
                Point2Di tipPos(mousePos.x + 15, mousePos.y - 20);
                auto windowPos = MapFromLocal(tipPos, nullptr);
                UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        GetWindow(), BuildItemTooltip(hoveredItem), windowPos);
            } else {
                UltraCanvasTooltipManager::HideTooltip();
            }
        }

        return hoveredItem.IsValid();
    }

    bool UltraCanvasSWOTDiagram::HandleItemClick(const UCEvent& event) {
        if (!enableSelection || !layout.valid) return false;

        SWOTItemRef clicked = FindItemAt(Point2Di(event.pointer.x, event.pointer.y));

        if (clicked.IsValid()) {
            selectedItem = (clicked == selectedItem) ? SWOTItemRef() : clicked;
            if (selectedItem.IsValid() && onItemSelect) {
                onItemSelect(static_cast<SWOTQuadrant>(selectedItem.quadrant),
                             static_cast<size_t>(selectedItem.item),
                             items[selectedItem.quadrant][selectedItem.item]);
            }
            if (onSelectionChange) onSelectionChange();
            RequestRedraw();
            return true;
        }

        if (selectedItem.IsValid()) {
            ClearSelection();
            if (onSelectionChange) onSelectionChange();
            return true;
        }
        return false;
    }

    bool UltraCanvasSWOTDiagram::HandleItemDoubleClick(const UCEvent& event) {
        if (!layout.valid) return false;

        SWOTItemRef clicked = FindItemAt(Point2Di(event.pointer.x, event.pointer.y));
        if (clicked.IsValid() && onItemDoubleClick) {
            onItemDoubleClick(static_cast<SWOTQuadrant>(clicked.quadrant),
                              static_cast<size_t>(clicked.item),
                              items[clicked.quadrant][clicked.item]);
            return true;
        }
        return false;
    }

    bool UltraCanvasSWOTDiagram::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left && HandleItemClick(event)) {
                    return true;
                }
                return UltraCanvasChartElementBase::OnEvent(event);

            case UCEventType::MouseDoubleClick:
                return HandleItemDoubleClick(event);

            case UCEventType::MouseLeave:
                if (hoveredItem.IsValid()) {
                    hoveredItem = SWOTItemRef();
                    RequestRedraw();
                }
                UltraCanvasTooltipManager::HideTooltip();
                return false;

            default:
                return UltraCanvasChartElementBase::OnEvent(event);
        }
    }

// =============================================================================
// SAMPLE DATA
// =============================================================================

    namespace SWOTDiagramSamples {

        std::array<std::vector<SWOTItem>, kSWOTQuadrantCount> BusinessExample() {
            return {{
                {SWOTItem("Strong brand recognition"),
                 SWOTItem("Experienced leadership team"),
                 SWOTItem("Loyal customer base"),
                 SWOTItem("Proprietary technology")},
                {SWOTItem("High production costs"),
                 SWOTItem("Limited market reach"),
                 SWOTItem("Aging infrastructure"),
                 SWOTItem("Dependence on few suppliers")},
                {SWOTItem("Growth in emerging markets"),
                 SWOTItem("New technology trends"),
                 SWOTItem("Strategic partnerships"),
                 SWOTItem("Changing customer habits")},
                {SWOTItem("Intense competition"),
                 SWOTItem("Regulatory changes"),
                 SWOTItem("Economic uncertainty"),
                 SWOTItem("Supply chain disruption")}
            }};
        }

    } // namespace SWOTDiagramSamples

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasSWOTDiagram> CreateSWOTDiagramElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasSWOTDiagram>(id, x, y, width, height);
    }

    std::shared_ptr<UltraCanvasSWOTDiagram> CreateSWOTDiagramElement(
            const std::string& id, int x, int y, int width, int height,
            SWOTDesign design) {
        auto diagram = std::make_shared<UltraCanvasSWOTDiagram>(id, x, y, width, height);
        diagram->SetDesign(design);
        return diagram;
    }

} // namespace UltraCanvas
