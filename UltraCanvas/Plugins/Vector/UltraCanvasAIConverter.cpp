// UltraCanvas/Plugins/Vector/UltraCanvasAIConverter.cpp
// Adobe Illustrator writer plus the shared export-only converter plumbing -
// see UltraCanvasMetafileConverters.h.
//
// Modern .ai files ARE PDF files: since Illustrator 9 the format is a PDF
// with Illustrator's private editing data attached as an optional stream,
// and any PDF without that stream is still a valid .ai that Illustrator
// (and every PDF consumer) opens. This writer therefore produces the
// Vector plugin's PDF output under the .ai extension. Legacy (v8 and
// older) EPS-based .ai files are recognized by validation but not written.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasMetafileConverters.h"
#include "UltraCanvasVectorStorage.h"

#include <fstream>
#include <sstream>

namespace UltraCanvas {
namespace VectorConverter {

// ===== ExportOnlyConverter (shared plumbing) =====

std::shared_ptr<VectorStorage::VectorDocument> ExportOnlyConverter::Import(
        const std::string& filename, const ConversionOptions& options) {
    // File and stream imports funnel into ImportFromString, so a subclass
    // that grows a reader (DXF, DWG, EMF, WMF) overrides only that method
    // plus CanImport; for pure writers the base ImportFromString warns.
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        if (CanImport() && options.WarningCallback) {
            options.WarningCallback("Cannot open file: " + filename);
        }
        if (!CanImport()) return ImportFromString("", options);   // format warning
        return nullptr;
    }
    return ImportFromStream(file, options);
}

std::shared_ptr<VectorStorage::VectorDocument> ExportOnlyConverter::ImportFromString(
        const std::string& data, const ConversionOptions& options) {
    (void)data;
    if (options.WarningCallback) {
        options.WarningCallback(GetFormatName() + " import is not supported; "
                                "this converter only writes");
    }
    return nullptr;
}

std::shared_ptr<VectorStorage::VectorDocument> ExportOnlyConverter::ImportFromStream(
        std::istream& stream, const ConversionOptions& options) {
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return ImportFromString(buffer.str(), options);
}

bool ExportOnlyConverter::Export(
        const VectorStorage::VectorDocument& document,
        const std::string& filename,
        const ConversionOptions& options) {
    std::string data = ExportToString(document, options);
    if (data.empty()) return false;
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        if (options.WarningCallback) {
            options.WarningCallback("Failed to create file: " + filename);
        }
        return false;
    }
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    return file.good();
}

bool ExportOnlyConverter::ExportToStream(
        const VectorStorage::VectorDocument& document,
        std::ostream& stream,
        const ConversionOptions& options) {
    std::string data = ExportToString(document, options);
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    return stream.good();
}

// ===== AIConverter =====

FormatCapabilities AIConverter::GetCapabilities() const {
    // The output is the PDF writer's, so the capabilities are too.
    return PDFVectorConverter().GetCapabilities();
}

std::string AIConverter::ExportToString(
        const VectorStorage::VectorDocument& document,
        const ConversionOptions& options) {
    PDFVectorConverter pdf;
    return pdf.ExportToString(document, options);
}

bool AIConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    std::string head(8, '\0');
    file.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(file.gcount()));
    return ValidateData(head);
}

bool AIConverter::ValidateData(const std::string& data) const {
    // Modern .ai is PDF-based; legacy (v8-) .ai is EPS/PostScript-based.
    return (data.size() >= 5 && data.compare(0, 5, "%PDF-") == 0) ||
           (data.size() >= 4 && data.compare(0, 4, "%!PS") == 0);
}

} // namespace VectorConverter
} // namespace UltraCanvas
