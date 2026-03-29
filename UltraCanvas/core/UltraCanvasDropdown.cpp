// core/UltraCanvasDropdown.cpp
// Interactive dropdown/combobox component with icon support and multi-selection
// Uses ListView popup for rendering dropdown items
#include "UltraCanvasDropdown.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasImage.h"

namespace UltraCanvas {

    // ===== DROPDOWN LIST MODEL =====

    int DropdownListModel::GetRowCount() const {
        return items ? static_cast<int>(items->size()) : 0;
    }

    ListDataValue DropdownListModel::GetData(const ListIndex& index, ListDataRole role) const {
        if (!items || index.row < 0 || index.row >= static_cast<int>(items->size()))
            return std::monostate{};

        const auto& item = (*items)[index.row];

        if (role == ListDataRole::DisplayRole)    return item.text;
        if (role == ListDataRole::DecorationRole) return item.iconPath;
        if (role == DropdownValueRole)            return item.value;
        if (role == DropdownEnabledRole)          return item.enabled ? 1 : 0;
        if (role == DropdownSeparatorRole)        return item.separator ? 1 : 0;
        if (role == DropdownSelectedRole)         return item.selected ? 1 : 0;

        return std::monostate{};
    }

    // ===== DROPDOWN ITEM DELEGATE =====

    void DropdownItemDelegate::RenderItem(IRenderContext* ctx, const IListModel* model,
                                          int row, int column,
                                          const ListItemStyleOption& option) {
        if (!style || !model) return;

        // Query item properties from model
        auto sepVal = model->GetData({row, 0}, DropdownSeparatorRole);
        bool isSeparator = (std::get_if<int>(&sepVal) && std::get<int>(sepVal) != 0);

        auto enabledVal = model->GetData({row, 0}, DropdownEnabledRole);
        bool isEnabled = !(std::get_if<int>(&enabledVal) && std::get<int>(enabledVal) == 0);

        // For separator and disabled items, paint over hover/selection background
        if (isSeparator || !isEnabled) {
            if (option.isHovered || option.isSelected) {
                ctx->SetFillPaint(style->listBackgroundColor);
                ctx->FillRectangle(option.rect.x, option.rect.y,
                                   option.rect.width, option.rect.height);
            }
        }

        if (isSeparator) {
            // Draw separator line centered vertically
            int sepY = option.rect.y + option.rect.height / 2;
            ctx->SetStrokePaint(style->listBorderColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(Point2Di(option.rect.x + 4, sepY),
                         Point2Di(option.rect.x + option.rect.width - 4, sepY));
            return;
        }

        // Determine text color
        Color textColor = isEnabled ? style->normalTextColor : style->disabledTextColor;

        ctx->SetFontFace(style->fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(style->fontSize);
        ctx->SetTextPaint(textColor);

        int currentX = option.rect.x + 4;

        // Render checkbox for multi-select
        if (multiSelectEnabled) {
            auto selVal = model->GetData({row, 0}, DropdownSelectedRole);
            bool isChecked = (std::get_if<int>(&selVal) && std::get<int>(selVal) != 0);

            Rect2Di checkboxRect(
                currentX,
                option.rect.y + (option.rect.height - static_cast<int>(style->checkboxSize)) / 2,
                static_cast<int>(style->checkboxSize),
                static_cast<int>(style->checkboxSize)
            );
            RenderCheckbox(ctx, isChecked, checkboxRect);
            currentX += static_cast<int>(style->checkboxSize + style->checkboxPadding);
        }

        // Render icon if present
        std::string iconPath = GetStringValue(model->GetData({row, 0}, ListDataRole::DecorationRole));
        if (!iconPath.empty()) {
            Rect2Di iconRect(
                currentX,
                option.rect.y + (option.rect.height - static_cast<int>(style->iconSize)) / 2,
                static_cast<int>(style->iconSize),
                static_cast<int>(style->iconSize)
            );
            RenderIcon(ctx, iconPath, iconRect);
            currentX += static_cast<int>(style->iconSize + style->iconPadding);
        }

        // Render text
        std::string text = GetStringValue(model->GetData({row, 0}, ListDataRole::DisplayRole));
        if (!text.empty()) {
            int textWidth, textHeight;
            ctx->GetTextLineDimensions(text, textWidth, textHeight);
            int textY = option.rect.y + (option.rect.height - textHeight) / 2;
            ctx->DrawText(text, Point2Di(currentX, textY));
        }
    }

    int DropdownItemDelegate::GetRowHeight(const IListModel* model, int row) const {
        return style ? static_cast<int>(style->itemHeight) : 24;
    }

    void DropdownItemDelegate::RenderCheckbox(IRenderContext* ctx, bool checked, const Rect2Di& checkboxRect) const {
        if (!style) return;

        // Checkbox border
        ctx->DrawFilledRectangle(checkboxRect, Colors::White, 1.0f, style->checkboxBorderColor);

        if (checked) {
            // Checked background
            Rect2Di innerRect(
                checkboxRect.x + 2, checkboxRect.y + 2,
                checkboxRect.width - 4, checkboxRect.height - 4
            );
            ctx->DrawFilledRectangle(innerRect, style->checkboxCheckedColor, 0.0f, Colors::Transparent);

            // Checkmark
            ctx->SetStrokePaint(style->checkmarkColor);
            ctx->SetStrokeWidth(2.0f);

            int cx = checkboxRect.x + checkboxRect.width / 2;
            int cy = checkboxRect.y + checkboxRect.height / 2;

            ctx->DrawLine(
                Point2Di(checkboxRect.x + 3, cy),
                Point2Di(cx - 1, checkboxRect.y + checkboxRect.height - 4)
            );
            ctx->DrawLine(
                Point2Di(cx - 1, checkboxRect.y + checkboxRect.height - 4),
                Point2Di(checkboxRect.x + checkboxRect.width - 3, checkboxRect.y + 3)
            );
        }
    }

    void DropdownItemDelegate::RenderIcon(IRenderContext* ctx, const std::string& iconPath, const Rect2Di& iconRect) const {
        if (iconPath.empty()) return;

        auto icon = UCImage::Get(iconPath);
        if (icon && icon->IsValid()) {
            ctx->DrawImage(*icon.get(), iconRect, ImageFitMode::Contain);
        }
    }

    // ===== DROPDOWN CONSTRUCTOR =====

    UltraCanvasDropdown::UltraCanvasDropdown(const std::string &identifier, long id, long x, long y,
                                             long w, long h)
            : UltraCanvasUIElement(identifier, id, x, y, w, h) {
        style.scrollbarStyle = ScrollbarStyle::DropDown();
        CreatePopupListView();
        WireListViewCallbacks();
    }

    // ===== ITEM MANAGEMENT =====

    void UltraCanvasDropdown::AddItem(const std::string &text) {
        items.emplace_back(text);
        dropdownModel.DataChanged();
    }

    void UltraCanvasDropdown::AddItem(const std::string &text, const std::string &value) {
        items.emplace_back(text, value);
        dropdownModel.DataChanged();
    }

    void UltraCanvasDropdown::AddItem(const std::string &text, const std::string &value, const std::string &iconPath) {
        items.emplace_back(text, value, iconPath);
        dropdownModel.DataChanged();
    }

    void UltraCanvasDropdown::AddItem(const DropdownItem &item) {
        items.push_back(item);
        dropdownModel.DataChanged();
    }

    void UltraCanvasDropdown::AddSeparator() {
        DropdownItem separator;
        separator.separator = true;
        separator.enabled = false;
        items.push_back(separator);
        dropdownModel.DataChanged();
    }

    void UltraCanvasDropdown::ClearItems() {
        items.clear();
        selectedIndex = -1;
        selectedIndices.clear();
        dropdownModel.DataChanged();
    }

    void UltraCanvasDropdown::RemoveItem(int index) {
        if (index >= 0 && index < (int)items.size()) {
            items.erase(items.begin() + index);

            // Update single selection
            if (selectedIndex == index) {
                selectedIndex = -1;
            } else if (selectedIndex > index) {
                selectedIndex--;
            }

            // Update multi-selection indices
            if (multiSelectEnabled) {
                std::set<int> newSelectedIndices;
                for (int idx : selectedIndices) {
                    if (idx < index) {
                        newSelectedIndices.insert(idx);
                    } else if (idx > index) {
                        newSelectedIndices.insert(idx - 1);
                    }
                }
                selectedIndices = newSelectedIndices;
            }

            dropdownModel.DataChanged();
        }
    }

    // ===== SELECTION MANAGEMENT =====

    void UltraCanvasDropdown::SetSelectedIndex(int index, bool runNotifications) {
        if (multiSelectEnabled) {
            return;
        }

        if (index >= -1 && index < (int)items.size()) {
            if (selectedIndex != index) {
                selectedIndex = index;

                if (index >= 0) {
                    if (runNotifications && onSelectionChanged) {
                        onSelectionChanged(index, items[index]);
                    }
                    UCEvent ev;
                    ev.type = UCEventType::DropdownSelect;
                    ev.targetElement = this;
                    ev.userDataInt = index;
                    UltraCanvasApplication::GetInstance()->PushEvent(ev);
                }
            }
        }
    }

    const DropdownItem* UltraCanvasDropdown::GetSelectedItem() const {
        if (selectedIndex >= 0 && selectedIndex < (int)items.size()) {
            return &items[selectedIndex];
        }
        return nullptr;
    }

    const DropdownItem* UltraCanvasDropdown::GetItem(int index) const {
        if (index >= 0 && index < (int)items.size()) {
            return &items[index];
        }
        return nullptr;
    }

    // ===== MULTI-SELECTION =====

    void UltraCanvasDropdown::SetMultiSelectEnabled(bool enabled) {
        if (multiSelectEnabled != enabled) {
            multiSelectEnabled = enabled;

            if (enabled) {
                selectedIndices.clear();
                if (selectedIndex >= 0) {
                    selectedIndices.insert(selectedIndex);
                    items[selectedIndex].selected = true;
                }
            } else {
                if (!selectedIndices.empty()) {
                    selectedIndex = *selectedIndices.begin();
                } else {
                    selectedIndex = -1;
                }
                for (auto& item : items) {
                    item.selected = false;
                }
                selectedIndices.clear();
            }

            // Update delegate and selection model
            dropdownDelegate->SetMultiSelectEnabled(enabled);
            dropdownModel.DataChanged();
            RequestRedraw();
        }
    }

    void UltraCanvasDropdown::SetItemSelected(int index, bool selected) {
        if (!multiSelectEnabled || index < 0 || index >= (int)items.size()) {
            return;
        }

        if (items[index].separator || !items[index].enabled) {
            return;
        }

        bool changed = false;

        if (selected) {
            if (selectedIndices.insert(index).second) {
                items[index].selected = true;
                changed = true;
            }
        } else {
            if (selectedIndices.erase(index) > 0) {
                items[index].selected = false;
                changed = true;
            }
        }

        if (changed) {
            dropdownModel.DataChanged();

            if (onMultiSelectionChanged) {
                std::vector<int> indices(selectedIndices.begin(), selectedIndices.end());
                onMultiSelectionChanged(indices);
            }

            if (onSelectedItemsChanged) {
                onSelectedItemsChanged(GetSelectedItems());
            }

            RequestRedraw();
        }
    }

    bool UltraCanvasDropdown::IsItemSelected(int index) const {
        if (!multiSelectEnabled) {
            return index == selectedIndex;
        }
        return selectedIndices.find(index) != selectedIndices.end();
    }

    void UltraCanvasDropdown::SelectAll() {
        if (!multiSelectEnabled) return;

        selectedIndices.clear();
        for (int i = 0; i < (int)items.size(); ++i) {
            if (!items[i].separator && items[i].enabled) {
                selectedIndices.insert(i);
                items[i].selected = true;
            }
        }

        dropdownModel.DataChanged();

        if (onMultiSelectionChanged) {
            std::vector<int> indices(selectedIndices.begin(), selectedIndices.end());
            onMultiSelectionChanged(indices);
        }
        if (onSelectedItemsChanged) {
            onSelectedItemsChanged(GetSelectedItems());
        }
        RequestRedraw();
    }

    void UltraCanvasDropdown::DeselectAll() {
        if (!multiSelectEnabled) return;

        selectedIndices.clear();
        for (auto& item : items) {
            item.selected = false;
        }

        dropdownModel.DataChanged();

        if (onMultiSelectionChanged) {
            std::vector<int> empty;
            onMultiSelectionChanged(empty);
        }
        if (onSelectedItemsChanged) {
            std::vector<DropdownItem> empty;
            onSelectedItemsChanged(empty);
        }
        RequestRedraw();
    }

    std::vector<int> UltraCanvasDropdown::GetSelectedIndices() const {
        if (!multiSelectEnabled) {
            if (selectedIndex >= 0) return {selectedIndex};
            return {};
        }
        return std::vector<int>(selectedIndices.begin(), selectedIndices.end());
    }

    std::vector<DropdownItem> UltraCanvasDropdown::GetSelectedItems() const {
        std::vector<DropdownItem> result;
        if (!multiSelectEnabled) {
            if (selectedIndex >= 0 && selectedIndex < (int)items.size()) {
                result.push_back(items[selectedIndex]);
            }
        } else {
            for (int idx : selectedIndices) {
                if (idx >= 0 && idx < (int)items.size()) {
                    result.push_back(items[idx]);
                }
            }
        }
        return result;
    }

    // ===== DROPDOWN OPEN / CLOSE =====

    void UltraCanvasDropdown::OpenDropdown() {
        if (!isPopup && !items.empty() && window) {
            dropdownOpen = true;
            dropdownModel.DataChanged();
            CalculateAndSetPopupSize();

            Point2Di pos = CalculatePopupPosition();

            PopupElementSettings settings;
            settings.popupOwner = shared_from_this();
            settings.closeByEscapeKey = true;
            settings.closeByClickOutside = true;

            window->OpenPopup(pos, *popupListView, settings);

            // Sync current selection to the ListView
            if (!multiSelectEnabled && selectedIndex >= 0) {
                auto* sel = popupListView->GetSelection();
                if (sel) sel->Select(selectedIndex);
                popupListView->EnsureRowVisible(selectedIndex);
            }

            if (onDropdownOpened) onDropdownOpened();
            RequestRedraw();
        }
    }

    void UltraCanvasDropdown::CloseDropdown() {
        if (window && popupListView) {
            window->ClosePopup(*popupListView);
        }
    }

    // ===== STYLING =====

    void UltraCanvasDropdown::SetStyle(const DropdownStyle &newStyle) {
        style = newStyle;
        ApplyStyleToListView();
        RequestRedraw();
    }

    // ===== POPUP SIZING AND POSITIONING =====

    void UltraCanvasDropdown::CalculateAndSetPopupSize() {
        if (items.empty() || !popupListView) return;

        auto ctx = GetRenderContext();
        if (!ctx) return;

        ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(style.fontSize);

        // Calculate maximum width needed
        int maxTextWidth = 0;
        for (const auto& item : items) {
            if (item.separator) continue;

            int textWidth, textHeight;
            ctx->GetTextLineDimensions(item.text, textWidth, textHeight);

            if (!item.iconPath.empty()) {
                textWidth += static_cast<int>(style.iconSize + style.iconPadding * 2);
            }
            if (multiSelectEnabled) {
                textWidth += static_cast<int>(style.checkboxSize + style.checkboxPadding * 2);
            }

            maxTextWidth = std::max(maxTextWidth, textWidth);
        }

        int dropdownWidth = maxTextWidth + static_cast<int>(style.paddingLeft + style.paddingRight);
        dropdownWidth = std::max(dropdownWidth, GetBounds().width);
        dropdownWidth = std::min(dropdownWidth, style.maxItemWidth);

        // Calculate height
        int itemCount = static_cast<int>(items.size());
        int visibleItems = (style.maxVisibleItems == -1)
            ? itemCount
            : std::min(itemCount, style.maxVisibleItems);

        int dropdownHeight = static_cast<int>(visibleItems * style.itemHeight + style.borderWidth * 2);

        popupListView->SetSize(dropdownWidth, dropdownHeight);
    }

    Point2Di UltraCanvasDropdown::CalculatePopupPosition() {
        Point2Di globalPos = GetPositionInWindow();
        Rect2Di buttonRect = GetBounds();

        int windowHeight = window ? window->GetHeight() : 9999;
        int windowWidth = window ? window->GetWidth() : 9999;

        int popupWidth = popupListView->GetBounds().width;
        int popupHeight = popupListView->GetBounds().height;

        // Calculate available space below and above the button
        int spaceBelow = windowHeight - (globalPos.y + buttonRect.height);
        int spaceAbove = globalPos.y;

        int effectiveHeight = popupHeight;
        int listY;

        if (effectiveHeight <= spaceBelow) {
            // Fits below
            listY = globalPos.y + buttonRect.height;
        } else if (effectiveHeight <= spaceAbove) {
            // Fits above
            listY = globalPos.y - effectiveHeight;
        } else if (spaceBelow >= spaceAbove) {
            // More space below — clamp
            effectiveHeight = std::max(spaceBelow, static_cast<int>(style.itemHeight) + 2);
            listY = globalPos.y + buttonRect.height;
        } else {
            // More space above — clamp
            effectiveHeight = std::max(spaceAbove, static_cast<int>(style.itemHeight) + 2);
            listY = globalPos.y - effectiveHeight;
        }

        // Update popup size if clamped
        if (effectiveHeight != popupHeight) {
            popupListView->SetSize(popupWidth, effectiveHeight);
        }

        int listX = globalPos.x;
        // Horizontal adjustment
        if (listX + popupWidth > windowWidth) {
            listX = windowWidth - popupWidth;
        }

        return Point2Di(listX, listY);
    }

    // ===== LISTVIEW SETUP =====

    void UltraCanvasDropdown::CreatePopupListView() {
        popupListView = std::make_shared<UltraCanvasListView>(
            GetIdentifier() + "_popup_lv", 0, 0, 0, 200, 100);

        dropdownModel.SetItems(&items);
        popupListView->SetModel(&dropdownModel);
        popupListView->SetShowHeader(false);

        dropdownDelegate = std::make_shared<DropdownItemDelegate>();
        dropdownDelegate->SetStyle(&style);
        dropdownDelegate->SetMultiSelectEnabled(multiSelectEnabled);
        popupListView->SetDelegate(dropdownDelegate);

        auto sel = std::make_shared<UltraCanvasSingleSelection>();
        popupListView->SetSelection(sel);

        ApplyStyleToListView();
    }

    void UltraCanvasDropdown::WireListViewCallbacks() {
        popupListView->onItemClicked = [this](int row) {
            if (row < 0 || row >= (int)items.size()) return;
            if (items[row].separator || !items[row].enabled) return;

            if (multiSelectEnabled) {
                SetItemSelected(row, !IsItemSelected(row));
            } else {
                SetSelectedIndex(row);
                CloseDropdown();
            }
        };

        popupListView->onItemHovered = [this](int row) {
            if (row >= 0 && row < (int)items.size() && onItemHovered) {
                onItemHovered(row, items[row]);
            }
        };

        popupListView->onPopupClosed = [this](ClosePopupReason /*reason*/) {
            dropdownOpen = false;
            if (onDropdownClosed) onDropdownClosed();
        };
    }

    void UltraCanvasDropdown::ApplyStyleToListView() {
        if (!popupListView) return;

        ListViewStyle lvStyle;
        lvStyle.backgroundColor = style.listBackgroundColor;
        lvStyle.rowHeight = static_cast<int>(style.itemHeight);
        lvStyle.showHeader = false;
        lvStyle.showGridLines = false;
        lvStyle.alternateRowColors = false;
        lvStyle.hoverBackgroundColor = style.itemHoverColor;
        lvStyle.selectionBackgroundColor = style.itemSelectedColor;
        lvStyle.scrollbarStyle = style.scrollbarStyle;

        popupListView->SetStyle(lvStyle);
        popupListView->SetBorders(static_cast<int>(style.borderWidth), style.listBorderColor);
    }

    // ===== GEOMETRY OVERRIDES =====

    void UltraCanvasDropdown::UpdateGeometry(IRenderContext* ctx) {
        UltraCanvasUIElement::UpdateGeometry(ctx);
    }

    void UltraCanvasDropdown::SetWindow(UltraCanvasWindowBase *win) {
        UltraCanvasUIElement::SetWindow(win);
        if (popupListView) {
            popupListView->SetWindow(win);
        }
    }

    // ===== RENDERING =====

    void UltraCanvasDropdown::Render(IRenderContext* ctx) {
        ctx->PushState();
        RenderButton(ctx);
        ctx->PopState();
    }

    std::string UltraCanvasDropdown::GetDisplayText() const {
        if (multiSelectEnabled) {
            int count = static_cast<int>(selectedIndices.size());
            if (count == 0) {
                return "Select items...";
            } else if (count == 1) {
                int idx = *selectedIndices.begin();
                if (idx >= 0 && idx < (int)items.size()) {
                    return items[idx].text;
                }
            } else {
                return std::to_string(count) + " items selected";
            }
        } else {
            if (selectedIndex >= 0 && selectedIndex < (int)items.size()) {
                return items[selectedIndex].text;
            }
        }
        return "";
    }

    void UltraCanvasDropdown::RenderButton(IRenderContext *ctx) {
        Rect2Di buttonRect = GetBounds();

        // Determine button state colors
        Color bgColor = style.normalColor;
        Color textColor = style.normalTextColor;
        Color borderColor = style.borderColor;

        if (IsDisabled()) {
            bgColor = style.disabledColor;
            textColor = style.disabledTextColor;
        } else if (buttonPressed || dropdownOpen) {
            bgColor = style.pressedColor;
        } else if (IsHovered()) {
            bgColor = style.hoverColor;
        }

        if (IsFocused() && !dropdownOpen) {
            borderColor = style.focusBorderColor;
        }

        // Draw button background
        ctx->DrawFilledRectangle(buttonRect, bgColor, style.borderWidth, borderColor);

        // Get display text
        std::string displayText = GetDisplayText();

        if (!displayText.empty()) {
            ctx->PushState();
            int arrowSpace = static_cast<int>(style.arrowSize * 2 + 2);
            ctx->ClipRect(Rect2Di(buttonRect.x + 1, buttonRect.y,
                                  buttonRect.width - arrowSpace - 1, buttonRect.height));

            ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
            ctx->SetFontSize(style.fontSize);
            ctx->SetTextPaint(textColor);

            int textWidth, textHeight;
            ctx->GetTextLineDimensions(displayText, textWidth, textHeight);

            int textX = buttonRect.x + (int)style.paddingLeft;
            int textY = buttonRect.y + (buttonRect.height - textHeight) / 2;

            // Render icon if single selection and item has icon
            if (!multiSelectEnabled && selectedIndex >= 0 && selectedIndex < (int)items.size()) {
                const auto& item = items[selectedIndex];
                if (!item.iconPath.empty()) {
                    Rect2Di iconRect(
                        textX,
                        buttonRect.y + (buttonRect.height - (int)style.iconSize) / 2,
                        (int)style.iconSize,
                        (int)style.iconSize
                    );
                    auto icon = UCImage::Get(item.iconPath);
                    if (icon && icon->IsValid()) {
                        ctx->DrawImage(*icon.get(), iconRect, ImageFitMode::Contain);
                    }
                    textX += (int)(style.iconSize + style.iconPadding);
                }
            }

            ctx->DrawText(displayText, Point2Di(textX, textY));
            ctx->PopState();
        }

        // Draw dropdown arrow
        RenderDropdownArrow(buttonRect, textColor, ctx);

        // Draw focus indicator
        if (IsFocused() && !dropdownOpen) {
            Rect2Di focusRect(buttonRect.x + 1, buttonRect.y + 1,
                              buttonRect.width - 2, buttonRect.height - 2);
            ctx->DrawFilledRectangle(focusRect, Colors::Transparent, 1.0, style.focusBorderColor);
        }
    }

    void UltraCanvasDropdown::RenderDropdownArrow(const Rect2Di &buttonRect, const Color &color, IRenderContext *ctx) {
        float arrowX = buttonRect.x + buttonRect.width - (style.arrowSize + style.arrowSize);
        float arrowY = buttonRect.y + (buttonRect.height - style.arrowSize) / 2 + 2;

        float arrowCenterX = arrowX + style.arrowSize / 2;
        float arrowBottom = arrowY + style.arrowSize / 2;

        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(1.0f);

        ctx->DrawLine(arrowX, arrowY, arrowCenterX, arrowBottom);
        ctx->DrawLine(arrowCenterX, arrowBottom, arrowX + style.arrowSize, arrowY);
    }

    // ===== EVENT HANDLING =====

    bool UltraCanvasDropdown::OnEvent(const UCEvent &event) {
        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);

            case UCEventType::MouseUp:
                return HandleMouseUp(event);

            case UCEventType::MouseMove:
                return HandleMouseMove(event);

            case UCEventType::MouseLeave:
                HandleMouseLeave(event);
                return false;

            case UCEventType::KeyDown:
                if (onKeyDown && onKeyDown(event)) return true;
                return HandleKeyDown(event);

            case UCEventType::FocusLost:
                HandleFocusLost();
                return false;

            default:
                return false;
        }
    }

    bool UltraCanvasDropdown::HandleMouseDown(const UCEvent &event) {
        Rect2Di buttonRect = GetBounds();

        if (buttonRect.Contains(event.x, event.y)) {
            buttonPressed = true;
            if (dropdownOpen) {
                CloseDropdown();
            } else {
                OpenDropdown();
            }
            return true;
        }

        return false;
    }

    bool UltraCanvasDropdown::HandleMouseUp(const UCEvent &event) {
        buttonPressed = false;
        return false;
    }

    bool UltraCanvasDropdown::HandleMouseMove(const UCEvent &event) {
        Rect2Di buttonRect = GetBounds();
        if (buttonRect.Contains(event.x, event.y)) {
            SetMouseCursor(UCMouseCursor::ContextMenu);
        } else {
            SetMouseCursor(UCMouseCursor::Default);
        }
        return false;
    }

    void UltraCanvasDropdown::HandleMouseLeave(const UCEvent &event) {
        // Nothing needed — ListView handles its own hover
    }

    bool UltraCanvasDropdown::HandleKeyDown(const UCEvent &event) {
        if (!dropdownOpen || items.empty()) {
            // Open on Down/Space when closed
            if (event.virtualKey == UCKeys::Down || event.virtualKey == UCKeys::Space) {
                OpenDropdown();
                return true;
            }
            return false;
        }

        // Forward navigation keys to the ListView
        switch (event.virtualKey) {
            case UCKeys::Up:
            case UCKeys::Down:
            case UCKeys::PageUp:
            case UCKeys::PageDown:
            case UCKeys::Home:
            case UCKeys::End:
                return popupListView->OnEvent(event);

            case UCKeys::Return: {
                // Get the focused row from the ListView's selection
                auto* sel = popupListView->GetSelection();
                int focusedRow = sel ? sel->GetCurrentRow() : -1;

                if (focusedRow >= 0 && focusedRow < (int)items.size() &&
                    !items[focusedRow].separator && items[focusedRow].enabled) {
                    if (multiSelectEnabled) {
                        SetItemSelected(focusedRow, !IsItemSelected(focusedRow));
                    } else {
                        SetSelectedIndex(focusedRow);
                        CloseDropdown();
                    }
                }
                return true;
            }

            case UCKeys::Space:
                if (multiSelectEnabled) {
                    auto* sel = popupListView->GetSelection();
                    int focusedRow = sel ? sel->GetCurrentRow() : -1;
                    if (focusedRow >= 0 && focusedRow < (int)items.size() &&
                        !items[focusedRow].separator && items[focusedRow].enabled) {
                        SetItemSelected(focusedRow, !IsItemSelected(focusedRow));
                    }
                    return true;
                }
                return false;

            case UCKeys::Escape:
                CloseDropdown();
                return true;

            default:
                return false;
        }
    }

    void UltraCanvasDropdown::HandleFocusLost() {
        if (dropdownOpen && !multiSelectEnabled) {
            CloseDropdown();
        }
    }

} // namespace UltraCanvas
