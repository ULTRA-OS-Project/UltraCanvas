// include/UltraCanvasPresentationPpt.h
// PowerPoint 97-2003 presentations - the binary .ppt, and the same format
// as a show (.pps) and a template (.pot) - read into the deck model every
// presentation reader fills (UltraCanvasPresentationPptx.h), so the slide
// editor, the show, the page views, PDF export and the outline work on them
// as on a .pptx or an .odp; saving writes .pptx or .odp, there is no .ppt
// writer.
//
// A .ppt is a compound file (UltraCanvasCompoundFile.h). The reader follows
// the "Current User" stream to the newest UserEditAtom in the "PowerPoint
// Document" stream and walks the chain of edits back, so a deck saved
// incrementally ("fast save") reads as its last edit: each persist
// directory, newest first, maps the objects to their records. From the
// DocumentContainer it reads the slide size (576 master units per inch, kept
// in EMU), the font collection, the document's text defaults, the picture
// store and the hyperlinks, and the three SlideListWithText lists - masters,
// slides, notes - with the outline text PowerPoint keeps there.
//
// Each main master becomes a PptxMaster (its placeholders, background and
// colour scheme) and its text styles (TxMasterStyleAtom: title, body by
// level, notes, other, and the centred / half / quarter body variants) are
// what a run inherits; a title master becomes the master's "title" layout,
// and the other layouts are made from the slide layouts (SlideAtom geom) the
// slides use. Each slide's drawing (OfficeArt, read through
// UltraCanvasOfficeArt.h, shared with the .doc reader) gives its shapes:
// place (converted to EMU, a group's members mapped from its coordinate
// space), rotation (the anchor of one turned by 45-135 degrees is stored
// turned) and mirroring, the shape type as a DrawingML preset, a freeform's
// outline, fill (solid, a shade as a gradient), outline (colour, width,
// dash, arrowheads), text insets, anchor, wrap and direction, pictures (PNG,
// JPEG, EMF, WMF from the BStore and the "Pictures" stream, with their crop
// and description), and placeholders (OEPlaceholderAtom) as the model's
// title, subtitle and content kinds - a placeholder with no place of its own
// takes its master's. A group marked as a table (tableProperties) is read as
// a table: its cells' grid from their places, their text, fills and the
// border lines around them.
//
// Text (TextHeaderAtom, TextCharsAtom / TextBytesAtom, or the outline text
// an OutlineTextRefAtom points at) keeps its paragraphs - level, alignment,
// spacing, margin and indent (StyleTextPropAtom, else the TextRulerAtom),
// bullet with its character, font, colour and size, numbering from the
// PowerPoint 2000 extension (StyleTextProp9Atom) - and its runs' bold,
// italic, underline, size, colour (a scheme index resolved through the
// slide's colour scheme), typeface (the font collection), super- and
// subscript, and hyperlinks (TextInteractiveInfoAtom to ExHyperlink: a URL,
// or "#slideN" for a jump to a slide). What a run does not say comes from
// its master's text style for its text type and level, then the document's.
// Each slide also keeps its notes (the notes page's body text), whether it
// is hidden, its background colour, and its transition (SSSlideInfoAtom:
// the effect as the model's kind - one the model has none for as Fade - the
// speed as the length, and the automatic advance).
//
// What is not read: animations (beyond the transition), sounds, embedded
// OLE objects and charts (their picture is drawn when the file keeps one),
// comments, picture and pattern fills (their colour is kept), shadows and
// other effects, WordArt, the handout, and VBA. A password-protected deck
// (an encrypted "PowerPoint Document", CryptSession10Container) is refused
// with a reason: its RC4 CryptoAPI encryption is not read.
//
// The reader trusts no length: every record is bounds-checked against the
// stream and its container, nesting and loops are capped, and a damaged
// file gives an error or what could be read, never a crash or a hang.
//
// Specification: [MS-PPT] PowerPoint (.ppt) Binary File Format; [MS-ODRAW]
// Office Drawing Binary File Format; [MS-CFB].
// Version: 1.0.0 - ReadPptPresentation / ReadPptPresentationFromMemory, IsPptPresentationFile, IsPptExtension
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasPresentationPptx.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

// True for a PowerPoint 97-2003 deck by its content: a compound file holding
// a "PowerPoint Document" stream. `filePath` is UTF-8.
bool IsPptPresentationFile(const std::string& filePath);
// True for the extensions a PowerPoint 97-2003 deck goes by (ppt, pps, pot),
// with or without a dot, any case.
bool IsPptExtension(const std::string& extensionOrPath);

// Reads a deck; `filePath` is UTF-8. False with a reason in `error` a user
// can be shown: not a compound file, no "PowerPoint Document" stream, a
// password-protected deck, or one too damaged to find its document.
// `withPictures` also keeps the pictures' bytes (PptxPresentation::pictures,
// by PptxShape::pictureTarget, "ppt/media/imageN.png" ...), for drawing the
// slides and for saving them as .pptx or .odp.
bool ReadPptPresentation(const std::string& filePath, PptxPresentation& out, std::string& error,
                         bool withPictures = false);
// The same for a compound file already in memory.
bool ReadPptPresentationFromMemory(std::vector<uint8_t> data, PptxPresentation& out, std::string& error,
                                   bool withPictures = false);

} // namespace UltraCanvas
