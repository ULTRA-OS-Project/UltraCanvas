// UltraCanvas/Plugins/Vector/UltraCanvasMetafileConverters.h
// Writers for the Windows metafile formats and Adobe Illustrator:
//   EMFConverter - Enhanced Metafile ([MS-EMF]), 32-bit records with real
//                  bezier support, GDI path fills, ExtTextOutW text.
//   WMFConverter - legacy Windows Metafile ([MS-WMF]), 16-bit records with
//                  a placeable (Aldus) header; WMF has no bezier record, so
//                  curves flatten to polylines.
//   AIConverter  - Adobe Illustrator. Modern .ai files are PDF-based (a PDF
//                  with Illustrator's private data attached), so this writer
//                  produces the Vector plugin's PDF output under the .ai
//                  extension - valid for Illustrator and every PDF consumer.
//
// EMF and WMF read back through the record parsers in
// UltraCanvasEMFReader.cpp / UltraCanvasWMFReader.cpp; AI stays
// export-only (PDF-based .ai files are the MuPDF PDF plugin's to read).
// GDI metafiles have no alpha channel, so opacity flattens toward the
// white page with a warning on export (as in the EPS writer).
// Version: 1.1.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasVectorConverter.h"

namespace UltraCanvas {
    namespace VectorConverter {

        // Common scaffolding for the writer-first converters declared below
        // and in UltraCanvasCADConverters.h: file/stream plumbing around
        // ExportToString on the export side, and file/stream imports that
        // funnel into ImportFromString on the import side — so a converter
        // that has a reader (DXF, DWG, EMF, WMF) overrides only
        // ImportFromString and CanImport, while pure writers (AI) inherit
        // the warn-and-null default.
        class ExportOnlyConverter : public IVectorFormatConverter {
        public:
            bool CanImport() const override { return false; }
            bool CanExport() const override { return true; }

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
            bool ExportToStream(
                    const VectorStorage::VectorDocument& document,
                    std::ostream& stream,
                    const ConversionOptions& options = ConversionOptions()) override;
        };

        class EMFConverter : public ExportOnlyConverter {
        public:
            VectorFormat GetFormat() const override { return VectorFormat::EMF; }
            std::string GetFormatName() const override { return "Enhanced Metafile"; }
            std::string GetFormatVersion() const override { return "1.0"; }
            std::vector<std::string> GetFileExtensions() const override { return {".emf"}; }
            std::string GetMimeType() const override { return "image/emf"; }
            FormatCapabilities GetCapabilities() const override;

            bool CanImport() const override { return true; }
            std::shared_ptr<VectorStorage::VectorDocument> ImportFromString(
                    const std::string& data,
                    const ConversionOptions& options = ConversionOptions()) override;

            std::string ExportToString(
                    const VectorStorage::VectorDocument& document,
                    const ConversionOptions& options = ConversionOptions()) override;
            bool ValidateFile(const std::string& filename) const override;
            bool ValidateData(const std::string& data) const override;
        };

        class WMFConverter : public ExportOnlyConverter {
        public:
            VectorFormat GetFormat() const override { return VectorFormat::WMF; }
            std::string GetFormatName() const override { return "Windows Metafile"; }
            std::string GetFormatVersion() const override { return "3.0"; }
            std::vector<std::string> GetFileExtensions() const override { return {".wmf"}; }
            std::string GetMimeType() const override { return "image/wmf"; }
            FormatCapabilities GetCapabilities() const override;

            bool CanImport() const override { return true; }
            std::shared_ptr<VectorStorage::VectorDocument> ImportFromString(
                    const std::string& data,
                    const ConversionOptions& options = ConversionOptions()) override;

            std::string ExportToString(
                    const VectorStorage::VectorDocument& document,
                    const ConversionOptions& options = ConversionOptions()) override;
            bool ValidateFile(const std::string& filename) const override;
            bool ValidateData(const std::string& data) const override;
        };

        class AIConverter : public ExportOnlyConverter {
        public:
            VectorFormat GetFormat() const override { return VectorFormat::AI; }
            std::string GetFormatName() const override { return "Adobe Illustrator"; }
            std::string GetFormatVersion() const override { return "PDF-based"; }
            std::vector<std::string> GetFileExtensions() const override { return {".ai"}; }
            std::string GetMimeType() const override {
                return "application/postscript";   // the registered .ai type
            }
            FormatCapabilities GetCapabilities() const override;

            std::string ExportToString(
                    const VectorStorage::VectorDocument& document,
                    const ConversionOptions& options = ConversionOptions()) override;
            bool ValidateFile(const std::string& filename) const override;
            bool ValidateData(const std::string& data) const override;
        };

    } // namespace VectorConverter
} // namespace UltraCanvas
