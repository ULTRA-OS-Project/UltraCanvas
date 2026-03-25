// core/UltraCanvasAutoComplete.cpp
// AutoComplete text input with popup suggestion list (composite: TextInput + Menu)
// Version: 2.0.0
// Last Modified: 2026-03-25
// Author: UltraCanvas Framework
#include "UltraCanvasAutoComplete.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasApplication.h"

#include <cctype>

namespace UltraCanvas {

    UltraCanvasAutoComplete::UltraCanvasAutoComplete(const std::string& identifier, long id,
                                                     long x, long y, long w, long h)
        : UltraCanvasUIElement(identifier, id, x, y, w, h) {
        CreateTextInput();
        CreatePopupMenu();
        WireCallbacks();
        UltraCanvasApplicationBase::InstallWindowEventFilter(this, {UCEventType::KeyDown});
    }

    UltraCanvasAutoComplete::~UltraCanvasAutoComplete() {
        UltraCanvasApplicationBase::UnInstallWindowEventFilter(this);
    }

    // ===== ITEM MANAGEMENT =====

    void UltraCanvasAutoComplete::AddItem(const std::string& text) {
        allItems.emplace_back(text);
    }

    void UltraCanvasAutoComplete::AddItem(const std::string& text, const std::string& value) {
        allItems.emplace_back(text, value);
    }

    void UltraCanvasAutoComplete::AddItem(const AutoCompleteItem& item) {
        allItems.push_back(item);
    }

    void UltraCanvasAutoComplete::SetItems(const std::vector<AutoCompleteItem>& items) {
        allItems = items;
        filteredItems.clear();
        selectedIndex = -1;
    }

    void UltraCanvasAutoComplete::ClearItems() {
        allItems.clear();
        filteredItems.clear();
        selectedIndex = -1;
        if (popupOpen) {
            ClosePopup();
        }
    }

    // ===== TEXT ACCESS =====

    void UltraCanvasAutoComplete::SetText(const std::string& text) {
        textInput->SetText(text, false);
    }

    const std::string& UltraCanvasAutoComplete::GetText() const {
        return textInput->GetText();
    }

    void UltraCanvasAutoComplete::SetPlaceholder(const std::string& placeholder) {
        textInput->SetPlaceholder(placeholder);
    }

    // ===== SELECTED ITEM =====

    const AutoCompleteItem* UltraCanvasAutoComplete::GetSelectedItem() const {
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(filteredItems.size())) {
            return &filteredItems[selectedIndex];
        }
        return nullptr;
    }

    // ===== POPUP STATE =====

    void UltraCanvasAutoComplete::OpenPopup() {
        if (!popupOpen && !filteredItems.empty()) {
            popupOpen = true;
            PopulateMenuFromFiltered();

            Point2Di pos = CalculatePopupPosition();
            popupMenu->ShowAt(pos.x, pos.y, false, false);

            if (autoSelectFirst && !filteredItems.empty()) {
                // Simulate a Down key press to select the first item in the menu
                UCEvent downEvent;
                downEvent.type = UCEventType::KeyDown;
                downEvent.virtualKey = UCKeys::Down;
                popupMenu->HandleEvent(downEvent);
            }

            if (onPopupOpened) onPopupOpened();
            RequestRedraw();
        }
    }

    void UltraCanvasAutoComplete::ClosePopup() {
        if (popupOpen) {
            popupOpen = false;
            popupMenu->Hide();
            if (onPopupClosed) onPopupClosed();
        }
    }

    // ===== STYLING =====

    void UltraCanvasAutoComplete::SetStyle(const AutoCompleteStyle& newStyle) {
        style = newStyle;
        ApplyStyleToMenu();
        RequestRedraw();
    }

    void UltraCanvasAutoComplete::SetTextInputStyle(const TextInputStyle& inputStyle) {
        textInput->SetStyle(inputStyle);
    }

    // ===== WINDOW =====

    void UltraCanvasAutoComplete::SetWindow(UltraCanvasWindowBase* win) {
        UltraCanvasUIElement::SetWindow(win);
        textInput->SetWindow(win);
        if (popupMenu) popupMenu->SetWindow(win);
    }

    // ===== RENDERING =====

    void UltraCanvasAutoComplete::Render(IRenderContext* ctx) {
        if (!IsVisible()) return;

        // Keep TextInput bounds in sync
        textInput->SetBounds(GetBounds());
        textInput->Render(ctx);
    }

    // ===== EVENT HANDLING =====

    bool UltraCanvasAutoComplete::OnEvent(const UCEvent& event) {
        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);
            case UCEventType::MouseUp:
                return HandleMouseUp(event);
            case UCEventType::MouseMove:
                return HandleMouseMove(event);
            case UCEventType::MouseWheel:
                return HandleMouseWheel(event);
            case UCEventType::FocusGained:
                textInput->SetFocus(true);
                return true;
            case UCEventType::FocusLost:
                HandleFocusLost();
                return false;
            default:
                return textInput->OnEvent(event);
        }
    }

    bool UltraCanvasAutoComplete::OnWindowEventFilter(const UCEvent& event) {
        if (textInput->IsFocused()) {
            if (popupOpen) {
                switch (event.virtualKey) {
                    case UCKeys::Down:
                    case UCKeys::Up:
                    case UCKeys::PageDown:
                    case UCKeys::PageUp:
                    case UCKeys::Return:
                        return popupMenu->HandleEvent(event);

                    case UCKeys::Escape:
                        ClosePopup();
                        return true;

                    default:
                        break; // Fall through to TextInput
                }
            } else {
                if (event.virtualKey == UCKeys::Down || event.virtualKey == UCKeys::PageDown) {
                    OpenPopup();
                    return true;
                }
            }
        }
        return false;
    }

    bool UltraCanvasAutoComplete::HandleMouseDown(const UCEvent& event) {
        if (GetBounds().Contains(event.x, event.y)) {
            return textInput->OnEvent(event);
        }
        return false;
    }

    bool UltraCanvasAutoComplete::HandleMouseUp(const UCEvent& event) {
        if (popupOpen && popupMenu->Contains(event.x, event.y)) {
            return popupMenu->HandleEvent(event);
        }
        return textInput->OnEvent(event);
    }

    bool UltraCanvasAutoComplete::HandleMouseMove(const UCEvent& event) {
        if (popupOpen) {
            // Forward to menu for hover tracking
            popupMenu->HandleEvent(event);
        }

        return textInput->OnEvent(event);
    }

    bool UltraCanvasAutoComplete::HandleMouseWheel(const UCEvent& event) {
        if (!popupOpen) return false;

        if (popupMenu->Contains(event.x, event.y)) {
            return popupMenu->HandleEvent(event);
        }

        return false;
    }

    void UltraCanvasAutoComplete::HandleFocusLost() {
        ClosePopup();
        if (window && window->GetFocusedElement() != textInput.get()) {
            UCEvent focusEvent;
            focusEvent.type = UCEventType::FocusLost;
            textInput->OnEvent(focusEvent);
        }
    }

    // ===== SELECTION =====

    void UltraCanvasAutoComplete::SelectItem(int filteredIndex) {
        if (filteredIndex < 0 || filteredIndex >= static_cast<int>(filteredItems.size())) return;

        const AutoCompleteItem& item = filteredItems[filteredIndex];
        selectedIndex = filteredIndex;

        // Set text input to the selected item's text (suppress re-filtering)
        textInput->SetText(item.text, false);

        if (onItemSelected) {
            onItemSelected(filteredIndex, item);
        }

        if (closeOnSelect) {
            ClosePopup();
        }
    }

    // ===== FILTERING =====

    void UltraCanvasAutoComplete::FilterSuggestions(const std::string& query) {
        if (onRequestSuggestions) {
            // Dynamic provider mode
            filteredItems = onRequestSuggestions(query);
        } else {
            // Static filtering: case-insensitive substring match
            filteredItems.clear();
            for (const auto& item : allItems) {
                if (MatchesFilter(item.text, query)) {
                    filteredItems.push_back(item);
                }
            }
        }

        selectedIndex = -1;

        // Re-populate menu if popup is open
        if (popupOpen) {
            PopulateMenuFromFiltered();
            Point2Di pos = CalculatePopupPosition();
            popupMenu->SetPosition(pos.x, pos.y);

            if (autoSelectFirst && !filteredItems.empty()) {
                UCEvent downEvent;
                downEvent.type = UCEventType::KeyDown;
                downEvent.virtualKey = UCKeys::Down;
                popupMenu->HandleEvent(downEvent);
            }
        }
    }

    bool UltraCanvasAutoComplete::MatchesFilter(const std::string& itemText, const std::string& query) const {
        if (query.empty()) return true;

        // Case-insensitive substring match
        auto toLower = [](const std::string& s) {
            std::string result = s;
            for (auto& c : result) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return result;
        };

        return toLower(itemText).find(toLower(query)) != std::string::npos;
    }

    // ===== POPUP POSITIONING =====

    Point2Di UltraCanvasAutoComplete::CalculatePopupPosition() {
        Point2Di globalPos = GetPositionInWindow();
        Rect2Di inputRect = GetBounds();

        // Position below the input field; Menu's ClampMenuToWindow
        // will flip above if there isn't enough space below.
        return Point2Di(globalPos.x, globalPos.y + inputRect.height);
    }

    // ===== PRIVATE SETUP =====

    void UltraCanvasAutoComplete::CreateTextInput() {
        Rect2Di b = GetBounds();
        textInput = std::make_shared<UltraCanvasTextInput>(
            GetIdentifier() + "_input", 0, b.x, b.y, b.width, b.height);
        textInput->SetShowValidationState(false);
    }

    void UltraCanvasAutoComplete::CreatePopupMenu() {
        popupMenu = std::make_shared<UltraCanvasMenu>(
            GetIdentifier() + "_popup", 0, 0, 0, 200, 100);
        popupMenu->SetMenuType(MenuType::PopupMenu);
        ApplyStyleToMenu();
    }

    void UltraCanvasAutoComplete::WireCallbacks() {
        textInput->onTextChanged = [this](const std::string& text) {
            FilterSuggestions(text);

            if (static_cast<int>(text.length()) >= minCharsToTrigger) {
                if (!filteredItems.empty()) {
                    OpenPopup();
                } else {
                    ClosePopup();
                }
            } else {
                ClosePopup();
            }

            if (onTextChanged) onTextChanged(text);
        };

        textInput->onFocusGained = [this]() {
            if (minCharsToTrigger == 0) {
                FilterSuggestions(GetText());
                if (!filteredItems.empty()) {
                    OpenPopup();
                }
            }
        };

        textInput->onFocusLost = [this]() {
            ClosePopup();
        };

        popupMenu->OnMenuClosed([this]() {
            ClosePopup();
        });
    }

    void UltraCanvasAutoComplete::ApplyStyleToMenu() {
        if (!popupMenu) return;

        MenuStyle menuStyle;
        menuStyle.backgroundColor = style.listBackgroundColor;
        menuStyle.borderColor = style.listBorderColor;
        menuStyle.hoverColor = style.itemHoverColor;
        menuStyle.hoverTextColor = style.itemTextColor;
        menuStyle.textColor = style.itemTextColor;
        menuStyle.borderWidth = static_cast<int>(style.borderWidth);
        menuStyle.itemHeight = static_cast<int>(style.itemHeight);
        menuStyle.borderRadius = 0;
        menuStyle.showShadow = false;
        menuStyle.paddingLeft = 8;
        menuStyle.paddingRight = 8;
        menuStyle.paddingTop = 0;
        menuStyle.paddingBottom = 0;
        menuStyle.scrollbarStyle = style.scrollbarStyle;
        menuStyle.minWidth = GetBounds().width;

        // Map font
        menuStyle.font.fontFamily = style.fontFamily;
        menuStyle.font.fontSize = style.fontSize;

        popupMenu->SetStyle(menuStyle);
    }

    void UltraCanvasAutoComplete::PopulateMenuFromFiltered() {
        popupMenu->Clear();
        for (int i = 0; i < static_cast<int>(filteredItems.size()); i++) {
            int idx = i;
            popupMenu->AddItem(MenuItemData::Action(
                filteredItems[i].text,
                [this, idx]() { SelectItem(idx); }
            ));
        }

        // Update minWidth in case input bounds changed
        MenuStyle menuStyle = popupMenu->GetStyle();
        menuStyle.minWidth = GetBounds().width;
        popupMenu->SetStyle(menuStyle);
    }

} // namespace UltraCanvas
