// Apps/ArtCreator/ArtCreatorDialogs.cpp
// New Drawing / Document Setup and Text windows.
// Version: 1.1.0 - cards on the section page, segmented unit and orientation, the matching
//                 preset on open, Bold / Italic side by side and a preview of the text
// Version: 1.0.0
// Last Modified: 2026-10-22
// Author: UltraCanvas Framework

#include "ArtCreatorDialogs.h"
#include "UltraCanvasFormLayout.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

namespace ArtCreatorDialogParts {

namespace {
    constexpr float kFieldW = 230.0f;    // every field of a form, so their right edges line up
    constexpr float kFieldH = 26.0f;
}

void StyleDialog(UltraCanvasWindow& win, WindowConfig& config) {
    config.backgroundColor = Colors::SectionPage;
    win.SetBackgroundColor(Colors::SectionPage);
    win.SetPadding(10);
    win.layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
}

std::shared_ptr<UltraCanvasGroupBox> FormCard(const std::string& id, const std::string& title,
                                              std::shared_ptr<UltraCanvasContainer>& grid) {
    auto card = CreateGroupBox(id, 0, 0, 0, 0, title);
    card->SetFrameStyle(GroupBoxFrameStyle::Header);
    GroupBoxVisualStyle style = GroupBoxVisualStyle::SectionCard();
    style.contentPadding = 12.0f;
    card->SetVisualStyle(style);
    card->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    card->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    grid = FormGrid(id + "-form");
    card->AddChild(grid);
    return card;
}

std::shared_ptr<UltraCanvasContainer> FormGrid(const std::string& id) {
    return CreateFormGrid(id, 8.0f, 12.0f);
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
    // At the foot of the window, whatever room the cards leave.
    auto row = std::make_shared<UltraCanvasContainer>(id, 0, 0, 0, 30);
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

    // The preset a page is, in either orientation; 0 (Custom) for none.
    int MatchingPreset(const ArtCreatorPageResult& page) {
        const auto same = [](double a, double b) { return std::fabs(a - b) < 0.005; };
        for (int i = 1; i < static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0])); ++i) {
            const PagePreset& p = kPresets[i];
            if (p.unit != page.unit) continue;
            if ((same(p.w, page.width) && same(p.h, page.height)) || (same(p.h, page.width) && same(p.w, page.height))) return i;
        }
        return 0;
    }

    // A segmented switch as wide as the form's fields, its segments equal.
    std::shared_ptr<UltraCanvasSegmentedControl> Switch(const std::string& id, const std::vector<std::string>& items, int selected) {
        auto sw = CreateSegmentedControl(id, 0, 0, ArtCreatorDialogParts::kFieldW, ArtCreatorDialogParts::kFieldH);
        SegmentedControlStyle st = sw->GetStyle();
        st.cornerRadius = 5.0f;
        st.paddingHorizontal = 6;
        st.paddingVertical = 4;
        sw->SetStyle(st);
        sw->SetWidthMode(SegmentWidthMode::Equal);
        for (const auto& item : items) sw->AddSegment(item);
        sw->SetSelectedIndex(selected);
        return sw;
    }
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
    config_.width = 400; config_.height = 262;
    config_.minWidth = 380; config_.minHeight = 262;
    config_.resizable = true;
    config_.deleteOnClose = true;
    StyleDialog(*this, config_);

    std::shared_ptr<UltraCanvasContainer> form;
    AddChild(FormCard("acp-card", "Page", form));

    presetDrop = CreateDropdown("acp-preset", 0, 0, kFieldW, kFieldH);
    for (const auto& p : kPresets) presetDrop->AddItem(p.name);
    presetDrop->SetSelectedIndex(MatchingPreset(initial), false);
    FormRow(form, "acp-preset-row", "Preset:", presetDrop);

    // Width x height on one line, each field half the column.
    auto sizeRow = std::make_shared<UltraCanvasContainer>("acp-size", 0, 0, kFieldW, kFieldH);
    sizeRow->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    const float spinW = (kFieldW - 22.0f) / 2.0f;
    widthSpin = CreateDecimalSpinner("acp-w", 0, 0, spinW, kFieldH, 1.0, 100000.0, initial.width, 1.0, 2);
    widthSpin->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    sizeRow->AddChild(widthSpin);
    auto times = CreateLabel("acp-x", 0, 0, 10, kFieldH, "x");
    times->SetAlignment(TextAlignment::Center);
    times->SetTextColor(Colors::SectionCardCaption);
    times->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    sizeRow->AddChild(times);
    heightSpin = CreateDecimalSpinner("acp-h", 0, 0, spinW, kFieldH, 1.0, 100000.0, initial.height, 1.0, 2);
    heightSpin->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    sizeRow->AddChild(heightSpin);
    FormRow(form, "acp-size-row", "Size:", sizeRow);

    unitSwitch = Switch("acp-unit", { kUnitNames[0], kUnitNames[1], kUnitNames[2], kUnitNames[3], kUnitNames[4] },
                        std::clamp(initial.unit, 0, 4));
    FormRow(form, "acp-unit-row", "Unit:", unitSwitch);

    orientationSwitch = Switch("acp-orient", { "Portrait", "Landscape" }, initial.landscape ? 1 : 0);
    FormRow(form, "acp-orient-row", "Orientation:", orientationSwitch);

    presetDrop->onSelectionChanged = [this](int i, const DropdownItem&) { ApplyPreset(i); };
    orientationSwitch->onSegmentSelected = [this](int) { SyncOrientation(); };
    unitSwitch->onSegmentSelected = [this](int i) {
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

    AddStretchSpacer();
    AddChild(ButtonRow("acp-buttons", this, [this]() {
        ArtCreatorPageResult r;
        r.width = widthSpin->GetValue();
        r.height = heightSpin->GetValue();
        r.unit = unitSwitch->GetSelectedIndex();
        r.landscape = orientationSwitch->GetSelectedIndex() == 1;
        if (onAccept) onAccept(r);
    }, setup ? "Apply" : "Create"));
}

void ArtCreatorPageDialog::ApplyPreset(int index) {
    if (index <= 0 || index >= static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]))) return;
    syncing = true;
    const PagePreset& p = kPresets[index];
    unitSwitch->SetSelectedIndex(p.unit);
    lastUnit = p.unit;
    const bool landscape = orientationSwitch->GetSelectedIndex() == 1;
    widthSpin->SetValue(landscape ? std::max(p.w, p.h) : std::min(p.w, p.h));
    heightSpin->SetValue(landscape ? std::min(p.w, p.h) : std::max(p.w, p.h));
    if (p.w == p.h) { widthSpin->SetValue(p.w); heightSpin->SetValue(p.h); }
    syncing = false;
}

void ArtCreatorPageDialog::SyncOrientation() {
    if (syncing) return;
    syncing = true;
    const double w = widthSpin->GetValue(), h = heightSpin->GetValue();
    const bool landscape = orientationSwitch->GetSelectedIndex() == 1;
    if ((landscape && h > w) || (!landscape && w > h)) { widthSpin->SetValue(h); heightSpin->SetValue(w); }
    syncing = false;
}

// ===========================================================================
// TEXT
// ===========================================================================

ArtCreatorTextDialog::ArtCreatorTextDialog(const ArtCreatorTextResult& initial) : UltraCanvasWindow() {
    config_.title = "Text";
    config_.width = 400; config_.height = 312;
    config_.minWidth = 380; config_.minHeight = 312;
    config_.resizable = true;
    config_.deleteOnClose = true;
    StyleDialog(*this, config_);

    std::shared_ptr<UltraCanvasContainer> form;
    AddChild(FormCard("act-card", "Text", form));

    textInput = CreateTextInput("act-text", 0, 0, kFieldW, kFieldH);
    textInput->SetText(initial.text);
    textInput->SetPlaceholder("Type the text to place");
    FormRow(form, "act-text-row", "Text:", textInput);

    fontDrop = CreateDropdown("act-font", 0, 0, kFieldW, kFieldH);
    for (const char* f : { "Sans", "Serif", "Monospace" }) fontDrop->AddItem(f);
    fontDrop->SetSelectedIndex(initial.font == "Serif" ? 1 : initial.font == "Monospace" ? 2 : 0, false);
    FormRow(form, "act-font-row", "Font:", fontDrop);

    sizeSpin = CreateIntSpinner("act-size", 0, 0, 110, kFieldH, 4, 500, initial.size, 1);
    sizeSpin->SetSuffix(" pt");
    FormRow(form, "act-size-row", "Size:", sizeSpin);
    // A size needs no more than a few digits: not stretched to the column.
    sizeSpin->layoutItem.SetFlexGrow(0).SetAlignSelf(CSSLayout::AlignSelf::Start);

    // Bold and Italic side by side, under the fields rather than each on a
    // row of its own.
    auto styleRow = std::make_shared<UltraCanvasContainer>("act-style", 0, 0, kFieldW, kFieldH);
    styleRow->layout.SetFlexRow().SetFlexGap(18).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    boldBox = std::make_shared<UltraCanvasCheckbox>("act-bold", 0, 0, 70, kFieldH, "Bold");
    boldBox->SetChecked(initial.bold);
    styleRow->AddChild(boldBox);
    italicBox = std::make_shared<UltraCanvasCheckbox>("act-italic", 0, 0, 70, kFieldH, "Italic");
    italicBox->SetChecked(initial.italic);
    styleRow->AddChild(italicBox);
    FormRow(form, "act-style-row", "Style:", styleRow);

    // The text as it will look, up to a size that fits the card.
    preview = CreateLabel("act-preview", 0, 0, 0, 44, "");
    preview->SetAlignment(TextAlignment::Center);
    preview->SetBackgroundColor(Colors::SectionPage);
    preview->SetBorders(1.0f, Colors::SectionCardBorder);
    preview->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    FormWideRow(form, preview);
    textInput->onTextChanged = [this](const std::string&) { UpdatePreview(); };
    fontDrop->onSelectionChanged = [this](int, const DropdownItem&) { UpdatePreview(); };
    sizeSpin->onValueChanged = [this](double) { UpdatePreview(); };
    sizeSpin->onValueChanging = [this](double) { UpdatePreview(); };
    boldBox->onStateChanged = [this](CheckedState, CheckedState) { UpdatePreview(); };
    italicBox->onStateChanged = [this](CheckedState, CheckedState) { UpdatePreview(); };
    UpdatePreview();

    AddStretchSpacer();
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

void ArtCreatorTextDialog::UpdatePreview() {
    if (!preview) return;
    LabelStyle st = preview->GetStyle();
    const int f = fontDrop->GetSelectedIndex();
    st.fontStyle.fontFamily = f == 1 ? "Serif" : f == 2 ? "Monospace" : "Sans";
    st.fontStyle.fontSize = std::clamp(sizeSpin->GetValue(), 8.0, 26.0);
    st.fontStyle.fontWeight = boldBox->IsChecked() ? FontWeight::Bold : FontWeight::Normal;
    st.fontStyle.fontSlant = italicBox->IsChecked() ? FontSlant::Italic : FontSlant::Normal;
    const std::string text = textInput->GetText();
    st.textColor = text.empty() ? Colors::SectionCardCaption : Colors::TextDefault;
    preview->SetStyle(st);
    preview->SetText(text.empty() ? "Preview" : text);
}

} // namespace UltraCanvas
