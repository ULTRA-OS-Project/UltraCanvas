// Apps/ArtCreator/ArtCreatorIcon.h
// Icon mode: drawing an icon for the size it is shown at. An icon is drawn
// once, at 16, 24 or 256 pixels, and is sharp there only where its straight
// edges land on whole pixels. Icon mode gives the page a pixel grid of that
// size; this file holds what it shows of the result - the page drawn at the
// icon's size, the pixels the drawing only partly covers (the soft, grey
// ones a shape half a pixel off leaves), and the panel element that shows
// both.
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasImage.h"
#include "DataFormats/UltraCanvasVectorStorage.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// The icon sizes ArtCreator offers, in pixels: menu and toolbar sizes up to
// the 256 px of a macOS Dock or taskbar icon.
const std::vector<int>& ArtIconSizes();

// The page drawn at the icon's size, and where it is not sharp.
struct ArtIconRaster {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;             // straight RGBA, 4 bytes a pixel, row by row
    std::vector<Point2Di> softPixels;      // pixels the drawing covers by more than 10 % and less than 90 %
    int coveredPixels = 0;                 // pixels it covers at all
    std::string error;                     // why nothing was drawn
};

// Draws the page of `document` at `width` x `height` pixels, as File >
// Export writes it at that size, and finds its soft pixels.
ArtIconRaster RasterizeIcon(const VectorStorage::VectorDocument& document, int width, int height);

// The icon at its real size on a light and a dark ground and, where the
// panel has room, enlarged with every soft pixel outlined in red - what
// "sharp" means, at a glance. Its height follows the icon's size.
class ArtIconPreview : public UltraCanvasUIElement {
public:
    ArtIconPreview(const std::string& id, float width);

    void SetRaster(std::shared_ptr<const ArtIconRaster> raster);
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

    // How many times the enlarged view magnifies an icon `size` pixels wide
    // in a panel `width` wide, at most 128 px tall: up to 8, 0 when it would
    // not be at least 2 (from 96 px the page itself is the enlarged view).
    static int Magnification(int size, float width);

private:
    void Layout();

    std::shared_ptr<const ArtIconRaster> raster;
    std::shared_ptr<UCPixmap> actual;      // the icon, premultiplied, 1:1
    std::shared_ptr<UCPixmap> enlarged;    // nearest-neighbour, on white, soft pixels outlined
    float panelWidth = 0;
    int magnification = 0;
    bool sideBySide = false;               // the light and dark real-size views in one row
};

} // namespace UltraCanvas
