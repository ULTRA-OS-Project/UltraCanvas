// include/UltraCanvasRasterText.h
// Text layers of a raster document: words a bitmap editor's Text tool sets on
// the canvas and keeps as words - a rich document in a box
// (RasterTextContent, UltraCanvasRasterLayer.h) - until the layer is merged
// or rasterized. This header draws them into the layer's pixels and readies
// the element that edits them on the canvas.
//
// Both go through UltraCanvasRichTextEdit, the framework's WYSIWYG element,
// with one style (TextLayerEditStyle): the renderer draws the element at
// 1:1 into an offscreen context, and the editor on the canvas is the same
// element at the canvas's zoom. The element lays text out in its own
// document space and draws a zoom as a scale, so the editor shows the words
// exactly where, how big and how wrapped the layer will hold them - at any
// zoom - and committing an edit changes nothing on the screen.
//
// UI-free in the sense the document is: no window is needed, the element is
// only used to lay out and draw. Rendering is the host's call; the document
// (UCRasterDocument) never renders text itself.
// Version: 1.0.0
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRasterLayer.h"
#include "UltraCanvasRichTextEdit.h"

#include <memory>
#include <string>

namespace UltraCanvas {

// The look a text layer is drawn with: no background, border, padding or
// scrolling, and the content's font, size and colour for the runs that have
// none of their own. The renderer uses it, and an editor given it
// (PrepareTextLayerEditor) lays the words out exactly as the layer has them.
RichTextEditStyle TextLayerEditStyle(const RasterTextContent& content);

// Readies `editor` to edit `content` on a canvas shown at `zoom` (view
// pixels per canvas pixel): the style above, no spell checking, a zoom range
// wide enough for any canvas zoom, and that zoom. It does not set the
// document: the host gives the editor its own copy of content.document. Place
// the editor over the box in view coordinates (the box times `zoom`) and keep
// it at least GetContentHeight() * zoom tall, so it never scrolls.
void PrepareTextLayerEditor(UltraCanvasRichTextEdit& editor, const RasterTextContent& content, double zoom);

// Draws `content` into a new text layer of `canvasWidth` x `canvasHeight`:
// transparent but for the text, its `text` the content with `height` set to
// what the text took, named after its first line. The box's corner is drawn
// at whole pixels (x and y rounded). The layer's document is a copy with the
// paragraph spacing stated - the gaps it was laid out with, written out - so
// it reads back from a file (an ODT, a .ucraster project) laid out the same.
// Null with `error` set when there is no document, the box has no width or
// the canvas no size.
std::shared_ptr<UCRasterLayer> RenderTextLayer(const RasterTextContent& content,
                                               int canvasWidth, int canvasHeight,
                                               std::string& error);

// `content` drawn into a layer that takes over `layer`'s attributes - its
// name, visibility, lock, opacity and blend mode - for a text layer whose text
// was edited (UCRasterDocument::ReplaceLayer puts it in). Null with `error`
// set as for RenderTextLayer.
std::shared_ptr<UCRasterLayer> RenderTextLayerLike(const UCRasterLayer& layer,
                                                   const RasterTextContent& content,
                                                   std::string& error);

// The text's first line, shortened to `maxCharacters` with an ellipsis - a
// text layer's name in a layer list. "Text" for a document with no words.
std::string TextLayerName(const UCRichDocument& document, size_t maxCharacters = 24);

// True when the document holds no characters at all (an edit that typed
// nothing, or deleted everything) - a text layer that would draw nothing.
bool IsTextLayerDocumentEmpty(const UCRichDocument& document);

} // namespace UltraCanvas
