// UltraCanvas/Plugins/Vector/UltraCanvasEPSConverter.h
// EPS (Encapsulated PostScript) writer for the Vector plugin: serializes a
// VectorStorage::VectorDocument as an EPSF-3.0 PostScript program.
//
// Write-only. Reading EPS means interpreting a PostScript program, which the
// EPS plugin (Plugins/Vector/EPS/UltraCanvasEPSPlugin.h) does against a
// render context; it does not produce a VectorDocument, so CanImport() is
// false here and the Import methods return null.
//
// The emitted program restricts itself to Level-1/2 operators the EPS
// plugin's interpreter (and ghostscript) handle: path construction, fill and
// stroke with flat RGB colour, dash patterns, and text via findfont/show.
// PostScript has no transparency and the plain operator set has no
// gradients, so opacity is flattened toward white and gradients fall back to
// the blend of their end stops (each with a warning).
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasVectorConverter.h"

namespace UltraCanvas {
    namespace VectorConverter {

        class EPSConverter : public IVectorFormatConverter {
        public:
            EPSConverter() = default;
            ~EPSConverter() override = default;

            VectorFormat GetFormat() const override { return VectorFormat::EPS; }
            std::string GetFormatName() const override { return "Encapsulated PostScript"; }
            std::string GetFormatVersion() const override { return "3.0"; }
            std::vector<std::string> GetFileExtensions() const override {
                return {".eps", ".epsf", ".ps"};
            }
            std::string GetMimeType() const override { return "application/postscript"; }

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
