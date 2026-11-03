// Apps/DemoApp/UltraCanvasSVGExamples.cpp
// Demo examples implementation for UltraCanvas Framework components
// Version: 1.5.0 - the shared page look (UltraCanvasDemoPageKit): the tiles in a
//                  card and a row that wraps, every text sized to fit
// Version: 1.4.1
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"
#include <iostream>
#include <memory>
#include <functional>

namespace UltraCanvas {

// ===== VECTOR/SVG EXAMPLES IMPLEMENTATION =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSVGVectorExamples() {
        auto container = DemoKit::CreatePage("VectorExamples",
                "SVG Graphics Demo - Click to Open in the Media Viewer",
                "Every .svg sample shipped under media/vector/SVG. Click a drawing to open it in the media viewer. "
                "The arrow keys step through the folder there; ESC closes it.");

        // Status label for feedback - made first because a tile reports a
        // failed load in it; it goes under the features.
        auto statusLabel = DemoKit::CreateText("SVGStatus",
                "Ready. Click on an SVG file to open it in the media viewer.");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));
        statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
        statusLabel->SetPadding(4.0f);

        auto tilesCard = DemoKit::AddCard(container, "SVGTilesCard");
        auto tilesRow = DemoKit::AddRow(tilesCard, "SVGTilesRow", 10);

        // One tile per sample drawing in media/vector/SVG/: the drawing at its
        // size, the file name under it as wide as the tile allows.
        auto makeTile = [&](const std::string& id, const std::string& fileName) {
            auto tile = DemoKit::CreateColumn(id, 8);
            tile->SetPadding(14.0f);
            tile->SetBackgroundColor(Color(250, 250, 250, 255));
            tile->SetBorders(2, Color(180, 180, 180, 255));
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(240);
            tile->boxConstraints = limits;

            auto element = std::make_shared<UltraCanvasImageElement>(id + "El", 0, 0, 200, 175);
            std::string path = NormalizePath(GetResourcesDir() + "media/vector/SVG/" + fileName);
            if (!element->LoadFromFile(path)) {
                statusLabel->SetText("Failed to load " + path);
            }
            DemoKit::AddFixed(tile, element);
            element->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);

            auto label = DemoKit::AddText(tile, id + "Label", fileName);
            label->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);
            label->SetFontSize(11);

            element->SetMouseCursor(UCMouseCursor::Hand);
            element->SetEventCallback([tile = tile.get(), statusLabel, path](const UCEvent& event) {
                switch (event.type) {
                    case UCEventType::MouseUp:
                        ShowInMediaViewer(path);
                        statusLabel->SetText("Opened in the media viewer: " + path);
                        return true;
                    case UCEventType::MouseEnter:
                        tile->SetBordersColor(Color(100, 149, 237, 255));
                        return true;
                    case UCEventType::MouseLeave:
                        tile->SetBordersColor(Color(180, 180, 180, 255));
                        return true;
                    default:
                        return false;
                }
            });

            DemoKit::Add(tilesRow, tile);
        };

        // Every .svg file shipped under media/vector/SVG; the row wraps.
        makeTile("SVGTile1", "filer-palette-rev3.svg");
        makeTile("SVGTile2", "demo1.svg");
        makeTile("SVGTile3", "demo2.svg");
        makeTile("SVGTile4", "svg-test.svg");
        makeTile("SVGTile5", "robot.svg");
        makeTile("SVGTile6", "astronaut.svg");
        makeTile("SVGTile7", "photo-camera.svg");
        makeTile("SVGTile8", "Logo_Texter.svg");

        // Information panel
        DemoKit::AddNotes(container, "InfoPanel", "SVG Features", {
                "Load from file or string",
                "ViewBox transformation",
                "Auto-resize",
                "Basic shapes, paths and curves",
                "Text rendering",
                "Group hierarchies",
                "Style attributes",
                "Click any drawing to open it in the media viewer, ESC to close"});

        DemoKit::Add(container, statusLabel);

        return container;
    }
}
