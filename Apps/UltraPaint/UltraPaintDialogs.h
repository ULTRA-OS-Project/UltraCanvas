// Apps/UltraPaint/UltraPaintDialogs.h
// UltraPaint's small parameter windows: New Image (size, background),
// Scale Image, Canvas Size and the Text tool's entry. Each is an
// UltraCanvasWindow built out of framework elements (spinners, dropdowns,
// text input, buttons) and reports through callbacks.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {

// ===== NEW IMAGE =====
struct UltraPaintNewImageResult {
    int width = 800;
    int height = 600;
    int background = 0;    // 0 white, 1 black, 2 transparent, 3 foreground colour, 4 background colour
};

class UltraPaintNewImageDialog : public UltraCanvasWindow {
public:
    explicit UltraPaintNewImageDialog(const UltraPaintNewImageResult& initial);
    std::function<void(const UltraPaintNewImageResult&)> onAccept;
private:
    std::shared_ptr<UltraCanvasSpinner> widthSpin, heightSpin;
    std::shared_ptr<UltraCanvasDropdown> presetDrop, backgroundDrop;
};

// ===== SCALE IMAGE / CANVAS SIZE =====
struct UltraPaintResizeResult {
    int width = 0;
    int height = 0;
    int anchor = 4;   // canvas size only: 0..8, row-major, 4 = centre
};

class UltraPaintResizeDialog : public UltraCanvasWindow {
public:
    // `canvasMode` true: Canvas Size (no resampling, with anchor); false: Scale Image.
    UltraPaintResizeDialog(int currentWidth, int currentHeight, bool canvasMode);
    std::function<void(const UltraPaintResizeResult&)> onAccept;
private:
    void SyncFromWidth();
    void SyncFromHeight();
    int origW, origH;
    bool canvas;
    bool syncing = false;
    std::shared_ptr<UltraCanvasSpinner> widthSpin, heightSpin, percentSpin;
    std::shared_ptr<UltraCanvasCheckbox> keepAspect;
    std::shared_ptr<UltraCanvasDropdown> anchorDrop;
};

// ===== TEXT =====
struct UltraPaintTextResult {
    std::string text;
    std::string font = "Sans";
    int size = 32;
    bool bold = false;
};

class UltraPaintTextDialog : public UltraCanvasWindow {
public:
    explicit UltraPaintTextDialog(const UltraPaintTextResult& initial);
    std::function<void(const UltraPaintTextResult&)> onAccept;
private:
    std::shared_ptr<UltraCanvasTextInput> textInput;
    std::shared_ptr<UltraCanvasDropdown> fontDrop;
    std::shared_ptr<UltraCanvasSpinner> sizeSpin;
    std::shared_ptr<UltraCanvasCheckbox> boldBox;
};

// ===== LAYER PROPERTIES =====
struct UltraPaintLayerProps {
    std::string name;
    float opacity = 1.0f;
    int blendIndex = 0;
    bool visible = true;
    bool locked = false;
};

class UltraPaintLayerDialog : public UltraCanvasWindow {
public:
    explicit UltraPaintLayerDialog(const UltraPaintLayerProps& initial);
    std::function<void(const UltraPaintLayerProps&)> onAccept;
private:
    std::shared_ptr<UltraCanvasTextInput> nameInput;
    std::shared_ptr<UltraCanvasSpinner> opacitySpin;
    std::shared_ptr<UltraCanvasDropdown> blendDrop;
    std::shared_ptr<UltraCanvasCheckbox> visibleBox, lockedBox;
};

// ===== SHARED BUILDING BLOCKS =====
namespace UltraPaintDialogParts {
    // A "Label: [widget]" row added to a flex-column window.
    std::shared_ptr<UltraCanvasContainer> LabelledRow(const std::string& id, const std::string& label,
                                                      std::shared_ptr<UltraCanvasUIElement> widget, float labelWidth = 110);
    // OK / Cancel row; `onOk` runs before the window closes.
    std::shared_ptr<UltraCanvasContainer> ButtonRow(const std::string& id, UltraCanvasWindow* win,
                                                    const std::function<void()>& onOk, const std::string& okLabel = "OK");
}

} // namespace UltraCanvas
