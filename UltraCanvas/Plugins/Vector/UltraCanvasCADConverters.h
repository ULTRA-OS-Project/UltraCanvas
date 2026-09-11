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
//   DWGConverter - AutoCAD Drawing. Reading is native (UltraCanvasDWGDecoder):
//                  the R13-R2018 binary drawing database - bit-coded objects,
//                  the R2004+ compressed page layout, the R2007 Reed-Solomon
//                  pages, object map, CLASSES, block definitions - is decoded
//                  and rendered as DXF for the DXF reader, so .dwg files open
//                  and preview without any external program. Writing
//                  delegates to GNU LibreDWG's dxf2dwg when it is installed
//                  (ULTRACANVAS_DXF2DWG or PATH): DWG has no public
//                  specification and the only open implementation is GPL,
//                  so it stays an optional external process; dwg2dxf is
//                  likewise only a fallback for files the native decoder
//                  declines. Without the tool the export warns and fails
//                  cleanly - DXF is AutoCAD's own exchange format and opens
//                  everywhere DWG does.
//
// DXF has no alpha channel that pre-2011 consumers honour, so style opacity
// is reported through the warning callback and colours are written at full
// strength.
// Version: 1.2.0
// Last Modified: 2026-09-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasMetafileConverters.h"   // ExportOnlyConverter
#include "DataFormats/UltraCanvasCADPalette.h"   // AciPaletteColor

namespace UltraCanvas {
    namespace VectorConverter {

        // The AutoCAD Color Index palette. It now lives in core
        // (DataFormats/UltraCanvasCADPalette.h) because the 3D model
        // converters resolve ACI too and neither plugin owns it; this name
        // stays so the vector converters read unchanged.
        inline Color AciPaletteColor(int aci) { return UltraCanvas::AciPaletteColor(aci); }

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

            // Native DWG -> DXF conversion: the tagged DXF text the decoder
            // produces for a DWG file image, empty (with a warning) when the
            // file cannot be decoded. This is what ImportFromString feeds
            // to the DXF reader; it is also a DWG-to-DXF converter in its
            // own right.
            static std::string DecodeToDxf(const std::string& data,
                                           const ConversionOptions& options = ConversionOptions());

            // The LibreDWG executables: the ULTRACANVAS_DXF2DWG /
            // ULTRACANVAS_DWG2DXF environment variables when set, otherwise
            // "dxf2dwg" / "dwg2dxf" found on PATH. Empty when neither
            // exists. dxf2dwg is what the export needs; dwg2dxf is only a
            // fallback for files the native decoder declines.
            static std::string FindDxf2Dwg();
            static std::string FindDwg2Dxf();
        };

    } // namespace VectorConverter
} // namespace UltraCanvas
