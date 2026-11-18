// Apps/UltraPaint/UltraPaintDialogs.cpp
// New Image, Scale Image / Canvas Size, Layer Properties, Import, Colour to
// Alpha and Password Protection windows.
// Version: 1.7.0 - every text in the person's language (Tr); Colour to Alpha's buttons size to their text
// Version: 1.6.0 - Scale Image: Resampling (Lanczos 3 ... Nearest) and Sharpen after scaling
// Version: 1.5.0 - "Open in new tab" in place of "Open new window"
// Version: 1.4.0 - Password Protection: the password a *.ucd is saved with
// Version: 1.3.0 - the Text window is gone: text is typed on the canvas
// Version: 1.2.1 - Colour to Alpha: the picker in the framework's light style, as the panel's
// Version: 1.2.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#include "UltraPaintDialogs.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8
#include "UltraPaintTools.h"   // PaintOptionWidgets
#include "UltraCanvasRasterLayer.h"
#include "UltraCanvasFormLayout.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

namespace UltraCanvas {

namespace UltraPaintDialogParts {

std::shared_ptr<UltraCanvasContainer> FormGrid(const std::string& id) {
    // The framework's two-column form: captions hug their text in the first
    // column, controls take the rest of the second. One grid per dialog is
    // what lines the controls up and lets a longer translation widen the
    // caption column instead of being cut off.
    return CreateFormGrid(id, 8.0f, 10.0f);
}

std::shared_ptr<UltraCanvasLabel> FormRow(const std::shared_ptr<UltraCanvasContainer>& grid,
                                          const std::string& id, const std::string& label,
                                          std::shared_ptr<UltraCanvasUIElement> widget) {
    widget->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    return AddFormRow(grid, id, label, widget);
}

void FormWideRow(const std::shared_ptr<UltraCanvasContainer>& grid,
                 std::shared_ptr<UltraCanvasUIElement> element) {
    AddFormWideRow(grid, element);
}

void SizeButtonToText(const std::shared_ptr<UltraCanvasButton>& button) {
    // Auto width plus a floor: "Speichern" has to fit, "OK" must not shrink to
    // a stub. Same idiom as UltraCanvasModalDialog::SizeButtonToLabel.
    if (!button) return;
    button->size.width = CSSLayout::Dimension::Auto();
    CSSLayout::BoxConstraints limits = button->boxConstraints.value_or(CSSLayout::BoxConstraints{});
    limits.minWidth = CSSLayout::Dimension::Px(88.0f);
    button->boxConstraints = limits;
    button->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
}

std::shared_ptr<UltraCanvasContainer> ButtonRow(const std::string& id, UltraCanvasWindow* win,
                                                const std::function<void()>& onOk, const std::string& okLabel) {
    auto row = std::make_shared<UltraCanvasContainer>(id, 0, 0, 0, 34);
    row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center)
                .SetFlexJustifyContent(CSSLayout::JustifyContent::FlexEnd);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto cancel = std::make_shared<UltraCanvasButton>(id + "-cancel", 0, 0, 84, 28, Tr("Cancel"));
    cancel->onClick = [win]() { win->Close(); };
    SizeButtonToText(cancel);
    row->AddChild(cancel);
    auto ok = std::make_shared<UltraCanvasButton>(id + "-ok", 0, 0, 84, 28, okLabel.empty() ? Tr("OK") : okLabel);
    ok->onClick = [win, onOk]() { if (onOk) onOk(); win->Close(); };
    // The action the dialog exists for reads as the default.
    ok->SetStyle(ButtonStyles::PrimaryStyle());
    SizeButtonToText(ok);
    row->AddChild(ok);
    return row;
}

std::string BlendModeDisplayName(RasterBlendMode mode) {
    switch (mode) {
        case RasterBlendMode::Normal:     return Tr("Normal");
        case RasterBlendMode::Multiply:   return Tr("Multiply");
        case RasterBlendMode::Screen:     return Tr("Screen");
        case RasterBlendMode::Overlay:    return Tr("Overlay");
        case RasterBlendMode::Darken:     return Tr("Darken");
        case RasterBlendMode::Lighten:    return Tr("Lighten");
        case RasterBlendMode::Difference: return Tr("Difference");
        case RasterBlendMode::Addition:   return Tr("Addition");
        case RasterBlendMode::Subtract:   return Tr("Subtract");
        case RasterBlendMode::SoftLight:  return Tr("Soft Light");
        case RasterBlendMode::HardLight:  return Tr("Hard Light");
    }
    return RasterBlendModeName(mode);
}

} // namespace UltraPaintDialogParts

using namespace UltraPaintDialogParts;

namespace {
    struct Preset { std::string name; int w, h; };
    // Built when the dialog is, not at static initialisation: the names are
    // in the person's language, and the language is not known before main().
    std::vector<Preset> Presets() {
        return {
            { Tr("Custom"), 0, 0 }, { "640 x 480", 640, 480 }, { "800 x 600", 800, 600 }, { "1024 x 768", 1024, 768 },
            { Tr("1280 x 720 (HD)"), 1280, 720 }, { Tr("1920 x 1080 (Full HD)"), 1920, 1080 }, { "2560 x 1440", 2560, 1440 },
            { Tr("3840 x 2160 (4K)"), 3840, 2160 }, { Tr("A4 300 dpi"), 2480, 3508 }, { Tr("Instagram square"), 1080, 1080 },
            { TrF("Icon {0}", 512), 512, 512 }, { TrF("Icon {0}", 256), 256, 256 }
        };
    }

    std::string FileNameOnly(const std::string& path) {
        if (path.empty()) return Tr("Untitled");
        return PathToUtf8(PathFromUtf8(path).filename());
    }
}

// ===========================================================================
// NEW IMAGE
// ===========================================================================

UltraPaintNewImageDialog::UltraPaintNewImageDialog(const UltraPaintNewImageResult& initial) : UltraCanvasWindow() {
    config_.title = Tr("New Image");
    config_.width = 360; config_.height = 230;
    config_.minWidth = 320; config_.minHeight = 200;
    // Resizable: a longer caption in another language widens the caption
    // column, and the user can give the controls their room back.
    config_.resizable = true;
    config_.deleteOnClose = true;
    SetPadding(12);
    layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto form = FormGrid("upn-form");
    AddChild(form);

    const std::vector<Preset> presets = Presets();
    presetDrop = CreateDropdown("upn-preset", 0, 0, 200, 24);
    for (const auto& p : presets) presetDrop->AddItem(p.name);
    presetDrop->SetSelectedIndex(0, false);
    FormRow(form, "upn-preset-row", Tr("Preset:"), presetDrop);

    widthSpin = CreateIntSpinner("upn-w", 0, 0, 120, 24, 1, 20000, initial.width, 1);
    widthSpin->SetSuffix(" px");
    FormRow(form, "upn-w-row", Tr("Width:"), widthSpin);
    heightSpin = CreateIntSpinner("upn-h", 0, 0, 120, 24, 1, 20000, initial.height, 1);
    heightSpin->SetSuffix(" px");
    FormRow(form, "upn-h-row", Tr("Height:"), heightSpin);

    backgroundDrop = CreateDropdown("upn-bg", 0, 0, 200, 24);
    for (const std::string& n : { Tr("White"), Tr("Black"), Tr("Transparent"), Tr("Foreground colour"), Tr("Background colour") })
        backgroundDrop->AddItem(n);
    backgroundDrop->SetSelectedIndex(std::clamp(initial.background, 0, 4), false);
    FormRow(form, "upn-bg-row", Tr("Background:"), backgroundDrop);

    presetDrop->onSelectionChanged = [this, presets](int i, const DropdownItem&) {
        if (i <= 0 || i >= static_cast<int>(presets.size())) return;
        widthSpin->SetValue(presets[i].w);
        heightSpin->SetValue(presets[i].h);
    };

    AddChild(ButtonRow("upn-buttons", this, [this]() {
        UltraPaintNewImageResult r;
        r.width = static_cast<int>(widthSpin->GetValue());
        r.height = static_cast<int>(heightSpin->GetValue());
        r.background = backgroundDrop->GetSelectedIndex();
        if (onAccept) onAccept(r);
    }, Tr("Create")));
}

// ===========================================================================
// SCALE IMAGE / CANVAS SIZE
// ===========================================================================

namespace {
    // The Resampling list, in its order: the sharpest first
    const UCResampleMethod kScaleMethods[] = {
        UCResampleMethod::Lanczos3, UCResampleMethod::Lanczos2, UCResampleMethod::Mitchell,
        UCResampleMethod::Bicubic, UCResampleMethod::Bilinear, UCResampleMethod::Nearest,
    };
    // How strong Sharpen after scaling is (UCScaleImageOptions::sharpen)
    constexpr double kScaleSharpen = 0.5;
}

UltraPaintResizeDialog::UltraPaintResizeDialog(int w, int h, bool canvasMode, const UCScaleImageOptions& initialScale)
    : UltraCanvasWindow(), origW(std::max(1, w)), origH(std::max(1, h)), canvas(canvasMode) {
    config_.title = canvas ? Tr("Canvas Size") : Tr("Scale Image");
    config_.width = canvas ? 360 : 440; config_.height = canvas ? 240 : 310;
    config_.minWidth = 320; config_.minHeight = 200;
    // Resizable: a longer caption in another language widens the caption
    // column, and the user can give the controls their room back.
    config_.resizable = true;
    config_.deleteOnClose = true;
    SetPadding(12);
    layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto info = CreateLabel("upr-info", 0, 0, 0, 22, TrF("Current size: {0} x {1} px", origW, origH));
    info->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    AddChild(info);

    auto form = FormGrid("upr-form");
    AddChild(form);

    widthSpin = CreateIntSpinner("upr-w", 0, 0, 120, 24, 1, 20000, origW, 1);
    widthSpin->SetSuffix(" px");
    widthSpin->onValueChanged = [this](double) { SyncFromWidth(); };
    FormRow(form, "upr-w-row", Tr("Width:"), widthSpin);
    heightSpin = CreateIntSpinner("upr-h", 0, 0, 120, 24, 1, 20000, origH, 1);
    heightSpin->SetSuffix(" px");
    heightSpin->onValueChanged = [this](double) { SyncFromHeight(); };
    FormRow(form, "upr-h-row", Tr("Height:"), heightSpin);

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
        FormRow(form, "upr-pct-row", Tr("Scale:"), percentSpin);
    }

    keepAspect = std::make_shared<UltraCanvasCheckbox>("upr-aspect", 0, 0, 0, 24, Tr("Keep aspect ratio"));
    keepAspect->SetChecked(!canvas);
    FormWideRow(form, keepAspect);

    if (!canvas) {
        methodDrop = CreateDropdown("upr-method", 0, 0, 280, 24);
        methodDrop->AddItem(Tr("Lanczos 3 (sharpest)"));
        methodDrop->AddItem(Tr("Lanczos 2 (sharp)"));
        methodDrop->AddItem(Tr("Mitchell (clean edges)"));
        methodDrop->AddItem(Tr("Bicubic"));
        methodDrop->AddItem(Tr("Bilinear (soft)"));
        methodDrop->AddItem(Tr("Nearest (hard pixels)"));
        int selected = 0;
        for (int i = 0; i < static_cast<int>(std::size(kScaleMethods)); ++i)
            if (kScaleMethods[i] == initialScale.method) selected = i;
        methodDrop->SetSelectedIndex(selected, false);
        methodDrop->SetTooltip(Tr("How the new pixels are worked out. Photos: Lanczos 3, or Mitchell for "
                                  "edges without a halo. Pixel art, icons and screenshots: Nearest, at 200 %, 300 % "
                                  "or 400 %. None can add detail the picture does not have."));
        FormRow(form, "upr-method-row", Tr("Resampling:"), methodDrop);

        sharpenBox = std::make_shared<UltraCanvasCheckbox>("upr-sharpen", 0, 0, 0, 24, Tr("Sharpen after scaling"));
        sharpenBox->SetChecked(initialScale.sharpen > 0.0);
        sharpenBox->SetTooltip(Tr("An unsharp mask after the scale, to win back some of the crispness an enlargement loses"));
        FormWideRow(form, sharpenBox);
        // Hard pixels have nothing to sharpen
        auto syncSharpen = [this]() {
            const int i = methodDrop->GetSelectedIndex();
            sharpenBox->SetDisabled(i >= 0 && kScaleMethods[i] == UCResampleMethod::Nearest);
        };
        methodDrop->onSelectionChanged = [syncSharpen](int, const DropdownItem&) { syncSharpen(); };
        syncSharpen();
    }

    if (canvas) {
        anchorDrop = CreateDropdown("upr-anchor", 0, 0, 200, 24);
        for (const std::string& n : { Tr("Top left"), Tr("Top"), Tr("Top right"), Tr("Left"), Tr("Centre"), Tr("Right"),
                                      Tr("Bottom left"), Tr("Bottom"), Tr("Bottom right") })
            anchorDrop->AddItem(n);
        anchorDrop->SetSelectedIndex(4, false);
        FormRow(form, "upr-anchor-row", Tr("Anchor:"), anchorDrop);
    }

    AddChild(ButtonRow("upr-buttons", this, [this]() {
        UltraPaintResizeResult r;
        r.width = static_cast<int>(widthSpin->GetValue());
        r.height = static_cast<int>(heightSpin->GetValue());
        r.anchor = anchorDrop ? anchorDrop->GetSelectedIndex() : 4;
        if (methodDrop) {
            const int i = methodDrop->GetSelectedIndex();
            if (i >= 0 && i < static_cast<int>(std::size(kScaleMethods))) r.scale.method = kScaleMethods[i];
            r.scale.sharpen = (sharpenBox->IsChecked() && r.scale.method != UCResampleMethod::Nearest) ? kScaleSharpen : 0.0;
        }
        if (onAccept) onAccept(r);
    }, canvas ? Tr("Resize") : Tr("Scale")));
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
// LAYER PROPERTIES
// ===========================================================================

UltraPaintLayerDialog::UltraPaintLayerDialog(const UltraPaintLayerProps& initial) : UltraCanvasWindow() {
    config_.title = Tr("Layer Properties");
    config_.width = 380; config_.height = 240;
    config_.minWidth = 320; config_.minHeight = 200;
    config_.deleteOnClose = true;
    SetPadding(12);
    layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto form = FormGrid("upl-form");
    AddChild(form);

    nameInput = CreateTextInput("upl-name", 0, 0, 0, 26);
    nameInput->SetText(initial.name);
    FormRow(form, "upl-name-row", Tr("Name:"), nameInput);

    opacitySpin = CreateIntSpinner("upl-opacity", 0, 0, 120, 24, 0, 100, static_cast<int>(std::lround(initial.opacity * 100.0f)), 1);
    opacitySpin->SetSuffix(" %");
    FormRow(form, "upl-opacity-row", Tr("Opacity:"), opacitySpin);

    blendDrop = CreateDropdown("upl-blend", 0, 0, 160, 24);
    for (RasterBlendMode m : AllRasterBlendModes()) blendDrop->AddItem(BlendModeDisplayName(m));
    blendDrop->SetSelectedIndex(initial.blendIndex, false);
    FormRow(form, "upl-blend-row", Tr("Blend mode:"), blendDrop);

    visibleBox = std::make_shared<UltraCanvasCheckbox>("upl-visible", 0, 0, 0, 24, Tr("Visible"));
    visibleBox->SetChecked(initial.visible);
    FormWideRow(form, visibleBox);
    lockedBox = std::make_shared<UltraCanvasCheckbox>("upl-locked", 0, 0, 0, 24, Tr("Locked (no painting)"));
    lockedBox->SetChecked(initial.locked);
    FormWideRow(form, lockedBox);

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
// PASSWORD PROTECTION
// ===========================================================================

UltraPaintPasswordDialog::UltraPaintPasswordDialog(bool protectedNow, bool hidePreview, bool available)
    : UltraCanvasWindow() {
    config_.title = Tr("Password Protection");
    config_.width = 440; config_.height = 250;
    config_.minWidth = 380; config_.minHeight = 260;
    config_.resizable = true;
    config_.deleteOnClose = true;
    SetPadding(12);
    layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // What it does and what it costs, before anything is typed.
    auto note = CreateLabel("upw-note", 0, 0, 0, 0,
        protectedNow
            ? Tr("This image is password-protected: saved as an UltraCanvas layered image (.ucd), "
                 "it is encrypted with its password. Change the password, or remove the protection.")
            : Tr("Saved as an UltraCanvas layered image (.ucd), the image is encrypted with this "
                 "password. Without it the file cannot be opened - a forgotten password cannot be "
                 "recovered."));
    note->SetWrap(TextWrap::WrapWord);
    note->size.height = CSSLayout::Dimension::Auto();
    note->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    AddChild(note);

    auto form = FormGrid("upw-form");
    AddChild(form);
    passwordInput = CreateRevealablePasswordInput("upw-password", 0, 0, 0, 26);
    FormRow(form, "upw-password-row", protectedNow ? Tr("New password:") : Tr("Password:"), passwordInput);
    confirmInput = CreateRevealablePasswordInput("upw-confirm", 0, 0, 0, 26);
    FormRow(form, "upw-confirm-row", Tr("Again:"), confirmInput);
    hidePreviewBox = std::make_shared<UltraCanvasCheckbox>("upw-private", 0, 0, 0, 24,
                                                           Tr("Hide the preview too (no thumbnail in file managers)"));
    hidePreviewBox->SetChecked(hidePreview);
    FormWideRow(form, hidePreviewBox);
    // Enter in the second field is OK.
    confirmInput->onEnterPressed = [this](const std::string&) { Accept(); return true; };

    message = CreateLabel("upw-message", 0, 0, 0, 20, "");
    message->SetTextColor(Color(176, 32, 32, 255));
    message->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    AddChild(message);
    if (!available) {
        message->SetText(Tr("This build cannot encrypt: it has no cryptography library (libsodium)."));
        passwordInput->SetDisabled(true);
        confirmInput->SetDisabled(true);
        hidePreviewBox->SetDisabled(true);
    }

    auto row = std::make_shared<UltraCanvasContainer>("upw-buttons", 0, 0, 0, 34);
    row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center)
                .SetFlexJustifyContent(CSSLayout::JustifyContent::FlexEnd);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    if (protectedNow) {
        auto remove = std::make_shared<UltraCanvasButton>("upw-remove", 0, 0, 84, 28, Tr("Remove Protection"));
        remove->onClick = [this]() {
            UltraPaintPasswordResult r;
            r.remove = true;
            if (onAccept) onAccept(r);
            Close();
        };
        SizeButtonToText(remove);
        row->AddChild(remove);
    }
    auto cancel = std::make_shared<UltraCanvasButton>("upw-cancel", 0, 0, 84, 28, Tr("Cancel"));
    cancel->onClick = [this]() { Close(); };
    SizeButtonToText(cancel);
    row->AddChild(cancel);
    auto ok = std::make_shared<UltraCanvasButton>("upw-ok", 0, 0, 84, 28, protectedNow ? Tr("Change") : Tr("Protect"));
    ok->onClick = [this]() { Accept(); };
    ok->SetStyle(ButtonStyles::PrimaryStyle());
    ok->SetDisabled(!available);
    SizeButtonToText(ok);
    row->AddChild(ok);
    AddChild(row);
}

void UltraPaintPasswordDialog::Accept() {
    if (passwordInput->IsDisabled()) return;
    const std::string password = passwordInput->GetText();
    if (password.empty()) { message->SetText(Tr("Type a password.")); return; }
    if (password != confirmInput->GetText()) {
        message->SetText(Tr("The two passwords differ - type it again."));
        return;
    }
    UltraPaintPasswordResult r;
    r.password = password;
    r.hidePreview = hidePreviewBox->IsChecked();
    if (onAccept) onAccept(r);
    Close();
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
    config_.title = req.offerMerge ? Tr("Open or Merge") : Tr("Open Drawing");
    config_.width = 470;
    config_.height = 128 + (req.vector && !req.provider.empty() ? 20 : 0) +
                     (showSize ? 64 : 0) + (paged ? 28 : 0) + (req.offerMerge ? 28 : 0);
    config_.minWidth = 420;
    config_.minHeight = 140;
    // Resizable: a longer caption in another language widens the caption
    // column, and the user can give the controls their room back.
    config_.resizable = true;
    config_.deleteOnClose = true;
    SetPadding(12);
    layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    const std::string heading = req.extraFiles > 0
            // One more file has a sentence of its own; TrN's forms all carry the count
            ? (req.extraFiles == 1
                   ? TrF("{0} and one more file", FileNameOnly(req.path))
                   : LocalisationDetail::Substitute(TrN("{0} and {n} more file", "{0} and {n} more files", req.extraFiles),
                                                    {FileNameOnly(req.path)}))
            : FileNameOnly(req.path);
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
        addSubtitle("upi-sub", size.empty() ? Tr("Vector drawing") : TrF("Vector drawing, {0} at its natural size", size));
        if (!req.provider.empty()) addSubtitle("upi-provider", TrF("Rendered by {0}", req.provider));
    } else {
        addSubtitle("upi-sub", size);
    }

    auto form = FormGrid("upi-form");
    AddChild(form);

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
        FormRow(form, "upi-w-row", Tr("Raster width:"), widthSpin);
        heightSpin = CreateIntSpinner("upi-h", 0, 0, 120, 24, 1, 20000, startH, 1);
        heightSpin->SetSuffix(" px");
        heightSpin->onValueChanged = [this](double) { SyncFromHeight(); };
        FormRow(form, "upi-h-row", Tr("Raster height:"), heightSpin);

        if (paged) {
            pageSpin = CreateIntSpinner("upi-page", 0, 0, 120, 24, 1, req.pageCount, 1, 1);
            pageSpin->SetSuffix(" " + TrF("of {0}", req.pageCount));
            FormRow(form, "upi-page-row", Tr("Page:"), pageSpin);
        }
    }

    // Merging a bitmap larger than the canvas would silently crop it, so the
    // fit is offered (and taken by default) exactly when it would.
    if (req.offerMerge && !req.vector && req.canvasWidth > 0 && req.canvasHeight > 0 &&
        (req.naturalWidth > req.canvasWidth || req.naturalHeight > req.canvasHeight)) {
        fitBox = std::make_shared<UltraCanvasCheckbox>("upi-fit", 0, 0, 0, 24,
                                                       Tr("Scale to fit the canvas when merging"));
        fitBox->SetChecked(true);
        FormWideRow(form, fitBox);
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
        if (action == UltraPaintImportResult::Action::Open ||
            action == UltraPaintImportResult::Action::NewTab) {
            b->SetStyle(ButtonStyles::PrimaryStyle());
        }
        SizeButtonToText(b);
        row->AddChild(b);
        return b;
    };
    addButton("upi-cancel", Tr("Cancel"), 88, UltraPaintImportResult::Action::Cancel);
    if (req.offerMerge) {
        addButton("upi-merge", Tr("Merge image"), 136, UltraPaintImportResult::Action::Merge);
        addButton("upi-tab", Tr("Open in new tab"), 176, UltraPaintImportResult::Action::NewTab);
    } else {
        addButton("upi-open", Tr("Open"), 96, UltraPaintImportResult::Action::Open);
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
    config_.title = Tr("Colour to Alpha");
    config_.width = 380; config_.height = 560;
    config_.minWidth = 340; config_.minHeight = 500;
    // Resizable: a longer caption in another language widens the caption
    // column, and the user can give the controls their room back.
    config_.resizable = true;
    config_.deleteOnClose = true;
    SetPadding(12);
    layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // Two labels rather than one string with a newline in it: UltraCanvasLabel
    // draws a single line.
    const std::string hintLines[] = { Tr("Pick the colour to make transparent."),
                                      Tr("The eyedropper samples it off the canvas.") };
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
    // The panel's light style, on the dialog's own grey rather than as a
    // white block on it.
    ColorPickerStyle pickerStyle = ColorPickerStyle::Light();
    pickerStyle.backgroundColor = config_.backgroundColor;
    picker->SetStyle(pickerStyle);
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
    PaintOptionWidgets::AddSliderRow(*sliders, "upc-tol", Tr("Tolerance"), 0, 255, static_cast<float>(params.tolerance), 1, true,
                                     [this](float v) { params.tolerance = static_cast<int>(v); EmitPreview(); }, kLabelW);
    PaintOptionWidgets::AddSliderRow(*sliders, "upc-soft", Tr("Softness"), 0, 255, static_cast<float>(params.softness), 1, true,
                                     [this](float v) { params.softness = static_cast<int>(v); EmitPreview(); }, kLabelW);
    PaintOptionWidgets::AddSliderRow(*sliders, "upc-amount", Tr("Transparency %"), 0, 100, static_cast<float>(params.transparency), 1, true,
                                     [this](float v) { params.transparency = static_cast<int>(v); EmitPreview(); }, kLabelW);
    PaintOptionWidgets::AddCheckbox(*sliders, "upc-despill", Tr("Remove colour fringe from soft edges"), params.despill,
                                    [this](bool v) { params.despill = v; EmitPreview(); });
    AddChild(sliders);

    auto row = std::make_shared<UltraCanvasContainer>("upc-buttons", 0, 0, 0, 32);
    row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    previewBox = std::make_shared<UltraCanvasCheckbox>("upc-preview", 0, 0, 90, 24, Tr("Preview"));
    previewBox->SetChecked(true);
    previewBox->onStateChanged = [this](CheckedState, CheckedState n) {
        previewEnabled = n == CheckedState::Checked;
        if (onPreview) onPreview(params, previewEnabled);
    };
    previewBox->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    row->AddChild(previewBox);

    auto cancel = std::make_shared<UltraCanvasButton>("upc-cancel", 0, 0, 80, 28, Tr("Cancel"));
    cancel->onClick = [this]() { Close(); };
    SizeButtonToText(cancel);
    row->AddChild(cancel);

    auto ok = std::make_shared<UltraCanvasButton>("upc-ok", 0, 0, 80, 28, Tr("OK"));
    ok->onClick = [this]() {
        accepted = true;
        if (onAccept) onAccept(params);
        Close();
    };
    SizeButtonToText(ok);
    row->AddChild(ok);
    AddChild(row);

    onWindowClosed = [this]() { if (!accepted && onCancel) onCancel(); };
}

void UltraPaintColourToAlphaDialog::EmitPreview() {
    if (previewEnabled && onPreview) onPreview(params, true);
}

} // namespace UltraCanvas
