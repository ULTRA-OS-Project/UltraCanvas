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
// Version: 1.4.0 - PptxSlideDrawOptions::background: the shapes alone, over what is drawn already
// Version: 1.3.0 - PptxBulletText: a paragraph's bullet as drawn
// Version: 1.2.0 - DrawPptxSlide: a slide on any render context, into a rectangle, with prompt text for
//                  empty placeholders and a shape's text left to an editor
// Version: 1.1.0 - outlines, turn, gradients, dashes, arrowheads, connectors, crops, styled and merged
//                  tables, charts, and the text's fonts, spacing, indents, bullets and effects
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasImage.h"
#include "UltraCanvasPresentationPptx.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {

class IRenderContext;

// How DrawPptxSlide draws a slide for an editor.
struct PptxSlideDrawOptions {
    // The grey text an empty placeholder shows ("Click to add title");
    // empty: none. Called with the shape and its index on the slide.
    std::function<std::string(const PptxShape&, size_t)> promptText;
    // A shape whose text is not drawn (an editor shows it in place); -1: none.
    int hideTextOfShape = -1;
    // The same for one cell of a table: shape, row, column; -1: none.
    int hideTextOfCellShape = -1, hideTextOfCellRow = -1, hideTextOfCellColumn = -1;
    // false: the shapes alone, over what the context holds (an editor's
    // faint copy of a picture's cropped-away parts).
    bool background = true;
};

// Paragraph `p`'s bullet as it is drawn, in Unicode: its character through
// its typeface (Wingdings' "§" is a square), or PowerPoint's dot or dash.
std::string PptxBulletText(const PptxParagraph& p);

// Slide `slide` of `deck` drawn on `ctx` into `rect` (the context's units),
// scaled to its width: the slide's background fills the rectangle, nothing
// outside it is touched. The editor, the thumbnails, the show and PDF
// export draw with it.
void DrawPptxSlide(IRenderContext* ctx, const PptxPresentation& deck, const PptxSlide& slide, const Rect2Dd& rect,
                   const PptxSlideDrawOptions& options = PptxSlideDrawOptions());

// Slide `index` (0-based, in presentation order) `widthPx` pixels wide, as
// high as the deck's slide shape makes it (16:9 when the deck gives none).
// Null when there is no such slide or nothing to draw on. Callable from a
// worker: the drawing is serialized inside.
std::shared_ptr<UCPixmap> RenderPptxSlide(const PptxPresentation& deck, size_t index, int widthPx);

} // namespace UltraCanvas
