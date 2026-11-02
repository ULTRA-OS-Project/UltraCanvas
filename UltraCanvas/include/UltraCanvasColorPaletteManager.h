// include/UltraCanvasColorPaletteManager.h
// The colour palettes under a colour picker: one or more rows of swatches,
// each a palette, with a small button at the end of the rows to add more.
//
//  - With one palette its row ends in a "+" button. With several, every row
//    ends in a "-" that takes that palette away, and the last row has the
//    "+" under its "-".
//  - "+" opens the palette manager: a popup with a title bar ("Palette
//    manager") it is dragged about by, listing ready-made schemes - a row
//    of basic colours up to a whole 256-colour GIF palette - grouped by
//    kind, each with a preview and its own "+" that adds it as a new row.
//  - "Add palette" at its foot opens the palette creator, a second such
//    window: a name, a picker and the colours added so far. Create keeps the
//    palette among the user's own (the manager's "My palettes" section, with
//    a button to delete it) and adds it as a row.
//  - A left click on a swatch picks its colour (onColorSelected). A right
//    click opens a small foreground-only colour picker next to it that sets
//    that one entry of the palette, live; Revert puts it back.
//  - The pointer on a swatch floods the rows' background with its colour
//    and reports it (onColorHovered), so the host can flood its colour
//    picker too (UltraCanvasColorPicker::SetSurfacePreview) - as hovering
//    the picker's own swatches does.
//
// Large palettes wrap (UltraCanvasColorSwatchBar::SetWrap), so a GIF palette
// is a block of small swatches rather than a hairline. The element sets its
// own height to its rows' (like a colour picker with SetAutoHeight), so in a
// flex column - a toolbox card - what is below it moves as rows come and go.
//
// The palettes are the host's to keep: onPalettesChanged fires after every
// change of the rows and onUserPalettesChanged after one of the user's own,
// and SerializePalettes / ParsePalettes turn either into one line of a
// settings file and back.
//
// Built from framework elements only: the rows are UltraCanvasColorSwatchBar,
// the buttons UltraCanvasButton, the captions UltraCanvasLabel, the name an
// UltraCanvasTextInput, the pickers UltraCanvasColorPicker
// (SetShowBackgroundColor(false)).
//
// Version: 1.1.0 - the gallery is the "Palette manager" with a title bar to
//                  drag it by; "Add palette" and the user's own palettes;
//                  the hovered swatch floods the rows (onColorHovered); the
//                  small presets have 16 colours, a full row; Game Boy,
//                  PICO-8 and VGA 16 are gone
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasColorSwatchBar.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasCommonTypes.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraCanvasButton;
class UltraCanvasColorPaletteGallery;
class UltraCanvasColorPaletteEntryEditor;
class UltraCanvasColorPaletteCreator;

// ===== A PALETTE =====
struct NamedColorPalette {
    std::string name;        // shown in the gallery and as the row's tooltip
    std::string category;    // the gallery's group ("Basics", "Retro", ...)
    std::vector<Color> colors;
};

// ===== STYLE =====
struct ColorPaletteManagerStyle {
    // The rows' swatches. The manager sizes them per row - the more colours a
    // palette has, the smaller and closer its swatches - from these limits.
    Color swatchBorder         = Color(200, 202, 208, 255);
    Color swatchHoverBorder    = Color(120, 124, 132, 255);
    Color swatchSelectedBorder = Color(37, 99, 235, 255);
    float swatchSize           = 16.0f;   // the largest a swatch gets

    float rowGap        = 4.0f;     // between two palettes
    float buttonSize    = 14.0f;    // the "+" / "-" micro buttons
    float buttonGap     = 4.0f;     // between the swatches and the buttons

    // The micro buttons, and the gallery's and editor's popups.
    Color buttonColor      = Color(243, 244, 246, 255);
    Color buttonHoverColor = Color(229, 231, 235, 255);
    Color buttonBorder     = Color(209, 213, 219, 255);
    Color buttonText       = Color(55, 65, 81, 255);
    Color accentColor      = Color(37, 99, 235, 255);   // the gallery's add buttons
    Color accentHoverColor = Color(29, 78, 216, 255);
    Color popupBackground  = Color(255, 255, 255, 255);
    Color popupBorder      = Color(0, 0, 0, 40);
    Color cardBackground   = Color(246, 247, 249, 255);
    Color cardBorder       = Color(229, 231, 235, 255);
    Color titleText        = Color(17, 24, 39, 255);
    Color mutedText        = Color(107, 114, 128, 255);
    float popupRadius      = 8.0f;
    float fontSize         = 11.0f;

    // The entry editor's picker; the host's own picker style fits best.
    ColorPickerStyle editorPickerStyle = ColorPickerStyle::Light();
    float editorPickerScale = 0.78f;
};

// ===== THE MANAGER =====
class UltraCanvasColorPaletteManager : public UltraCanvasContainer {
public:
    UltraCanvasColorPaletteManager(const std::string& identifier,
                                   float x, float y, float w, float h);
    UltraCanvasColorPaletteManager(const std::string& identifier, float w, float h)
        : UltraCanvasColorPaletteManager(identifier, -1, -1, w, h) {}
    ~UltraCanvasColorPaletteManager() override;

    // ===== THE PALETTES =====
    // Never empty: setting no palettes, or taking the last one away, leaves
    // the default one (DefaultPalette()).
    void SetPalettes(const std::vector<NamedColorPalette>& palettes);
    const std::vector<NamedColorPalette>& GetPalettes() const { return palettes_; }
    int  GetPaletteCount() const { return static_cast<int>(palettes_.size()); }
    // Adds a row at the end; fires onPalettesChanged.
    void AddPalette(const NamedColorPalette& palette);
    // Takes row `index` away (not the last remaining one); fires onPalettesChanged.
    void RemovePalette(int index);
    // Whether a palette of this name is one of the rows.
    bool HasPalette(const std::string& name) const;
    // Sets one entry; `notify` fires onPalettesChanged.
    void SetEntryColor(int palette, int entry, const Color& c, bool notify = true);

    // ===== THE USER'S OWN PALETTES =====
    // Made with the palette creator ("Add palette"), offered in the palette
    // manager under "My palettes" and kept by the host as the rows are. No
    // callback from SetUserPalettes.
    void SetUserPalettes(const std::vector<NamedColorPalette>& palettes);
    const std::vector<NamedColorPalette>& GetUserPalettes() const { return userPalettes_; }
    // Keeps `palette` among the user's own under a name nothing else has
    // (UniquePaletteName) and adds it as a row; the name it was kept under.
    // Fires onUserPalettesChanged and onPalettesChanged.
    std::string AddUserPalette(const NamedColorPalette& palette);
    // Forgets one of the user's own; a row of it stays. Fires onUserPalettesChanged.
    void RemoveUserPalette(const std::string& name);
    // `wanted` (or "Palette" when it is blank), else "wanted 2", "wanted 3"
    // ... - the first that no preset, row or palette of the user's has.
    std::string UniquePaletteName(const std::string& wanted) const;

    // ===== SELECTION =====
    // Marks the first swatch holding `c` (alpha ignored), in whichever row it
    // is; a colour in none of them clears the mark. No callback.
    void SelectColor(const Color& c);
    void ClearSelection();

    // ===== POPUPS =====
    // What the "+" button and a right click open; a host can open them too.
    // `anchorWindow` is the rectangle (window coordinates) the popup is put
    // next to: below it where there is room, above it otherwise. Each has a
    // title bar the user drags it about the window by.
    void OpenPaletteGallery(const Rect2Df& anchorWindow);
    void OpenEntryEditor(int palette, int entry, const Point2Di& windowPos);
    // The palette creator - what the palette manager's "Add palette" opens.
    void OpenPaletteCreator(const Rect2Df& anchorWindow);
    void ClosePopups();

    // ===== APPEARANCE =====
    void SetManagerStyle(const ColorPaletteManagerStyle& s);
    const ColorPaletteManagerStyle& GetManagerStyle() const { return style_; }

    // The height the rows take at `width`.
    float HeightForWidth(float width) const;

    // ===== CALLBACKS =====
    std::function<void(const Color&)> onColorSelected;   // a swatch was clicked
    std::function<void()>             onPalettesChanged; // added, removed, an entry set
    std::function<void()>             onUserPalettesChanged; // one made or deleted
    // The pointer came onto a swatch of a row (its colour), and later left
    // the rows: for a host flooding its picker with the colour meanwhile.
    std::function<void(const Color&)> onColorHovered;
    std::function<void()>             onColorHoverEnded;

    // ===== READY-MADE PALETTES =====
    // The row a manager starts with: six greys and twelve colours.
    static NamedColorPalette DefaultPalette();
    // What the gallery offers, in its order: basics, moods, design systems,
    // retro machines and the indexed (GIF) palettes up to 256 colours.
    static const std::vector<NamedColorPalette>& PresetPalettes();
    // The category of the user's own palettes, the gallery's first section.
    static const char* UserPaletteCategory() { return "My palettes"; }

    // ===== PERSISTENCE =====
    // One line for a settings file: "Name|RRGGBB,RRGGBB,...;Name|...". A
    // '|' or ';' in a name is dropped.
    static std::string SerializePalettes(const std::vector<NamedColorPalette>& palettes);
    // False, and `out` left alone, when the text holds no palette. A preset
    // saved with fewer colours than it has now (the presets grew to fill
    // their rows) comes back whole, unless an entry of it was changed.
    static bool ParsePalettes(const std::string& text, std::vector<NamedColorPalette>& out);

    // ===== OVERRIDES =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

private:
    friend class UltraCanvasColorPaletteGallery;
    friend class UltraCanvasColorPaletteEntryEditor;
    friend class UltraCanvasColorPaletteCreator;

    struct Row {
        std::shared_ptr<UltraCanvasContainer>      row;
        std::shared_ptr<UltraCanvasColorSwatchBar> bar;
        std::shared_ptr<UltraCanvasButton>         remove;   // with more than one palette
        std::shared_ptr<UltraCanvasButton>         add;      // on the last row
    };

    void Rebuild();
    void FitHeight();
    void StyleSwatchBar(UltraCanvasColorSwatchBar& bar, size_t colourCount) const;
    std::shared_ptr<UltraCanvasButton> MakeMicroButton(const std::string& id,
                                                       const std::string& text,
                                                       const std::string& tooltip);
    float BarWidth(float width) const;
    void  SelectInRow(int row, int entry);
    void  HoverInRow(int row, int entry);
    void  NotifyChanged();
    void  OpenCreatorAt(const Point2Di& windowPos);

    std::vector<NamedColorPalette> palettes_;
    std::vector<Row> rows_;
    // Widgets taken out of the element while one of them may still be in
    // its own click handler (the "-" of the row it removes); let go of at
    // the next rebuild.
    std::vector<std::shared_ptr<UltraCanvasUIElement>> retired_;
    ColorPaletteManagerStyle style_;
    float builtWidth_ = -1.0f;
    int selectedRow_ = -1;
    int selectedEntry_ = -1;
    // The swatch under the pointer: its row (-1: none) and colour, which
    // floods the rows' background.
    int hoverRow_ = -1;
    Color hoverColor_;
    std::vector<NamedColorPalette> userPalettes_;

    std::shared_ptr<UltraCanvasColorPaletteGallery> gallery_;
    std::shared_ptr<UltraCanvasColorPaletteEntryEditor> editor_;
    std::shared_ptr<UltraCanvasColorPaletteCreator> creator_;
};

// ===== FACTORY =====
inline std::shared_ptr<UltraCanvasColorPaletteManager> CreateColorPaletteManager(
        const std::string& identifier, float x, float y, float w) {
    return std::make_shared<UltraCanvasColorPaletteManager>(identifier, x, y, w, 24);
}

} // namespace UltraCanvas
