// include/UltraCanvasTooltipManager.h
// Updated tooltip system compatible with unified UltraCanvas architecture
// Version: 2.5.0 - the tooltip is shown in a window of its own (WindowType::Tooltip), above
//                  every other window: SetUseOwnWindow / GetUseOwnWindow, GetTooltipWindow,
//                  ForgetWindow, WindowScaleChanged
// Version: 1.2.0 - SetStyle is the style every tooltip shown without one takes; GetStyle
// Version: 2.1.1
// Last Modified: 2026-11-09
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasTooltipTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasTimer.h"
#include <string>
#include <functional>
#include <memory>
#include <vector>

namespace UltraCanvas {
    // TooltipStyle and TooltipContent live in UltraCanvasTooltipTypes.h

// ===== TOOLTIP MANAGER CLASS =====
    class UltraCanvasTooltipManager {
    private:
        // State tracking
        static UltraCanvasWindowBase* targetWindow;
        static std::unique_ptr<IRenderContext> renderCtx;
        static std::string currentText;
        static Rect2Di tooltipRect;
//        static Point2Di cursorPosition;
        static bool visible;
        static bool pendingShow;
        static bool pendingHide;
        static bool needsRedraw;

        // Timing (via Application timer API)
        static TimerId showTimerId;
        static TimerId hideTimerId;

        // Style and layout
        static TooltipStyle style;

        // Global state
        static bool enabled;

        // Its own window (see SetUseOwnWindow): the window, the window it was
        // made for, and whether the tooltip on screen is in it
        static std::shared_ptr<UltraCanvasWindowBase> ownWindow;
        static UltraCanvasWindowBase* ownWindowTarget;
        static bool useOwnWindow;
        static bool shownInOwnWindow;
        // The pointer, in the coordinates of the window the tooltip belongs
        // to, and where the own window goes, in native screen coordinates
        static Point2Di cursorPosition;
        static Point2Di screenPosition;

    public:
        // ===== CORE FUNCTIONALITY =====

        // Show a plain-text tooltip (the text may contain Pango markup for
        // inline styling, e.g. "<b>bold</b>")
        static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const std::string &text, const Point2Di &position, const TooltipStyle& newStyle);

        // Without a style: the manager's (SetStyle), the framework's dark
        // tooltip until one is set
        static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const std::string& text, const Point2Di& position) {
            UpdateAndShowTooltip(win, text, position, style);
        }

        // Show a structured tooltip (title, label/value rows, bullets, …).
        // The explicit-style overload wins over content.styleOverride.
        static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const TooltipContent& content, const Point2Di& position, const TooltipStyle& newStyle);

        static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const TooltipContent& content, const Point2Di& position) {
            UpdateAndShowTooltip(win, content, position,
                                 content.styleOverride ? *content.styleOverride : style);
        }

        // Hide current tooltip
        static void HideTooltip();
        static void HideTooltipImmediately();

        // Force immediate show/hide
        static void UpdateAndShowTooltipImmediately(UltraCanvasWindowBase* win, const std::string &text, const Point2Di &position, const TooltipStyle& newStyle);
        static void UpdateAndShowTooltipImmediately(UltraCanvasWindowBase* win, const std::string &text, const Point2Di &position) {
            UpdateAndShowTooltipImmediately(win, text, position, style);
        }
        static void UpdateAndShowTooltipImmediately(UltraCanvasWindowBase* win, const TooltipContent& content, const Point2Di& position, const TooltipStyle& newStyle);
        static void UpdateAndShowTooltipImmediately(UltraCanvasWindowBase* win, const TooltipContent& content, const Point2Di& position) {
            UpdateAndShowTooltipImmediately(win, content, position,
                                            content.styleOverride ? *content.styleOverride : style);
        }


        // ===== RENDERING =====

        // Render tooltip - call this during window rendering
        static IRenderContext* Render(UltraCanvasWindowBase* win);

        // ===== CONFIGURATION =====

        static void SetEnabled(bool enable) {
            enabled = enable;
            if (!enabled) {
                HideTooltipImmediately();
            }
        }

        static bool IsEnabled() {
            return enabled;
        }

        static bool IsVisible() {
            return visible;
        }

        static bool IsPending() {
            return pendingShow;
        }

//        static UltraCanvasUIElement* GetTooltipSource() {
//            return tooltipSource;
//        }
        // The style every tooltip shown without one takes - set it from the
        // theme (TooltipStyle::FromTheme) when the window's theme changes
        static void SetStyle(const TooltipStyle &newStyle);
        static const TooltipStyle& GetStyle() { return style; }

        static const std::string& GetCurrentText() {
            return currentText;
        }

        static Point2Di GetTooltipPosition() {
            return tooltipRect.TopLeft();
        }

        // Top-left corner of the rendered surface (tooltip position minus the
        // soft-shadow margins). Use this when compositing the Render() result.
        static Point2Di GetCompositePosition();

        static Size2Di GetTooltipSize() {
            return tooltipRect.Size();
        }

        static void UpdateTooltipPosition(const Point2Di& position);

        // ===== ITS OWN WINDOW =====

        // The tooltip is shown in a window of its own (WindowType::Tooltip):
        // above every other window, never focused, the pointer passing
        // through it, and free to reach past the edge of the window it
        // belongs to. Drawn inside that window instead, it was covered by any
        // window above it - every tooltip of a desktop's bars, which live in
        // a window kept below all others - and stopped at its edges. On by
        // default where the platform can place such a window (X11, Wayland,
        // Windows); elsewhere (macOS, the browser, Android), when it is
        // turned off, or when the window cannot be made, the tooltip is drawn
        // inside the window it belongs to, as before.
        static void SetUseOwnWindow(bool use);
        static bool GetUseOwnWindow();

        // The window the tooltip on screen is shown in: its own, or nullptr
        // while it is drawn inside the window it belongs to, or none is shown
        static UltraCanvasWindowBase* GetTooltipWindow();

        // A window is closing (UltraCanvasApplication calls this): a tooltip
        // of that window goes, and with it the tooltip's own window; and when
        // it is the tooltip's own window, the manager lets go of it.
        static void ForgetWindow(UltraCanvasWindowBase* win);

        // A window moved to a monitor of another scale (its
        // HandleDeviceScaleChange calls this): the tooltip's own window draws
        // it again at the new scale; any other hides the tooltip, whose
        // surface was made for the old one.
        static void WindowScaleChanged(UltraCanvasWindowBase* win);

    private:
        static Rect2Di screenBounds;

        // ===== INTERNAL HELPER METHODS =====

        static void CancelShowTimer();
        static void CancelHideTimer();

        static void CalculateTooltipLayout();

        // The own window: whether this build can show one; showing the
        // tooltip in it (false: it could not be, draw it in the window it
        // belongs to); placing it on the screen; and closing it
        static bool OwnWindowAvailable();
        static bool ShowInOwnWindow();
        static void PlaceOnScreen();
        static void CloseOwnWindow();
        // The window the tooltip is drawn into: its own, or the one it
        // belongs to
        static UltraCanvasWindowBase* RenderHost();
        // A tooltip already on screen took new content, a new style or a new
        // place: its window is told to draw it again
        static void RefreshShown();
    };
} // namespace UltraCanvas
