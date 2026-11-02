// core/UltraCanvasCard.cpp
// The card surface, its header, content and actions (UltraCanvasCard.h).
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "UltraCanvasCard.h"
#include <algorithm>

namespace UltraCanvas {

    CardStyle CardStyle::Default() {
        CardStyle s;
        s.titleFont.fontSize = 14.0;
        s.titleFont.fontWeight = FontWeight::Bold;
        s.subtitleFont.fontSize = 12.0;
        return s;
    }

    UltraCanvasCard::UltraCanvasCard(const std::string& identifier, float x, float y, float w, float h,
                                     const std::string& title, const std::string& subtitle)
            : UltraCanvasContainer(identifier, x, y, w, h), style(CardStyle::Default()) {
        Build();
        SetTitle(title);
        SetSubtitle(subtitle);
        if (h <= 0) size.height = CSSLayout::Dimension::Auto();
        layoutItem.SetFlexShrink(0);
    }

    void UltraCanvasCard::Build() {
        const std::string id = GetIdentifier();
        layout.SetFlexColumn()
              .SetFlexGap(style.gap)
              .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        SetPadding(style.padding);

        header = CreateContainer(id + ".header", 0, 0, 0, 0);
        header->layout.SetFlexRow().SetFlexGap(style.iconGap).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        header->size.height = CSSLayout::Dimension::Auto();
        header->layoutItem.SetFlexShrink(0);
        header->SetBackgroundColor(Colors::Transparent);

        titles = CreateContainer(id + ".titles", 0, 0, 0, 0);
        titles->layout.SetFlexColumn().SetFlexGap(2.0f).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        titles->size.height = CSSLayout::Dimension::Auto();
        titles->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        titles->SetBackgroundColor(Colors::Transparent);

        // No size of their own: each is as tall as its wrapped lines, so a
        // long title - another language, a larger font - makes the header
        // taller instead of being cut.
        titleLabel = std::make_shared<UltraCanvasLabel>(id + ".title", "");
        titleLabel->SetWrap(TextWrap::WrapWordChar);
        titleLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
        subtitleLabel = std::make_shared<UltraCanvasLabel>(id + ".subtitle", "");
        subtitleLabel->SetWrap(TextWrap::WrapWordChar);
        subtitleLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
        titles->AddChild(titleLabel);
        titles->AddChild(subtitleLabel);
        header->AddChild(titles);
        AddChild(header);

        content = CreateContainer(id + ".content", 0, 0, 0, 0);
        content->layout.SetFlexColumn().SetFlexGap(style.gap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        content->size.height = CSSLayout::Dimension::Auto();
        content->layoutItem.SetFlexShrink(0);
        content->SetBackgroundColor(Colors::Transparent);
        content->SetVisible(false);
        AddChild(content);

        actions = CreateContainer(id + ".actions", 0, 0, 0, 0);
        actions->layout.SetFlexRow()
                       .SetFlexGap(style.actionsGap)
                       .SetFlexJustifyContent(CSSLayout::JustifyContent::End)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        actions->size.height = CSSLayout::Dimension::Auto();
        actions->layoutItem.SetFlexShrink(0);
        actions->SetBackgroundColor(Colors::Transparent);
        actions->SetVisible(false);
        AddChild(actions);

        ApplyFonts();
        ApplySurface();
        UpdateHeaderVisibility();
    }

    void UltraCanvasCard::ApplyFonts() {
        titleLabel->SetFont(style.titleFont.fontFamily, static_cast<float>(style.titleFont.fontSize), style.titleFont.fontWeight);
        titleLabel->SetTextColor(style.titleColor);
        subtitleLabel->SetFont(style.subtitleFont.fontFamily, static_cast<float>(style.subtitleFont.fontSize), style.subtitleFont.fontWeight);
        subtitleLabel->SetTextColor(style.subtitleColor);
    }

    void UltraCanvasCard::ApplySurface() {
        Color bg = style.backgroundColor;
        if (selected) bg = style.selectedBackgroundColor;
        else if (IsClickable() && (IsHovered() || surfacePressed)) bg = style.hoverBackgroundColor;
        SetBackgroundColor(bg);
        const bool accent = highlighted || selected;
        SetBorders(accent ? style.highlightBorderWidth : style.borderWidth,
                   accent ? style.highlightBorderColor : style.borderColor, style.cornerRadius);
        SetMouseCursor(IsClickable() ? UCMouseCursor::Hand : UCMouseCursor::Default);
        RequestRedraw();
    }

    void UltraCanvasCard::UpdateHeaderVisibility() {
        const bool hasTitle = !titleLabel->GetText().empty();
        const bool hasSubtitle = !subtitleLabel->GetText().empty();
        titleLabel->SetVisible(hasTitle);
        subtitleLabel->SetVisible(hasSubtitle);
        header->SetVisible(hasTitle || hasSubtitle || trailing || iconImage);
        // The icon is drawn by the card at the header's left; the titles move over.
        header->SetPadding(0, 0, 0, iconImage ? style.iconSize + style.iconGap : 0.0f);
        InvalidateLayout();
        RequestRedraw();
    }

    // ----- header -----
    void UltraCanvasCard::SetTitle(const std::string& text) { titleLabel->SetText(text); UpdateHeaderVisibility(); }
    std::string UltraCanvasCard::GetTitle() const { return titleLabel->GetText(); }
    void UltraCanvasCard::SetSubtitle(const std::string& text) { subtitleLabel->SetText(text); UpdateHeaderVisibility(); }
    std::string UltraCanvasCard::GetSubtitle() const { return subtitleLabel->GetText(); }

    void UltraCanvasCard::SetIcon(const std::string& path) {
        iconPath = path;
        iconImage = path.empty() ? nullptr : UCImage::Get(path);
        UpdateHeaderVisibility();
    }

    void UltraCanvasCard::SetTrailing(std::shared_ptr<UltraCanvasUIElement> element) {
        if (trailing) header->RemoveChild(trailing);
        trailing = std::move(element);
        if (trailing) {
            trailing->layoutItem.SetFlexShrink(0);
            header->AddChild(trailing);
        }
        UpdateHeaderVisibility();
    }

    // ----- content -----
    void UltraCanvasCard::AddContent(std::shared_ptr<UltraCanvasUIElement> element) {
        if (!element) return;
        content->AddChild(std::move(element));
        content->SetVisible(true);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasCard::ClearContent() {
        content->ClearChildren();
        content->SetVisible(false);
        InvalidateLayout();
        RequestRedraw();
    }

    // ----- actions -----
    void UltraCanvasCard::AddAction(std::shared_ptr<UltraCanvasUIElement> element) {
        if (!element) return;
        element->layoutItem.SetFlexShrink(0);
        actions->AddChild(std::move(element));
        actions->SetVisible(true);
        InvalidateLayout();
        RequestRedraw();
    }

    std::shared_ptr<UltraCanvasButton> UltraCanvasCard::AddActionButton(const std::string& id, const std::string& text,
                                                                        std::function<void()> onClickFn, float minWidth) {
        auto button = CreateButton(id, 0, 0, minWidth, 28, text);
        button->onClick = std::move(onClickFn);
        AddAction(button);
        return button;
    }

    void UltraCanvasCard::ClearActions() {
        actions->ClearChildren();
        actions->SetVisible(false);
        InvalidateLayout();
        RequestRedraw();
    }

    // ----- states -----
    void UltraCanvasCard::SetClickable(bool c) { clickable = c; ApplySurface(); }
    void UltraCanvasCard::SetHighlighted(bool on) { if (highlighted != on) { highlighted = on; ApplySurface(); } }
    void UltraCanvasCard::SetSelected(bool on) { if (selected != on) { selected = on; ApplySurface(); } }

    void UltraCanvasCard::SetStyle(const CardStyle& newStyle) {
        style = newStyle;
        layout.SetFlexGap(style.gap);
        SetPadding(style.padding);
        header->layout.SetFlexGap(style.iconGap);
        content->layout.SetFlexGap(style.gap);
        actions->layout.SetFlexGap(style.actionsGap);
        ApplyFonts();
        ApplySurface();
        UpdateHeaderVisibility();
    }

    // ----- framework -----
    void UltraCanvasCard::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        UltraCanvasContainer::Render(ctx, dirtyRect);
        if (iconImage && header->IsVisible()) {
            const Rect2Df hb = header->GetBounds();
            const float cy = hb.y + hb.height / 2.0f;
            ctx->DrawImage(*iconImage,
                           Rect2Dd(hb.x, cy - style.iconSize / 2.0f, style.iconSize, style.iconSize),
                           ImageFitMode::Contain);
        }
    }

    bool UltraCanvasCard::OnEvent(const UCEvent& event) {
        // A press a child took never reaches here; what does is the surface.
        switch (event.type) {
            case UCEventType::MouseEnter:
                SetHovered(true);
                if (IsClickable()) ApplySurface();
                break;
            case UCEventType::MouseLeave:
                SetHovered(false);
                surfacePressed = false;
                if (IsClickable()) ApplySurface();
                break;
            case UCEventType::MouseDown:
                if (IsClickable() && event.button == UCMouseButton::Left) {
                    surfacePressed = true;
                    ApplySurface();
                    return true;
                }
                break;
            case UCEventType::MouseUp:
                if (surfacePressed && event.button == UCMouseButton::Left) {
                    surfacePressed = false;
                    ApplySurface();
                    if (GetLocalBounds().Contains(Point2Df(event.pointer.x, event.pointer.y)) && onClick) onClick();
                    return true;
                }
                break;
            case UCEventType::MouseDoubleClick:
                if (onDoubleClick && event.button == UCMouseButton::Left) { onDoubleClick(); return true; }
                break;
            default:
                break;
        }
        return UltraCanvasContainer::OnEvent(event);
    }

} // namespace UltraCanvas
