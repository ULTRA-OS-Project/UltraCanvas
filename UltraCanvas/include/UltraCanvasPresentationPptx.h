// include/UltraCanvasPresentationPptx.h
// PowerPoint presentations (.pptx, and .pptm / .ppsx / .potx, the same
// package): the slides' text read into a UI-free model - titles, the text of
// every placeholder and text box with its paragraph levels and bullets,
// tables, the pictures' names and alternative text, speaker notes, and which
// slides are hidden. PptxPresentationToRichDocument turns it into an outline
// for the document views; UltraCanvasFileLoader::LoadTextDocument opens a
// deck that way.
//
// A password-protected deck is decrypted on the way in
// (UltraCanvasOfficeEncryption.h), like a protected .xlsx or .docx.
//
// Markup Compatibility's mc:AlternateContent is read - around shapes and
// around runs - through its first Choice that gives anything, else its
// Fallback; an equation (a14:m) gives its text.
//
// For drawing a slide (UltraCanvasPptxSlideRender.h) the reader also resolves
// what a shape takes from its slide layout and slide master: its place (a
// placeholder that names none takes the layout's, then the master's), the
// size, colour and alignment of its text (the run, then the layout's and the
// master's list styles, then the master's text styles), its vertical anchor
// and autofit scale, a solid fill and outline, and the slide's background - a
// theme colour (schemeClr) resolved through the deck's theme.
//
// What is not read: theme fonts, gradients and pictures as fills, effects,
// shape geometry beyond rectangles, rounded rectangles and ellipses,
// animations, transitions, charts, SmartArt and embedded objects. There is no
// writer.
//
// Specification: ECMA-376 Part 1, PresentationML (§19) and DrawingML (§20-21).
// Version: 1.2.0 - mc:AlternateContent (Choice, else Fallback) around shapes and runs; equations
//                  (a14:m) read as their text
// Version: 1.1.0 - what a slide is drawn with: places inherited from the layout and master,
//                  text sizes, colours and alignment, anchors, fills, outlines, the background,
//                  the theme's colours, and (withPictures) the pictures' bytes
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRichDocument.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace UltraCanvas {

struct PptxRun {
    std::string text;            // UTF-8; "\n" for a line break within the paragraph
    bool bold = false;
    bool italic = false;
    bool underline = false;
    float sizePt = 0;            // resolved: the run's, else its list style's, else the master's; 0: unknown
    std::string color;           // resolved "RRGGBB"; empty: the slide's default text colour
};

enum class PptxAlign { Left, Center, Right, Justify };
enum class PptxAnchor { Top, Middle, Bottom };

struct PptxParagraph {
    std::vector<PptxRun> runs;
    int level = 0;               // 0-based outline level (a:pPr lvl)
    bool bullet = false;         // drawn with a bullet or a number
    bool numbered = false;       // a:buAutoNum
    PptxAlign align = PptxAlign::Left;   // resolved as the runs' size is
    std::string Text() const;    // the runs' text joined
};

enum class PptxShapeKind {
    Title,       // the title placeholder (also a title slide's centred title)
    Subtitle,
    Body,        // a content placeholder: bulleted unless it says otherwise
    Text,        // a text box or any other shape with text
    Table,
    Picture
};

struct PptxShape {
    PptxShapeKind kind = PptxShapeKind::Text;
    std::string name;            // as the deck names it ("Title 1", "TextBox 3")
    // Position and size in EMU (914400 per inch, 12700 per point): the
    // slide's, or the layout's or master's for a placeholder that names none
    // (placeInherited); all 0 when none of them gives one. A shape in a
    // group is placed on the slide, the group's scaling applied.
    int64_t x = 0, y = 0, width = 0, height = 0;
    bool placeInherited = false;
    std::string placeholder;     // the placeholder's type ("title", "body", "subTitle" ...); empty: none
    PptxAnchor anchor = PptxAnchor::Top;   // where the text sits in the box
    float fontScale = 1.0f;      // a:normAutofit fontScale: the text shrunk to fit
    std::string fill;            // solid fill "RRGGBB"; empty: none
    std::string line;            // outline colour "RRGGBB"; empty: none
    double lineWidthPt = 0;
    std::string geometry;        // a:prstGeom ("rect", "roundRect", "ellipse" ...); empty: rect
    std::vector<PptxParagraph> paragraphs;                   // the shape's text
    std::vector<std::vector<std::vector<PptxParagraph>>> rows;   // Table: rows, cells, paragraphs
    std::vector<int64_t> columnWidths;   // Table: EMU, from its grid
    std::vector<int64_t> rowHeights;     // Table: EMU, as each row asks (the text may need more)
    std::string pictureTarget;   // Picture: the package part, e.g. "ppt/media/image1.png"
    std::string altText;         // Picture: its description
};

struct PptxSlide {
    int number = 0;              // 1-based, in presentation order
    bool hidden = false;         // left out of the slide show
    std::string title;           // the title placeholder's text
    std::vector<PptxShape> shapes;           // in z-order, back to front
    std::vector<PptxParagraph> notes;        // the speaker notes
    std::string background;      // "RRGGBB" from the slide, its layout or its master; empty: white
    std::string textColor;       // the theme's dark text colour (tx1); empty: black
};

struct PptxPresentation {
    int64_t slideWidth = 0, slideHeight = 0; // EMU (16:9 is 12192000 x 6858000)
    // The theme's colours by name ("dk1", "lt1", "dk2", "lt2", "accent1" ...
    // "accent6", "hlink"), "RRGGBB".
    std::map<std::string, std::string> themeColors;
    std::vector<PptxSlide> slides;
    // The pictures' bytes by package part (PptxShape::pictureTarget), when
    // read withPictures; empty otherwise.
    std::map<std::string, std::vector<uint8_t>> pictures;
};

// True for a ZIP package holding ppt/presentation.xml. A password-protected
// deck is a compound file, not a ZIP: IsEncryptedOfficePackage tells that one.
bool IsPptxPackage(const std::string& filePath);
// True for the extensions a PowerPoint package goes by (pptx, pptm, ppsx,
// ppsm, potx, potm), with or without a dot, any case.
bool IsPptxExtension(const std::string& extensionOrPath);

// Reads a deck; `filePath` is UTF-8. `password` opens a password-protected
// one. False with a reason in `error` a user can be shown.
// `withPictures` also keeps the pictures' bytes (PptxPresentation::pictures),
// for drawing the slides.
bool ReadPptxPresentation(const std::string& filePath, PptxPresentation& out, std::string& error,
                          const std::string& password = std::string(), bool withPictures = false);
// The same for a package (the ZIP) already in memory.
bool ReadPptxPresentationFromPackage(std::vector<uint8_t> package, PptxPresentation& out,
                                     std::string& error, bool withPictures = false);

// The deck as an outline: per slide a heading (its number and title), its
// text - bulleted paragraphs as list items at their level, the rest as
// paragraphs - its tables, a line for each picture, and the speaker notes as
// a block quote; slides separated by a rule. Hidden slides are included and
// marked "(hidden)".
UCRichDocument PptxPresentationToRichDocument(const PptxPresentation& presentation);

} // namespace UltraCanvas
