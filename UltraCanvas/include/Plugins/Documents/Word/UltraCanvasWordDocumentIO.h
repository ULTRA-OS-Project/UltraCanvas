// include/Plugins/Documents/Word/UltraCanvasWordDocumentIO.h
// Readers/writers between word-processing file formats and UCRichDocument:
//   .odt  — OpenDocument Text (ODF package)            read + write
//   .docx — Word 2007+ (OOXML/OPC package)             read + write
//   .doc  — legacy Word 97-2003 (OLE2/CFB binary)      read (with formatting)
//   .tex  — LaTeX document subset (article class)      read only
// Format detection is signature-based (ZIP magic + mimetype /
// [Content_Types].xml probe, CFB magic, \documentclass head) so renamed
// files are classified correctly. See Docs/UltraCanvas/ODT-DOCX-Support-Proposal.md
// and Docs/UltraCanvas/UltraCanvasLaTeXDocumentReader.md.
// Version: 1.4.0
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRichDocument.h"

#include <string>

namespace UltraCanvas {

enum class WordDocumentFormat {
    Unknown,
    Odt,        // OpenDocument Text
    Docx,       // Office Open XML WordprocessingML
    LegacyDoc,  // Word 97-2003 binary (OLE2/CFB)
    LaTeX       // .tex source, document subset (UltraCanvasLaTeXDocumentReader)
};

// Classifies by file content (magic bytes + package probe), falling back to
// the extension when the file cannot be read.
WordDocumentFormat DetectWordDocumentFormat(const std::string& filePath);
// Classifies by extension only ("odt", "docx", "doc", "tex"; leading dot allowed).
WordDocumentFormat WordDocumentFormatFromExtension(const std::string& extension);

class UCWordDocumentIO {
public:
    // Auto-detects the format and loads. Returns false with a user-facing
    // reason in outError. When a legacy .doc cannot be read, the error
    // suggests converting it to .docx.
    //
    // `password` opens a password-protected .docx: the ECMA-376 agile
    // encryption Word 2010 and later write (UltraCanvasOfficeEncryption.h).
    // Such a file is a compound file, so DetectWordDocumentFormat calls it
    // LegacyDoc; Load and LoadDocx recognise it and decrypt it. Without the
    // password it fails with a message saying one is needed;
    // UltraCanvasFileLoader::RequestDocumentPassword asks the user for it.
    static bool Load(const std::string& filePath, UCRichDocument& outDocument,
                     std::string& outError, const std::string& password = std::string());
    static bool LoadOdt(const std::string& filePath, UCRichDocument& outDocument,
                        std::string& outError);
    static bool LoadDocx(const std::string& filePath, UCRichDocument& outDocument,
                         std::string& outError, const std::string& password = std::string());
    // Word 97-2003 binary (CFB container, piece table, FKPs, stylesheet,
    // list tables): headings, character formatting, alignment, bullet and
    // numbered lists, tables, hyperlinks and PNG/JPEG pictures. Headers,
    // footers, footnotes and floating drawings are not read. There is
    // deliberately no .doc writer; save as .docx or .odt instead.
    static bool LoadDoc(const std::string& filePath, UCRichDocument& outDocument,
                        std::string& outError);
    // Former name of LoadDoc, from when the import was text-only.
    [[deprecated("use LoadDoc")]]
    static bool LoadDocText(const std::string& filePath, UCRichDocument& outDocument,
                            std::string& outError);
    // LaTeX (.tex): the article document subset — sections, lists, tables,
    // images, footnotes, references and formulas (kept as LaTeX for the math
    // engine). Unsupported commands degrade to their text; the reader's
    // diagnostics are available through UltraCanvasLaTeXDocumentReader.
    // There is no LaTeX writer.
    static bool LoadLaTeX(const std::string& filePath, UCRichDocument& outDocument,
                          std::string& outError);

    // Picks the writer from the target extension (.odt/.docx).
    // `password` saves a password-protected .docx (agile encryption,
    // AES-256 / SHA-512, what Word 2013 and later write); any other format
    // with a password is refused rather than saved unprotected.
    static bool Save(const std::string& filePath, const UCRichDocument& document,
                     std::string& outError, const std::string& password = std::string());
    static bool SaveOdt(const std::string& filePath, const UCRichDocument& document,
                        std::string& outError);
    static bool SaveDocx(const std::string& filePath, const UCRichDocument& document,
                         std::string& outError, const std::string& password = std::string());

private:
    // Load's format dispatch, before the shared post-processing.
    static bool LoadByFormat(const std::string& filePath, UCRichDocument& outDocument,
                             std::string& outError, const std::string& password);
};

} // namespace UltraCanvas
