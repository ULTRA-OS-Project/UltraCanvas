// Apps/DemoApp/UltraCanvasDWGExamples.cpp
// AutoCAD DWG and DXF drawings demo - the Vector plugin's native CAD import.
// Each sample in media/vector/DWG is decoded by the native DWG reader
// (UltraCanvasDWGDecoder) and each sample in media/vector/DXF is read
// directly; both are built into a VectorStorage::VectorDocument by the DXF
// reader and shown in an UltraCanvasVectorElement; a click opens the drawing
// in the demo's media viewer window. The status line reports what the reader
// found and any entity types it skipped.
// Version: 1.2.0 - the shared page look (UltraCanvasDemoPageKit): the tiles in a card
//                  that wraps, tile captions and the status line sized to their text,
//                  the info and how-it-works panels as cards
// Version: 1.1.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasVectorFormatsPlugin.h"
#include "UltraCanvasVectorElement.h"
#include "UltraCanvasCADConverters.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

    // Decodes a CAD file through the converter matrix, collecting the
    // reader's warnings so the page can show them.
    struct CadLoadResult {
        std::shared_ptr<VectorStorage::VectorDocument> document;
        std::vector<std::string> warnings;
        double milliseconds = 0;
    };

    CadLoadResult LoadCadDocument(const std::string& path) {
        CadLoadResult result;
        auto converter = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(path);
        if (!converter || !converter->CanImport()) {
            result.warnings.push_back("no reader for " + path);
            return result;
        }
        VectorConverter::ConversionOptions options;
        options.WarningCallback = [&result](const std::string& w) { result.warnings.push_back(w); };
        auto start = std::chrono::steady_clock::now();
        result.document = converter->Import(path, options);
        result.milliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        return result;
    }

    size_t CountDrawables(const VectorStorage::VectorElement& e) {
        if (const auto* g = dynamic_cast<const VectorStorage::VectorGroup*>(&e)) {
            size_t n = 0;
            for (const auto& child : g->Children) if (child) n += CountDrawables(*child);
            return n;
        }
        return 1;
    }

    std::string DescribeDocument(const VectorStorage::VectorDocument& doc, double ms) {
        size_t drawables = 0;
        for (const auto& layer : doc.Layers) if (layer) drawables += CountDrawables(*layer);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%zu layers, %zu drawables, page %.0f x %.0f pt, decoded in %.0f ms",
                      doc.Layers.size(), drawables, doc.Size.width, doc.Size.height, ms);
        return buf;
    }

}   // namespace

// ===== DWG VECTOR EXAMPLES IMPLEMENTATION =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDWGVectorExamples() {
        auto page = DemoKit::CreatePage("DWGExamples",
                "AutoCAD DWG and DXF Drawings - Click to Open in the Media Viewer",
                "Native DWG decoding (R13 to R2018) with no external tools, and the DXF reader it shares: blocks, "
                "layers, hatches, splines, dimensions and 3D meshes projected to plan view. Click a drawing to open it in the media viewer.");

        auto samplesCard = DemoKit::AddCard(page, "DWGSamples");
        auto tilesRow = DemoKit::AddRow(samplesCard, "DWGTiles", 20);

        // The status line under the tiles; it grows with the reader's warnings.
        auto statusLabel = DemoKit::CreateText("DWGStatus", "Ready. Click a drawing to open it in the media viewer.");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));
        statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
        statusLabel->SetPadding(6);

        // One tile per sample. The flat drawings live in media/vector/; the one
        // sample that carries 3D geometry lives with the models in media/3D/
        // and is read here anyway, because projecting polyface meshes to plan
        // view is part of what a CAD reader has to do - `root` says which.
        auto makeTile = [&](const std::string& id, const std::string& root,
                            const std::string& folder, const std::string& fileName,
                            const std::string& caption) {
            // As wide as the drawing it frames (280 + padding + border); as tall
            // as its caption needs.
            auto tile = DemoKit::CreateColumn(id, 4);
            tile->SetBackgroundColor(Colors::White);
            tile->SetBorders(2, Color(180, 180, 180, 255));
            tile->SetPadding(10);

            std::string path = NormalizePath(GetResourcesDir() + root + folder + "/" + fileName);
            CadLoadResult loaded = LoadCadDocument(path);

            auto element = CreateVectorElement(id + "El", 0, 0, 280, 190);
            VectorElementOptions opts = element->GetOptions();
            opts.BackgroundColor = Colors::White;
            element->SetOptions(opts);

            std::string summary;
            if (loaded.document) {
                element->SetDocument(loaded.document);
                summary = fileName + ": " + DescribeDocument(*loaded.document, loaded.milliseconds);
            } else {
                summary = "Failed to load " + path;
            }
            for (const auto& w : loaded.warnings) summary += "\n" + w;

            element->SetMouseCursor(UCMouseCursor::Hand);
            element->SetEventCallback([tile = tile.get(), statusLabel, summary, path](const UCEvent& event) {
                switch (event.type) {
                    case UCEventType::MouseUp:
                        ShowInMediaViewer(path);
                        return true;
                    case UCEventType::MouseEnter:
                        tile->SetBordersColor(Color(0, 122, 204, 255));
                        statusLabel->SetText(summary);
                        return true;
                    case UCEventType::MouseLeave:
                        tile->SetBordersColor(Color(180, 180, 180, 255));
                        return true;
                    default:
                        return false;
                }
            });

            DemoKit::AddFixed(tile, element);

            auto label = DemoKit::CreateText(id + "Label", caption);
            label->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);
            label->SetFontSize(11);
            DemoKit::Add(tile, label);

            DemoKit::Add(tilesRow, tile);
            tile->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(304));
        };

        makeTile("DWGContainer1", "media/vector/", "DWG", "Audi-Q5-DWGFree.com_.dwg",
                 "DWG: Audi Q5 (R2013, 2D block)");
        makeTile("DWGContainer2", "media/vector/", "DWG", "womans hostel.dwg",
                 "DWG: hostel plans (R2007, blocks, hatches)");
        makeTile("DWGContainer3", "media/3D/", "DWG", "bagno_3d_1.dwg",
                 "DWG: bathroom (R2013, 3D polyface meshes, projected)");
        makeTile("DXFContainer1", "media/vector/", "DXF", "millennium-falcon.dxf",
                 "DXF: Millennium Falcon (1015 LWPOLYLINEs, 507 LINEs)");
        makeTile("DXFContainer2", "media/vector/", "DXF", "women-body.dxf",
                 "DXF: figure study (74 NURBS SPLINEs)");

        DemoKit::Add(samplesCard, statusLabel);

        auto infoRow = DemoKit::AddRow(page, "DWGInfoRow");

        // ===== INFO PANEL =====
        DemoKit::AddNotes(infoRow, "InfoPanel", "Native CAD import", {
                "DWG R13, R14, 2000, 2004, 2007, 2010, 2013, 2018",
                "DXF R12 and later (read and write)",
                "Lines, arcs, circles, ellipses, splines",
                "Polylines, LW polylines, 3D and mesh polylines",
                "Hatches (solid, pattern, gradient boundaries)",
                "Text, MTEXT, attributes, leaders, dimensions",
                "Blocks and nested inserts, mirrored and arrayed",
                "Layers, true colours, lineweights, linetypes",
                "DWG to DXF conversion: DWGConverter::DecodeToDxf",
                "DWG writing needs LibreDWG's dxf2dwg (optional)"});

        // ===== HOW IT WORKS =====
        auto howCard = DemoKit::AddCard(infoRow, "HowPanel", "How a .dwg becomes a VectorDocument");
        DemoKit::AddText(howCard, "HowText",
                "1. UltraCanvasDWGDecoder decodes the binary drawing database - bit-coded values, the R2004+ compressed "
                "pages, the R2007 Reed-Solomon pages, the object map, CLASSES and the block definitions - and renders "
                "it as tagged DXF text (DWGConverter::DecodeToDxf).\n"
                "2. The DXF reader builds the VectorStorage::VectorDocument, the same path a .dxf file takes directly: "
                "blocks expand into transformed groups, layers keep their names, colours and visibility, and the page "
                "follows the drawing's real extents.");
        DemoKit::AddCaption(howCard, "HowCode", "auto doc = DWGConverter().Import(\"plan.dwg\");");

        return page;
    }

} // namespace UltraCanvas
