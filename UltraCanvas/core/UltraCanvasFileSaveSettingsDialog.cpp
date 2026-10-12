// core/UltraCanvasFileSaveSettingsDialog.cpp
// UltraCanvasFileLoader::ShowSaveSettingsDialog: one format's save settings
// in a modal dialog - a checkbox, a slider or a list per setting, built from
// the format's description, so it needs to know nothing about the format.
// The Save dialog's Settings button opens it. See UltraCanvasFileSaveSettings.h.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasFileSaveSettings.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    namespace {
        constexpr int kRowHeight = 28;
        constexpr float kRowGap = 8.0f;
        constexpr float kColumnGap = 12.0f;

        // The element id of a setting's control, which a test (or an
        // application driving the dialog) finds it by.
        std::string ControlId(const FileSaveSettingSpec& spec) {
            return "SaveSetting_" + spec.key;
        }

        int ChoiceIndex(const FileSaveSettingSpec& spec, const std::string& value) {
            for (size_t i = 0; i < spec.choices.size(); ++i)
                if (spec.choices[i].value == value) return static_cast<int>(i);
            return 0;
        }
    } // namespace

    void UltraCanvasFileLoader::ShowSaveSettingsDialog(const std::string& extensionOrPath,
                                                       UltraCanvasWindowBase* parent,
                                                       std::function<void(DialogResult)> onClosed) {
        FileSaveFormat format;
        if (!FindSaveFormat(extensionOrPath, format) || format.settings.empty()) {
            if (onClosed) onClosed(DialogResult::Cancel);
            return;
        }
        // What the controls change. Stored only on OK, so Cancel leaves every
        // application's settings as they were.
        auto values = std::make_shared<FileSaveSettings>(GetSaveSettings(format.Key()));

        DialogConfig config;
        config.title = format.name + " settings";
        config.message = "How " + format.name + " files are written. Every application "
                         "saving a " + format.name + " file uses these.";
        // Information builds the message column the form goes into (Custom
        // builds nothing); the severity icon is left off.
        config.dialogType = DialogType::Information;
        config.buttons = DialogButtons::OKCancel;
        config.defaultButton = DialogButton::OK;
        config.showIcon = false;
        config.width = 460;
        config.height = 220;
        auto dialog = UltraCanvasDialogManager::CreateDialog(config);
        if (!dialog) {
            if (onClosed) onClosed(DialogResult::Cancel);
            return;
        }
        const float fontSize = dialog->GetStyle().messageFontSize;

        // "caption: control", one row per setting. The grid carries its own
        // height so the dialog is sized for it before anything is measured.
        auto form = CreateFormGrid("SaveSettingsForm", kRowGap, kColumnGap);
        const int rows = static_cast<int>(format.settings.size());
        form->size.height = CSSLayout::Dimension::Px(
                static_cast<float>(rows * kRowHeight) + kRowGap * static_cast<float>(rows - 1));

        // Sets every control from `values` again (the Defaults button).
        auto refreshers = std::make_shared<std::vector<std::function<void()>>>();

        for (const FileSaveSettingSpec& spec : format.settings) {
            const std::string id = ControlId(spec);
            const std::string key = spec.key;
            switch (spec.kind) {
                case FileSaveSettingKind::Toggle: {
                    // The checkbox is its own caption; it lines up with the
                    // other controls rather than with the captions.
                    auto box = UltraCanvasCheckbox::CreateCheckbox(id, 0, 0, -1, kRowHeight, spec.label,
                                                                   values->GetBool(key));
                    box->SetFontSize(fontSize);
                    if (!spec.help.empty()) box->SetTooltip(spec.help);
                    box->onStateChanged = [values, key](CheckedState, CheckedState now) {
                        values->SetBool(key, now == CheckedState::Checked);
                    };
                    UltraCanvasCheckbox* raw = box.get();
                    refreshers->push_back([values, key, raw]() { raw->SetChecked(values->GetBool(key)); });
                    auto caption = AddFormRow(form, id, "", box);
                    if (caption) caption->SetFontSize(fontSize);
                    break;
                }
                case FileSaveSettingKind::Number: {
                    auto cell = CreateFormCellRow(id + "-row", 10.0f, kRowHeight);
                    auto slider = std::make_shared<UltraCanvasSlider>(id, 0, 0, 200, 24);
                    slider->SetRange(static_cast<float>(spec.minimum), static_cast<float>(spec.maximum));
                    slider->SetStep(1.0f);
                    slider->SetValue(static_cast<float>(values->GetInt(key, spec.minimum)));
                    slider->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
                    if (!spec.help.empty()) slider->SetTooltip(spec.help);
                    auto number = std::make_shared<UltraCanvasLabel>(id + "-value", 0, 0, 40, 24,
                            std::to_string(values->GetInt(key, spec.minimum)));
                    number->SetFontSize(fontSize);
                    number->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);
                    number->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                    UltraCanvasLabel* numberRaw = number.get();
                    slider->onValueChanged = [values, key, numberRaw](float v) {
                        const int n = static_cast<int>(std::lround(v));
                        values->SetInt(key, n);
                        numberRaw->SetText(std::to_string(n));
                    };
                    UltraCanvasSlider* sliderRaw = slider.get();
                    const int fallback = spec.minimum;
                    refreshers->push_back([values, key, sliderRaw, numberRaw, fallback]() {
                        const int n = values->GetInt(key, fallback);
                        sliderRaw->SetValue(static_cast<float>(n));
                        numberRaw->SetText(std::to_string(n));
                    });
                    cell->AddChild(slider);
                    cell->AddChild(number);
                    auto caption = AddFormRow(form, id, spec.label + ":", cell);
                    if (caption) caption->SetFontSize(fontSize);
                    break;
                }
                case FileSaveSettingKind::Choice: {
                    auto list = std::make_shared<UltraCanvasDropdown>(id, 0, 0, 220, kRowHeight);
                    for (const FileSaveChoice& choice : spec.choices) list->AddItem(choice.label, choice.value);
                    list->SetSelectedIndex(ChoiceIndex(spec, values->Get(key)), false);
                    if (!spec.help.empty()) list->SetTooltip(spec.help);
                    const std::vector<FileSaveChoice> choices = spec.choices;
                    list->onSelectionChanged = [values, key, choices](int index, const DropdownItem&) {
                        if (index >= 0 && index < static_cast<int>(choices.size()))
                            values->Set(key, choices[static_cast<size_t>(index)].value);
                    };
                    UltraCanvasDropdown* raw = list.get();
                    const FileSaveSettingSpec copy = spec;
                    refreshers->push_back([values, key, raw, copy]() {
                        raw->SetSelectedIndex(ChoiceIndex(copy, values->Get(key)), false);
                    });
                    auto caption = AddFormRow(form, id, spec.label + ":", list);
                    if (caption) caption->SetFontSize(fontSize);
                    break;
                }
            }
        }
        dialog->AddDialogElement(form);

        // Back to the format's defaults, in the controls only: OK stores them,
        // Cancel keeps what was there.
        auto defaults = std::make_shared<UltraCanvasButton>("SaveSettingsDefaults", 0, 0, 90, 28);
        defaults->SetText("Defaults");
        defaults->SetTooltip("Put every setting back to " + format.name + "'s default");
        defaults->size.width = CSSLayout::Dimension::Px(90);
        defaults->size.height = CSSLayout::Dimension::Px(28);
        const std::vector<FileSaveSettingSpec> specs = format.settings;
        defaults->onClick = [values, specs, refreshers]() {
            for (const FileSaveSettingSpec& spec : specs) values->Set(spec.key, spec.defaultValue);
            for (const auto& refresh : *refreshers) refresh();
        };
        dialog->AddFooterElement(defaults);

        UltraCanvasDialogManager::ShowDialog(dialog, [values, onClosed](DialogResult result) {
            if (result == DialogResult::OK) SetSaveSettings(*values);
            if (onClosed) onClosed(result);
        }, parent);
    }

} // namespace UltraCanvas
