// core/UltraCanvasSplitPane.cpp
// Split pane container that divides content into N draggable panes along one axis
// Version: 1.1.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework

#include "UltraCanvasSplitPane.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// =====================================================================
// UltraCanvasSplitter
// =====================================================================

    UltraCanvasSplitter::UltraCanvasSplitter(const std::string& id, SplitOrientation orient,
                                             UltraCanvasSplitPane* ownerPane)
            : UltraCanvasUIElement(id, 0, 0, 0, 0),
              orientation(orient),
              owner(ownerPane) {
        SetMouseCursor(orientation == SplitOrientation::Horizontal
                       ? UCMouseCursor::SizeWE
                       : UCMouseCursor::SizeNS);
    }

// ---------------------------------------------------------------------
// Geometry
//
// Everything below works in "along" (parallel to the split line) and "cross"
// (perpendicular to it) terms, so one set of formulas serves both orientations.
// A Horizontal split pane has a vertical splitter strip: along = Y, cross = X.
// ---------------------------------------------------------------------

    void UltraCanvasSplitter::UpdateGeometry() {
        handleRect = Rect2Df(0, 0, 0, 0);
        iconRects.clear();
        iconHitRects.clear();
        iconIndices.clear();

        const bool horizontal = (orientation == SplitOrientation::Horizontal);
        const float w = GetWidth();
        const float h = GetHeight();
        const float alongExtent = horizontal ? h : w;
        const float crossExtent = horizontal ? w : h;
        if (alongExtent <= 0.0f || crossExtent <= 0.0f) return;

        auto makeRect = [horizontal](float alongPos, float alongLen,
                                     float crossPos, float crossLen) {
            return horizontal ? Rect2Df(crossPos, alongPos, crossLen, alongLen)
                              : Rect2Df(alongPos, crossPos, alongLen, crossLen);
        };

        // Visible icons of this splitter.
        std::vector<size_t> visible;
        if (owner) {
            const auto& icons = owner->GetSplitterIcons(index);
            for (size_t i = 0; i < icons.size(); ++i) {
                if (icons[i].visible) visible.push_back(i);
            }
        }

        const SplitterIconStyle& is = style.icons;
        const int count = static_cast<int>(visible.size());
        const float iconBox = static_cast<float>(std::max(0, is.size));
        const float groupLen = (count > 0)
                ? count * iconBox + (count - 1) * static_cast<float>(std::max(0, is.spacing))
                : 0.0f;

        // ----- handle -----
        const SplitterHandleStyle& hs = style.handle;
        const bool hasHandle = (hs.shape != SplitterHandleShape::NoHandle) && hs.crossSize > 0;
        float handleAlongCenter = 0.0f;
        if (hasHandle) {
            float handleCross = std::min(static_cast<float>(hs.crossSize), crossExtent);
            float handleAlong = static_cast<float>((hs.axisLength > 0) ? hs.axisLength : hs.crossSize);
            if (hs.containsIcons && count > 0) {
                handleAlong = std::max(handleAlong, groupLen + 2.0f * std::max(0, is.padding));
            }
            handleAlong = std::min(handleAlong, alongExtent);
            if (handleAlong > 0.0f && handleCross > 0.0f) {
                float half = handleAlong / 2.0f;
                handleAlongCenter = std::clamp(hs.position * alongExtent, half, alongExtent - half);
                handleRect = makeRect(handleAlongCenter - half, handleAlong,
                                      (crossExtent - handleCross) / 2.0f, handleCross);
            }
        }

        // ----- icons -----
        if (count == 0 || iconBox <= 0.0f || groupLen > alongExtent) return;
        float iconCross = std::min(iconBox, crossExtent);

        float groupCenter;
        if (hasHandle && hs.containsIcons && handleRect.width > 0.0f) {
            groupCenter = handleAlongCenter;
        } else {
            float half = groupLen / 2.0f;
            groupCenter = std::clamp(is.position * alongExtent, half, alongExtent - half);
        }
        float alongPos = groupCenter - groupLen / 2.0f;

        const float grow = static_cast<float>(std::max(0, is.hoverPadding));
        for (size_t i = 0; i < visible.size(); ++i) {
            Rect2Df box = makeRect(alongPos, iconBox, (crossExtent - iconCross) / 2.0f, iconCross);
            Rect2Df hit(box.x - grow, box.y - grow, box.width + 2 * grow, box.height + 2 * grow);
            // Keep the click target inside the strip so it never steals pane clicks.
            hit.x = std::max(hit.x, 0.0f);
            hit.y = std::max(hit.y, 0.0f);
            hit.width  = std::min(hit.width,  w - hit.x);
            hit.height = std::min(hit.height, h - hit.y);

            iconRects.push_back(box);
            iconHitRects.push_back(hit);
            iconIndices.push_back(visible[i]);
            alongPos += iconBox + std::max(0, is.spacing);
        }
    }

    int UltraCanvasSplitter::HitTestIcon(const Point2Df& localPoint) const {
        for (size_t i = 0; i < iconHitRects.size(); ++i) {
            if (iconHitRects[i].Contains(localPoint)) return static_cast<int>(iconIndices[i]);
        }
        return -1;
    }

    bool UltraCanvasSplitter::IsIconActionable(int iconIndex) const {
        if (iconIndex < 0 || !owner) return false;
        const SplitPaneIcon* icon = owner->GetSplitterIcon(index, static_cast<size_t>(iconIndex));
        return icon && icon->visible && icon->enabled;
    }

    void UltraCanvasSplitter::SetHoveredIcon(int iconIndex, const UCEvent& event) {
        if (iconIndex == hoveredIcon) return;
        hoveredIcon = iconIndex;

        // The application only raises tooltips on element enter, so per-icon
        // tooltips have to be driven from here as the pointer crosses icons.
        const SplitPaneIcon* icon = (iconIndex >= 0 && owner)
                ? owner->GetSplitterIcon(index, static_cast<size_t>(iconIndex)) : nullptr;
        const std::string tip = (icon && !icon->tooltip.empty()) ? icon->tooltip : std::string();
        SetTooltip(tip);
        if (tip.empty()) {
            UltraCanvasTooltipManager::HideTooltip();
        } else if (auto* win = GetWindow()) {
            UltraCanvasTooltipManager::UpdateAndShowTooltip(win, tip, event.pointerWindow);
        }
        RequestRedraw();
    }

// ---------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------

    void UltraCanvasSplitter::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        UpdateGeometry();

        if (style.showSplitterBackground) {
            Color fill = style.splitterColor;
            if (dragging)         fill = style.splitterActiveColor;
            else if (IsHovered()) fill = style.splitterHoverColor;

            // Only the line itself is painted; the hit margin and any extra room
            // taken by the handle/icons stay transparent. With no handle, no icons
            // and no hit margin the band is the whole element, as it always was.
            const bool horizontal = (orientation == SplitOrientation::Horizontal);
            float crossExtent = horizontal ? GetWidth() : GetHeight();
            float lineCross = std::min(static_cast<float>(std::max(0, style.splitterThickness)), crossExtent);
            float crossPos = (crossExtent - lineCross) / 2.0f;

            ctx->SetFillPaint(fill);
            ctx->FillRectangle(horizontal
                               ? Rect2Dd(crossPos, 0, lineCross, GetHeight())
                               : Rect2Dd(0, crossPos, GetWidth(), lineCross));
        }

        DrawHandle(ctx);
        DrawIcons(ctx);
    }

    void UltraCanvasSplitter::DrawHandle(IRenderContext* ctx) {
        if (handleRect.width <= 0.0f || handleRect.height <= 0.0f) return;

        const SplitterHandleStyle& hs = style.handle;
        Color fill = hs.color;
        if (dragging)         fill = hs.activeColor;
        else if (IsHovered()) fill = hs.hoverColor;

        Rect2Dd r(handleRect.x, handleRect.y, handleRect.width, handleRect.height);
        double radius = 0.0;
        if (hs.shape == SplitterHandleShape::RoundedSquare) {
            radius = std::max(0.0f, hs.cornerRadius);
        } else if (hs.shape == SplitterHandleShape::Round) {
            radius = std::min(r.width, r.height) / 2.0;   // circle when square, capsule when not
        }
        radius = std::min(radius, std::min(r.width, r.height) / 2.0);

        ctx->SetFillPaint(fill);
        if (radius > 0.0) ctx->FillRoundedRectangle(r, radius);
        else              ctx->FillRectangle(r);

        if (hs.borderColor.a != 0 && hs.borderWidth > 0.0f) {
            ctx->SetStrokePaint(hs.borderColor);
            ctx->SetStrokeWidth(hs.borderWidth);
            if (radius > 0.0) ctx->DrawRoundedRectangle(r, radius);
            else              ctx->DrawRectangle(r);
        }

        // Grip lines only when the handle is not carrying the icons.
        bool carriesIcons = hs.containsIcons && !iconRects.empty();
        if (!hs.showGrip || carriesIcons || hs.gripLineCount <= 0) return;

        const bool horizontal = (orientation == SplitOrientation::Horizontal);
        float alongLen  = horizontal ? handleRect.height : handleRect.width;
        float crossLen  = horizontal ? handleRect.width  : handleRect.height;
        float thickness = std::max(1.0f, hs.gripThickness);
        float spacing   = static_cast<float>(std::max(0, hs.gripSpacing));
        int   lines     = hs.gripLineCount;

        float total = lines * thickness + (lines - 1) * spacing;
        if (total > alongLen) return;
        float gripLen = std::max(2.0f, crossLen * 0.5f);
        float alongStart = (horizontal ? handleRect.y : handleRect.x) + (alongLen - total) / 2.0f;
        float crossPos = (horizontal ? handleRect.x : handleRect.y) + (crossLen - gripLen) / 2.0f;

        ctx->SetFillPaint(hs.gripColor);
        for (int i = 0; i < lines; ++i) {
            float a = alongStart + i * (thickness + spacing);
            ctx->FillRectangle(horizontal ? Rect2Dd(crossPos, a, gripLen, thickness)
                                          : Rect2Dd(a, crossPos, thickness, gripLen));
        }
    }

    void UltraCanvasSplitter::DrawIcons(IRenderContext* ctx) {
        if (iconRects.empty() || !owner) return;
        const SplitterIconStyle& is = style.icons;
        const auto& icons = owner->GetSplitterIcons(index);

        for (size_t i = 0; i < iconRects.size(); ++i) {
            size_t oi = iconIndices[i];
            if (oi >= icons.size()) continue;
            const SplitPaneIcon& icon = icons[oi];
            const int oiInt = static_cast<int>(oi);

            // Hover / pressed background
            Color bg = Colors::Transparent;
            if (icon.enabled && hoveredIcon == oiInt) {
                bg = (pressedIcon == oiInt) ? is.pressedBackgroundColor : is.hoverBackgroundColor;
            }
            if (bg.a != 0) {
                Rect2Df hr = iconHitRects[i];
                ctx->SetFillPaint(bg);
                ctx->FillRoundedRectangle(Rect2Dd(hr.x, hr.y, hr.width, hr.height),
                                          std::max(0.0f, is.hoverCornerRadius));
            }

            Color tint = (icon.iconColor.a != 0) ? icon.iconColor : is.iconColor;
            if (!icon.enabled)                                        tint = is.iconDisabledColor;
            else if (hoveredIcon == oiInt && icon.iconColor.a == 0)   tint = is.iconHoverColor;

            Rect2Df br = iconRects[i];
            Rect2Dd box(br.x, br.y, br.width, br.height);

            if (!icon.iconPath.empty()) {
                if (!icon.enabled) {
                    ctx->PushState();
                    ctx->SetAlpha(0.45);
                }
                if (is.useIconAsMask) {
                    ctx->DrawMask(tint, icon.iconPath, box, ImageFitMode::Contain);
                } else {
                    ctx->DrawImage(icon.iconPath, box, ImageFitMode::Contain);
                }
                if (!icon.enabled) ctx->PopState();
            } else if (!icon.label.empty()) {
                ctx->SetFontFace(is.labelFontFamily, FontWeight::Bold, FontSlant::Normal);
                ctx->SetFontSize(is.labelFontSize);
                Size2Di ts = ctx->GetTextLineDimensions(icon.label);
                ctx->SetTextPaint(tint);
                ctx->DrawText(icon.label,
                              Point2Dd(box.x + (box.width - ts.width) / 2.0,
                                       box.y + (box.height - ts.height) / 2.0));
            }
        }
    }

// ---------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------

    bool UltraCanvasSplitter::OnEvent(const UCEvent& event) {
        switch (event.type) {
            case UCEventType::MouseEnter:
                SetHovered(true);
                RequestRedraw();
                return true;

            case UCEventType::MouseLeave:
                SetHovered(false);
                if (hoveredIcon != -1) {
                    hoveredIcon = -1;
                    SetTooltip("");
                    UltraCanvasTooltipManager::HideTooltip();
                }
                pressedIcon = -1;
                SetMouseCursor(orientation == SplitOrientation::Horizontal
                               ? UCMouseCursor::SizeWE
                               : UCMouseCursor::SizeNS);
                RequestRedraw();
                return true;

            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left && owner) {
                    // An icon click must not start a drag.
                    UpdateGeometry();
                    int hit = HitTestIcon(Point2Df(event.pointer.x, event.pointer.y));
                    if (IsIconActionable(hit)) {
                        pressedIcon = hit;
                        // A handler may swap this icon out (collapse -> restore), which
                        // would leave the old tooltip on screen describing an icon that
                        // is no longer there.
                        SetTooltip("");
                        UltraCanvasTooltipManager::HideTooltip();
                        // Capture so the release comes back here even if the pointer
                        // slips off the icon; the application drops it on MouseUp.
                        if (auto* app = UltraCanvasApplication::GetInstance()) {
                            app->CaptureMouse(this);
                        }
                        RequestRedraw();
                        return true;
                    }
                    if (hit >= 0) return true;   // disabled icon swallows the press
                    dragging = true;
                    dragStartGlobalAxis = (orientation == SplitOrientation::Horizontal)
                                          ? event.pointerGlobal.x
                                          : event.pointerGlobal.y;
                    if (auto* app = UltraCanvasApplication::GetInstance()) {
                        app->CaptureMouse(this);
                    }
                    owner->BeginSplitterDrag(index);
                    RequestRedraw();
                    return true;
                }
                return false;

            case UCEventType::MouseMove:
                if (!dragging) {
                    UpdateGeometry();
                    int hit = HitTestIcon(Point2Df(event.pointer.x, event.pointer.y));
                    SetHoveredIcon(hit, event);
                    SetMouseCursor(IsIconActionable(hit)
                                   ? UCMouseCursor::Hand
                                   : (orientation == SplitOrientation::Horizontal
                                      ? UCMouseCursor::SizeWE
                                      : UCMouseCursor::SizeNS));
                    if (hit >= 0) return true;
                }
                if (dragging && owner) {
                    int currentAxis = (orientation == SplitOrientation::Horizontal)
                                      ? event.pointerGlobal.x
                                      : event.pointerGlobal.y;
                    int delta = currentAxis - dragStartGlobalAxis;
                    owner->UpdateSplitterDrag(index, delta);
                    // Force a serviced relayout+paint this frame, independent of any
                    // stale arrangeValid left by concurrent (e.g. debug-session)
                    // invalidations that break the upward InvalidateLayout bubble.
                    // Invalidating the WINDOW (the root) cannot be defeated by the
                    // Element::InvalidateLayout early-return since it has no parent to
                    // bubble to; UpdateAndRender then re-Arranges down to PerformLayout,
                    // applying the new pane weights — the same geometry pass a resize gets.
                    if (auto* w = owner->GetWindow()) w->InvalidateLayout();
                    RequestRedraw();
                    return true;
                }
                return false;

            case UCEventType::MouseUp:
                if (dragging) {
                    dragging = false;
                    if (owner) {
                        owner->EndSplitterDrag(index);
                        // Guarantee the final committed geometry is laid out and painted.
                        if (auto* w = owner->GetWindow()) w->InvalidateLayout();
                    }
                    RequestRedraw();
                    return true;
                }
                if (pressedIcon >= 0) {
                    int pressed = pressedIcon;
                    pressedIcon = -1;
                    UpdateGeometry();
                    int hit = HitTestIcon(Point2Df(event.pointer.x, event.pointer.y));
                    // Forget the hover so the next move re-resolves it (and the
                    // tooltip) against whatever the handler left on the line.
                    hoveredIcon = -1;
                    RequestRedraw();
                    // Fire only when the release lands on the icon that was pressed.
                    if (hit == pressed && owner && IsIconActionable(pressed)) {
                        owner->TriggerSplitterIcon(index, static_cast<size_t>(pressed));
                    }
                    return true;
                }
                return false;

            default:
                break;
        }
        return UltraCanvasUIElement::OnEvent(event);
    }

// =====================================================================
// UltraCanvasSplitPane
// =====================================================================

    UltraCanvasSplitPane::UltraCanvasSplitPane(const std::string& id, float x, float y, float w, float h,
                                               SplitOrientation orient)
            : UltraCanvasContainer(id, x, y, w, h),
              orientation(orient) {
        ContainerStyle cs = GetContainerStyle();
        cs.autoShowScrollbars = false;
        cs.forceShowVerticalScrollbar = false;
        cs.forceShowHorizontalScrollbar = false;
        SetContainerStyle(cs);
    }

    void UltraCanvasSplitPane::SetBounds(const Rect2Df& b) {
        UltraCanvasContainer::SetBounds(b);
        InvalidateLayout();
    }

    void UltraCanvasSplitPane::PushStyleToSplitters() {
        for (auto& sp : splitters) sp->SetStyle(splitStyle);
    }

    void UltraCanvasSplitPane::SetSplitPaneStyle(const SplitPaneStyle& s) {
        splitStyle = s;
        PushStyleToSplitters();
        InvalidateLayout();
    }

    void UltraCanvasSplitPane::SetSplitterHandleStyle(const SplitterHandleStyle& s) {
        splitStyle.handle = s;
        PushStyleToSplitters();
        InvalidateLayout();
    }

    void UltraCanvasSplitPane::SetSplitterHandleShape(SplitterHandleShape shape, int crossSize,
                                                      int axisLength, float cornerRadius) {
        splitStyle.handle.shape = shape;
        splitStyle.handle.crossSize = std::max(0, crossSize);
        splitStyle.handle.axisLength = axisLength;
        splitStyle.handle.cornerRadius = cornerRadius;
        PushStyleToSplitters();
        InvalidateLayout();
    }

    void UltraCanvasSplitPane::SetSplitterIconStyle(const SplitterIconStyle& s) {
        splitStyle.icons = s;
        PushStyleToSplitters();
        InvalidateLayout();
    }

// ===== SPLITTER ACTION ICONS =====

    const std::vector<SplitPaneIcon>& UltraCanvasSplitPane::GetSplitterIcons(size_t splitterIndex) const {
        static const std::vector<SplitPaneIcon> empty;
        if (splitterIndex >= splitterIcons.size()) return empty;
        return splitterIcons[splitterIndex];
    }

    const SplitPaneIcon* UltraCanvasSplitPane::GetSplitterIcon(size_t splitterIndex, size_t iconIndex) const {
        const auto& icons = GetSplitterIcons(splitterIndex);
        if (iconIndex >= icons.size()) return nullptr;
        return &icons[iconIndex];
    }

    size_t UltraCanvasSplitPane::SplitterIconCount(size_t splitterIndex) const {
        return GetSplitterIcons(splitterIndex).size();
    }

    void UltraCanvasSplitPane::SetSplitterIcons(size_t splitterIndex, const std::vector<SplitPaneIcon>& icons) {
        if (splitterIndex >= splitterIcons.size()) return;
        splitterIcons[splitterIndex] = icons;
        InvalidateLayout();
    }

    size_t UltraCanvasSplitPane::AddSplitterIcon(size_t splitterIndex, const SplitPaneIcon& icon) {
        if (splitterIndex >= splitterIcons.size()) return 0;
        splitterIcons[splitterIndex].push_back(icon);
        InvalidateLayout();
        return splitterIcons[splitterIndex].size() - 1;
    }

    bool UltraCanvasSplitPane::InsertSplitterIcon(size_t splitterIndex, size_t iconIndex,
                                                  const SplitPaneIcon& icon) {
        if (splitterIndex >= splitterIcons.size()) return false;
        auto& icons = splitterIcons[splitterIndex];
        if (iconIndex > icons.size()) iconIndex = icons.size();
        icons.insert(icons.begin() + iconIndex, icon);
        InvalidateLayout();
        return true;
    }

    bool UltraCanvasSplitPane::RemoveSplitterIcon(size_t splitterIndex, size_t iconIndex) {
        if (splitterIndex >= splitterIcons.size()) return false;
        auto& icons = splitterIcons[splitterIndex];
        if (iconIndex >= icons.size()) return false;
        icons.erase(icons.begin() + iconIndex);
        InvalidateLayout();
        return true;
    }

    void UltraCanvasSplitPane::ClearSplitterIcons(size_t splitterIndex) {
        if (splitterIndex >= splitterIcons.size()) return;
        if (splitterIcons[splitterIndex].empty()) return;
        splitterIcons[splitterIndex].clear();
        InvalidateLayout();
    }

    void UltraCanvasSplitPane::ClearAllSplitterIcons() {
        for (auto& icons : splitterIcons) icons.clear();
        InvalidateLayout();
    }

    void UltraCanvasSplitPane::SetSplitterIconEnabled(size_t splitterIndex, size_t iconIndex, bool enabled) {
        if (splitterIndex >= splitterIcons.size()) return;
        auto& icons = splitterIcons[splitterIndex];
        if (iconIndex >= icons.size() || icons[iconIndex].enabled == enabled) return;
        icons[iconIndex].enabled = enabled;
        RequestRedraw();
    }

    void UltraCanvasSplitPane::SetSplitterIconVisible(size_t splitterIndex, size_t iconIndex, bool visible) {
        if (splitterIndex >= splitterIcons.size()) return;
        auto& icons = splitterIcons[splitterIndex];
        if (iconIndex >= icons.size() || icons[iconIndex].visible == visible) return;
        icons[iconIndex].visible = visible;
        InvalidateLayout();   // the group length, and possibly the strip width, changed
    }

    void UltraCanvasSplitPane::SetSplitterIconTooltip(size_t splitterIndex, size_t iconIndex,
                                                      const std::string& tooltip) {
        if (splitterIndex >= splitterIcons.size()) return;
        auto& icons = splitterIcons[splitterIndex];
        if (iconIndex >= icons.size()) return;
        icons[iconIndex].tooltip = tooltip;
    }

    void UltraCanvasSplitPane::SetSplitterIconPath(size_t splitterIndex, size_t iconIndex,
                                                   const std::string& iconPath) {
        if (splitterIndex >= splitterIcons.size()) return;
        auto& icons = splitterIcons[splitterIndex];
        if (iconIndex >= icons.size() || icons[iconIndex].iconPath == iconPath) return;
        icons[iconIndex].iconPath = iconPath;
        RequestRedraw();
    }

    void UltraCanvasSplitPane::TriggerSplitterIcon(size_t splitterIndex, size_t iconIndex) {
        if (splitterIndex >= splitterIcons.size()) return;
        const auto& icons = splitterIcons[splitterIndex];
        if (iconIndex >= icons.size()) return;
        // Copy: a handler may mutate the icon list (add/remove icons, swap a pane).
        SplitPaneIcon icon = icons[iconIndex];
        if (!icon.enabled || !icon.visible) return;
        if (icon.onClick) icon.onClick(splitterIndex, iconIndex);
        if (onSplitterIconClicked) onSplitterIconClicked(splitterIndex, iconIndex, icon);
        // Handlers commonly retune weights or icon visibility. Invalidate the
        // window for the same reason the drag path does: the upward
        // InvalidateLayout bubble can be defeated by a stale arrangeValid, and
        // the root has no parent to bubble to.
        if (auto* w = GetWindow()) w->InvalidateLayout();
        RequestRedraw();
    }

// ===== LAYOUT METRICS =====

    int UltraCanvasSplitPane::EffectiveSplitterThickness() const {
        int thickness = std::max(0, splitStyle.splitterThickness) +
                        2 * std::max(0, splitStyle.splitterHitMargin);

        if (splitStyle.handle.shape != SplitterHandleShape::NoHandle) {
            thickness = std::max(thickness, std::max(0, splitStyle.handle.crossSize));
        }
        for (const auto& icons : splitterIcons) {
            for (const auto& icon : icons) {
                if (!icon.visible) continue;
                thickness = std::max(thickness, std::max(0, splitStyle.icons.size) +
                                                2 * std::max(0, splitStyle.icons.padding));
                break;   // one visible icon is enough; all icons share a size
            }
        }
        return thickness;
    }

    int UltraCanvasSplitPane::AxisLength() const {
        auto area = const_cast<UltraCanvasSplitPane*>(this)->GetContentArea();
        return (orientation == SplitOrientation::Horizontal) ? area.width : area.height;
    }

    std::shared_ptr<UltraCanvasContainer> UltraCanvasSplitPane::AddPane(double weight) {
        return InsertPane(panes.size(), weight);
    }

    std::shared_ptr<UltraCanvasContainer> UltraCanvasSplitPane::InsertPane(size_t index, double weight) {
        if (index > panes.size()) index = panes.size();

        std::string paneId = GetIdentifier() + "_pane_" + std::to_string(panes.size());
        auto pane = std::make_shared<UltraCanvasContainer>(paneId);

        PaneSlot slot;
        slot.pane = pane;
        slot.weight = (weight > 0.0) ? weight : 1.0;
        panes.insert(panes.begin() + index, slot);

        // Keep each existing splitter's icons on the same split line: the new pane
        // introduces a fresh (empty) splitter slot at `index`, everything after it
        // shifts along.
        if (panes.size() > 1) {
            size_t at = std::min(index, splitterIcons.size());
            splitterIcons.insert(splitterIcons.begin() + at, std::vector<SplitPaneIcon>());
        }

        AddChild(pane);
        EnsureSplitterCountMatches();
        RebindSplitterIndices();
        InvalidateLayout();
        return pane;
    }

    void UltraCanvasSplitPane::RemovePane(size_t index) {
        if (index >= panes.size()) return;
        auto paneShared = panes[index].pane;
        panes.erase(panes.begin() + index);
        // Drop the icons of the split line that disappears with the pane.
        if (!splitterIcons.empty()) {
            size_t at = std::min(index, splitterIcons.size() - 1);
            splitterIcons.erase(splitterIcons.begin() + at);
        }
        if (paneShared) UltraCanvasContainer::RemoveChild(paneShared);
        EnsureSplitterCountMatches();
        RebindSplitterIndices();
        InvalidateLayout();
    }

    void UltraCanvasSplitPane::RemovePane(UltraCanvasContainer* pane) {
        int idx = GetPaneIndex(pane);
        if (idx >= 0) RemovePane(static_cast<size_t>(idx));
    }

    std::shared_ptr<UltraCanvasContainer> UltraCanvasSplitPane::GetPane(size_t index) const {
        if (index >= panes.size()) return nullptr;
        return panes[index].pane;
    }

    int UltraCanvasSplitPane::GetPaneIndex(UltraCanvasContainer* pane) const {
        for (size_t i = 0; i < panes.size(); ++i) {
            if (panes[i].pane.get() == pane) return static_cast<int>(i);
        }
        return -1;
    }

    void UltraCanvasSplitPane::SetPaneWeight(size_t index, double weight) {
        if (index >= panes.size()) return;
        panes[index].weight = (weight > 0.0) ? weight : 0.0001;
        InvalidateLayout();
    }

    double UltraCanvasSplitPane::GetPaneWeight(size_t index) const {
        if (index >= panes.size()) return 0.0;
        return panes[index].weight;
    }

    void UltraCanvasSplitPane::SetPaneMinSize(size_t index, int px) {
        if (index >= panes.size()) return;
        panes[index].minSize = std::max(0, px);
        InvalidateLayout();
    }

    void UltraCanvasSplitPane::SetPaneMaxSize(size_t index, int px) {
        if (index >= panes.size()) return;
        panes[index].maxSize = std::max(0, px);
        InvalidateLayout();
    }

    void UltraCanvasSplitPane::SetPaneSizes(const std::vector<int>& pixelSizes) {
        if (pixelSizes.empty() || pixelSizes.size() != panes.size()) return;
        double total = 0.0;
        for (int s : pixelSizes) total += std::max(0, s);
        if (total <= 0.0) return;
        for (size_t i = 0; i < panes.size(); ++i) {
            double w = static_cast<double>(std::max(0, pixelSizes[i])) / total;
            panes[i].weight = (w > 0.0) ? w : 0.0001;
        }
        InvalidateLayout();
    }

    void UltraCanvasSplitPane::EnsureSplitterCountMatches() {
        size_t needed = (panes.size() > 0) ? panes.size() - 1 : 0;
        while (splitters.size() > needed) {
            auto sp = splitters.back();
            splitters.pop_back();
            UltraCanvasContainer::RemoveChild(sp);
        }
        while (splitters.size() < needed) {
            std::string sid = GetIdentifier() + "_splitter_" + std::to_string(splitters.size());
            auto sp = std::make_shared<UltraCanvasSplitter>(sid, orientation, this);
            sp->SetStyle(splitStyle);
            sp->SetZIndex(OverlayZOrder::Controls);
            splitters.push_back(sp);
            AddChild(sp);
        }
        if (splitterIcons.size() != splitters.size()) splitterIcons.resize(splitters.size());
    }

    void UltraCanvasSplitPane::RebindSplitterIndices() {
        for (size_t i = 0; i < splitters.size(); ++i) {
            splitters[i]->SetIndex(i);
        }
    }

    std::vector<int> UltraCanvasSplitPane::ComputePaneSizes(int availableAxis) const {
        std::vector<int> sizes(panes.size(), 0);
        if (panes.empty() || availableAxis <= 0) return sizes;

        double totalWeight = 0.0;
        for (const auto& p : panes) totalWeight += p.weight;
        if (totalWeight <= 0.0) totalWeight = static_cast<double>(panes.size());

        // Initial proportional distribution
        int assigned = 0;
        for (size_t i = 0; i < panes.size(); ++i) {
            int s = static_cast<int>(std::round(availableAxis * panes[i].weight / totalWeight));
            sizes[i] = s;
            assigned += s;
        }
        // Push rounding drift onto the last pane
        if (!sizes.empty()) {
            sizes.back() += (availableAxis - assigned);
        }

        // Single-pass clamp; redistribute slack onto unclamped neighbours
        std::vector<bool> clamped(panes.size(), false);
        for (size_t i = 0; i < panes.size(); ++i) {
            int s = sizes[i];
            int minS = panes[i].minSize;
            int maxS = (panes[i].maxSize > 0) ? panes[i].maxSize : availableAxis;
            if (s < minS) { sizes[i] = minS; clamped[i] = true; }
            else if (s > maxS) { sizes[i] = maxS; clamped[i] = true; }
        }

        int sum = 0;
        for (int s : sizes) sum += s;
        int delta = availableAxis - sum;
        if (delta != 0) {
            int unclampedCount = 0;
            for (bool c : clamped) if (!c) ++unclampedCount;
            if (unclampedCount == 0) {
                // All clamped: dump residual on the last pane
                sizes.back() += delta;
                if (sizes.back() < 0) sizes.back() = 0;
            } else {
                int per = delta / unclampedCount;
                int rem = delta - per * unclampedCount;
                for (size_t i = 0; i < panes.size(); ++i) {
                    if (clamped[i]) continue;
                    sizes[i] += per;
                    if (rem != 0) {
                        int step = (rem > 0) ? 1 : -1;
                        sizes[i] += step;
                        rem -= step;
                    }
                    if (sizes[i] < 0) sizes[i] = 0;
                }
            }
        }

        return sizes;
    }

    void UltraCanvasSplitPane::PerformLayout(const CSSLayout::LayoutContext& ctx) {
        EnsureSplitterCountMatches();
        RebindSplitterIndices();

        Rect2Di area = GetContentArea();
        int axisLen = (orientation == SplitOrientation::Horizontal) ? area.width : area.height;
        int crossLen = (orientation == SplitOrientation::Horizontal) ? area.height : area.width;
        int splitterThickness = EffectiveSplitterThickness();
        int splittersTotal = static_cast<int>(splitters.size()) * splitterThickness;
        int available = std::max(0, axisLen - splittersTotal);

        auto sizes = ComputePaneSizes(available);

        // Size each child through the CSS engine (Measure+Arrange) rather than SetBounds, so
        // each pane's Container::Arrange -> UpdateScrollability runs at its real split size and
        // shows scrollbars when its content overflows.
        auto arrangeChild = [&ctx](UltraCanvasUIElement* child, const Rect2Di& r) {
            if (!child) return;
            Rect2Df rf(r.x, r.y, r.width, r.height);
            CSSLayout::MeasureConstraints mc{
                    { CSSLayout::ConstraintMode::Exact, rf.width },
                    { CSSLayout::ConstraintMode::Exact, rf.height } };
            child->Measure(mc, ctx);
            child->Arrange(rf, ctx);
        };

        int axisOffset = 0;
        for (size_t i = 0; i < panes.size(); ++i) {
            int paneAxis = sizes[i];
            Rect2Di r;
            if (orientation == SplitOrientation::Horizontal) {
                r = Rect2Di(area.x + axisOffset, area.y, paneAxis, crossLen);
            } else {
                r = Rect2Di(area.x, area.y + axisOffset, crossLen, paneAxis);
            }
            arrangeChild(panes[i].pane.get(), r);
            axisOffset += paneAxis;

            if (i < splitters.size()) {
                Rect2Di sr;
                if (orientation == SplitOrientation::Horizontal) {
                    sr = Rect2Di(area.x + axisOffset, area.y, splitterThickness, crossLen);
                } else {
                    sr = Rect2Di(area.x, area.y + axisOffset, crossLen, splitterThickness);
                }
                arrangeChild(splitters[i].get(), sr);
                axisOffset += splitterThickness;
            }
        }
    }

    void UltraCanvasSplitPane::Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) {
        UltraCanvasContainer::Arrange(finalRect, ctx);
        PerformLayout(ctx);
    }

    void UltraCanvasSplitPane::BeginSplitterDrag(size_t splitterIndex) {
        if (splitterIndex >= splitters.size()) return;
        dragStartPaneSizes.clear();
        dragStartPaneSizes.reserve(panes.size());
        for (auto& slot : panes) {
            if (!slot.pane) { dragStartPaneSizes.push_back(0); continue; }
            int axisSize = (orientation == SplitOrientation::Horizontal)
                           ? slot.pane->GetWidth()
                           : slot.pane->GetHeight();
            dragStartPaneSizes.push_back(axisSize);
        }
        if (onSplitterDragStart) onSplitterDragStart(splitterIndex);
    }

    void UltraCanvasSplitPane::UpdateSplitterDrag(size_t splitterIndex, int delta) {
        if (splitterIndex >= splitters.size()) return;
        size_t a = splitterIndex;
        size_t b = splitterIndex + 1;
        if (b >= panes.size() || dragStartPaneSizes.size() != panes.size()) return;

        int startA = dragStartPaneSizes[a];
        int startB = dragStartPaneSizes[b];
        int pairTotal = startA + startB;

        int newA = startA + delta;

        int minA = panes[a].minSize;
        int maxA = (panes[a].maxSize > 0) ? panes[a].maxSize : pairTotal;
        int minB = panes[b].minSize;
        int maxB = (panes[b].maxSize > 0) ? panes[b].maxSize : pairTotal;

        // Clamp newA against its own min/max, and against what's feasible for B
        int lower = std::max(minA, pairTotal - maxB);
        int upper = std::min(maxA, pairTotal - minB);
        if (lower > upper) {
            // Constraints contradictory: prefer satisfying B's min
            newA = std::max(0, pairTotal - minB);
        } else {
            newA = std::clamp(newA, lower, upper);
        }
        int newB = pairTotal - newA;
        if (newA < 0) newA = 0;
        if (newB < 0) newB = 0;

        // Convert all current pane sizes to weights (only a and b changed)
        std::vector<int> currentSizes = dragStartPaneSizes;
        currentSizes[a] = newA;
        currentSizes[b] = newB;

        double totalPx = 0.0;
        for (int s : currentSizes) totalPx += s;
        if (totalPx <= 0.0) return;

        double totalWeight = 0.0;
        for (const auto& p : panes) totalWeight += p.weight;
        if (totalWeight <= 0.0) totalWeight = static_cast<double>(panes.size());

        // Scale: keep total weight constant; redistribute proportionally to new px sizes
        for (size_t i = 0; i < panes.size(); ++i) {
            double w = (currentSizes[i] / totalPx) * totalWeight;
            panes[i].weight = (w > 0.0) ? w : 0.0001;
        }

        InvalidateLayout();

        if (onWeightsChanged) {
            std::vector<double> weights;
            weights.reserve(panes.size());
            for (const auto& p : panes) weights.push_back(p.weight);
            onWeightsChanged(weights);
        }
    }

    void UltraCanvasSplitPane::EndSplitterDrag(size_t splitterIndex) {
        dragStartPaneSizes.clear();
        if (onSplitterDragEnd) onSplitterDragEnd(splitterIndex);
    }

} // namespace UltraCanvas
