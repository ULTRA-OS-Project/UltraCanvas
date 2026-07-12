# UltraCanvas Layout System Documentation

## Overview

UltraCanvas provides three complementary layout managers for arranging child UI elements inside a container:

- **Box Layout** — Linear horizontal or vertical arrangement (like `QBoxLayout`)
- **Grid Layout** — Row/column grid with cell spanning (like `QGridLayout`)
- **Flex Layout** — CSS Flexbox-style layout with wrap, justify, and align

Every layout manager attaches to an `UltraCanvasContainer*`, accepts shared pointers to UI elements via `AddUIElement(...)`, and recalculates child positions whenever the container is invalidated.

**Version:** 1.0.0
**Headers:**
- `include/UltraCanvasBoxLayout.h`
- `include/UltraCanvasGridLayout.h`
- `include/UltraCanvasFlexLayout.h`

**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasLayout`

## Header Includes

```cpp
#include "UltraCanvasBoxLayout.h"
#include "UltraCanvasGridLayout.h"
#include "UltraCanvasFlexLayout.h"
```

## Shared Concepts

All three layouts share a common item base (`UltraCanvasLayoutItem`) and a small vocabulary:

- `SizeMode` — `Auto` (size from preferred), `Fixed` (explicit pixels), `Fill`, etc.
- `LayoutAlignment` — `Start`, `Center`, `End`, `Fill`, `Auto`
- All layouts expose `SetSpacing(int)` and inherit padding handling from the container.
- Item helpers return `this` so chained configuration is straightforward.

---

## 1. Box Layout (`UltraCanvasBoxLayout`)

A one-dimensional layout that arranges children left-to-right or top-to-bottom.

### Header

```cpp
#include "UltraCanvasBoxLayout.h"
```

### Construction

```cpp
explicit UltraCanvasBoxLayout(UltraCanvasContainer* parent,
                              BoxLayoutDirection dir = BoxLayoutDirection::Vertical);

// Convenience factories
UltraCanvasBoxLayout* CreateHBoxLayout(UltraCanvasContainer* parent = nullptr);
UltraCanvasBoxLayout* CreateVBoxLayout(UltraCanvasContainer* parent = nullptr);
```

### BoxLayoutDirection

```cpp
enum class BoxLayoutDirection {
    Horizontal = 0,    // Left to right
    Vertical   = 1     // Top to bottom
};
```

### Key Methods

```cpp
void SetDirection(BoxLayoutDirection dir);
BoxLayoutDirection GetDirection() const;

void SetDefaultMainAxisAlignment(LayoutAlignment align);
void SetDefaultCrossAxisAlignment(LayoutAlignment align);

// Items
UltraCanvasBoxLayoutItem* AddUIElement(std::shared_ptr<UltraCanvasUIElement> element,
                                       float stretch = 0);
UltraCanvasLayoutItem*    InsertUIElement(std::shared_ptr<UltraCanvasUIElement> element,
                                          int index);
void RemoveUIElement(std::shared_ptr<UltraCanvasUIElement> element);
void ClearItems();
int  GetItemCount() const;

UltraCanvasBoxLayoutItem* GetItemAt(int index) const;
UltraCanvasBoxLayoutItem* GetItemForUIElement(std::shared_ptr<UltraCanvasUIElement> elem) const;

// Non-element fillers
void AddSpacing(int size);           // Fixed gap
void AddStretch(int stretch = 1);    // Flexible space

// Geometry queries
Size2Di CalculateMinimumSize() const   override;
Size2Di CalculatePreferredSize() const override;
Size2Di CalculateMaximumSize() const   override;
```

### Box Item Methods

```cpp
SetWidthMode / SetHeightMode / SetSizeMode(SizeMode)
SetFixedWidth / SetFixedHeight / SetFixedSize(int, int)
SetMinimumWidth / SetMinimumHeight / SetMinimumSize(int, int)
SetMaximumWidth / SetMaximumHeight / SetMaximumSize(int, int)
SetStretch(float)
SetCrossAlignment(LayoutAlignment)
SetMainAlignment(LayoutAlignment)
```

### Vertical Box Example

```cpp
auto vboxDemo = std::make_shared<UltraCanvasContainer>("VBoxDemo", 20, 200, 300, 200);
vboxDemo->SetBackgroundColor(Color(245, 245, 250, 255));
vboxDemo->SetPadding(15);

auto vboxLayout = CreateVBoxLayout(vboxDemo.get());
vboxLayout->SetSpacing(10);

auto vboxBtn1 = std::make_shared<UltraCanvasButton>("VBtn1", 0, 0, 150, 35);
vboxBtn1->SetText("Button 1");
auto vboxBtn2 = std::make_shared<UltraCanvasButton>("VBtn2", 0, 0, 150, 35);
vboxBtn2->SetText("Button 2");
auto vboxBtn3 = std::make_shared<UltraCanvasButton>("VBtn3", 0, 0, 150, 35);
vboxBtn3->SetText("Button 3");

auto vboxStatus = std::make_shared<UltraCanvasLabel>("VStatus", 0, 0, 150, 25);
vboxStatus->SetText("Click any button");

vboxBtn1->SetOnClick([vboxStatus]() { vboxStatus->SetText("Button 1 clicked!"); });
vboxBtn2->SetOnClick([vboxStatus]() { vboxStatus->SetText("Button 2 clicked!"); });
vboxBtn3->SetOnClick([vboxStatus]() { vboxStatus->SetText("Button 3 clicked!"); });

vboxLayout->AddUIElement(vboxBtn1, 0)->SetCrossAlignment(LayoutAlignment::Center);
vboxLayout->AddUIElement(vboxBtn2, 0)->SetCrossAlignment(LayoutAlignment::Center);
vboxLayout->AddUIElement(vboxBtn3, 0)->SetCrossAlignment(LayoutAlignment::Center);
vboxLayout->AddStretch(1);                                 // Pushes status to the bottom
vboxLayout->AddUIElement(vboxStatus, 0)->SetCrossAlignment(LayoutAlignment::Center);
```

### Horizontal Toolbar Example

`AddSpacing` inserts a fixed gap and `AddStretch` consumes all remaining space, so items added after it sit against the right edge.

```cpp
auto hboxDemo = std::make_shared<UltraCanvasContainer>("HBoxDemo", 20, 500, 960, 50);
hboxDemo->SetBackgroundColor(Color(245, 245, 250, 255));
hboxDemo->SetPadding(10);

auto hboxLayout = CreateHBoxLayout(hboxDemo.get());
hboxLayout->SetSpacing(5);

auto newBtn      = std::make_shared<UltraCanvasButton>("NewBtn",      0, 0, 60, 30);  newBtn->SetText("New");
auto openBtn     = std::make_shared<UltraCanvasButton>("OpenBtn",     0, 0, 65, 30);  openBtn->SetText("Open");
auto saveBtn     = std::make_shared<UltraCanvasButton>("SaveBtn",     0, 0, 60, 30);  saveBtn->SetText("Save");
auto settingsBtn = std::make_shared<UltraCanvasButton>("SettingsBtn", 0, 0, 80, 30);  settingsBtn->SetText("Settings");
auto helpBtn     = std::make_shared<UltraCanvasButton>("HelpBtn",     0, 0, 60, 30);  helpBtn->SetText("Help");

hboxLayout->AddUIElement(newBtn,      0)->SetCrossAlignment(LayoutAlignment::Center);
hboxLayout->AddUIElement(openBtn,     0)->SetCrossAlignment(LayoutAlignment::Center);
hboxLayout->AddUIElement(saveBtn,     0)->SetCrossAlignment(LayoutAlignment::Center);
hboxLayout->AddSpacing(15);                    // Visual separator
hboxLayout->AddStretch(1);                     // Push the next items to the right
hboxLayout->AddUIElement(settingsBtn, 0)->SetCrossAlignment(LayoutAlignment::Center);
hboxLayout->AddUIElement(helpBtn,     0)->SetCrossAlignment(LayoutAlignment::Center);
```

---

## 2. Grid Layout (`UltraCanvasGridLayout`)

A two-dimensional layout with explicit row and column definitions. Items live in a (row, column) cell, optionally spanning multiple rows or columns.

### Header

```cpp
#include "UltraCanvasGridLayout.h"
```

### Construction

```cpp
explicit UltraCanvasGridLayout(UltraCanvasContainer* parent,
                               int rows = 1, int columns = 1);

UltraCanvasGridLayout* CreateGridLayout(UltraCanvasContainer* parent = nullptr,
                                        int rows = 0, int columns = 0);
```

### GridSizeMode

```cpp
enum class GridSizeMode {
    Fixed   = 0,   // Pixels
    Auto    = 1,   // Size to content
    Percent = 2,   // Percentage of container
    Star    = 3    // Proportional weight in the remaining space
};
```

### GridRowColumnDefinition

```cpp
struct GridRowColumnDefinition {
    GridSizeMode sizeMode = GridSizeMode::Auto;
    int size    = 0;        // Pixels / percent / weight
    int minSize = 0;
    int maxSize = 10000;

    static GridRowColumnDefinition Fixed(int pixels);
    static GridRowColumnDefinition Auto();
    static GridRowColumnDefinition Percent(int percent);
    static GridRowColumnDefinition Star(int weight = 1);
};
```

### Key Methods

```cpp
// Row / column definitions
void AddRowDefinition(const GridRowColumnDefinition& def);
void AddColumnDefinition(const GridRowColumnDefinition& def);
void SetRowDefinition(int row, const GridRowColumnDefinition& def);
void SetColumnDefinition(int column, const GridRowColumnDefinition& def);
void SetGridSize(int rows, int columns);

int GetRowCount()    const;
int GetColumnCount() const;

// Default alignments
void SetDefaultHorizontalAlignment(LayoutAlignment align);
void SetDefaultVerticalAlignment(LayoutAlignment align);

// Items
UltraCanvasGridLayoutItem* AddUIElement(std::shared_ptr<UltraCanvasUIElement> element,
                                        int row, int column,
                                        int rowSpan = 1, int columnSpan = 1);

UltraCanvasGridLayoutItem* GetItemAt(int index) const;
UltraCanvasGridLayoutItem* GetItemAt(int row, int column) const;
UltraCanvasGridLayoutItem* GetItemForUIElement(std::shared_ptr<UltraCanvasUIElement> element) const;
```

### Grid Item Methods

```cpp
SetRow / SetColumn / SetPosition(int, int)
SetRowSpan / SetColumnSpan / SetSpan(int, int)
SetWidthMode / SetHeightMode / SetSizeMode(SizeMode)
SetFixedWidth / SetFixedHeight / SetFixedSize(int, int)
SetMinimumSize / SetMaximumSize(int, int)
SetHorizontalAlignment(LayoutAlignment)
SetVerticalAlignment(LayoutAlignment)
SetAlignment(LayoutAlignment horizontal, LayoutAlignment vertical)
```

### Form Layout Example

A typical "labels in the first column, inputs filling the second column" form. The submit button spans both columns.

```cpp
auto gridDemo = std::make_shared<UltraCanvasContainer>("GridDemo", 20, 700, 450, 200);
gridDemo->SetBackgroundColor(Color(245, 245, 250, 255));
gridDemo->SetPadding(10);

auto gridLayout = CreateGridLayout(gridDemo.get(), 4, 2);
gridLayout->SetSpacing(10);
gridLayout->SetColumnDefinition(0, GridRowColumnDefinition::Auto());      // Labels: auto
gridLayout->SetColumnDefinition(1, GridRowColumnDefinition::Star(1));     // Inputs: fill

int row = 0;

auto nameLabel = std::make_shared<UltraCanvasLabel>("NameLbl", 0, 0, 70, 25);
nameLabel->SetText("Name:");
auto nameInput = std::make_shared<UltraCanvasTextInput>("NameIn", 0, 0, 250, 25);
gridLayout->AddUIElement(nameLabel, row,   0);
gridLayout->AddUIElement(nameInput, row++, 1);

auto emailLabel = std::make_shared<UltraCanvasLabel>("EmailLbl", 0, 0, 70, 25);
emailLabel->SetText("Email:");
auto emailInput = std::make_shared<UltraCanvasTextInput>("EmailIn", 0, 0, 250, 25);
gridLayout->AddUIElement(emailLabel, row,   0);
gridLayout->AddUIElement(emailInput, row++, 1);

auto phoneLabel = std::make_shared<UltraCanvasLabel>("PhoneLbl", 0, 0, 70, 25);
phoneLabel->SetText("Phone:");
auto phoneInput = std::make_shared<UltraCanvasTextInput>("PhoneIn", 0, 0, 250, 25);
gridLayout->AddUIElement(phoneLabel, row,   0);
gridLayout->AddUIElement(phoneInput, row++, 1);

auto submitBtn = std::make_shared<UltraCanvasButton>("SubmitBtn", 0, 0, 150, 30);
submitBtn->SetText("Submit");
gridLayout->AddUIElement(submitBtn, row, 0, 1, 2);    // rowSpan=1, columnSpan=2
```

---

## 3. Flex Layout (`UltraCanvasFlexLayout`)

A CSS-flexbox-style layout that supports wrapping, justification along the main axis, alignment along the cross axis, and per-item grow/shrink/basis.

### Header

```cpp
#include "UltraCanvasFlexLayout.h"
```

### Construction

```cpp
explicit UltraCanvasFlexLayout(UltraCanvasContainer* parent,
                               FlexDirection dir = FlexDirection::Row);

UltraCanvasFlexLayout* CreateFlexLayout(UltraCanvasContainer* parent = nullptr,
                                        FlexDirection direction = FlexDirection::Row);
```

### Enumerations

```cpp
enum class FlexDirection   { Row, RowReverse, Column, ColumnReverse };
enum class FlexWrap        { NoWrap, Wrap, WrapReverse };
enum class FlexJustifyContent { Start, End, Center, SpaceBetween, SpaceAround, SpaceEvenly };
enum class FlexAlignItems     { Start, End, Center, Stretch, Baseline };
enum class FlexAlignContent   { Start, End, Center, Stretch, SpaceBetween, SpaceAround };
```

### Key Methods

```cpp
void SetFlexDirection(FlexDirection dir);
void SetFlexWrap(FlexWrap w);
void SetJustifyContent(FlexJustifyContent justify);
void SetAlignItems(FlexAlignItems align);
void SetAlignContent(FlexAlignContent align);

// Gaps (CSS-like)
void SetGap(int gap);
void SetGap(int row, int column);
void SetRowGap(int gap);
void SetColumnGap(int gap);

// Items
UltraCanvasFlexLayoutItem* AddUIElement(std::shared_ptr<UltraCanvasUIElement> element,
                                        float flexGrow   = 0,
                                        float flexShrink = 0,
                                        float flexBasis  = 0);

UltraCanvasFlexLayoutItem* GetItemAt(int index) const;
UltraCanvasFlexLayoutItem* GetItemForUIElement(std::shared_ptr<UltraCanvasUIElement> elem) const;
```

### Flex Item Methods

```cpp
SetFlexGrow(float)
SetFlexShrink(float)
SetFlexBasis(int)
SetFlex(float grow, float shrink, int basis)
SetWidthMode / SetHeightMode / SetSizeMode(SizeMode)
SetFixedWidth / SetFixedHeight / SetFixedSize(int, int)
SetMinimumSize / SetMaximumSize(int, int)
SetAlignSelf(LayoutAlignment)
```

### Responsive Card Grid Example

This example builds four cards that wrap to multiple lines as the container shrinks. Each card is itself a vertical box layout containing a title, body, stretch, and an action button.

```cpp
auto flexDemo = std::make_shared<UltraCanvasContainer>("FlexDemo", 20, 1000, 960, 260);
flexDemo->SetBackgroundColor(Color(245, 245, 250, 255));
flexDemo->SetPadding(15);

auto flexLayout = CreateFlexLayout(flexDemo.get(), FlexDirection::Row);
flexLayout->SetFlexWrap(FlexWrap::Wrap);
flexLayout->SetJustifyContent(FlexJustifyContent::SpaceAround);
flexLayout->SetAlignItems(FlexAlignItems::Start);
flexLayout->SetGap(15, 15);

const char* cardTitles[] = {"Card 1", "Card 2", "Card 3", "Card 4"};
const char* cardTexts[]  = {
    "Flexible layout",
    "Wraps automatically",
    "Responsive design",
    "Modern pattern"
};

for (int i = 0; i < 4; i++) {
    auto card = std::make_shared<UltraCanvasContainer>(
        std::string("Card") + std::to_string(i), 0, 0, 220, 110);
    card->SetBackgroundColor(Color(255, 255, 255, 255));
    card->SetPadding(15);

    auto cardLayout = CreateVBoxLayout(card.get());
    cardLayout->SetSpacing(8);

    auto cardTitle = std::make_shared<UltraCanvasLabel>(
        std::string("CardTitle") + std::to_string(i), 0, 0, 190, 20);
    cardTitle->SetText(cardTitles[i]);
    cardTitle->SetFontSize(14);
    cardTitle->SetFontWeight(FontWeight::Bold);

    auto cardText = std::make_shared<UltraCanvasLabel>(
        std::string("CardText") + std::to_string(i), 0, 0, 190, 35);
    cardText->SetText(cardTexts[i]);
    cardText->SetTextColor(Color(80, 80, 80, 255));
    cardText->SetFontSize(11);

    auto cardBtn = std::make_shared<UltraCanvasButton>(
        std::string("CardBtn") + std::to_string(i), 0, 0, 80, 25);
    cardBtn->SetText("Action");

    cardLayout->AddUIElement(cardTitle, 0);
    cardLayout->AddUIElement(cardText,  0);
    cardLayout->AddStretch(1);
    cardLayout->AddUIElement(cardBtn,   0);

    // flexGrow=0, flexShrink=1, flexBasis=220
    flexLayout->AddUIElement(card, 0, 1, 220);
}
```

---

## Choosing a Layout

| Layout | Best for | Notable features |
|--------|---------|------------------|
| **VBox / HBox** | Toolbars, button rows, sidebars, simple stacks | `AddStretch`, `AddSpacing`, per-item cross alignment |
| **Grid** | Forms, tables, dashboard tiles | Per-row/column `Auto`/`Fixed`/`Percent`/`Star` sizing, cell spans |
| **Flex** | Responsive card grids, adaptive toolbars, wrap-as-needed UIs | Wrap, `justifyContent`, `alignItems`, gaps, grow/shrink/basis |

All three layouts share:

- Spacing between items (`SetSpacing`)
- Container padding (set on the parent `UltraCanvasContainer`)
- Per-item size constraints (`SetMinimumSize`, `SetMaximumSize`, `SetFixedSize`)
- Alignment helpers

## Common Patterns

### Pushing items to the far end (Box)

```cpp
hbox->AddUIElement(leftBtn, 0);
hbox->AddStretch(1);         // Eats remaining space
hbox->AddUIElement(rightBtn, 0);
```

### Auto-labels + Stretching inputs (Grid)

```cpp
grid->SetColumnDefinition(0, GridRowColumnDefinition::Auto());
grid->SetColumnDefinition(1, GridRowColumnDefinition::Star(1));
```

### Wrapping responsive cards (Flex)

```cpp
flex->SetFlexWrap(FlexWrap::Wrap);
flex->SetJustifyContent(FlexJustifyContent::SpaceAround);
flex->SetGap(15, 15);
flex->AddUIElement(card, /*grow*/ 0, /*shrink*/ 1, /*basis*/ 220);
```

## See Also

- [UltraCanvasContainer](UltraCanvasContainer.md) - The parent container that hosts a layout
- [UltraCanvasButton](UltraCanvasButton.md) - Commonly arranged in box/grid layouts
- [UltraCanvasLabel](UltraCanvasLabel.md) - Form labels, status text
- [UltraCanvasTextInput](UltraCanvasTextInput.md) - Form inputs typically placed in grid cells
