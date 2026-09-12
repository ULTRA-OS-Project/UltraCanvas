// Apps/UltraPaint/UltraPaintDialogs.cpp
// New Image, Scale Image / Canvas Size, Text, Layer Properties, Import and
// Colour to Alpha windows.
// Version: 1.1.0
// Last Modified: 2026-09-12
// Author: UltraCanvas Framework

#include "UltraPaintDialogs.h"
#include "UltraPaintTools.h"   // PaintOptionWidgets
#include "UltraCanvasRasterLayer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

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

    std::string FileNameOnly(const std::string& path) {
        if (path.empty()) return "Untitled";
        return std::filesystem::path(path).filename().string();
    }
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
// IMPORT
// ===========================================================================

UltraPaintImportDialog::UltraPaintImportDialog(const UltraPaintImportRequest& request)
    : UltraCanvasWindow(), req(request) {
    // The size controls belong to one drawing; a multi-file drop takes each
    // file at its own natural size instead.
    const bool showSize = req.vector && req.extraFiles == 0;
    const bool paged = showSize && req.pageCount > 1;
    config_.title = req.offerMerge ? "Open or Merge" : "Open Drawing";
    config_.width = 470;
    config_.height = 128 + (req.vector && !req.provider.empty() ? 20 : 0) +
                     (showSize ? 64 : 0) + (paged ? 28 : 0) + (req.offerMerge ? 28 : 0);
    config_.minWidth = 420;
    config_.minHeight = 140;
    config_.resizable = false;
    config_.deleteOnClose = true;
    SetPadding(12);
    layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    std::string heading = FileNameOnly(req.path);
    if (req.extraFiles == 1) heading += " and 1 more file";
    else if (req.extraFiles > 1) heading += " and " + std::to_string(req.extraFiles) + " more files";
    auto name = CreateLabel("upi-name", 0, 0, 0, 22, heading);
    name->SetFontWeight(FontWeight::Bold);
    name->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    AddChild(name);

    auto addSubtitle = [this](const std::string& id, const std::string& text) {
        if (text.empty()) return;
        auto sub = CreateLabel(id, 0, 0, 0, 18, text);
        sub->SetFontSize(11);
        sub->SetTextColor(Color(90, 90, 100, 255));
        sub->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        AddChild(sub);
    };
    const std::string size = (req.naturalWidth > 0 && req.naturalHeight > 0)
            ? std::to_string(req.naturalWidth) + " x " + std::to_string(req.naturalHeight) + " px"
            : std::string();
    if (req.vector) {
        addSubtitle("upi-sub", size.empty() ? "Vector drawing" : "Vector drawing, " + size + " at its natural size");
        if (!req.provider.empty()) addSubtitle("upi-provider", "Rendered by " + req.provider);
    } else {
        addSubtitle("upi-sub", size);
    }

    if (showSize) {
        // A vector drawing has no pixels until one asks for them, so the size
        // is the question: it defaults to the natural size, or to whatever
        // fits the open canvas when the drawing is going to be merged into it.
        int startW = std::max(1, req.naturalWidth);
        int startH = std::max(1, req.naturalHeight);
        if (req.offerMerge && req.canvasWidth > 0 && req.canvasHeight > 0 &&
            (startW > req.canvasWidth || startH > req.canvasHeight)) {
            const double k = std::min(static_cast<double>(req.canvasWidth) / startW,
                                      static_cast<double>(req.canvasHeight) / startH);
            startW = std::max(1, static_cast<int>(std::lround(startW * k)));
            startH = std::max(1, static_cast<int>(std::lround(startH * k)));
        }
        widthSpin = CreateIntSpinner("upi-w", 0, 0, 120, 24, 1, 20000, startW, 1);
        widthSpin->SetSuffix(" px");
        widthSpin->onValueChanged = [this](double) { SyncFromWidth(); };
        AddChild(LabelledRow("upi-w-row", "Raster width:", widthSpin, 120));
        heightSpin = CreateIntSpinner("upi-h", 0, 0, 120, 24, 1, 20000, startH, 1);
        heightSpin->SetSuffix(" px");
        heightSpin->onValueChanged = [this](double) { SyncFromHeight(); };
        AddChild(LabelledRow("upi-h-row", "Raster height:", heightSpin, 120));

        if (paged) {
            pageSpin = CreateIntSpinner("upi-page", 0, 0, 120, 24, 1, req.pageCount, 1, 1);
            pageSpin->SetSuffix(" of " + std::to_string(req.pageCount));
            AddChild(LabelledRow("upi-page-row", "Page:", pageSpin, 120));
        }
    }

    // Merging a bitmap larger than the canvas would silently crop it, so the
    // fit is offered (and taken by default) exactly when it would.
    if (req.offerMerge && !req.vector && req.canvasWidth > 0 && req.canvasHeight > 0 &&
        (req.naturalWidth > req.canvasWidth || req.naturalHeight > req.canvasHeight)) {
        fitBox = std::make_shared<UltraCanvasCheckbox>("upi-fit", 0, 0, 0, 24,
                                                       "Scale to fit the canvas when merging");
        fitBox->SetChecked(true);
        fitBox->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        AddChild(fitBox);
    }

    // ----- buttons -----
    auto row = std::make_shared<UltraCanvasContainer>("upi-buttons", 0, 0, 0, 34);
    row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center)
               .SetFlexJustifyContent(CSSLayout::JustifyContent::FlexEnd);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto addButton = [&](const std::string& id, const std::string& text, int width,
                         UltraPaintImportResult::Action action) {
        auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, 28, text);
        b->onClick = [this, action]() { Finish(action); };
        b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        row->AddChild(b);
        return b;
    };
    addButton("upi-cancel", "Cancel", 88, UltraPaintImportResult::Action::Cancel);
    if (req.offerMerge) {
        addButton("upi-merge", "Merge image", 136, UltraPaintImportResult::Action::Merge);
        addButton("upi-window", "Open new window", 176, UltraPaintImportResult::Action::NewWindow);
    } else {
        addButton("upi-open", "Open", 96, UltraPaintImportResult::Action::Open);
    }
    AddChild(row);
}

void UltraPaintImportDialog::SyncFromWidth() {
    if (syncing || !widthSpin || !heightSpin) return;
    if (req.naturalWidth <= 0 || req.naturalHeight <= 0) return;
    syncing = true;
    const double w = widthSpin->GetValue();
    heightSpin->SetValue(std::max(1.0, std::round(w * req.naturalHeight / req.naturalWidth)));
    syncing = false;
}

void UltraPaintImportDialog::SyncFromHeight() {
    if (syncing || !widthSpin || !heightSpin) return;
    if (req.naturalWidth <= 0 || req.naturalHeight <= 0) return;
    syncing = true;
    const double h = heightSpin->GetValue();
    widthSpin->SetValue(std::max(1.0, std::round(h * req.naturalWidth / req.naturalHeight)));
    syncing = false;
}

UltraPaintImportResult UltraPaintImportDialog::Collect(UltraPaintImportResult::Action action) const {
    UltraPaintImportResult r;
    r.action = action;
    r.width = widthSpin ? static_cast<int>(widthSpin->GetValue()) : req.naturalWidth;
    r.height = heightSpin ? static_cast<int>(heightSpin->GetValue()) : req.naturalHeight;
    r.page = pageSpin ? std::max(0, static_cast<int>(pageSpin->GetValue()) - 1) : 0;
    r.scaleToFit = fitBox && fitBox->IsChecked();
    return r;
}

void UltraPaintImportDialog::Finish(UltraPaintImportResult::Action action) {
    // Same order as UltraPaintDialogParts::ButtonRow: the callback runs while
    // the dialog is still alive, then the window closes itself.
    if (onAccept) onAccept(Collect(action));
    Close();
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
