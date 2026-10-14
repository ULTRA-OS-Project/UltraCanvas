// core/UltraCanvasFileSaveSettingsDialog.cpp
// UltraCanvasFileLoader::ShowSaveSettingsDialog: one format's save settings
// in a modal dialog - a checkbox, a slider or a list per setting, built from
// the format's description, so it needs to know nothing about the format.
// The Save dialog's Settings button opens it. See UltraCanvasFileSaveSettings.h.
// For a picture of known size a bitmap format's dialog starts with a Size row:
// width x height and Keep proportions, for this save only. A drawing has no
// pixel size of its own: its row opens on the size it is drawn at and says it
// is drawn sharp at any.
// Version: 1.2.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasFileSaveSettings.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSeparator.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSpinner.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {

    namespace {
        constexpr int kRowHeight = 28;
        constexpr float kRowGap = 10.0f;
        constexpr float kColumnGap = 16.0f;
        // Every control is this wide and starts where the captions end, so
        // the sliders, lists and size fields make one straight column; a
        // slider's value sits right beside it rather than at the far edge.
        constexpr int kControlWidth = 260;
        constexpr int kValueWidth = 36;
        constexpr float kSliderGap = 10.0f;
        constexpr int kSliderWidth = kControlWidth - kValueWidth - static_cast<int>(kSliderGap);
        constexpr int kSpinnerWidth = 92;

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

        // The largest side a bitmap is offered at: JPEG's limit, and far
        // beyond any screen.
        constexpr int kMaxSide = 65535;

        // The Size row's state while the dialog is open. `syncing` is set
        // while one side is being made to follow the other, so the follower's
        // own change does not turn round and move the first.
        struct SizeChoice {
            int pictureWidth = 0;
            int pictureHeight = 0;
            int width = 0;
            int height = 0;
            bool keep = true;
            bool syncing = false;

            int Follow(int side, int fromSide, int toSide) const {
                const long long scaled = std::llround(static_cast<double>(side) * toSide / fromSide);
                return static_cast<int>(std::clamp<long long>(scaled, 1, kMaxSide));
            }
            int HeightFor(int w) const { return Follow(w, pictureWidth, pictureHeight); }
            int WidthFor(int h) const { return Follow(h, pictureHeight, pictureWidth); }
        };
    } // namespace

    void UltraCanvasFileLoader::ShowSaveSettingsDialog(const std::string& extensionOrPath,
                                                       UltraCanvasWindowBase* parent,
                                                       std::function<void(DialogResult)> onClosed) {
        FileSaveFormat format;
        if (!FindSaveFormat(extensionOrPath, format) || format.settings.empty()) {
            if (onClosed) onClosed(DialogResult::Cancel);
            return;
        }
        ShowSaveSettingsDialog(extensionOrPath, 0, 0, FileSaveImageSize(), parent,
                [onClosed](DialogResult result, const FileSaveImageSize&) {
                    if (onClosed) onClosed(result);
                });
    }

    void UltraCanvasFileLoader::ShowSaveSettingsDialog(
            const std::string& extensionOrPath, int pictureWidth, int pictureHeight,
            const FileSaveImageSize& size, UltraCanvasWindowBase* parent,
            std::function<void(DialogResult, const FileSaveImageSize&)> onClosed) {
        FileSavePicture picture;
        picture.width = pictureWidth;
        picture.height = pictureHeight;
        ShowSaveSettingsDialog(extensionOrPath, picture, size, parent, std::move(onClosed));
    }

    void UltraCanvasFileLoader::ShowSaveSettingsDialog(
            const std::string& extensionOrPath, const FileSavePicture& picture,
            const FileSaveImageSize& size, UltraCanvasWindowBase* parent,
            std::function<void(DialogResult, const FileSaveImageSize&)> onClosed) {
        const int pictureWidth = picture.width;
        const int pictureHeight = picture.height;
        FileSaveFormat format;
        const bool found = FindSaveFormat(extensionOrPath, format);
        // The size is offered for a picture of known size in a pixel format,
        // even one with no settings of its own (BMP); a drawing's is the size
        // it is drawn at.
        const bool offerSize = found && format.bitmap && picture.HasSize();
        const bool drawing = offerSize && picture.drawing;
        if (!found || (format.settings.empty() && !offerSize)) {
            if (onClosed) onClosed(DialogResult::Cancel, size);
            return;
        }
        // What the controls change. Stored only on OK, so Cancel leaves every
        // application's settings as they were.
        auto values = std::make_shared<FileSaveSettings>(GetSaveSettings(format.Key()));
        auto chosen = std::make_shared<SizeChoice>();
        chosen->pictureWidth = pictureWidth;
        chosen->pictureHeight = pictureHeight;
        chosen->width = size.IsOriginal() ? pictureWidth : std::min(size.width, kMaxSide);
        chosen->height = size.IsOriginal() ? pictureHeight : std::min(size.height, kMaxSide);
        chosen->keep = size.keepProportions;

        DialogConfig config;
        config.title = format.name + " settings";
        // No text above the controls: the title names the format, and what a
        // setting does is its tooltip.
        // Information builds the message column the form goes into (Custom
        // builds nothing); the severity icon is left off.
        config.dialogType = DialogType::Information;
        config.buttons = DialogButtons::OKCancel;
        config.defaultButton = DialogButton::OK;
        config.showIcon = false;
        config.width = 420;
        config.height = 160;
        auto dialog = UltraCanvasDialogManager::CreateDialog(config);
        if (!dialog) {
            if (onClosed) onClosed(DialogResult::Cancel, size);
            return;
        }
        if (auto message = dialog->FindChildById("MessageArea")) message->SetVisible(false);
        const float fontSize = dialog->GetStyle().messageFontSize;

        // "caption: control", one row per setting; a rule between the size,
        // which is this save's, and the format's own settings. The grid
        // carries its own height so the dialog is sized for it before
        // anything is measured.
        auto form = CreateFormGrid("SaveSettingsForm", kRowGap, kColumnGap);
        const bool rule = offerSize && !format.settings.empty();
        const int rows = static_cast<int>(format.settings.size()) + (offerSize ? 2 : 0);
        const int gaps = rows - 1 + (rule ? 1 : 0);
        form->size.height = CSSLayout::Dimension::Px(
                static_cast<float>(rows * kRowHeight + (rule ? 1 : 0)) + kRowGap * static_cast<float>(gaps));

        // Sets every control from `values` (and the size from `chosen`)
        // again: the Defaults button.
        auto refreshers = std::make_shared<std::vector<std::function<void()>>>();

        if (offerSize) {
            // "Size: [ 1920 ] x [ 1080 ] px", and Keep proportions under it.
            auto cell = CreateFormCellRow("SaveSize-row", 6.0f, kRowHeight);
            cell->size.width = CSSLayout::Dimension::Px(kControlWidth);
            auto width = CreateIntSpinner("SaveSizeWidth", 0, 0, kSpinnerWidth, kRowHeight,
                                          1, kMaxSide, chosen->width, 1);
            auto height = CreateIntSpinner("SaveSizeHeight", 0, 0, kSpinnerWidth, kRowHeight,
                                           1, kMaxSide, chosen->height, 1);
            width->SetTooltip(drawing ? "Width in pixels: the drawing is drawn sharp at any"
                                      : "Width in pixels");
            height->SetTooltip(drawing ? "Height in pixels: the drawing is drawn sharp at any"
                                       : "Height in pixels");
            width->layoutItem.SetFlexShrink(0);
            height->layoutItem.SetFlexShrink(0);
            auto times = std::make_shared<UltraCanvasLabel>("SaveSize-times", 0, 0, 14, kRowHeight, "\u00D7");
            auto unit = std::make_shared<UltraCanvasLabel>("SaveSize-unit", 0, 0, 22, kRowHeight, "px");
            for (const auto& label : {times, unit}) {
                label->SetFontSize(fontSize);
                label->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
                label->layoutItem.SetFlexShrink(0);
            }
            cell->AddChild(width);
            cell->AddChild(times);
            cell->AddChild(height);
            cell->AddChild(unit);
            auto caption = AddFormRow(form, "SaveSize", "Size:", cell);
            if (caption) caption->SetFontSize(fontSize);

            auto keep = UltraCanvasCheckbox::CreateCheckbox("SaveSizeKeepProportions", 0, 0, -1, kRowHeight,
                                                            "Keep proportions", chosen->keep);
            keep->SetFontSize(fontSize);
            keep->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Start);
            keep->SetTooltip(drawing ? "Change one side and the other follows, as the drawing is shaped"
                                     : "Change one side and the other follows, as the picture is shaped");
            auto keepCaption = AddFormRow(form, "SaveSizeKeep", "", keep);
            if (keepCaption) keepCaption->SetFontSize(fontSize);

            UltraCanvasSpinner* widthRaw = width.get();
            UltraCanvasSpinner* heightRaw = height.get();
            width->onValueChanged = [chosen, heightRaw](double v) {
                if (chosen->syncing) return;
                chosen->width = static_cast<int>(std::llround(v));
                if (!chosen->keep) return;
                chosen->syncing = true;
                chosen->height = chosen->HeightFor(chosen->width);
                heightRaw->SetValue(chosen->height);
                chosen->syncing = false;
            };
            height->onValueChanged = [chosen, widthRaw](double v) {
                if (chosen->syncing) return;
                chosen->height = static_cast<int>(std::llround(v));
                if (!chosen->keep) return;
                chosen->syncing = true;
                chosen->width = chosen->WidthFor(chosen->height);
                widthRaw->SetValue(chosen->width);
                chosen->syncing = false;
            };
            keep->onStateChanged = [chosen, heightRaw](CheckedState, CheckedState now) {
                chosen->keep = now == CheckedState::Checked;
                if (!chosen->keep || chosen->syncing) return;
                // Locked again: the height falls back in line with the width.
                chosen->syncing = true;
                chosen->height = chosen->HeightFor(chosen->width);
                heightRaw->SetValue(chosen->height);
                chosen->syncing = false;
            };
            UltraCanvasCheckbox* keepRaw = keep.get();
            refreshers->push_back([chosen, widthRaw, heightRaw, keepRaw]() {
                chosen->syncing = true;
                chosen->width = chosen->pictureWidth;
                chosen->height = chosen->pictureHeight;
                chosen->keep = true;
                widthRaw->SetValue(chosen->width);
                heightRaw->SetValue(chosen->height);
                keepRaw->SetChecked(true);
                chosen->syncing = false;
            });

            if (rule) {
                auto line = std::make_shared<UltraCanvasSeparator>(false, 1, kControlWidth);
                line->size.width = CSSLayout::Dimension::Auto();
                line->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Stretch);
                AddFormWideRow(form, line);
            }
        }

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
                    box->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Start);
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
                    auto cell = CreateFormCellRow(id + "-row", kSliderGap, kRowHeight);
                    cell->size.width = CSSLayout::Dimension::Px(kControlWidth);
                    auto slider = std::make_shared<UltraCanvasSlider>(id, 0, 0, kSliderWidth, 24);
                    // A horizontal slider draws its track and handle along its
                    // bottom edge (the room above is for a value shown over
                    // it). Exactly as tall as the handle, the row centres it
                    // on the caption and the value beside it.
                    const SliderVisualStyle& look = slider->GetStyle();
                    const float sliderHeight = std::ceil(std::max(look.handleSize,
                                                                  look.trackHeight + 2.0f * look.borderWidth));
                    slider->size.height = CSSLayout::Dimension::Px(sliderHeight);
                    slider->SetRange(static_cast<float>(spec.minimum), static_cast<float>(spec.maximum));
                    slider->SetStep(1.0f);
                    slider->SetValue(static_cast<float>(values->GetInt(key, spec.minimum)));
                    slider->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                    if (!spec.help.empty()) slider->SetTooltip(spec.help);
                    auto number = std::make_shared<UltraCanvasLabel>(id + "-value", 0, 0, kValueWidth, 24,
                            std::to_string(values->GetInt(key, spec.minimum)));
                    number->SetFontSize(fontSize);
                    number->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
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
                    auto list = std::make_shared<UltraCanvasDropdown>(id, 0, 0, kControlWidth, kRowHeight);
                    list->size.width = CSSLayout::Dimension::Px(kControlWidth);
                    list->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Start);
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
        defaults->SetTooltip(offerSize ? "Put every setting back to " + format.name +
                                         "'s default, and the size back to the " +
                                         (drawing ? "drawing's" : "picture's")
                                       : "Put every setting back to " + format.name + "'s default");
        defaults->size.width = CSSLayout::Dimension::Px(90);
        defaults->size.height = CSSLayout::Dimension::Px(28);
        const std::vector<FileSaveSettingSpec> specs = format.settings;
        defaults->onClick = [values, specs, refreshers]() {
            for (const FileSaveSettingSpec& spec : specs) values->Set(spec.key, spec.defaultValue);
            for (const auto& refresh : *refreshers) refresh();
        };
        dialog->AddFooterElement(defaults);

        UltraCanvasDialogManager::ShowDialog(dialog,
                [values, chosen, offerSize, size, onClosed](DialogResult result) {
            FileSaveImageSize picked = size;
            if (result == DialogResult::OK) {
                if (!values->values.empty()) SetSaveSettings(*values);
                if (offerSize) {
                    picked.keepProportions = chosen->keep;
                    // The picture's own size is no size to choose.
                    const bool own = chosen->width == chosen->pictureWidth &&
                                     chosen->height == chosen->pictureHeight;
                    picked.width = own ? 0 : chosen->width;
                    picked.height = own ? 0 : chosen->height;
                }
            }
            if (onClosed) onClosed(result, picked);
        }, parent);
    }

} // namespace UltraCanvas
