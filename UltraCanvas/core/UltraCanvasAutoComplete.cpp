// core/UltraCanvasAutoComplete.cpp
// AutoComplete text input with popup suggestion list (inherits TextInput, uses ListView popup)
// Version: 4.0.0
// Last Modified: 2026-03-29
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
        CreatePopupListView();
        WireListViewCallbacks();
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
            CloseAutocompletePopup();
        }
    }

    // ===== SELECTED ITEM =====

    const AutoCompleteItem* UltraCanvasAutoComplete::GetSelectedItem() const {
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(filteredItems.size())) {
            return &filteredItems[selectedIndex];
        }
        return nullptr;
    }

    // ===== POPUP STATE =====

    void UltraCanvasAutoComplete::OpenAutocompletePopup() {
        if (!popupOpen && !filteredItems.empty()) {
            popupOpen = true;
            PopulateListFromFiltered();

            Point2Di pos = MapFromLocal({0, GetHeight()});
            popupListView->SetWidth(GetWidth());

            PopupElementSettings settings;
            settings.popupOwner = shared_from_this();
//            if (isPopup) {
//                settings.closeByEscapeKey = false;
//                settings.closeByClickOutside = false;
//            } else {
                settings.closeByEscapeKey = true;
                settings.closeByClickOutside = true;
//            }

            GetWindow()->OpenPopup(pos, *popupListView, settings);

            if (autoSelectFirst && !filteredItems.empty()) {
                UCEvent downEvent;
                downEvent.type = UCEventType::KeyDown;
                downEvent.virtualKey = UCKeys::Down;
                popupListView->OnEvent(downEvent);
            }

            if (onAutocompletePopupOpened) onAutocompletePopupOpened();
            RequestRedraw();
        }
    }

    void UltraCanvasAutoComplete::CloseAutocompletePopup() {
        if (popupOpen) {
            popupOpen = false;
            if (GetWindow()) {
                GetWindow()->ClosePopup(*popupListView);
            }
            if (onAutocompletePopupClosed) onAutocompletePopupClosed();
        }
    }

    // ===== STYLING =====

    void UltraCanvasAutoComplete::SetAutocompleteStyle(const AutoCompleteStyle& newStyle) {
        acStyle = newStyle;
        ApplyStyleToListView();
        RequestRedraw();
    }

    // ===== WINDOW =====

    void UltraCanvasAutoComplete::SetWindow(UltraCanvasWindowBase* win) {
        UltraCanvasTextInput::SetWindow(win);
        if (popupListView) popupListView->SetWindow(win);
    }

    // ===== TEXT CHANGED (VIRTUAL OVERRIDE) =====

    void UltraCanvasAutoComplete::TextChanged() {
        const std::string& currentText = GetText();
        FilterSuggestions(currentText);

        if (static_cast<int>(currentText.length()) >= minCharsToTrigger) {
            if (!filteredItems.empty()) {
                OpenAutocompletePopup();
//            } else {
//                CloseAutocompletePopup();
            }
        } else {
            CloseAutocompletePopup();
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
                            return popupListView->OnEvent(event);

                        case UCKeys::Escape:
                            CloseAutocompletePopup();
                            return true;

                        default:
                            break;
                    }
                } else {
                    if (event.virtualKey == UCKeys::Down || event.virtualKey == UCKeys::PageDown) {
                        OpenAutocompletePopup();
                        return true;
                    }
                }
                return UltraCanvasTextInput::OnEvent(event);

            case UCEventType::FocusGained:
                if (minCharsToTrigger == 0) {
                    FilterSuggestions(GetText());
                    if (!filteredItems.empty()) {
                        OpenAutocompletePopup();
                    }
                }
                return UltraCanvasTextInput::OnEvent(event);

            case UCEventType::FocusLost:
                CloseAutocompletePopup();
                return UltraCanvasTextInput::OnEvent(event);

            default:
                return UltraCanvasTextInput::OnEvent(event);
        }
    }

    // ===== SELECTION =====

    void UltraCanvasAutoComplete::SelectItem(int filteredIndex) {
        if (filteredIndex < 0 || filteredIndex >= static_cast<int>(filteredItems.size())) return;

        const AutoCompleteItem& item = filteredItems[filteredIndex];
        selectedIndex = filteredIndex;

        // Set text without triggering re-filtering
        auto prevOnTextChanged = onTextChanged;
        onTextChanged = nullptr;
        SetText(item.text);
        onTextChanged = prevOnTextChanged;

        if (onItemSelected) {
            onItemSelected(filteredIndex, item);
        }

        if (closeOnSelect) {
            CloseAutocompletePopup();
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

        // Re-populate list if popup is open
        if (popupOpen) {
            PopulateListFromFiltered();
//            Point2Di pos = CalculatePopupPosition();
//            popupListView->SetPosition(pos.x, pos.y);

            if (autoSelectFirst && !filteredItems.empty()) {
                UCEvent downEvent;
                downEvent.type = UCEventType::KeyDown;
                downEvent.virtualKey = UCKeys::Down;
                popupListView->OnEvent(downEvent);
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

    // ===== PRIVATE SETUP =====

    void UltraCanvasAutoComplete::CreatePopupListView() {
        popupListView = std::make_shared<UltraCanvasListView>(
            GetIdentifier() + "_popup_lv", 0, 0, 0, 200, 100);
        popupListView->SetModel(&listModel);
        popupListView->SetShowHeader(false);

        auto delegate = std::make_shared<UltraCanvasDefaultListDelegate>();
        delegate->SetTextPadding(8);
        popupListView->SetDelegate(delegate);

        auto sel = std::make_shared<UltraCanvasSingleSelection>();
        popupListView->SetSelection(sel);

        ApplyStyleToListView();
    }

    void UltraCanvasAutoComplete::WireListViewCallbacks() {
        popupListView->onItemActivated = [this](int row) {
            SelectItem(row);
        };

        popupListView->onItemClicked = [this](int row) {
            SelectItem(row);
        };

        popupListView->onPopupClosed = [this](ClosePopupReason /*reason*/) {
            if (popupOpen) {
                popupOpen = false;
                if (onAutocompletePopupClosed) onAutocompletePopupClosed();
            }
        };
    }

    void UltraCanvasAutoComplete::ApplyStyleToListView() {
        if (!popupListView) return;

        ListViewStyle lvStyle;
        lvStyle.backgroundColor = acStyle.listBackgroundColor;
        lvStyle.rowHeight = static_cast<int>(acStyle.itemHeight);
        lvStyle.showHeader = false;
        lvStyle.showGridLines = false;
        lvStyle.alternateRowColors = false;
        lvStyle.hoverBackgroundColor = acStyle.itemHoverColor;
        lvStyle.selectionBackgroundColor = acStyle.itemHoverColor;
        lvStyle.scrollbarStyle = acStyle.scrollbarStyle;

        popupListView->SetStyle(lvStyle);
        popupListView->SetBorders(static_cast<int>(acStyle.borderWidth), acStyle.listBorderColor);

        auto* delegate = dynamic_cast<UltraCanvasDefaultListDelegate*>(popupListView->GetDelegate());
        if (delegate) {
            delegate->SetFontSize(acStyle.fontSize);
            delegate->SetTextColor(acStyle.itemTextColor);
            delegate->SetSelectedTextColor(acStyle.itemTextColor);
            delegate->SetTextPadding(8);
            delegate->SetRowHeight(static_cast<int>(acStyle.itemHeight));
        }
    }

    void UltraCanvasAutoComplete::PopulateListFromFiltered() {
        std::vector<ListItem> items;
        items.reserve(filteredItems.size());
        for (const auto& acItem : filteredItems) {
            items.emplace_back(acItem.text);
        }
        listModel.SetItems(items);

        CalculateAndSetPopupSize();
    }

    void UltraCanvasAutoComplete::CalculateAndSetPopupSize() {
        int itemCount = static_cast<int>(filteredItems.size());
        int visibleItems = std::min(itemCount, acStyle.maxVisibleItems);
        int listHeight = visibleItems * static_cast<int>(acStyle.itemHeight);
        listHeight += static_cast<int>(acStyle.borderWidth) * 2;

        int listWidth = std::max(GetBounds().width, 100);
        if (acStyle.maxPopupWidth > 0) {
            listWidth = std::min(listWidth, acStyle.maxPopupWidth);
        }

        popupListView->SetSize(listWidth, listHeight);
    }

} // namespace UltraCanvas
