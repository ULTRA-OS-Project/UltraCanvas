// core/UltraCanvasCaret.cpp
// Application-wide text caret: blinking and painting for the focused widget
// Version: 1.0.0
// Last Modified: 2026-07-20
// Author: UltraCanvas Framework

#include "UltraCanvasCaret.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"

namespace UltraCanvas {

    UltraCanvasCaret& UltraCanvasCaret::GetInstance() {
        static UltraCanvasCaret instance;
        return instance;
    }

    void UltraCanvasCaret::Show(UltraCanvasUIElement* newOwner, const Rect2Di& rectInWindow,
                                const Color& newColor, int newBlinkRate) {
        if (!painter && newColor == color && !surfaceDirty) {
            // Solid caret with unchanged color: surface only needs repainting
            // if the size changes (handled in ShowInternal).
        } else {
            surfaceDirty = true;
        }
        painter = nullptr;
        color = newColor;
        ShowInternal(newOwner, rectInWindow, newBlinkRate);
    }

    void UltraCanvasCaret::Show(UltraCanvasUIElement* newOwner, const Rect2Di& rectInWindow,
                                PaintFunction newPainter, int newBlinkRate) {
        // Painters can't be compared, so a custom caret is repainted whenever
        // it is (re)shown — the surface is tiny, this is negligible.
        painter = std::move(newPainter);
        surfaceDirty = true;
        ShowInternal(newOwner, rectInWindow, newBlinkRate);
    }

    void UltraCanvasCaret::ShowInternal(UltraCanvasUIElement* newOwner,
                                        const Rect2Di& rectInWindow, int newBlinkRate) {
        if (!newOwner || rectInWindow.width <= 0 || rectInWindow.height <= 0) return;

        UltraCanvasWindowBase* newWindow = newOwner->GetWindow();
        if (!newWindow) return;

        if (rect.width != rectInWindow.width || rect.height != rectInWindow.height) {
            surfaceDirty = true;
        }

        // Moving the caret to another window: erase it from the old one.
        if (window && window != newWindow) {
            window->RequestWindowComposition();
        }

        owner = newOwner;
        window = newWindow;
        rect = rectInWindow;
        blinkRate = newBlinkRate;

        // Any (re)show counts as activity: caret solid, blink interval restarted.
        phaseVisible = true;
        StartBlinkTimer();

        // Show() is normally called from the owner's Render() pass, i.e. inside
        // UpdateAndRender() with a full composition already pending — this only
        // matters when a widget repositions the caret outside a render pass.
        RequestComposition(true);
    }

    void UltraCanvasCaret::Hide(UltraCanvasUIElement* ownerElement) {
        if (!owner || owner != ownerElement) return;

        StopBlinkTimer();
        phaseVisible = false;

        // The content surface never contains the caret, so a plain
        // re-composition erases it.
        if (window) {
            RequestComposition(true);
        }
        owner = nullptr;
        window = nullptr;
    }

    void UltraCanvasCaret::ResetBlink(UltraCanvasUIElement* ownerElement) {
        if (!owner || owner != ownerElement) return;
        bool wasVisible = phaseVisible;
        phaseVisible = true;
        StartBlinkTimer();
        if (!wasVisible) {
            RequestComposition(true);
        }
    }

    void UltraCanvasCaret::StartBlinkTimer() {
        StopBlinkTimer();
        if (blinkRate <= 0) return;  // solid caret

        auto* app = UltraCanvasApplication::GetInstance();
        if (!app) return;

        // blinkRate counts full on/off cycles per second, so the caret
        // toggles every half cycle.
        unsigned int halfPeriodMs = static_cast<unsigned int>(std::max(1, 500 / blinkRate));
        blinkTimerId = app->StartTimer(halfPeriodMs, true, [this](TimerId) {
            phaseVisible = !phaseVisible;
            RequestComposition(true);
        });
    }

    void UltraCanvasCaret::StopBlinkTimer() {
        if (blinkTimerId == InvalidTimerId) return;
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(blinkTimerId);
        blinkTimerId = InvalidTimerId;
    }

    void UltraCanvasCaret::RequestComposition(bool caretAreaOnly) {
        if (!window) return;
        if (caretAreaOnly) {
            window->RequestCaretComposition();
        } else {
            window->RequestWindowComposition();
        }
    }

    void UltraCanvasCaret::Composite(UltraCanvasWindowBase* win, NativeSurfacePtr toSurface) {
        if (!IsOnWindow(win) || !phaseVisible || !toSurface) return;
        if (rect.width <= 0 || rect.height <= 0) return;

        Size2Di wanted(rect.width, rect.height);
        if (!caretContext || caretContext->GetSurfaceSize() != wanted) {
            caretContext = CreateRenderContext(wanted, toSurface);
            surfaceDirty = true;
        }
        if (!caretContext) return;

        if (surfaceDirty) {
            IRenderContext* ctx = caretContext.get();
            ctx->Clear(Colors::Transparent);
            if (painter) {
                painter(ctx, rect.width, rect.height);
            } else {
                ctx->SetFillPaint(color);
                ctx->FillRectangle(Rect2Dd(0, 0, rect.width, rect.height));
            }
            surfaceDirty = false;
        }

        // Alpha-blend so shaped/translucent carets don't punch holes into the
        // window content beneath them.
        caretContext->CompositeToSurface(toSurface, {(double)rect.x, (double)rect.y});
    }

    void UltraCanvasCaret::OnWindowClosed(UltraCanvasWindowBase* win) {
        if (window != win) return;
        StopBlinkTimer();
        phaseVisible = false;
        owner = nullptr;
        window = nullptr;
        // The caret surface was created similar to this window's native
        // surface; drop it so it is lazily rebuilt against the next window.
        caretContext.reset();
        surfaceDirty = true;
    }

} // namespace UltraCanvas
