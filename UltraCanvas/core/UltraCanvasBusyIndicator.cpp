// core/UltraCanvasBusyIndicator.cpp
// Platform-independent busy indicator (ring, dual ring, dots, bar, pulse) implementation.
// Version: 1.1.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework

#include "UltraCanvasBusyIndicator.h"
#include "UltraCanvasApplication.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    namespace {
        constexpr double kPi = 3.14159265358979323846;

        // `c` with its alpha scaled by `factor` (0..1).
        Color Faded(const Color& c, double factor) {
            const double a = std::clamp(factor, 0.0, 1.0) * c.a;
            return Color(c.r, c.g, c.b, static_cast<uint8_t>(std::lround(a)));
        }

        // 0 at the start of a cycle, 1 halfway, back to 0 at the end: a smooth
        // swell for anything that pulses rather than turns.
        double Swell(double phase) {
            return 0.5 - 0.5 * std::cos(phase * 2.0 * kPi);
        }
    }

    UltraCanvasBusyIndicator::UltraCanvasBusyIndicator(const std::string& identifier,
                                                       float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        mouseCursor = UCMouseCursor::Default;
    }

    UltraCanvasBusyIndicator::~UltraCanvasBusyIndicator() {
        // The timer callback captures `this`; it must not outlive the element.
        if (timerId != InvalidTimerId) {
            if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(timerId);
            timerId = InvalidTimerId;
        }
    }

    double UltraCanvasBusyIndicator::CurrentPhase() const {
        if (timerId == InvalidTimerId) return stoppedPhase;
        // Phase from elapsed time, not from a per-tick step, so a late timer
        // never slows the animation down.
        const double seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - startedAt).count();
        const double cycles = stoppedPhase + seconds * style.revolutionsPerSecond;
        return cycles - std::floor(cycles);
    }

    void UltraCanvasBusyIndicator::Start() {
        if (timerId != InvalidTimerId) return;
        auto* app = UltraCanvasApplication::GetInstance();
        if (!app) return;
        startedAt = std::chrono::steady_clock::now();
        timerId = app->StartTimer(std::max(1u, style.frameIntervalMs), true,
                                  [this](TimerId) { RequestRedraw(); });
        RequestRedraw();
    }

    void UltraCanvasBusyIndicator::Stop() {
        if (timerId == InvalidTimerId) return;
        stoppedPhase = CurrentPhase();
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(timerId);
        timerId = InvalidTimerId;
        RequestRedraw();
    }

    void UltraCanvasBusyIndicator::SetStyle(const BusyIndicatorStyle& s) {
        const bool wasRunning = IsRunning();
        const bool intervalChanged = s.frameIntervalMs != style.frameIntervalMs;
        if (wasRunning && intervalChanged) Stop();
        style = s;
        if (wasRunning && intervalChanged) Start();
        RequestRedraw();
    }

    void UltraCanvasBusyIndicator::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!ctx) return;
        if (!IsRunning() && style.hideWhenStopped) return;

        const float w = GetWidth(), h = GetHeight();
        if (std::min(w, h) <= 2.0f && style.kind != BusyIndicatorKind::Bar) return;
        if (w <= 2.0f || h <= 0.5f) return;

        const double phase = CurrentPhase();
        switch (style.kind) {
            case BusyIndicatorKind::Ring:  RenderRing(ctx, w, h, phase);  break;
            case BusyIndicatorKind::DualRing: RenderDualRing(ctx, w, h, phase); break;
            case BusyIndicatorKind::Dots:  RenderDots(ctx, w, h, phase);  break;
            case BusyIndicatorKind::Bar:   RenderBar(ctx, w, h, phase);   break;
            case BusyIndicatorKind::Pulse: RenderPulse(ctx, w, h, phase); break;
        }
        ctx->ClearPath();
    }

    void UltraCanvasBusyIndicator::RenderRing(IRenderContext* ctx, float w, float h, double phase) {
        const float side = std::min(w, h);
        const float thickness = style.thickness > 0.0f ? style.thickness
                                                       : std::max(1.5f, side / 8.0f);
        const double radius = (side - thickness) / 2.0;
        const double cx = w / 2.0, cy = h / 2.0;

        ctx->SetLineCap(LineCap::Round);
        ctx->SetStrokeWidth(thickness);

        if (style.trackColor.a > 0) {
            ctx->ClearPath();
            ctx->Arc(cx, cy, radius, 0.0, 2.0 * kPi);
            ctx->SetStrokePaint(style.trackColor);
            ctx->Stroke();
        }

        const double start = phase * 2.0 * kPi - kPi / 2.0;   // 12 o'clock at rest
        const double sweep = std::clamp(static_cast<double>(style.arcDegrees), 10.0, 350.0)
                             * kPi / 180.0;
        ctx->ClearPath();
        ctx->Arc(cx, cy, radius, start, start + sweep);
        ctx->SetStrokePaint(style.arcColor);
        ctx->Stroke();
    }

    void UltraCanvasBusyIndicator::RenderDualRing(IRenderContext* ctx, float w, float h, double phase) {
        const float side = std::min(w, h);
        const float thickness = style.thickness > 0.0f ? style.thickness
                                                       : std::max(1.5f, side / 8.0f);
        const double outerRadius = (side - thickness) / 2.0;
        // The inner ring sits one and a half strokes inside the outer one.
        const double innerRadius = std::max(thickness / 2.0, outerRadius - thickness * 1.5);
        const double cx = w / 2.0, cy = h / 2.0;
        const double sweep = std::clamp(static_cast<double>(style.arcDegrees) / 2.0, 10.0, 175.0)
                             * kPi / 180.0;

        ctx->SetLineCap(LineCap::Round);
        ctx->SetStrokeWidth(thickness);

        if (style.trackColor.a > 0) {
            for (double radius : {outerRadius, innerRadius}) {
                ctx->ClearPath();
                ctx->Arc(cx, cy, radius, 0.0, 2.0 * kPi);
                ctx->SetStrokePaint(style.trackColor);
                ctx->Stroke();
            }
        }

        // Outer ring clockwise from 12 o'clock, inner ring anticlockwise from
        // 6 o'clock, so at rest the two arcs face each other.
        const double outerStart = phase * 2.0 * kPi - kPi / 2.0;
        const double innerStart = -phase * 2.0 * kPi + kPi / 2.0;
        ctx->SetStrokePaint(style.arcColor);
        ctx->ClearPath();
        ctx->Arc(cx, cy, outerRadius, outerStart, outerStart + sweep);
        ctx->Stroke();
        ctx->ClearPath();
        ctx->Arc(cx, cy, innerRadius, innerStart, innerStart + sweep);
        ctx->Stroke();
    }

    void UltraCanvasBusyIndicator::RenderDots(IRenderContext* ctx, float w, float h, double phase) {
        const int count = std::clamp(style.dotCount, 2, 12);
        // Dots one diameter wide with 0.6 of a diameter between them, as large
        // as the box allows.
        const double diameter = std::min<double>(h, w / (count + (count - 1) * 0.6));
        const double gap = diameter * 0.6;
        const double rowWidth = count * diameter + (count - 1) * gap;
        const double left = (w - rowWidth) / 2.0 + diameter / 2.0;
        const double cy = h / 2.0;
        const double maxRadius = diameter / 2.0;

        for (int i = 0; i < count; ++i) {
            const double cx = left + i * (diameter + gap);
            if (style.trackColor.a > 0) {
                ctx->ClearPath();
                ctx->Circle(cx, cy, maxRadius);
                ctx->SetFillPaint(style.trackColor);
                ctx->Fill();
            }
            // Each dot swells a little after the one before it.
            double dotPhase = phase - static_cast<double>(i) / count;
            dotPhase -= std::floor(dotPhase);
            const double swell = Swell(dotPhase);
            ctx->ClearPath();
            ctx->Circle(cx, cy, maxRadius * (0.55 + 0.45 * swell));
            ctx->SetFillPaint(Faded(style.arcColor, 0.3 + 0.7 * swell));
            ctx->Fill();
        }
    }

    void UltraCanvasBusyIndicator::RenderBar(IRenderContext* ctx, float w, float h, double phase) {
        const double barHeight = std::min<double>(h, style.thickness > 0.0f ? style.thickness : h);
        const double top = (h - barHeight) / 2.0;
        const double corner = barHeight / 2.0;

        if (style.trackColor.a > 0) {
            ctx->ClearPath();
            ctx->RoundedRect(0.0, top, w, barHeight, corner);
            ctx->SetFillPaint(style.trackColor);
            ctx->Fill();
        }

        // The segment enters from the left edge and leaves at the right one;
        // the part outside the track is cut off rather than drawn.
        const double segment = w * std::clamp(static_cast<double>(style.barFraction), 0.05, 0.9);
        const double x = -segment + (w + segment) * phase;
        const double x0 = std::max(0.0, x);
        const double x1 = std::min<double>(w, x + segment);
        if (x1 - x0 < 0.5) return;
        ctx->ClearPath();
        ctx->RoundedRect(x0, top, x1 - x0, barHeight, std::min(corner, (x1 - x0) / 2.0));
        ctx->SetFillPaint(style.arcColor);
        ctx->Fill();
    }

    void UltraCanvasBusyIndicator::RenderPulse(IRenderContext* ctx, float w, float h, double phase) {
        const double maxRadius = std::min(w, h) / 2.0;
        const double cx = w / 2.0, cy = h / 2.0;

        if (style.trackColor.a > 0) {
            ctx->ClearPath();
            ctx->Circle(cx, cy, maxRadius);
            ctx->SetFillPaint(style.trackColor);
            ctx->Fill();
        }

        // Smallest and palest at the start of a cycle, full size and full
        // colour halfway through.
        const double swell = Swell(phase);
        ctx->ClearPath();
        ctx->Circle(cx, cy, maxRadius * (0.4 + 0.6 * swell));
        ctx->SetFillPaint(Faded(style.arcColor, 0.35 + 0.65 * swell));
        ctx->Fill();
    }

} // namespace UltraCanvas
