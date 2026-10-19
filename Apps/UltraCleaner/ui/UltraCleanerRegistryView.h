// Apps/UltraCleaner/ui/UltraCleanerRegistryView.h
// The Windows registry page: what the registry scan found, grouped by kind,
// one row per entry naming the key, the value and the file that is gone.
//
// The page holds its own copy of the report and edits the ticks on it, the
// way the System junk page edits its items: double-clicking a row keeps or
// drops it, and the two shortcut buttons go back to the defaults or to
// nothing. Scanning and cleaning block on the registry, so the window runs
// them on its worker thread and hands the results back here.
//
// The page says, before anything else, what it does not do: it is not a
// "registry optimiser", it never makes Windows faster, and every removal is
// backed up first. Promising more would be the dishonesty those products
// are known for.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCleanerRegistry.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasColumnsTreeView.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCleaner {

class RegistryView {
public:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build(float width,
                                                             float height);

    // Replaces the list; ticks start at each kind's default.
    void SetReport(RegistryScanReport report);
    const RegistryScanReport& Report() const { return report_; }
    bool Scanned() const { return scanned_; }

    void SetStatus(const std::string& text);
    void SetBusy(bool busy);

    std::function<void()> onScan;
    std::function<void()> onStop;
    std::function<void()> onClean;
    std::function<void()> onRestore;

private:
    void RefreshList();
    void RefreshRow(size_t index);
    void RefreshSummary();
    void Toggle(size_t index);
    void SelectDefaults();
    void SelectNone();

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasColumnsTreeView> tree_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> summaryLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> statusLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> scanButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> stopButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> cleanButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> restoreButton_;

    RegistryScanReport report_;
    bool scanned_ = false;
};

} // namespace UltraCleaner
