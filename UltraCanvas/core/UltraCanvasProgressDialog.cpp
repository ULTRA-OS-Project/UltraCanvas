// core/UltraCanvasProgressDialog.cpp
// Implementation of the modal progress window.
// See UltraCanvasProgressDialog.h for the contract.
// Version: 1.0.0
// Last Modified: 2026-08-23
// Author: UltraCanvas Framework

#include "UltraCanvasProgressDialog.h"

#include "UltraCanvasModalDialog.h"
#include "Plugins/Charts/UltraCanvasCircularProgressChart.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    namespace {
        constexpr int kRingSize = 132;   // px, square

        // The centre reading. A negative fraction means the total is unknown,
        // so there is no honest percentage to show.
        std::string PercentText(double fraction) {
            if (fraction < 0.0) return "...";
            const int pct = static_cast<int>(std::lround(
                    std::clamp(fraction, 0.0, 1.0) * 100.0));
            return std::to_string(pct) + "%";
        }

        // Keep the detail line to one line's worth of characters: the dialog is
        // sized once, when it opens, and a longer name would wrap and push the
        // ring out of the window instead of growing it.
        std::string Ellipsize(const std::string& text, size_t maxChars) {
            if (text.size() <= maxChars) return text;
            if (maxChars <= 3) return text.substr(0, maxChars);
            // Keep the tail: a file name is more useful than its folder.
            return "..." + text.substr(text.size() - (maxChars - 3));
        }
    }

    std::shared_ptr<UltraCanvasProgressDialog> UltraCanvasProgressDialog::Show(
            UltraCanvasWindowBase* parent,
            const std::string& title,
            const std::string& caption,
            std::function<void()> onCancel) {

        DialogConfig cfg;
        cfg.title = title;
        cfg.dialogType = DialogType::Information;
        cfg.message = caption;
        cfg.details = " ";           // reserved for the detail line
        cfg.buttons = DialogButtons::NoButtons;   // Cancel is added below
        cfg.width = 460;
        cfg.height = 320;
        cfg.closeOnEscape = true;

        auto modal = UltraCanvasDialogManager::CreateDialog(cfg);
        if (!modal) return nullptr;   // dialogs unavailable — caller runs bare

        auto self = std::shared_ptr<UltraCanvasProgressDialog>(
                new UltraCanvasProgressDialog());
        self->dialog = modal;
        self->caption = caption;
        self->onCancel = std::move(onCancel);

        // The ring: one arc over a grey track, the percentage in the middle.
        // Explicit size — content measuring needs a render context, which the
        // dialog does not have while it is first laid out.
        self->ring = CreateCircularProgressChart(
                "ProgressRing", 0, 0, kRingSize, kRingSize);
        self->ring->SetSubStyle(CircularProgressStyle::SingleRing);
        self->ring->AddRing("", 0.0, Color(60, 140, 220, 255));
        self->ring->SetInnerRadiusFraction(0.72f);
        self->ring->SetTrackMode(RingTrackMode::Explicit);
        self->ring->SetTrackColor(Color(232, 232, 238, 255));
        self->ring->SetCapStyle(RingCapStyle::Round);
        self->ring->SetTipLabelContent(RingTipLabel::NoLabel);
        self->ring->SetRingNameLabels(RingNamePosition::Hidden);
        self->ring->SetLegendStyle(CircularLegendStyle::NoLegend);
        self->ring->SetShowCenterDisc(false);
        self->ring->SetCenterText(PercentText(0.0));
        self->ring->SetCenterFont("Arial", 22.0f, FontWeight::Bold);
        self->ring->SetTooltipsEnabled(false);
        self->ring->SetHoverHighlightEnabled(false);
        self->ring->size.width  = CSSLayout::Dimension::Px(kRingSize);
        self->ring->size.height = CSSLayout::Dimension::Px(kRingSize);
        self->ring->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                              .SetAlignSelf(CSSLayout::AlignSelf::Center);
        modal->AddDialogElement(self->ring);

        modal->AddCustomButton("Cancel", DialogResult::Cancel, nullptr);
        // Every way out of the window - the button, Escape, the title bar - is
        // a cancel, and each of them means the window is already going away.
        std::weak_ptr<UltraCanvasProgressDialog> weak = self;
        modal->onResult = [weak](DialogResult) {
            auto me = weak.lock();
            if (!me || me->cancelled) return;
            me->cancelled = true;
            me->closed = true;          // the dialog closes itself on a result
            if (me->onCancel) me->onCancel();
        };

        UltraCanvasDialogManager::ShowDialog(modal, nullptr, parent);
        return self;
    }

    UltraCanvasProgressDialog::~UltraCanvasProgressDialog() {
        Close();
    }

    void UltraCanvasProgressDialog::SetProgress(double value) {
        const double clamped = value < 0.0 ? -1.0 : std::clamp(value, 0.0, 1.0);
        if (std::abs(clamped - fraction) < 0.0005) return;   // same rounded reading
        fraction = clamped;
        if (!ring) return;
        ring->SetRingValue(0, fraction < 0.0 ? 25.0 : fraction * 100.0);
        ring->SetCenterText(PercentText(fraction));
        ring->RequestRedraw();
    }

    void UltraCanvasProgressDialog::SetCaption(const std::string& text) {
        if (caption == text) return;
        caption = text;
        UpdateText();
    }

    void UltraCanvasProgressDialog::SetDetail(const std::string& text) {
        const std::string trimmed = Ellipsize(text, 56);
        if (detail == trimmed) return;
        detail = trimmed;
        UpdateText();
    }

    void UltraCanvasProgressDialog::UpdateText() {
        if (!dialog || closed) return;
        dialog->SetMessage(caption);
        // A blank line keeps the message area's height stable while there is
        // nothing to name yet.
        dialog->SetDetails(detail.empty() ? " " : detail);
    }

    void UltraCanvasProgressDialog::Close() {
        if (closed) return;
        closed = true;
        auto modal = dialog;
        dialog.reset();
        ring.reset();
        if (!modal) return;
        // Closing from code is not the user cancelling: drop the handler first
        // so the caller is not told its own Close() was a cancel.
        modal->onResult = nullptr;
        modal->CloseDialog(DialogResult::OK);
    }

} // namespace UltraCanvas
