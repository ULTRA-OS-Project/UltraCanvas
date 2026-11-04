// VideoFX/ultracanvas/VideoFXUltraCanvasText.h
// UltraCanvas's text engine as VideoFX's text renderer: a text overlay that
// names a font family (VideoFXOverlay::fontFamily) is drawn by the
// UltraCanvasRichTextEdit that types it on a frame (UltraCanvasRasterText.h)
// into a picture the engine lays over the frame - fonts by name, bold,
// italic, underline and strikethrough on any word, alignment, line and letter
// spacing, none of which the backend's text filter (FFmpeg drawtext) sets -
// with its shadow and band drawn as the filter drew them. UltraVideo installs
// it, and so does the `videofx` tool when it is built with UltraCanvas, so a
// project renders in the tool exactly as the program shows it.
//
// A library of its own (VideoFXUltraCanvasText) beside VideoFX, which stays
// free of UltraCanvas: an application that does video without UltraCanvas's
// text, or a VideoFX built on its own, never loads the framework.
// Version: 0.1.0 - from UltraVideo's titles (UltraVideoTitles 0.1.0)
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRasterLayer.h"
#include "UltraCanvasRichDocument.h"
#include "VideoFX/VideoFX.h"

#include <memory>
#include <string>

namespace VideoFX {

// Installs the renderer (VideoFX_SetTextRenderer). In a program without an
// UltraCanvasApplication - a command-line tool - it first makes the
// framework's bundled fonts available, so a text set in them (titles'
// default, Ubuntu) looks as it does in the applications.
void VideoFX_UseUltraCanvasText();

// The text engine's points per pixel (96 dpi).
constexpr float kTextOverlayPointsPerPixel = 72.0f / 96.0f;

// Whether the renderer draws an overlay: a text overlay with a fontFamily.
bool IsUltraCanvasTextOverlay(const VideoFXOverlay& overlay);

// A text overlay's words as rich text: its richText, else its plain text, a
// paragraph a line - aligned and spaced as the overlay says, its sizes for a
// frame `frameHeight` pixels high.
std::shared_ptr<UltraCanvas::UCRichDocument> TextOverlayWords(const VideoFXOverlay& overlay, int frameHeight);

// Rich text as an overlay keeps it (VideoFXOverlay::richText, HTML).
std::string TextOverlayWordsToHtml(const UltraCanvas::UCRichDocument& words);

// What UltraCanvas sets an overlay's text in at a frame `frameHeight` pixels
// high: the base font, size, colour and letter spacing, and the words, in a
// box `boxWidth` wide at (0, 0).
UltraCanvas::RasterTextContent TextOverlayContent(const VideoFXOverlay& overlay, int frameHeight, double boxWidth);

// A text overlay drawn for a frame `frameHeight` pixels high: the RGBA
// picture, and where in it the text's box lies (the band and the shadow lie
// around it). Kept: an overlay drawn once for a height is not drawn again
// until something about it changes (the last few dozen are kept).
struct TextOverlayPicture {
    VideoFXFrame image;
    double textX = 0.0, textY = 0.0;
    double textWidth = 0.0, textHeight = 0.0;
};
TextOverlayPicture DrawTextOverlay(const VideoFXOverlay& overlay, int frameHeight);

// Where a picture `pictureW` x `pictureH` lies in a frame `frameW` x `frameH`
// for the overlay's anchor and margin - the engine's rule for an overlay.
UltraCanvas::Rect2Dd OverlayPictureRect(const VideoFXOverlay& overlay, int frameW, int frameH,
                                        int pictureW, int pictureH);

} // namespace VideoFX
