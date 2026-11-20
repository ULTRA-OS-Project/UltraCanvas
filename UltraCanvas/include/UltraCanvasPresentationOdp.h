// include/UltraCanvasPresentationOdp.h
// OpenDocument presentations - Impress decks (.odp), their templates (.otp)
// and the flat single-XML form (.fodp) - read into the same UI-free model as a
// PowerPoint deck (UltraCanvasPresentationPptx.h): the slides in order, their
// titles, the text of every placeholder, text box and shape with its list
// levels, bullets and numbering, bold / italic / underline, tables, the
// pictures' parts and descriptions, speaker notes, and which slides are
// hidden. PptxPresentationToRichDocument turns the result into the same
// outline, and UltraCanvasFileLoader::LoadTextDocument opens a deck that way.
//
// Positions are kept in EMU like a .pptx's (svg:x / svg:y / svg:width /
// svg:height converted), on the page: ODF places a grouped shape on the page,
// not in its group's coordinates.
//
// What is not read: the layout as drawn (master pages, styles beyond bold /
// italic / underline, fills), charts and other embedded objects, comments,
// animations and transitions. A password-protected deck - LibreOffice's "Save
// with password" - opens with its password (UltraCanvasOdfEncryption.h);
// without it, or with a wrong one, it is refused with a reason.
//
// WriteOdpPresentation writes the model - a deck read from an .odp or a
// .pptx, or laid out from a document - as an Impress deck LibreOffice and
// this reader open.
//
// Specification: OASIS OpenDocument 1.3, Part 3 (Schema), §10 (drawing and
// presentation content) and §5-6 (paragraph and list content).
// Version: 1.2.0 - WriteOdpPresentation / WriteOdpPresentationToMemory (.odp, .otp, .fodp); charts read
// Version: 1.1.0 - ReadOdpPresentation / ReadOdpPresentationFromMemory take a password
// Version: 1.0.0 - ReadOdpPresentation / ReadOdpPresentationFromMemory, IsOdpPresentationFile, IsOdpExtension
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasPresentationPptx.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

// True for an OpenDocument presentation by its content: a ZIP package whose
// mimetype (or manifest) says application/vnd.oasis.opendocument.presentation
// (or -template), or a flat XML document whose office:mimetype says so.
bool IsOdpPresentationFile(const std::string& filePath);
// True for the extensions an OpenDocument presentation goes by (odp, otp,
// fodp), with or without a dot, any case.
bool IsOdpExtension(const std::string& extensionOrPath);

// Reads a deck; `filePath` is UTF-8. `password` opens a password-protected
// one. False with a reason in `error` a user can be shown - a protected deck
// without its password, or with a wrong one, says so.
bool ReadOdpPresentation(const std::string& filePath, PptxPresentation& out, std::string& error,
                         const std::string& password = std::string());
// The same for a deck already in memory: the ZIP package, or a flat .fodp's XML.
bool ReadOdpPresentationFromMemory(std::vector<uint8_t> data, PptxPresentation& out, std::string& error,
                                   const std::string& password = std::string());

// ----- Writing -----
// Writes `deck` as an OpenDocument presentation to `filePath` (UTF-8): the
// slides in order on one master page of the deck's size, each slide's
// background and hidden flag (a drawing-page style), its shapes - titles,
// subtitles and content as presentation frames (title, subtitle, outline),
// text boxes as frames, other shapes as custom shapes (rectangles, ellipses
// and rounded rectangles by their ODF type, every other preset and a custom
// outline as an enhanced path), turned and mirrored, with their fill (solid,
// or a two-colour gradient from the first and last stop), outline (dash,
// arrowheads as markers), text insets, anchor and wrap; connectors as lines
// or paths; pictures (from `deck.pictures`, not cropped); tables with merged
// cells, fills, borders, padding and anchors; charts as chart objects
// (UltraCanvasOdfChart); speaker notes. Text keeps its paragraphs'
// alignment, spacing, margins, bullets (as list styles: the character, a
// symbol font's code as the Unicode character, colour, size) and numbering
// (format, punctuation, the number reached), and its runs' size, colour,
// typeface, bold, italic, underline, strike, super- and subscript, capitals,
// highlight and links ("#slideN" as a jump to that page). The extension
// names the form: .odp, .otp (a template) or .fodp (one XML file, pictures
// inline). No password: ODF encryption is read, not written. False with a
// reason in `error` a user can be shown.
bool WriteOdpPresentation(const PptxPresentation& deck, const std::string& filePath, std::string& error);
// The same in memory: the package, or a flat file's XML for "fodp".
bool WriteOdpPresentationToMemory(const PptxPresentation& deck, std::vector<uint8_t>& out, std::string& error,
                                  const std::string& extension = "odp");

} // namespace UltraCanvas
