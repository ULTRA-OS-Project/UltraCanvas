// core/UltraCanvasCollapsiblePanel.cpp
// The fold, the header strip, the chevron and the resize grip
// (UltraCanvasCollapsiblePanel.h).
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "UltraCanvasCollapsiblePanel.h"
#include <algorithm>

namespace UltraCanvas {

    CollapsiblePanelStyle CollapsiblePanelStyle::Default() {
        CollapsiblePanelStyle s;
        s.titleFont.fontSize = 13.0;
        s.titleFont.fontWeight = FontWeight::Bold;
        s.detailFont.fontSize = 12.0;
        return s;
    }

    UltraCanvasCollapsiblePanel::UltraCanvasCollapsiblePanel(const std::string& identifier, float x, float y,
                                                             float w, float h, const std::string& title)
            : UltraCanvasContainer(identifier, x, y, w, h), style(CollapsiblePanelStyle::Default()) {
        if (h > 0) expandedHeight = h;
        Build();
        SetTitle(title);
        ApplyHeight();
    }

    void UltraCanvasCollapsiblePanel::Build() {
        const std::string id = GetIdentifier();
        layout.SetFlexColumn().SetFlexGap(0.0f).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        SetPadding(0);
        layoutItem.SetFlexShrink(0);
        SetBackgroundColor(style.backgroundColor);
        SetBorders(style.borderWidth, style.borderColor, style.cornerRadius);

        header = CreateContainer(id + ".header", 0, 0, 0, style.headerHeight);
        header->layout.SetFlexRow().SetFlexGap(style.gap).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        header->layoutItem.SetFlexShrink(0);
        // Room for the chevron, drawn by the panel at the left.
        header->SetPadding(0, style.paddingH, 0, style.paddingH + style.chevronSize + style.gap);
        header->SetBackgroundColor(Colors::Transparent);

        titleLabel = CreateLabel(id + ".title", 0, 0, 0, style.headerHeight, "");
        titleLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
        titleLabel->layoutItem.SetFlexShrink(0);
        header->AddChild(titleLabel);

        detailLabel = CreateLabel(id + ".detail", 0, 0, 0, style.headerHeight, "");
        detailLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
        detailLabel->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        detailLabel->SetVisible(false);
        header->AddChild(detailLabel);

        headerActions = CreateContainer(id + ".actions", 0, 0, 0, style.headerHeight);
        headerActions->layout.SetFlexRow().SetFlexGap(style.gap).SetFlexAlignItems(CSSLayout::AlignItems::Center)
                             .SetFlexJustifyContent(CSSLayout::JustifyContent::End);
        headerActions->layoutItem.SetFlexShrink(0);
        headerActions->SetBackgroundColor(Colors::Transparent);
        headerActions->SetVisible(false);
        header->AddChild(headerActions);
        AddChild(header);

        body = CreateContainer(id + ".body", 0, 0, 0, 0);
        body->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        body->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        body->size.height = CSSLayout::Dimension::Auto();
        body->SetBackgroundColor(Colors::Transparent);
        AddChild(body);

        ApplyFonts();
    }

    void UltraCanvasCollapsiblePanel::ApplyFonts() {
        titleLabel->SetFont(style.titleFont.fontFamily, static_cast<float>(style.titleFont.fontSize), style.titleFont.fontWeight);
        titleLabel->SetTextColor(style.titleColor);
        detailLabel->SetFont(style.detailFont.fontFamily, static_cast<float>(style.detailFont.fontSize), style.detailFont.fontWeight);
        detailLabel->SetTextColor(style.detailColor);
    }

    void UltraCanvasCollapsiblePanel::ApplyHeight() {
        body->SetVisible(expanded);
        if (!expanded) {
            size.height = CSSLayout::Dimension::Px(style.headerHeight + 2.0f * style.borderWidth);
        } else if (expandedHeight > 0.0f) {
            size.height = CSSLayout::Dimension::Px(expandedHeight);
        } else {
            size.height = CSSLayout::Dimension::Auto();
        }
        InvalidateLayout();
        RequestRedraw();
    }

    // ----- header -----
    void UltraCanvasCollapsiblePanel::SetTitle(const std::string& text) { titleLabel->SetText(text); InvalidateLayout(); RequestRedraw(); }
    std::string UltraCanvasCollapsiblePanel::GetTitle() const { return titleLabel->GetText(); }

    void UltraCanvasCollapsiblePanel::SetDetail(const std::string& text) {
        detailLabel->SetText(text);
        detailLabel->SetVisible(!text.empty());
        InvalidateLayout();
        RequestRedraw();
    }
    std::string UltraCanvasCollapsiblePanel::GetDetail() const { return detailLabel->GetText(); }

    void UltraCanvasCollapsiblePanel::AddHeaderAction(std::shared_ptr<UltraCanvasUIElement> element) {
        if (!element) return;
        element->layoutItem.SetFlexShrink(0);
        headerActions->AddChild(std::move(element));
        headerActions->SetVisible(true);
        InvalidateLayout();
        RequestRedraw();
    }

    std::shared_ptr<UltraCanvasButton> UltraCanvasCollapsiblePanel::AddHeaderButton(const std::string& id, const std::string& text,
                                                                                    std::function<void()> onClickFn, float minWidth) {
        auto button = CreateButton(id, 0, 0, minWidth, std::max(20.0f, style.headerHeight - 8.0f), text);
        button->onClick = std::move(onClickFn);
        AddHeaderAction(button);
        return button;
    }

    void UltraCanvasCollapsiblePanel::ClearHeaderActions() {
        headerActions->ClearChildren();
        headerActions->SetVisible(false);
        InvalidateLayout();
        RequestRedraw();
    }

    // ----- fold -----
    void UltraCanvasCollapsiblePanel::SetExpanded(bool value) {
        if (expanded == value) return;
        expanded = value;
        ApplyHeight();
        if (onExpandedChanged) onExpandedChanged(expanded);
    }

    void UltraCanvasCollapsiblePanel::SetExpandedHeight(float height) {
        expandedHeight = height <= 0.0f ? 0.0f : std::max(height, style.minExpandedHeight);
        ApplyHeight();
    }

    void UltraCanvasCollapsiblePanel::SetCollapsible(bool value) {
        collapsible = value;
        if (!collapsible && !expanded) SetExpanded(true);
        RequestRedraw();
    }

    void UltraCanvasCollapsiblePanel::SetStyle(const CollapsiblePanelStyle& newStyle) {
        style = newStyle;
        SetBackgroundColor(style.backgroundColor);
        SetBorders(style.borderWidth, style.borderColor, style.cornerRadius);
        header->SetHeight(style.headerHeight);
        header->SetPadding(0, style.paddingH, 0, style.paddingH + style.chevronSize + style.gap);
        header->layout.SetFlexGap(style.gap);
        titleLabel->SetHeight(style.headerHeight);
        detailLabel->SetHeight(style.headerHeight);
        headerActions->SetHeight(style.headerHeight);
        ApplyFonts();
        ApplyHeight();
    }

    Rect2Df UltraCanvasCollapsiblePanel::HeaderRect() const {
        return header ? header->GetBounds() : Rect2Df(0, 0, GetWidth(), style.headerHeight);
    }

    bool UltraCanvasCollapsiblePanel::InGrip(const Point2Df& p) const {
        return resizable && expanded && p.y >= 0.0f && p.y <= style.gripSize && p.x >= 0.0f && p.x <= GetWidth();
    }

    // ----- framework -----
    void UltraCanvasCollapsiblePanel::DrawChevron(IRenderContext* ctx, const Rect2Df& rect) const {
        ctx->SetFillPaint(style.chevronColor);
        ctx->ClearPath();
        if (!expanded) {
            // Right-pointing
            ctx->MoveTo(rect.x + rect.width * 0.30f, rect.y + rect.height * 0.20f);
            ctx->LineTo(rect.x + rect.width * 0.78f, rect.y + rect.height * 0.50f);
            ctx->LineTo(rect.x + rect.width * 0.30f, rect.y + rect.height * 0.80f);
        } else {
            // Down-pointing
            ctx->MoveTo(rect.x + rect.width * 0.20f, rect.y + rect.height * 0.32f);
            ctx->LineTo(rect.x + rect.width * 0.80f, rect.y + rect.height * 0.32f);
            ctx->LineTo(rect.x + rect.width * 0.50f, rect.y + rect.height * 0.74f);
        }
        ctx->ClosePath();
        ctx->Fill();
        ctx->ClearPath();
    }

    void UltraCanvasCollapsiblePanel::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        // The surface and the children first (the base draws background and border).
        const Rect2Df hr = HeaderRect();
        ctx->PushState();
        // The header strip's own fill, under the header's children.
        const Color hb = (headerHovered && collapsible) ? style.headerHoverColor : style.headerBackgroundColor;
        if (hb.a > 0) {
            ctx->SetFillPaint(hb);
            ctx->FillRectangle(Rect2Dd(hr.x, hr.y, hr.width, hr.height));
        }
        ctx->PopState();
        UltraCanvasContainer::Render(ctx, dirtyRect);

        ctx->PushState();
        if (collapsible) {
            const float cs = style.chevronSize;
            DrawChevron(ctx, Rect2Df(hr.x + style.paddingH, hr.y + (hr.height - cs) / 2.0f, cs, cs));
        }
        if (expanded) {
            ctx->SetStrokePaint(style.separatorColor);
            ctx->SetStrokeWidth(1.0f);
            const float y = hr.y + hr.height - 0.5f;
            ctx->DrawLine(Point2Dd(hr.x, y), Point2Dd(hr.x + hr.width, y));
        }
        if (dragging || gripHovered) {
            ctx->SetFillPaint(style.gripColor);
            ctx->FillRectangle(Rect2Dd(0, 0, GetWidth(), 2.0));
        }
        ctx->PopState();
    }

    bool UltraCanvasCollapsiblePanel::OnEvent(const UCEvent& event) {
        const Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
        switch (event.type) {
            case UCEventType::MouseMove: {
                if (dragging) {
                    // The panel is docked at the bottom: dragging the top edge up
                    // makes it taller.
                    const float delta = dragStartY - p.y;
                    const float wanted = std::max(style.minExpandedHeight, dragStartHeight + delta);
                    if (std::abs(wanted - expandedHeight) >= 0.5f) {
                        expandedHeight = wanted;
                        ApplyHeight();
                    }
                    return true;
                }
                const bool overHeader = HeaderRect().Contains(p);
                const bool overGrip = InGrip(p);
                if (overHeader != headerHovered || overGrip != gripHovered) {
                    headerHovered = overHeader;
                    gripHovered = overGrip;
                    SetMouseCursor(gripHovered ? UCMouseCursor::SizeNS
                                                : (collapsible && headerHovered ? UCMouseCursor::Hand : UCMouseCursor::Default));
                    RequestRedraw();
                }
                break;
            }
            case UCEventType::MouseLeave:
                if (headerHovered || gripHovered) {
                    headerHovered = gripHovered = false;
                    SetMouseCursor(UCMouseCursor::Default);
                    RequestRedraw();
                }
                break;
            case UCEventType::MouseDown:
            case UCEventType::MouseDoubleClick:
                if (event.button != UCMouseButton::Left) break;
                if (InGrip(p) && event.type == UCEventType::MouseDown) {
                    dragging = true;
                    dragStartY = p.y;
                    dragStartHeight = expandedHeight > 0.0f ? expandedHeight : GetHeight();
                    RequestRedraw();
                    return true;
                }
                // A press on a header button never arrives here; one on the
                // strip itself folds the panel.
                if (collapsible && HeaderRect().Contains(p)) {
                    Toggle();
                    return true;
                }
                break;
            case UCEventType::MouseUp:
                if (dragging) {
                    dragging = false;
                    RequestRedraw();
                    if (onExpandedHeightChanged) onExpandedHeightChanged(expandedHeight);
                    return true;
                }
                break;
            default:
                break;
        }
        return UltraCanvasContainer::OnEvent(event);
    }

} // namespace UltraCanvas
