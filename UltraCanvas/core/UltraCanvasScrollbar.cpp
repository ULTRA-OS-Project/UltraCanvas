// core/UltraCanvasScrollbar.cpp
// Platform-independent scrollbar component implementation
// Version: 1.0.0
// Last Modified: 2025-08-15
// Author: UltraCanvas Framework

#include "UltraCanvasScrollbar.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasUI.h"
#include <cmath>

namespace UltraCanvas {

// This file is intentionally minimal since most implementation
// is in the header file using inline methods for better performance.
// Additional platform-specific optimizations can be added here if needed.
    UltraCanvasScrollbar::UltraCanvasScrollbar(const std::string& id, long uid, int x, int y, int w, int h,
                         ScrollbarOrientation orient)
            : UltraCanvasUIElement(id, uid, x, y, w, h)
            , orientation(orient) {
        UpdateLayout();
    }

    void UltraCanvasScrollbar::SetOrientation(ScrollbarOrientation orient) {
        if (orientation != orient) {
            orientation = orient;
            layoutDirty = true;
            RequestRedraw();
        }
    }

    void UltraCanvasScrollbar::SetStyle(const ScrollbarStyle &newStyle) {
        style = newStyle;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasScrollbar::SetScrollDimensions(int viewportSize, int contentSize) {
        scrollState.viewportSize = std::max(1, viewportSize);
        scrollState.contentSize = std::max(1, contentSize);
        scrollState.UpdateMaxPosition();
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasScrollbar::SetViewportSize(int size) {
        scrollState.viewportSize = std::max(1, size);
        scrollState.UpdateMaxPosition();
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasScrollbar::SetBounds(const Rect2Di& b) {
        if (b != bounds) {
            UltraCanvasUIElement::SetBounds(b);
            layoutDirty = true;
            RequestRedraw();
        }
    }

    void UltraCanvasScrollbar::SetContentSize(int size) {
        scrollState.contentSize = std::max(1, size);
        scrollState.UpdateMaxPosition();
        layoutDirty = true;
        RequestRedraw();
    }

    bool UltraCanvasScrollbar::SetScrollPosition(int position) {
        int newPos = std::clamp(position, 0, scrollState.maxPosition);
        if (newPos != scrollState.position) {
            scrollState.position = newPos;
            layoutDirty = true;
            RequestRedraw();

            if (onScrollChange) {
                onScrollChange(scrollState.position);
            }
            return true;
        }
        return false;
    }

    void UltraCanvasScrollbar::Render(IRenderContext *ctx) {
        if (!ctx || !ShouldBeVisible()) return;

        ctx->PushState();

        if (layoutDirty) {
            UpdateLayout();
        }

        // Render track
        RenderTrack(ctx);

        // Render arrow buttons if enabled
        if (style.arrowButtonSize > 0) {
            RenderArrowButton(ctx, true);   // Up/Left
            RenderArrowButton(ctx, false);  // Down/Right
        }

        // Render thumb
        RenderThumb(ctx);

        ctx->PopState();
    }

    bool UltraCanvasScrollbar::ScrollByWheel(int delta) {
        int scrollAmount = delta * style.scrollSpeed;
        return ScrollBy(-scrollAmount);  // Invert for natural scrolling
    }

    bool UltraCanvasScrollbar::OnEvent(const UCEvent &event) {
        if (IsDisabled() || !ShouldBeVisible()) return false;

        if (layoutDirty) {
            UpdateLayout();
        }

        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);

            case UCEventType::MouseUp:
                return HandleMouseUp(event);

            case UCEventType::MouseMove:
                return HandleMouseMove(event);

            case UCEventType::MouseWheel:
                return HandleMouseWheel(event);

            case UCEventType::MouseLeave:
                return HandleMouseLeave(event);

            default:
                return false;
        }
    }

    void UltraCanvasScrollbar::UpdateLayout() {
        Rect2Di bounds = GetBounds();

        if (IsVertical()) {
            UpdateVerticalLayout(bounds);
        } else {
            UpdateHorizontalLayout(bounds);
        }

        UpdateThumbRect();
        layoutDirty = false;
    }

    void UltraCanvasScrollbar::UpdateVerticalLayout(const Rect2Di &bounds) {
        if (style.arrowButtonSize > 0) {
            // With arrow buttons
            upArrowRect = Rect2Di(bounds.x, bounds.y,
                                  bounds.width, style.arrowButtonSize);
            downArrowRect = Rect2Di(bounds.x, bounds.y + bounds.height - style.arrowButtonSize,
                                    bounds.width, style.arrowButtonSize);
            trackRect = Rect2Di(bounds.x, bounds.y + style.arrowButtonSize,
                                bounds.width, bounds.height - 2 * style.arrowButtonSize);
        } else {
            // No arrow buttons
            trackRect = bounds;
            upArrowRect = Rect2Di(0, 0, 0, 0);
            downArrowRect = Rect2Di(0, 0, 0, 0);
        }
    }

    void UltraCanvasScrollbar::UpdateHorizontalLayout(const Rect2Di &bounds) {
        if (style.arrowButtonSize > 0) {
            // With arrow buttons
            upArrowRect = Rect2Di(bounds.x, bounds.y,
                                  style.arrowButtonSize, bounds.height);
            downArrowRect = Rect2Di(bounds.x + bounds.width - style.arrowButtonSize, bounds.y,
                                    style.arrowButtonSize, bounds.height);
            trackRect = Rect2Di(bounds.x + style.arrowButtonSize, bounds.y,
                                bounds.width - 2 * style.arrowButtonSize, bounds.height);
        } else {
            // No arrow buttons
            trackRect = bounds;
            upArrowRect = Rect2Di(0, 0, 0, 0);
            downArrowRect = Rect2Di(0, 0, 0, 0);
        }
    }

    void UltraCanvasScrollbar::UpdateThumbRect() {
        if (!scrollState.IsScrollable()) {
            // Full track thumb when not scrollable
            thumbRect = trackRect;
            return;
        }

        float thumbRatio = scrollState.GetThumbRatio();
        float scrollRatio = scrollState.GetScrollRatio();

        if (IsVertical()) {
            int thumbHeight = std::max(style.thumbMinSize,
                                       static_cast<int>(trackRect.height * thumbRatio));
            int availableSpace = trackRect.height - thumbHeight;
            int thumbY = trackRect.y + static_cast<int>(availableSpace * scrollRatio);

            thumbRect = Rect2Di(trackRect.x, thumbY, trackRect.width, thumbHeight);
        } else {
            int thumbWidth = std::max(style.thumbMinSize,
                                      static_cast<int>(trackRect.width * thumbRatio));
            int availableSpace = trackRect.width - thumbWidth;
            int thumbX = trackRect.x + static_cast<int>(availableSpace * scrollRatio);

            thumbRect = Rect2Di(thumbX, trackRect.y, thumbWidth, trackRect.height);
        }
    }

    void UltraCanvasScrollbar::RenderTrack(IRenderContext *ctx) {
        Color trackColor = interactionState.trackHovered ? style.trackHoverColor : style.trackColor;
        Color borderColor = style.showTrackBorder ? style.trackBorderColor : style.trackColor;
        ctx->DrawFilledRectangle(trackRect, trackColor, 1, borderColor, style.trackCornerRadius);
    }

    void UltraCanvasScrollbar::RenderThumb(IRenderContext *ctx) {
        if (thumbRect.width <= 0 || thumbRect.height <= 0) return;

        // Determine thumb color based on state
        Color thumbColor = style.thumbColor;
        if (interactionState.thumbPressed || interactionState.isDragging) {
            thumbColor = style.thumbPressedColor;
        } else if (interactionState.thumbHovered) {
            thumbColor = style.thumbHoverColor;
        }
        Color borderColor = style.showThumbBorder ? style.thumbBorderColor : thumbColor;

        ctx->DrawFilledRectangle(thumbRect, thumbColor, 1, borderColor, style.thumbCornerRadius);
    }

    void UltraCanvasScrollbar::RenderArrowButton(IRenderContext *ctx, bool isUpOrLeft) {
        Rect2Di& arrowRect = isUpOrLeft ? upArrowRect : downArrowRect;
        bool hovered = isUpOrLeft ? interactionState.upArrowHovered : interactionState.downArrowHovered;
        bool pressed = isUpOrLeft ? interactionState.upArrowPressed : interactionState.downArrowPressed;

        if (arrowRect.width <= 0 || arrowRect.height <= 0) return;

        // Background
        Color bgColor = pressed ? style.arrowBackgroundHoverColor :
                        (hovered ? style.arrowBackgroundHoverColor : style.arrowBackgroundColor);
        ctx->DrawFilledRectangle(arrowRect, bgColor);

        // Arrow icon
        Color arrowColor = pressed ? style.arrowPressedColor :
                           (hovered ? style.arrowHoverColor : style.arrowColor);
        ctx->SetStrokePaint(arrowColor);
        ctx->SetStrokeWidth(2.0f);

        int cx = arrowRect.x + arrowRect.width / 2;
        int cy = arrowRect.y + arrowRect.height / 2;
        int arrowSize = std::min(arrowRect.width, arrowRect.height) / 3;

        if (IsVertical()) {
            if (isUpOrLeft) {
                // Up arrow
                ctx->DrawLine(Point2Di(cx - arrowSize, cy + arrowSize / 2),
                              Point2Di(cx, cy - arrowSize / 2));
                ctx->DrawLine(Point2Di(cx, cy - arrowSize / 2),
                              Point2Di(cx + arrowSize, cy + arrowSize / 2));
            } else {
                // Down arrow
                ctx->DrawLine(Point2Di(cx - arrowSize, cy - arrowSize / 2),
                              Point2Di(cx, cy + arrowSize / 2));
                ctx->DrawLine(Point2Di(cx, cy + arrowSize / 2),
                              Point2Di(cx + arrowSize, cy - arrowSize / 2));
            }
        } else {
            if (isUpOrLeft) {
                // Left arrow
                ctx->DrawLine(Point2Di(cx + arrowSize / 2, cy - arrowSize),
                              Point2Di(cx - arrowSize / 2, cy));
                ctx->DrawLine(Point2Di(cx - arrowSize / 2, cy),
                              Point2Di(cx + arrowSize / 2, cy + arrowSize));
            } else {
                // Right arrow
                ctx->DrawLine(Point2Di(cx - arrowSize / 2, cy - arrowSize),
                              Point2Di(cx + arrowSize / 2, cy));
                ctx->DrawLine(Point2Di(cx + arrowSize / 2, cy),
                              Point2Di(cx - arrowSize / 2, cy + arrowSize));
            }
        }
    }

    bool UltraCanvasScrollbar::HandleMouseDown(const UCEvent &event) {
        Point2Di mousePos(event.x, event.y);

        // Check thumb first
        if (thumbRect.Contains(mousePos)) {
            interactionState.thumbPressed = true;
            interactionState.isDragging = true;
            interactionState.dragStartMousePos = IsVertical() ? event.windowY : event.windowX;
            interactionState.dragStartScrollPos = scrollState.position;

            // Capture mouse
            if (auto* app = UltraCanvasApplication::GetInstance()) {
                app->CaptureMouse(this);
            }
            RequestRedraw();
            return true;
        }

        // Check arrow buttons
        if (style.arrowButtonSize > 0) {
            if (upArrowRect.Contains(mousePos)) {
                interactionState.upArrowPressed = true;
                ScrollLineUp();
                RequestRedraw();
                return true;
            }

            if (downArrowRect.Contains(mousePos)) {
                interactionState.downArrowPressed = true;
                ScrollLineDown();
                RequestRedraw();
                return true;
            }
        }

        // Check track (page scroll)
        if (trackRect.Contains(mousePos)) {
            interactionState.trackPressed = true;

            // Determine if click is above or below thumb
            if (IsVertical()) {
                if (mousePos.y < thumbRect.y) {
                    ScrollPageUp();
                } else if (mousePos.y > thumbRect.y + thumbRect.height) {
                    ScrollPageDown();
                }
            } else {
                if (mousePos.x < thumbRect.x) {
                    ScrollPageUp();  // Page left
                } else if (mousePos.x > thumbRect.x + thumbRect.width) {
                    ScrollPageDown();  // Page right
                }
            }
            RequestRedraw();
            return true;
        }

        return false;
    }

    bool UltraCanvasScrollbar::HandleMouseUp(const UCEvent &event) {
        bool wasInteracting = interactionState.isDragging ||
                              interactionState.thumbPressed ||
                              interactionState.upArrowPressed ||
                              interactionState.downArrowPressed ||
                              interactionState.trackPressed;

        if (interactionState.isDragging) {
            if (auto* app = UltraCanvasApplication::GetInstance()) {
                app->ReleaseMouse(this);
            }
        }

        interactionState.isDragging = false;
        interactionState.thumbPressed = false;
        interactionState.upArrowPressed = false;
        interactionState.downArrowPressed = false;
        interactionState.trackPressed = false;

        if (wasInteracting) {
            RequestRedraw();
        }

        return wasInteracting;
    }

    bool UltraCanvasScrollbar::HandleMouseMove(const UCEvent &event) {
        Point2Di mousePos(event.x, event.y);

        // Handle dragging
        if (interactionState.isDragging) {
            int currentPos = IsVertical() ? event.windowY : event.windowX;
            int delta = currentPos - interactionState.dragStartMousePos;

            // Convert pixel delta to scroll delta
            int trackSize = IsVertical() ? trackRect.height : trackRect.width;
            int thumbSize = IsVertical() ? thumbRect.height : thumbRect.width;
            int availableSpace = trackSize - thumbSize;

            if (availableSpace > 0) {
                int scrollDelta = (delta * scrollState.maxPosition) / availableSpace;
                SetScrollPosition(interactionState.dragStartScrollPos + scrollDelta);
            }
            return true;
        }

        // Update hover states
        bool needsRedraw = false;
        bool newThumbHovered = thumbRect.Contains(mousePos);
        bool newTrackHovered = trackRect.Contains(mousePos) && !newThumbHovered;
        bool newUpArrowHovered = (style.arrowButtonSize > 0) && upArrowRect.Contains(mousePos);
        bool newDownArrowHovered = (style.arrowButtonSize > 0) && downArrowRect.Contains(mousePos);

        if (newThumbHovered != interactionState.thumbHovered ||
            newTrackHovered != interactionState.trackHovered ||
            newUpArrowHovered != interactionState.upArrowHovered ||
            newDownArrowHovered != interactionState.downArrowHovered) {
            needsRedraw = true;
        }

        interactionState.thumbHovered = newThumbHovered;
        interactionState.trackHovered = newTrackHovered;
        interactionState.upArrowHovered = newUpArrowHovered;
        interactionState.downArrowHovered = newDownArrowHovered;

        if (needsRedraw) {
            RequestRedraw();
        }

        return false;  // Don't consume move events unless dragging
    }

    bool UltraCanvasScrollbar::HandleMouseWheel(const UCEvent &event) {
        //if (!Contains(event.x, event.y)) return false;
        if (IsVertical()) {
            ScrollByWheel(event.wheelDelta);
            return true;
        } else {
            if (event.shift) {
                ScrollByWheel(event.wheelDelta);
                return true;
            }
        }
        return false;
    }

    bool UltraCanvasScrollbar::HandleMouseLeave(const UCEvent &event) {
        if (interactionState.thumbHovered || interactionState.trackHovered ||
            interactionState.upArrowHovered || interactionState.downArrowHovered) {
            interactionState.thumbHovered = false;
            interactionState.trackHovered = false;
            interactionState.upArrowHovered = false;
            interactionState.downArrowHovered = false;
            RequestRedraw();
        }
        return false;
    }

    void ScrollbarScrollState::UpdateMaxPosition() {
        maxPosition = std::max(0, contentSize - viewportSize);
        position = std::clamp(position, 0, maxPosition);
    }
} // namespace UltraCanvas