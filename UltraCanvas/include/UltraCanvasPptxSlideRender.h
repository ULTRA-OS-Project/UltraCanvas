// include/UltraCanvasPptxSlideRender.h
// A PowerPoint slide drawn as a picture, from a deck read by
// UltraCanvasPresentationPptx (read it withPictures for the pictures): the
// background, the shapes back to front - turned and mirrored, in their preset
// or custom outlines, solid or gradient fills, solid, dashed or dotted
// outlines with arrowheads; connectors; pictures cropped and clipped to their
// outline; tables in their cells' fills and borders, merged cells spanning;
// charts through the chart engine (DrawRichDocumentChart); and text in its
// boxes' insets with its fonts, sizes, colours, alignment, spacing, indents,
// bullet characters and numbering, strike, super- and subscript, capitals,
// highlight and links, at its anchor, shrunk to fit where it would overflow,
// vertical text turned. What the reader leaves out is not drawn (effects,
// pattern and picture fills). UltraClipboard's previews, UltraViewer's
// slides, the Filer's deck thumbnails.
//
//   PptxPresentation deck;
//   std::string error;
//   if (ReadPptxPresentation(path, deck, error, "", true))
//       auto first = RenderPptxSlide(deck, 0, 1280);   // 1280 x 720 for 16:9
//
// Version: 1.1.0 - outlines, turn, gradients, dashes, arrowheads, connectors, crops, styled and merged
//                  tables, charts, and the text's fonts, spacing, indents, bullets and effects
// Version: 1.0.0
// Last Modified: 2026-11-20
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
