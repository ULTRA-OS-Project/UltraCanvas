// include/UltraCanvasSmoothScroll.h
// Shared smooth-scroll animator for elements that own their scroll offsets.
//
// UltraCanvasScrollbar animates its own position, so every element that scrolls
// through a real scrollbar child (containers, list and tree views, dropdowns)
// already glides. The self-rendered views — the filer and album pages, the text
// area, the spreadsheet, the markdown and PDF pages, the diagram and chart
// viewports — keep their scroll position in plain members and used to jump it
// by a fixed step per wheel notch. This is the piece they were missing: bind it
// to that member once and wheel notches, arrow keys and page steps animate
// towards their target with the same easing and duration the scrollbar uses, so
// scrolling feels the same everywhere in an application.
//
//   // in the element
//   UltraCanvasSmoothScroll scrollAnim;
//   ...
//   scrollAnim.Bind([this] { return double(scrollOffsetY); },
//                   [this](double v) { scrollOffsetY = int(std::lround(v));
//                                      RequestRedraw(); });
//   // wheel / key handler
//   scrollAnim.AnimateBy(-event.wheelDelta * kWheelStep, 0, MaxScrollY());
//
// Anything that must land immediately — dragging the scrollbar thumb, revealing
// an entry, a programmatic SetScrollPosition — calls Jump() (or Cancel() before
// writing the member directly) so the view tracks the pointer exactly instead
// of chasing it.
//
// The animator is also used for wheel-driven zoom: a zoom factor eased over the
// same duration reads as one continuous move rather than a series of steps —
// bind it to the zoom scalar and animate that. Views that zoom about the cursor
// keep the focus point fixed while the factor eases, so the write callback
// recomputes the pan from the eased factor (see the chart and diagram
// viewports).
#pragma once

#include "UltraCanvasTimer.h"
#include <chrono>
#include <functional>

namespace UltraCanvas {

// ===== APP-WIDE SMOOTH SCROLLING DEFAULTS =====
// Smooth scrolling is on by default framework-wide, so every UltraCanvas
// application gets it without opting in. An application that wants the old
// instant jumps (or a different feel) sets it once at start-up:
//
//   UltraCanvas::SetSmoothScrollingEnabled(false);      // instant everywhere
//   UltraCanvas::SetSmoothScrollDuration(220);          // slower glide
//
// Both are read at the moment a scroll starts, so a change takes effect on the
// next wheel notch — there is nothing to re-apply to existing elements. New
// ScrollbarStyle instances pick the same values up (see UltraCanvasScrollbar.h),
// which is what keeps the scrollbar-backed elements and the self-rendered ones
// in step.
    void SetSmoothScrollingEnabled(bool enabled);
    bool IsSmoothScrollingEnabled();
    // Duration of one animated scroll step, in milliseconds. <= 0 disables the
    // animation as surely as SetSmoothScrollingEnabled(false).
    void SetSmoothScrollDuration(int milliseconds);
    int  GetSmoothScrollDuration();

// ===== THE ANIMATOR =====
// One animator drives one scalar (a vertical offset, a horizontal offset, a
// zoom level). A view that scrolls both ways owns two.
    class UltraCanvasSmoothScroll {
    public:
        using Reader = std::function<double()>;
        using Writer = std::function<void(double)>;

        UltraCanvasSmoothScroll() = default;
        ~UltraCanvasSmoothScroll();
        // The running timer captures `this`, so the animator stays put.
        UltraCanvasSmoothScroll(const UltraCanvasSmoothScroll&) = delete;
        UltraCanvasSmoothScroll& operator=(const UltraCanvasSmoothScroll&) = delete;

        // `read` returns the scalar's current value, `write` stores an animated
        // one (and normally asks for a redraw). Both are called on the UI thread
        // only. Re-binding an animator that is mid-flight cancels the flight.
        void Bind(Reader read, Writer write);
        bool IsBound() const { return readValue && writeValue; }

        // Animate to `value` / by `delta`, clamped to [lo, hi]. Consecutive
        // calls chain: a second wheel notch extends the pending target instead
        // of restarting from where the first one happens to have reached, so a
        // fast spin is one long glide rather than a stutter. Returns true when
        // the value is going to change (so a caller can report the event as
        // handled).
        bool AnimateTo(double value, double lo, double hi);
        bool AnimateBy(double delta, double lo, double hi);

        // Where an in-flight animation is heading (the current value when idle).
        // Callers that need to know the settled position — "is the last row
        // already reached?" — ask this rather than reading the member, which is
        // still mid-glide.
        double PendingValue() const;

        // Land on `value` right now: no animation, any in-flight one dropped.
        // For thumb drags, reveal-this-entry and programmatic positioning.
        void Jump(double value, double lo, double hi);
        // Drop an in-flight animation and leave the value where it got to. Call
        // this before writing the bound member directly.
        void Cancel();
        bool IsAnimating() const { return animating; }

        // Per-instance duration override; < 0 (the default) follows the app-wide
        // GetSmoothScrollDuration(). Views with unusually large steps can slow
        // their glide down without changing the rest of the application.
        void SetDuration(int milliseconds) { durationOverride = milliseconds; }
        int  DurationMs() const;

    private:
        void Tick();
        void StopTimer();

        Reader readValue;
        Writer writeValue;
        bool   animating = false;
        double startValue = 0.0;
        double targetValue = 0.0;
        double rangeLo = 0.0;
        double rangeHi = 0.0;
        int    durationOverride = -1;
        TimerId timerId = InvalidTimerId;
        // When the current flight started, so the eased path follows real time
        // and not the timer's actual (jittery) tick rate.
        std::chrono::steady_clock::time_point startTime;
    };

// ===== WHEEL ZOOM =====
// Zooming about the cursor is multiplicative, and applying two factors in a row
// about the same point is exactly the same as applying their product once — the
// pan solve holds that point fixed after each application. That is what lets a
// zoom be eased without touching an element's transform maths: this animator
// spreads one wheel notch's factor over the smooth-scroll duration and hands the
// element a series of small incremental factors, which it applies with the very
// code it used for the single one.
//
//   // in the element
//   UltraCanvasSmoothZoom zoomAnim;
//   ...
//   zoomAnim.Bind([this](double f) { ApplyZoomFactorAtCursor(f, zoomCursor); },
//                 [this] { RequestRedraw(); });
//   // wheel handler
//   zoomCursor = event.pointer;
//   zoomAnim.ZoomBy(factor, zoomLevel, minZoom, maxZoom);
//
// The element keeps clamping its own zoom; the range passed in only stops
// notches spun against a limit from piling up into a factor that would take a
// visible moment to unwind.
    class UltraCanvasSmoothZoom {
    public:
        // Applies one incremental zoom factor about the gesture's anchor.
        using StepApplier = std::function<void(double incrementalFactor)>;

        void Bind(StepApplier apply, std::function<void()> repaint);
        bool IsBound() const { return applyStep != nullptr; }

        // Eases `factor` in. `currentZoom`, `minZoom` and `maxZoom` bound how
        // far the pending total may run past the element's own limits.
        // Consecutive notches multiply into the pending total, so a fast spin is
        // one continuous zoom.
        bool ZoomBy(double factor, double currentZoom, double minZoom, double maxZoom);
        void Cancel() { anim.Cancel(); }
        bool IsAnimating() const { return anim.IsAnimating(); }

    private:
        // The gesture is eased in log space — where multiplying is adding — so
        // the same chaining and easing the scroll animator does apply unchanged.
        UltraCanvasSmoothScroll anim;
        double appliedLog = 0.0;
        StepApplier applyStep;
        std::function<void()> requestRepaint;
    };

} // namespace UltraCanvas
