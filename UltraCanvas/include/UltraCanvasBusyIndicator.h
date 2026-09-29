// include/UltraCanvasBusyIndicator.h
// Busy indicator: the turning ring that says "working on it" when there is no
// percentage to show — a mail fetch, a network call, a folder scan. A partial
// arc turns over a faint full track while Start() is in effect; Stop() halts
// the timer and (by default) leaves the space blank, so the indicator can sit
// permanently in a status line and cost nothing while idle.
//
// For a known fraction use UltraCanvasGaugeDiagramElement (GaugeMode::LinearBar)
// or UltraCanvasProgressDialog; this element has no value.
//
// Version: 1.0.0
// Last Modified: 2026-09-28
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTimer.h"
#include "UltraCanvasCommonTypes.h"
#include <chrono>
#include <memory>
#include <string>

namespace UltraCanvas {

// ===== BUSY INDICATOR STYLE =====
    struct BusyIndicatorStyle {
        Color arcColor   = Color(0, 120, 215, 255);   // the turning arc
        Color trackColor = Color(0, 0, 0, 28);        // the faint full ring behind it; alpha 0 = none
        float thickness  = 0.0f;       // stroke width in px; <= 0 means side / 8
        float arcDegrees = 270.0f;     // length of the turning arc
        float revolutionsPerSecond = 1.0f;
        unsigned int frameIntervalMs = 33;   // ~30 fps
        bool  hideWhenStopped = true;  // false draws the idle track (and a still arc)
    };

// ===== BUSY INDICATOR COMPONENT =====
    class UltraCanvasBusyIndicator : public UltraCanvasUIElement {
    public:
        UltraCanvasBusyIndicator(const std::string& identifier, float x, float y, float w, float h);
        ~UltraCanvasBusyIndicator() override;

        // Start/Stop are idempotent: calling Start() on a running indicator
        // keeps it turning without a jump, Stop() on a stopped one is a no-op.
        void Start();
        void Stop();
        void SetRunning(bool running) { running ? Start() : Stop(); }
        bool IsRunning() const { return timerId != InvalidTimerId; }

        BusyIndicatorStyle& GetStyle() { return style; }
        const BusyIndicatorStyle& GetStyle() const { return style; }
        void SetStyle(const BusyIndicatorStyle& s);

        bool AcceptsFocus() const override { return false; }
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

    private:
        BusyIndicatorStyle style;
        TimerId timerId = InvalidTimerId;
        std::chrono::steady_clock::time_point startedAt{};
        double stoppedAngle = 0.0;   // radians; where the arc rests after Stop()

        double CurrentAngle() const;
    };

// ===== FACTORY =====
    // A square indicator `size` px on a side.
    inline std::shared_ptr<UltraCanvasBusyIndicator> CreateBusyIndicator(
            const std::string& identifier, float x, float y, float size = 16.0f) {
        return std::make_shared<UltraCanvasBusyIndicator>(identifier, x, y, size, size);
    }

} // namespace UltraCanvas
