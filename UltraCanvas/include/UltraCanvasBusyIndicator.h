// include/UltraCanvasBusyIndicator.h
// Busy indicator: the animation that says "working on it" when there is no
// percentage to show — a mail fetch, a network call, a folder scan. Five
// kinds: a partial arc turning over a faint track (Ring), two concentric arcs
// turning against each other (DualRing), dots swelling in
// turn (Dots), a segment sliding along a track (Bar) and a circle that
// breathes in and out (Pulse). Start() runs it; Stop() halts the timer and
// (by default) leaves the space blank, so the indicator can sit permanently
// in a status line and cost nothing while idle.
//
// For a known fraction use UltraCanvasGaugeDiagramElement (GaugeMode::LinearBar)
// or UltraCanvasProgressDialog; this element has no value.
//
// Version: 1.1.0
// Last Modified: 2026-10-01
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

// ===== BUSY INDICATOR KIND =====
    enum class BusyIndicatorKind {
        Ring,    // a partial arc turning over a faint full ring (square element)
        DualRing,// two concentric arcs turning in opposite directions (square element)
        Dots,    // dotCount dots in a row, swelling and brightening in turn
        Bar,     // a segment sliding left to right along a track (wide element)
        Pulse    // a filled circle breathing in and out over a faint disc
    };

// ===== BUSY INDICATOR STYLE =====
    struct BusyIndicatorStyle {
        BusyIndicatorKind kind = BusyIndicatorKind::Ring;
        Color arcColor   = Color(0, 120, 215, 255);   // the moving part: arc, dots, segment, circle
        Color trackColor = Color(0, 0, 0, 28);        // the faint still part behind it; alpha 0 = none
        float thickness  = 0.0f;       // Ring/DualRing: stroke width, <= 0 means side / 8;
                                       // Bar: bar height, <= 0 means the element height
        float arcDegrees = 270.0f;     // Ring: length of the turning arc; DualRing: each
                                       // ring's arc is half of this
        int   dotCount   = 3;          // Dots: how many, clamped to 2..12
        float barFraction = 0.3f;      // Bar: segment length as a share of the width, 0.05..0.9
        float revolutionsPerSecond = 1.0f;   // animation cycles per second, every kind
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
        double stoppedPhase = 0.0;   // cycles, 0..1; where the animation rests after Stop()

        double CurrentPhase() const;   // 0..1 through the current cycle
        void RenderRing(IRenderContext* ctx, float w, float h, double phase);
        void RenderDualRing(IRenderContext* ctx, float w, float h, double phase);
        void RenderDots(IRenderContext* ctx, float w, float h, double phase);
        void RenderBar(IRenderContext* ctx, float w, float h, double phase);
        void RenderPulse(IRenderContext* ctx, float w, float h, double phase);
    };

// ===== FACTORY =====
    // A square indicator `size` px on a side (a Ring by default).
    inline std::shared_ptr<UltraCanvasBusyIndicator> CreateBusyIndicator(
            const std::string& identifier, float x, float y, float size = 16.0f) {
        return std::make_shared<UltraCanvasBusyIndicator>(identifier, x, y, size, size);
    }

    // An indicator of the given kind and size. Dots and Bar want a wide box
    // (e.g. 36 x 10 for Dots, 120 x 4 for Bar); Ring, DualRing and Pulse a
    // square one.
    inline std::shared_ptr<UltraCanvasBusyIndicator> CreateBusyIndicator(
            const std::string& identifier, float x, float y, float w, float h,
            BusyIndicatorKind kind) {
        auto indicator = std::make_shared<UltraCanvasBusyIndicator>(identifier, x, y, w, h);
        BusyIndicatorStyle style = indicator->GetStyle();
        style.kind = kind;
        indicator->SetStyle(style);
        return indicator;
    }

} // namespace UltraCanvas
