// core/UltraCanvasToolRows.cpp
// The rows of a tool panel: icon buttons and toggles, labelled values, lists
// (UltraCanvasToolRows.h).
// Version: 1.1.0 - a caption longer than kCaptionBreakLength letters on two lines (TwoLineCaption)
// Version: 1.0.0
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework
#include "UltraCanvasToolRows.h"
#include "UltraCanvasFormLayout.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    namespace {
        constexpr float kValueRowHeight = 24.0f;
        constexpr float kFontRowHeight = 26.0f;
        constexpr float kSwatchRowHeight = 22.0f;

        // The tooltip of a tool: its name and, when it has one, its shortcut.
        std::string ToolTip(const std::string& name, const std::string& shortcut) {
            if (shortcut.empty()) return name;
            return name + " (" + shortcut + ")";
        }
    }

    // ===== BUTTON ROW =====

    UltraCanvasToolButtonRow::UltraCanvasToolButtonRow(const std::string& identifier, float size)
            : UltraCanvasContainer(identifier, 0, 0, 0, 0), buttonSize(size > 0 ? size : 28.0f) {
        layout.SetFlexRow()
              .SetFlexWrap(CSSLayout::FlexWrap::Wrap)
              .SetFlexGap(2.0f)
              .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        layoutItem.SetFlexShrink(0);
        SetBackgroundColor(Colors::Transparent);
        ApplyThemeIfFollowing();
    }

    void UltraCanvasToolButtonRow::StyleButton(UltraCanvasButton& button, const UltraCanvasTheme& theme) const {
        ButtonStyle base = button.GetStyle();
        base.cornerRadius = 4.0f;
        base.useIconAsMask = true;
        base.borderWidth = 0.0f;
        button.SetThemeVariant(ButtonVariant::Ghost);
        button.SetStyle(ButtonStyle::FromTheme(theme, base, ButtonVariant::Ghost));
    }

    void UltraCanvasToolButtonRow::ApplyTheme(const UltraCanvasTheme& theme) {
        UltraCanvasContainer::ApplyTheme(theme);
        for (auto& entry : entries) StyleButton(*entry.button, theme);
    }

    std::shared_ptr<UltraCanvasButton> UltraCanvasToolButtonRow::AddButton(const ToolRowButton& spec) {
        const bool toggle = static_cast<bool>(spec.isChecked) || static_cast<bool>(spec.setChecked);
        const float width = spec.text.empty() ? buttonSize : -1.0f;
        auto button = std::make_shared<UltraCanvasButton>(spec.id, 0, 0, width, buttonSize, spec.text);
        if (!spec.iconPath.empty()) {
            button->SetIcon(spec.iconPath);
            const int icon = static_cast<int>(std::round(buttonSize * 0.64f));
            button->SetIconSize(icon, icon);
        }
        if (!spec.text.empty()) button->SetPadding(0, 6, 0, 6);
        button->SetTooltip(ToolTip(spec.name, spec.shortcut));
        if (!spec.name.empty()) button->SetAccessibleName(spec.name);
        button->layoutItem.SetFlexShrink(0);
        button->SetCanToggled(toggle);
        StyleButton(*button, UltraCanvasTheme::Current());
        if (toggle) {
            // The button is the row's child: the setter is the host's.
            auto setter = spec.setChecked;
            button->onToggle = [setter](bool pressed) {
                if (setter) setter(pressed);
            };
            if (spec.isChecked) button->SetPressed(spec.isChecked());
        } else {
            auto click = spec.onClick;
            button->onClick = [click]() {
                if (click) click();
            };
        }
        if (spec.isEnabled) button->SetDisabled(!spec.isEnabled());
        AddChild(button);
        entries.push_back({spec, button});
        return button;
    }

    std::shared_ptr<UltraCanvasButton> UltraCanvasToolButtonRow::AddButton(const std::string& id, const std::string& iconPath,
                                                                         const std::string& name, const std::string& shortcut,
                                                                         std::function<void()> onClick) {
        ToolRowButton spec;
        spec.id = id;
        spec.iconPath = iconPath;
        spec.name = name;
        spec.shortcut = shortcut;
        spec.onClick = std::move(onClick);
        return AddButton(spec);
    }

    std::shared_ptr<UltraCanvasButton> UltraCanvasToolButtonRow::AddToggle(const std::string& id, const std::string& iconPath,
                                                                         const std::string& name, const std::string& shortcut,
                                                                         std::function<bool()> isChecked,
                                                                         std::function<void(bool)> setChecked) {
        ToolRowButton spec;
        spec.id = id;
        spec.iconPath = iconPath;
        spec.name = name;
        spec.shortcut = shortcut;
        spec.isChecked = std::move(isChecked);
        spec.setChecked = std::move(setChecked);
        return AddButton(spec);
    }

    void UltraCanvasToolButtonRow::AddGap(float width) {
        auto gap = std::make_shared<UltraCanvasUIElement>(GetIdentifier() + ".gap" + std::to_string(gapCount++),
                                                          std::max(0.0f, width - 2.0f), 1.0f);
        gap->layoutItem.SetFlexShrink(0);
        gap->SetInteractive(false);
        AddChild(gap);
    }

    std::shared_ptr<UltraCanvasButton> UltraCanvasToolButtonRow::GetButton(const std::string& id) const {
        for (const auto& entry : entries) {
            if (entry.spec.id == id) return entry.button;
        }
        return nullptr;
    }

    void UltraCanvasToolButtonRow::Sync() {
        for (auto& entry : entries) {
            if (entry.spec.isChecked && entry.button->CanToggle()) {
                const bool on = entry.spec.isChecked();
                if (entry.button->IsPressed() != on) entry.button->SetPressed(on);
            }
            if (entry.spec.isEnabled) {
                const bool enabled = entry.spec.isEnabled();
                if (entry.button->IsDisabled() == enabled) entry.button->SetDisabled(!enabled);
            }
        }
    }

    // ===== VALUE ROWS =====

    std::string UltraCanvasToolValueRows::TwoLineCaption(const std::string& caption) {
        // Its letters (UTF-8 lead bytes) and the spaces it could break at.
        std::vector<size_t> spaces;
        size_t letters = 0;
        for (size_t i = 0; i < caption.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(caption[i]);
            if ((c & 0xC0) == 0x80) continue;
            if (c == ' ') spaces.push_back(i);
            if (c == '\n') return caption;   // broken already
            ++letters;
        }
        if (letters <= kCaptionBreakLength || spaces.empty()) return caption;
        // The space nearest the middle, by bytes: near enough for a caption.
        size_t best = spaces.front();
        for (size_t at : spaces)
            if (std::abs(static_cast<long>(at) - static_cast<long>(caption.size() / 2)) <
                std::abs(static_cast<long>(best) - static_cast<long>(caption.size() / 2)))
                best = at;
        return caption.substr(0, best) + "\n" + caption.substr(best + 1);
    }

    UltraCanvasToolValueRows::UltraCanvasToolValueRows(const std::string& identifier, float rowGap, float columnGap)
            : UltraCanvasContainer(identifier, 0, 0, 0, 0) {
        // The form grid of UltraCanvasFormLayout.h: the captions' column as
        // wide as the widest caption, the controls in the rest. A long
        // caption is set on two lines (AddRow), so that it does not narrow
        // the controls until their values are cut off.
        layout.SetGrid();
        CSSLayout::GridTrackSize captionColumn;
        CSSLayout::GridTrackSize controlColumn;
        controlColumn.kind = CSSLayout::GridTrackSizeKind::Fr;
        controlColumn.value = CSSLayout::Dimension::Fr(1);
        layout.SetGridColumns({captionColumn, controlColumn});
        layout.SetGridGap(rowGap, columnGap);
        layout.SetGridAlignItems(CSSLayout::AlignItems::Center);
        layout.SetGridJustifyItems(CSSLayout::JustifyItems::Stretch);
        layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        SetBackgroundColor(Colors::Transparent);
        ContainerStyle plain = GetContainerStyle();
        plain.autoShowScrollbars = false;
        plain.forceShowVerticalScrollbar = false;
        plain.forceShowHorizontalScrollbar = false;
        SetContainerStyle(plain);
    }

    void UltraCanvasToolValueRows::AddRow(const std::string& id, const std::string& caption,
                                          const std::shared_ptr<UltraCanvasUIElement>& control,
                                          std::function<void()> sync) {
        if (!control) return;
        auto label = CreateFormCaption(id + "-caption", TwoLineCaption(caption));
        AddChild(label);
        AddChild(control);
        if (sync) syncers.push_back(std::move(sync));
    }

    void UltraCanvasToolValueRows::AddWideRow(const std::shared_ptr<UltraCanvasUIElement>& element,
                                              std::function<void()> sync) {
        if (!element) return;
        CSSLayout::GridLine autoStart;
        CSSLayout::GridLine spanTwo;
        spanTwo.type = CSSLayout::GridLineKind::Span;
        spanTwo.index = 2;
        element->layoutItem.SetGridColumn(autoStart, spanTwo);
        AddChild(element);
        if (sync) syncers.push_back(std::move(sync));
    }

    std::shared_ptr<UltraCanvasSpinner> UltraCanvasToolValueRows::AddSpinner(const std::string& id, const std::string& caption,
                                                                           double minValue, double maxValue, double step,
                                                                           int decimals, std::function<double()> getValue,
                                                                           std::function<void(double)> setValue,
                                                                           const std::string& suffix) {
        auto spinner = decimals > 0
                ? CreateDecimalSpinner(id, 0, 0, 0, kValueRowHeight, minValue, maxValue, minValue, step, decimals)
                : CreateIntSpinner(id, 0, 0, 0, kValueRowHeight, static_cast<int>(minValue), static_cast<int>(maxValue),
                                   static_cast<int>(minValue), static_cast<int>(std::max(1.0, step)));
        if (!suffix.empty()) spinner->SetSuffix(" " + suffix);
        spinner->SetAccessibleName(caption);
        if (getValue) spinner->SetValue(getValue());
        spinner->onValueChanged = [this, setValue](double value) {
            if (!syncing && setValue) setValue(value);
        };
        UltraCanvasSpinner* shown = spinner.get();
        AddRow(id, caption, spinner, [shown, getValue]() {
            if (getValue) shown->SetValue(getValue());
        });
        return spinner;
    }

    std::shared_ptr<UltraCanvasDropdown> UltraCanvasToolValueRows::AddDropdown(const std::string& id, const std::string& caption,
                                                                             const std::vector<std::string>& items,
                                                                             std::function<int()> getIndex,
                                                                             std::function<void(int)> setIndex) {
        auto dropdown = CreateDropdown(id, 0, 0, 0, kValueRowHeight);
        for (const auto& item : items) dropdown->AddItem(item);
        dropdown->SetAccessibleName(caption);
        if (getIndex) dropdown->SetSelectedIndex(getIndex(), false);
        dropdown->onSelectionChanged = [this, setIndex](int index, const DropdownItem&) {
            if (!syncing && setIndex) setIndex(index);
        };
        UltraCanvasDropdown* shown = dropdown.get();
        AddRow(id, caption, dropdown, [shown, getIndex]() {
            if (getIndex) shown->SetSelectedIndex(getIndex(), false);
        });
        return dropdown;
    }

    std::shared_ptr<UltraCanvasColorSwatchBar> UltraCanvasToolValueRows::AddColorSwatches(const std::string& id,
                                                                                        const std::string& caption,
                                                                                        const std::vector<Color>& colors,
                                                                                        std::function<Color()> getColor,
                                                                                        std::function<void(const Color&)> setColor) {
        auto swatches = std::make_shared<UltraCanvasColorSwatchBar>(id, 0, 0, 0, kSwatchRowHeight);
        swatches->SetColors(colors);
        swatches->SetAccessibleName(caption);
        if (getColor) swatches->SelectColor(getColor(), false);
        swatches->onColorSelected = [this, setColor](const Color& color) {
            if (!syncing && setColor) setColor(color);
        };
        UltraCanvasColorSwatchBar* shown = swatches.get();
        AddRow(id, caption, swatches, [shown, getColor]() {
            if (getColor) shown->SelectColor(getColor(), false);
        });
        return swatches;
    }

    std::shared_ptr<UltraCanvasDropdown> UltraCanvasToolValueRows::AddListRow(const std::string& id,
                                                                            const std::vector<std::string>& items,
                                                                            std::function<int()> getIndex,
                                                                            std::function<void(int)> setIndex) {
        auto dropdown = CreateDropdown(id, 0, 0, 0, kValueRowHeight);
        for (const auto& item : items) dropdown->AddItem(item);
        if (getIndex) dropdown->SetSelectedIndex(getIndex(), false);
        dropdown->onSelectionChanged = [this, setIndex](int index, const DropdownItem&) {
            if (!syncing && setIndex) setIndex(index);
        };
        UltraCanvasDropdown* shown = dropdown.get();
        AddWideRow(dropdown, [shown, getIndex]() {
            if (getIndex) shown->SetSelectedIndex(getIndex(), false);
        });
        return dropdown;
    }

    std::shared_ptr<UltraCanvasFontPicker> UltraCanvasToolValueRows::AddFontPicker(const std::string& id,
                                                                                 std::function<std::string()> getFamily,
                                                                                 std::function<void(const FontPickerChoice&)> setFont) {
        auto picker = CreateFontPicker(id, 0, 0, 0, kFontRowHeight);
        auto show = [](UltraCanvasFontPicker* p, const std::string& family) {
            if (family.empty()) p->SetMixed();
            else p->SetCurrentFont(family);
        };
        if (getFamily) show(picker.get(), getFamily());
        picker->onFontChosen = [this, setFont](const FontPickerChoice& choice) {
            if (!syncing && setFont) setFont(choice);
        };
        UltraCanvasFontPicker* shown = picker.get();
        AddWideRow(picker, [shown, getFamily, show]() {
            if (getFamily) show(shown, getFamily());
        });
        return picker;
    }

    void UltraCanvasToolValueRows::Sync() {
        syncing = true;
        for (auto& sync : syncers) sync();
        syncing = false;
    }

    // ===== HELPERS =====

    std::shared_ptr<UltraCanvasContainer> CreateToolPanelContent(const std::string& identifier, float rowGap) {
        auto content = std::make_shared<UltraCanvasContainer>(identifier, 0, 0, 0, 0);
        content->layout.SetFlexColumn().SetFlexGap(rowGap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        content->layoutItem.SetFlexShrink(0);
        content->SetBackgroundColor(Colors::Transparent);
        DisableScrollbars(content);
        return content;
    }

    void SyncToolRows(UltraCanvasContainer* container) {
        if (!container) return;
        if (auto* buttons = dynamic_cast<UltraCanvasToolButtonRow*>(container)) buttons->Sync();
        if (auto* values = dynamic_cast<UltraCanvasToolValueRows*>(container)) values->Sync();
        for (const auto& child : container->GetChildren()) {
            if (auto* inner = dynamic_cast<UltraCanvasContainer*>(child.get())) SyncToolRows(inner);
        }
    }

} // namespace UltraCanvas
