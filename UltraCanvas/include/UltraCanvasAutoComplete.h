// include/UltraCanvasAutoComplete.h
// AutoComplete text input with popup suggestion list (composite: TextInput + Menu)
// Version: 2.0.0
// Last Modified: 2026-03-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <algorithm>

namespace UltraCanvas {

// ===== AUTOCOMPLETE ITEM DATA =====
    struct AutoCompleteItem {
        std::string text;       // Display text (what the user sees)
        std::string value;      // Programmatic key
        void* userData = nullptr;

        AutoCompleteItem() = default;
        AutoCompleteItem(const std::string& itemText)
            : text(itemText), value(itemText) {}
        AutoCompleteItem(const std::string& itemText, const std::string& itemValue)
            : text(itemText), value(itemValue) {}
    };

// ===== AUTOCOMPLETE STYLING =====
    struct AutoCompleteStyle {
        // List/popup appearance
        Color listBackgroundColor = Colors::White;
        Color listBorderColor = Color(180, 180, 180, 255);
        Color itemHoverColor = Color(240, 245, 255, 255);
        Color itemTextColor = Colors::Black;

        // Dimensions
        float borderWidth = 1.0f;
        float itemHeight = 24.0f;
        int maxVisibleItems = 8;
        int maxPopupWidth = 400;

        // Font (for popup items; TextInput has its own font style)
        std::string fontFamily = "Sans";
        float fontSize = 12.0f;

        // Scrollbar
        ScrollbarStyle scrollbarStyle = ScrollbarStyle::DropDown();
    };

// ===== AUTOCOMPLETE COMPONENT =====
    class UltraCanvasAutoComplete : public UltraCanvasUIElement {
    public:
        // ===== CALLBACKS =====
        std::function<void(int, const AutoCompleteItem&)> onItemSelected;
        std::function<void(const std::string&)> onTextChanged;
        std::function<void()> onPopupOpened;
        std::function<void()> onPopupClosed;

        // Dynamic suggestion provider callback
        // Called with current text; should return filtered suggestions
        std::function<std::vector<AutoCompleteItem>(const std::string&)> onRequestSuggestions;

    private:
        std::shared_ptr<UltraCanvasTextInput> textInput;
        std::shared_ptr<UltraCanvasMenu> popupMenu;

        std::vector<AutoCompleteItem> allItems;         // Full static list
        std::vector<AutoCompleteItem> filteredItems;    // Currently displayed (post-filter)

        AutoCompleteStyle style;
        bool popupOpen = false;
        int selectedIndex = -1;

        int minCharsToTrigger = 1;
        bool closeOnSelect = true;
        bool autoSelectFirst = false;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasAutoComplete(const std::string& identifier, long id,
                                long x, long y, long w, long h = 28);
        virtual ~UltraCanvasAutoComplete();

        // ===== ITEM MANAGEMENT (STATIC MODE) =====
        void AddItem(const std::string& text);
        void AddItem(const std::string& text, const std::string& value);
        void AddItem(const AutoCompleteItem& item);
        void SetItems(const std::vector<AutoCompleteItem>& items);
        void ClearItems();
        const std::vector<AutoCompleteItem>& GetAllItems() const { return allItems; }

        // ===== TEXT ACCESS (delegates to inner TextInput) =====
        void SetText(const std::string& text);
        const std::string& GetText() const;
        void SetPlaceholder(const std::string& placeholder);

        // ===== SELECTED ITEM =====
        const AutoCompleteItem* GetSelectedItem() const;
        int GetSelectedIndex() const { return selectedIndex; }

        // ===== POPUP STATE =====
        void OpenPopup();
        void ClosePopup();
        bool IsPopupOpen() const { return popupOpen; }

        // ===== STYLING =====
        void SetStyle(const AutoCompleteStyle& newStyle);
        const AutoCompleteStyle& GetStyle() const { return style; }
        void SetTextInputStyle(const TextInputStyle& inputStyle);

        // ===== BEHAVIOR =====
        int GetMinCharsToTrigger() const { return minCharsToTrigger; }
        void SetMinCharsToTrigger(int value) { minCharsToTrigger = value; }

        bool GetCloseOnSelect() const { return closeOnSelect; }
        void SetCloseOnSelect(bool value) { closeOnSelect = value; }

        bool GetAutoSelectFirst() const { return autoSelectFirst; }
        void SetAutoSelectFirst(bool value) { autoSelectFirst = value; }

        // ===== FOCUS =====
        bool AcceptsFocus() const override { return true; }

        // ===== WINDOW =====
        void SetWindow(UltraCanvasWindowBase* win) override;

        // ===== RENDERING =====
        void Render(IRenderContext* ctx) override;

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;
        bool OnWindowEventFilter(const UCEvent& event) override;

    private:
        void CreateTextInput();
        void CreatePopupMenu();
        void WireCallbacks();
        void ApplyStyleToMenu();
        void PopulateMenuFromFiltered();

        void FilterSuggestions(const std::string& query);
        bool MatchesFilter(const std::string& itemText, const std::string& query) const;

        Point2Di CalculatePopupPosition();
        void SelectItem(int filteredIndex);

        // Event routing
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);
        bool HandleMouseWheel(const UCEvent& event);
        void HandleFocusLost();
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasAutoComplete> CreateAutoComplete(
            const std::string& identifier, long id, long x, long y, long w, long h = 28) {
        return std::make_shared<UltraCanvasAutoComplete>(identifier, id, x, y, w, h);
    }

} // namespace UltraCanvas
