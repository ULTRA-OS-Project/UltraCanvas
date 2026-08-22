// Apps/UltraCleaner/ui/UltraCleanerCategoryPanel.h
// The left-hand list of what a scan found: one row per category, each an
// UltraCanvasCheckbox for "clean this" plus an UltraCanvasBadge with the
// recoverable size. Categories whose removal costs the user something — the
// trash, a Maven repository — arrive unticked. A category shows the
// indeterminate state while only some of its paths are still ticked, but a
// click is always a plain on/off toggle: the third state is something the
// panel paints, never something the user has to click through.
//
// Pure view: it holds no engine state beyond the summaries handed to it and
// tells the window what changed through its callbacks.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCleanerTypes.h"

#include "UltraCanvasBadge.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

#include <functional>
#include <memory>
#include <vector>

namespace UltraCleaner {

class CategoryPanel {
public:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build(float x, float y,
                                                             float width,
                                                             float height);

    // Rebuilds the rows from a scan report. Ticked state comes from the
    // report's own items, so a category the rules left unticked stays that way.
    void SetReport(const ScanReport& report);

    // Reflects item-level changes (a row toggled in the detail table) back
    // into the checkboxes without rebuilding them.
    void RefreshFromReport(const ScanReport& report);

    // Shows "nothing found yet" / scan-in-progress text instead of rows.
    void ShowMessage(const std::string& message);

    // (category, nowSelected) — the window applies it to the report's items.
    std::function<void(CleanCategory, bool)> onCategoryToggled;

private:
    struct Row {
        CleanCategory category = CleanCategory::TemporaryFiles;
        std::shared_ptr<UltraCanvas::UltraCanvasCheckbox> checkbox;
        std::shared_ptr<UltraCanvas::UltraCanvasBadge> sizeBadge;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> detail;
        bool safeByDefault = true;
    };

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::vector<Row> rows_;
    bool suppressCallbacks_ = false;
};

} // namespace UltraCleaner
