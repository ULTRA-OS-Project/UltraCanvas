// UltraCanvas/Plugins/Vector/UltraCanvasCADConverters.h
// CAD format converters (read and write):
//   DXFConverter - AutoCAD Drawing Exchange Format, R2000 (AC1015) tagged
//                  ASCII per Autodesk's public DXF reference. Writing: layers
//                  map to real DXF layers, fills become solid HATCH entities
//                  whose boundaries carry exact bezier spline edges, strokes
//                  become LWPOLYLINE/SPLINE entities with lineweights and
//                  linetypes, text becomes TEXT entities. Reading
//                  (UltraCanvasDXFReader.cpp): LINE/CIRCLE/ARC/ELLIPSE/
//                  LWPOLYLINE (with bulges)/POLYLINE/SPLINE/HATCH/SOLID/
//                  TEXT/MTEXT with the LAYER/LTYPE/STYLE tables, ACI and
//                  true colours, lineweights and dash linetypes.
//   DWGConverter - AutoCAD Drawing. DWG is a proprietary binary format with
//                  no public specification; the only open-source
//                  implementation is GNU LibreDWG. This converter delegates
//                  both directions to LibreDWG's command-line tools when
//                  they are available (on PATH or named by environment
//                  variable): writing goes through the DXF writer plus
//                  dxf2dwg (ULTRACANVAS_DXF2DWG), reading through dwg2dxf
//                  (ULTRACANVAS_DWG2DXF) plus the DXF reader. Without the
//                  tools it warns and fails cleanly - DXF is AutoCAD's own
//                  exchange format and opens everywhere DWG does.
//
// DXF has no alpha channel that pre-2011 consumers honour, so style opacity
// is reported through the warning callback and colours are written at full
// strength.
// Version: 1.1.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasMetafileConverters.h"   // ExportOnlyConverter

namespace UltraCanvas {
    namespace VectorConverter {

        // The AutoCAD Color Index palette (defined in UltraCanvasDXFReader.cpp):
        // exact classic colours 1-9 and greys 250-255, the standard 24-hue
        // construction for 10-249. Shared between the writer's nearest-ACI
        // fallback and the reader's ACI resolution so colours round-trip
        // consistently through ACI-only consumers (LibreDWG among them).
        Color AciPaletteColor(int aci);

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

        class DWGConverter : public ExportOnlyConverter {
        public:
            VectorFormat GetFormat() const override { return VectorFormat::DWG; }
            std::string GetFormatName() const override { return "AutoCAD Drawing"; }
            std::string GetFormatVersion() const override { return "R2000"; }
            std::vector<std::string> GetFileExtensions() const override { return {".dwg"}; }
            std::string GetMimeType() const override { return "image/vnd.dwg"; }
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

            // The LibreDWG executables used for the conversions: the
            // ULTRACANVAS_DXF2DWG / ULTRACANVAS_DWG2DXF environment
            // variables when set, otherwise "dxf2dwg" / "dwg2dxf" found on
            // PATH. Empty when neither exists.
            static std::string FindDxf2Dwg();
            static std::string FindDwg2Dxf();
        };

    } // namespace VectorConverter
} // namespace UltraCanvas
