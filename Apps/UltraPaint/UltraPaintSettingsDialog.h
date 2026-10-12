// Apps/UltraPaint/UltraPaintSettingsDialog.h
// UltraPaint's settings window (Edit > Settings...): a tree of settings
// pages on the left - Display > Mouse pointer magnification glass - and the
// selected page on the right. A change applies to the running editor at
// once through onChanged and is saved right away; the window has only a
// Close button. Built out of framework elements like the other dialogs and
// shown through UltraPaintWindow::ShowDialog.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include "UltraPaintSettings.h"

#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasLabel.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace UltraCanvas {

class UltraPaintSettingsDialog : public UltraCanvasWindow {
public:
    // `settings` must outlive the window; it is written on every change.
    explicit UltraPaintSettingsDialog(UltraPaintSettings* settings);

    // After every change, once the settings hold the new value and before
    // they are saved: the host re-applies them to its widgets.
    std::function<void()> onChanged;

private:
    std::shared_ptr<UltraCanvasContainer> BuildMagnifierPage();
    void ShowPage(const std::string& pageId);
    void UpdateMagnifierControls();
    void Changed();

    UltraPaintSettings* settings;
    // Set while the controls are pushed to the settings' values, so the
    // change handlers that fires do not write and save them once more.
    bool syncing = false;

    std::shared_ptr<UltraCanvasTreeView>  tree;
    std::shared_ptr<UltraCanvasContainer> pageArea;
    std::map<std::string, std::shared_ptr<UltraCanvasContainer>> pages;

    // Display > Mouse pointer magnification glass
    std::shared_ptr<UltraCanvasCheckbox> magnifierBox;
    std::shared_ptr<UltraCanvasRadio>    factorRadios[3];   // 2x, 3x, 4x
    UltraCanvasRadioGroup                factorGroup;
    std::shared_ptr<UltraCanvasSlider>   sizeSlider;
    std::shared_ptr<UltraCanvasLabel>    sizeValue;
};

} // namespace UltraCanvas
