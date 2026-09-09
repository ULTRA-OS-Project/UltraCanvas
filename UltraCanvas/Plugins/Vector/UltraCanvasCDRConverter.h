// UltraCanvas/Plugins/Vector/UltraCanvasCDRConverter.h
// CDR (CorelDRAW) writer for the Vector plugin: serializes a
// VectorStorage::VectorDocument as a version-7 RIFF CDR file.
//
// Write-only. Reading CDR is the CDR plugin's job
// (Plugins/Vector/CDR/UltraCanvasCDRPlugin.h, built on libcdr), so
// CanImport() is false here and the Import methods return null.
//
// CorelDRAW's format has no public specification; the writer targets the
// record layouts libcdr's parser (the reference open-source reader, and the
// engine behind this framework's own CDR plugin) consumes for version 700:
// 32-bit coordinates in 1/254000 inch, page-centred with Y up, objects in
// reverse z-order, geometry as line-and-curve loda chunks, solid fills and
// outline styles as referenced fild/outl definitions. Gradients fall back
// to the blend of their end stops, text and bitmaps are skipped — each
// reported through the warning callback.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasVectorConverter.h"

namespace UltraCanvas {
    namespace VectorConverter {

        class CDRConverter : public IVectorFormatConverter {
        public:
            CDRConverter() = default;
            ~CDRConverter() override = default;

            VectorFormat GetFormat() const override { return VectorFormat::CDR; }
            std::string GetFormatName() const override { return "CorelDRAW"; }
            std::string GetFormatVersion() const override { return "7"; }
            std::vector<std::string> GetFileExtensions() const override {
                return {".cdr"};
            }
            std::string GetMimeType() const override {
                return "application/vnd.corel-draw";
            }

            FormatCapabilities GetCapabilities() const override;
            bool CanImport() const override { return false; }
            bool CanExport() const override { return true; }

            // Import is not supported (see the header comment); these return
            // null after reporting through options.WarningCallback.
            std::shared_ptr<VectorStorage::VectorDocument> Import(
                    const std::string& filename,
                    const ConversionOptions& options = ConversionOptions()) override;
            std::shared_ptr<VectorStorage::VectorDocument> ImportFromString(
                    const std::string& data,
                    const ConversionOptions& options = ConversionOptions()) override;
            std::shared_ptr<VectorStorage::VectorDocument> ImportFromStream(
                    std::istream& stream,
                    const ConversionOptions& options = ConversionOptions()) override;

            bool Export(
                    const VectorStorage::VectorDocument& document,
                    const std::string& filename,
                    const ConversionOptions& options = ConversionOptions()) override;
            std::string ExportToString(
                    const VectorStorage::VectorDocument& document,
                    const ConversionOptions& options = ConversionOptions()) override;
            bool ExportToStream(
                    const VectorStorage::VectorDocument& document,
                    std::ostream& stream,
                    const ConversionOptions& options = ConversionOptions()) override;

            bool ValidateFile(const std::string& filename) const override;
            bool ValidateData(const std::string& data) const override;
        };

    } // namespace VectorConverter
} // namespace UltraCanvas
