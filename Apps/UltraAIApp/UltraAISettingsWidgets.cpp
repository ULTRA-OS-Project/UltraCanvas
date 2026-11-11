// Apps/UltraAIApp/UltraAISettingsWidgets.cpp
// The settings pages' shared parts (UltraAISettingsWidgets.h).
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraAI Module

#include "UltraAISettingsWidgets.h"

#include <utility>

namespace UltraAIApp {
namespace SettingsWidgets {

using namespace UltraCanvas;

// ===== TEXT =====

std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id,
                                            const std::string& text,
                                            float fontSize,
                                            const Color& color) {
    auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 20);
    l->SetText(text);
    l->SetFontSize(fontSize);
    l->SetTextColor(color);
    l->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    l->size.width  = CSSLayout::Dimension::Auto();
    l->size.height = CSSLayout::Dimension::Auto();
    return l;
}

std::shared_ptr<UltraCanvasLabel> MakeText(const std::string& id,
                                           const std::string& text,
                                           int width,
                                           float fontSize,
                                           const Color& color) {
    auto l = MakeLabel(id, text, fontSize, color);
    l->SetWrap(TextWrap::WrapWord);
    l->size.width = CSSLayout::Dimension::Px(width);
    return l;
}

// ===== PAGE SKELETON =====

PageParts MakePage(const std::string& id, const std::string& title,
                   const std::string& caption) {
    PageParts parts;
    parts.page = std::make_shared<UltraCanvasContainer>(id);
    parts.page->layout.SetFlexColumn().SetFlexGap(0)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    parts.page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);

    auto titleLabel = MakeLabel(id + "-title", title, kTitleFontSize);
    titleLabel->SetFontWeight(FontWeight::Bold);
    parts.page->AddChild(titleLabel);

    auto captionLabel = MakeText(id + "-caption", caption);
    captionLabel->SetMargin(6, 0, 0, 0);
    parts.page->AddChild(captionLabel);

    parts.body = std::make_shared<UltraCanvasContainer>(id + "-body");
    parts.body->layout.SetFlexColumn().SetFlexGap(8)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Start);
    parts.body->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    parts.body->SetMargin(14, 0, 0, 0);
    parts.page->AddChild(parts.body);

    // The spacer pushes the notes to the foot of the page.
    auto spacer = std::make_shared<UltraCanvasContainer>(id + "-spacer");
    spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    spacer->size.height = CSSLayout::Dimension::Px(16);
    parts.page->AddChild(spacer);

    parts.notes = std::make_shared<UltraCanvasContainer>(id + "-notes");
    parts.notes->layout.SetFlexColumn().SetFlexGap(6)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Start);
    parts.notes->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    parts.notes->SetBackgroundColor(kNoteBackground);
    parts.notes->SetBorderLeft(3, kNoteAccent);
    parts.notes->SetPadding(10, 12, 10, 12);
    parts.page->AddChild(parts.notes);
    return parts;
}

void AddNote(PageParts& parts, const std::string& id, const std::string& text) {
    parts.notes->AddChild(MakeText(id, text, kNoteWidth, kNoteFontSize,
                                   kNoteTextColor));
}

// ===== CONTROLS =====

std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id,
                                              const std::string& label,
                                              int width,
                                              std::function<void()> onClick) {
    auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, 28, label);
    b->SetFontSize(kTextFontSize);
    b->SetCornerRadius(4.0f);
    b->SetColors(Color(255, 255, 255, 255), Color(233, 238, 244, 255));
    b->SetTextColors(kTextColor);
    b->SetBorder(1.0f, Color(0, 0, 0, 60));
    if (onClick) b->SetOnClick(std::move(onClick));
    b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return b;
}

std::shared_ptr<UltraCanvasCheckbox> MakeCheckbox(
        const std::string& id, const std::string& text, int width,
        bool checked, std::function<void(bool)> onChange) {
    auto box = std::make_shared<UltraCanvasCheckbox>(id, 0, 0,
            static_cast<float>(width), static_cast<float>(kControlHeight),
            text);
    box->SetFontSize(kTextFontSize);
    CheckboxVisualStyle style = box->GetVisualStyle();
    style.base.textColor      = kTextColor;
    style.base.textHoverColor = kTextColor;
    box->SetVisualStyle(style);
    box->SetChecked(checked);
    box->size.width  = CSSLayout::Dimension::Px(width);
    box->size.height = CSSLayout::Dimension::Px(kControlHeight);
    box->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    box->onStateChanged = [onChange](CheckedState, CheckedState now) {
        if (onChange) onChange(now == CheckedState::Checked);
    };
    return box;
}

// A "label above input" field group for the endpoint editor; the input is
// handed back through `out`.
std::shared_ptr<UltraCanvasContainer> MakeField(
        const std::string& id, const std::string& label,
        const std::string& placeholder,
        std::shared_ptr<UltraCanvasTextInput>& out) {
    auto col = std::make_shared<UltraCanvasContainer>(id + "-grp");
    col->layout.SetFlexColumn().SetFlexGap(4)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    col->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    col->size.height = CSSLayout::Dimension::Px(50);

    auto lbl = MakeLabel(id + "-lbl", label);
    lbl->size.height = CSSLayout::Dimension::Px(18);
    col->AddChild(lbl);

    out = std::make_shared<UltraCanvasTextInput>(id, 0, 0, 0, 28);
    out->SetPlaceholder(placeholder);
    out->size.height = CSSLayout::Dimension::Px(28);
    out->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    col->AddChild(out);
    return col;
}


} // namespace SettingsWidgets
} // namespace UltraAIApp
