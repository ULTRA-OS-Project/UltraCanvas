// include/UltraCanvasPresentationOdp.h
// OpenDocument presentations - Impress decks (.odp), their templates (.otp)
// and the flat single-XML form (.fodp) - read into the same UI-free model as a
// PowerPoint deck (UltraCanvasPresentationPptx.h): the slides in order, their
// titles, the text of every placeholder, text box and shape with its list
// levels, bullets and numbering, bold / italic / underline, tables, the
// pictures' parts and descriptions, charts, speaker notes, and which slides
// are hidden. PptxPresentationToRichDocument turns the result into the same
// outline, and UltraCanvasFileLoader::LoadTextDocument opens a deck that way.
//
// Positions are kept in EMU like a .pptx's (svg:x / svg:y / svg:width /
// svg:height converted), on the page: ODF places a grouped shape on the page,
// not in its group's coordinates.
//
// For drawing a slide (UltraCanvasPptxSlideRender.h) the reader also resolves
// the look as drawn, each style through its parents and the family defaults
// (common styles, the content's automatic ones, the master pages' own):
// a shape's turn (draw:transform) and mirroring, its outline - Office presets
// LibreOffice names "ooxml-...", ODF's own shape types (rectangle, ellipse,
// diamond, arrows, stars, flowchart shapes ...) as the preset they draw, an
// enhanced path in plain numbers as a custom outline; draw:rect, ellipse,
// polygon, polyline, path and regular-polygon; lines and connectors - its fill
// (solid, or a gradient's stops: linear, axial, radial), outline (colour,
// width, dashes, arrowheads from markers), text area (anchor, padding, wrap,
// vertical writing); text with its runs' size (points or a share), colour,
// typeface (through the font-face declarations), bold, italic, underline,
// strike, super- and subscript, capitals, highlight and links ("#name" of a
// page as a jump to its slide), its paragraphs' alignment, spacing, line
// height and margins, and its lists' labels - the bullet character (a symbol
// font's private-use code as its code), font, colour and size, the numbering
// scheme and the number each item shows, and where the label hangs; an
// outline's levels take their presentation styles ("...-outline2"); tables'
// column widths, row heights, cells' fills, borders, padding, anchors and
// spans, a table template applied; each slide's background (its page style,
// else its master page's) and the theme's colours (LibreOffice's loext:theme).
// With `withPictures` the pictures' bytes are kept, inline ones of a flat
// file too.
//
// The design: each master page (style:master-page) is a PptxMaster - its
// name, background and placeholder frames (title, subtitle, outline, date,
// footer, number; an empty one as Impress leaves it gets Impress's prompt) -
// and each presentation page layout a PptxLayout on the master page its
// slides use it with (one per master when slides use it on several), its
// placeholders where the page layout puts them with the master's look, named
// and typed from what they hold ("Title and Content", "obj") unless the file
// names it; a deck with no page layouts gets one layout per master page, and
// a slide on none a blank one. Each slide's layoutIndex follows its
// presentation:presentation-page-layout-name and master page. A slide's
// transition is read from its drawing-page style (smil:type, the speed,
// presentation:transition-type automatic with presentation:duration) and the
// page's anim:transitionFilter (the exact length). An empty title, subtitle
// or outline frame - or one showing its prompt (presentation:placeholder) - is
// kept as an empty placeholder, one empty paragraph in its look.
//
// What is not read: master pages' shapes other than placeholders, shadows and
// other effects, bitmap and hatch fills (a hatch on a solid ground keeps the
// ground), outlines built from formulas (an Office preset still draws by its
// name), picture crops, embedded objects other than charts, comments,
// animations, a transition's direction, and "advance only after the time"
// (ODF advances on a click as well). A password-protected deck - LibreOffice's "Save with password"
// - opens with its password (UltraCanvasOdfEncryption.h); without it, or with
// a wrong one, it is refused with a reason.
//
// WriteOdpPresentation writes the model - a deck read from an .odp or a
// .pptx, or laid out from a document - as an Impress deck LibreOffice and
// this reader open, with its masters as master pages and its layouts as
// presentation page layouts, and the transitions.
//
// Specification: OASIS OpenDocument 1.3, Part 3 (Schema), §10 (drawing and
// presentation content) and §5-6 (paragraph and list content).
// Version: 1.4.0 - masters (master pages), layouts (presentation page layouts) and transitions,
//                  read and written
// Version: 1.3.0 - the look as drawn: fills, outlines, fonts, spacing, bullets, tables' cells,
//                  backgrounds and turns; `withPictures` on ReadOdpPresentation / ...FromMemory
// Version: 1.2.0 - WriteOdpPresentation / WriteOdpPresentationToMemory (.odp, .otp, .fodp); charts read
// Version: 1.1.0 - ReadOdpPresentation / ReadOdpPresentationFromMemory take a password
// Version: 1.0.0 - ReadOdpPresentation / ReadOdpPresentationFromMemory, IsOdpPresentationFile, IsOdpExtension
// Last Modified: 2026-11-25
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
// without its password, or with a wrong one, says so. `withPictures` also
// keeps the pictures' bytes (PptxPresentation::pictures, by
// PptxShape::pictureTarget), for drawing the slides.
bool ReadOdpPresentation(const std::string& filePath, PptxPresentation& out, std::string& error,
                         const std::string& password = std::string(), bool withPictures = false);
// The same for a deck already in memory: the ZIP package, or a flat .fodp's XML.
bool ReadOdpPresentationFromMemory(std::vector<uint8_t> data, PptxPresentation& out, std::string& error,
                                   const std::string& password = std::string(), bool withPictures = false);

// ----- Writing -----
// Writes `deck` as an OpenDocument presentation to `filePath` (UTF-8): the
// deck's masters as master pages of the deck's size (name, background,
// placeholder frames, and "<master>-title" / "-subtitle" / "-outline1..9"
// presentation styles with their look; one plain master page for a deck
// without masters), its layouts as presentation page layouts ("UCL1" ...,
// shown by the layout's name), the slides in order each on its layout
// (-1: the first) and that layout's master, each slide's background, hidden
// flag and transition (a drawing-page style: smil type and subtype, speed,
// automatic advance; the exact length as the page's anim:transitionFilter),
// its shapes - titles,
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
