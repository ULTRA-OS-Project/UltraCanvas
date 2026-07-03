// Plugins/Documents/Word/UltraCanvasWordDocumentIO.cpp
// Format detection and load/save dispatch for word-processing documents.
// The per-format readers/writers live in UltraCanvasOdtFormat.cpp and
// UltraCanvasDocxFormat.cpp.
// Version: 1.0.0
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework

#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"
#include "UltraCanvasWordFormatInternal.h"
#include "UltraCanvasZipPackage.h"

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace UltraCanvas {

WordDocumentFormat WordDocumentFormatFromExtension(const std::string& extension) {
    std::string ext = WordFormatInternal::ToLower(extension);
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    if (ext == "odt") return WordDocumentFormat::Odt;
    if (ext == "docx") return WordDocumentFormat::Docx;
    if (ext == "doc") return WordDocumentFormat::LegacyDoc;
    return WordDocumentFormat::Unknown;
}

WordDocumentFormat DetectWordDocumentFormat(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    uint8_t magic[8] = {};
    if (file.is_open()) {
        file.read(reinterpret_cast<char*>(magic), sizeof(magic));
    }
    if (!file.is_open() || file.gcount() < 4) {
        // Unreadable/short file: the extension is the best remaining signal.
        return WordDocumentFormatFromExtension(
            std::filesystem::path(filePath).extension().string());
    }

    // OLE2 Compound File Binary — the legacy Word 97-2003 container.
    static const uint8_t cfbMagic[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    if (file.gcount() >= 8 && std::equal(cfbMagic, cfbMagic + 8, magic)) {
        return WordDocumentFormat::LegacyDoc;
    }

    // ZIP container: distinguish ODF (mimetype entry) from OPC ([Content_Types].xml).
    if (magic[0] == 'P' && magic[1] == 'K' && magic[2] == 0x03 && magic[3] == 0x04) {
        UCZipPackageReader zip;
        if (zip.Open(filePath)) {
            std::string mimetype;
            if (zip.ReadEntry("mimetype", mimetype)
                && mimetype.find("application/vnd.oasis.opendocument.text") != std::string::npos) {
                return WordDocumentFormat::Odt;
            }
            if (zip.HasEntry("word/document.xml")) {
                return WordDocumentFormat::Docx;
            }
            // Some producers omit the mimetype entry; fall back to content.xml
            // presence for ODF-shaped packages with an .odt extension.
            if (zip.HasEntry("content.xml")
                && WordDocumentFormatFromExtension(
                       std::filesystem::path(filePath).extension().string())
                       == WordDocumentFormat::Odt) {
                return WordDocumentFormat::Odt;
            }
        }
        return WordDocumentFormat::Unknown;
    }

    return WordDocumentFormat::Unknown;
}

bool UCWordDocumentIO::Load(const std::string& filePath, UCRichDocument& outDocument,
                            std::string& outError) {
    outError.clear();
    switch (DetectWordDocumentFormat(filePath)) {
        case WordDocumentFormat::Odt:
            return LoadOdt(filePath, outDocument, outError);
        case WordDocumentFormat::Docx:
            return LoadDocx(filePath, outDocument, outError);
        case WordDocumentFormat::LegacyDoc:
            outError = "This is a legacy Word 97-2003 (.doc) file, which is not "
                       "supported yet. Please open it in a word processor and save "
                       "it as .docx or .odt first.";
            return false;
        default:
            outError = "The file is not a recognized word-processing document "
                       "(.odt or .docx): " + filePath;
            return false;
    }
}

bool UCWordDocumentIO::Save(const std::string& filePath, const UCRichDocument& document,
                            std::string& outError) {
    outError.clear();
    switch (WordDocumentFormatFromExtension(
        std::filesystem::path(filePath).extension().string())) {
        case WordDocumentFormat::Odt:
            return SaveOdt(filePath, document, outError);
        case WordDocumentFormat::Docx:
            return SaveDocx(filePath, document, outError);
        case WordDocumentFormat::LegacyDoc:
            outError = "Saving to the legacy .doc format is not supported. "
                       "Save as .docx instead.";
            return false;
        default:
            outError = "Unsupported document extension for saving: " + filePath;
            return false;
    }
}

} // namespace UltraCanvas
