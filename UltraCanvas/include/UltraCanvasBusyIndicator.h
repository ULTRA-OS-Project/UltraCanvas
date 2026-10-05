// include/UltraCanvasBusyIndicator.h
// Busy indicator: the animation that says "working on it" when there is no
// percentage to show — a mail fetch, a network call, a folder scan. Six
// kinds: a partial arc turning over a faint track (Ring), two concentric arcs
// in two colours turning against each other (DualRing), dots swelling in
// turn (Dots), a segment sliding along a track (Bar), a circle that
// breathes in and out (Pulse) and a ring of dots with a bright head
// circling round it (DotRing). Start() runs it; Stop() halts the timer and
// (by default) leaves the space blank, so the indicator can sit permanently
// in a status line and cost nothing while idle.
//
// For a known fraction use UltraCanvasGaugeDiagramElement (GaugeMode::LinearBar)
// or UltraCanvasProgressDialog; this element has no value.
//
// Version: 1.2.0
// Last Modified: 2026-10-04
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTimer.h"
#include "UltraCanvasCommonTypes.h"
#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== BUSY INDICATOR KIND =====
    enum class BusyIndicatorKind {
        Ring,    // a partial arc turning over a faint full ring (square element)
        DualRing,// two concentric arcs turning in opposite directions, outer in
                 // arcColor, inner in secondArcColor (square element)
        Dots,    // dotCount dots in a row, swelling and brightening in turn
        Bar,     // a segment sliding left to right along a track (wide element)
        Pulse,   // a filled circle breathing in and out over a faint disc
        DotRing  // ringDotCount dots on a circle, a head running round them
                 // clockwise; dotRingFade picks how the dots behind it look
                 // (square element)
    };

// ===== DOT RING FADE =====
    // How the dots of a DotRing show where the head is. In every mode the dot
    // under the head is full size and the ones behind it shrink.
    enum class BusyDotRingFade {
        NoFade,          // every dot in full arcColor; the head shows by size alone
                         // (not `None`: X11 defines that as a macro)
        Fade,            // dots fade out behind the head and back in just ahead of it
        FadeRandomColor  // as Fade, and each dot fades back in in a new random
                         // colour, different from the one it faded out in
    };

// ===== BUSY INDICATOR STYLE =====
    struct BusyIndicatorStyle {
        BusyIndicatorKind kind = BusyIndicatorKind::Ring;
        Color arcColor   = Color(0, 120, 215, 255);   // the moving part: arc, dots, segment, circle;
                                                      // DualRing: the outer arc
        Color secondArcColor = Color(240, 130, 30, 255); // DualRing: the inner arc
        Color trackColor = Color(0, 0, 0, 28);        // the faint still part behind it; alpha 0 = none
        float thickness  = 0.0f;       // Ring/DualRing: stroke width, <= 0 means side / 8;
                                       // Bar: bar height, <= 0 means the element height;
                                       // DotRing: dot diameter, <= 0 fits the dots to the ring
        float arcDegrees = 270.0f;     // Ring: length of the turning arc; DualRing: each
                                       // ring's arc is half of this
        int   dotCount   = 3;          // Dots: how many, clamped to 2..12
        float barFraction = 0.3f;      // Bar: segment length as a share of the width, 0.05..0.9
        float barLength  = 0.0f;       // Bar: segment length in px; > 0 overrides barFraction
        int   ringDotCount = 8;        // DotRing: how many dots on the ring, clamped to 4..16
        BusyDotRingFade dotRingFade = BusyDotRingFade::Fade;   // DotRing: see BusyDotRingFade
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
        double stoppedCycles = 0.0;  // cycles run so far; where the animation rests after Stop()

        // DotRing in FadeRandomColor: each dot's hue, and the fade-in it was
        // picked for, so a dot changes colour once per fade-in and not per frame.
        // The generator is seeded when the first colour is picked.
        std::vector<float> dotHues;
        std::vector<long long> dotHueCycles;
        std::minstd_rand colorRandom;

        double CurrentCycles() const;  // cycles since the first Start(), counting pauses out
        void RenderRing(IRenderContext* ctx, float w, float h, double phase);
        void RenderDualRing(IRenderContext* ctx, float w, float h, double phase);
        void RenderDots(IRenderContext* ctx, float w, float h, double phase);
        void RenderBar(IRenderContext* ctx, float w, float h, double phase);
        void RenderPulse(IRenderContext* ctx, float w, float h, double phase);
        void RenderDotRing(IRenderContext* ctx, float w, float h, double cycles);
        float DotHue(int dot, long long fadeIn);
    };

// ===== FACTORY =====
    // A square indicator `size` px on a side (a Ring by default).
    inline std::shared_ptr<UltraCanvasBusyIndicator> CreateBusyIndicator(
            const std::string& identifier, float x, float y, float size = 16.0f) {
        return std::make_shared<UltraCanvasBusyIndicator>(identifier, x, y, size, size);
    }

    // An indicator of the given kind and size. Dots and Bar want a wide box
    // (e.g. 36 x 10 for Dots, 120 x 4 for Bar); Ring, DualRing, Pulse and
    // DotRing a square one.
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
