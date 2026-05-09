// UltraCanvasWindowBase.cpp
// Fixed implementation of cross-platform window management system
// Version: 1.2.1
// Last Modified: 2026-04-05
// Author: UltraCanvas Framework

#include "UltraCanvasWindow.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasTooltipManager.h"

#include <iostream>
#include <algorithm>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {
    UltraCanvasWindowBase::UltraCanvasWindowBase()
            : UltraCanvasContainer("Window", 0, 0, 0, 0, 0) {
        // Configure container for window behavior
        visible = false;
        window = this;
        nativeSurface = nullptr;
    }

//    UltraCanvasWindowBase::~UltraCanvasWindowBase() {
//        UnregisterWindow();
//        debugOutput << "UltraCanvas: Window deleted" << std::endl;
//    }

    // ===== FOCUS MANAGEMENT IMPLEMENTATION =====

    bool UltraCanvasWindowBase::IsWindowFocused() const {
        return UltraCanvasApplication::GetInstance()->GetFocusedWindow() == this;
    }

    void UltraCanvasWindowBase::SetFocusedElement(UltraCanvasUIElement* element) {
        // Don't do anything if already focused
        if (_focusedElement == element) {
            return;
        }

        // Validate element belongs to this window
        if (element && element->GetWindow() != this) {
            debugOutput << "Warning: Trying to focus element from different window" << std::endl;
            return;
        }

        UltraCanvasUIElement* prev = _focusedElement;

        // Remove focus from current element
        if (_focusedElement) {
            SendFocusLostEvent(_focusedElement);
        }

        // Set new focused element
        _focusedElement = element;

        // Set focus on new element
        if (_focusedElement) {
            SendFocusGainedEvent(_focusedElement);
        }

        // Repaint focus rings on both old and new focused elements.
        if (prev) prev->RequestRedraw();
        if (_focusedElement) _focusedElement->RequestRedraw();

        debugOutput << "Focus changed to: " << (element ? element->GetIdentifier() : "none") << std::endl;
    }

    void UltraCanvasWindowBase::ClearFocus() {
        SetFocusedElement(nullptr);
    }

    bool UltraCanvasWindowBase::RequestElementFocus(UltraCanvasUIElement* element) {
        // Validate element can receive focus
        if (!element || !element->CanReceiveFocus()) {
            return false;
        }

        // Set focus through window's focus management
        SetFocusedElement(element);
        return true;
    }

    void UltraCanvasWindowBase::FocusNextElement() {
        std::vector<UltraCanvasUIElement*> focusableElements = GetFocusableElements();

        if (focusableElements.empty()) {
            return;
        }

        // Find current focus index
        int currentIndex = -1;
        for (size_t i = 0; i < focusableElements.size(); ++i) {
            if (focusableElements[i] == _focusedElement) {
                currentIndex = static_cast<int>(i);
                break;
            }
        }

        // Calculate next index
        int nextIndex = (currentIndex + 1) % static_cast<int>(focusableElements.size());

        // Set focus to next element
        SetFocusedElement(focusableElements[nextIndex]);
    }

    void UltraCanvasWindowBase::FocusPreviousElement() {
        std::vector<UltraCanvasUIElement*> focusableElements = GetFocusableElements();

        if (focusableElements.empty()) {
            return;
        }

        // Find current focus index
        int currentIndex = -1;
        for (size_t i = 0; i < focusableElements.size(); ++i) {
            if (focusableElements[i] == _focusedElement) {
                currentIndex = static_cast<int>(i);
                break;
            }
        }

        // Calculate previous index
        int prevIndex = (currentIndex <= 0) ?
                        static_cast<int>(focusableElements.size()) - 1 :
                        currentIndex - 1;

        // Set focus to previous element
        SetFocusedElement(focusableElements[prevIndex]);
    }

    std::vector<UltraCanvasUIElement*> UltraCanvasWindowBase::GetFocusableElements() {
        std::vector<UltraCanvasUIElement*> focusableElements;

        // Start collecting from the window container itself
        CollectFocusableElements(this, focusableElements);

        return focusableElements;
    }

    void UltraCanvasWindowBase::CollectFocusableElements(UltraCanvasContainer* container,
                                                         std::vector<UltraCanvasUIElement*>& elements) {
        if (!container) return;

        // Get all child elements from the container
        const auto& children = container->GetChildren();

        for (const auto& child : children) {
            UltraCanvasUIElement* element = child.get();

            // Check if element can be focused
            if (element && element->CanReceiveFocus()) {
                elements.push_back(element);
            }

            // If the element is also a container, recursively collect from it
            if (auto childContainer = dynamic_cast<UltraCanvasContainer*>(element)) {
                CollectFocusableElements(childContainer, elements);
            }
        }
    }

    void UltraCanvasWindowBase::SendFocusGainedEvent(UltraCanvasUIElement* element) {
        if (!element) return;

        UCEvent focusEvent;
        focusEvent.type = UCEventType::FocusGained;
        focusEvent.nativeWindowHandle = GetNativeHandle();
        element->OnEvent(focusEvent);
    }

    void UltraCanvasWindowBase::SendFocusLostEvent(UltraCanvasUIElement* element) {
        if (!element) return;

        UCEvent focusEvent;
        focusEvent.type = UCEventType::FocusLost;
        focusEvent.nativeWindowHandle = GetNativeHandle();
        element->OnEvent(focusEvent);
    }

    bool UltraCanvasWindowBase::OnEvent(const UCEvent &event) {
        // Handle window-specific events first
        if (HandleWindowEvent(event)) {
            return true;
        }

        // Handle focus-related keyboard events
        if (event.IsKeyboardEvent()) {
            if (event.type == UCEventType::KeyDown && event.virtualKey == UCKeys::Tab && !event.ctrl && !event.alt && !event.meta) {
                if (event.shift) {
                    FocusPreviousElement();
                } else {
                    FocusNextElement();
                }
                return true;
            }
        }

        return UltraCanvasContainer::OnEvent(event);
    }

    void UltraCanvasWindowBase::InstallEventFilter(const std::string& uniqueFilterId, const std::function<bool(const UCEvent&)>& filterFunc, const std::vector<UCEventType>& interestedEvents) {
        for(auto et : interestedEvents) {
            if (eventFilters.find(et) == eventFilters.end()) {
                eventFilters[et] = {};
            }
            eventFilters[et].emplace_back(FilterFunction{.id=uniqueFilterId, .func=filterFunc});
        }
    }

    void UltraCanvasWindowBase::UnInstallWindowEventFilter(const std::string& uniqueFilterId) {
        if (eventFilters.empty()) {
            for(auto &ef : eventFilters) {
                auto funcs = ef.second;
                for(auto it = funcs.begin(); it != funcs.end(); ++it) {
                    if (it->id == uniqueFilterId) {
                        funcs.erase(it);
                        break;
                    }
                }
            }
        }
    }


    bool UltraCanvasWindowBase::HandleEventFilters(const UCEvent &event) {
        auto found = eventFilters.find(event.type);
        if (found != eventFilters.end()) {
            for(auto const &filterFunc : found->second) {
                if (filterFunc.func(event)) {
                    return true;
                }
            }
        }
        return false;
    }

    bool UltraCanvasWindowBase::HandleWindowEvent(const UCEvent &event) {
        switch (event.type) {
            case UCEventType::WindowBlur:
                if (onWindowBlur) {
                    onWindowBlur();
                }
                RequestRedraw();
                return true;
            case UCEventType::WindowFocus:
                if (onWindowFocus) {
                    onWindowFocus();
                }
                RequestRedraw();
                return true;
            case UCEventType::WindowCloseRequest:
                Close();
                return true;

            case UCEventType::WindowResize:
                HandleResizeEvent(event.width, event.height);
                RequestRedraw();
                return true;

            case UCEventType::WindowMove:
                HandleMoveEvent(event.pointer.x, event.pointer.y);
                return true;

            case UCEventType::WindowRepaint:
            case UCEventType::Redraw: {
                // Honour the platform damage rect when present; otherwise repaint
                // the full window. Without this, an occluded-then-revealed window
                // would have an empty dirty list and paint nothing.
                Rect2Di damage(event.pointerWindow.x, event.pointerWindow.y,
                               event.width, event.height);
                if (damage.width <= 0 || damage.height <= 0) {
                    damage = Rect2Di(0, 0, config_.width, config_.height);
                }
                AddDirtyRectangle(damage);
                return true;
            }

            default:
                return false;
        }
    }

//    void UltraCanvasWindowBase::HandleCloseEvent() {
//        Close();
////        _state = WindowState::Closing;
////        if (onWindowClose) onWindowClose();
//    }

    void UltraCanvasWindowBase::HandleResizeEvent(int width, int height) {
        config_.width = width;
        config_.height = height;
        _needsResize = true;
        debugOutput << "UltraCanvasWindowBase::HandleResizeEvent nativeh=" << GetNativeHandle() << " (" << config_.width << "x" << config_.height << ")" << std::endl;
    }

    void UltraCanvasWindowBase::DoResize() {
        _needsResize = false;
        if (GetWidth() == config_.width && GetHeight() == config_.height) {
            debugOutput << "UltraCanvasWindowBase::DoResize windows already has same size, return. nativeh=" << GetNativeHandle() << " (" << config_.width << "x" << config_.height << ")" << std::endl;
            return;
        }
        debugOutput << "UltraCanvasWindowBase::DoResize nativeh=" << GetNativeHandle() << " (" << config_.width << "x" << config_.height << ")" << std::endl;

        if (renderContext) {
            renderContext->ResizeSurface({config_.width, config_.height});
        }
        DoResizeNative();
        SetOriginalSize(config_.width, config_.height);

        if (onWindowResize) onWindowResize(config_.width, config_.height);

        AddDirtyRectangle(Rect2Di(0, 0, config_.width, config_.height));
    }

    void UltraCanvasWindowBase::HandleMoveEvent(int x, int y) {
        config_.x = x;
        config_.y = y;
        // position must be always 0,0
        //UltraCanvasContainer::SetPosition(x, y);
        if (onWindowMove) onWindowMove(x, y);
    }


    void UltraCanvasWindowBase::UpdateAndRender() {
        if (!visible || !_created) return;
        auto ctx = GetRenderContext();
        if (IsNeedsResize()) {
            DoResize();
        }
        if (!ctx) return;

        if (needsUpdateGeometry) {
            UpdateGeometry(ctx);
        }

        // ---- Window content pass: loop once per optimised dirty rect ----
        if (dirtyRectManager.HasDirtyRects()) {
            const auto& rects = dirtyRectManager.GetOptimizedRectangles();
            for (const auto& rect : rects) {
                ctx->PushState();
                ctx->ClipRect(Rect2Df(rect.x, rect.y, rect.width, rect.height));
                Render(ctx, rect);
                RenderCustomContent(ctx, rect);
                ctx->PopState();
            }
            dirtyRectManager.Clear();
            _needsWindowComposition = true;
        }

        // ---- Popup pass: each popup owns its own dirty queue, in popup-local coords ----
        for (auto& pe : popupElements) {
            auto* p = pe.element;
            if (!p || !p->IsVisible()) continue;

            if (_needsPopupGeometry || p->needsUpdateGeometry) {
                p->UpdateGeometry(ctx);
                p->needsUpdateGeometry = false;
            }

            Size2Di want = p->GetSize();
            if (want.width <= 0 || want.height <= 0) continue;

            if (!p->renderContext) {
                p->renderContext = CreateRenderContext(want, nativeSurface);
                p->needsUpdateGeometry = true;
                pe.dirtyRectManager.Add(Rect2Di(0, 0, want.width, want.height));
            } else if (p->renderContext->GetSurfaceSize() != want) {
                p->renderContext->ResizeSurface(want);
                pe.dirtyRectManager.Add(Rect2Di(0, 0, want.width, want.height));
            }

            if (pe.dirtyRectManager.HasDirtyRects()) {
                const auto& popupRects = pe.dirtyRectManager.GetOptimizedRectangles();
                for (const auto& rect : popupRects) {
                    p->renderContext->PushState();
                    p->renderContext->ClipRect(Rect2Df(rect.x, rect.y, rect.width, rect.height));
                    p->Render(p->renderContext.get(), rect);
                    p->renderContext->PopState();
                }
                pe.dirtyRectManager.Clear();
                _needsWindowComposition = true;
            }
        }

        // Composite popups and tooltips onto the native surface
        if (_needsWindowComposition) {
            renderContext->FlushToSurface(nativeSurface, {0, 0});

            if (!popupElements.empty()) {
                for (auto& pe : popupElements) {
                    auto* p = pe.element;
                    if (!p || !p->IsVisible() || !p->renderContext) continue;
                    auto pos = p->GetPositionInWindow();
                    p->renderContext->FlushToSurface(nativeSurface,
                                                     {(float)pos.x, (float)pos.y});
                }
            }

            auto tooltipCtx = UltraCanvasTooltipManager::Render(this);
            if (tooltipCtx) {
                tooltipCtx->FlushToSurface(nativeSurface, UltraCanvasTooltipManager::GetTooltipPosition());
            }

            InvalidateWindowNative();
        }

        _needsPopupGeometry = false;
        _needsWindowComposition = false;
    }

    void UltraCanvasWindowBase::AddDirtyRectangle(const Rect2Di& windowRect) {
        // Clip to window bounds so off-window invalidations don't waste cycles.
        Rect2Di winBounds(0, 0, config_.width, config_.height);
        Rect2Di clipped = windowRect.Intersection(winBounds);
        if (clipped.width <= 0 || clipped.height <= 0) return;
        dirtyRectManager.Add(clipped);
    }

    void UltraCanvasWindowBase::AddPopupDirtyRect(UltraCanvasUIElement* popup,
                                                  const Rect2Di& popupLocalRect) {
        if (!popup) return;
        for (auto& pe : popupElements) {
            if (pe.element == popup) {
                Size2Di sz = popup->GetSize();
                Rect2Di popupBounds(0, 0, sz.width, sz.height);
                Rect2Di clipped = popupLocalRect.Intersection(popupBounds);
                if (clipped.width > 0 && clipped.height > 0) {
                    pe.dirtyRectManager.Add(clipped);
                }
                return;
            }
        }
    }

    void UltraCanvasWindowBase::OpenPopup(const Point2Di& pos, UltraCanvasUIElement& elem, const PopupElementSettings& settings) {
        for(auto it = popupElements.begin(); it != popupElements.end(); ++it) {
            if (it->element == &elem) {
                popupElements.erase(it);
                break;
            }
        }
        popupElements.push_back({&elem, settings, {}});
        AddChild(elem.shared_from_this());
        elem.SetPosition(pos);
        elem.SetVisible(true);
        elem.zOrder = OverlayZOrder::Popups;
        elem.isPopup = true;

        // Allocate the popup's own off-screen render context. Size guard handles the
        // case where the popup hasn't run a layout pass yet — it'll be resized in
        // UpdateAndRender once the real bounds are known.
        Size2Di sz = elem.GetSize();
        if (sz.width <= 0 || sz.height <= 0) sz = {1, 1};
        elem.renderContext = CreateRenderContext(sz, nativeSurface);
        elem.needsUpdateGeometry = true;

        // Seed the popup's dirty list so the first frame paints fully.
        popupElements.back().dirtyRectManager.Add(Rect2Di(0, 0, sz.width, sz.height));

        elem.OnPopupOpened();
    }

    bool UltraCanvasWindowBase::ClosePopup(UltraCanvasUIElement& elem, ClosePopupReason reason) {
        if (elem.isPopup) {
            for(auto it = popupElements.begin(); it != popupElements.end(); ++it) {
                if (it->element == &elem) {
                    if (elem.OnPopupAboutToClose(reason)) {
                        popupElements.erase(it);
                        elem.isPopup = false;
                        elem.SetVisible(false);
                        RemoveChild(elem.shared_from_this());
                        elem.renderContext.reset();
                        elem.OnPopupClosed(reason);
                        // Closing a popup uncovers window pixels, so we need a full
                        // window recomposite to refresh the area underneath.
                        RequestWindowComposition();
                        return true;
                    } else {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    PopupElement* UltraCanvasWindowBase::GetActivePopupElement() {
        if (!popupElements.empty()) {
            return &popupElements.back();
        } else {
            return nullptr;
        }
    }

    void UltraCanvasWindowBase::CloseAllPopups() {
        
        for(auto it = popupElements.rbegin(); it != popupElements.rend(); ++it) {
            if (it->element) {
                ClosePopup(*it->element);
            }
        }
        popupElements.clear();
    }

    bool UltraCanvasWindowBase::Create(const WindowConfig& config) {
        config_ = config;
        return Create();
    }

    bool UltraCanvasWindowBase::Create() {
        _state = WindowState::Normal;
        _created = false;

        ContainerStyle containerStyle;
        containerStyle.forceShowVerticalScrollbar = config_.enableWindowScrolling;
        containerStyle.forceShowHorizontalScrollbar = config_.enableWindowScrolling;
        SetContainerStyle(containerStyle);
        SetBackgroundColor(config_.backgroundColor);

        auto application = UltraCanvasApplication::GetInstance();
        SetBounds(Rect2Di(0, 0, config_.width, config_.height));

        if (CreateNative()) {
            renderContext = CreateRenderContext(Size2Di(config_.width, config_.height), nativeSurface);
            if (renderContext) {
                UltraCanvasApplication::GetInstance()->RegisterWindow(std::dynamic_pointer_cast<UltraCanvasWindowBase>(shared_from_this()));
                _created = true;
                // Seed full-window dirty rect so the first frame paints.
                AddDirtyRectangle(Rect2Di(0, 0, config_.width, config_.height));
            } else {
                debugOutput << "UltraCanvasWindowBase::Create CreateRenderContext failed" << std::endl;
            }
        }
        return _created;
    }

    bool UltraCanvasWindowBase::Close() {
        if (!_created || _state == WindowState::Closing 
            || _state == WindowState::Closed) {
            return false;
        }
        debugOutput << "UltraCanvas: Window close requested this=" << this << std::endl;
        if (!onWindowClosing || onWindowClosing()) {
            PerformClose();
            return true;
        } else {
            return false;
        }
    }

    void UltraCanvasWindowBase::PerformClose() {
        if (!_created || _state == WindowState::Closing
            || _state == WindowState::Closed) {
            return;
        }
        debugOutput << "UltraCanvas: Window perform close this=" << this << std::endl;
        _state = WindowState::Closing;

        CloseAllPopups();

        UltraCanvasApplication::GetInstance()->CleanupWindowReferences(this);

        DestroyNative();
        _created = false;
        _state = WindowState::Closed;

        if (onWindowClosed) {
            onWindowClosed();
        }
    }

    bool UltraCanvasWindowBase::SelectMouseCursor(UCMouseCursor ptr) {
        if (currentMouseCursor != ptr) {
            if (UltraCanvasApplication::GetInstance()->SelectMouseCursorNative(this, ptr)) {
                currentMouseCursor = ptr;
                return true;
            }
        }
        return false;
    }

    bool UltraCanvasWindowBase::SelectMouseCursor(UCMouseCursor ptr, const char* filename, int hotspotX, int hotspotY) {
        if (currentMouseCursor != ptr) {
            if (UltraCanvasApplication::GetInstance()->SelectMouseCursorNative(this, ptr, filename, hotspotX, hotspotY)) {
                currentMouseCursor = ptr;
                return true;
            }
        }
        return false;
    }


    UltraCanvasWindowBase* UltraCanvasWindowBase::GetParentWindow() {
        if (config_.parentWindow && !UltraCanvasApplication::GetInstance()->IsWindowRegistered(config_.parentWindow)) {
            config_.parentWindow = nullptr;
        }
        return config_.parentWindow;
    }

    void UltraCanvasWindowBase::GetScreenBounds(int& x, int& y, int& width, int& height) const {
        x = 0;
        y = 0;
        GetScreenSize(width, height);
    }

    void UltraCanvasWindowBase::CenterOnScreen() {
        int sx = 0, sy = 0, sw = 0, sh = 0;
        GetScreenBounds(sx, sy, sw, sh);
        if (sw > 0 && sh > 0) {
            int x = sx + (sw - config_.width) / 2;
            int y = sy + (sh - config_.height) / 2;
            SetWindowPosition(x, y);
        }
    }

    void UltraCanvasWindowBase::CenterOnParent(UltraCanvasWindowBase* parent) {
        if (!parent) {
            CenterOnScreen();
            return;
        }
        int parentX = 0, parentY = 0, parentW = 0, parentH = 0;
        parent->GetWindowPosition(parentX, parentY);
        parent->GetWindowSize(parentW, parentH);
        int x = parentX + (parentW - config_.width) / 2;
        int y = parentY + (parentH - config_.height) / 2;
        SetWindowPosition(x, y);
    }

    void UltraCanvasWindowBase::CenterOnScreenOfWindow(UltraCanvasWindowBase* referenceWindow) {
        if (!referenceWindow) {
            CenterOnScreen();
            return;
        }
        int sx = 0, sy = 0, sw = 0, sh = 0;
        referenceWindow->GetScreenBounds(sx, sy, sw, sh);
        if (sw > 0 && sh > 0) {
            int x = sx + (sw - config_.width) / 2;
            int y = sy + (sh - config_.height) / 2;
            SetWindowPosition(x, y);
        }
    }

    std::shared_ptr<UltraCanvasWindow> CreateWindow() {
        auto win = std::make_shared<UltraCanvasWindow>();
        return win;
    }

    std::shared_ptr<UltraCanvasWindow> CreateWindow(const WindowConfig& config) {
        auto win = std::make_shared<UltraCanvasWindow>();
        win->Create(config);
        return win;
    }

} // namespace UltraCanvas