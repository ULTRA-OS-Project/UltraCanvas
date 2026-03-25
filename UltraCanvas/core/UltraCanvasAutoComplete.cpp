// core/UltraCanvasAutoComplete.cpp
// AutoComplete text input with popup suggestion list (inherits TextInput, composes Menu)
// Version: 3.0.0
// Last Modified: 2026-03-25
// Author: UltraCanvas Framework
#include "UltraCanvasAutoComplete.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasApplication.h"

#include <cctype>

namespace UltraCanvas {

    UltraCanvasAutoComplete::UltraCanvasAutoComplete(const std::string& identifier, long id,
                                                     long x, long y, long w, long h)
        : UltraCanvasTextInput(identifier, id, x, y, w, h) {
        SetShowValidationState(false);
        CreatePopupMenu();
        WireCallbacks();
    }

    UltraCanvasAutoComplete::~UltraCanvasAutoComplete() = default;

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
        UltraCanvasTextInput::SetText(text, false);
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
        acStyle = newStyle;
        ApplyStyleToMenu();
        RequestRedraw();
    }

    // ===== WINDOW =====

    void UltraCanvasAutoComplete::SetWindow(UltraCanvasWindowBase* win) {
        UltraCanvasTextInput::SetWindow(win);
        if (popupMenu) popupMenu->SetWindow(win);
    }

    // ===== TEXT CHANGED (VIRTUAL OVERRIDE) =====

    void UltraCanvasAutoComplete::TextChanged() {
        const std::string& currentText = GetText();
        FilterSuggestions(currentText);

        if (static_cast<int>(currentText.length()) >= minCharsToTrigger) {
            if (!filteredItems.empty()) {
                OpenPopup();
            } else {
                ClosePopup();
            }
        } else {
            ClosePopup();
        }

        UltraCanvasTextInput::TextChanged();  // fires onTextChanged callback
    }

    // ===== EVENT HANDLING =====

    bool UltraCanvasAutoComplete::OnEvent(const UCEvent& event) {
        switch (event.type) {
            case UCEventType::KeyDown:
                // Handle popup navigation BEFORE base TextInput processes the key
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
                            break;
                    }
                } else {
                    if (event.virtualKey == UCKeys::Down || event.virtualKey == UCKeys::PageDown) {
                        OpenPopup();
                        return true;
                    }
                }
                return UltraCanvasTextInput::OnEvent(event);

            // case UCEventType::MouseUp:
            //     return HandleMouseUp(event);

            // case UCEventType::MouseMove:
            //     return HandleMouseMove(event);

            // case UCEventType::MouseWheel:
            //     if (popupOpen && popupMenu->Contains(event.x, event.y)) {
            //         return popupMenu->HandleEvent(event);
            //     }
            //     return UltraCanvasTextInput::OnEvent(event);

            case UCEventType::FocusGained:
                if (minCharsToTrigger == 0) {
                    FilterSuggestions(GetText());
                    if (!filteredItems.empty()) {
                        OpenPopup();
                    }
                }
                return UltraCanvasTextInput::OnEvent(event);

            case UCEventType::FocusLost:
                ClosePopup();
                return UltraCanvasTextInput::OnEvent(event);

            default:
                return UltraCanvasTextInput::OnEvent(event);
        }
    }

    // bool UltraCanvasAutoComplete::HandleMouseUp(const UCEvent& event) {
    //     if (popupOpen && popupMenu->Contains(event.x, event.y)) {
    //         return popupMenu->HandleEvent(event);
    //     }
    //     return UltraCanvasTextInput::OnEvent(event);
    // }

    // bool UltraCanvasAutoComplete::HandleMouseMove(const UCEvent& event) {
    //     if (popupOpen) {
    //         popupMenu->HandleEvent(event);
    //     }
    //     return UltraCanvasTextInput::OnEvent(event);
    // }

    // ===== SELECTION =====

    void UltraCanvasAutoComplete::SelectItem(int filteredIndex) {
        if (filteredIndex < 0 || filteredIndex >= static_cast<int>(filteredItems.size())) return;

        const AutoCompleteItem& item = filteredItems[filteredIndex];
        selectedIndex = filteredIndex;

        // Set text without triggering re-filtering
        UltraCanvasTextInput::SetText(item.text, false);

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
            filteredItems = onRequestSuggestions(query);
        } else {
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

        return Point2Di(globalPos.x, globalPos.y + inputRect.height);
    }

    // ===== PRIVATE SETUP =====

    void UltraCanvasAutoComplete::CreatePopupMenu() {
        popupMenu = std::make_shared<UltraCanvasMenu>(
            GetIdentifier() + "_popup", 0, 0, 0, 200, 100);
        popupMenu->SetMenuType(MenuType::PopupMenu);
        ApplyStyleToMenu();
    }

    void UltraCanvasAutoComplete::WireCallbacks() {
        popupMenu->OnMenuClosed([this]() {
            ClosePopup();
        });
    }

    void UltraCanvasAutoComplete::ApplyStyleToMenu() {
        if (!popupMenu) return;

        MenuStyle menuStyle;
        menuStyle.backgroundColor = acStyle.listBackgroundColor;
        menuStyle.borderColor = acStyle.listBorderColor;
        menuStyle.hoverColor = acStyle.itemHoverColor;
        menuStyle.hoverTextColor = acStyle.itemTextColor;
        menuStyle.textColor = acStyle.itemTextColor;
        menuStyle.borderWidth = static_cast<int>(acStyle.borderWidth);
        menuStyle.itemHeight = static_cast<int>(acStyle.itemHeight);
        menuStyle.borderRadius = 0;
        menuStyle.showShadow = false;
        menuStyle.paddingLeft = 8;
        menuStyle.paddingRight = 8;
        menuStyle.paddingTop = 0;
        menuStyle.paddingBottom = 0;
        menuStyle.scrollbarStyle = acStyle.scrollbarStyle;
        menuStyle.minWidth = GetBounds().width;

        menuStyle.font.fontFamily = acStyle.fontFamily;
        menuStyle.font.fontSize = acStyle.fontSize;

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
