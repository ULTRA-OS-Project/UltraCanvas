// UltraCanvas/Plugins/Vector/UltraCanvasCADConverters.h
// CAD format writers:
//   DXFConverter - AutoCAD Drawing Exchange Format, R2000 (AC1015) tagged
//                  ASCII per Autodesk's public DXF reference. Layers map to
//                  real DXF layers, fills become solid HATCH entities whose
//                  boundaries carry exact bezier spline edges, strokes
//                  become LWPOLYLINE/SPLINE entities with lineweights and
//                  linetypes, text becomes TEXT entities.
//   DWGConverter - AutoCAD Drawing. DWG is a proprietary binary format with
//                  no public specification; the only open-source writer is
//                  GNU LibreDWG. This converter produces the DXF output and
//                  converts it with LibreDWG's dxf2dwg tool when one is
//                  available (on PATH or named by ULTRACANVAS_DXF2DWG);
//                  without the tool, Export warns and fails cleanly - the
//                  DXF the converter is built on is AutoCAD's own exchange
//                  format and opens everywhere DWG does.
//
// Both are export-only. DXF has no alpha channel that pre-2011 consumers
// honour, so style opacity is reported through the warning callback and
// colours are written at full strength.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasMetafileConverters.h"   // ExportOnlyConverter

namespace UltraCanvas {
    namespace VectorConverter {

        class DXFConverter : public ExportOnlyConverter {
        public:
            VectorFormat GetFormat() const override { return VectorFormat::DXF; }
            std::string GetFormatName() const override {
                return "AutoCAD Drawing Exchange Format";
            }
            std::string GetFormatVersion() const override { return "R2000"; }
            std::vector<std::string> GetFileExtensions() const override { return {".dxf"}; }
            std::string GetMimeType() const override { return "image/vnd.dxf"; }
            FormatCapabilities GetCapabilities() const override;

            std::string ExportToString(
                    const VectorStorage::VectorDocument& document,
                    const ConversionOptions& options = ConversionOptions()) override;
            bool ValidateFile(const std::string& filename) const override;
            bool ValidateData(const std::string& data) const override;
        };

        class DWGConverter : public ExportOnlyConverter {
        public:
            VectorFormat GetFormat() const override { return VectorFormat::DWG; }
            std::string GetFormatName() const override { return "AutoCAD Drawing"; }
            std::string GetFormatVersion() const override { return "R2000"; }
            std::vector<std::string> GetFileExtensions() const override { return {".dwg"}; }
            std::string GetMimeType() const override { return "image/vnd.dwg"; }
            FormatCapabilities GetCapabilities() const override;

            std::string ExportToString(
                    const VectorStorage::VectorDocument& document,
                    const ConversionOptions& options = ConversionOptions()) override;
            bool ValidateFile(const std::string& filename) const override;
            bool ValidateData(const std::string& data) const override;

            // The LibreDWG dxf2dwg executable used for the conversion:
            // the ULTRACANVAS_DXF2DWG environment variable when set,
            // otherwise "dxf2dwg" found on PATH. Empty when neither exists.
            static std::string FindDxf2Dwg();
        };

    } // namespace VectorConverter
} // namespace UltraCanvas
