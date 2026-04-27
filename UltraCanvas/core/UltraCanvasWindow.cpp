// UltraCanvasWindowBase.cpp
// Fixed implementation of cross-platform window management system
// Version: 1.2.1
// Last Modified: 2026-04-05
// Author: UltraCanvas Framework

#include "UltraCanvasWindow.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasTooltipManager.h"
//#include "UltraCanvasZOrderManager.h"

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

        // Request window redraw to update focus indicators
        needsRedraw = true;

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
            case UCEventType::Redraw:
                needsRedraw = true;
                return true;

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

        needsRedraw = true;
    }

    void UltraCanvasWindowBase::HandleMoveEvent(int x, int y) {
        config_.x = x;
        config_.y = y;
        // position must be always 0,0
        //UltraCanvasContainer::SetPosition(x, y);
        if (onWindowMove) onWindowMove(x, y);
    }


    void UltraCanvasWindowBase::UpdateAndRender() {
        if (visible && _created) {
            auto ctx = GetRenderContext();
            if (IsNeedsResize()) {
                DoResize();
            }
            if (ctx) {
                if (needsUpdateGeometry) {
                    UpdateGeometry(ctx);
                }
//                  debugOutput << "Redraw window w=" << window << " nativeh=" << window->GetNativeHandle() << std::endl;
                bool windowUpdated = false;
                if (_needsWindowRedraw) {
                    Render(ctx);
                    RenderCustomContent(ctx);
                    windowUpdated = true;
                }
                if (UltraCanvasTooltipManager::Render(ctx, this)) {
                    windowUpdated = true;
                }
                if (windowUpdated) {
                    renderContext->FlushToSurface(nativeSurface, {0, 0});
                    FlushNative();
                }
                _needsWindowRedraw = false;
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
        popupElements.push_back({&elem, settings});
        AddChild(elem.shared_from_this());
        elem.SetPosition(pos);
        elem.SetVisible(true);
        elem.zOrder = OverlayZOrder::Popups;
        elem.isPopup = true;
        RequestRedraw();

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
                        elem.OnPopupClosed(reason);
                        RequestRedraw();
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


//    void UpdateZOrderSort() {
//        sortedElements = elements;
//        UltraCanvasZOrderManager::SortElementsByZOrder(sortedElements);
//        zOrderDirty = false;
//
//        debugOutput << "Window z-order updated with " << sortedElements.size() << " elements:" << std::endl;
//        for (size_t i = 0; i < sortedElements.size(); i++) {
//            auto* element = sortedElements[i];
//            if (element) {
//                debugOutput << "  [" << i << "] Z=" << element->GetZIndex()
//                          << " " << element->GetIdentifier() << std::endl;
//            }
//        }
//    }

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