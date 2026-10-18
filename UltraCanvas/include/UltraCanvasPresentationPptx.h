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
// What is not read: the layout as drawn (masters, themes, fills, fonts and
// sizes, animations, transitions), charts, SmartArt and embedded objects.
// Shape positions are kept as the slide gives them - a shape that inherits its
// place from the layout has none. There is no writer.
//
// Specification: ECMA-376 Part 1, PresentationML (§19) and DrawingML (§20-21).
// Version: 1.0.0
// Last Modified: 2026-10-18
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRichDocument.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

struct PptxRun {
    std::string text;            // UTF-8; "\n" for a line break within the paragraph
    bool bold = false;
    bool italic = false;
    bool underline = false;
};

struct PptxParagraph {
    std::vector<PptxRun> runs;
    int level = 0;               // 0-based outline level (a:pPr lvl)
    bool bullet = false;         // drawn with a bullet or a number
    bool numbered = false;       // a:buAutoNum
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
    // Position and size in EMU (914400 per inch, 12700 per point), as the
    // slide gives them; all 0 when the shape takes its place from the layout.
    // Inside a group they are in the group's own coordinates.
    int64_t x = 0, y = 0, width = 0, height = 0;
    std::vector<PptxParagraph> paragraphs;                   // the shape's text
    std::vector<std::vector<std::vector<PptxParagraph>>> rows;   // Table: rows, cells, paragraphs
    std::string pictureTarget;   // Picture: the package part, e.g. "ppt/media/image1.png"
    std::string altText;         // Picture: its description
};

struct PptxSlide {
    int number = 0;              // 1-based, in presentation order
    bool hidden = false;         // left out of the slide show
    std::string title;           // the title placeholder's text
    std::vector<PptxShape> shapes;           // in z-order, back to front
    std::vector<PptxParagraph> notes;        // the speaker notes
};

struct PptxPresentation {
    int64_t slideWidth = 0, slideHeight = 0; // EMU (16:9 is 12192000 x 6858000)
    std::vector<PptxSlide> slides;
};

// True for a ZIP package holding ppt/presentation.xml. A password-protected
// deck is a compound file, not a ZIP: IsEncryptedOfficePackage tells that one.
bool IsPptxPackage(const std::string& filePath);
// True for the extensions a PowerPoint package goes by (pptx, pptm, ppsx,
// ppsm, potx, potm), with or without a dot, any case.
bool IsPptxExtension(const std::string& extensionOrPath);

// Reads a deck; `filePath` is UTF-8. `password` opens a password-protected
// one. False with a reason in `error` a user can be shown.
bool ReadPptxPresentation(const std::string& filePath, PptxPresentation& out, std::string& error,
                          const std::string& password = std::string());
// The same for a package (the ZIP) already in memory.
bool ReadPptxPresentationFromPackage(std::vector<uint8_t> package, PptxPresentation& out,
                                     std::string& error);

// The deck as an outline: per slide a heading (its number and title), its
// text - bulleted paragraphs as list items at their level, the rest as
// paragraphs - its tables, a line for each picture, and the speaker notes as
// a block quote; slides separated by a rule. Hidden slides are included and
// marked "(hidden)".
UCRichDocument PptxPresentationToRichDocument(const PptxPresentation& presentation);

} // namespace UltraCanvas
