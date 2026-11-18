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
// without it, or with a wrong one, it is refused with a reason. There is no
// writer.
//
// Specification: OASIS OpenDocument 1.3, Part 3 (Schema), §10 (drawing and
// presentation content) and §5-6 (paragraph and list content).
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

} // namespace UltraCanvas
