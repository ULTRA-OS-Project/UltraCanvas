// core/UltraCanvasBusyIndicator.cpp
// Platform-independent busy indicator (turning ring) implementation.
// Version: 1.0.0
// Last Modified: 2026-09-28
// Author: UltraCanvas Framework

#include "UltraCanvasBusyIndicator.h"
#include "UltraCanvasApplication.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    namespace {
        constexpr double kPi = 3.14159265358979323846;
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

    double UltraCanvasBusyIndicator::CurrentAngle() const {
        if (timerId == InvalidTimerId) return stoppedAngle;
        // Angle from elapsed time, not from a per-tick step, so a late timer
        // never slows the ring down.
        const double seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - startedAt).count();
        const double turns = seconds * style.revolutionsPerSecond;
        return stoppedAngle + (turns - std::floor(turns)) * 2.0 * kPi;
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
        stoppedAngle = std::fmod(CurrentAngle(), 2.0 * kPi);
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
        const float side = std::min(w, h);
        if (side <= 2.0f) return;

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

        const double start = CurrentAngle() - kPi / 2.0;   // 12 o'clock at rest
        const double sweep = std::clamp(static_cast<double>(style.arcDegrees), 10.0, 350.0)
                             * kPi / 180.0;
        ctx->ClearPath();
        ctx->Arc(cx, cy, radius, start, start + sweep);
        ctx->SetStrokePaint(style.arcColor);
        ctx->Stroke();
        ctx->ClearPath();
    }

} // namespace UltraCanvas
