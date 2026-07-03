// include/Plugins/Documents/Word/UltraCanvasWordDocumentIO.h
// Readers/writers between word-processing file formats and UCRichDocument:
//   .odt  — OpenDocument Text (ODF package)            read + write
//   .docx — Word 2007+ (OOXML/OPC package)             read + write
//   .doc  — legacy Word 97-2003 (OLE2/CFB binary)      detected, not parsed
// Format detection is signature-based (ZIP magic + mimetype /
// [Content_Types].xml probe, CFB magic) so renamed files are classified
// correctly. See Docs/UltraCanvas/ODT-DOCX-Support-Proposal.md.
// Version: 1.0.0
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Documents/Word/UltraCanvasRichDocument.h"

#include <string>

namespace UltraCanvas {

enum class WordDocumentFormat {
    Unknown,
    Odt,        // OpenDocument Text
    Docx,       // Office Open XML WordprocessingML
    LegacyDoc   // Word 97-2003 binary (OLE2/CFB)
};

// Classifies by file content (magic bytes + package probe), falling back to
// the extension when the file cannot be read.
WordDocumentFormat DetectWordDocumentFormat(const std::string& filePath);
// Classifies by extension only ("odt", "docx", "doc"; leading dot allowed).
WordDocumentFormat WordDocumentFormatFromExtension(const std::string& extension);

class UCWordDocumentIO {
public:
    // Auto-detects the format and loads. Returns false with a user-facing
    // reason in outError (legacy .doc reports a clear "convert to .docx"
    // message rather than parsing).
    static bool Load(const std::string& filePath, UCRichDocument& outDocument,
                     std::string& outError);
    static bool LoadOdt(const std::string& filePath, UCRichDocument& outDocument,
                        std::string& outError);
    static bool LoadDocx(const std::string& filePath, UCRichDocument& outDocument,
                         std::string& outError);

    // Picks the writer from the target extension (.odt/.docx).
    static bool Save(const std::string& filePath, const UCRichDocument& document,
                     std::string& outError);
    static bool SaveOdt(const std::string& filePath, const UCRichDocument& document,
                        std::string& outError);
    static bool SaveDocx(const std::string& filePath, const UCRichDocument& document,
                         std::string& outError);
};

} // namespace UltraCanvas
