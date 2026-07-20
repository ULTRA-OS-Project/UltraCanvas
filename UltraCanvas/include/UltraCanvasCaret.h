// include/UltraCanvasCaret.h
// Application-wide text caret: blinking and painting for the focused widget
// Version: 1.0.0
// Last Modified: 2026-07-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTimer.h"
#include <functional>
#include <memory>

namespace UltraCanvas {

    class UltraCanvasUIElement;
    class UltraCanvasWindowBase;

    // ===== APPLICATION-WIDE CARET =====
    // A single caret exists per application — the one in the focused text
    // widget — so one shared instance owns the blink timer and the caret
    // pixels for everyone.
    //
    // The caret is painted once into its own small offscreen surface and
    // composited (alpha-blended) over the owning window's native surface as
    // the final step of window composition. A blink toggle therefore never
    // re-renders any widget: when nothing else changed, the window only
    // restores the few pixels under the caret from its content surface and
    // blends the caret back on top (see UltraCanvasWindowBase::UpdateAndRender).
    //
    // Widget protocol:
    //  - While focused, call Show() from your Render() pass whenever you know
    //    the caret geometry (it is cheap to call repeatedly; the surface is
    //    only repainted when it actually changed).
    //  - Call Hide() on focus loss and in your destructor.
    //  - Call ResetBlink() on key activity so the caret stays solid while
    //    the user types.
    class UltraCanvasCaret {
    public:
        // Paints the caret's look into its surface. The context is cleared to
        // transparent first; w/h is the caret rect size in logical pixels.
        using PaintFunction = std::function<void(IRenderContext* ctx, int w, int h)>;

        static UltraCanvasCaret& GetInstance();

        // Claim the caret for `newOwner` and (re)position it. rectInWindow is
        // in window coordinates; blinkRate counts full on/off cycles per
        // second (<= 0 shows a solid, non-blinking caret). The default look
        // is a solid rectangle of `color`.
        void Show(UltraCanvasUIElement* newOwner, const Rect2Di& rectInWindow,
                  const Color& color, int blinkRate = 1);

        // Same, but with a custom look (e.g. the hex editor's translucent
        // block cursor). The painter is invoked lazily at composite time.
        void Show(UltraCanvasUIElement* newOwner, const Rect2Di& rectInWindow,
                  PaintFunction painter, int blinkRate = 1);

        // Release the caret. No-op unless `ownerElement` currently owns it,
        // so a widget losing focus can never hide a successor's caret.
        void Hide(UltraCanvasUIElement* ownerElement);

        // Restart the blink interval with the caret visible (typing activity).
        void ResetBlink(UltraCanvasUIElement* ownerElement);

        // ===== WINDOW COMPOSITOR INTEGRATION =====
        bool IsOnWindow(const UltraCanvasWindowBase* win) const {
            return owner != nullptr && window == win;
        }
        const Rect2Di& GetRect() const { return rect; }
        bool IsPhaseVisible() const { return owner != nullptr && phaseVisible; }

        // Blend the caret over `toSurface` if it lives on `win` and is in its
        // visible blink phase. Called by UltraCanvasWindowBase at the end of
        // window composition.
        void Composite(UltraCanvasWindowBase* win, NativeSurfacePtr toSurface);

        // Drop all references to a window being destroyed.
        void OnWindowClosed(UltraCanvasWindowBase* win);

    private:
        UltraCanvasCaret() = default;

        void ShowInternal(UltraCanvasUIElement* newOwner, const Rect2Di& rectInWindow,
                          int newBlinkRate);
        void StartBlinkTimer();
        void StopBlinkTimer();
        void RequestComposition(bool caretAreaOnly);

        UltraCanvasUIElement* owner = nullptr;
        UltraCanvasWindowBase* window = nullptr;
        Rect2Di rect;
        Color color;
        PaintFunction painter;         // null => solid `color` rectangle
        int blinkRate = 1;
        bool phaseVisible = false;
        bool surfaceDirty = true;
        TimerId blinkTimerId = InvalidTimerId;
        std::unique_ptr<IRenderContext> caretContext;  // the small caret surface
    };

} // namespace UltraCanvas
