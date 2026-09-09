// core/UltraCanvasSmoothScroll.cpp
// Shared smooth-scroll animator — see UltraCanvasSmoothScroll.h for what it is
// for and how an element binds one.
#include "UltraCanvasSmoothScroll.h"
#include "UltraCanvasApplication.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// ===== APP-WIDE DEFAULTS =====
// On by default: every UltraCanvas application scrolls smoothly without opting
// in, and an application that wants the old instant jumps turns it off once.
    namespace {
        bool gSmoothScrollingEnabled = true;
        int  gSmoothScrollDuration = 150;   // ms, matches ScrollbarStyle
        // One tick per display frame at 60 Hz. The animation reads the clock
        // rather than counting ticks, so a slower timer only costs frames, never
        // accuracy.
        constexpr unsigned int kTickIntervalMs = 16;
    }

    void SetSmoothScrollingEnabled(bool enabled) { gSmoothScrollingEnabled = enabled; }
    bool IsSmoothScrollingEnabled() { return gSmoothScrollingEnabled; }

    void SetSmoothScrollDuration(int milliseconds) { gSmoothScrollDuration = milliseconds; }
    int  GetSmoothScrollDuration() { return gSmoothScrollDuration; }

// ===== THE ANIMATOR =====
    UltraCanvasSmoothScroll::~UltraCanvasSmoothScroll() {
        // The tick callback captures `this`; it must never outlive the animator.
        StopTimer();
    }

    void UltraCanvasSmoothScroll::Bind(Reader read, Writer write) {
        Cancel();
        readValue = std::move(read);
        writeValue = std::move(write);
    }

    int UltraCanvasSmoothScroll::DurationMs() const {
        return durationOverride >= 0 ? durationOverride : GetSmoothScrollDuration();
    }

    double UltraCanvasSmoothScroll::PendingValue() const {
        if (animating) return targetValue;
        return readValue ? readValue() : 0.0;
    }

    bool UltraCanvasSmoothScroll::AnimateBy(double delta, double lo, double hi) {
        if (delta == 0.0) return false;
        // Chain onto the pending target, not onto wherever the last flight has
        // reached, so a fast wheel spin adds up into one long glide. The base is
        // clamped first: without it, notches spun against an edge would pile up
        // into a target far outside the range and the view would sit still for a
        // moment before it started moving back.
        double base = std::clamp(PendingValue(), std::min(lo, hi), std::max(lo, hi));
        return AnimateTo(base + delta, lo, hi);
    }

    bool UltraCanvasSmoothScroll::AnimateTo(double value, double lo, double hi) {
        if (!IsBound()) return false;
        const double low = std::min(lo, hi), high = std::max(lo, hi);
        const double target = std::clamp(value, low, high);
        const double current = readValue();

        const int duration = DurationMs();
        if (!IsSmoothScrollingEnabled() || duration <= 0 ||
            !UltraCanvasApplication::GetInstance()) {
            if (target == current) return false;
            Jump(target, low, high);
            return true;
        }
        // Standing still and asked to stay: nothing to do. (Mid-flight the same
        // target is a real request — it is the wheel being spun back the other
        // way, and the glide has to turn around.)
        if (target == current && !animating) return false;

        animating = true;
        startValue = current;
        targetValue = target;
        rangeLo = low;
        rangeHi = high;
        startTime = std::chrono::steady_clock::now();

        if (timerId == InvalidTimerId) {
            timerId = UltraCanvasApplication::GetInstance()->StartTimer(
                    kTickIntervalMs, true, [this](TimerId) { Tick(); });
        }
        return true;
    }

    void UltraCanvasSmoothScroll::Tick() {
        if (!animating || !IsBound()) {
            Cancel();
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const double elapsed =
                std::chrono::duration<double, std::milli>(now - startTime).count();
        const double duration = static_cast<double>(std::max(1, DurationMs()));
        const double t = std::min(1.0, elapsed / duration);

        // Ease-out cubic, the same curve UltraCanvasScrollbar animates on: quick
        // off the mark so the view answers the notch at once, gentle onto the
        // target so it does not appear to stop dead.
        const double eased = 1.0 - std::pow(1.0 - t, 3.0);
        // The content can grow or shrink mid-flight (a rescan, a reflow), so the
        // target is re-clamped to the range the flight was started with rather
        // than trusted blindly.
        const double target = std::clamp(targetValue, rangeLo, rangeHi);
        const double value = startValue + (target - startValue) * eased;

        if (t >= 1.0) {
            // Land exactly on the target: the eased value is a fraction short of
            // it, and a view left one pixel from its end would never look
            // scrolled all the way down.
            animating = false;
            StopTimer();
            writeValue(target);
            return;
        }
        writeValue(value);
    }

    void UltraCanvasSmoothScroll::Jump(double value, double lo, double hi) {
        Cancel();
        if (!IsBound()) return;
        writeValue(std::clamp(value, std::min(lo, hi), std::max(lo, hi)));
    }

    void UltraCanvasSmoothScroll::Cancel() {
        animating = false;
        StopTimer();
    }

    void UltraCanvasSmoothScroll::StopTimer() {
        if (timerId == InvalidTimerId) return;
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(timerId);
        timerId = InvalidTimerId;
    }

// ===== WHEEL ZOOM =====
    void UltraCanvasSmoothZoom::Bind(StepApplier apply, std::function<void()> repaint) {
        anim.Cancel();
        applyStep = std::move(apply);
        requestRepaint = std::move(repaint);
        appliedLog = 0.0;
        anim.Bind([this] { return appliedLog; },
                  [this](double v) {
                      // The eased value is the cumulative log factor; the element
                      // gets the step from where it was last left.
                      const double step = std::exp(v - appliedLog);
                      appliedLog = v;
                      if (applyStep) applyStep(step);
                      if (requestRepaint) requestRepaint();
                  });
    }

    bool UltraCanvasSmoothZoom::ZoomBy(double factor, double currentZoom,
                                       double minZoom, double maxZoom) {
        if (!IsBound() || factor <= 0.0 || currentZoom <= 0.0) return false;
        // How much room the element's own limits leave from where it is now.
        const double headroom = std::log(std::max(maxZoom, currentZoom) / currentZoom);
        const double footroom = std::log(std::min(minZoom, currentZoom) / currentZoom);
        return anim.AnimateBy(std::log(factor),
                              appliedLog + footroom, appliedLog + headroom);
    }

} // namespace UltraCanvas
