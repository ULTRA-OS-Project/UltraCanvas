// Apps/UltraPaint/UltraPaintDialogs.cpp
// New Image, Scale Image / Canvas Size, Text, Layer Properties and
// Colour to Alpha windows.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraPaintDialogs.h"
#include "UltraPaintTools.h"   // PaintOptionWidgets
#include "UltraCanvasRasterLayer.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

namespace UltraPaintDialogParts {

std::shared_ptr<UltraCanvasContainer> LabelledRow(const std::string& id, const std::string& label,
                                                  std::shared_ptr<UltraCanvasUIElement> widget, float labelWidth) {
    auto row = std::make_shared<UltraCanvasContainer>(id, 0, 0, 0, 28);
    row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto lbl = CreateLabel(id + "-label", 0, 0, labelWidth, 24, label);
    lbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->AddChild(lbl);
    widget->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    row->AddChild(widget);
    return row;
}

std::shared_ptr<UltraCanvasContainer> ButtonRow(const std::string& id, UltraCanvasWindow* win,
                                                const std::function<void()>& onOk, const std::string& okLabel) {
    auto row = std::make_shared<UltraCanvasContainer>(id, 0, 0, 0, 34);
    row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center)
                .SetFlexJustifyContent(CSSLayout::JustifyContent::FlexEnd);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto cancel = std::make_shared<UltraCanvasButton>(id + "-cancel", 0, 0, 84, 28, "Cancel");
    cancel->onClick = [win]() { win->Close(); };
    cancel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->AddChild(cancel);
    auto ok = std::make_shared<UltraCanvasButton>(id + "-ok", 0, 0, 84, 28, okLabel);
    ok->onClick = [win, onOk]() { if (onOk) onOk(); win->Close(); };
    ok->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->AddChild(ok);
    return row;
}

} // namespace UltraPaintDialogParts

using namespace UltraPaintDialogParts;

namespace {
    struct Preset { const char* name; int w, h; };
    const Preset kPresets[] = {
        { "Custom", 0, 0 }, { "640 x 480", 640, 480 }, { "800 x 600", 800, 600 }, { "1024 x 768", 1024, 768 },
        { "1280 x 720 (HD)", 1280, 720 }, { "1920 x 1080 (Full HD)", 1920, 1080 }, { "2560 x 1440", 2560, 1440 },
        { "3840 x 2160 (4K)", 3840, 2160 }, { "A4 300 dpi", 2480, 3508 }, { "Instagram square", 1080, 1080 },
        { "Icon 512", 512, 512 }, { "Icon 256", 256, 256 }
    };
}

// ===========================================================================
// NEW IMAGE
// ===========================================================================

UltraPaintNewImageDialog::UltraPaintNewImageDialog(const UltraPaintNewImageResult& initial) : UltraCanvasWindow() {
    config_.title = "New Image";
    config_.width = 360; config_.height = 230;
    config_.minWidth = 320; config_.minHeight = 200;
    config_.resizable = false;
    config_.deleteOnClose = true;
    SetPadding(12);
    layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    presetDrop = CreateDropdown("upn-preset", 0, 0, 200, 24);
    for (const auto& p : kPresets) presetDrop->AddItem(p.name);
    presetDrop->SetSelectedIndex(0, false);
    AddChild(LabelledRow("upn-preset-row", "Preset:", presetDrop));

    widthSpin = CreateIntSpinner("upn-w", 0, 0, 120, 24, 1, 20000, initial.width, 1);
    widthSpin->SetSuffix(" px");
    AddChild(LabelledRow("upn-w-row", "Width:", widthSpin));
    heightSpin = CreateIntSpinner("upn-h", 0, 0, 120, 24, 1, 20000, initial.height, 1);
    heightSpin->SetSuffix(" px");
    AddChild(LabelledRow("upn-h-row", "Height:", heightSpin));

    backgroundDrop = CreateDropdown("upn-bg", 0, 0, 200, 24);
    for (const char* n : { "White", "Black", "Transparent", "Foreground colour", "Background colour" }) backgroundDrop->AddItem(n);
    backgroundDrop->SetSelectedIndex(std::clamp(initial.background, 0, 4), false);
    AddChild(LabelledRow("upn-bg-row", "Background:", backgroundDrop));

    presetDrop->onSelectionChanged = [this](int i, const DropdownItem&) {
        if (i <= 0 || i >= static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]))) return;
        widthSpin->SetValue(kPresets[i].w);
        heightSpin->SetValue(kPresets[i].h);
    };

    AddChild(ButtonRow("upn-buttons", this, [this]() {
        UltraPaintNewImageResult r;
        r.width = static_cast<int>(widthSpin->GetValue());
        r.height = static_cast<int>(heightSpin->GetValue());
        r.background = backgroundDrop->GetSelectedIndex();
        if (onAccept) onAccept(r);
    }, "Create"));
}

// ===========================================================================
// SCALE IMAGE / CANVAS SIZE
// ===========================================================================

UltraPaintResizeDialog::UltraPaintResizeDialog(int w, int h, bool canvasMode)
    : UltraCanvasWindow(), origW(std::max(1, w)), origH(std::max(1, h)), canvas(canvasMode) {
    config_.title = canvas ? "Canvas Size" : "Scale Image";
    config_.width = 360; config_.height = canvas ? 240 : 240;
    config_.minWidth = 320; config_.minHeight = 200;
    config_.resizable = false;
    config_.deleteOnClose = true;
    SetPadding(12);
    layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto info = CreateLabel("upr-info", 0, 0, 0, 22, "Current size: " + std::to_string(origW) + " x " + std::to_string(origH) + " px");
    info->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    AddChild(info);

    widthSpin = CreateIntSpinner("upr-w", 0, 0, 120, 24, 1, 20000, origW, 1);
    widthSpin->SetSuffix(" px");
    widthSpin->onValueChanged = [this](double) { SyncFromWidth(); };
    AddChild(LabelledRow("upr-w-row", "Width:", widthSpin));
    heightSpin = CreateIntSpinner("upr-h", 0, 0, 120, 24, 1, 20000, origH, 1);
    heightSpin->SetSuffix(" px");
    heightSpin->onValueChanged = [this](double) { SyncFromHeight(); };
    AddChild(LabelledRow("upr-h-row", "Height:", heightSpin));

    if (!canvas) {
        percentSpin = CreateDecimalSpinner("upr-pct", 0, 0, 120, 24, 1.0, 1000.0, 100.0, 1.0, 1);
        percentSpin->SetSuffix(" %");
        percentSpin->onValueChanged = [this](double v) {
            if (syncing) return;
            syncing = true;
            widthSpin->SetValue(std::max(1.0, std::round(origW * v / 100.0)));
            heightSpin->SetValue(std::max(1.0, std::round(origH * v / 100.0)));
            syncing = false;
        };
        AddChild(LabelledRow("upr-pct-row", "Scale:", percentSpin));
    }

    keepAspect = std::make_shared<UltraCanvasCheckbox>("upr-aspect", 0, 0, 0, 24, "Keep aspect ratio");
    keepAspect->SetChecked(!canvas);
    keepAspect->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    AddChild(keepAspect);

    if (canvas) {
        anchorDrop = CreateDropdown("upr-anchor", 0, 0, 200, 24);
        for (const char* n : { "Top left", "Top", "Top right", "Left", "Centre", "Right", "Bottom left", "Bottom", "Bottom right" })
            anchorDrop->AddItem(n);
        anchorDrop->SetSelectedIndex(4, false);
        AddChild(LabelledRow("upr-anchor-row", "Anchor:", anchorDrop));
    }

    AddChild(ButtonRow("upr-buttons", this, [this]() {
        UltraPaintResizeResult r;
        r.width = static_cast<int>(widthSpin->GetValue());
        r.height = static_cast<int>(heightSpin->GetValue());
        r.anchor = anchorDrop ? anchorDrop->GetSelectedIndex() : 4;
        if (onAccept) onAccept(r);
    }, canvas ? "Resize" : "Scale"));
}

void UltraPaintResizeDialog::SyncFromWidth() {
    if (syncing) return;
    syncing = true;
    const double w = widthSpin->GetValue();
    if (keepAspect->IsChecked()) heightSpin->SetValue(std::max(1.0, std::round(w * origH / origW)));
    if (percentSpin) percentSpin->SetValue(w * 100.0 / origW);
    syncing = false;
}

void UltraPaintResizeDialog::SyncFromHeight() {
    if (syncing) return;
    syncing = true;
    const double h = heightSpin->GetValue();
    if (keepAspect->IsChecked()) widthSpin->SetValue(std::max(1.0, std::round(h * origW / origH)));
    if (percentSpin) percentSpin->SetValue(h * 100.0 / origH);
    syncing = false;
}

// ===========================================================================
// TEXT
// ===========================================================================

UltraPaintTextDialog::UltraPaintTextDialog(const UltraPaintTextResult& initial) : UltraCanvasWindow() {
    config_.title = "Text";
    config_.width = 420; config_.height = 210;
    config_.minWidth = 320; config_.minHeight = 180;
    config_.deleteOnClose = true;
    SetPadding(12);
    layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    textInput = CreateTextInput("upt-text", 0, 0, 0, 26);
    textInput->SetText(initial.text);
    AddChild(LabelledRow("upt-text-row", "Text:", textInput, 60));

    fontDrop = CreateDropdown("upt-font", 0, 0, 160, 24);
    for (const char* n : { "Sans", "Serif", "Monospace" }) fontDrop->AddItem(n);
    fontDrop->SetSelectedIndex(initial.font == "Serif" ? 1 : initial.font == "Monospace" ? 2 : 0, false);
    AddChild(LabelledRow("upt-font-row", "Font:", fontDrop, 60));

    sizeSpin = CreateIntSpinner("upt-size", 0, 0, 120, 24, 4, 500, initial.size, 1);
    sizeSpin->SetSuffix(" px");
    AddChild(LabelledRow("upt-size-row", "Size:", sizeSpin, 60));

    boldBox = std::make_shared<UltraCanvasCheckbox>("upt-bold", 0, 0, 0, 24, "Bold");
    boldBox->SetChecked(initial.bold);
    boldBox->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    AddChild(boldBox);

    AddChild(ButtonRow("upt-buttons", this, [this]() {
        UltraPaintTextResult r;
        r.text = textInput->GetText();
        r.font = fontDrop->GetSelectedIndex() == 1 ? "Serif" : fontDrop->GetSelectedIndex() == 2 ? "Monospace" : "Sans";
        r.size = static_cast<int>(sizeSpin->GetValue());
        r.bold = boldBox->IsChecked();
        if (onAccept) onAccept(r);
    }, "Place"));
    textInput->onEnterPressed = [this](const std::string&) -> bool {
        UltraPaintTextResult r;
        r.text = textInput->GetText();
        r.font = fontDrop->GetSelectedIndex() == 1 ? "Serif" : fontDrop->GetSelectedIndex() == 2 ? "Monospace" : "Sans";
        r.size = static_cast<int>(sizeSpin->GetValue());
        r.bold = boldBox->IsChecked();
        if (onAccept) onAccept(r);
        Close();
        return true;
    };
}

// ===========================================================================
// LAYER PROPERTIES
// ===========================================================================

UltraPaintLayerDialog::UltraPaintLayerDialog(const UltraPaintLayerProps& initial) : UltraCanvasWindow() {
    config_.title = "Layer Properties";
    config_.width = 380; config_.height = 240;
    config_.minWidth = 320; config_.minHeight = 200;
    config_.deleteOnClose = true;
    SetPadding(12);
    layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    nameInput = CreateTextInput("upl-name", 0, 0, 0, 26);
    nameInput->SetText(initial.name);
    AddChild(LabelledRow("upl-name-row", "Name:", nameInput, 90));

    opacitySpin = CreateIntSpinner("upl-opacity", 0, 0, 120, 24, 0, 100, static_cast<int>(std::lround(initial.opacity * 100.0f)), 1);
    opacitySpin->SetSuffix(" %");
    AddChild(LabelledRow("upl-opacity-row", "Opacity:", opacitySpin, 90));

    blendDrop = CreateDropdown("upl-blend", 0, 0, 160, 24);
    for (RasterBlendMode m : AllRasterBlendModes()) blendDrop->AddItem(RasterBlendModeName(m));
    blendDrop->SetSelectedIndex(initial.blendIndex, false);
    AddChild(LabelledRow("upl-blend-row", "Blend mode:", blendDrop, 90));

    visibleBox = std::make_shared<UltraCanvasCheckbox>("upl-visible", 0, 0, 0, 24, "Visible");
    visibleBox->SetChecked(initial.visible);
    visibleBox->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    AddChild(visibleBox);
    lockedBox = std::make_shared<UltraCanvasCheckbox>("upl-locked", 0, 0, 0, 24, "Locked (no painting)");
    lockedBox->SetChecked(initial.locked);
    lockedBox->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    AddChild(lockedBox);

    AddChild(ButtonRow("upl-buttons", this, [this]() {
        UltraPaintLayerProps r;
        r.name = nameInput->GetText();
        r.opacity = static_cast<float>(opacitySpin->GetValue() / 100.0);
        r.blendIndex = blendDrop->GetSelectedIndex();
        r.visible = visibleBox->IsChecked();
        r.locked = lockedBox->IsChecked();
        if (onAccept) onAccept(r);
    }));
}

// ===========================================================================
// COLOUR TO ALPHA
// ===========================================================================

UltraPaintColourToAlphaDialog::UltraPaintColourToAlphaDialog(const UltraPaintColourToAlphaParams& initial)
    : UltraCanvasWindow(), params(initial) {
    config_.title = "Colour to Alpha";
    config_.width = 380; config_.height = 560;
    config_.minWidth = 340; config_.minHeight = 500;
    config_.resizable = false;
    config_.deleteOnClose = true;
    SetPadding(12);
    layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // Two labels rather than one string with a newline in it: UltraCanvasLabel
    // draws a single line.
    const char* hintLines[] = { "Pick the colour to make transparent.",
                                "The eyedropper samples it off the canvas." };
    for (int i = 0; i < 2; ++i) {
        auto hint = CreateLabel("upc-hint" + std::to_string(i), 0, 0, 0, 17, hintLines[i]);
        hint->SetFontSize(11);
        hint->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        AddChild(hint);
    }

    // The colour itself: the framework picker, whose built-in eyedropper lets
    // the user take the colour straight off the image behind this window.
    // Same proportions the right-hand panel's picker uses, so it reads as the
    // same control.
    picker = CreateColorPicker("upc-colour", params.colour, 0, 0, 290, 300);
    picker->SetUIScale(0.78f);
    picker->SetShowAlpha(false);
    picker->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    picker->onColorChanged = [this](const Color& c) { params.colour = c; EmitPreview(); };
    picker->onColorChanging = [this](const Color& c) { params.colour = c; EmitPreview(); };
    AddChild(picker);

    sliders = std::make_shared<UltraCanvasContainer>("upc-sliders", 0, 0, 0, 124);
    sliders->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    sliders->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    constexpr float kLabelW = 132.0f;   // wide enough for "Transparency %"
    PaintOptionWidgets::AddSliderRow(*sliders, "upc-tol", "Tolerance", 0, 255, static_cast<float>(params.tolerance), 1, true,
                                     [this](float v) { params.tolerance = static_cast<int>(v); EmitPreview(); }, kLabelW);
    PaintOptionWidgets::AddSliderRow(*sliders, "upc-soft", "Softness", 0, 255, static_cast<float>(params.softness), 1, true,
                                     [this](float v) { params.softness = static_cast<int>(v); EmitPreview(); }, kLabelW);
    PaintOptionWidgets::AddSliderRow(*sliders, "upc-amount", "Transparency %", 0, 100, static_cast<float>(params.transparency), 1, true,
                                     [this](float v) { params.transparency = static_cast<int>(v); EmitPreview(); }, kLabelW);
    PaintOptionWidgets::AddCheckbox(*sliders, "upc-despill", "Remove colour fringe from soft edges", params.despill,
                                    [this](bool v) { params.despill = v; EmitPreview(); });
    AddChild(sliders);

    auto row = std::make_shared<UltraCanvasContainer>("upc-buttons", 0, 0, 0, 32);
    row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    previewBox = std::make_shared<UltraCanvasCheckbox>("upc-preview", 0, 0, 90, 24, "Preview");
    previewBox->SetChecked(true);
    previewBox->onStateChanged = [this](CheckedState, CheckedState n) {
        previewEnabled = n == CheckedState::Checked;
        if (onPreview) onPreview(params, previewEnabled);
    };
    previewBox->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    row->AddChild(previewBox);

    auto cancel = std::make_shared<UltraCanvasButton>("upc-cancel", 0, 0, 80, 28, "Cancel");
    cancel->onClick = [this]() { Close(); };
    cancel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->AddChild(cancel);

    auto ok = std::make_shared<UltraCanvasButton>("upc-ok", 0, 0, 80, 28, "OK");
    ok->onClick = [this]() {
        accepted = true;
        if (onAccept) onAccept(params);
        Close();
    };
    ok->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->AddChild(ok);
    AddChild(row);

    onWindowClosed = [this]() { if (!accepted && onCancel) onCancel(); };
}

void UltraPaintColourToAlphaDialog::EmitPreview() {
    if (previewEnabled && onPreview) onPreview(params, true);
}

} // namespace UltraCanvas
