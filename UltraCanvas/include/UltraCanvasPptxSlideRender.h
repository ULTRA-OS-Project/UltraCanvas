// include/UltraCanvasPptxSlideRender.h
// A PowerPoint slide drawn as a picture, from a deck read by
// UltraCanvasPresentationPptx (read it withPictures for the pictures): the
// background, the shapes back to front - rectangles, rounded rectangles and
// ellipses with their fills and outlines, pictures, tables, and text in its
// boxes with its sizes, colours, alignment, bullets and anchor, shrunk to fit
// where it would overflow. For previews (UltraClipboard's): what the
// reader leaves out is not drawn (theme fonts, gradients, charts, SmartArt),
// and the text is set in the system's font.
//
//   PptxPresentation deck;
//   std::string error;
//   if (ReadPptxPresentation(path, deck, error, "", true))
//       auto first = RenderPptxSlide(deck, 0, 1280);   // 1280 x 720 for 16:9
//
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasImage.h"
#include "UltraCanvasPresentationPptx.h"

#include <cstddef>
#include <memory>

namespace UltraCanvas {

// Slide `index` (0-based, in presentation order) `widthPx` pixels wide, as
// high as the deck's slide shape makes it (16:9 when the deck gives none).
// Null when there is no such slide or nothing to draw on. Callable from a
// worker: the drawing is serialized inside.
std::shared_ptr<UCPixmap> RenderPptxSlide(const PptxPresentation& deck, size_t index, int widthPx);

} // namespace UltraCanvas
