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
// placeholder that names none takes the layout's, then the master's), its
// text's size, colour, font, alignment, spacing, indents and bullets (the
// run, then the layout's and the master's list styles, then the master's text
// styles; the theme's heading and body fonts), its vertical anchor, insets and
// autofit scale, its fill (solid or gradient), outline (colour, width, dash,
// arrowheads), outline shape (a preset or a custom path), rotation and
// mirroring, and the slide's background - a theme colour (schemeClr)
// resolved through the deck's theme. Connectors are read as lines; a table's
// cells keep their merges, fills and borders, its style (ppt/tableStyles.xml)
// applied; a picture keeps its crop. A chart (c:chartSpace) is read through
// UltraCanvasDrawingML into a RichChart; SmartArt is read from the drawing
// PowerPoint saves beside it, as ordinary shapes. Runs keep their hyperlinks.
//
// What is not read: effects (shadows, glow, 3D), pattern and picture fills,
// animations, transitions, comments, embedded objects (beyond the picture
// they show), video and audio (beyond the poster frame). There is no writer.
//
// Specification: ECMA-376 Part 1, PresentationML (§19) and DrawingML (§20-21).
// Version: 1.3.0 - what a slide looks like: fonts, spacing, indents, bullet characters and numbering
//                  schemes, insets, rotation and mirroring, preset and custom outlines, gradients,
//                  dashes and arrowheads, connectors, merged and styled table cells, picture crops,
//                  hyperlinks, charts (UltraCanvasDrawingML) and SmartArt drawings
// Version: 1.2.0 - mc:AlternateContent (Choice, else Fallback) around shapes and runs; equations
//                  (a14:m) read as their text
// Version: 1.1.0 - what a slide is drawn with: places inherited from the layout and master,
//                  text sizes, colours and alignment, anchors, fills, outlines, the background,
//                  the theme's colours, and (withPictures) the pictures' bytes
// Version: 1.0.0
// Last Modified: 2026-11-20
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
    std::string font;            // resolved typeface (the theme's for +mj-lt / +mn-lt); empty: the system's
    bool strike = false;
    int baseline = 0;            // per cent of the size: > 0 superscript, < 0 subscript
    bool allCaps = false, smallCaps = false;
    std::string highlight;       // "RRGGBB" behind the text; empty: none
    std::string hyperlink;       // the link's target (a URL, or "#slide3" for a jump to a slide)
};

enum class PptxAlign { Left, Center, Right, Justify };

// Paragraph spacing: either a share of the line (1.0 = single) or points.
struct PptxSpacing {
    float percent = 0;           // of a line; 0: not said
    float points = 0;            // exact points; 0: not said
    bool IsSet() const { return percent > 0 || points > 0; }
};
enum class PptxAnchor { Top, Middle, Bottom };

struct PptxParagraph {
    std::vector<PptxRun> runs;
    int level = 0;               // 0-based outline level (a:pPr lvl)
    bool bullet = false;         // drawn with a bullet or a number
    bool numbered = false;       // a:buAutoNum
    PptxAlign align = PptxAlign::Left;   // resolved as the runs' size is
    // Resolved like the alignment; unset spacings take PowerPoint's own.
    PptxSpacing spaceBefore, spaceAfter, lineSpacing;
    int64_t marginLeft = -1;     // EMU, the text's left edge (a:pPr marL); -1: not said
    int64_t indent = 0;          // EMU, the first line's (and bullet's) offset from it, usually negative
    std::string bulletChar;      // a:buChar's character ("" with bullet: PowerPoint's default dot)
    std::string bulletFont;      // its typeface ("Wingdings", "Arial" ...)
    std::string bulletColor;     // "RRGGBB"; empty: the text's
    float bulletSizePercent = 0; // of the text's size; 0: the same
    std::string numberScheme;    // a:buAutoNum type ("arabicPeriod", "alphaLcParenR", "romanUcPeriod" ...)
    int numberStartAt = 1;
    std::string Text() const;    // the runs' text joined
};

// The number PowerPoint shows for item `number` (1-based) of an
// a:buAutoNum scheme, with its punctuation: "3.", "c)", "(iv)", "IV.".
std::string PptxNumberLabel(const std::string& scheme, int number);

enum class PptxShapeKind {
    Title,       // the title placeholder (also a title slide's centred title)
    Subtitle,
    Body,        // a content placeholder: bulleted unless it says otherwise
    Text,        // a text box or any other shape with text
    Table,
    Picture,
    Connector,   // a line or connector (p:cxnSp): its box runs from start to end, mirrored as flipped
    Chart        // a chart (PptxShape::chart)
};

// A table cell's look, after the table's style and the cell's own: its
// fill, its four borders and how it spans.
struct PptxCellStyle {
    std::string fill;            // "RRGGBB"; empty: none
    std::string textColor;       // from the table style ("RRGGBB"); empty: the runs'
    bool bold = false;           // from the table style (the header row's)
    struct Border {
        std::string color;       // "RRGGBB"; empty: none
        double widthPt = 0;
    };
    Border left, right, top, bottom;
    int gridSpan = 1, rowSpan = 1;
    bool mergedAway = false;     // covered by a cell to its left or above (hMerge / vMerge)
    int64_t insetLeft = 91440, insetRight = 91440, insetTop = 45720, insetBottom = 45720;   // EMU
    PptxAnchor anchor = PptxAnchor::Top;
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
    // a:custGeom: its outline, each point 0..1 across and down the box (its
    // first sub-path's coordinate space); empty: the preset.
    std::vector<RichShape::PathPoint> customPath;
    double rotationDeg = 0;      // clockwise about the box's centre
    bool flipH = false, flipV = false;
    // A gradient fill (`fill` holds its first colour): its stops ("RRGGBB")
    // and its angle (0: left to right, 90: top to bottom), or round from the centre.
    std::vector<RichShape::GradientStop> gradientStops;
    double gradientAngleDeg = 0;
    bool gradientRadial = false;
    RichShape::Dash dash = RichShape::Dash::Solid;
    bool arrowStart = false, arrowEnd = false;   // on a line's or connector's ends
    // The text's insets from the box (a:bodyPr lIns ...), EMU; -1: PowerPoint's own (0.1", 0.05").
    int64_t insetLeft = -1, insetRight = -1, insetTop = -1, insetBottom = -1;
    bool wrap = true;            // a:bodyPr wrap="none": one line, as wide as it needs
    int textRotation = 0;        // a:bodyPr vert: 90 (vert, eaVert), 270 (vert270); 0: across
    std::vector<PptxParagraph> paragraphs;                   // the shape's text
    std::vector<std::vector<std::vector<PptxParagraph>>> rows;   // Table: rows, cells, paragraphs
    std::vector<int64_t> columnWidths;   // Table: EMU, from its grid
    std::vector<int64_t> rowHeights;     // Table: EMU, as each row asks (the text may need more)
    std::vector<std::vector<PptxCellStyle>> cellStyles;   // Table: each cell's, as `rows`
    std::string pictureTarget;   // Picture: the package part, e.g. "ppt/media/image1.png"
    std::string altText;         // Picture: its description
    // Picture: what its a:srcRect crops away from each side, a share of the picture (0..1).
    double cropLeft = 0, cropTop = 0, cropRight = 0, cropBottom = 0;
    RichChart chart;             // Chart: its data (UltraCanvasDrawingML)
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
    // The theme's heading and body typefaces (a:majorFont / a:minorFont's latin).
    std::string majorFont, minorFont;
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
