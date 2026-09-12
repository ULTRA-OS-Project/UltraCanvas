// Apps/UltraPaint/UltraPaintDialogs.h
// UltraPaint's small parameter windows: New Image (size, background),
// Scale Image, Canvas Size, the Text tool's entry, layer properties,
// Colour to Alpha and the import question a dropped file asks. Each is an
// UltraCanvasWindow built out of framework elements (spinners, dropdowns,
// text input, colour picker, buttons) and reports through callbacks.
// Version: 1.1.0
// Last Modified: 2026-09-12
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
#include "UltraCanvasColorPicker.h"

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

// ===== IMPORT (a dropped file, or opening a vector drawing) =====
// What the window already knows about an incoming file when it asks what to
// do with it. For a bitmap `naturalWidth`/`naturalHeight` are the pixels it
// decoded to; for a vector drawing they are the size the artwork asks for,
// and the dialog offers to rasterize at a different one.
struct UltraPaintImportRequest {
    std::string path;
    bool vector = false;          // needs rasterizing: the size controls appear
    int  naturalWidth = 0;
    int  naturalHeight = 0;
    int  pageCount = 1;           // > 1 only for a paged vector source (PDF)
    std::string provider;         // what rasterizes it, named in the subtitle
    bool offerMerge = false;      // a document is open: Merge / New Window
    int  canvasWidth = 0;         // the open document, for the fit checkbox
    int  canvasHeight = 0;
    // The rest of a multi-file drop. They follow the same answer, at their
    // own natural size, so they are named in the dialog but not sized by it.
    size_t extraFiles = 0;
};

struct UltraPaintImportResult {
    enum class Action { Cancel, Open, NewWindow, Merge };
    Action action = Action::Cancel;
    int  width = 0;               // raster size for a vector source
    int  height = 0;
    int  page = 0;                // 0-based
    bool scaleToFit = false;      // merge: shrink the image into the canvas
};

class UltraPaintImportDialog : public UltraCanvasWindow {
public:
    explicit UltraPaintImportDialog(const UltraPaintImportRequest& request);
    std::function<void(const UltraPaintImportResult&)> onAccept;
private:
    UltraPaintImportResult Collect(UltraPaintImportResult::Action action) const;
    void Finish(UltraPaintImportResult::Action action);
    void SyncFromWidth();
    void SyncFromHeight();
    UltraPaintImportRequest req;
    bool syncing = false;
    std::shared_ptr<UltraCanvasSpinner> widthSpin, heightSpin, pageSpin;
    std::shared_ptr<UltraCanvasCheckbox> fitBox;
};

// ===== COLOUR TO ALPHA =====
// Which colour to key out of the layer and how hard. `transparency` is a
// percentage so the colour can be faded rather than only removed outright:
// 100 makes it fully transparent, 40 takes 40% of its opacity away.
struct UltraPaintColourToAlphaParams {
    Color colour = Colors::White;
    int  tolerance = 0;       // 0..255, how far from the colour still counts as it
    int  softness = 32;       // 0..255, the width of the ramp past the tolerance
    int  transparency = 100;  // 0..100 %
    bool despill = true;      // un-mix the colour out of the part-transparent edges
};

class UltraPaintColourToAlphaDialog : public UltraCanvasWindow {
public:
    explicit UltraPaintColourToAlphaDialog(const UltraPaintColourToAlphaParams& initial);

    const UltraPaintColourToAlphaParams& GetParams() const { return params; }
    bool IsPreviewEnabled() const { return previewEnabled; }

    // Live while the user drags; `enabled` false means "show the original".
    std::function<void(const UltraPaintColourToAlphaParams&, bool enabled)> onPreview;
    std::function<void(const UltraPaintColourToAlphaParams&)> onAccept;
    std::function<void()> onCancel;

private:
    void EmitPreview();

    UltraPaintColourToAlphaParams params;
    bool previewEnabled = true;
    bool accepted = false;
    std::shared_ptr<UltraCanvasColorPicker> picker;
    std::shared_ptr<UltraCanvasContainer> sliders;
    std::shared_ptr<UltraCanvasCheckbox> previewBox;
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
