// dialogs/UltraCanvasMemoryStatsDialog.h
// "Task manager" dialog that shows UltraCanvas memory usage: one row per scope
// (e.g. per browser tab) with its total and a usage bar, plus a per-category
// breakdown of what the memory is used for. Built on the modal-dialog framework
// and embeds UltraCanvasMemoryStatsPanel as its content.
//
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework
#pragma once

#include "../include/UltraCanvasModalDialog.h"
#include "../include/UltraCanvasMemoryStatsPanel.h"
#include "../include/UltraCanvasButton.h"
#include <memory>

namespace UltraCanvas {

// ===== DIALOG CONFIGURATION =====
    struct MemoryStatsDialogConfig : public DialogConfig {
        // Also list the concrete C++ types under each scope, not just categories.
        bool showTypeBreakdown = false;

        MemoryStatsDialogConfig() {
            title = "Memory Usage";
            width = 380;
            height = 520;
            // Footer buttons (Refresh + Close) are added by the dialog itself,
            // since the base only knows OK/Cancel/Yes/No/Retry/Abort/Ignore.
            buttons = DialogButtons::NoButtons;
            dialogType = DialogType::Custom; // no info/warning icon
            resizable = true;
        }
    };

// ===== MEMORY STATS DIALOG =====
    class UltraCanvasMemoryStatsDialog : public UltraCanvasModalDialog {
    public:
        // Build the dialog window and its content. Call once after construction.
        void CreateMemoryStatsDialog(const MemoryStatsDialogConfig& config = MemoryStatsDialogConfig());

        // Re-snapshot and repaint now (also wired to the footer "Refresh" button).
        void RefreshNow();

        void SetShowTypeBreakdown(bool show);

        // Convenience: construct, build, and show in one call.
        static std::shared_ptr<UltraCanvasMemoryStatsDialog> Show(
                UltraCanvasWindowBase* parent = nullptr,
                const MemoryStatsDialogConfig& config = MemoryStatsDialogConfig());

    private:
        std::shared_ptr<UltraCanvasMemoryStatsPanel> panel;
        std::shared_ptr<UltraCanvasButton>           refreshButton;
    };

// ===== FACTORY HELPER =====
    inline std::shared_ptr<UltraCanvasMemoryStatsDialog> CreateMemoryStatsDialog(
            const MemoryStatsDialogConfig& config = MemoryStatsDialogConfig()) {
        auto dialog = std::make_shared<UltraCanvasMemoryStatsDialog>();
        dialog->CreateMemoryStatsDialog(config);
        return dialog;
    }

} // namespace UltraCanvas
