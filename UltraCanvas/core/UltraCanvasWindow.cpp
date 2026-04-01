// UltraCanvasWindowBase.cpp
// Fixed implementation of cross-platform window management system
// Version: 1.2.0
// Last Modified: 2025-07-15
// Author: UltraCanvas Framework

#include "../include/UltraCanvasWindow.h"
#include "../include/UltraCanvasRenderContext.h"
#include "../include/UltraCanvasApplication.h"
#include "../include/UltraCanvasTooltipManager.h"
//#include "../include/UltraCanvasZOrderManager.h"
#include "../include/UltraCanvasMenu.h"
#include "UltraCanvasApplication.h"

#include <iostream>
#include <algorithm>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {
    UltraCanvasWindowBase::UltraCanvasWindowBase()
            : UltraCanvasContainer("Window", 0, 0, 0, 0, 0) {
        // Configure container for window behavior
        visible = false;
        window = this;
    }

//    UltraCanvasWindowBase::~UltraCanvasWindowBase() {
//        UnregisterWindow();
//        debugOutput << "UltraCanvas: Window deleted" << std::endl;
//    }

    // ===== FOCUS MANAGEMENT IMPLEMENTATION =====

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
            case UCEventType::WindowCloseRequest:
                RequestClose();
                return true;

            case UCEventType::WindowResize:
                HandleResizeEvent(event.width, event.height);
                return true;

            case UCEventType::WindowMove:
                HandleMoveEvent(event.x, event.y);
                return true;

            case UCEventType::WindowFocus:
                HandleFocusEvent(true);
                return true;

            case UCEventType::WindowBlur:
                HandleFocusEvent(false);
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

    void UltraCanvasWindowBase::HandleFocusEvent(bool focused) {
        if (focused) {
            if (!_focused) {
                _focused = true;
                if (onWindowFocus) onWindowFocus();
            }
        } else {
            if (_focused) {
                _focused = false;
                if (onWindowBlur) onWindowBlur();
            }
        }
        needsRedraw = true;
    }

    void UltraCanvasWindowBase::Render(IRenderContext* ctx) {
        if (!visible || !_created) return;
        UltraCanvasContainer::Render(ctx);

        RenderCustomContent(ctx);

        UltraCanvasTooltipManager::Render(ctx, this);
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

        ContainerStyle containerStyle;
        containerStyle.forceShowVerticalScrollbar = config_.enableWindowScrolling;
        containerStyle.forceShowHorizontalScrollbar = config_.enableWindowScrolling;
        SetContainerStyle(containerStyle);
        SetBackgroundColor(config_.backgroundColor);

        auto application = UltraCanvasApplication::GetInstance();
        SetBounds(Rect2Di(0, 0, config_.width, config_.height));

        if (CreateNative()) {
            UltraCanvasApplication::GetInstance()->RegisterWindow(std::dynamic_pointer_cast<UltraCanvasWindowBase>(shared_from_this()));
            _created = true;
        } else {
            _created = false;
        }
        return _created;
    }

    void UltraCanvasWindowBase::Destroy() {
        if (!_created || _state == WindowState::Deleted) {
            return;
        }
        CloseAllPopups();

        DestroyNative();
        _created = false;
        _state = WindowState::Deleted;
    }

    void UltraCanvasWindowBase::RequestDelete() {
        _state = WindowState::DeleteRequested;
    }

    bool UltraCanvasWindowBase::RequestClose() {
        if (!_created || _state == WindowState::Closing 
            || _state == WindowState::DeleteRequested
            || _state == WindowState::Deleted) {
            return false;
        }
        if (!onWindowCloseRequest || onWindowCloseRequest()) {        
            Close();
            return true;
        } else {
            return false;
        }
    }

    void UltraCanvasWindowBase::Close() {
        if (!_created || _state == WindowState::Closing
            || _state == WindowState::DeleteRequested
            || _state == WindowState::Deleted) {
            return;
        }

        _state = WindowState::Closing;
        debugOutput << "UltraCanvas: Window close requested" << std::endl;

        CloseAllPopups();

        if (onWindowClose) {
            onWindowClose();
        } else {
            UCEvent ev;
            ev.type = UCEventType::WindowClose;
            ev.targetWindow = this;
            UltraCanvasApplication::GetInstance()->PushEvent(ev);
        }

        if (config_.deleteOnClose) {
            RequestDelete();
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