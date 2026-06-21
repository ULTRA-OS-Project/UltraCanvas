// dialogs/UltraCanvasMemoryStatsDialog.cpp
// Implementation of the memory-usage "task manager" dialog.
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework
#include "UltraCanvasMemoryStatsDialog.h"

namespace UltraCanvas {

    void UltraCanvasMemoryStatsDialog::CreateMemoryStatsDialog(const MemoryStatsDialogConfig& config) {
        // Build the standard dialog window (content + footer).
        CreateDialog(config);

        // This is a data panel, not a message: hide the icon and message labels
        // the base content section creates.
        if (iconContainer) iconContainer->SetVisible(false);
        if (messageLabel)  messageLabel->SetVisible(false);
        if (detailsLabel)  detailsLabel->SetVisible(false);

        // The stats panel is the content; let it grow to fill the dialog body.
        panel = CreateMemoryStatsPanel("MemStatsPanel", 0, 0, 0, 0);
        panel->SetShowTypeBreakdown(config.showTypeBreakdown);
        panel->layoutItem.SetFlexGrow(1);
        AddDialogElement(panel);

        // "Refresh" re-snapshots WITHOUT closing the dialog (AddCustomButton
        // always closes, so we wire this one by hand and add it first).
        refreshButton = std::make_shared<UltraCanvasButton>(
                "MemStatsRefresh", 0, 0,
                static_cast<long>(style.buttonWidth), static_cast<long>(style.buttonHeight));
        refreshButton->SetText("Refresh");
        refreshButton->onClick = [this]() { RefreshNow(); };
        dialogButtons.push_back(refreshButton);
        if (footerSection) footerSection->AddChild(refreshButton);

        // "Close" dismisses the dialog.
        AddCustomButton("Close", DialogResult::Close);
    }

    void UltraCanvasMemoryStatsDialog::RefreshNow() {
        if (panel) panel->RefreshNow();
        RequestRedraw();
    }

    void UltraCanvasMemoryStatsDialog::SetShowTypeBreakdown(bool show) {
        if (panel) panel->SetShowTypeBreakdown(show);
    }

    std::shared_ptr<UltraCanvasMemoryStatsDialog> UltraCanvasMemoryStatsDialog::Show(
            UltraCanvasWindowBase* parent, const MemoryStatsDialogConfig& config) {
        auto dialog = std::make_shared<UltraCanvasMemoryStatsDialog>();
        dialog->CreateMemoryStatsDialog(config);
        UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
        return dialog;
    }

} // namespace UltraCanvas
