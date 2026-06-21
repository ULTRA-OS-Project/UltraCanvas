// include/UltraCanvasMemoryStatsPanel.h
// Ready-made "task manager" widget that visualizes UltraCanvas memory usage.
// Renders one row per scope (e.g. per browser tab) with its total and a usage
// bar, plus an indented per-category breakdown showing WHAT the memory is used
// for. Data comes from the public UltraCanvas::MemoryStats:: API, so the panel
// works in any UltraCanvas application without extra wiring.
//
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasMemoryStats.h"
#include <string>

namespace UltraCanvas {

// ===== MEMORY STATS PANEL =====
    class UltraCanvasMemoryStatsPanel : public UltraCanvasUIElement {
    public:
        UltraCanvasMemoryStatsPanel(const std::string& id, float x, float y, float w, float h);

        // Re-pull the snapshot on the next Render even if nothing changed.
        void RefreshNow() { forceRefresh = true; RequestRedraw(); }

        // Show the concrete-C++-type breakdown under each scope in addition to
        // the category breakdown (off by default to keep the panel compact).
        void SetShowTypeBreakdown(bool show) { showTypes = show; RequestRedraw(); }
        bool GetShowTypeBreakdown() const { return showTypes; }

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

    private:
        // Re-snapshot if the tracker's generation advanced since last draw.
        void RefreshIfStale();

        MemoryReport cached;
        uint64_t     lastGeneration = static_cast<uint64_t>(-1);
        bool         forceRefresh   = true;
        bool         showTypes      = false;

        float rowHeight = 20.0f;
    };

// ===== FACTORY HELPER (matches the framework's CreateXxx convention) =====
    inline std::shared_ptr<UltraCanvasMemoryStatsPanel> CreateMemoryStatsPanel(
            const std::string& id, float x, float y, float w, float h) {
        return UltraCanvasUIElementFactory::Create<UltraCanvasMemoryStatsPanel>(id, x, y, w, h);
    }

} // namespace UltraCanvas
