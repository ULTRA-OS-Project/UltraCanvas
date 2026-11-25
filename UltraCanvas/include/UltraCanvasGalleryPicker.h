// include/UltraCanvasGalleryPicker.h
// Pick one of many choices shown as pictures: tiles in titled sections, each
// a live element drawn at tile size (a chart, a gauge, any element) over its
// title. A click picks a tile, a double click or Enter takes it; the arrow
// keys, Home / End and Page Up / Down move the pick. The gallery is as tall
// as its tiles need (ContentHeightFor) and scrolls in its container: put it
// in CreateScrollableContainer, and a pick moved by the keys is scrolled into
// view.
//
// doc-check: std::shared_ptr<UltraCanvasUIElement> columnChart; std::shared_ptr<UltraCanvasUIElement> pieChart;
// doc-check: example
//     auto gallery = std::make_shared<UltraCanvasGalleryPicker>("ChartTypes", 0, 0, 600, 400);
//     gallery->SetItems({{"Columns", "Compare values", "Column and bar", columnChart},
//                        {"Pie", "Parts of a whole", "Pie and circular", pieChart}});
//     gallery->onSelectionChanged = [](int index) { (void)index; };
//     gallery->onItemActivated = [](int index) { (void)index; };
//
// The pictures are drawn, never sent events: a tile is one target.
//
// Version: 1.0.0 - sections of tiles with live pictures; mouse and keyboard picking
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

struct GalleryPickerItem {
    std::string title;                               // under the picture
    std::string description;                         // the caller shows it (a tooltip, a line under the gallery)
    std::string section;                             // the heading it is shown under; consecutive items share one
    std::shared_ptr<UltraCanvasUIElement> picture;   // drawn in the tile at the tile's picture size; may be null
    bool faded = false;                              // the picture drawn lighter (an example, not the real data)
};

class UltraCanvasGalleryPicker : public UltraCanvasUIElement {
public:
    UltraCanvasGalleryPicker(const std::string& identifier, float x, float y, float w, float h);

    bool AcceptsFocus() const override { return true; }
    AccessibleRole GetAccessibleRole() const override { return AccessibleRole::List; }

    void SetItems(const std::vector<GalleryPickerItem>& items);
    const std::vector<GalleryPickerItem>& GetItems() const { return items; }

    // The picked tile, -1 for none. Setting it does not call onSelectionChanged.
    void SetSelectedIndex(int index);
    int GetSelectedIndex() const { return selected; }

    // A tile's size, its picture's height within it (default 168 x 132, 100).
    void SetTileSize(int width, int height, int pictureHeight);
    int GetTileWidth() const { return tileWidth; }
    int GetTileHeight() const { return tileHeight; }
    int GetPictureHeight() const { return pictureHeight; }

    // How tall the gallery is at `width`: every section with its tiles.
    int ContentHeightFor(int width) const;
    // The tile's box (and the section heading's top) in the element.
    Rect2Di TileBox(int index) const;
    int SectionTop(const std::string& section) const;
    // The tile under a point of the element, -1 for none.
    int TileAt(int x, int y) const;
    // Scrolls the container holding the gallery so the tile is in view.
    void ScrollIntoView(int index);
    // Scrolls the container so the section's heading is at its top.
    void ScrollToSection(const std::string& section);

    std::function<void(int)> onSelectionChanged;     // a click or a key moved the pick
    std::function<void(int)> onItemActivated;        // double click, Enter

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    struct Placed {
        Rect2Di box;
        int row = 0;          // the row of tiles across the whole gallery
        int column = 0;
    };
    std::vector<GalleryPickerItem> items;
    int selected = -1;
    int hovered = -1;
    int tileWidth = 168, tileHeight = 132, pictureHeight = 100;
    int gap = 10, padding = 8, headingHeight = 26;
    mutable std::vector<Placed> placed;
    mutable std::vector<std::pair<std::string, int>> headings;   // section, top
    mutable int placedForWidth = -1;

    void Place(int width) const;
    int ColumnsFor(int width) const;
    void Pick(int index, bool tell);
    int Neighbour(int index, int dx, int dy) const;
};

} // namespace UltraCanvas
