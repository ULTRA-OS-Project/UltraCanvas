// Apps/UltraVideo/UltraVideoTitles.h
// A title drawn by UltraCanvas's text engine - the UltraCanvasRichTextEdit
// that types it on the frame (UltraCanvasRasterText.h) - into a picture the
// video engine lays over the frame (an image overlay), so the viewer, the
// export and the words being typed agree to the pixel: fonts by name, bold,
// italic, underline and strikethrough on any word, alignment, line and letter
// spacing - none of which the engine's own text filter (FFmpeg drawtext) can
// set. The shadow and the band behind the text are drawn into the picture as
// the text filter drew them.
//
// A saved project still carries each title as a plain text overlay as well,
// so `videofx render` renders it without UltraCanvas; UltraVideo reads the
// titles back from its own list (Document::titles) with their formatting.
// Version: 0.1.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework
#pragma once

#include "UltraVideoDocument.h"

#include "UltraCanvasRasterLayer.h"
#include "UltraCanvasRichDocument.h"
#include "VideoFX/VideoFX.h"

#include <memory>
#include <string>

namespace UltraVideo {

// The text engine's points per pixel (96 dpi).
constexpr float kTitlePointsPerPixel = 72.0f / 96.0f;

// A title's words as rich text: what was typed on the frame (TitleSpan::words),
// else its plain text, a paragraph a line - aligned and spaced as the title
// says, its sizes for a frame `frameHeight` pixels high.
std::shared_ptr<UltraCanvas::UCRichDocument> TitleWords(const TitleSpan& title, int frameHeight);

// Rich text as a title keeps it (HTML), and its plain text (TitleSpan::look.text).
std::string TitleWordsToHtml(const UltraCanvas::UCRichDocument& words);

// What UltraCanvas sets a title in at a frame `frameHeight` pixels high: the
// base font, size, colour and letter spacing, and the words, in a box
// `boxWidth` wide at (0, 0).
UltraCanvas::RasterTextContent TitleContent(const TitleSpan& title, int frameHeight, double boxWidth);

// A title drawn for a frame `frameHeight` pixels high: the RGBA picture, and
// where in it the text's box lies (the band and the shadow lie around it).
struct TitlePicture {
    VideoFX::VideoFXFrame image;
    double textX = 0.0, textY = 0.0;
    double textWidth = 0.0, textHeight = 0.0;
};
TitlePicture RenderTitle(const TitleSpan& title, int frameHeight);
// The same, kept: a title drawn once for a height is not drawn again until
// something about it changes (the last few dozen are kept).
TitlePicture CachedTitlePicture(const TitleSpan& title, int frameHeight);

// Where a picture `pictureW` x `pictureH` lies in a frame `frameW` x `frameH`
// for the title's anchor and margin - the engine's rule for an overlay.
UltraCanvas::Rect2Dd TitlePictureRect(const TitleSpan& title, int frameW, int frameH, int pictureW, int pictureH);

} // namespace UltraVideo
