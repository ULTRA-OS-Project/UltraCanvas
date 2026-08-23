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

    UltraCanvasAutoComplete::UltraCanvasAutoComplete(const std::string& identifier,
                                                     float x, float y, float w, float h)
        : UltraCanvasTextInput(identifier, x, y, w, h) {
        listModel = std::make_shared<UltraCanvasSimpleListModel>();
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
        selectedIndex = -1;
        if (popupOpen) {
            // The open popup shows rows built from filteredItems, and SelectItem()
            // resolves a clicked row against that same vector. Clearing it here would
            // leave a full list on screen wired to nothing: every click and Enter would
            // be silently ignored. Re-filter instead, so list and data stay in step.
            FilterSuggestions(GetText());
        } else {
            filteredItems.clear();
        }
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

            popupListView->SetWidth(GetWidth());
            Point2Di pos = CalculatePopupPosition();

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
            popupPlaced = true;

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
            popupPlaced = false;
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

    void UltraCanvasAutoComplete::RefreshSuggestions() {
        const std::string& currentText = GetText();
        FilterSuggestions(currentText);

        if (static_cast<int>(currentText.length()) >= minCharsToTrigger && !filteredItems.empty()) {
            OpenAutocompletePopup();
        } else {
            CloseAutocompletePopup();
        }
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

        // A copy, not a reference: the callbacks below may replace the item list
        // (SetItems() from an onItemSelected handler is a normal thing to do), which
        // would leave a reference into filteredItems dangling mid-call.
        const AutoCompleteItem item = filteredItems[filteredIndex];
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
            GetIdentifier() + "_popup_lv", 0, 0, 200, 100);
        popupListView->SetModel(listModel);
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

        // Omnibox mode: moving the highlight with Up/Down copies the row's text into the input so the
        // field always reflects what will be navigated to. (Clearing selection sends an empty vector.)
        popupListView->onSelectionChanged = [this](const std::vector<int>& sel) {
            if (!omniboxMode || sel.empty())
                return;
            int row = sel.front();
            if (row < 0 || row >= static_cast<int>(filteredItems.size()))
                return;
            selectedIndex = row;
            auto prevOnTextChanged = onTextChanged;
            onTextChanged = nullptr;
            SetText(filteredItems[row].text);
            SetCaretPosition(GetText().length());
            onTextChanged = prevOnTextChanged;
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
        listModel->SetItems(items);

        // Omnibox mode: the list contents just changed, so drop any stale highlight/focus — the inline
        // completion in the text field is the default, and the first Up/Down starts from the top row.
        if (omniboxMode && popupListView) {
            popupListView->ResetSelection();
            selectedIndex = -1;
        }

        CalculateAndSetPopupSize();
    }

    void UltraCanvasAutoComplete::CalculateAndSetPopupSize() {
        int itemCount = static_cast<int>(filteredItems.size());
        int visibleItems = std::min(itemCount, acStyle.maxVisibleItems);
        int itemHeight = std::max(1, static_cast<int>(acStyle.itemHeight));
        int borders = static_cast<int>(acStyle.borderWidth) * 2;
        int listHeight = visibleItems * itemHeight + borders;

        // The popup is composited into the window surface, so whatever does not fit in
        // the window is simply cut off — those rows can be neither seen nor clicked.
        // Clamp to the space actually available and let the ListView scroll the rest.
        int availableHeight = AvailablePopupHeight();
        if (availableHeight > 0 && listHeight > availableHeight) {
            listHeight = std::max(availableHeight, itemHeight + borders);
        }

        int listWidth = std::max(static_cast<int>(GetBounds().width), 100);
        if (acStyle.maxPopupWidth > 0) {
            listWidth = std::min(listWidth, acStyle.maxPopupWidth);
        }

        popupListView->SetElementSize(Size2Df(listWidth, listHeight));

        // An open popup that was flipped above the input (or pushed left) has to be
        // re-anchored whenever its size changes, e.g. while the user narrows the filter.
        if (popupOpen && popupPlaced) {
            Point2Di newPos = CalculatePopupPosition();
            Point2Df oldPos = popupListView->GetPositionInWindow();
            if (static_cast<int>(oldPos.x) != newPos.x || static_cast<int>(oldPos.y) != newPos.y) {
                if (auto* win = GetWindow()) {
                    // Repaint the strip the popup is vacating, then move it.
                    win->AddDirtyRectangle(Rect2Di(static_cast<int>(oldPos.x),
                                                   static_cast<int>(oldPos.y),
                                                   static_cast<int>(popupListView->GetBounds().width),
                                                   static_cast<int>(popupListView->GetBounds().height)));
                    win->RequestWindowComposition();
                }
                popupListView->SetElementAbsolutePosition(Point2Df(newPos.x, newPos.y));
            }
        }
    }

    int UltraCanvasAutoComplete::AvailablePopupHeight() const {
        auto* win = GetWindow();
        if (!win) return 0;

        float inputTop = GetPositionInWindow().y;
        float spaceBelow = win->GetHeight() - (inputTop + GetHeight());
        float spaceAbove = inputTop;
        return static_cast<int>(std::max(0.0f, std::max(spaceBelow, spaceAbove)));
    }

    Point2Di UltraCanvasAutoComplete::CalculatePopupPosition() const {
        Point2Df inputPos = GetPositionInWindow();
        float popupWidth = popupListView->GetBounds().width;
        float popupHeight = popupListView->GetBounds().height;

        auto* win = GetWindow();
        float windowWidth = win ? win->GetWidth() : popupWidth;
        float windowHeight = win ? win->GetHeight() : (inputPos.y + GetHeight() + popupHeight);

        float spaceBelow = windowHeight - (inputPos.y + GetHeight());
        float spaceAbove = inputPos.y;

        // Below the input is the natural place; go above only when the list does not
        // fit below and there is genuinely more room up there.
        float y;
        if (popupHeight <= spaceBelow || spaceBelow >= spaceAbove) {
            y = inputPos.y + GetHeight();
        } else {
            y = std::max(0.0f, inputPos.y - popupHeight);
        }

        float x = inputPos.x;
        if (x + popupWidth > windowWidth) {
            x = std::max(0.0f, windowWidth - popupWidth);
        }

        return Point2Di(static_cast<int>(x), static_cast<int>(y));
    }

} // namespace UltraCanvas
