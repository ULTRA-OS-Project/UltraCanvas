// Apps/UltraPaint/UltraPaintDialogs.h
// UltraPaint's small parameter windows: New Image (size, background),
// Scale Image, Canvas Size, layer properties, Colour to Alpha, the
// import question a dropped file asks and the password a *.ucd is saved with. Each is an UltraCanvasWindow built
// out of framework elements (spinners, dropdowns, text input, colour picker,
// buttons) and reports through callbacks.
// Version: 1.8.0 - UltraPaintImagePickerDialog: a grid of thumbnails to pick an image from
// Version: 1.7.0 - every text in the person's language; BlendModeDisplayName for the blend-mode lists
// Version: 1.6.0 - Scale Image: the resampling method and Sharpen after scaling
// Version: 1.5.0 - the import question offers "Open in new tab" (Action::NewTab) in place of a new window
// Version: 1.4.0 - Password Protection: the password a *.ucd is saved with
// Version: 1.3.0 - the Text tool's entry is gone: text is typed on the canvas
// Version: 1.2.0
// Last Modified: 2026-11-18
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
#include "UltraCanvasRasterDocument.h"   // UCScaleImageOptions

#include <functional>
#include <memory>
#include <string>
#include <vector>

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
    UCScaleImageOptions scale;   // scale image only: how the pixels are resampled
};

class UltraPaintResizeDialog : public UltraCanvasWindow {
public:
    // `canvasMode` true: Canvas Size (no resampling, with anchor); false: Scale Image.
    // `initialScale` (Scale Image): the method and sharpening chosen last time.
    UltraPaintResizeDialog(int currentWidth, int currentHeight, bool canvasMode,
                           const UCScaleImageOptions& initialScale = UCScaleImageOptions());
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
    std::shared_ptr<UltraCanvasDropdown> methodDrop;     // scale image only
    std::shared_ptr<UltraCanvasCheckbox> sharpenBox;     // scale image only
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

// ===== PASSWORD PROTECTION =====
// File > Password Protection: the password the image is encrypted with when
// it is saved as an UltraCanvas layered image (*.ucd).
struct UltraPaintPasswordResult {
    bool remove = false;          // take the protection off
    std::string password;         // otherwise: the new password
    bool hidePreview = false;     // and leave the thumbnail out
};

class UltraPaintPasswordDialog : public UltraCanvasWindow {
public:
    // `protectedNow`: the image has a password - the dialog offers to remove
    // it. `available`: this build can encrypt (UCD::EncryptionAvailable()).
    UltraPaintPasswordDialog(bool protectedNow, bool hidePreview, bool available);
    std::function<void(const UltraPaintPasswordResult&)> onAccept;
private:
    void Accept();
    std::shared_ptr<UltraCanvasTextInput> passwordInput, confirmInput;
    std::shared_ptr<UltraCanvasCheckbox> hidePreviewBox;
    std::shared_ptr<UltraCanvasLabel> message;
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
    bool offerMerge = false;      // a document is open: Merge / Open in new tab
    int  canvasWidth = 0;         // the open document, for the fit checkbox
    int  canvasHeight = 0;
    // The rest of a multi-file drop. They follow the same answer, at their
    // own natural size, so they are named in the dialog but not sized by it.
    size_t extraFiles = 0;
};

struct UltraPaintImportResult {
    enum class Action { Cancel, Open, NewTab, Merge };
    Action action = Action::Cancel;
    int  width = 0;               // raster size for a vector source
    int  height = 0;
    int  page = 0;                // 0-based
    bool scaleToFit = false;      // merge: shrink the image into the canvas
    // A password-protected PDF's password, given before the dialog asked
    // anything (the dialog leaves it alone): what rasterizes it.
    std::string password;
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

// ===== IMAGE PICKER =====
// A grid of image thumbnails to choose one from: the start page's
// "Clipboard history" lists the images copied last. A click picks the
// image and closes the window.
struct UltraPaintPickerItem {
    std::string thumbnailPath;   // a PNG; "" shows a generic picture icon
    std::string caption;         // under the thumbnail: "1920 x 1080"
    std::string tooltip;         // "Copied 5 minutes ago"
};

class UltraPaintImagePickerDialog : public UltraCanvasWindow {
public:
    // `fallbackIcon`: what an item without a thumbnail shows.
    UltraPaintImagePickerDialog(const std::string& title, const std::vector<UltraPaintPickerItem>& items,
                                const std::string& fallbackIcon);
    std::function<void(int index)> onPick;
};

// ===== SHARED BUILDING BLOCKS =====
namespace UltraPaintDialogParts {
    // The two-column grid every dialog puts its rows in (UltraCanvasFormLayout):
    // captions share the first column, controls the second, so the controls
    // line up and a longer caption in another language widens the column
    // instead of being cut off.
    std::shared_ptr<UltraCanvasContainer> FormGrid(const std::string& id);
    // A "Label: [widget]" row of such a grid; returns the caption.
    std::shared_ptr<UltraCanvasLabel> FormRow(const std::shared_ptr<UltraCanvasContainer>& grid,
                                              const std::string& id, const std::string& label,
                                              std::shared_ptr<UltraCanvasUIElement> widget);
    // A control that is its own caption (a checkbox), across both columns.
    void FormWideRow(const std::shared_ptr<UltraCanvasContainer>& grid,
                     std::shared_ptr<UltraCanvasUIElement> element);
    // Auto width with a minimum, so a translated button label fits.
    void SizeButtonToText(const std::shared_ptr<UltraCanvasButton>& button);
    // OK / Cancel row; `onOk` runs before the window closes. An empty
    // `okLabel` is "OK" in the person's language.
    std::shared_ptr<UltraCanvasContainer> ButtonRow(const std::string& id, UltraCanvasWindow* win,
                                                    const std::function<void()>& onOk, const std::string& okLabel = {});
    // A blend mode's name as a list shows it, in the person's language
    // (RasterBlendModeName is the English one).
    std::string BlendModeDisplayName(RasterBlendMode mode);
}

} // namespace UltraCanvas
