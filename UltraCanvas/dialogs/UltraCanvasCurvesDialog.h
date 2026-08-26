// dialogs/UltraCanvasCurvesDialog.h
// The "Curves" box of an image editor: a channel selector, the interactive
// curve grid (UltraCanvasCurveEditor) and the buttons that apply, reset or
// discard the edit.
//
// The dialog owns no pixels. It edits a ToneCurveSet and hands it back through
// onCurvesChanged() while the user drags, so the host previews the change on
// the image it is already showing (full size, not a thumbnail); onAccept() /
// onCancel() end the edit. That keeps the dialog usable by anything that can
// map an image through a lookup table — the media viewer's Curves button is
// the first caller.
//
// Version: 1.0.0
// Last Modified: 2026-08-25
// Author: UltraCanvas Framework
#pragma once

#include "../include/UltraCanvasWindow.h"
#include "../include/UltraCanvasContainer.h"
#include "../include/UltraCanvasCurveEditor.h"
#include "../include/UltraCanvasButton.h"
#include "../include/UltraCanvasCheckbox.h"
#include "../include/UltraCanvasDropdown.h"
#include "../include/UltraCanvasLabel.h"
#include <functional>
#include <memory>
#include <vector>

namespace UltraCanvas {

class UltraCanvasCurvesDialog : public UltraCanvasWindow {
public:
    UltraCanvasCurvesDialog();
    explicit UltraCanvasCurvesDialog(const ToneCurveSet& initial);
    ~UltraCanvasCurvesDialog() override = default;

    // ===== CURVES =====
    void SetCurves(const ToneCurveSet& set);
    const ToneCurveSet& GetCurves() const;

    // ===== HISTOGRAM BACKDROP =====
    // Bin counts per 8-bit level for one channel of the image being edited.
    void SetHistogram(ToneCurveChannel channel, const std::vector<uint32_t>& bins);

    UltraCanvasCurveEditor* GetEditor() const { return editor.get(); }

    // ===== CALLBACKS =====
    // Live during editing — only while the Preview box is ticked.
    std::function<void(const ToneCurveSet&)> onCurvesChanged;
    // OK: the final curves. The dialog closes itself afterwards.
    std::function<void(const ToneCurveSet&)> onAccept;
    // Cancel / window close: the host restores what it had before.
    std::function<void()> onCancel;

private:
    void BuildLayout();
    void UpdateReadout();
    void EmitPreview();

    std::shared_ptr<UltraCanvasContainer>   headerRow;
    std::shared_ptr<UltraCanvasLabel>       channelLabel;
    std::shared_ptr<UltraCanvasDropdown>    channelDropdown;
    std::shared_ptr<UltraCanvasCheckbox>    previewCheckbox;
    std::shared_ptr<UltraCanvasCurveEditor> editor;
    std::shared_ptr<UltraCanvasContainer>   readoutRow;
    std::shared_ptr<UltraCanvasLabel>       readoutLabel;
    std::shared_ptr<UltraCanvasContainer>   buttonRow;
    std::shared_ptr<UltraCanvasButton>      resetChannelButton;
    std::shared_ptr<UltraCanvasButton>      resetAllButton;
    std::shared_ptr<UltraCanvasButton>      okButton;
    std::shared_ptr<UltraCanvasButton>      cancelButton;

    bool previewEnabled = true;
    bool accepted = false;   // OK pressed — the close handler must not cancel
};

// ===== FACTORY =====
inline std::shared_ptr<UltraCanvasCurvesDialog> CreateCurvesDialog() {
    return std::make_shared<UltraCanvasCurvesDialog>();
}
inline std::shared_ptr<UltraCanvasCurvesDialog> CreateCurvesDialog(const ToneCurveSet& initial) {
    return std::make_shared<UltraCanvasCurvesDialog>(initial);
}

} // namespace UltraCanvas
