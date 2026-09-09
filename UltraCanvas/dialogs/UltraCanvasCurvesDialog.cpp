// dialogs/UltraCanvasCurvesDialog.cpp
// Implementation of the Curves dialog. See UltraCanvasCurvesDialog.h.
// Version: 1.0.0
// Last Modified: 2026-08-25
// Author: UltraCanvas Framework

#include "UltraCanvasCurvesDialog.h"
#include "../include/UltraCanvasSpacer.h"
#include "CSSLayout/CSSLayout.h"
#include <string>

namespace UltraCanvas {

namespace {
    constexpr float kSpacing = 10.0f;
    constexpr float kRowHeight = 28.0f;

    ToneCurveChannel ChannelFromIndex(int index) {
        switch (index) {
            case 1:  return ToneCurveChannel::Red;
            case 2:  return ToneCurveChannel::Green;
            case 3:  return ToneCurveChannel::Blue;
            default: return ToneCurveChannel::RGB;
        }
    }
}

UltraCanvasCurvesDialog::UltraCanvasCurvesDialog()
    : UltraCanvasWindow() {
    config_.width = 480;
    config_.height = 560;
    config_.minWidth = 400;
    config_.minHeight = 440;
    config_.deleteOnClose = true;
    config_.title = "Curves";

    SetPadding(12);
    BuildLayout();

    // Closing through the window frame counts as cancelling the edit.
    onWindowClosed = [this]() {
        if (!accepted && onCancel) onCancel();
    };
}

UltraCanvasCurvesDialog::UltraCanvasCurvesDialog(const ToneCurveSet& initial)
    : UltraCanvasCurvesDialog() {
    SetCurves(initial);
}

void UltraCanvasCurvesDialog::SetCurves(const ToneCurveSet& set) {
    if (editor) editor->SetCurves(set);
    UpdateReadout();
}

const ToneCurveSet& UltraCanvasCurvesDialog::GetCurves() const {
    static ToneCurveSet empty;
    return editor ? editor->GetCurves() : empty;
}

void UltraCanvasCurvesDialog::SetHistogram(ToneCurveChannel channel,
                                           const std::vector<uint32_t>& bins) {
    if (editor) editor->SetHistogram(channel, bins);
}

void UltraCanvasCurvesDialog::BuildLayout() {
    layout.SetFlexColumn().SetFlexGap(static_cast<int>(kSpacing))
          .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // ----- CHANNEL SELECTOR + PREVIEW TOGGLE -----
    headerRow = std::make_shared<UltraCanvasContainer>("CurvesHeader", 0, 0, 0, kRowHeight);
    headerRow->layout.SetFlexRow().SetFlexGap(static_cast<int>(kSpacing))
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    headerRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    channelLabel = std::make_shared<UltraCanvasLabel>("CurvesChannelLabel", 0, 0, 80, kRowHeight, "Channel:");
    channelLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    headerRow->AddChild(channelLabel);

    channelDropdown = std::make_shared<UltraCanvasDropdown>("CurvesChannel", 0, 0, 130, kRowHeight);
    channelDropdown->AddItem("RGB");
    channelDropdown->AddItem("Red");
    channelDropdown->AddItem("Green");
    channelDropdown->AddItem("Blue");
    channelDropdown->SetSelectedIndex(0, false);
    channelDropdown->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (editor) editor->SetActiveChannel(ChannelFromIndex(index));
        UpdateReadout();
    };
    channelDropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    headerRow->AddChild(channelDropdown);

    previewCheckbox = std::make_shared<UltraCanvasCheckbox>("CurvesPreview", 0, 0, 100, kRowHeight, "Preview");
    previewCheckbox->SetChecked(true);
    previewCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
        previewEnabled = (newState == CheckedState::Checked);
        // Turning the preview back on re-sends the current curves; turning it
        // off sends the identity so the host shows the untouched image again.
        if (onCurvesChanged) {
            if (previewEnabled && editor) onCurvesChanged(editor->GetCurves());
            else onCurvesChanged(ToneCurveSet());
        }
    };
    previewCheckbox->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    headerRow->AddChild(previewCheckbox);
    AddChild(headerRow);

    // ----- THE CURVE GRID -----
    editor = std::make_shared<UltraCanvasCurveEditor>("CurvesEditor", 0, 0, 0, 0);
    editor->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    editor->onCurveChanged = [this](const ToneCurveSet&) {
        UpdateReadout();
        EmitPreview();
    };
    editor->onSelectionChanged = [this](int) { UpdateReadout(); };
    AddChild(editor);

    // ----- SELECTED POINT READOUT -----
    readoutRow = std::make_shared<UltraCanvasContainer>("CurvesReadout", 0, 0, 0, 22);
    readoutRow->layout.SetFlexRow().SetFlexGap(static_cast<int>(kSpacing))
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    readoutRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    readoutLabel = std::make_shared<UltraCanvasLabel>("CurvesReadoutLabel", 0, 0, 0, 22, "");
    readoutLabel->SetFontSize(11);
    readoutLabel->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    readoutRow->AddChild(readoutLabel);
    AddChild(readoutRow);

    // ----- BUTTONS -----
    buttonRow = std::make_shared<UltraCanvasContainer>("CurvesButtons", 0, 0, 0, 34);
    buttonRow->layout.SetFlexRow().SetFlexGap(static_cast<int>(kSpacing))
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    resetChannelButton = std::make_shared<UltraCanvasButton>("CurvesResetChannel", 0, 0, 124, 30, "Reset channel");
    resetChannelButton->onClick = [this]() {
        if (editor) editor->ResetActiveChannel();
        UpdateReadout();
    };
    resetChannelButton->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    buttonRow->AddChild(resetChannelButton);

    resetAllButton = std::make_shared<UltraCanvasButton>("CurvesResetAll", 0, 0, 96, 30, "Reset all");
    resetAllButton->onClick = [this]() {
        if (editor) editor->ResetAllChannels();
        UpdateReadout();
    };
    resetAllButton->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    buttonRow->AddChild(resetAllButton);

    // Pushes OK / Cancel to the right edge.
    buttonRow->AddChild(std::make_shared<UltraCanvasSpacer>(0.0f, 0.0f, 1.0f));

    okButton = std::make_shared<UltraCanvasButton>("CurvesOK", 0, 0, 80, 30, "OK");
    okButton->onClick = [this]() {
        accepted = true;
        if (onAccept && editor) onAccept(editor->GetCurves());
        PerformClose();
    };
    okButton->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    buttonRow->AddChild(okButton);

    cancelButton = std::make_shared<UltraCanvasButton>("CurvesCancel", 0, 0, 80, 30, "Cancel");
    cancelButton->onClick = [this]() {
        accepted = true;              // the close handler must not cancel twice
        if (onCancel) onCancel();
        PerformClose();
    };
    cancelButton->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    buttonRow->AddChild(cancelButton);
    AddChild(buttonRow);

    UpdateReadout();
}

void UltraCanvasCurvesDialog::UpdateReadout() {
    if (!readoutLabel || !editor) return;
    int in = -1, out = -1;
    editor->GetSelectedPointValues(in, out);
    if (in < 0) {
        readoutLabel->SetText("Click to add a point \xE2\x80\x94 right-click removes one");
    } else {
        readoutLabel->SetText("Input " + std::to_string(in) +
                              "  \xE2\x86\x92  Output " + std::to_string(out));
    }
}

void UltraCanvasCurvesDialog::EmitPreview() {
    if (!previewEnabled || !onCurvesChanged || !editor) return;
    onCurvesChanged(editor->GetCurves());
}

} // namespace UltraCanvas
