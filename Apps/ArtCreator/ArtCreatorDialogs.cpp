// Apps/ArtCreator/ArtCreatorDialogs.cpp
// New Drawing / Document Setup and Text windows.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "ArtCreatorDialogs.h"
#include "UltraCanvasFormLayout.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

namespace ArtCreatorDialogParts {

std::shared_ptr<UltraCanvasContainer> FormGrid(const std::string& id) {
    return CreateFormGrid(id, 8.0f, 10.0f);
}

std::shared_ptr<UltraCanvasLabel> FormRow(const std::shared_ptr<UltraCanvasContainer>& grid,
                                          const std::string& id, const std::string& label,
                                          std::shared_ptr<UltraCanvasUIElement> widget) {
    widget->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    return AddFormRow(grid, id, label, widget);
}

void FormWideRow(const std::shared_ptr<UltraCanvasContainer>& grid, std::shared_ptr<UltraCanvasUIElement> element) {
    AddFormWideRow(grid, element);
}

void SizeButtonToText(const std::shared_ptr<UltraCanvasButton>& button) {
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
    auto cancel = std::make_shared<UltraCanvasButton>(id + "-cancel", 0, 0, 84, 28, "Cancel");
    cancel->onClick = [win]() { win->Close(); };
    SizeButtonToText(cancel);
    row->AddChild(cancel);
    auto ok = std::make_shared<UltraCanvasButton>(id + "-ok", 0, 0, 84, 28, okLabel);
    ok->onClick = [win, onOk]() { if (onOk) onOk(); win->Close(); };
    ok->SetStyle(ButtonStyles::PrimaryStyle());
    SizeButtonToText(ok);
    row->AddChild(ok);
    return row;
}

} // namespace ArtCreatorDialogParts

using namespace ArtCreatorDialogParts;

// ===========================================================================
// PAGE
// ===========================================================================

namespace {
    // Presets in millimetres or pixels (unit index per ArtCreatorPageResult).
    struct PagePreset { const char* name; double w, h; int unit; };
    const PagePreset kPresets[] = {
        { "Custom", 0, 0, 0 },
        { "A4 (210 x 297 mm)", 210, 297, 0 },
        { "A3 (297 x 420 mm)", 297, 420, 0 },
        { "A5 (148 x 210 mm)", 148, 210, 0 },
        { "US Letter (8.5 x 11 in)", 8.5, 11, 2 },
        { "US Legal (8.5 x 14 in)", 8.5, 14, 2 },
        { "Square 200 mm", 200, 200, 0 },
        { "Business card (85 x 55 mm)", 85, 55, 0 },
        { "Full HD (1920 x 1080 px)", 1920, 1080, 4 },
        { "4K (3840 x 2160 px)", 3840, 2160, 4 },
        { "Instagram (1080 x 1080 px)", 1080, 1080, 4 },
        { "Icon (512 x 512 px)", 512, 512, 4 },
    };
    const char* kUnitNames[] = { "mm", "cm", "in", "pt", "px" };
}

double ArtCreatorPageResult::PointsPerUnit() const {
    switch (unit) {
        case 0: return 72.0 / 25.4;
        case 1: return 72.0 / 2.54;
        case 2: return 72.0;
        case 3: return 1.0;
        case 4: return 0.75;
        default: return 1.0;
    }
}

const char* ArtCreatorPageResult::UnitSymbol(int unit) {
    return (unit >= 0 && unit < 5) ? kUnitNames[unit] : "pt";
}

ArtCreatorPageDialog::ArtCreatorPageDialog(const ArtCreatorPageResult& initial, bool setup) : UltraCanvasWindow() {
    config_.title = setup ? "Document Setup" : "New Drawing";
    config_.width = 380; config_.height = 250;
    config_.minWidth = 340; config_.minHeight = 220;
    config_.resizable = true;
    config_.deleteOnClose = true;
    SetPadding(12);
    layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto form = FormGrid("acp-form");
    AddChild(form);

    presetDrop = CreateDropdown("acp-preset", 0, 0, 220, 24);
    for (const auto& p : kPresets) presetDrop->AddItem(p.name);
    presetDrop->SetSelectedIndex(0, false);
    FormRow(form, "acp-preset-row", "Page:", presetDrop);

    widthSpin = CreateDecimalSpinner("acp-w", 0, 0, 120, 24, 1.0, 100000.0, initial.width, 1.0, 2);
    FormRow(form, "acp-w-row", "Width:", widthSpin);
    heightSpin = CreateDecimalSpinner("acp-h", 0, 0, 120, 24, 1.0, 100000.0, initial.height, 1.0, 2);
    FormRow(form, "acp-h-row", "Height:", heightSpin);

    unitDrop = CreateDropdown("acp-unit", 0, 0, 120, 24);
    for (const char* u : kUnitNames) unitDrop->AddItem(u);
    unitDrop->SetSelectedIndex(std::clamp(initial.unit, 0, 4), false);
    FormRow(form, "acp-unit-row", "Unit:", unitDrop);

    orientationDrop = CreateDropdown("acp-orient", 0, 0, 120, 24);
    orientationDrop->AddItem("Portrait");
    orientationDrop->AddItem("Landscape");
    orientationDrop->SetSelectedIndex(initial.landscape ? 1 : 0, false);
    FormRow(form, "acp-orient-row", "Orientation:", orientationDrop);

    presetDrop->onSelectionChanged = [this](int i, const DropdownItem&) { ApplyPreset(i); };
    orientationDrop->onSelectionChanged = [this](int, const DropdownItem&) { SyncOrientation(); };
    unitDrop->onSelectionChanged = [this](int i, const DropdownItem&) {
        // Convert the numbers to the new unit so the page keeps its size.
        if (syncing) return;
        syncing = true;
        ArtCreatorPageResult from; from.unit = lastUnit; from.width = widthSpin->GetValue(); from.height = heightSpin->GetValue();
        ArtCreatorPageResult to; to.unit = i;
        widthSpin->SetValue(from.WidthPoints() / to.PointsPerUnit());
        heightSpin->SetValue(from.HeightPoints() / to.PointsPerUnit());
        lastUnit = i;
        syncing = false;
    };
    lastUnit = std::clamp(initial.unit, 0, 4);

    AddChild(ButtonRow("acp-buttons", this, [this]() {
        ArtCreatorPageResult r;
        r.width = widthSpin->GetValue();
        r.height = heightSpin->GetValue();
        r.unit = unitDrop->GetSelectedIndex();
        r.landscape = orientationDrop->GetSelectedIndex() == 1;
        if (onAccept) onAccept(r);
    }, setup ? "Apply" : "Create"));
}

void ArtCreatorPageDialog::ApplyPreset(int index) {
    if (index <= 0 || index >= static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]))) return;
    syncing = true;
    const PagePreset& p = kPresets[index];
    unitDrop->SetSelectedIndex(p.unit, false);
    lastUnit = p.unit;
    const bool landscape = orientationDrop->GetSelectedIndex() == 1;
    widthSpin->SetValue(landscape ? std::max(p.w, p.h) : std::min(p.w, p.h));
    heightSpin->SetValue(landscape ? std::min(p.w, p.h) : std::max(p.w, p.h));
    if (p.w == p.h) { widthSpin->SetValue(p.w); heightSpin->SetValue(p.h); }
    syncing = false;
}

void ArtCreatorPageDialog::SyncOrientation() {
    if (syncing) return;
    syncing = true;
    const double w = widthSpin->GetValue(), h = heightSpin->GetValue();
    const bool landscape = orientationDrop->GetSelectedIndex() == 1;
    if ((landscape && h > w) || (!landscape && w > h)) { widthSpin->SetValue(h); heightSpin->SetValue(w); }
    syncing = false;
}

// ===========================================================================
// TEXT
// ===========================================================================

ArtCreatorTextDialog::ArtCreatorTextDialog(const ArtCreatorTextResult& initial) : UltraCanvasWindow() {
    config_.title = "Text";
    config_.width = 400; config_.height = 230;
    config_.minWidth = 340; config_.minHeight = 200;
    config_.resizable = true;
    config_.deleteOnClose = true;
    SetPadding(12);
    layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto form = FormGrid("act-form");
    AddChild(form);

    textInput = CreateTextInput("act-text", 0, 0, 240, 26);
    textInput->SetText(initial.text);
    FormRow(form, "act-text-row", "Text:", textInput);

    fontDrop = CreateDropdown("act-font", 0, 0, 160, 24);
    for (const char* f : { "Sans", "Serif", "Monospace" }) fontDrop->AddItem(f);
    fontDrop->SetSelectedIndex(initial.font == "Serif" ? 1 : initial.font == "Monospace" ? 2 : 0, false);
    FormRow(form, "act-font-row", "Font:", fontDrop);

    sizeSpin = CreateIntSpinner("act-size", 0, 0, 120, 24, 4, 500, initial.size, 1);
    sizeSpin->SetSuffix(" pt");
    FormRow(form, "act-size-row", "Size:", sizeSpin);

    boldBox = std::make_shared<UltraCanvasCheckbox>("act-bold", 0, 0, 0, 24, "Bold");
    boldBox->SetChecked(initial.bold);
    FormWideRow(form, boldBox);
    italicBox = std::make_shared<UltraCanvasCheckbox>("act-italic", 0, 0, 0, 24, "Italic");
    italicBox->SetChecked(initial.italic);
    FormWideRow(form, italicBox);

    AddChild(ButtonRow("act-buttons", this, [this]() {
        ArtCreatorTextResult r;
        r.text = textInput->GetText();
        const int f = fontDrop->GetSelectedIndex();
        r.font = f == 1 ? "Serif" : f == 2 ? "Monospace" : "Sans";
        r.size = static_cast<int>(sizeSpin->GetValue());
        r.bold = boldBox->IsChecked();
        r.italic = italicBox->IsChecked();
        if (onAccept) onAccept(r);
    }, "Place"));
}

} // namespace UltraCanvas
