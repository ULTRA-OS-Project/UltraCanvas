// Apps/UltraPaint/UltraPaintSettingsDialog.cpp
// UltraPaint's settings window. One page so far - Display > Mouse pointer
// magnification glass - laid out the way UltraFiler's settings are: a bold
// title, a caption, the controls, and a notes block at the foot that says
// what the setting does. The tree's root row is hidden, so Display is the
// top level; selecting it moves on to its first page.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#include "UltraPaintSettingsDialog.h"

#include "UltraCanvasButton.h"

#include <string>

namespace UltraCanvas {

namespace {
    constexpr float kTitleFontSize = 15.0f;
    constexpr float kTextFontSize  = 10.0f;
    constexpr float kNoteFontSize  = 9.0f;
    constexpr float kTreeFontSize  = 9.0f;
    constexpr int   kPagePadding   = 20;
    constexpr int   kTextWidth     = 400;
    constexpr int   kControlHeight = 22;

    const Color kTextColor      = Color(40, 40, 44, 255);
    const Color kNoteTextColor  = Color(96, 96, 104, 255);
    const Color kNoteBackground = Color(244, 245, 248, 255);
    const Color kNoteAccent     = Color(160, 176, 200, 255);

    // Page ids double as tree node ids.
    constexpr const char* kPageDisplay   = "display";
    constexpr const char* kPageMagnifier = "display/magnifier";

    std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id, const std::string& text,
                                                float fontSize = kTextFontSize, const Color& color = kTextColor) {
        auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 20);
        l->SetText(text);
        l->SetFontSize(fontSize);
        l->SetTextColor(color);
        l->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        l->size.width  = CSSLayout::Dimension::Auto();
        l->size.height = CSSLayout::Dimension::Auto();
        return l;
    }

    // A paragraph: word-wrapped at `width`, as tall as its lines.
    std::shared_ptr<UltraCanvasLabel> MakeText(const std::string& id, const std::string& text,
                                               int width = kTextWidth, float fontSize = kTextFontSize,
                                               const Color& color = kTextColor) {
        auto l = MakeLabel(id, text, fontSize, color);
        l->SetWrap(TextWrap::WrapWord);
        l->size.width = CSSLayout::Dimension::Px(width);
        return l;
    }

    std::shared_ptr<UltraCanvasRadio> MakeChoice(const std::string& id, const std::string& text, bool checked) {
        auto radio = UltraCanvasRadio::Create(id, -1, -1, text, checked);
        RadioVisualStyle style = radio->GetVisualStyle();
        style.base.fontSize       = kTextFontSize;
        style.base.textColor      = kTextColor;
        style.base.textHoverColor = kTextColor;
        radio->SetVisualStyle(style);
        radio->size.width  = CSSLayout::Dimension::Px(kTextWidth);
        radio->size.height = CSSLayout::Dimension::Px(kControlHeight);
        radio->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return radio;
    }
}

UltraPaintSettingsDialog::UltraPaintSettingsDialog(UltraPaintSettings* s) : UltraCanvasWindow(), settings(s) {
    config_.title = "UltraPaint - Settings";
    config_.width = 660; config_.height = 480;
    config_.minWidth = 560; config_.minHeight = 400;
    config_.resizable = true;
    config_.type = WindowType::Dialog;
    config_.deleteOnClose = true;
    layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    SetBackgroundColor(Color(249, 249, 251, 255));

    // ----- settings-page tree | page area -----
    auto content = std::make_shared<UltraCanvasContainer>("ups-content");
    content->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    content->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    tree = std::make_shared<UltraCanvasTreeView>("ups-tree");
    tree->SetFontSize(kTreeFontSize);
    tree->SetRowHeight(24);
    tree->SetSelectionMode(TreeSelectionMode::Single);
    tree->SetLineStyle(TreeLineStyle::NoLine);
    tree->SetBackgroundColor(Color(243, 243, 246, 255));
    // A section (Display) has no page of its own: selecting it moves on to
    // its first page.
    tree->SetShowFirstChildOnExpand(true);
    tree->SetAutoExpandSelectedNode(true);
    tree->size.width = CSSLayout::Dimension::Px(200);
    tree->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    TreeNodeData rootData;
    rootData.nodeId = "settings";
    rootData.text = "Settings";
    tree->SetRootNode(rootData);
    TreeNodeData display;
    display.nodeId = kPageDisplay;
    display.text = "Display";
    tree->AddNode("settings", display);
    TreeNodeData magnifier;
    magnifier.nodeId = kPageMagnifier;
    magnifier.text = "Mouse pointer magnification glass";
    tree->AddNode(kPageDisplay, magnifier);
    // After the nodes: hiding the root promotes its children to the top
    // level, which it can only do once they exist.
    tree->SetRootVisible(false);
    content->AddChild(tree);

    pageArea = std::make_shared<UltraCanvasContainer>("ups-pages");
    pageArea->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    pageArea->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    pageArea->SetBackgroundColor(Color(255, 255, 255, 255));
    content->AddChild(pageArea);

    auto page = BuildMagnifierPage();
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    pages[kPageMagnifier] = page;
    pageArea->AddChild(page);
    AddChild(content);

    // ----- bottom bar: [Restore defaults]            [Close] -----
    auto bottom = std::make_shared<UltraCanvasContainer>("ups-bottom");
    bottom->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center)
                  .SetFlexJustifyContent(CSSLayout::JustifyContent::SpaceBetween);
    bottom->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    bottom->SetPadding(8, 12, 8, 12);
    bottom->SetBorderTop(1, Color(225, 225, 230, 255));

    auto restore = std::make_shared<UltraCanvasButton>("ups-restore", 0, 0, 150, 28, "Restore defaults");
    restore->SetFontSize(kTextFontSize);
    restore->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    restore->onClick = [this]() {
        if (!settings) return;
        settings->RestoreMagnifierDefaults();
        UpdateMagnifierControls();
        Changed();
    };
    bottom->AddChild(restore);

    auto close = std::make_shared<UltraCanvasButton>("ups-close", 0, 0, 90, 28, "Close");
    close->SetFontSize(kTextFontSize);
    close->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    close->onClick = [this]() { Close(); };
    bottom->AddChild(close);
    AddChild(bottom);

    tree->onNodeSelected = [this](TreeNode* node) {
        if (!node) return;
        TreeNode* leaf = node;
        while (leaf && leaf->HasChildren()) leaf = leaf->FirstChild();
        if (leaf && leaf != node) { tree->SelectNode(leaf); return; }
        ShowPage(node->data.nodeId);
    };
    // The one page is up from the start, with its row selected.
    if (TreeNode* node = tree->FindNode(kPageMagnifier)) {
        for (TreeNode* parent = node->parent; parent; parent = parent->parent) tree->ExpandNode(parent);
        tree->SelectNode(node);
    }
    ShowPage(kPageMagnifier);

    // Escape closes, matching the framework's dialog convention.
    SetEventCallback([this](const UCEvent& event) {
        if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) { Close(); return true; }
        return false;
    });
}

std::shared_ptr<UltraCanvasContainer> UltraPaintSettingsDialog::BuildMagnifierPage() {
    const std::string id = "ups-page-magnifier";
    auto page = std::make_shared<UltraCanvasContainer>(id);
    page->layout.SetFlexColumn().SetFlexGap(0).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);

    auto title = MakeLabel(id + "-title", "Mouse pointer magnification glass", kTitleFontSize);
    title->SetFontWeight(FontWeight::Bold);
    page->AddChild(title);
    auto caption = MakeText(id + "-caption",
            "A small view over the canvas that shows the pixels around the pointer enlarged, "
            "with a crosshair on the pointer's own pixel, while the rectangle selection tool is active:");
    caption->SetMargin(6, 0, 0, 0);
    page->AddChild(caption);

    auto body = std::make_shared<UltraCanvasContainer>(id + "-body");
    body->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    body->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    body->SetMargin(14, 0, 0, 0);

    magnifierBox = std::make_shared<UltraCanvasCheckbox>(id + "-enabled", 0, 0,
            static_cast<float>(kTextWidth), static_cast<float>(kControlHeight), "Show the magnification glass");
    magnifierBox->SetFontSize(kTextFontSize);
    CheckboxVisualStyle boxStyle = magnifierBox->GetVisualStyle();
    boxStyle.base.textColor      = kTextColor;
    boxStyle.base.textHoverColor = kTextColor;
    magnifierBox->SetVisualStyle(boxStyle);
    magnifierBox->size.width  = CSSLayout::Dimension::Px(kTextWidth);
    magnifierBox->size.height = CSSLayout::Dimension::Px(kControlHeight);
    magnifierBox->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    magnifierBox->onStateChanged = [this](CheckedState, CheckedState now) {
        if (syncing || !settings) return;
        settings->magnifierEnabled = (now == CheckedState::Checked);
        Changed();
    };
    body->AddChild(magnifierBox);

    auto factorCaption = MakeText(id + "-factor-caption", "Magnification, on top of the view's own zoom:");
    factorCaption->SetMargin(10, 0, 0, 0);
    body->AddChild(factorCaption);
    const char* factorTexts[3] = { "2x - twice the size", "3x - three times the size", "4x - four times the size" };
    for (int i = 0; i < 3; ++i) {
        const int factor = UltraPaintSettings::kMinMagnifierFactor + i;
        factorRadios[i] = MakeChoice(id + "-factor-" + std::to_string(factor), factorTexts[i],
                                     settings && settings->magnifierFactor == factor);
        factorGroup.AddRadioButton(factorRadios[i]);
        body->AddChild(factorRadios[i]);
    }
    factorGroup.onSelectionChanged = [this](std::shared_ptr<UltraCanvasRadio> selected) {
        if (syncing || !selected || !settings) return;
        for (int i = 0; i < 3; ++i)
            if (selected == factorRadios[i]) settings->magnifierFactor = UltraPaintSettings::kMinMagnifierFactor + i;
        Changed();
    };

    auto sizeRow = std::make_shared<UltraCanvasContainer>(id + "-size-row");
    sizeRow->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    sizeRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    sizeRow->size.width  = CSSLayout::Dimension::Px(kTextWidth);
    sizeRow->size.height = CSSLayout::Dimension::Px(32);
    sizeRow->SetMargin(10, 0, 0, 0);
    auto sizeLabel = MakeLabel(id + "-size-label", "Window size:");
    sizeLabel->size.width  = CSSLayout::Dimension::Px(110);
    sizeLabel->size.height = CSSLayout::Dimension::Px(20);
    sizeRow->AddChild(sizeLabel);
    sizeSlider = CreateSlider(id + "-size", 0, 0, 200, 24);
    sizeSlider->SetRange(static_cast<float>(UltraPaintSettings::kMinMagnifierSize),
                         static_cast<float>(UltraPaintSettings::kMaxMagnifierSize));
    sizeSlider->SetStep(10.0f);
    sizeSlider->SetValue(static_cast<float>(settings ? settings->magnifierSize : UltraPaintSettings::kDefaultMagnifierSize));
    sizeSlider->size.width  = CSSLayout::Dimension::Px(200);
    sizeSlider->size.height = CSSLayout::Dimension::Px(24);
    sizeSlider->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    // Reported while the handle is dragged and when it is let go, so the
    // glass follows the slider instead of jumping at the end.
    auto sizeChanged = [this](float v) {
        if (syncing || !settings) return;
        settings->magnifierSize = static_cast<int>(v + 0.5f);
        if (sizeValue) { sizeValue->SetText(std::to_string(settings->magnifierSize) + " px"); sizeValue->RequestRedraw(); }
        Changed();
    };
    sizeSlider->onValueChanging = sizeChanged;
    sizeSlider->onValueChanged = sizeChanged;
    sizeRow->AddChild(sizeSlider);
    sizeValue = MakeLabel(id + "-size-value", "");
    sizeValue->size.width  = CSSLayout::Dimension::Px(60);
    sizeValue->size.height = CSSLayout::Dimension::Px(20);
    sizeRow->AddChild(sizeValue);
    body->AddChild(sizeRow);
    page->AddChild(body);

    // The spacer pushes the notes to the foot of the page.
    auto spacer = std::make_shared<UltraCanvasContainer>(id + "-spacer");
    spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    spacer->size.height = CSSLayout::Dimension::Px(24);
    page->AddChild(spacer);

    auto notes = std::make_shared<UltraCanvasContainer>(id + "-notes");
    notes->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    notes->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    notes->SetBackgroundColor(kNoteBackground);
    notes->SetBorderLeft(3, kNoteAccent);
    notes->SetPadding(10, 12, 10, 12);
    notes->AddChild(MakeText(id + "-note1",
            "The glass sits in a corner of the canvas and moves to another one when the pointer "
            "comes near it, so it never covers the spot being selected. Its crosshair leaves the "
            "centre pixel clear: that pixel is the one under the pointer.",
            kTextWidth - 10, kNoteFontSize, kNoteTextColor));
    notes->AddChild(MakeText(id + "-note2",
            "The pixel grid appears in the glass from 8x, so at 4x it shows the pixel edges "
            "whenever the view itself is at 200 % or more.",
            kTextWidth - 10, kNoteFontSize, kNoteTextColor));
    page->AddChild(notes);

    UpdateMagnifierControls();
    return page;
}

void UltraPaintSettingsDialog::ShowPage(const std::string& pageId) {
    for (auto& [id, page] : pages) page->SetVisible(id == pageId);
}

void UltraPaintSettingsDialog::UpdateMagnifierControls() {
    if (!settings) return;
    syncing = true;
    if (magnifierBox) magnifierBox->SetChecked(settings->magnifierEnabled);
    for (int i = 0; i < 3; ++i)
        if (factorRadios[i] && settings->magnifierFactor == UltraPaintSettings::kMinMagnifierFactor + i)
            factorGroup.SelectButton(factorRadios[i]);
    if (sizeSlider) sizeSlider->SetValue(static_cast<float>(settings->magnifierSize));
    if (sizeValue) { sizeValue->SetText(std::to_string(settings->magnifierSize) + " px"); sizeValue->RequestRedraw(); }
    syncing = false;
}

void UltraPaintSettingsDialog::Changed() {
    if (onChanged) onChanged();
    if (settings) settings->Save();
}

} // namespace UltraCanvas
