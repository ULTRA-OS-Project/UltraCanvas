// core/UltraCanvasEmbeddedWindowManager.cpp
// The in-program window manager: stacking, focus, placement, moving and
// resizing, hit testing, and compositing onto one output. No platform code
// and no element code - the frame windows that carry the title bars are the
// backend's (UltraCanvasEmbeddedWindowFrame), this keeps their geometry.
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include "UltraCanvasEmbeddedWindowManager.h"

#include <cairo/cairo.h>

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    namespace {
        // How far a decorated window's shadow reaches beyond it.
        constexpr int kShadow = 10;
        constexpr double kPi = 3.14159265358979323846;

        // std::clamp with bounds that may cross (a window wider than the
        // output): the lower bound wins.
        int ClampSafe(int v, int lo, int hi) { return std::max(lo, std::min(v, std::max(lo, hi))); }

        bool RectEmpty(const Rect2Di& r) { return r.width <= 0 || r.height <= 0; }

        Rect2Di Unite(const Rect2Di& a, const Rect2Di& b) {
            if (RectEmpty(a)) return b;
            if (RectEmpty(b)) return a;
            const int l = std::min(a.x, b.x);
            const int t = std::min(a.y, b.y);
            const int r = std::max(a.x + a.width, b.x + b.width);
            const int btm = std::max(a.y + a.height, b.y + b.height);
            return Rect2Di(l, t, r - l, btm - t);
        }

        bool Overlap(const Rect2Di& a, const Rect2Di& b) {
            return a.x < b.x + b.width && b.x < a.x + a.width &&
                   a.y < b.y + b.height && b.y < a.y + a.height;
        }

        Rect2Di Grow(const Rect2Di& r, int by) {
            return Rect2Di(r.x - by, r.y - by, r.width + 2 * by, r.height + 2 * by);
        }

        bool Inside(const Rect2Di& r, Point2Di p) {
            return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
        }
    }

    // ===== LOOKUP =====

    UltraCanvasEmbeddedWindowManager::Window* UltraCanvasEmbeddedWindowManager::Find(uint64_t id) {
        auto it = windows.find(id);
        return it == windows.end() ? nullptr : &it->second;
    }

    const UltraCanvasEmbeddedWindowManager::Window* UltraCanvasEmbeddedWindowManager::Find(uint64_t id) const {
        auto it = windows.find(id);
        return it == windows.end() ? nullptr : &it->second;
    }

    uint64_t UltraCanvasEmbeddedWindowManager::ClientOf(uint64_t id) const {
        const Window* w = Find(id);
        return (w && w->info.frameOf) ? w->info.frameOf : id;
    }

    Rect2Di UltraCanvasEmbeddedWindowManager::OuterRect(const Window& w) const {
        Rect2Di r = w.info.rect;
        if (w.info.frameId) {
            if (const Window* f = Find(w.info.frameId)) r = Unite(r, f->info.rect);
        }
        return r;
    }

    Rect2Di UltraCanvasEmbeddedWindowManager::DamageRect(const Window& w) const {
        return w.info.decorated ? Grow(OuterRect(w), kShadow) : w.info.rect;
    }

    // ===== OUTPUT =====

    void UltraCanvasEmbeddedWindowManager::SetOutputSize(int width, int height) {
        width = std::max(1, width);
        height = std::max(1, height);
        if (outputSize.width == width && outputSize.height == height) return;
        outputSize = Size2Di(width, height);
        for (auto& [id, w] : windows) {
            if (w.info.frameOf) continue;   // follows its client
            if (w.info.fillsOutput) {
                ApplyRect(w, Rect2Di(0, 0, width, height), true);
            } else if (w.info.maximized) {
                w.info.maximized = false;   // re-maximize into the new work area
                Maximize(id);
            } else {
                ApplyRect(w, ClampToOutput(w, w.info.rect), true);
            }
        }
        pointer.x = std::clamp(pointer.x, 0, width - 1);
        pointer.y = std::clamp(pointer.y, 0, height - 1);
        DamageAll();
    }

    void UltraCanvasEmbeddedWindowManager::ReserveEdges(uint64_t id, int left, int right, int top, int bottom) {
        if (left <= 0 && right <= 0 && top <= 0 && bottom <= 0) reserved.erase(id);
        else reserved[id] = Rect2Di(std::max(0, left), std::max(0, top),
                                    std::max(0, right), std::max(0, bottom));
        for (auto& [wid, w] : windows) {
            if (w.info.maximized) { w.info.maximized = false; Maximize(wid); }
        }
    }

    Rect2Di UltraCanvasEmbeddedWindowManager::GetWorkArea() const {
        int l = 0, t = 0, r = 0, b = 0;
        for (const auto& [id, edges] : reserved) {
            l = std::max(l, edges.x);
            t = std::max(t, edges.y);
            r = std::max(r, edges.width);
            b = std::max(b, edges.height);
        }
        const int w = std::max(1, outputSize.width - l - r);
        const int h = std::max(1, outputSize.height - t - b);
        return Rect2Di(l, t, w, h);
    }

    // ===== STACK =====

    void UltraCanvasEmbeddedWindowManager::PlaceInStack(uint64_t id) {
        stack.erase(std::remove(stack.begin(), stack.end(), id), stack.end());
        const Window* w = Find(id);
        if (!w) return;
        // Top of its band: before the first window of a higher band.
        auto pos = std::find_if(stack.begin(), stack.end(), [&](uint64_t other) {
            const Window* o = Find(other);
            return o && static_cast<int>(o->info.layer) > static_cast<int>(w->info.layer);
        });
        stack.insert(pos, id);
    }

    void UltraCanvasEmbeddedWindowManager::Restack(uint64_t id) {
        Window* w = Find(id);
        if (!w) return;
        PlaceInStack(id);
        if (w->info.frameId) PlaceInStack(w->info.frameId);
        // Its dialogs stay above it.
        std::vector<uint64_t> transients;
        for (uint64_t other : stack) {
            const Window* o = Find(other);
            if (o && o->info.transientFor == id && other != id) transients.push_back(other);
        }
        for (uint64_t t : transients) Restack(t);
        AddDamage(DamageRect(*w));
    }

    void UltraCanvasEmbeddedWindowManager::Raise(uint64_t id) {
        Restack(ClientOf(id));
    }

    // ===== WINDOWS =====

    Rect2Di UltraCanvasEmbeddedWindowManager::Constrain(const Window& w, Rect2Di rect) const {
        int minW = std::max(1, w.minSize.width), minH = std::max(1, w.minSize.height);
        rect.width = std::max(rect.width, minW);
        rect.height = std::max(rect.height, minH);
        if (w.maxSize.width > 0) rect.width = std::min(rect.width, w.maxSize.width);
        if (w.maxSize.height > 0) rect.height = std::min(rect.height, w.maxSize.height);
        return rect;
    }

    Rect2Di UltraCanvasEmbeddedWindowManager::ClampToOutput(const Window& w, Rect2Di rect) const {
        if (w.info.fillsOutput) return Rect2Di(0, 0, outputSize.width, outputSize.height);
        const int bar = w.info.decorated ? titleBarHeight : 0;
        const Rect2Di work = GetWorkArea();
        // Never larger than the work area (title bar included), where it fits.
        if (w.info.layer == EmbeddedWindowLayer::Normal || w.info.layer == EmbeddedWindowLayer::AlwaysOnTop) {
            rect.width = std::min(rect.width, std::max(w.minSize.width, work.width));
            rect.height = std::min(rect.height, std::max(w.minSize.height, work.height - bar));
        }
        // The title bar stays below the top of the work area, and part of the
        // window on the output on every side.
        const int keep = std::min(keepVisible, std::max(1, rect.width));
        rect.x = ClampSafe(rect.x, keep - rect.width, outputSize.width - keep);
        rect.y = ClampSafe(rect.y, bar > 0 ? work.y + bar : keep - rect.height,
                           std::max(0, outputSize.height - keep));
        return rect;
    }

    void UltraCanvasEmbeddedWindowManager::SyncFrame(Window& client, bool notify) {
        if (!client.info.frameId) return;
        Window* frame = Find(client.info.frameId);
        if (!frame) return;
        const Rect2Di want(client.info.rect.x, client.info.rect.y - titleBarHeight,
                           client.info.rect.width, titleBarHeight);
        if (frame->info.rect.x == want.x && frame->info.rect.y == want.y &&
            frame->info.rect.width == want.width && frame->info.rect.height == want.height) return;
        AddDamage(frame->info.rect);
        frame->info.rect = want;
        AddDamage(want);
        if (notify && onGeometryChanged) onGeometryChanged(frame->info.id, want);
    }

    void UltraCanvasEmbeddedWindowManager::ApplyRect(Window& w, const Rect2Di& rect, bool notify) {
        const Rect2Di& old = w.info.rect;
        if (old.x == rect.x && old.y == rect.y && old.width == rect.width && old.height == rect.height) {
            SyncFrame(w, notify);
            return;
        }
        if (IsShown(w)) AddDamage(DamageRect(w));
        w.info.rect = rect;
        SyncFrame(w, notify);
        if (IsShown(w)) AddDamage(DamageRect(w));
        if (notify && onGeometryChanged) onGeometryChanged(w.info.id, rect);
    }

    Rect2Di UltraCanvasEmbeddedWindowManager::AddWindow(uint64_t id, const EmbeddedWindowSpec& spec) {
        Window w;
        w.info.id = id;
        w.info.layer = spec.layer;
        w.info.fillsOutput = spec.fillsOutput;
        w.info.decorated = spec.decorated && !spec.fillsOutput;
        w.info.focusable = spec.focusable;
        w.info.transientFor = spec.transientFor;
        w.info.title = spec.title;
        w.resizable = spec.resizable && !spec.fillsOutput;
        w.minSize = spec.minSize;
        w.maxSize = spec.maxSize;

        Rect2Di rect = Constrain(w, spec.rect);
        if (spec.fillsOutput) {
            rect = Rect2Di(0, 0, outputSize.width, outputSize.height);
        } else if (rect.x < 0 || rect.y < 0) {
            const Rect2Di work = GetWorkArea();
            const int bar = w.info.decorated ? titleBarHeight : 0;
            const Window* parent = spec.transientFor ? Find(spec.transientFor) : nullptr;
            if (parent) {
                // Centred over the window it belongs to.
                rect.x = parent->info.rect.x + (parent->info.rect.width - rect.width) / 2;
                rect.y = parent->info.rect.y + (parent->info.rect.height - rect.height) / 2;
            } else if (w.info.layer == EmbeddedWindowLayer::Normal ||
                       w.info.layer == EmbeddedWindowLayer::AlwaysOnTop) {
                // Cascaded from near the work area's top left, centred when
                // it is too big to step.
                const int step = titleBarHeight;
                const int slots = std::max(1, std::min((work.width - rect.width) / step,
                                                       (work.height - bar - rect.height) / step));
                const int n = cascade++ % std::max(1, std::min(slots, 8));
                if (rect.width >= work.width - step || rect.height >= work.height - bar - step) {
                    rect.x = work.x + (work.width - rect.width) / 2;
                    rect.y = work.y + bar + (work.height - bar - rect.height) / 2;
                } else {
                    rect.x = work.x + step + n * step;
                    rect.y = work.y + bar + step / 2 + n * step;
                }
            } else {
                rect.x = std::max(rect.x, 0);
                rect.y = std::max(rect.y, 0);
            }
        }
        rect = ClampToOutput(w, rect);
        w.info.rect = rect;
        w.restoreRect = rect;
        windows[id] = w;
        PlaceInStack(id);
        return rect;
    }

    void UltraCanvasEmbeddedWindowManager::AttachFrame(uint64_t clientId, uint64_t frameId) {
        Window* client = Find(clientId);
        Window* frame = Find(frameId);
        if (!client || !frame || clientId == frameId) return;
        client->info.frameId = frameId;
        client->info.decorated = true;
        frame->info.frameOf = clientId;
        frame->info.layer = client->info.layer;
        frame->info.decorated = false;
        frame->info.focusable = false;
        frame->info.visible = client->info.visible;
        frame->info.minimized = client->info.minimized;
        SyncFrame(*client, true);
        Restack(clientId);
    }

    void UltraCanvasEmbeddedWindowManager::RemoveWindow(uint64_t id) {
        Window* w = Find(id);
        if (!w) return;
        if (IsShown(*w)) AddDamage(DamageRect(*w));
        const uint64_t frameId = w->info.frameId;
        const uint64_t frameOf = w->info.frameOf;
        if (frameOf) {
            if (Window* client = Find(frameOf)) client->info.frameId = 0;
        }
        for (auto& [oid, o] : windows) {
            if (o.info.transientFor == id) o.info.transientFor = 0;
        }
        reserved.erase(id);
        windows.erase(id);
        stack.erase(std::remove(stack.begin(), stack.end(), id), stack.end());
        if (hoverId == id) hoverId = 0;
        if (grabId == id) { grabId = 0; buttonsDown = 0; }
        if (interactionId == id) interaction = Interaction::None;
        if (frameId) {
            if (Window* f = Find(frameId)) f->info.frameOf = 0;   // the backend removes it
        }
        if (focusedId == id) FocusSomethingElse(id);
    }

    bool UltraCanvasEmbeddedWindowManager::GetInfo(uint64_t id, EmbeddedWindowInfo& out) const {
        const Window* w = Find(id);
        if (!w) return false;
        out = w->info;
        return true;
    }

    std::vector<EmbeddedWindowInfo> UltraCanvasEmbeddedWindowManager::ListWindows() const {
        std::vector<EmbeddedWindowInfo> out;
        out.reserve(stack.size());
        for (uint64_t id : stack) {
            if (const Window* w = Find(id)) out.push_back(w->info);
        }
        return out;
    }

    std::vector<EmbeddedWindowInfo> UltraCanvasEmbeddedWindowManager::ListApplicationWindows() const {
        std::vector<EmbeddedWindowInfo> out;
        for (uint64_t id : stack) {
            const Window* w = Find(id);
            if (!w || w->info.frameOf) continue;
            if (w->info.layer != EmbeddedWindowLayer::Normal &&
                w->info.layer != EmbeddedWindowLayer::AlwaysOnTop) continue;
            if (!w->info.focusable) continue;
            out.push_back(w->info);
        }
        return out;
    }

    void UltraCanvasEmbeddedWindowManager::Show(uint64_t id) {
        Window* w = Find(id);
        if (!w || w->info.visible) return;
        w->info.visible = true;
        if (w->info.frameId) {
            if (Window* f = Find(w->info.frameId)) f->info.visible = true;
        }
        Restack(ClientOf(id));
        AddDamage(DamageRect(*w));
        if (w->info.focusable && !w->info.frameOf && w->info.layer != EmbeddedWindowLayer::Desktop) {
            SetFocus(id);
        } else if (w->info.layer == EmbeddedWindowLayer::Desktop && focusedId == 0) {
            SetFocus(id);
        }
    }

    void UltraCanvasEmbeddedWindowManager::Hide(uint64_t id) {
        Window* w = Find(id);
        if (!w || !w->info.visible) return;
        if (IsShown(*w)) AddDamage(DamageRect(*w));
        w->info.visible = false;
        if (w->info.frameId) {
            if (Window* f = Find(w->info.frameId)) f->info.visible = false;
        }
        if (hoverId == id) hoverId = 0;
        if (grabId == id) { grabId = 0; buttonsDown = 0; }
        if (focusedId == id) FocusSomethingElse(id);
    }

    void UltraCanvasEmbeddedWindowManager::SetTitle(uint64_t id, const std::string& title) {
        if (Window* w = Find(id)) w->info.title = title;
    }

    void UltraCanvasEmbeddedWindowManager::SetRect(uint64_t id, const Rect2Di& rect) {
        Window* w = Find(id);
        if (!w || w->info.fillsOutput || w->info.frameOf) return;
        if (w->info.maximized) {
            w->info.maximized = false;
            if (onStateChanged) onStateChanged(id);
        }
        Rect2Di r = ClampToOutput(*w, Constrain(*w, rect));
        w->restoreRect = r;
        // The window asked for it: its own size change is already under way,
        // so only the frame hears about it.
        ApplyRect(*w, r, false);
        if (w->info.frameId) {
            if (Window* f = Find(w->info.frameId)) {
                if (onGeometryChanged) onGeometryChanged(f->info.id, f->info.rect);
            }
        }
    }

    void UltraCanvasEmbeddedWindowManager::SetFocus(uint64_t id) {
        if (id == focusedId) return;
        const uint64_t lost = focusedId;
        focusedId = id;
        // The title bars show the change.
        for (uint64_t changed : {lost, id}) {
            if (const Window* w = Find(changed)) {
                AddDamage(DamageRect(*w));
            }
        }
        if (onFocusChanged) onFocusChanged(lost, id);
    }

    uint64_t UltraCanvasEmbeddedWindowManager::TopmostFocusable(uint64_t except) const {
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            const Window* w = Find(*it);
            if (!w || *it == except || w->info.frameOf || !w->info.focusable || !IsShown(*w)) continue;
            if (w->info.layer == EmbeddedWindowLayer::Popup ||
                w->info.layer == EmbeddedWindowLayer::Tooltip ||
                w->info.layer == EmbeddedWindowLayer::Notification) continue;
            return *it;
        }
        return 0;
    }

    void UltraCanvasEmbeddedWindowManager::FocusSomethingElse(uint64_t leaving) {
        // The window it belongs to first, else whatever is on top.
        const Window* w = Find(leaving);
        if (w && w->info.transientFor) {
            const Window* parent = Find(w->info.transientFor);
            if (parent && IsShown(*parent) && parent->info.focusable) {
                SetFocus(parent->info.id);
                return;
            }
        }
        SetFocus(TopmostFocusable(leaving));
    }

    uint64_t UltraCanvasEmbeddedWindowManager::GetKeyboardTarget() const {
        if (const Window* w = Find(focusedId)) {
            if (IsShown(*w)) return focusedId;
        }
        return TopmostFocusable();
    }

    void UltraCanvasEmbeddedWindowManager::Activate(uint64_t id) {
        const uint64_t client = ClientOf(id);
        Window* w = Find(client);
        if (!w) return;
        if (w->info.minimized) Restore(client);
        Restack(client);
        if (w->info.focusable) SetFocus(client);
    }

    void UltraCanvasEmbeddedWindowManager::Minimize(uint64_t id) {
        id = ClientOf(id);
        Window* w = Find(id);
        if (!w || w->info.minimized || w->info.layer == EmbeddedWindowLayer::Desktop) return;
        if (IsShown(*w)) AddDamage(DamageRect(*w));
        w->info.minimized = true;
        if (w->info.frameId) {
            if (Window* f = Find(w->info.frameId)) f->info.minimized = true;
        }
        if (grabId == id) { grabId = 0; buttonsDown = 0; }
        if (focusedId == id) FocusSomethingElse(id);
        if (onStateChanged) onStateChanged(id);
    }

    void UltraCanvasEmbeddedWindowManager::Maximize(uint64_t id) {
        id = ClientOf(id);
        Window* w = Find(id);
        if (!w || w->info.fillsOutput || w->info.maximized) return;
        if (w->info.minimized) Restore(id);
        if (!w->info.maximized) w->restoreRect = w->info.rect;
        const Rect2Di work = GetWorkArea();
        const int bar = w->info.decorated ? titleBarHeight : 0;
        Rect2Di r(work.x, work.y + bar, work.width, work.height - bar);
        if (w->maxSize.width > 0) r.width = std::min(r.width, w->maxSize.width);
        if (w->maxSize.height > 0) r.height = std::min(r.height, w->maxSize.height);
        w->info.maximized = true;
        ApplyRect(*w, r, true);
        if (onStateChanged) onStateChanged(id);
    }

    void UltraCanvasEmbeddedWindowManager::Restore(uint64_t id) {
        id = ClientOf(id);
        Window* w = Find(id);
        if (!w) return;
        if (w->info.minimized) {
            w->info.minimized = false;
            if (w->info.frameId) {
                if (Window* f = Find(w->info.frameId)) f->info.minimized = false;
            }
            Restack(id);
            AddDamage(DamageRect(*w));
            if (w->info.focusable) SetFocus(id);
            if (onStateChanged) onStateChanged(id);
            return;
        }
        if (w->info.maximized) {
            w->info.maximized = false;
            ApplyRect(*w, ClampToOutput(*w, w->restoreRect), true);
            if (onStateChanged) onStateChanged(id);
        }
    }

    void UltraCanvasEmbeddedWindowManager::ToggleMaximize(uint64_t id) {
        const Window* w = Find(ClientOf(id));
        if (!w) return;
        if (w->info.maximized) Restore(w->info.id);
        else Maximize(w->info.id);
    }

    // ===== POINTER =====

    Rect2Di UltraCanvasEmbeddedWindowManager::PointerBounds() const {
        // The shapes are drawn within 24 x 24 of their hot spot, centred
        // shapes reaching 12 the other way.
        return Rect2Di(pointer.x - 14, pointer.y - 14, 40, 40);
    }

    void UltraCanvasEmbeddedWindowManager::SetPointerPosition(Point2Di position) {
        position.x = std::clamp(position.x, 0, std::max(0, outputSize.width - 1));
        position.y = std::clamp(position.y, 0, std::max(0, outputSize.height - 1));
        if (position.x == pointer.x && position.y == pointer.y) return;
        if (pointerVisible) AddDamage(PointerBounds());
        pointer = position;
        if (pointerVisible) AddDamage(PointerBounds());
    }

    Point2Di UltraCanvasEmbeddedWindowManager::MovePointerBy(double dx, double dy) {
        SetPointerPosition(Point2Di(static_cast<int>(std::lround(pointer.x + dx)),
                                    static_cast<int>(std::lround(pointer.y + dy))));
        return pointer;
    }

    void UltraCanvasEmbeddedWindowManager::SetPointerVisible(bool visible) {
        if (visible == pointerVisible) return;
        pointerVisible = visible;
        AddDamage(PointerBounds());
    }

    void UltraCanvasEmbeddedWindowManager::SetWindowCursor(uint64_t id, UCMouseCursor cursor) {
        Window* w = Find(id);
        if (!w || w->cursor == cursor) return;
        w->cursor = cursor;
        if (hoverId == id && pointerVisible) AddDamage(PointerBounds());
    }

    EmbeddedWindowPart UltraCanvasEmbeddedWindowManager::HitTest(Point2Di p, uint64_t* windowId) const {
        if (windowId) *windowId = 0;
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            const Window* w = Find(*it);
            if (!w || !IsShown(*w)) continue;
            if (Inside(w->info.rect, p)) {
                if (windowId) *windowId = *it;
                return EmbeddedWindowPart::Content;
            }
            // The resize edges lie just outside a decorated window and its
            // title bar.
            if (w->info.frameOf || !w->info.decorated || !w->resizable || w->info.maximized) continue;
            const Rect2Di outer = OuterRect(*w);
            const Rect2Di grip = Grow(outer, resizeGrip);
            if (!Inside(grip, p)) continue;
            const bool left = p.x < outer.x, right = p.x >= outer.x + outer.width;
            const bool top = p.y < outer.y, bottom = p.y >= outer.y + outer.height;
            if (windowId) *windowId = *it;
            if (top && left) return EmbeddedWindowPart::ResizeTopLeft;
            if (top && right) return EmbeddedWindowPart::ResizeTopRight;
            if (bottom && left) return EmbeddedWindowPart::ResizeBottomLeft;
            if (bottom && right) return EmbeddedWindowPart::ResizeBottomRight;
            if (left) return EmbeddedWindowPart::ResizeLeft;
            if (right) return EmbeddedWindowPart::ResizeRight;
            if (top) return EmbeddedWindowPart::ResizeTop;
            return EmbeddedWindowPart::ResizeBottom;
        }
        return EmbeddedWindowPart::Nothing;
    }

    UCMouseCursor UltraCanvasEmbeddedWindowManager::CursorFor(EmbeddedWindowPart part) {
        switch (part) {
            case EmbeddedWindowPart::ResizeLeft:
            case EmbeddedWindowPart::ResizeRight: return UCMouseCursor::SizeWE;
            case EmbeddedWindowPart::ResizeTop:
            case EmbeddedWindowPart::ResizeBottom: return UCMouseCursor::SizeNS;
            case EmbeddedWindowPart::ResizeTopLeft:
            case EmbeddedWindowPart::ResizeBottomRight: return UCMouseCursor::SizeNWSE;
            case EmbeddedWindowPart::ResizeTopRight:
            case EmbeddedWindowPart::ResizeBottomLeft: return UCMouseCursor::SizeNESW;
            default: return UCMouseCursor::Default;
        }
    }

    EmbeddedPointerRoute UltraCanvasEmbeddedWindowManager::RouteTo(uint64_t id, Point2Di p,
                                                                  EmbeddedPointerRoute route) const {
        const Window* w = Find(id);
        if (!w) return route;
        route.windowId = id;
        route.local = Point2Di(p.x - w->info.rect.x, p.y - w->info.rect.y);
        route.cursor = w->cursor;
        return route;
    }

    EmbeddedPointerRoute UltraCanvasEmbeddedWindowManager::PointerMove(Point2Di position) {
        SetPointerPosition(position);
        const Point2Di p = pointer;
        EmbeddedPointerRoute route;

        if (interaction == Interaction::Moving) {
            if (Window* w = Find(interactionId)) {
                Rect2Di r = interactionStart;
                r.x += p.x - interactionAnchor.x;
                r.y += p.y - interactionAnchor.y;
                r = ClampToOutput(*w, r);
                w->restoreRect = r;
                ApplyRect(*w, r, true);
            }
            route.managerCursor = true;
            route.cursor = UCMouseCursor::SizeAll;
            return route;
        }
        if (interaction == Interaction::Resizing) {
            if (Window* w = Find(interactionId)) {
                const int dx = p.x - interactionAnchor.x, dy = p.y - interactionAnchor.y;
                Rect2Di r = interactionStart;
                const EmbeddedWindowPart part = resizePart;
                const bool left = part == EmbeddedWindowPart::ResizeLeft ||
                                  part == EmbeddedWindowPart::ResizeTopLeft ||
                                  part == EmbeddedWindowPart::ResizeBottomLeft;
                const bool right = part == EmbeddedWindowPart::ResizeRight ||
                                   part == EmbeddedWindowPart::ResizeTopRight ||
                                   part == EmbeddedWindowPart::ResizeBottomRight;
                const bool top = part == EmbeddedWindowPart::ResizeTop ||
                                 part == EmbeddedWindowPart::ResizeTopLeft ||
                                 part == EmbeddedWindowPart::ResizeTopRight;
                const bool bottom = part == EmbeddedWindowPart::ResizeBottom ||
                                    part == EmbeddedWindowPart::ResizeBottomLeft ||
                                    part == EmbeddedWindowPart::ResizeBottomRight;
                if (right) r.width = interactionStart.width + dx;
                if (bottom) r.height = interactionStart.height + dy;
                if (left) r.width = interactionStart.width - dx;
                if (top) r.height = interactionStart.height - dy;
                r = Constrain(*w, r);
                // The edge that did not move stays where it was.
                if (left) r.x = interactionStart.x + interactionStart.width - r.width;
                if (top) r.y = interactionStart.y + interactionStart.height - r.height;
                const Rect2Di work = GetWorkArea();
                const int bar = w->info.decorated ? titleBarHeight : 0;
                if (top && r.y < work.y + bar) {
                    r.height -= work.y + bar - r.y;
                    r.y = work.y + bar;
                }
                w->restoreRect = r;
                ApplyRect(*w, r, true);
            }
            route.managerCursor = true;
            route.cursor = CursorFor(resizePart);
            return route;
        }

        uint64_t over = 0;
        const EmbeddedWindowPart part = HitTest(p, &over);
        // A button held down in a window keeps the pointer there.
        const uint64_t target = grabId ? grabId : (part == EmbeddedWindowPart::Content ? over : 0);
        if (hoverId && hoverId != target) route.leftWindowId = hoverId;
        hoverId = target;
        if (target) {
            route = RouteTo(target, p, route);
        } else if (part != EmbeddedWindowPart::Nothing) {
            route.managerCursor = true;
            route.cursor = CursorFor(part);
        }
        return route;
    }

    EmbeddedPointerRoute UltraCanvasEmbeddedWindowManager::PointerButton(Point2Di position, bool down) {
        SetPointerPosition(position);
        const Point2Di p = pointer;
        EmbeddedPointerRoute route;

        if (!down) {
            if (interaction != Interaction::None) {
                interaction = Interaction::None;
                interactionId = 0;
                // The button that ended the move or resize may have pressed in
                // a frame window, which hears its release.
                if (grabId) {
                    route = RouteTo(grabId, p, route);
                    grabId = 0;
                    buttonsDown = 0;
                }
                return route;
            }
            if (grabId) {
                route = RouteTo(grabId, p, route);
                if (--buttonsDown <= 0) {
                    buttonsDown = 0;
                    grabId = 0;
                }
                return route;
            }
            uint64_t over = 0;
            if (HitTest(p, &over) == EmbeddedWindowPart::Content) route = RouteTo(over, p, route);
            return route;
        }

        if (grabId) {           // a second button while one is held
            ++buttonsDown;
            return RouteTo(grabId, p, route);
        }

        uint64_t over = 0;
        const EmbeddedWindowPart part = HitTest(p, &over);
        if (part == EmbeddedWindowPart::Nothing) return route;
        const uint64_t client = ClientOf(over);
        if (Window* cw = Find(client)) {
            if (cw->info.layer != EmbeddedWindowLayer::Popup &&
                cw->info.layer != EmbeddedWindowLayer::Tooltip) {
                Restack(client);
                if (cw->info.focusable) SetFocus(client);
            }
        }
        if (part != EmbeddedWindowPart::Content) {
            if (Window* w = Find(over)) {
                interaction = Interaction::Resizing;
                interactionId = over;
                resizePart = part;
                interactionAnchor = p;
                interactionStart = w->info.rect;
            }
            route.managerCursor = true;
            route.cursor = CursorFor(part);
            return route;
        }
        grabId = over;
        buttonsDown = 1;
        hoverId = over;
        return RouteTo(over, p, route);
    }

    void UltraCanvasEmbeddedWindowManager::BeginMove(uint64_t clientId) {
        Window* w = Find(ClientOf(clientId));
        if (!w || w->info.fillsOutput) return;
        if (w->info.maximized) {
            // Taken off the top of the screen: back to its own size, under the
            // pointer where it was taken.
            const Rect2Di maxRect = w->info.rect;
            w->info.maximized = false;
            Rect2Di r = w->restoreRect;
            const double along = maxRect.width > 0
                    ? static_cast<double>(pointer.x - maxRect.x) / maxRect.width : 0.5;
            r.x = pointer.x - static_cast<int>(along * r.width);
            r.y = maxRect.y;
            ApplyRect(*w, ClampToOutput(*w, r), true);
            if (onStateChanged) onStateChanged(w->info.id);
        }
        interaction = Interaction::Moving;
        interactionId = w->info.id;
        interactionAnchor = pointer;
        interactionStart = w->info.rect;
    }

    // ===== DAMAGE =====

    void UltraCanvasEmbeddedWindowManager::AddDamage(const Rect2Di& rect) {
        Rect2Di r = rect;
        // Clipped to the output.
        const int l = std::max(0, r.x), t = std::max(0, r.y);
        const int rr = std::min(outputSize.width, r.x + r.width);
        const int b = std::min(outputSize.height, r.y + r.height);
        if (rr <= l || b <= t) return;
        r = Rect2Di(l, t, rr - l, b - t);
        // Merged into one it overlaps; a long list becomes one rectangle.
        for (auto& d : damage) {
            if (Overlap(d, r)) {
                d = Unite(d, r);
                return;
            }
        }
        damage.push_back(r);
        if (damage.size() > 16) {
            Rect2Di all = damage.front();
            for (const auto& d : damage) all = Unite(all, d);
            damage.assign(1, all);
        }
    }

    void UltraCanvasEmbeddedWindowManager::DamageAll() {
        damage.assign(1, Rect2Di(0, 0, outputSize.width, outputSize.height));
    }

    void UltraCanvasEmbeddedWindowManager::WindowPresented(uint64_t id) {
        const Window* w = Find(id);
        if (w && IsShown(*w)) AddDamage(w->info.rect);
    }

    std::vector<Rect2Di> UltraCanvasEmbeddedWindowManager::TakeDamage() {
        // A merge can make two earlier rectangles overlap: merge again.
        bool merged = true;
        while (merged && damage.size() > 1) {
            merged = false;
            for (size_t i = 0; i < damage.size() && !merged; ++i) {
                for (size_t j = i + 1; j < damage.size(); ++j) {
                    if (Overlap(damage[i], damage[j])) {
                        damage[i] = Unite(damage[i], damage[j]);
                        damage.erase(damage.begin() + static_cast<long>(j));
                        merged = true;
                        break;
                    }
                }
            }
        }
        std::vector<Rect2Di> out;
        out.swap(damage);
        return out;
    }

    // ===== COMPOSITING =====

    void UltraCanvasEmbeddedWindowManager::DrawPointer(cairo_t* cr, UCMouseCursor cursor,
                                                       double x, double y, double s) {
        // Vector shapes with a white fill and a dark outline, readable on any
        // ground; `s` is the size of a logical pixel.
        cairo_save(cr);
        cairo_translate(cr, x, y);
        cairo_scale(cr, s, s);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        auto finish = [cr](double width) {
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_fill_preserve(cr);
            cairo_set_source_rgb(cr, 0.08, 0.08, 0.08);
            cairo_set_line_width(cr, width);
            cairo_stroke(cr);
        };
        auto doubleArrow = [&](double angle) {
            cairo_save(cr);
            cairo_rotate(cr, angle);
            cairo_move_to(cr, -11, 0);
            cairo_line_to(cr, -6, -5);  cairo_line_to(cr, -6, -2);
            cairo_line_to(cr, 6, -2);   cairo_line_to(cr, 6, -5);
            cairo_line_to(cr, 11, 0);
            cairo_line_to(cr, 6, 5);    cairo_line_to(cr, 6, 2);
            cairo_line_to(cr, -6, 2);   cairo_line_to(cr, -6, 5);
            cairo_close_path(cr);
            finish(1.2);
            cairo_restore(cr);
        };
        switch (cursor) {
            case UCMouseCursor::NoCursor:
                break;
            case UCMouseCursor::Text:
                cairo_move_to(cr, -3, -9); cairo_line_to(cr, 3, -9);
                cairo_move_to(cr, 0, -9);  cairo_line_to(cr, 0, 9);
                cairo_move_to(cr, -3, 9);  cairo_line_to(cr, 3, 9);
                cairo_set_source_rgb(cr, 1, 1, 1);
                cairo_set_line_width(cr, 3.5);
                cairo_stroke_preserve(cr);
                cairo_set_source_rgb(cr, 0.08, 0.08, 0.08);
                cairo_set_line_width(cr, 1.5);
                cairo_stroke(cr);
                break;
            case UCMouseCursor::SizeWE:   doubleArrow(0); break;
            case UCMouseCursor::SizeNS:   doubleArrow(kPi / 2); break;
            case UCMouseCursor::SizeNWSE: doubleArrow(kPi / 4); break;
            case UCMouseCursor::SizeNESW: doubleArrow(-kPi / 4); break;
            case UCMouseCursor::SizeAll:  doubleArrow(0); doubleArrow(kPi / 2); break;
            case UCMouseCursor::Cross:
                cairo_move_to(cr, -10, 0); cairo_line_to(cr, 10, 0);
                cairo_move_to(cr, 0, -10); cairo_line_to(cr, 0, 10);
                cairo_set_source_rgb(cr, 1, 1, 1);
                cairo_set_line_width(cr, 3.5);
                cairo_stroke_preserve(cr);
                cairo_set_source_rgb(cr, 0.08, 0.08, 0.08);
                cairo_set_line_width(cr, 1.5);
                cairo_stroke(cr);
                break;
            case UCMouseCursor::Wait:
                cairo_arc(cr, 0, 0, 8, 0, 2 * kPi);
                finish(1.5);
                cairo_arc(cr, 0, 0, 5, -kPi / 2, kPi / 2);
                cairo_set_source_rgb(cr, 0.2, 0.45, 0.85);
                cairo_set_line_width(cr, 2.5);
                cairo_stroke(cr);
                break;
            default: {
                // The arrow, hot spot at its tip; the hand and the other
                // shapes use it with a mark beside it.
                cairo_move_to(cr, 0, 0);
                cairo_line_to(cr, 0, 17);
                cairo_line_to(cr, 4.2, 13.2);
                cairo_line_to(cr, 7.2, 19.8);
                cairo_line_to(cr, 10, 18.6);
                cairo_line_to(cr, 7.2, 12.2);
                cairo_line_to(cr, 12.6, 12.2);
                cairo_close_path(cr);
                finish(1.3);
                if (cursor == UCMouseCursor::Hand || cursor == UCMouseCursor::AppStarting ||
                    cursor == UCMouseCursor::Help || cursor == UCMouseCursor::NotAllowed) {
                    cairo_arc(cr, 15, 17, 4, 0, 2 * kPi);
                    if (cursor == UCMouseCursor::NotAllowed) cairo_set_source_rgb(cr, 0.85, 0.2, 0.2);
                    else cairo_set_source_rgb(cr, 0.2, 0.45, 0.85);
                    cairo_fill(cr);
                }
                break;
            }
        }
        cairo_restore(cr);
    }

    Rect2Di UltraCanvasEmbeddedWindowManager::Compose(cairo_surface_t* output, float scale,
                                                      const SurfaceProvider& surfaceOf,
                                                      const std::vector<Rect2Di>& damaged) {
        if (!output || damaged.empty()) return Rect2Di();
        const int outW = cairo_image_surface_get_width(output);
        const int outH = cairo_image_surface_get_height(output);
        if (scale <= 0.0f) scale = 1.0f;

        cairo_t* cr = cairo_create(output);
        // The damaged rectangles in physical pixels, rounded outwards.
        Rect2Di drawn;
        for (const Rect2Di& d : damaged) {
            const int l = std::max(0, static_cast<int>(std::floor(d.x * scale)));
            const int t = std::max(0, static_cast<int>(std::floor(d.y * scale)));
            const int r = std::min(outW, static_cast<int>(std::ceil((d.x + d.width) * scale)));
            const int b = std::min(outH, static_cast<int>(std::ceil((d.y + d.height) * scale)));
            if (r <= l || b <= t) continue;
            cairo_rectangle(cr, l, t, r - l, b - t);
            drawn = Unite(drawn, Rect2Di(l, t, r - l, b - t));
        }
        if (RectEmpty(drawn)) {
            cairo_destroy(cr);
            return Rect2Di();
        }
        cairo_clip(cr);

        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgb(cr, ((backgroundRgb >> 16) & 0xFF) / 255.0,
                             ((backgroundRgb >> 8) & 0xFF) / 255.0, (backgroundRgb & 0xFF) / 255.0);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

        for (uint64_t id : stack) {
            const Window* w = Find(id);
            if (!w || !IsShown(*w)) continue;
            const Rect2Di& r = w->info.rect;

            if (w->info.decorated) {
                // A soft shadow: rings of falling opacity around the window and
                // its title bar.
                const Rect2Di outer = OuterRect(*w);
                for (int i = kShadow; i >= 1; --i) {
                    const double a = 0.10 * (1.0 - static_cast<double>(i) / (kShadow + 1));
                    cairo_set_source_rgba(cr, 0, 0, 0, a);
                    cairo_rectangle(cr, (outer.x - i) * scale, (outer.y - i + 2) * scale,
                                    (outer.width + 2 * i) * scale, (outer.height + 2 * i) * scale);
                    cairo_rectangle(cr, outer.x * scale, outer.y * scale,
                                    outer.width * scale, outer.height * scale);
                    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
                    cairo_fill(cr);
                }
                cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
            }

            cairo_surface_t* surface = surfaceOf ? surfaceOf(id) : nullptr;
            if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) continue;
            double sx = 1.0, sy = 1.0;
            cairo_surface_get_device_scale(surface, &sx, &sy);
            if (sx <= 0) sx = 1.0;
            if (sy <= 0) sy = 1.0;
            const double dstX = std::round(r.x * scale), dstY = std::round(r.y * scale);
            const double dstW = std::round(r.width * scale), dstH = std::round(r.height * scale);
            cairo_save(cr);
            cairo_rectangle(cr, dstX, dstY, dstW, dstH);
            cairo_clip(cr);
            cairo_translate(cr, dstX, dstY);
            // The surface's own pixels onto the output's, one to one: its
            // device scale makes a user unit `sx` of its pixels.
            cairo_scale(cr, sx, sy);
            cairo_set_source_surface(cr, surface, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_FAST);
            cairo_paint(cr);
            cairo_restore(cr);
        }

        if (pointerVisible) {
            UCMouseCursor shape = UCMouseCursor::Default;
            if (interaction == Interaction::Moving) shape = UCMouseCursor::SizeAll;
            else if (interaction == Interaction::Resizing) shape = CursorFor(resizePart);
            else {
                uint64_t over = 0;
                const EmbeddedWindowPart part = HitTest(pointer, &over);
                const uint64_t target = grabId ? grabId : (part == EmbeddedWindowPart::Content ? over : 0);
                if (target) {
                    if (const Window* tw = Find(target)) shape = tw->cursor;
                } else {
                    shape = CursorFor(part);
                }
            }
            DrawPointer(cr, shape, pointer.x * scale, pointer.y * scale, scale);
        }

        cairo_destroy(cr);
        cairo_surface_flush(output);
        return drawn;
    }

} // namespace UltraCanvas
