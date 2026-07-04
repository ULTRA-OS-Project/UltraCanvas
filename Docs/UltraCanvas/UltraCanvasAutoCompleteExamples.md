# UltraCanvasAutoComplete Documentation

## Overview

**UltraCanvasAutoComplete** is a text input control with a popup suggestion list. It inherits from `UltraCanvasTextInput` and uses an internal `UltraCanvasListView` to display filtered suggestions as the user types. Items can be supplied as a static list or generated on demand through a dynamic suggestion provider callback.

**Version:** 4.0.0
**Header:** `include/UltraCanvasAutoComplete.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasTextInput`

## Features

- **Static and Dynamic Modes**: Pre-populated item lists or runtime suggestion callbacks
- **Substring Filtering**: Filters items as the user types
- **Scrollable Popup**: Built-in scrollbar for long suggestion lists
- **Item Values**: Each item has a display text and a separate programmatic value
- **Configurable Trigger**: Minimum character count before the popup opens
- **Keyboard Navigation**: Inherits text input keyboard handling and adds popup navigation
- **Event Callbacks**: Selection, popup open/close, and request-suggestions events

## Header Include

```cpp
#include "UltraCanvasAutoComplete.h"
```

## Class Reference

### Constructor

```cpp
UltraCanvasAutoComplete(const std::string& identifier,
                        long x, long y, long w, long h = 28);
```

### Factory Function

```cpp
std::shared_ptr<UltraCanvasAutoComplete> CreateAutoComplete(
    const std::string& identifier, long x, long y, long w, long h = 28);
```

### AutoCompleteItem Structure

```cpp
struct AutoCompleteItem {
    std::string text;       // Display text (what the user sees)
    std::string value;      // Programmatic key
    void* userData = nullptr;

    AutoCompleteItem() = default;
    AutoCompleteItem(const std::string& itemText);
    AutoCompleteItem(const std::string& itemText, const std::string& itemValue);
};
```

### Item Management (Static Mode)

```cpp
void AddItem(const std::string& text);
void AddItem(const std::string& text, const std::string& value);
void AddItem(const AutoCompleteItem& item);
void SetItems(const std::vector<AutoCompleteItem>& items);
void ClearItems();
const std::vector<AutoCompleteItem>& GetAllItems() const;
```

### Selected Item

```cpp
const AutoCompleteItem* GetSelectedItem() const;
int GetSelectedIndex() const;
```

### Popup State

```cpp
void OpenAutocompletePopup();
void CloseAutocompletePopup();
bool IsPopupOpen() const;
```

### Styling

```cpp
void SetAutocompleteStyle(const AutoCompleteStyle& newStyle);
const AutoCompleteStyle& GetAutoCompleteStyle() const;
```

### Behavior

```cpp
int  GetMinCharsToTrigger() const;
void SetMinCharsToTrigger(int value);

bool GetCloseOnSelect() const;
void SetCloseOnSelect(bool value);

bool GetAutoSelectFirst() const;
void SetAutoSelectFirst(bool value);
```

## AutoCompleteStyle Structure

```cpp
struct AutoCompleteStyle {
    // List/popup appearance
    Color listBackgroundColor = Colors::White;
    Color listBorderColor     = Color(180, 180, 180, 255);
    Color itemHoverColor      = Color(240, 245, 255, 255);
    Color itemTextColor       = Colors::Black;

    // Dimensions
    float borderWidth     = 1.0f;
    float itemHeight      = 24.0f;
    int   maxVisibleItems = 8;
    int   maxPopupWidth   = 400;

    // Font (popup items)
    std::string fontFamily;
    float fontSize = 11.0f;

    // Scrollbar
    ScrollbarStyle scrollbarStyle = ScrollbarStyle::DropDown();
};
```

## Events and Callbacks

```cpp
// Called when an item is selected (mouse click or Enter)
std::function<void(int, const AutoCompleteItem&)> onItemSelected;

// Called when the popup opens
std::function<void()> onAutocompletePopupOpened;

// Called when the popup closes
std::function<void()> onAutocompletePopupClosed;

// Dynamic suggestion provider: called with current text,
// should return filtered suggestions for the popup
std::function<std::vector<AutoCompleteItem>(const std::string&)> onRequestSuggestions;
```

In addition, callbacks inherited from `UltraCanvasTextInput` (such as `onTextChanged`) remain available.

## Usage Examples

### Basic Static AutoComplete

```cpp
auto fruitAC = CreateAutoComplete("FruitAC", 30, 100, 250);
fruitAC->SetPlaceholder("Type a fruit name...");
fruitAC->AddItem("Apple", "apple");
fruitAC->AddItem("Apricot", "apricot");
fruitAC->AddItem("Banana", "banana");
fruitAC->AddItem("Blueberry", "blueberry");
fruitAC->AddItem("Cherry", "cherry");
fruitAC->AddItem("Grape", "grape");
fruitAC->AddItem("Lemon", "lemon");
fruitAC->AddItem("Mango", "mango");
fruitAC->AddItem("Orange", "orange");
fruitAC->AddItem("Peach", "peach");
fruitAC->AddItem("Pear", "pear");
fruitAC->AddItem("Pineapple", "pineapple");
fruitAC->AddItem("Strawberry", "strawberry");
fruitAC->AddItem("Watermelon", "watermelon");

// Open popup as soon as the field gets focus, even with empty input
fruitAC->SetMinCharsToTrigger(0);

fruitAC->onItemSelected = [](int index, const AutoCompleteItem& item) {
    std::cerr << "Selected: " << item.text
              << " (value: " << item.value << ")" << std::endl;
};

container->AddChild(fruitAC);
```

### Display Text vs. Programmatic Value (Country Codes)

The `text` field is what the user sees; the `value` field is the key your code uses.

```cpp
auto countryAC = CreateAutoComplete("CountryAC", 30, 200, 280);
countryAC->SetPlaceholder("Search countries...");
countryAC->AddItem("Argentina",      "AR");
countryAC->AddItem("Australia",      "AU");
countryAC->AddItem("Brazil",         "BR");
countryAC->AddItem("Canada",         "CA");
countryAC->AddItem("France",         "FR");
countryAC->AddItem("Germany",        "DE");
countryAC->AddItem("Japan",          "JP");
countryAC->AddItem("United Kingdom", "GB");
countryAC->AddItem("United States",  "US");

countryAC->onItemSelected = [](int index, const AutoCompleteItem& item) {
    // item.text  = "United States"
    // item.value = "US"
    std::cerr << "Country code: " << item.value << std::endl;
};
```

### Dynamic Suggestion Provider

Instead of preloading a static list, supply an `onRequestSuggestions` callback that returns suggestions for the current query.

```cpp
auto dynamicAC = CreateAutoComplete("DynamicAC", 30, 300, 280);
dynamicAC->SetPlaceholder("Search programming topics...");

dynamicAC->onRequestSuggestions =
    [](const std::string& query) -> std::vector<AutoCompleteItem> {
        static const std::vector<std::pair<std::string, std::string>> allTopics = {
            {"C++", "cpp"}, {"C#", "csharp"}, {"C", "c"},
            {"Java", "java"}, {"JavaScript", "javascript"},
            {"Python", "python"}, {"PHP", "php"},
            {"Ruby", "ruby"}, {"Rust", "rust"},
            {"Go", "go"}, {"Swift", "swift"}, {"Kotlin", "kotlin"},
            {"TypeScript", "typescript"},
            {"Vue.js", "vuejs"}, {"Angular", "angular"},
            {"Node.js", "nodejs"}, {"Next.js", "nextjs"},
            {"Docker", "docker"}, {"Kubernetes", "kubernetes"},
        };

        if (query.empty()) return {};

        std::vector<AutoCompleteItem> results;
        std::string lowerQuery;
        for (char c : query)
            lowerQuery += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        for (const auto& [text, value] : allTopics) {
            std::string lowerText;
            for (char c : text)
                lowerText += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lowerText.find(lowerQuery) != std::string::npos) {
                results.emplace_back(text, value);
            }
        }
        return results;
    };

dynamicAC->onItemSelected = [](int index, const AutoCompleteItem& item) {
    std::cerr << "Selected topic: " << item.text << std::endl;
};
```

### Large List with Automatic Scrollbar

When the number of filtered items exceeds `AutoCompleteStyle::maxVisibleItems`, a scrollbar appears automatically.

```cpp
auto statesAC = CreateAutoComplete("StatesAC", 30, 400, 280);
statesAC->SetPlaceholder("Search US states...");

statesAC->AddItem("Alabama",       "AL");
statesAC->AddItem("Alaska",        "AK");
statesAC->AddItem("Arizona",       "AZ");
// ... 47 more states ...
statesAC->AddItem("Wyoming",       "WY");

statesAC->onItemSelected = [](int index, const AutoCompleteItem& item) {
    std::cerr << "State: " << item.text << " (" << item.value << ")" << std::endl;
};
```

### Listening to Popup and Text Events

```cpp
auto interactiveAC = CreateAutoComplete("InteractiveAC", 30, 500, 250);
interactiveAC->SetPlaceholder("Type anything...");
interactiveAC->AddItem("Alpha",   "a");
interactiveAC->AddItem("Beta",    "b");
interactiveAC->AddItem("Gamma",   "g");
interactiveAC->AddItem("Delta",   "d");
interactiveAC->AddItem("Epsilon", "e");

interactiveAC->onTextChanged = [](const std::string& text) {
    std::cerr << "Text now: \"" << text << "\"" << std::endl;
};

interactiveAC->onItemSelected = [](int index, const AutoCompleteItem& item) {
    std::cerr << "Selected " << item.text
              << " at index " << index << std::endl;
};

interactiveAC->onAutocompletePopupOpened = []() {
    std::cerr << "Popup opened" << std::endl;
};

interactiveAC->onAutocompletePopupClosed = []() {
    std::cerr << "Popup closed" << std::endl;
};
```

### Custom Popup Style

```cpp
AutoCompleteStyle style;
style.listBackgroundColor = Color(252, 252, 252);
style.listBorderColor     = Color(150, 150, 200);
style.itemHoverColor      = Color(220, 235, 255);
style.itemTextColor       = Colors::Black;
style.itemHeight          = 28.0f;
style.maxVisibleItems     = 10;
style.maxPopupWidth       = 350;
style.fontSize            = 12.0f;

myAC->SetAutocompleteStyle(style);
```

## Behavior Notes

- `SetMinCharsToTrigger(0)` causes the popup to appear as soon as the field receives focus.
- When both static items and `onRequestSuggestions` are present, the dynamic callback takes precedence when supplied.
- `SetCloseOnSelect(false)` keeps the popup open after a selection so the user can refine the query.
- `SetAutoSelectFirst(true)` highlights the first matching item, so pressing Enter accepts it immediately.

## See Also

- [UltraCanvasTextInput](UltraCanvasTextInput.md) - Base class for text entry
- [UltraCanvasListView](UltraCanvasListView.md) - Used internally for the suggestion popup
- [UltraCanvasComboBox](UltraCanvasComboBox.md) - Dropdown selector with fixed list
- [UltraCanvasLabel](UltraCanvasLabel.md) - Companion text display element
