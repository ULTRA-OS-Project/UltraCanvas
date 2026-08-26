// include/UltraCanvasColorSwatchBar.h
// A strip of colour swatches: one click picks a colour. Built for the places
// where a full colour picker is too much furniture — the row of backdrop
// colours under a transparent image in the media viewer, a quick fill palette
// in an editor, a highlight colour in a toolbar.
//
// The bar sizes its swatches to the width (and height) it is given: each one
// grows towards `swatchSize` and shrinks towards `minSwatchSize` so a full
// palette still fits a narrow preview pane, and the row is centred in whatever
// space is left over. That is why this is an element rather than a row of
// buttons — a fixed row of buttons cannot adapt, and a palette that overflows
// its pane is a palette with colours nobody can reach.
//
// An optional leading "checkered" swatch stands for the transparency pattern
// instead of a colour, for the common case of choosing what shows through a
// transparent image: a colour, or the checkerboard that means "no colour".
//
// Version: 1.0.0
// Last Modified: 2026-08-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== SWATCH BAR STYLE =====
struct ColorSwatchBarStyle {
    Color background     = Color(0, 0, 0, 0);          // transparent by default
    Color border         = Color(90, 90, 96, 255);     // around every swatch
    Color hoverBorder    = Color(200, 200, 208, 255);
    Color selectedBorder = Color(70, 140, 220, 255);
    // The checkerboard drawn in the "checkered" swatch (see SetShowCheckeredEntry).
    Color checkerLight   = Color(255, 255, 255, 255);
    Color checkerDark    = Color(203, 203, 203, 255);

    float swatchSize     = 20.0f;   // preferred edge length of one swatch
    // The floor the swatches shrink to while the row still fits. A strip too
    // narrow even for that keeps shrinking them (and closes the gaps) rather
    // than pushing colours off its end.
    float minSwatchSize  = 8.0f;
    float spacing        = 3.0f;    // gap between swatches
    float padding        = 4.0f;    // gap to the element's edges
    float borderWidth    = 1.0f;
    float selectedBorderWidth = 2.0f;
    float cornerRadius   = 2.0f;
    float checkerCell    = 4.0f;    // checkerboard square size
};

// ===== COLOUR SWATCH BAR =====
class UltraCanvasColorSwatchBar : public UltraCanvasUIElement {
public:
    // ===== CONSTRUCTORS (REQUIRED PATTERN) =====
    UltraCanvasColorSwatchBar(const std::string& identifier,
                              float x, float y, float w, float h);
    UltraCanvasColorSwatchBar(const std::string& identifier, float w, float h)
        : UltraCanvasColorSwatchBar(identifier, -1, -1, w, h) {}
    explicit UltraCanvasColorSwatchBar(const std::string& identifier)
        : UltraCanvasColorSwatchBar(identifier, -1, -1, -1, -1) {}

    // ===== THE COLOURS =====
    // Replacing the palette clears the selection unless the previously selected
    // colour is in the new one.
    void SetColors(const std::vector<Color>& colors);
    const std::vector<Color>& GetColors() const { return colors_; }
    int  GetColorCount() const { return static_cast<int>(colors_.size()); }

    // A leading swatch showing the transparency checkerboard rather than a
    // colour. It selects like any other swatch but reports through
    // onCheckeredSelected, so a host can switch to "no backdrop colour".
    void SetShowCheckeredEntry(bool show);
    bool GetShowCheckeredEntry() const { return showCheckered_; }

    // ===== READY-MADE PALETTES =====
    // `steps` shades from white to black, evenly spaced (>= 2).
    static std::vector<Color> GrayscalePalette(int steps = 6);
    // A spectrum of saturated colours, red through magenta.
    static std::vector<Color> ColorPalette();
    // Grayscale followed by the colours — the palette a backdrop chooser wants.
    static std::vector<Color> DefaultPalette();

    // ===== SELECTION =====
    // Selects the swatch holding `c` (ignoring alpha); a colour that is not in
    // the palette clears the selection instead. runCallback fires
    // onColorSelected, as a click does.
    void SelectColor(const Color& c, bool runCallback = false);
    void SelectCheckered(bool runCallback = false);
    void ClearSelection();
    bool  IsCheckeredSelected() const { return checkeredSelected_; }
    bool  HasColorSelected() const { return selectedIndex_ >= 0; }
    int   GetSelectedIndex() const { return selectedIndex_; }   // -1 == none
    // The selected colour; black when nothing (or the checkerboard) is selected —
    // ask HasColorSelected() first.
    Color GetSelectedColor() const;

    // ===== APPEARANCE =====
    void SetStyle(const ColorSwatchBarStyle& s) { style_ = s; RequestRedraw(); }
    const ColorSwatchBarStyle& GetStyle() const { return style_; }
    ColorSwatchBarStyle& GetStyle() { return style_; }
    void SetSwatchSize(float size);
    // Width the bar would like: every swatch at its preferred size.
    float GetPreferredWidth() const;
    // The edge length the swatches are drawn at right now (after fitting them
    // into the element's own width and height).
    float GetEffectiveSwatchSize() const;

    // ===== CALLBACKS =====
    std::function<void(const Color&)> onColorSelected;   // a colour swatch was clicked
    std::function<void()>             onCheckeredSelected;

    // ===== OVERRIDES =====
    // A pointer control: it never takes the keyboard, so hosts that use the
    // arrow keys for their own navigation (the media viewer browses files with
    // them) keep them.
    bool AcceptsFocus() const override { return false; }
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    // Slots are the drawn cells: slot 0 is the checkerboard when it is shown,
    // and the colours follow. ColorIndexOfSlot() maps back to `colors_`.
    int   SlotCount() const;
    int   ColorIndexOfSlot(int slot) const;
    bool  IsCheckeredSlot(int slot) const { return showCheckered_ && slot == 0; }
    float SpacingForBounds() const;
    float SwatchSizeForBounds() const;
    Rect2Df SwatchRect(int slot) const;          // element-local
    int   SlotAt(const Point2Di& local) const;   // -1 when between/outside
    void  DrawCheckerboard(IRenderContext* ctx, const Rect2Df& r) const;
    // "#RRGGBB" for the swatch under the pointer, shown as the tooltip.
    static std::string HexOf(const Color& c);

    std::vector<Color>  colors_;
    ColorSwatchBarStyle style_;
    bool showCheckered_    = false;
    int  selectedIndex_    = -1;      // into colors_, -1 == none
    bool checkeredSelected_ = false;
    int  hoverSlot_        = -1;
};

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasColorSwatchBar> CreateColorSwatchBar(
        const std::string& identifier, float x, float y, float w, float h,
        const std::vector<Color>& colors = UltraCanvasColorSwatchBar::DefaultPalette()) {
    auto bar = std::make_shared<UltraCanvasColorSwatchBar>(identifier, x, y, w, h);
    bar->SetColors(colors);
    return bar;
}

// A backdrop chooser: the checkerboard entry first, then the default palette.
inline std::shared_ptr<UltraCanvasColorSwatchBar> CreateBackdropSwatchBar(
        const std::string& identifier, float x, float y, float w, float h) {
    auto bar = std::make_shared<UltraCanvasColorSwatchBar>(identifier, x, y, w, h);
    bar->SetColors(UltraCanvasColorSwatchBar::DefaultPalette());
    bar->SetShowCheckeredEntry(true);
    return bar;
}

} // namespace UltraCanvas
