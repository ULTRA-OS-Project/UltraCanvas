// UltraCanvas/Plugins/Vector/UltraCanvasODGConverter.h
// OpenDocument Drawing (.odg / .otg / .fodg) reader and writer for the
// Vector plugin - the native format of LibreOffice Draw (and of Apache
// OpenOffice Draw before it), read into and written from the editable
// VectorStorage::VectorDocument like SVG and XAR.
//
// Reading takes the ZIP package (.odg, and the .otg template, which is the
// same package under another media type) or the single-file flat XML form
// (.fodg), and turns the first page into the model:
//  - shapes: draw:rect (corner radius), draw:ellipse / draw:circle (full,
//    section, cut and arc), draw:line, draw:polyline, draw:polygon,
//    draw:path (svg:d through the path's own viewBox), draw:regular-polygon
//    (stars too), draw:connector, draw:measure and draw:caption (as their
//    line or frame), draw:frame with a draw:image (from Pictures/ or inline
//    office:binary-data) or a draw:text-box, draw:g groups, and
//    draw:custom-shape - LibreOffice's basic shapes, stars, arrows,
//    flowchart and callout shapes - by evaluating its draw:enhanced-geometry
//    (the enhanced path's every command, the equations and their functions,
//    the modifiers, stretch points and mirroring);
//  - draw:transform (rotate / translate / scale / skewX / matrix, in
//    ODF's left-to-right order and LibreOffice's rotation direction);
//  - styles through their whole chain (the default graphic style, the
//    parent styles, the automatic ones): solid fills, gradients (linear,
//    axial, radial, ellipsoid, square and rectangular, with the border,
//    intensities and LibreOffice's multi-colour stops), hatches (drawn as
//    clipped lines), bitmap fills (the picture clipped to the shape),
//    fill opacity and opacity gradients, stroke colour / width / opacity /
//    dashes / joins / caps, arrowheads (draw:marker-start / -end mapped to
//    the model's gallery), shadows;
//  - text in shapes and text boxes: paragraphs, spans with their own font,
//    size, weight, slant, colour, underline and strike-through, line
//    breaks, the text area's padding and its horizontal and vertical
//    alignment, wrapped to the frame where the frame does not grow;
//  - layers (draw:layer-set; a layer's visibility and lock), master-page
//    objects beneath the page's own, the page size and background, and the
//    title / description / author from meta.xml.
// Other pages, 3D scenes, OLE objects without a replacement picture,
// controls and form fields are reported through the warning callback.
//
// Writing produces a package LibreOffice opens as it was drawn: the page
// sized to the document, one draw:layer per model layer, every group's and
// element's transform folded into the geometry (a rotated text or picture
// keeps a draw:transform), shapes as draw:rect / draw:ellipse / draw:line /
// draw:polyline / draw:polygon / draw:path, text as text boxes, pictures
// embedded under Pictures/, gradients, dashes and arrowheads as named
// styles. A .fodg path is written as flat XML with the pictures inline.
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasVectorConverter.h"

namespace UltraCanvas {
    namespace VectorConverter {

        class ODGConverter : public IVectorFormatConverter {
        public:
            ODGConverter() = default;
            ~ODGConverter() override = default;

            VectorFormat GetFormat() const override { return VectorFormat::ODG; }
            std::string GetFormatName() const override { return "OpenDocument Drawing"; }
            std::string GetFormatVersion() const override { return "1.3"; }
            std::vector<std::string> GetFileExtensions() const override {
                return {".odg", ".otg", ".fodg"};
            }
            std::string GetMimeType() const override {
                return "application/vnd.oasis.opendocument.graphics";
            }

            FormatCapabilities GetCapabilities() const override;
            bool CanImport() const override { return true; }
            bool CanExport() const override { return true; }

            // A path ending in .fodg is read and written as flat XML; any
            // other is the ZIP package (.otg with the template media type).
            // ImportFromString / ImportFromStream tell the two apart by
            // their bytes.
            std::shared_ptr<VectorStorage::VectorDocument> Import(
                    const std::string& filename,
                    const ConversionOptions& options = ConversionOptions()) override;
            std::shared_ptr<VectorStorage::VectorDocument> ImportFromString(
                    const std::string& data,
                    const ConversionOptions& options = ConversionOptions()) override;
            std::shared_ptr<VectorStorage::VectorDocument> ImportFromStream(
                    std::istream& stream,
                    const ConversionOptions& options = ConversionOptions()) override;

            // ExportToString / ExportToStream produce the .odg package's
            // bytes; ExportFlatXml the .fodg text.
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
            std::string ExportFlatXml(
                    const VectorStorage::VectorDocument& document,
                    const ConversionOptions& options = ConversionOptions());

            bool ValidateFile(const std::string& filename) const override;
            bool ValidateData(const std::string& data) const override;
        };

        // The reader and writer behind the converter, in their own
        // translation units (UltraCanvasODGReader.cpp; the writer is in UltraCanvasODGConverter.cpp).
        // `data` is a package's bytes or a flat XML document; `asPackage`
        // picks the writer's output form, `templateType` the .otg media type.
        std::shared_ptr<VectorStorage::VectorDocument> ReadODGDocument(
                const std::string& data, const ConversionOptions& options);
        std::string WriteODGDocument(const VectorStorage::VectorDocument& document,
                                     const ConversionOptions& options,
                                     bool asPackage, bool templateType);

    } // namespace VectorConverter
} // namespace UltraCanvas
